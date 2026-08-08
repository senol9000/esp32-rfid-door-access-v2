#include "config/Config.h"
#include <ArduinoJson.h>
#include "utils/JsonFile.h"

static const char* kConfigPath = "/config.json";

bool ConfigManager::load(AppConfig& cfg) {
  JsonDocument doc;
  if (!JsonFile::load(kConfigPath, doc)) {
    return false;
  }

  cfg.wifi.ssid = doc["wifi"]["ssid"] | "";
  cfg.wifi.password = doc["wifi"]["password"] | "";
  cfg.wifi.hostname = doc["wifi"]["hostname"] | "esp32-door";

  cfg.ntp.server = doc["ntp"]["server"] | "pool.ntp.org";
  cfg.ntp.utcOffsetMin = doc["ntp"]["utcOffsetMin"] | 180;
  cfg.ntp.autoDst = doc["ntp"]["autoDst"] | false;

  // GPIO ayarları (eski kayıtlarda alan yoksa varsayılanlar kullanılır)
  cfg.gpio.relayPin = doc["gpio"]["relayPin"] | 4;
  cfg.gpio.relayActiveLow = doc["gpio"]["relayActiveLow"] | false;
  cfg.gpio.relayActiveMs = doc["gpio"]["relayActiveMs"] | 1000;
  cfg.gpio.buzzerPin = doc["gpio"]["buzzerPin"] | 16;
  cfg.gpio.statusLedPin = doc["gpio"]["statusLedPin"] | 2;
  cfg.gpio.doorSensorPin = doc["gpio"]["doorSensorPin"] | 17;
  cfg.gpio.doorSensorActiveLow = doc["gpio"]["doorSensorActiveLow"] | true;
  cfg.gpio.exitButtonPin = doc["gpio"]["exitButtonPin"] | 32;

  // RFID okuyucu ayarları
  cfg.rfid.type = doc["rfid"]["type"] | "auto";
  cfg.rfid.hz1050RxPin = doc["rfid"]["hz1050RxPin"] | 25;
  cfg.rfid.hz1050TxPin = doc["rfid"]["hz1050TxPin"] | 26;
  cfg.rfid.hz1050Baud = doc["rfid"]["hz1050Baud"] | 9600;
  cfg.rfid.mfrc522SckPin = doc["rfid"]["mfrc522SckPin"] | 18;
  cfg.rfid.mfrc522MisoPin = doc["rfid"]["mfrc522MisoPin"] | 19;
  cfg.rfid.mfrc522MosiPin = doc["rfid"]["mfrc522MosiPin"] | 23;
  cfg.rfid.mfrc522SdaPin = doc["rfid"]["mfrc522SdaPin"] | 5;
  cfg.rfid.mfrc522RstPin = doc["rfid"]["mfrc522RstPin"] | 21;

  // MQTT ayarları (eski kayıtlarda alan yoksa kapalı)
  cfg.mqtt.enabled = doc["mqtt"]["enabled"] | false;
  cfg.mqtt.server = doc["mqtt"]["server"] | "";
  cfg.mqtt.port = doc["mqtt"]["port"] | 1883;
  cfg.mqtt.username = doc["mqtt"]["username"] | "";
  cfg.mqtt.password = doc["mqtt"]["password"] | "";
  cfg.mqtt.clientId = doc["mqtt"]["clientId"] | "esp32-door";
  cfg.mqtt.topicPrefix = doc["mqtt"]["topicPrefix"] | "esp32door";
  cfg.mqtt.discoveryPrefix = doc["mqtt"]["discoveryPrefix"] | "homeassistant";

  // Güvenlik (auth) ayarları (eski kayıtlarda alan yoksa kilit kapalı)
  cfg.auth.enabled = doc["auth"]["enabled"] | false;
  cfg.auth.username = doc["auth"]["username"] | "admin";
  cfg.auth.passwordHash = doc["auth"]["passwordHash"] | "";
  cfg.auth.sessionTimeoutMin = doc["auth"]["sessionTimeoutMin"] | 60;

  // I2C LCD ayarları (-1 pin / 0 adres = otomatik algıla)
  cfg.lcd.enabled = doc["lcd"]["enabled"] | false;
  cfg.lcd.sdaPin = doc["lcd"]["sdaPin"] | -1;
  cfg.lcd.sclPin = doc["lcd"]["sclPin"] | -1;
  cfg.lcd.address = doc["lcd"]["address"] | 0;
  cfg.lcd.cols = doc["lcd"]["cols"] | 16;
  cfg.lcd.rows = doc["lcd"]["rows"] | 2;
  return true;
}

