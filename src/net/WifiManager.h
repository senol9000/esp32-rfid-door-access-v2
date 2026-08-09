#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include "config/Config.h"

/**
 * WiFi yöneticisi.
 *
 * - Yapılandırma yoksa AP moduna geçer (kurulum).
 * - Yapılandırma varsa STA modunda bağlanır, koparsa otomatik yeniden bağlanır.
 * - STA zaman aşımı olursa AP moduna düşer.
 * - Captive portal yok: AP'ye bağlanan cihazlar "internet yok" görür,
 *   tarayıcıya otomatik yönlendirme yapılmaz; 192.168.4.1 elle girilir.
 */
class WifiManager {
 public:
  enum class Mode : uint8_t { None, Sta, Ap };

  /** Kayıtlı ayarlara göre WiFi'yi başlatır. */
  void begin(AppConfig& cfg);

  /** Periyodik olarak çağrılmalı; bağlantı durumunu idare eder. */
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
  String apSsid_;
  unsigned long staStartedMs_ = 0;
  bool wasConnected_ = false;

  void startSta();
  void startAp();
};
