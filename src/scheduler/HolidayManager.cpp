#include "scheduler/HolidayManager.h"
#include <time.h>
#include <vector>
#include "utils/JsonFile.h"

static const char* kHolidaysPath = "/holidays.json";

void HolidayManager::begin() {
  load();
  Serial.printf("[HOL] %d tatil günü yüklendi.\n", (int)holidays_.size());
}

void HolidayManager::loop() {
  if (dirty_ && millis() - lastSaveMs_ > 2000) {
    dirty_ = false;
    save();
  }
}

bool HolidayManager::isTodayHoliday() const {
  const time_t t = time(nullptr);
  if (t < 1600000000) {
    return false;  // NTP senkron olmadan tarih bilinemez
  }
  struct tm tmv;
  localtime_r(&t, &tmv);
  char buf[16];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &tmv);
  return holidays_.count(buf) > 0;
}

bool HolidayManager::isHoliday(const String& date) const {
  return holidays_.count(date) > 0;
}

bool HolidayManager::add(const String& date, const String& name) {
  if (date.length() != 10 || date.charAt(4) != '-' || date.charAt(7) != '-') {
    return false;
  }
  holidays_[date] = name;
  dirty_ = true;
  lastSaveMs_ = millis();
  return true;
}

bool HolidayManager::remove(const String& date) {
  if (holidays_.erase(date) == 0) {
    return false;
  }
  dirty_ = true;
  lastSaveMs_ = millis();
  return true;
}

bool HolidayManager::save() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const auto& kv : holidays_) {
    JsonObject o = arr.add<JsonObject>();
    o["date"] = kv.first;
    o["name"] = kv.second;
  }
  const bool ok = JsonFile::saveAtomic(kHolidaysPath, doc);
  if (!ok) {
    Serial.println("[HOL] HATA: Tatil dosyası yazılamadı.");
  }
  return ok;
}

bool HolidayManager::load() {
  JsonDocument doc;
  if (!JsonFile::load(kHolidaysPath, doc)) {
    return false;
  }
  holidays_.clear();
  for (JsonObject o : doc.as<JsonArray>()) {
    const String d = o["date"] | "";
    const String n = o["name"] | "";
    if (!d.isEmpty()) {
      holidays_[d] = n;
    }
  }
  return true;
}
