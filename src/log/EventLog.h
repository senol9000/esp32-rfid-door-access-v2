#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <deque>

/** Sistem olay tipi. */
enum class EventType : uint8_t {
  None = 0,
  Boot,             // Cihaz açıldı
  Restart,          // Yeniden başlatma
  WifiConnected,    // WiFi'ye bağlandı
  WifiDisconnected, // WiFi koptu
  MqttConnected,    // MQTT broker'a bağlandı
  MqttError,        // MQTT bağlantı hatası
  NtpSync,          // NTP saat senkronu
  ConfigSave,       // Yapılandırma kaydedildi
  ConfigRestore,    // Yapılandırma geri yüklendi
  FirmwareUpdate,   // Firmware güncellendi
  Login,            // Web girişi
  Logout,           // Web çıkışı
  Unauthorized,     // Yetkisiz erişim denemesi
  JsonParseError,   // JSON parse hatası
  FlashError,       // Flash (LittleFS) hatası
  RfidChanged,      // RFID okuyucu tipi değiştirildi
  UserAdded,        // Kullanıcı eklendi
  UserDeleted       // Kullanıcı silindi
};

/** Tek bir sistem olayı kaydı. */
struct EventRecord {
  uint32_t id = 0;        // Benzersiz kayıt kimliği
  uint32_t timestamp = 0; // Epoch saniye
  EventType type = EventType::None;
  String message;         // İnsan okunur açıklama
};

/**
 * Sistem olay (event) log yöneticisi.
 *
 * Erişim logundan ayrıdır: boot, WiFi, MQTT, NTP, yapılandırma gibi
 * sistem durum değişimlerini kaydeder.
 * - RAM içinde son N kayıt (hızlı sorgu).
 * - /event_log.jsonl üzerinde JSONL ekleme ile kalıcılık (debounce'lu flush).
 * - Dosya boyutu sınırı aşınca en son kayıtlar korunarak kırpılır.
 */
class EventLog {
 public:
  void begin();
  void loop();

  /** Yeni sistem olayı ekler; RAM'e anında, dosyaya arka planda yazılır. */
  void add(EventType type, const String& message);

  /** Tüm logları temizler. */
  void clear();

  size_t count() const { return log_.size(); }

  const std::deque<EventRecord>& entries() const { return log_; }

  static void toJson(const EventRecord& r, JsonObject obj);
  static EventRecord fromJson(JsonObject obj);
  static const char* typeName(EventType t);

 private:
  std::deque<EventRecord> log_;       // RAM içi son kayıtlar
  std::deque<EventRecord> pending_;   // dosyaya yazılmayı bekleyen kayıtlar
  uint32_t nextId_ = 1;
  unsigned long lastFlushMs_ = 0;

  bool load();
  void flush();
  void trimFile();
};
