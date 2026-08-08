#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <deque>
#include <vector>

/** Geçiş sonucu. */
enum class AccessResult : uint8_t { Allowed = 0, Denied = 1 };

/** Reddedilme sebebi (Allowed sonuçlarında Valid kullanılır). */
enum class AccessReason : uint8_t {
  None = 0,
  Valid,
  OutOfHours,     // Saat dışı
  CardNotFound,   // Kart bulunamadı
  CardInactive,   // Pasif kart
  Holiday,        // Tatil günü
  DoorOpen,       // Kapı zaten açık
  InvalidUid,     // Geçersiz UID
  Error
};

/** Tek bir erişim log kaydı. */
struct AccessRecord {
  uint32_t id = 0;            // Benzersiz kayıt kimliği
  uint32_t timestamp = 0;     // Epoch saniye
  String uid;                 // Okunan kart UID'si
  String name;                // Ad Soyad (kart bulunamadıysa boş)
  uint8_t door = 1;           // Kapı numarası
  AccessResult result = AccessResult::Denied;
  AccessReason reason = AccessReason::CardNotFound;
  String ip;                  // İsteği yapan IP (web API'den girişlerde)
  int32_t rssi = 0;           // WiFi sinyal gücü
};

/**
 * Erişim log yöneticisi.
 *
 * - RAM içinde son N kayıt (hızlı sorgu / dashboard).
 * - /access_log.jsonl üzerinde JSONL ekleme ile kalıcılık (debounce'lu flush).
 * - Dosya boyutu sınırı aşınca en son kayıtlar korunarak kırpılır.
 */
class AccessLog {
 public:
  void begin();
  void loop();

  /** Yeni kayıt ekler; RAM'e anında, dosyaya arka planda yazılır. */
  void add(AccessRecord rec);

  /** Tüm logları (dosya + RAM) temizler. */
  void clear();

  size_t count() const { return log_.size(); }
  size_t countToday() const;
  size_t countTodayAllowed() const;

  const std::deque<AccessRecord>& entries() const { return log_; }

  static void toJson(const AccessRecord& r, JsonObject obj);
  static AccessRecord fromJson(JsonObject obj);

  static const char* resultName(AccessResult r);
  static const char* reasonName(AccessReason r);

 private:
  std::deque<AccessRecord> log_;       // RAM içi son kayıtlar
  std::vector<AccessRecord> pending_;  // dosyaya yazılmayı bekleyen kayıtlar
  uint32_t nextId_ = 1;
  unsigned long lastFlushMs_ = 0;

  bool load();
  void flush();
  void trimFile();
};
