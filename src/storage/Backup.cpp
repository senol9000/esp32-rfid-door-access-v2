#include "storage/Backup.h"

#include <time.h>
#include "utils/JsonFile.h"

namespace {

// Yönetici dosya yolları (her .cpp'de static olduğu için burada yeniden tanımlı).
constexpr const char* kUsersPath = "/users.json";
constexpr const char* kSchedulesPath = "/schedules.json";
constexpr const char* kHolidaysPath = "/holidays.json";

// saveAtomic JsonDocument& bekler: diziyi yeni bir belgeye kopyalayıp yazar.
bool applyUsers(UserManager& users, JsonArray arr) {
  JsonDocument doc;
  doc.set(arr);
  if (!JsonFile::saveAtomic(kUsersPath, doc)) return false;
  return users.load();
}

bool applySchedules(TimeSchedule& schedule, JsonArray arr) {
  JsonDocument doc;
  doc.set(arr);
  if (!JsonFile::saveAtomic(kSchedulesPath, doc)) return false;
  return schedule.load();
}

bool applyHolidays(HolidayManager& holidays, JsonArray arr) {
  JsonDocument doc;
  doc.set(arr);
  if (!JsonFile::saveAtomic(kHolidaysPath, doc)) return false;
  return holidays.load();
}

}  // namespace

