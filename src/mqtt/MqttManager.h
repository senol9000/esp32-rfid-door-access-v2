#pragma once

#include <Arduino.h>
#include <functional>
#include <PubSubClient.h>
#include <WiFi.h>
#include "config/Config.h"

// İleri bildirimler (yöneticilere referans olarak bağlanır).
class WifiManager;
class NtpManager;
class GpioManager;
class EventLog;

/**
 * MQTT yöneticisi.
 *
 * - PubSubClient ile broker bağlantısı (LWT ile online/offline bildirimi).
 * - Publish: kapı durumu, son kart/kullanıcı, izin/red, RSSI, heap, IP, uptime.
 * - Subscribe: kapıyı aç, restart, buzzer, LED, config reload.
 * - Home Assistant Auto Discovery (sensor / binary_sensor / button / switch).
 *
 * Tüm işlemler non-blocking; reconnect arka planda zamanlayıcı ile yapılır.
 */
class MqttManager {
 public:
  /** Komut topic'lerinden gelen mesajları işlemek için callback. */
  using CommandCallback = std::function<void(const String& topic, const String& payload)>;

  void begin(AppConfig& cfg, WifiManager& wifi, NtpManager& ntp,
             GpioManager& gpio, EventLog& events);
  void loop();

  /** Yeni MQTT ayarlarını uygular (bağlantıyı sıfırlar). */
  void applyConfig(AppConfig& cfg);

  /** MQTT etkin mi (config'den) ve bağlı mı? */
  bool isEnabled() const { return cfg_ && cfg_->mqtt.enabled; }
  bool isConnected() { return client_.connected(); }

  /** Kapı/erişim olaylarını publish eder (AccessEngine callback'i). */
  void publishAccess(const char* result, const char* uid, const char* name,
                     const char* reason);

  /** Kapı durumunu publish eder. */
  void publishDoorState();

  /** Periyodik telemetri (RSSI/heap/IP/uptime). */
  void publishTelemetry();

  /** Komut topic'lerinden gelen mesajlar için callback atar. */
  void setCommandCallback(CommandCallback cb) { onCommand_ = cb; }

  /** Home Assistant discovery yapılandırma mesajlarını yayınlar. */
  void publishDiscovery();

 private:
  AppConfig* cfg_ = nullptr;
  WifiManager* wifi_ = nullptr;
  NtpManager* ntp_ = nullptr;
  GpioManager* gpio_ = nullptr;
  EventLog* events_ = nullptr;

  WiFiClient net_;
  PubSubClient client_;
  CommandCallback onCommand_;
  unsigned long lastReconnectMs_ = 0;
  unsigned long lastTelemetryMs_ = 0;
  bool lastDoorState_ = false;

  // Topic yardımcıları
  String topic(const char* suffix) const;
  String cmdTopic(const char* suffix) const;

  void connect();
  void subscribeAll();
  static void onMessage(char* topic, byte* payload, unsigned int length);
  void handleMessage(const String& t, const String& p);
  void publishHaConfig(const char* platform, const char* objectId,
                       const String& name, const String& stateTopic,
                       const char* deviceClass, const char* unit,
                       const char* commandTopic, const char* payloadOn,
                       const char* payloadOff, const char* valueTemplate);
};
