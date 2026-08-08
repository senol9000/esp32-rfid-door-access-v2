#pragma once

#include <Arduino.h>

/** Uygulama adı ve firma. Web arayüzü/Footer ve MQTT/HA'da kullanılır. */
#define APP_NAME "ESP32 Kapı Kontrol"
#define APP_COMPANY "akevtek"
#define APP_VERSION "1.0.0"

/** WiFi yapılandırması. Web arayüzünden düzenlenir ve JSON olarak saklanır. */
struct WifiConfig {
  String ssid;        // Bağlanılacak ağ adı
  String password;    // Ağ şifresi
  String hostname;    // Cihazın ağ üzerindeki adı
};

/** NTP / saat yapılandırması. */
struct NtpConfig {
  String server = "pool.ntp.org";  // NTP sunucusu
  int utcOffsetMin = 180;          // UTC ofseti (dakika). Türkiye için GMT+3 = 180
  bool autoDst = false;            // Avrupa yaz/kış saati kuralını otomatik uygula
};

/** RFID okuyucu yapılandırması. Okuyucu tipi runtime'da web üzerinden seçilir. */
struct RfidConfig {
  String type = "auto";  // auto | hz1050 | mfrc522

  // HZ1050 (UART) — varsayılanlar buzzer(16)/door sensor(17) ile çakışmaz
  int hz1050RxPin = 25;      // RX: okuyucunun TX ucuna bağlanır
  int hz1050TxPin = 26;      // TX: okuyucunun RX ucuna bağlanır
  long hz1050Baud = 9600;    // 9600 veya 19200

  // MFRC522 (SPI) — SCK=18 exit butonu(18) ile çakışmaması için exit=32 varsayılanı kullan
  int mfrc522SckPin = 18;
  int mfrc522MisoPin = 19;
  int mfrc522MosiPin = 23;
  int mfrc522SdaPin = 5;   // SS / SDA
  int mfrc522RstPin = 21;
};

/** MQTT broker yapılandırması. Home Assistant Auto Discovery destekler. */
struct MqttConfig {
  bool enabled = false;          // MQTT bağlantısı açık/kapalı
  String server;                 // Broker adresi (örn. 192.168.1.10)
  int port = 1883;               // Broker portu
  String username;               // Opsiyonel kullanıcı adı
  String password;               // Opsiyonel şifre
  String clientId = "esp32-door";// MQTT istemci kimliği
  String topicPrefix = "esp32door";  // Tüm topic'lerin kökü (örn. esp32door/...)
  String discoveryPrefix = "homeassistant";  // HA discovery topic kökü
};

/**
 * Web paneli güvenlik (auth) yapılandırması.
 *
 * passwordHash boş olduğu sürece kilit kapalıdır (ilk kurulumda kullanıcı
 * önce şifre belirler, sonra sistem kilitlenir). Şifre PBKDF2-HMAC-SHA256
 * ile tuzlanarak saklanır; düz metin asla yazılmaz.
 */
struct AuthConfig {
  bool enabled = false;            // Kilit aktif mi?
  String username = "admin";       // Giriş kullanıcı adı
  String passwordHash = "";        // "pbkdf2$<iterations>$<saltHex>$<hashHex>"
  int sessionTimeoutMin = 60;      // Oturum zaman aşımı (dakika)
};

/** Kapı donanımı (GPIO) yapılandırması. Tüm pinler web üzerinden değiştirilebilir. */
struct GpioConfig {
  // Röle çıkışı
  int relayPin = 4;          // Röle bağlı GPIO
  bool relayActiveLow = false;  // false: aktifken HIGH (NO), true: aktifken LOW (NC)
  int relayActiveMs = 1000;  // Kapı açık kalma süresi (ms): 500/1000/2000/5000/10000

  // Buzzer, status LED, kapı sensörü, çıkış butonu (negatif = devre dışı)
  int buzzerPin = 16;
  int statusLedPin = 2;
  int doorSensorPin = 17;    // manyetik kontak: kapı açıkken LOW kabul edilir (ayarlanabilir)
  bool doorSensorActiveLow = true;
  int exitButtonPin = 32;    // içeriden çıkış butonu; aktifken HIGH
};

/**
 * I2C 16x2 LCD (PCF8574 adaptörlü HD44780) yapılandırması.
 *
 * Pinler ve I2C adresi "otomatik" olabilir: sdaPin/sclPin -1, address 0
 * verilirse LcdManager açılışta veya web'deki "Otomatik Algıla" butonuyla
 * donanımı tarar, bulduğu değerleri buraya yazar (sonraki açılışta hızlı).
 */
struct LcdConfig {
  bool enabled = false;  // LCD kullanımı açık mı?
  int sdaPin = -1;       // -1 = otomatik algıla
  int sclPin = -1;       // -1 = otomatik algıla
  int address = 0;       // 0 = otomatik tara (0x27 / 0x3F öncelikli)
  int cols = 16;         // Sütun sayısı
  int rows = 2;          // Satır sayısı
};

/** Uygulama geneli yapılandırma şeması. */
struct AppConfig {
  WifiConfig wifi;
  NtpConfig ntp;
  GpioConfig gpio;
  RfidConfig rfid;
  MqttConfig mqtt;
  AuthConfig auth;
  LcdConfig lcd;
};

/**
 * Yapılandırma yöneticisi.
 * Ayarlar Preferences yerine LittleFS üzerinde /config.json olarak saklanır.
 */
class ConfigManager {
 public:
  /** /config.json dosyasından ayarları okur. Dosya yoksa false döner. */
  static bool load(AppConfig& cfg);

  /** Ayarları /config.json dosyasına atomik olarak yazar. */
  static bool save(const AppConfig& cfg);
};
