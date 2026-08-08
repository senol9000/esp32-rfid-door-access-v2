#include "ntp/NtpManager.h"
#include <sys/time.h>

void NtpManager::begin(AppConfig& cfg) {
  cfg_ = &cfg;
  syncStartedMs_ = millis();
  applyTimezone();
  sync();
}

void NtpManager::applyTimezone() {
  if (!cfg_) {
    return;
  }
  char tz[48];
  if (cfg_->ntp.autoDst) {
    // Avrupa yaz/kış saati kuralı: Mart son Pazar 03:00, Ekim son Pazar 04:00.
    // POSIX TZ biçimi: "EET-2" -> EET, UTC'den 2 saat ileride.
    strncpy(tz, "EET-2EEST,M3.5.0/3,M10.5.0/4", sizeof(tz) - 1);
    tz[sizeof(tz) - 1] = '\0';
  } else {
    // Sabit UTC ofseti. POSIX TZ işareti ters çevrilir:
    // GMT+3 istiyorsak "UTC-3" yazılmalıdır.
    const int hours = cfg_->ntp.utcOffsetMin / 60;
    snprintf(tz, sizeof(tz), "UTC%+d", -hours);
  }
  setenv("TZ", tz, 1);
  tzset();
  Serial.printf("[NTP] Saat dilimi: %s\n", tz);
}

void NtpManager::sync() {
  const char* server =
      cfg_ && !cfg_->ntp.server.isEmpty() ? cfg_->ntp.server.c_str() : "pool.ntp.org";
  // configTime çağrılır; zaman dilimi TZ üzerinden işlendiği için ofset 0 verilir.
  configTime(0, 0, server, "pool.ntp.org", "time.google.com");
  nextSyncMs_ = millis() + 6UL * 3600UL * 1000UL;  // 6 saatte bir yeniden senkron
  Serial.printf("[NTP] Senkron başlatıldı (sunucu: %s).\n", server);
}

void NtpManager::loop() {
  const unsigned long nowMs = millis();
  if (!synced_ && nowMs - syncStartedMs_ > 10000) {
    // 2020 yılından sonrası -> SNTP gerçek zamanı getirdi.
    if (time(nullptr) > 1600000000) {
      synced_ = true;
      Serial.printf("[NTP] Saat senkronize: %s\n", currentDateTime().c_str());
    }
  }
  if (synced_ && nowMs >= nextSyncMs_) {
    sync();
  }
}

String NtpManager::currentDateTime(const char* fmt) const {
  time_t t = time(nullptr);
  struct tm tmv;
  localtime_r(&t, &tmv);
  char buf[40];
  strftime(buf, sizeof(buf), fmt, &tmv);
  return String(buf);
}
