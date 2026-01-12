
// kütüphaneler
#include <linux/module.h>   // modül olması için şart
#include <linux/kernel.h>   // temel fonksiyonlar
#include <linux/init.h>     // giriş-çıkış makroları
#include <linux/input.h>    // fare/klavye işlemleri
#include <linux/slab.h>     // hafıza yönetimi (ram)
#include <linux/fs.h>       // dosya işlemleri
#include <linux/uaccess.h>  // veri kopyalama
#include <linux/cdev.h>     // karakter cihazı yapısı
#include <linux/device.h>   // cihaz oluşturma
#include <linux/jiffies.h>  // zaman sayacı
#include <linux/mutex.h>    // kilit mekanizması

// sabit ayarlar, isimleri burdan değiştiriyorum
#define DRIVER_NAME "usb_mouse_pattern"
#define DEV_NAME    "mousepattern"
#define EVENT_BUF_SIZE 1024 // hafızada tutulacak harf sayısı
#define THRESHOLD 5         // titreme payı (hassasiyet)

// bütün verileri tutacak kutumuz (struct)
struct mouse_pattern_dev {
    char event_buf[EVENT_BUF_SIZE]; // harfleri buraya diziyoruz
    size_t head;                    // yazma sırası nerde
    size_t tail;                    // okuma sırası nerde
    
    wait_queue_head_t read_queue;   // veri bekleyenlerin sırası
    struct mutex lock;              // karışıklık olmasın kilidi
    
    int combo_state;                // şifrenin kaçıncı adımındayız
    bool right_button_pressed;      // sağ tuşa basıldı mı
    int right_click_count;          // kaç kere tıklandı
    unsigned long last_click_time;  // en son ne zaman tıklandı
    
    dev_t devt;                     // cihaz numarası
    struct cdev cdev;               // cihazın teknik yapısı
    struct class *class;            // sınıf adresi
    struct device *device;          // cihaz adresi
    
    struct input_handler handler;   // fareyi dinleyen eleman
    struct input_handle *handle;    // tutamaç
};

// ana değişken, başlangıçta boş
static struct mouse_pattern_dev *gdev = NULL;

// listeye yazı ekleme fonksiyonu
static void event_buf_push(const char *msg) {
    size_t len = strlen(msg); // yazının boyunu ölç
    size_t i;                 // döngü sayacı
    
    if (!gdev) return;        // eğer cihaz yoksa çık

    mutex_lock(&gdev->lock);  // kapıyı kilitle, başkası girmesin
    
    for(i=0; i<len; i++) {    // harf harf dönüyoruz
        gdev->event_buf[gdev->head] = msg[i]; // harfi sıraya koy
        
        // sırayı bir kaydır, sona gelirse başa dön (mod alma)
        gdev->head = (gdev->head + 1) % EVENT_BUF_SIZE;
        
        // eğer baş kuyruğa çarparsa kuyruğu itele
        if(gdev->head == gdev->tail)
            gdev->tail = (gdev->tail + 1) % EVENT_BUF_SIZE;
    }
    
    mutex_unlock(&gdev->lock); // işim bitti kilidi aç
    
    // bekleyen program varsa uyandır
    wake_up_interruptible(&gdev->read_queue);
}

// okuma yapıldığında çalışacak fonksiyon (cat komutu gibi)
static ssize_t my_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
    size_t copied = 0; // kaç tane kopyaladık sayacı
    char c;            // o anki harf
    
    if (!gdev) return -EFAULT; // hata kontrolü

    // veri yoksa (baş == kuyruk) programı uyut
    if(wait_event_interruptible(gdev->read_queue, (gdev->head != gdev->tail)))
        return -ERESTARTSYS; // uyku bozulursa çık
        
    mutex_lock(&gdev->lock); // okurken de kilitliyoruz
    
    // istenen sayı kadar veya veri bitene kadar dön
    while(copied < count && gdev->head != gdev->tail) {
        c = gdev->event_buf[gdev->tail]; // harfi al
        gdev->tail = (gdev->tail + 1) % EVENT_BUF_SIZE; // sırayı kaydır
        
        mutex_unlock(&gdev->lock); // kopyalarken kilidi açmak lazım
        
        // kernelden kullanıcıya harfi gönder
        if(put_user(c, buf+copied)) return -EFAULT;
        
        mutex_lock(&gdev->lock); // tekrar kilitle
        copied++; // sayacı arttır
    }
    
    mutex_unlock(&gdev->lock); // en son kilidi aç
    return copied; // kaç harf okunduğunu söyle
}

// dosya açılınca (bir şey yapmıyor ama lazım)
static int my_open(struct inode *inode, struct file *file) { return 0; }

// işlemleri fonksiyonlara bağlıyoruz
static const struct file_operations my_fops = { 
    .owner = THIS_MODULE, 
    .read = my_read, 
    .open = my_open 
};

