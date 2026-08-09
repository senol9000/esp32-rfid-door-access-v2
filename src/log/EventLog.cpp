#include "log/EventLog.h"
#include <LittleFS.h>
#include <time.h>
#include "utils/JsonFile.h"

static const char* kEventPath = "/event_log.jsonl";
static const size_t kMaxInMemory = 300;
static const size_t kMaxFileBytes = 32 * 1024;  // 32 KB üzeri kırpılır
static const size_t kKeepOnTrim = 150;
static const unsigned long kFlushIntervalMs = 3000;

static const char* kEventTypeNames[] = {
    "none",       "boot",      "restart",      "wifi_connected",
    "wifi_disconnected", "mqtt_connected", "mqtt_error", "ntp_sync",
    "config_save", "config_restore", "firmware_update", "login",
    "logout",     "unauthorized", "json_parse_error", "flash_error",
    "rfid_changed", "user_added", "user_deleted", "watchdog_wifi_reset",
    "watchdog_reboot"};

const char* EventLog::typeName(EventType t) {
  const int i = (int)t;
  return (i >= 0 && i < (int)(sizeof(kEventTypeNames) / sizeof(const char*)))
             ? kEventTypeNames[i]
             : "none";
}

void EventLog::toJson(const EventRecord& r, JsonObject obj) {
  obj["id"] = r.id;
  obj["ts"] = r.timestamp;
  obj["type"] = typeName(r.type);
  obj["message"] = r.message;
}

EventRecord EventLog::fromJson(JsonObject obj) {
  EventRecord r;
  r.id = obj["id"] | 0u;
  r.timestamp = obj["ts"] | 0u;
  const char* type = obj["type"] | "none";
  r.type = EventType::None;
  for (int i = 0; i < (int)(sizeof(kEventTypeNames) / sizeof(const char*));
       i++) {
    if (strcmp(type, kEventTypeNames[i]) == 0) {
      r.type = (EventType)i;
      break;
    }
  }
  r.message = obj["message"] | "";
  return r;
}

void EventLog::begin() {
  lastFlushMs_ = millis();
  load();
  Serial.printf("[EVT] Olay log hazır (%u kayıt RAM'de).\n", log_.size());
}

void EventLog::loop() {
  if (pending_.empty()) {
    return;
  }
  const unsigned long now = millis();
  if (now - lastFlushMs_ < kFlushIntervalMs) {
    return;
  }
  lastFlushMs_ = now;
  flush();
}

bool EventLog::load() {
  File f = LittleFS.open(kEventPath, "r");
  if (!f) {
    return false;
  }
  log_.clear();
  String line;
  JsonDocument doc;
  uint32_t maxId = 0;
  while (f.available()) {
    const char c = (char)f.read();
    if (c == '\n') {
      if (line.length() > 0) {
        if (!deserializeJson(doc, line)) {
          EventRecord r = fromJson(doc.as<JsonObject>());
          if (r.id > maxId) {
            maxId = r.id;
          }
          log_.push_back(r);
          while (log_.size() > kMaxInMemory) {
            log_.pop_front();
          }
        }
        line = "";
      }
    } else {
      line += c;
    }
  }
  f.close();
  nextId_ = maxId + 1;
  return true;
}

void EventLog::add(EventType type, const String& message) {
  EventRecord rec;
  rec.id = nextId_++;
  rec.timestamp = (uint32_t)time(nullptr);
  rec.type = type;
  rec.message = message;
  log_.push_back(rec);
  pending_.push_back(rec);
  while (log_.size() > kMaxInMemory) {
    log_.pop_front();
  }
  while (pending_.size() > kMaxInMemory) {
    pending_.pop_front();
  }
}

void EventLog::flush() {
  if (pending_.empty()) {
    return;
  }
  for (const EventRecord& r : pending_) {
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    toJson(r, obj);
    JsonFile::appendLine(kEventPath, doc);
  }
  pending_.clear();
  if (JsonFile::fileSize(kEventPath) > kMaxFileBytes) {
    trimFile();
  }
}

void EventLog::trimFile() {
  const size_t n = log_.size();
  if (n == 0) {
    LittleFS.remove(kEventPath);
    return;
  }
  const char* tmp = "/event_log.tmp";
  File f = LittleFS.open(tmp, "w");
  if (!f) {
    return;
  }
  const size_t start = n > kKeepOnTrim ? n - kKeepOnTrim : 0;
  for (size_t i = start; i < n; i++) {
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    toJson(log_[i], obj);
    serializeJson(doc, f);
    f.write('\n');
  }
  f.close();
  LittleFS.remove(kEventPath);
  LittleFS.rename(tmp, kEventPath);
}

void EventLog::clear() {
  LittleFS.remove(kEventPath);
  log_.clear();
  pending_.clear();
  Serial.println("[EVT] Olay logları temizlendi.");
}
