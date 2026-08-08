#pragma once

#include <Arduino.h>
#include <time.h>
#include "config/Config.h"

/**
 * NTP / saat yöneticisi.
 *
 * - SNTP ile otomatik senkron (boot'ta + her 6 saatte bir).
 * - TZ ortam değişkeni ile zaman dilimi; istenirse Avrupa yaz/kış saati kuralı.
 * - Senkron olduktan sonra internet kesilse bile ESP32 dahili RTC zamanı
 *   saymaya devam eder.
 */
class NtpManager {
 public:
  void begin(AppConfig& cfg);
  void loop();

  /** Yeni NTP ayarlarını uygular (sunucu/ofset/DST) ve yeniden senkronlar. */
  void applyConfig() {
    applyTimezone();
    sync();
  }

  /** Zaman senkron oldu mu? (gerçek epoch > 2020) */
  bool isSynced() const { return synced_; }

  /** Geçerli epoch (saniye). */
  time_t now() const { return time(nullptr); }

  /** Yerel zaman diliminde biçimlendirilmiş tarih/saat döner. */
  String currentDateTime(const char* fmt = "%Y-%m-%d %H:%M:%S") const;

 private:
  AppConfig* cfg_ = nullptr;
  bool synced_ = false;
  unsigned long syncStartedMs_ = 0;
  unsigned long nextSyncMs_ = 0;

  void applyTimezone();
  void sync();
};
