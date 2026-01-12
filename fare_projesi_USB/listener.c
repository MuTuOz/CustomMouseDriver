#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
// kütüphaneler burda. 
// stdio -> printf (ekrana yazma) için
// unistd -> read, sleep falan için lazım
// fcntl -> dosya açma (open) ayarları için
// string -> strstr (yazı arama) için
// stdlib -> system komutu için

// müziği susturan foksiyon
void stop_music() {
    printf("\nMÜZİK DURDURULUYOR...\n");
    
    // system() komutu terminale yazı yazar gibi komut çalıştırmayı sağlıyo.
    // >/dev/null 2>&1 kısmı önemli:
    // çıkan yazıları çöpe (null) atıyoruz ki ekranda hata mata görünmesin, temiz olsun.
    system("killall mpg123 >/dev/null 2>&1");
}

// müzik açan foksiyon
void play_music() {
    // önce bi susturuyoz ki üst üste binmesin sesler
    stop_music();
    
    printf("\nŞİFRE DOĞRU! MÜZİK BAŞLIYOR\n");
    
    // burda system() içine yazdığımız komut uzun.
    // sondaki '&' işareti ÇOK ÖNEMLİ. 
    // '&' koymazsak program müzik bitene kadar burda donar kalır.
    // arka planda (background) çalışsın diye onu koyduk.
    int ret = system("mpg123 '/home/ubuntu/fare_projesi_serial/Bu Bayrak - Turkish Patriotic Song (This Flag).mp3' &");
    
    // system() fonksiyonu 0 dönerse her şey yolunda demektir
    // 0 değilse bi sıkıntı var
    if (ret != 0) {
        printf("HATA: Muzik calinamadi.\n");
    }
}

int main() {
    // open() ile driver dosyamızı açıyoruz.
    // O_RDONLY -> Read Only (Sadece Okuma) modunda açtık.
    // fd (file descriptor) -> dosyayı temsil eden bi sayı dönüyo bize.
    int fd = open("/dev/mousepattern", O_RDONLY);
    
    // eğer fd 0'dan küçükse dosya açılamamış demektir (hata durumu)
    if (fd < 0) {
        printf("HATA: Cihaz dosyasi acilmadi! 'sudo' ile calistir.\n");
        return 1; // 1 dönmek hata var demek
    }

    // kullanıcıya bilgi verelim
    printf("***************************************************\n");
    printf("   HAREKET ALGILAMA MODU AKTIF\n");
    printf("***************************************************\n");
    printf("1. BASLATMAK ICIN: Sag Tik Basili Tut + (Sag-Sol-Yukari-Asagi)\n");
    printf("2. DURDURMAK ICIN: Sag Tika 3 kere hizlica bas\n\n");

    // kernelden gelen veriyi saklıcağımız dizi (buffer)
    // 128 karakterlik yer ayırdım, yeter de artar
    char buffer[128];

    // while(1) sonsuz döngü demek. programı biz kapatana kadar sürekli dinlicek.
    while (1) {
        // read() fonksiyonu dosyadan veri okur.
        // fd: hangi dosyadan okucaz
        // buffer: okuyup nereye yazcaz
        // sizeof-1: taşma olmasın diye 1 eksiğini okuyoz
        // n: kaç harf okuduğumuzu söylüyo
        int n = read(fd, buffer, sizeof(buffer)-1);
        
        // n <= 0 ise veri gelmemiştir veya hata vardır, başa dön (continue)
        if (n <= 0) continue;

        // --- C DİLİ PÜF NOKTASI ---
        // C'de stringlerin bittiği yere '\0' (null terminator) koymak zorundayız.
        // yoksa printf nerde durucağını bilemez, hafızadaki saçma şeyleri basar.
        buffer[n] = '\0';
        
        // strstr(samanlık, iğne) mantığı.
        // buffer'ın içinde "ADIM_SAG" kelimesi geçiyo mu diye bakıyo.
        // bulursa true gibi çalışır, bulamazsa NULL döner.
        
        if (strstr(buffer, "ADIM_SAG")) {
            printf("[HAREKET] --> Sağa Gidildi (1/4 Tamam)\n");
        }
        else if (strstr(buffer, "ADIM_SOL")) {
            printf("[HAREKET] <-- Sola Gidildi (2/4 Tamam)\n");
        }
        else if (strstr(buffer, "ADIM_YUKARI")) {
            printf("[HAREKET]  ^  Yukarı Gidildi (3/4 Tamam)\n");
        }
        else if (strstr(buffer, "RESET")) {
            printf("[DURUM] Tuş Bırakıldı - KOMBİNASYON SIFIRLANDI!\n");
        }
        // tıklama sayacı mesajları
        else if (strstr(buffer, "TIK_1")) {
            printf("[KLİK] Tık 1...\n");
        }
        else if (strstr(buffer, "TIK_2")) {
            printf("[KLİK] Tık 2...\n");
        }
        // driverdan START_MUSIC gelirse müziği çalıyoz
        else if (strstr(buffer, "START_MUSIC")) {
            play_music();
            // sleep(2) -> programı 2 saniye uyut.
            // peş peşe müzik açmaya çalışmasın, işlemci rahatlasın.
            sleep(2);
        }
        // STOP_MUSIC gelirse
        else if (strstr(buffer, "STOP_MUSIC")) {
            printf("[KLİK] Tık 3! (KOMUT ALINDI)\n");
            stop_music();
            sleep(1);
        }
    }

    // dosya işimiz bitince kapatıyoz (gerçi sonsuz döngüden çıkmaz ama olsun)
    close(fd);
    return 0; // program başarıyla bitti demek
}
