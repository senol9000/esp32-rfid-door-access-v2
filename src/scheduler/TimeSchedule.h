#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <map>

/** Haftanın bir günündeki tek bir zaman aralığı (dakika cinsinden). */
struct TimeRange {
  int startMin = 0;  // 0..1439 (örn. 8*60=480)
  int endMin = 0;    // startMin'tan büyük olmalı
};

/** Bir kullanıcının 7 günlük zaman planı. */
struct WeeklySchedule {
  String uid;                       // Bağlı olduğu kart UID'si
  bool enabled = false;             // Bu kullanıcı için zaman planı aktif mi?
  bool dayEnabled[7] = {false, false, false, false, false, false, false};  // Pzt..Paz
  std::vector<TimeRange> ranges[7];  // Her gün için çoklu aralık (örn. 08-12, 13-18)
};

/**
 * Haftalık geçiş takvimi yöneticisi.
 *
 * Her kullanıcı için 7 gün, her gün için birden çok zaman aralığı desteklenir.
 * Veriler /schedules.json içinde JSON nesnesi (uid -> plan) olarak saklanır.
 */
class TimeSchedule {
 public:
  void begin();
  void loop();

  /** Belirtilen gün (0=Pazartesi..6=Pazar) ve dakikada erişim var mı? */
  bool isAllowed(const WeeklySchedule& s, int weekday, int minuteOfDay) const;

  /** UID için planı döndürür; yoksa boş plan oluşturur. */
  WeeklySchedule& get(const String& uid);

  /** Planı kaydeder (debounce'lu). */
  void set(const WeeklySchedule& s);

  /** UID planını siler. */
  bool remove(const String& uid);

  const std::map<String, WeeklySchedule>& all() const { return plans_; }
  bool has(const String& uid) const { return plans_.count(uid) > 0; }

  bool save();
  bool load();

  // JSON serileştirme
  static void toJson(const WeeklySchedule& s, JsonObject obj);
  static WeeklySchedule fromJson(JsonObject obj);

 private:
  std::map<String, WeeklySchedule> plans_;
  bool dirty_ = false;
  unsigned long lastSaveMs_ = 0;
};
