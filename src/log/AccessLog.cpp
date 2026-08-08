#include "log/AccessLog.h"
#include <LittleFS.h>
#include <time.h>
#include "utils/JsonFile.h"

static const char* kAccessPath = "/access_log.jsonl";
static const size_t kMaxInMemory = 400;
static const size_t kMaxFileBytes = 64 * 1024;  // 64 KB üzeri kırpılır
static const size_t kKeepOnTrim = 200;
static const unsigned long kFlushIntervalMs = 5000;
static const size_t kFlushBatchSize = 20;

static const char* kResultNames[] = {"allowed", "denied"};
static const char* kReasonNames[] = {
    "none",       "valid",        "out_of_hours", "card_not_found",
    "card_inactive", "holiday",   "door_open",    "invalid_uid",
    "error"};

const char* AccessLog::resultName(AccessResult r) { return kResultNames[(int)r]; }

const char* AccessLog::reasonName(AccessReason r) {
  return kReasonNames[(int)r];
}

void AccessLog::toJson(const AccessRecord& r, JsonObject obj) {
  obj["id"] = r.id;
  obj["ts"] = r.timestamp;
  obj["uid"] = r.uid;
  obj["name"] = r.name;
  obj["door"] = r.door;
  obj["result"] = resultName(r.result);
  obj["reason"] = reasonName(r.reason);
  obj["ip"] = r.ip;
  obj["rssi"] = r.rssi;
}

AccessRecord AccessLog::fromJson(JsonObject obj) {
  AccessRecord r;
  r.id = obj["id"] | 0u;
  r.timestamp = obj["ts"] | 0u;
  r.uid = obj["uid"] | "";
  r.name = obj["name"] | "";
  r.door = obj["door"] | 1u;
  const char* res = obj["result"] | "denied";
  r.result = (strcmp(res, "allowed") == 0) ? AccessResult::Allowed : AccessResult::Denied;
  const char* reason = obj["reason"] | "none";
  r.reason = AccessReason::None;
  for (int i = 0; i < 9; i++) {
    if (strcmp(reason, kReasonNames[i]) == 0) {
      r.reason = (AccessReason)i;
      break;
    }
  }
  r.ip = obj["ip"] | "";
  r.rssi = obj["rssi"] | 0;
  return r;
}

void AccessLog::begin() {
  lastFlushMs_ = millis();
  load();
  Serial.printf("[LOG] Erişim log hazır (%u kayıt RAM'de).\n", log_.size());
}

void AccessLog::loop() {
  if (pending_.empty()) {
    return;
  }
  const unsigned long now = millis();
  if (now - lastFlushMs_ < kFlushIntervalMs && pending_.size() < kFlushBatchSize) {
    return;
  }
  lastFlushMs_ = now;
  flush();
}

bool AccessLog::load() {
  File f = LittleFS.open(kAccessPath, "r");
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
          AccessRecord r = fromJson(doc.as<JsonObject>());
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

void AccessLog::add(AccessRecord rec) {
  if (rec.id == 0) {
    rec.id = nextId_++;
  }
  log_.push_back(rec);
  pending_.push_back(rec);
  while (log_.size() > kMaxInMemory) {
    log_.pop_front();
  }
}

void AccessLog::flush() {
  if (pending_.empty()) {
    return;
  }
  for (AccessRecord& r : pending_) {
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    toJson(r, obj);
    JsonFile::appendLine(kAccessPath, doc);
  }
  pending_.clear();
  if (JsonFile::fileSize(kAccessPath) > kMaxFileBytes) {
    trimFile();
  }
}

void AccessLog::trimFile() {
  const size_t n = log_.size();
  if (n == 0) {
    LittleFS.remove(kAccessPath);
    return;
  }
  const char* tmp = "/access_log.tmp";
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
  LittleFS.remove(kAccessPath);
  LittleFS.rename(tmp, kAccessPath);
  Serial.println("[LOG] Log dosyası kırpıldı.");
}

void AccessLog::clear() {
  LittleFS.remove(kAccessPath);
  log_.clear();
  pending_.clear();
  Serial.println("[LOG] Erişim logları temizlendi.");
}

size_t AccessLog::countToday() const {
  time_t t = time(nullptr);
  if (t < 1600000000) {
    return 0;  // NTP senkron değil
  }
  struct tm tmv;
  localtime_r(&t, &tmv);
  tmv.tm_hour = 0;
  tmv.tm_min = 0;
  tmv.tm_sec = 0;
  const time_t start = mktime(&tmv);
  size_t c = 0;
  for (const AccessRecord& r : log_) {
    if (r.timestamp >= start && r.timestamp < start + 86400) {
      c++;
    }
  }
  return c;
}

size_t AccessLog::countTodayAllowed() const {
  time_t t = time(nullptr);
  if (t < 1600000000) {
    return 0;
  }
  struct tm tmv;
  localtime_r(&t, &tmv);
  tmv.tm_hour = 0;
  tmv.tm_min = 0;
  tmv.tm_sec = 0;
  const time_t start = mktime(&tmv);
  size_t c = 0;
  for (const AccessRecord& r : log_) {
    if (r.result == AccessResult::Allowed && r.timestamp >= start &&
        r.timestamp < start + 86400) {
      c++;
    }
  }
  return c;
}