// --- farenin hareketlerini burda yakalıyoruz ---
static void mouse_event(struct input_handle *handle, unsigned int type, unsigned int code, int value) {
    unsigned long now; // zamanı tutmak için
    if (!gdev) return; // boşsa işlem yapma

    // tuşa basılma olayı ve sağ tık mı?
    if (type == EV_KEY && code == BTN_RIGHT) {
        if (value == 1) { // 1 demek basıldı demek
            gdev->right_button_pressed = true; // basılı olduğunu not et
            now = jiffies; // şu anki zamanı al
            
            // 400ms içinde mi bastı kontrol et (hızlı tıklama)
            if (time_before(now, gdev->last_click_time + msecs_to_jiffies(400))) {
                gdev->right_click_count++; // sayacı arttır
            } else {
                gdev->right_click_count = 1; // değilse 1'den başlat
            }
            gdev->last_click_time = now; // son zamanı güncelle
            
            // tıklama sayısına göre mesaj at
            if(gdev->right_click_count == 1) event_buf_push("TIK_1\n");
            if(gdev->right_click_count == 2) event_buf_push("TIK_2\n");
            if(gdev->right_click_count == 3) {
                event_buf_push("STOP_MUSIC\n"); // 3 kere bastı durdur
                gdev->right_click_count = 0;    // sayacı sıfırla
            }
        } 
        else if (value == 0) { // 0 demek bırakıldı demek
            gdev->right_button_pressed = false; // basılı değil artık
            
            // eğer hareket yarım kaldıysa reset yaz
            if(gdev->combo_state != 0) event_buf_push("RESET\n");
            gdev->combo_state = 0; // durumları sıfırla
        }
    }
    
    // fare hareketi olayı ve sağ tık basılıysa
    if (type == EV_REL && gdev->right_button_pressed) {
        
        // code 0 = yatay (sağ-sol) hareketi
        if (gdev->combo_state == 0 && code == 0 && value > THRESHOLD) {
            gdev->combo_state = 1; // 1. adımı geçtik
            event_buf_push("ADIM_SAG\n");
        }
        // sola gitme (negatif değer)
        else if (gdev->combo_state == 1 && code == 0 && value < -THRESHOLD) {
            gdev->combo_state = 2; // 2. adımı geçtik
            event_buf_push("ADIM_SOL\n");
        }
        
        // code 1 = dikey (yukarı-aşağı) hareketi
        // not: yukarı gitmek eksi (-) değer veriyor!
        else if (gdev->combo_state == 2 && code == 1 && value < -THRESHOLD) {
            gdev->combo_state = 3; // 3. adımı geçtik
            event_buf_push("ADIM_YUKARI\n");
        }
        // aşağı gitmek artı (+) değer
        else if (gdev->combo_state == 3 && code == 1 && value > THRESHOLD) {
            gdev->combo_state = 0; // bitti başa dön
            event_buf_push("START_MUSIC\n");
        }
    }
}

// fare takıldığında çalışan fonksiyon
static int mouse_connect(struct input_handler *handler, struct input_dev *dev, const struct input_device_id *id) {
    struct input_handle *handle; // bağlantı kolu
    int error;

    // sol tıkı (btn_left) yoksa bu fare değildir
    if (!test_bit(BTN_LEFT, dev->keybit)) return -ENODEV;

    // ram'den yer ayır (sıfırlayarak)
    handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
    if (!handle) return -ENOMEM; // yer yoksa hata dön

    // bağlantı ayarlarını yap
    handle->dev = dev;
    handle->handler = handler;
    handle->name = "mouse_pattern_handle";

    // sistemi haberdar et
    error = input_register_handle(handle);
    if (error) { kfree(handle); return error; } // hata varsa temizle

    // cihazı aç
    error = input_open_device(handle);
    if (error) { 
        input_unregister_handle(handle); // kaydı sil
        kfree(handle); // hafızayı sil
        return error; 
    }
    
    // global değişkene ata
    if (gdev) gdev->handle = handle;
    
    // loga yaz
    printk(KERN_INFO "usb fare baglandi: %s\n", dev->name);
    return 0; // her şey yolunda
}

// fare sökülünce
static void mouse_disconnect(struct input_handle *handle) {
    input_close_device(handle);      // kapat
    input_unregister_handle(handle); // kaydı sil
    kfree(handle);                   // hafızayı geri ver
    
    if (gdev) gdev->handle = NULL;   // pointerı boşa çıkar
    printk(KERN_INFO "usb fare ayrildi\n");
}

// hangi cihazları kabul edicez
static const struct input_device_id mouse_ids[] = {
    {
        // hem tuş hem hareket özelliği olanlara bak
        .flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_KEYBIT | INPUT_DEVICE_ID_MATCH_RELBIT,
        .evbit = { BIT_MASK(EV_KEY) | BIT_MASK(EV_REL) },       // olay tipleri
        .keybit = { [BIT_WORD(BTN_LEFT)] = BIT_MASK(BTN_LEFT) },// sol tık şart
        .relbit = { BIT_MASK(REL_X) | BIT_MASK(REL_Y) },        // hareket şart
    },
    { }, // liste sonu işareti
};
MODULE_DEVICE_TABLE(input, mouse_ids); // tabloyu kaydet

