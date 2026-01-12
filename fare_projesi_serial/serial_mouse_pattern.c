// Kernel modülü yazarken standart <stdio.h> kütüphanesi yok
// Linux çekirdeğinin (kernel) kendi fonksiyonları var
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h> // put_user: Kernel'den User'a veri atma
#include <linux/slab.h>    // kzalloc: Hafıza tahsisi (malloc'un kernel hali)
#include <linux/wait.h>    // wait_queue: Bekleme kuyrukları
#include <linux/mutex.h>   // mutex: Çakışma önleyici kilit
#include <linux/serio.h>   // serio: Seri port işlemleri
#include <linux/jiffies.h> // jiffies: Zaman sayacı
#include <linux/sched.h> 

// #define (MACRO): Kodda DRIVER_NAME gördüğün yere derleyici otomatik
// tırnak içindeki yazıyı yapıştırır. Değişken değil, sabit metindir.
#define DRIVER_NAME "serial_mouse_pattern"
#define DEV_NAME    "mousepattern"

// HEX (Onaltılık) Sayılar: 0x20 demek 00100000 (binary) demek.
// Maskeleme işleminde "sadece bu bit 1 mi?" diye bakmak için kullanılır.
#define MS_BTN_LEFT_MASK  0x20 
#define MS_BTN_RIGHT_MASK 0x10

#define EVENT_BUF_SIZE    1024
#define THRESHOLD 3 
#define CLICK_SPEED_LIMIT 500

// STRUCT (YAPI): Birden fazla değişkeni tek bir paket yapmaya yarar.
// "mousepattern_dev" adında yeni bir veri tipi oluşturuyoruz.
struct mousepattern_dev {
    dev_t devt;         // Cihaz kimlik numarası
    struct cdev cdev;   // Karakter cihazı özellikleri
    struct class *class; // Pointer (*): Class yapısının RAM'deki adresini tutar
    
    char event_buf[EVENT_BUF_SIZE]; // Dizi (Array): Mesajları saklayan kutu
    size_t head;        // size_t: İşaretsiz tamsayı (unsigned int gibi)
    size_t tail;        
    
    wait_queue_head_t read_queue;
    struct mutex lock;  
    struct serio *serio; 

    // unsigned char: 0-255 arası sayı tutar (negatif olamaz).
    // Mouse verisi byte byte geldiği için bu tip en uygunu.
    unsigned char packet[3];
    int packet_idx;

    int combo_state;    
    int right_click_count;          
    unsigned long last_click_time;  // unsigned long: Çok büyük pozitif sayı (zaman için)
    bool prev_right_state;          // bool: Sadece true/false (1/0) alır
};

// Global Pointer: Her yerden erişebilmek için gdev adında bir işaretçi tanımladık.
// Başlangıçta NULL (boş) yapıyoruz.
static struct mousepattern_dev *gdev = NULL;

// const char *msg: Değiştirilemez (constant) karakter dizisi (string) pointerı.
static void event_buf_push(struct mousepattern_dev *dev, const char *msg)
{
    size_t len = strlen(msg); // Yazının uzunluğunu bulur
    size_t i;
    
    // &dev->lock: "dev" pointer olduğu için "->" ile içine gireriz.
    // "&" işareti "adresini ver" demektir. Mutex fonksiyonu adres ister.
    mutex_lock(&dev->lock);
    
    for (i = 0; i < len; i++) {
        dev->event_buf[dev->head] = msg[i];
        
        // MODULO (%): Kalanı bulma işlemi.
        // head 1024 olunca, 1024 % 1024 = 0 olur. Başa döner.
        // Buna "Dairesel Buffer" denir.
        dev->head = (dev->head + 1) % EVENT_BUF_SIZE;
        
        if (dev->head == dev->tail)
            dev->tail = (dev->tail + 1) % EVENT_BUF_SIZE;
    }
    mutex_unlock(&dev->lock);
    wake_up_interruptible(&dev->read_queue);
}

// ssize_t: Signed Size Type. Okunan byte sayısını veya hata (-1) döner.
// __user: Bu pointer'ın kernel değil, kullanıcı alanına ait olduğunu belirtir (güvenlik notu).
static ssize_t mousepattern_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    // void pointerı kendi struct tipimize dönüştürüyoruz (cast).
    struct mousepattern_dev *dev = file->private_data;
    size_t copied = 0;
    char c;

    // wait_event: Şart sağlanana kadar (veri gelene kadar) süreci uyutur.
    if (wait_event_interruptible(dev->read_queue, (dev->head != dev->tail))) 
        return -ERESTARTSYS;

    mutex_lock(&dev->lock);
    while (copied < count && dev->head != dev->tail) {
        c = dev->event_buf[dev->tail];
        dev->tail = (dev->tail + 1) % EVENT_BUF_SIZE;
        mutex_unlock(&dev->lock);
        
        // put_user: Kernel hafızasından User hafızasına (buf) tek byte kopyalar.
        // Doğrudan "buf[i] = c" YAPILAMAZ, çünkü bellek alanları yasaklıdır.
        if (put_user(c, buf + copied)) return -EFAULT;
        
        mutex_lock(&dev->lock);
        copied++;
    }
    mutex_unlock(&dev->lock);
    return copied;
}

