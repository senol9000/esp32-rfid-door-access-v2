#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <WiFi.h>
#include "config/Config.h"

/**
 * WiFi yöneticisi.
 *
 * - Yapılandırma yoksa AP moduna geçer (kurulum / captive portal).
 * - Yapılandırma varsa STA modunda bağlanır, koparsa otomatik yeniden bağlanır.
 * - STA zaman aşımı olursa AP moduna düşer.
 */
class WifiManager {
 public:
  enum class Mode : uint8_t { None, Sta, Ap };

  /** Kayıtlı ayarlara göre WiFi'yi başlatır. */
  void begin(AppConfig& cfg);

  /** Periyodik olarak çağrılmalı; bağlantı ve captive portal DNS'i idare eder. */
  void loop();

  /** Yapılandırma değiştiğinde yeniden bağlanmak için çağrılır. */
  void applyConfig();

  Mode mode() const { return mode_; }
  bool isApMode() const { return mode_ == Mode::Ap; }
  bool isStaConnected() const { return mode_ == Mode::Sta && WiFi.status() == WL_CONNECTED; }

  /** Cihazın geçerli IP adresi (STA veya AP). */
  String getIp() const;

  int32_t getRssi() const { return WiFi.RSSI(); }
  String apSsid() const { return apSsid_; }

 private:
  AppConfig* cfg_ = nullptr;
  Mode mode_ = Mode::None;
  DNSServer dns_;
  String apSsid_;
  unsigned long staStartedMs_ = 0;
  bool wasConnected_ = false;

  void startSta();
  void startAp();
};
