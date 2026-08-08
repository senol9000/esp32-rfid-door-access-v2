#include "mqtt/MqttManager.h"
#include <ArduinoJson.h>
#include "gpio/GpioManager.h"
#include "log/EventLog.h"
#include "net/WifiManager.h"
#include "ntp/NtpManager.h"

// Statik PubSubClient callback'inin örnek yöneticiye ulaşması için global
// işaretçi. Uygulamada yalnızca tek bir MqttManager örneği vardır.
MqttManager* g_mqttManager = nullptr;

// Yeniden bağlanma denemeleri arası bekleme (ms)
static const unsigned long kReconnectIntervalMs = 5000;
// Telemetri yayın periyodu (ms)
static const unsigned long kTelemetryIntervalMs = 30000;

void MqttManager::begin(AppConfig& cfg, WifiManager& wifi, NtpManager& ntp,
                        GpioManager& gpio, EventLog& events) {
  cfg_ = &cfg;
  wifi_ = &wifi;
  ntp_ = &ntp;
  gpio_ = &gpio;
  events_ = &events;
  g_mqttManager = this;  // statik callback için örnek işaretçisi
  client_.setClient(net_);
  client_.setCallback(onMessage);
  lastReconnectMs_ = 0;
  lastTelemetryMs_ = 0;
  Serial.println("[MQTT] Yönetici hazır.");
}

void MqttManager::applyConfig(AppConfig& cfg) {
  cfg_ = &cfg;
  if (client_.connected()) {
    client_.disconnect();
  }
  lastReconnectMs_ = 0;  // hemen yeniden bağlanmayı dene
}

void MqttManager::loop() {
  if (!isEnabled()) {
    return;
  }
  const unsigned long now = millis();

  if (!client_.connected()) {
    if (now - lastReconnectMs_ >= kReconnectIntervalMs) {
      lastReconnectMs_ = now;
      connect();
    }
    return;
  }

  client_.loop();

  // Kapı durumu değişimini anında yayınla
  const bool door = gpio_->isDoorOpen();
  if (door != lastDoorState_) {
    lastDoorState_ = door;
    publishDoorState();
  }

  // Periyodik telemetri
  if (now - lastTelemetryMs_ >= kTelemetryIntervalMs) {
    lastTelemetryMs_ = now;
    publishTelemetry();
  }
}

String MqttManager::topic(const char* suffix) const {
  return cfg_->mqtt.topicPrefix + String("/") + suffix;
}

String MqttManager::cmdTopic(const char* suffix) const {
  return topic("cmd/") + suffix;
}

void MqttManager::connect() {
  if (cfg_->mqtt.server.isEmpty()) {
    if (events_) {
      events_->add(EventType::MqttError, "MQTT sunucu adresi boş");
    }
    return;
  }

  client_.setServer(cfg_->mqtt.server.c_str(), cfg_->mqtt.port);

  const String willTopic = topic("status");
  const String clientId = cfg_->mqtt.clientId;

  bool ok;
  if (!cfg_->mqtt.username.isEmpty()) {
    ok = client_.connect(clientId.c_str(), cfg_->mqtt.username.c_str(),
                         cfg_->mqtt.password.c_str(), willTopic.c_str(), 1,
                         true, "offline");
  } else {
    ok = client_.connect(clientId.c_str(), willTopic.c_str(), 1, true,
                         "offline");
  }

  if (!ok) {
    if (events_) {
      events_->add(EventType::MqttError,
                   "MQTT bağlantı hatası (rc=" + String(client_.state()) + ")");
    }
    Serial.printf("[MQTT] Bağlantı hatası, rc=%d\n", client_.state());
    return;
  }

  lastDoorState_ = gpio_->isDoorOpen();
  client_.publish(willTopic.c_str(), "online", true);
  publishDoorState();
  publishTelemetry();
  publishDiscovery();
  subscribeAll();
  if (events_) {
    events_->add(EventType::MqttConnected,
                 "Broker'a bağlandı: " + cfg_->mqtt.server);
  }
  Serial.println("[MQTT] Broker'a bağlandı.");
}

void MqttManager::subscribeAll() {
  client_.subscribe(cmdTopic("door").c_str(), 0);
  client_.subscribe(cmdTopic("restart").c_str(), 0);
  client_.subscribe(cmdTopic("beep").c_str(), 0);
  client_.subscribe(cmdTopic("led").c_str(), 0);
  client_.subscribe(cmdTopic("reload").c_str(), 0);
}

void MqttManager::onMessage(char* topic, byte* payload, unsigned int length) {
  // Statik PubSubClient callback'i: örnek yöneticiye global işaretçi üzerinden ulaşılır.
  String t(topic);
  String p;
  p.reserve(length);
  for (unsigned int i = 0; i < length; i++) {
    p += (char)payload[i];
  }
  if (g_mqttManager) {
    g_mqttManager->handleMessage(t, p);
  }
}

void MqttManager::handleMessage(const String& t, const String& p) {
  // Komut topic'lerini özyinelemeli eşleştir: {prefix}/cmd/<komut>
  const String cmdPrefix = topic("cmd/");
  if (t.startsWith(cmdPrefix)) {
    const String cmd = t.substring(cmdPrefix.length());
    if (cmd == "door") {
      if (p.equalsIgnoreCase("open")) {
        gpio_->openDoor(cfg_ ? cfg_->gpio.relayActiveMs : 1000);
        gpio_->beepOpen();
      } else if (p.equalsIgnoreCase("close")) {
        gpio_->closeDoorNow();
      }
    } else if (cmd == "beep") {
      gpio_->beep();
    } else if (cmd == "led") {
      gpio_->blinkLed(3);
    } else if (cmd == "restart") {
      ESP.restart();
    } else if (cmd == "reload") {
      // App tarafından atanan callback varsa tetiklenir (ör. config yeniden yüklenir)
    }
  }
  if (onCommand_) {
    onCommand_(t, p);
  }
}