static int mousepattern_open(struct inode *inode, struct file *file) { 
    file->private_data = gdev; 
    return 0; 
}

// Struct içinde fonksiyon pointerları tanımlama (Callback mantığı).
// Sistem "read" çağırınca bizim "mousepattern_read" çalışsın diyoruz.
static const struct file_operations mousepattern_fops = { 
    .owner = THIS_MODULE, 
    .open = mousepattern_open, 
    .read = mousepattern_read, 
};

// INTERRUPT HANDLER: Donanım sinyali geldiğinde çalışan kod.
static irqreturn_t mousepattern_serio_interrupt(struct serio *serio, unsigned char data, unsigned int flags)
{
    struct mousepattern_dev *dev = serio_get_drvdata(serio);
    unsigned char byte0;
    signed char move_x, move_y; // signed: Negatif değer alabilir (sola/aşağı hareket)
    bool right_click_current;
    unsigned long now;

    if (!dev) return IRQ_HANDLED;
    
    // Gelen veriyi diziye ekle ve indeksi bir arttır (++)
    dev->packet[dev->packet_idx++] = data;

    // BITWISE AND (&): data & 0x40
    // 0x40 (01000000) ile VE işlemi yapıyoruz. 
    // Bu, "6. bit 1 mi?" kontrolüdür. Protokol gereği ilk byte böyle olmalı.
    if (dev->packet_idx == 1 && !(data & 0x40)) { 
        dev->packet_idx = 0; // Hatalı paket, sıfırla.
        return IRQ_HANDLED; 
    }

    if (dev->packet_idx == 3) { // 3 byte tamamlandıysa çözümle
        byte0 = dev->packet[0];
        
        // --- BIT KAYDIRMA VE BİRLEŞTİRME (EN KARIŞIK YER) ---
        // (dev->packet[0] & 0x03): İlk byte'ın son 2 bitini al (00000011).
        // << 6: Bu iki biti 6 basamak sola kaydır. (XX000000 olur).
        // | (OR): Diğer byte ile birleştir.
        // Bu işlem Microsoft Mouse Protokolü gereği parça parça gelen X verisini birleştirir.
        move_x = (signed char) (((dev->packet[0] & 0x03) << 6) | (dev->packet[1] & 0x3F));
        move_y = (signed char) (((dev->packet[0] & 0x0C) << 4) | (dev->packet[2] & 0x3F));
        
        // TERNARY OPERATOR (?:) : Kısa if-else yapısı.
        // Soru işaretinden öncesi doğruysa 'true', yanlışsa 'false' ata.
        right_click_current = (byte0 & MS_BTN_RIGHT_MASK) ? true : false;

        // --- MANTIK KISMI ---
        if (right_click_current && !dev->prev_right_state) {
            now = jiffies;
            // time_before: Zaman taşması (overflow) korumalı karşılaştırma
            if (time_before(now, dev->last_click_time + msecs_to_jiffies(CLICK_SPEED_LIMIT))) 
                dev->right_click_count++;
            else 
                dev->right_click_count = 1;
            
            dev->last_click_time = now;
            
            if (dev->right_click_count == 1) event_buf_push(dev, "TIK_1\n");
            if (dev->right_click_count == 2) event_buf_push(dev, "TIK_2\n");
            if (dev->right_click_count == 3) {
                event_buf_push(dev, "STOP_MUSIC\n");
                dev->right_click_count = 0;
            }
        }
        dev->prev_right_state = right_click_current;

        if (!right_click_current) {
            if (dev->combo_state != 0) event_buf_push(dev, "RESET\n"); 
            dev->combo_state = 0;
        } 
        else {
            // Hareket kombinasyonları (State Machine mantığı)
            if (dev->combo_state == 0 && move_x > THRESHOLD) {
                dev->combo_state = 1;
                event_buf_push(dev, "ADIM_SAG\n"); 
            }
            else if (dev->combo_state == 1 && move_x < -THRESHOLD) {
                dev->combo_state = 2;
                event_buf_push(dev, "ADIM_SOL\n"); 
            }
            else if (dev->combo_state == 2 && move_y < -THRESHOLD) { // Y ekseni ters olabilir
                dev->combo_state = 3;
                event_buf_push(dev, "ADIM_YUKARI\n"); 
            }
            else if (dev->combo_state == 3 && move_y > THRESHOLD) {
                dev->combo_state = 0;
                event_buf_push(dev, "START_MUSIC\n");
            }
        }
        dev->packet_idx = 0;
    }
    return IRQ_HANDLED;
}

