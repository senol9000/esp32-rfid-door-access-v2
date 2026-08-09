#pragma once

#include <Arduino.h>
#include <ping/ping_sock.h>
#include "config/Config.h"

class WifiManager;
class EventLog;

/**
 * Ping watchdog modülü.
 *
 * İnternet erişimini ICMP ping ile sürekli denetler:
 * - STA modunda ve WiFi bağlıyken periyodik olarak hedefe ping atar.
 * - Ardışık failCountBeforeReconnect başarısız ping'de WiFi'yi yeniden bağlatır
 *   (WifiManager::applyConfig).
 * - Bu işlem reconnectLimit kez tekrarlanıp hâlâ çözülemezse cihazı yeniden
 *   başlatır (ESP.restart) — en kesin kurtarma yöntemi.
 * - AP kurulum modunda veya WiFi bağlı değilken pasiftir.
 *
 * esp_ping (lwIP) API'si kendi FreeRTOS task'ında çalışır; bu modül yalnızca
 * oturum başlatır/bitirir ve sonuçları App::loop içinde işler. Hiçbir yerde
 * delay() kullanılmaz, hiçbir task bloklanmaz.
 */
class PingWatchdog {
 public:
  /** Yapılandırmayı bağlar. events isteğe bağlıdır (olay loguna yazar). */
  void begin(AppConfig& cfg, WifiManager& wifi, EventLog* events);

  /** Periyodik olarak çağrılmalı (App::loop); ping oturumunu ve sonuçları yönetir. */
  void loop();

  /** Yapılandırma değiştiğinde değerleri yeniden okur ve oturumu sıfırlar. */
  void applyConfig();

  bool isEnabled() const { return enabled_; }
  bool isOnline() const { return online_; }
  int consecutiveFails() const { return consecutiveFails_; }
  int reconnectCount() const { return reconnects_; }
  unsigned long lastCheckMs() const { return lastCheckMs_; }

 private:
  AppConfig* cfg_ = nullptr;
  WifiManager* wifi_ = nullptr;
  EventLog* events_ = nullptr;

  bool enabled_ = false;
  bool online_ = true;          // Son ping sonucu (kabaca durum göstergesi)
  bool pingActive_ = false;     // Bir ping oturumu şu anda çalışıyor mu?
  volatile bool pingDone_ = false;    // Ping oturumu bitti (ping task'ından set)
  volatile bool lastReplyOk_ = false; // Son ping yanıtı geldi mi
  esp_ping_handle_t pingHandle_ = nullptr;
  int consecutiveFails_ = 0;    // Ardışık başarısız ping sayısı
  int reconnects_ = 0;          // Bu döngüde yapılan WiFi yeniden bağlanma sayısı
  unsigned long lastCheckMs_ = 0;

  bool resolveTarget(ip_addr_t& out);
  void startPing();
  void stopSession();
  void handleResult();

  // esp_ping task bağlamında çağrılan statik callback'ler
  static void onPingSuccess(esp_ping_handle_t hdl, void* args);
  static void onPingTimeout(esp_ping_handle_t hdl, void* args);
  static void onPingEnd(esp_ping_handle_t hdl, void* args);
};