// driver özelliklerini paketliyoruz
static struct input_handler pattern_handler = {
    .event = mouse_event,
    .connect = mouse_connect,
    .disconnect = mouse_disconnect,
    .name = "mouse_pattern_handler",
    .id_table = mouse_ids,
};

// driver yüklenince (insmod yapınca) ilk çalışan yer burası
static int __init my_init(void) {
    int ret; // hata kodlarını tutmak için değişken
    
    // struct için ram'den yer ayırıyoruz
    // kzalloc: hem yer ayırır hem içini sıfırlar (tertemiz yapar)
    gdev = kzalloc(sizeof(struct mouse_pattern_dev), GFP_KERNEL);
    if (!gdev) return -ENOMEM; // eğer ram doluyse hata ver ve çık
    
    // kilitleri ve bekleme kuyruğunu hazırla
    // bu fonksiyonlar pointer istiyor o yüzden & koyduk
    mutex_init(&gdev->lock);            // kilit mekanizmasını kur
    init_waitqueue_head(&gdev->read_queue); // bekleme sırasını kur
    
    // cihaz numarası al (kimlik no gibi)
    // kernelden bize boş bir numara vermesini istiyoruz
    ret = alloc_chrdev_region(&gdev->devt, 0, 1, DEV_NAME);
    if (ret < 0) { 
        kfree(gdev); // numara alamazsak ayırdığımız ram'i iade et
        return ret;  // hatayı döndür
    }

    //karakter cihazını hazırla
    // bizim yazdığımız fonksiyonları (my_fops) cihaza bağlıyoruz
    cdev_init(&gdev->cdev, &my_fops);
    
    // cihazı sisteme resmen ekliyoruz
    ret = cdev_add(&gdev->cdev, gdev->devt, 1);
    if (ret < 0) { 
        // ekleyemezsek numarayı geri ver
        unregister_chrdev_region(gdev->devt, 1); 
        kfree(gdev); // ram'i geri ver
        return ret; 
    }
    
    // sınıf oluştur
    // bu işlem /sys/class altında klasör açar
    gdev->class = class_create(DEV_NAME);
    if (IS_ERR(gdev->class)) { // pointer hatası kontrolü
        cdev_del(&gdev->cdev); // cihazı sil
        unregister_chrdev_region(gdev->devt, 1); // numarayı sil
        kfree(gdev); // ram'i sil
        return PTR_ERR(gdev->class); // hatayı döndür
    }
    
    // cihaz dosyasını oluştur
    // işte burası /dev/mousepattern dosyasını yaratan yer
    gdev->device = device_create(gdev->class, NULL, gdev->devt, NULL, DEV_NAME);
    if (IS_ERR(gdev->device)) { 
        class_destroy(gdev->class); // sınıfı yık
        cdev_del(&gdev->cdev); // cihazı sil
        unregister_chrdev_region(gdev->devt, 1); // numarayı sil
        kfree(gdev); // ram'i sil
        return PTR_ERR(gdev->device); 
    }
    
    //en son fare dinleyicisini başlat
    // burası çalışınca artık fare hareketlerini görmeye başlıyoruz
    ret = input_register_handler(&pattern_handler);
    if (ret) {
        // en kötü senaryo: her şeyi kurduk ama burda patladı
        // sondan başa doğru (lifo) temizlik yapmamız lazım
        device_destroy(gdev->class, gdev->devt); // dosyayı sil
        class_destroy(gdev->class); // sınıfı sil
        cdev_del(&gdev->cdev); // cihazı sil
        unregister_chrdev_region(gdev->devt, 1); // numarayı sil
        kfree(gdev); // ram'i sil
        return ret;
    }
    
    // her şey yolunda gittiyse loga yaz
    printk(KERN_INFO "driver yuklendi, haziriz\n");
    return 0; // 0 demek başarı demek
}

// driver silinince çalışan yer
static void __exit my_exit(void) {
    if (gdev) {
        // önce dinlemeyi durdur
        input_unregister_handler(&pattern_handler);
        
        // sonra oluşturulan dosyaları sil
        if (gdev->class) {
            device_destroy(gdev->class, gdev->devt);
            class_destroy(gdev->class);
        }
        
        // cihazı ve numarayı sil
        cdev_del(&gdev->cdev);
        unregister_chrdev_region(gdev->devt, 1);
        
        // en son ram'i boşalt
        kfree(gdev);
        gdev = NULL; // boşta kalan adresi sıfırla
    }
    printk(KERN_INFO "driver silindi, baybay\n");
}

// giriş ve çıkış noktalarını belirtiyoruz
module_init(my_init);
module_exit(my_exit);
MODULE_LICENSE("GPL"); // lisans tipi