static int mousepattern_serio_connect(struct serio *serio, struct serio_driver *drv)
{
    struct mousepattern_dev *dev = gdev;
    if (serio_open(serio, drv)) return -1;
    serio_set_drvdata(serio, dev); // Sürücü verisini porta kaydet
    dev->serio = serio;
    dev->packet_idx = 0;
    dev->combo_state = 0;
    dev->right_click_count = 0;
    dev->prev_right_state = false;
    dev->last_click_time = 0;
    return 0;
}
static void mousepattern_serio_disconnect(struct serio *serio) { serio_close(serio); }

// ID Table: Bu sürücünün hangi cihazları desteklediği.
// SERIO_ANY: Her türlü seri port cihazını kabul et dedik.
static struct serio_device_id mousepattern_serio_ids[] = { 
    { .type = SERIO_ANY, .proto = SERIO_ANY, .id = SERIO_ANY, .extra = SERIO_ANY }, 
    { 0 } 
};
MODULE_DEVICE_TABLE(serio, mousepattern_serio_ids);

static struct serio_driver mousepattern_serio_driver = {
    .driver = { .name = DRIVER_NAME },
    .description = "CS350 Project",
    .id_table = mousepattern_serio_ids,
    .interrupt = mousepattern_serio_interrupt,
    .connect = mousepattern_serio_connect,
    .disconnect = mousepattern_serio_disconnect,
};
// __init: Bu fonksiyon modül yüklenirken bir kez çalışır.
// İşi bitince kernel bu kodu RAM'den siler.
// Amaç bellek tasarrufudur.

static int __init mousepattern_init(void)
{
    int ret; // Fonksiyonlardan dönen hata kodlarını tutmak için

    // Cihaz yapısı için bellek ayır
    gdev = kzalloc(sizeof(struct mousepattern_dev), GFP_KERNEL);
    if (!gdev)
        return -ENOMEM; // Bellek yoksa yükleme başarısız

    // Kilit ve bekleme kuyruğunu başlat
    mutex_init(&gdev->lock);
    init_waitqueue_head(&gdev->read_queue);

    // Major / minor numarası al
    ret = alloc_chrdev_region(&gdev->devt, 0, 1, DEV_NAME);
    if (ret)
        goto err_free; // Hata varsa geri sar

    // Character device'i başlat
    cdev_init(&gdev->cdev, &mousepattern_fops);
    ret = cdev_add(&gdev->cdev, gdev->devt, 1);
    if (ret)
        goto err_unreg_chrdev;

    // /sys/class altında class oluştur
    gdev->class = class_create(DEV_NAME);
    if (IS_ERR(gdev->class)) {
        ret = PTR_ERR(gdev->class);
        goto err_cdev_del;
    }

    // /dev/mousepattern dosyasını oluştur
    if (IS_ERR(device_create(gdev->class, NULL, gdev->devt, NULL, DEV_NAME))) {
        ret = -ENOMEM;
        goto err_class_destroy;
    }

    // SERIO sürücüsünü kernel'e kaydet
    // BU FONKSİYON HATA DÖNDÜREBİLİR
    ret = serio_register_driver(&mousepattern_serio_driver);
    if (ret)
        goto err_device_destroy; // Başarısızsa her şeyi geri al

    return 0; // Her şey başarılı

err_device_destroy:
    device_destroy(gdev->class, gdev->devt); // /dev dosyasını sil
err_class_destroy:
    class_destroy(gdev->class); // Class'ı sil
err_cdev_del:
    cdev_del(&gdev->cdev); // Character device'i kaldır
err_unreg_chrdev:
    unregister_chrdev_region(gdev->devt, 1); // Major/minor'ı bırak
err_free:
    kfree(gdev); // Ayrılan belleği geri ver
    gdev = NULL;
    return ret; // Hata kodunu kernel'e bildir
}


// __exit: rmmod yapıldığında çalışır.
static void __exit mousepattern_exit(void) {
    serio_unregister_driver(&mousepattern_serio_driver);
    device_destroy(gdev->class, gdev->devt);
    class_destroy(gdev->class);
    cdev_del(&gdev->cdev);
    unregister_chrdev_region(gdev->devt, 1);
    kfree(gdev); // Ayırdığımız hafızayı geri veriyoruz.
}

module_init(mousepattern_init);
module_exit(mousepattern_exit);
MODULE_LICENSE("GPL");
