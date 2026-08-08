#include "scheduler/TimeSchedule.h"
#include <vector>
#include "utils/JsonFile.h"
#include <LittleFS.h>

static const char* kSchedulesPath = "/schedules.json";
static const char* kDayNames[7] = {"monday", "tuesday", "wednesday", "thursday",
                                   "friday", "saturday", "sunday"};

void TimeSchedule::begin() {
  load();
  if (plans_.empty()) {
    Serial.println("[SCH] Zaman planı dosyası yok; boş plan ile başlandı.");
  } else {
    Serial.printf("[SCH] %d kullanıcı zaman planı yüklendi.\n", (int)plans_.size());
  }
}

void TimeSchedule::loop() {
  if (dirty_ && millis() - lastSaveMs_ > 2000) {
    dirty_ = false;
    save();
  }
}

bool TimeSchedule::isAllowed(const WeeklySchedule& s, int weekday, int minuteOfDay) const {
  if (!s.enabled || weekday < 0 || weekday >= 7) {
    return true;  // zaman planı kapalıysa kısıtlama yok
  }
  if (!s.dayEnabled[weekday]) {
    return false;  // gün pasif
  }
  for (const TimeRange& r : s.ranges[weekday]) {
    if (minuteOfDay >= r.startMin && minuteOfDay < r.endMin) {
      return true;
    }
  }
  return false;
}

WeeklySchedule& TimeSchedule::get(const String& uid) {
  auto it = plans_.find(uid);
  if (it != plans_.end()) {
    return it->second;
  }
  WeeklySchedule s;
  s.uid = uid;
  return plans_[uid] = s;
}

void TimeSchedule::set(const WeeklySchedule& s) {
  plans_[s.uid] = s;
  dirty_ = true;
  lastSaveMs_ = millis();
}

bool TimeSchedule::remove(const String& uid) {
  if (plans_.erase(uid) == 0) {
    return false;
  }
  dirty_ = true;
  lastSaveMs_ = millis();
  return true;
}

bool TimeSchedule::save() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const auto& kv : plans_) {
    JsonObject o = arr.add<JsonObject>();
    toJson(kv.second, o);
  }
  const bool ok = JsonFile::saveAtomic(kSchedulesPath, doc);
  if (!ok) {
    Serial.println("[SCH] HATA: Plan dosyası yazılamadı.");
  }
  return ok;
}

bool TimeSchedule::load() {
  JsonDocument doc;
  if (!JsonFile::load(kSchedulesPath, doc)) {
    return false;
  }
  plans_.clear();
  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject o : arr) {
    WeeklySchedule s = fromJson(o);
    if (!s.uid.isEmpty()) {
      plans_[s.uid] = s;
    }
  }
  return true;
}

void TimeSchedule::toJson(const WeeklySchedule& s, JsonObject obj) {
  obj["uid"] = s.uid;
  obj["enabled"] = s.enabled;
  JsonArray days = obj["days"].to<JsonArray>();
  for (int d = 0; d < 7; d++) {
    JsonObject day = days.add<JsonObject>();
    day["name"] = kDayNames[d];
    day["enabled"] = s.dayEnabled[d];
    JsonArray rng = day["ranges"].to<JsonArray>();
    for (const TimeRange& r : s.ranges[d]) {
      JsonObject ro = rng.add<JsonObject>();
      ro["start"] = r.startMin;
      ro["end"] = r.endMin;
    }
  }
}

WeeklySchedule TimeSchedule::fromJson(JsonObject obj) {
  WeeklySchedule s;
  s.uid = obj["uid"] | "";
  s.enabled = obj["enabled"] | false;
  int d = 0;
  for (JsonObject day : obj["days"].as<JsonArray>()) {
    if (d >= 7) {
      break;
    }
    s.dayEnabled[d] = day["enabled"] | false;
    for (JsonObject r : day["ranges"].as<JsonArray>()) {
      TimeRange tr;
      tr.startMin = r["start"] | 0;
      tr.endMin = r["end"] | 0;
      if (tr.startMin >= 0 && tr.endMin > tr.startMin && tr.endMin <= 1440) {
        s.ranges[d].push_back(tr);
      }
    }
    d++;
  }
  return s;
}