namespace Backup {

void exportAll(AppConfig& cfg, UserManager& users, TimeSchedule& schedule,
               HolidayManager& holidays, AccessLog& accessLog, EventLog& events,
               JsonDocument& doc) {
  doc["version"] = kVersion;
  doc["exported_at"] = (uint32_t)time(nullptr);

  // Yapılandırma: ConfigManager::save ile aynı biçimde (şifre hash'li yazılır)
  JsonObject config = doc["config"].to<JsonObject>();

  JsonObject wifi = config["wifi"].to<JsonObject>();
  wifi["ssid"] = cfg.wifi.ssid;
  wifi["password"] = cfg.wifi.password;
  wifi["hostname"] = cfg.wifi.hostname;

  JsonObject ntp = config["ntp"].to<JsonObject>();
  ntp["server"] = cfg.ntp.server;
  ntp["utcOffsetMin"] = cfg.ntp.utcOffsetMin;
  ntp["autoDst"] = cfg.ntp.autoDst;

  JsonObject gpio = config["gpio"].to<JsonObject>();
  gpio["relayPin"] = cfg.gpio.relayPin;
  gpio["relayActiveLow"] = cfg.gpio.relayActiveLow;
  gpio["relayActiveMs"] = cfg.gpio.relayActiveMs;
  gpio["buzzerPin"] = cfg.gpio.buzzerPin;
  gpio["statusLedPin"] = cfg.gpio.statusLedPin;
  gpio["doorSensorPin"] = cfg.gpio.doorSensorPin;
  gpio["doorSensorActiveLow"] = cfg.gpio.doorSensorActiveLow;
  gpio["exitButtonPin"] = cfg.gpio.exitButtonPin;

  JsonObject rfid = config["rfid"].to<JsonObject>();
  rfid["type"] = cfg.rfid.type;
  rfid["hz1050RxPin"] = cfg.rfid.hz1050RxPin;
  rfid["hz1050TxPin"] = cfg.rfid.hz1050TxPin;
  rfid["hz1050Baud"] = cfg.rfid.hz1050Baud;
  rfid["mfrc522SckPin"] = cfg.rfid.mfrc522SckPin;
  rfid["mfrc522MisoPin"] = cfg.rfid.mfrc522MisoPin;
  rfid["mfrc522MosiPin"] = cfg.rfid.mfrc522MosiPin;
  rfid["mfrc522SdaPin"] = cfg.rfid.mfrc522SdaPin;
  rfid["mfrc522RstPin"] = cfg.rfid.mfrc522RstPin;

  JsonObject mqtt = config["mqtt"].to<JsonObject>();
  mqtt["enabled"] = cfg.mqtt.enabled;
  mqtt["server"] = cfg.mqtt.server;
  mqtt["port"] = cfg.mqtt.port;
  mqtt["username"] = cfg.mqtt.username;
  mqtt["password"] = cfg.mqtt.password;
  mqtt["clientId"] = cfg.mqtt.clientId;
  mqtt["topicPrefix"] = cfg.mqtt.topicPrefix;
  mqtt["discoveryPrefix"] = cfg.mqtt.discoveryPrefix;

  JsonObject auth = config["auth"].to<JsonObject>();
  auth["enabled"] = cfg.auth.enabled;
  auth["username"] = cfg.auth.username;
  auth["passwordHash"] = cfg.auth.passwordHash;
  auth["sessionTimeoutMin"] = cfg.auth.sessionTimeoutMin;

  JsonObject lcd = config["lcd"].to<JsonObject>();
  lcd["enabled"] = cfg.lcd.enabled;
  lcd["sdaPin"] = cfg.lcd.sdaPin;
  lcd["sclPin"] = cfg.lcd.sclPin;
  lcd["address"] = cfg.lcd.address;
  lcd["cols"] = cfg.lcd.cols;
  lcd["rows"] = cfg.lcd.rows;

  // Kullanıcılar
  JsonArray usersArr = doc["users"].to<JsonArray>();
  for (const auto& u : users.all()) {
    JsonObject o = usersArr.add<JsonObject>();
    UserManager::toJson(u, o);
  }

  // Zaman planları
  JsonArray schArr = doc["schedules"].to<JsonArray>();
  for (const auto& kv : schedule.all()) {
    JsonObject o = schArr.add<JsonObject>();
    TimeSchedule::toJson(kv.second, o);
  }

  // Tatiller
  JsonArray holArr = doc["holidays"].to<JsonArray>();
  for (const auto& kv : holidays.all()) {
    JsonObject o = holArr.add<JsonObject>();
    o["date"] = kv.first;
    o["name"] = kv.second;
  }

  // Loglar (RAM'deki son kayıtlar)
  JsonArray accArr = doc["access_log"].to<JsonArray>();
  for (const auto& r : accessLog.entries()) {
    JsonObject o = accArr.add<JsonObject>();
    AccessLog::toJson(r, o);
  }
  JsonArray evtArr = doc["event_log"].to<JsonArray>();
  for (const auto& r : events.entries()) {
    JsonObject o = evtArr.add<JsonObject>();
    EventLog::toJson(r, o);
  }
}

bool restoreAll(AppConfig& cfg, UserManager& users, TimeSchedule& schedule,
                HolidayManager& holidays, AccessLog& accessLog, EventLog& events,
                JsonDocument& doc) {
  // ---- Doğrulama: hiçbir şey yazmadan önce tüm bölümleri kontrol et ----
  const int version = doc["version"] | 0;
  if (version != kVersion) {
    Serial.println("[BKP] HATA: Desteklenmeyen yedek sürümü.");
    return false;
  }
  if (!doc["config"].is<JsonObject>()) return false;
  if (!doc["users"].is<JsonArray>()) return false;
  if (!doc["schedules"].is<JsonArray>()) return false;
  if (!doc["holidays"].is<JsonArray>()) return false;

  // ---- Yapılandırmayı uygula ----
  // ConfigManager::save ile aynı biçimi üretmek için config alt nesnesini
  // doğrudan /config.json'a yazarız; ConfigManager::load aynı şemayı okur.
  {
    JsonDocument cfgDoc;
    cfgDoc.set(doc["config"].as<JsonObject>());
    if (!JsonFile::saveAtomic("/config.json", cfgDoc)) {
      Serial.println("[BKP] HATA: config.json yazılamadı.");
      return false;
    }
    if (!ConfigManager::load(cfg)) return false;
  }

  // ---- Kullanıcılar, planlar, tatiller ----
  if (!applyUsers(users, doc["users"].as<JsonArray>())) {
    Serial.println("[BKP] HATA: Kullanıcılar geri yüklenemedi.");
    return false;
  }
  if (!applySchedules(schedule, doc["schedules"].as<JsonArray>())) {
    Serial.println("[BKP] HATA: Zaman planları geri yüklenemedi.");
    return false;
  }
  if (!applyHolidays(holidays, doc["holidays"].as<JsonArray>())) {
    Serial.println("[BKP] HATA: Tatiller geri yüklenemedi.");
    return false;
  }

  // ---- Loglar (varsa) ----
  if (doc["access_log"].is<JsonArray>() && doc["access_log"].size() > 0) {
    accessLog.clear();
    for (JsonObject o : doc["access_log"].as<JsonArray>()) {
      AccessRecord r = AccessLog::fromJson(o);
      if (r.timestamp != 0) accessLog.add(r);
    }
  }
  if (doc["event_log"].is<JsonArray>() && doc["event_log"].size() > 0) {
    events.clear();
    for (JsonObject o : doc["event_log"].as<JsonArray>()) {
      EventRecord r = EventLog::fromJson(o);
      if (r.type != EventType::None) events.add(r.type, r.message);
    }
  }

  Serial.println("[BKP] Geri yükleme başarılı.");
  return true;
}

}  // namespace Backup