bool ConfigManager::save(const AppConfig& cfg) {
  JsonDocument doc;
  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["ssid"] = cfg.wifi.ssid;
  wifi["password"] = cfg.wifi.password;
  wifi["hostname"] = cfg.wifi.hostname;

  JsonObject ntp = doc["ntp"].to<JsonObject>();
  ntp["server"] = cfg.ntp.server;
  ntp["utcOffsetMin"] = cfg.ntp.utcOffsetMin;
  ntp["autoDst"] = cfg.ntp.autoDst;

  JsonObject gpio = doc["gpio"].to<JsonObject>();
  gpio["relayPin"] = cfg.gpio.relayPin;
  gpio["relayActiveLow"] = cfg.gpio.relayActiveLow;
  gpio["relayActiveMs"] = cfg.gpio.relayActiveMs;
  gpio["buzzerPin"] = cfg.gpio.buzzerPin;
  gpio["statusLedPin"] = cfg.gpio.statusLedPin;
  gpio["doorSensorPin"] = cfg.gpio.doorSensorPin;
  gpio["doorSensorActiveLow"] = cfg.gpio.doorSensorActiveLow;
  gpio["exitButtonPin"] = cfg.gpio.exitButtonPin;

  JsonObject rfid = doc["rfid"].to<JsonObject>();
  rfid["type"] = cfg.rfid.type;
  rfid["hz1050RxPin"] = cfg.rfid.hz1050RxPin;
  rfid["hz1050TxPin"] = cfg.rfid.hz1050TxPin;
  rfid["hz1050Baud"] = cfg.rfid.hz1050Baud;
  rfid["mfrc522SckPin"] = cfg.rfid.mfrc522SckPin;
  rfid["mfrc522MisoPin"] = cfg.rfid.mfrc522MisoPin;
  rfid["mfrc522MosiPin"] = cfg.rfid.mfrc522MosiPin;
  rfid["mfrc522SdaPin"] = cfg.rfid.mfrc522SdaPin;
  rfid["mfrc522RstPin"] = cfg.rfid.mfrc522RstPin;

  JsonObject mqtt = doc["mqtt"].to<JsonObject>();
  mqtt["enabled"] = cfg.mqtt.enabled;
  mqtt["server"] = cfg.mqtt.server;
  mqtt["port"] = cfg.mqtt.port;
  mqtt["username"] = cfg.mqtt.username;
  mqtt["password"] = cfg.mqtt.password;
  mqtt["clientId"] = cfg.mqtt.clientId;
  mqtt["topicPrefix"] = cfg.mqtt.topicPrefix;
  mqtt["discoveryPrefix"] = cfg.mqtt.discoveryPrefix;

  JsonObject auth = doc["auth"].to<JsonObject>();
  auth["enabled"] = cfg.auth.enabled;
  auth["username"] = cfg.auth.username;
  auth["passwordHash"] = cfg.auth.passwordHash;
  auth["sessionTimeoutMin"] = cfg.auth.sessionTimeoutMin;

  JsonObject lcd = doc["lcd"].to<JsonObject>();
  lcd["enabled"] = cfg.lcd.enabled;
  lcd["sdaPin"] = cfg.lcd.sdaPin;
  lcd["sclPin"] = cfg.lcd.sclPin;
  lcd["address"] = cfg.lcd.address;
  lcd["cols"] = cfg.lcd.cols;
  lcd["rows"] = cfg.lcd.rows;

  return JsonFile::saveAtomic(kConfigPath, doc);
}
