#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <map>

/**
 * Resmi tatil günleri yöneticisi.
 *
 * Tarihler "YYYY-MM-DD" biçiminde, adıyla birlikte saklanır.
 * Tatil günlerinde normal kartlar engellenir; admin kartlar etkilenmez
 * (karar motoru bunu yönetir). Veri /holidays.json içinde JSON dizisi olarak tutulur.
 */
class HolidayManager {
 public:
  void begin();
  void loop();

  /** Bugün (yerel saat) tatil mi? */
  bool isTodayHoliday() const;

  /** Belirtilen YYYY-MM-DD tarihi tatil mi? */
  bool isHoliday(const String& date) const;

  /** Tatil ekler (tarih + ad). Geçersizse false döner. */
  bool add(const String& date, const String& name);

  /** Tatili siler. */
  bool remove(const String& date);

  /** Tarih -> ad haritası. */
  const std::map<String, String>& all() const { return holidays_; }
  size_t count() const { return holidays_.size(); }

  bool save();
  bool load();

 private:
  std::map<String, String> holidays_;  // "YYYY-MM-DD" -> "Adı"
  bool dirty_ = false;
  unsigned long lastSaveMs_ = 0;
};