void MqttManager::publishAccess(const char* result, const char* uid,
                                const char* name, const char* reason) {
  if (!client_.connected()) {
    return;
  }
  JsonDocument doc;
  doc["result"] = result;
  doc["uid"] = uid;
  doc["name"] = name;
  doc["reason"] = reason;
  doc["ts"] = ntp_ ? (uint32_t)ntp_->now() : 0;
  doc["rssi"] = wifi_->getRssi();
  String out;
  serializeJson(doc, out);
  client_.publish(topic("access").c_str(), out.c_str());
  client_.publish(topic("last_result").c_str(), result, true);
}

void MqttManager::publishDoorState() {
  if (!client_.connected()) {
    return;
  }
  client_.publish(topic("door").c_str(), gpio_->isDoorOpen() ? "open" : "closed",
                  true);
}

void MqttManager::publishTelemetry() {
  if (!client_.connected()) {
    return;
  }
  JsonDocument doc;
  doc["rssi"] = wifi_->getRssi();
  doc["heap"] = ESP.getFreeHeap();
  doc["ip"] = wifi_->getIp();
  doc["uptime_ms"] = millis();
  doc["ntp_synced"] = ntp_->isSynced();
  String out;
  serializeJson(doc, out);
  client_.publish(topic("telemetry").c_str(), out.c_str());
}

// ---------------------------------------------------------------------------
// Home Assistant Auto Discovery
// ---------------------------------------------------------------------------

void MqttManager::publishHaConfig(const char* platform, const char* objectId,
                                  const String& name, const String& stateTopic,
                                  const char* deviceClass, const char* unit,
                                  const char* commandTopic,
                                  const char* payloadOn,
                                  const char* payloadOff,
                                  const char* valueTemplate) {
  if (!client_.connected()) {
    return;
  }
  const String base = cfg_->mqtt.discoveryPrefix + String("/") + platform +
                      String("/esp32door_") + objectId + "/config";
  const String uniqueId = String("esp32door_") + objectId;

  JsonDocument doc;
  JsonObject dev = doc["dev"].to<JsonObject>();
  dev["ids"] = "esp32door";
  dev["name"] = "ESP32 Kapı Kontrol";
  dev["mf"] = "ESP32";
  dev["sw"] = "1.0.0";

  doc["name"] = name;
  doc["unique_id"] = uniqueId;
  doc["state_topic"] = stateTopic;
  if (deviceClass && strlen(deviceClass)) {
    doc["device_class"] = deviceClass;
  }
  if (unit && strlen(unit)) {
    doc["unit_of_measurement"] = unit;
  }
  if (commandTopic && strlen(commandTopic)) {
    doc["command_topic"] = commandTopic;
  }
  if (payloadOn && strlen(payloadOn)) {
    doc["payload_on"] = payloadOn;
  }
  if (payloadOff && strlen(payloadOff)) {
    doc["payload_off"] = payloadOff;
  }
  if (valueTemplate && strlen(valueTemplate)) {
    doc["value_template"] = valueTemplate;
  }

  String out;
  serializeJson(doc, out);
  client_.publish(base.c_str(), out.c_str(), true);
}

void MqttManager::publishDiscovery() {
  if (!client_.connected()) {
    return;
  }

  const String doorTopic = topic("door");
  const String accessTopic = topic("access");
  const String teleTopic = topic("telemetry");
  const String statusTopic = topic("status");

  // Binary sensor: kapı durumu
  publishHaConfig("binary_sensor", "door", "Kapı", doorTopic, "door", "",
                  nullptr, nullptr, nullptr, nullptr);

  // Sensor: son sonuç / kart / kullanıcı / RSSI / heap
  publishHaConfig("sensor", "last_result", "Son Sonuç", topic("last_result"),
                  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
  publishHaConfig("sensor", "last_uid", "Son Kart UID", accessTopic, nullptr,
                  nullptr, nullptr, nullptr, nullptr,
                  "{{ value_json.uid }}");
  publishHaConfig("sensor", "last_user", "Son Kullanıcı", accessTopic, nullptr,
                  nullptr, nullptr, nullptr, nullptr,
                  "{{ value_json.name }}");
  publishHaConfig("sensor", "rssi", "WiFi RSSI", teleTopic, "signal_strength",
                  "dBm", nullptr, nullptr, nullptr, "{{ value_json.rssi }}");
  publishHaConfig("sensor", "heap", "Heap", teleTopic, nullptr, "B", nullptr,
                  nullptr, nullptr, "{{ value_json.heap }}");
  publishHaConfig("sensor", "ip", "IP Adresi", teleTopic, nullptr, nullptr,
                  nullptr, nullptr, nullptr, "{{ value_json.ip }}");

  // Button: kapıyı aç
  publishHaConfig("button", "open_door", "Kapıyı Aç", "", nullptr, nullptr,
                  cmdTopic("door").c_str(), "open", "close", nullptr);

  // Switch: sistem durumu (LWT tabanlı online/offline)
  publishHaConfig("binary_sensor", "status", "Sistem Durumu", statusTopic,
                  "connectivity", nullptr, nullptr, nullptr, nullptr, nullptr);
}
