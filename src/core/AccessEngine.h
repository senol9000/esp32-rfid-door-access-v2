#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>
#include "core/UserManager.h"
#include "log/AccessLog.h"
#include "gpio/GpioManager.h"
#include "scheduler/TimeSchedule.h"
#include "scheduler/HolidayManager.h"
#include "ntp/NtpManager.h"

/**
 * Erişim karar motoru.
 *
 * RFID okuma -> kullanıcı bulma -> zaman kuralları -> tatil kontrolü ->
 * karar (Allowed/Denied) -> kapı açma + loglama + istatistik.
 *
 * Karar akışı:
 *   1. UID bilinen bir kart değilse            -> Denied (kart bulunamadı)
 *   2. Kart pasif ise                          -> Denied (pasif)
 *   3. Admin ise                               -> Allowed (her zaman)
 *   4. Tatil günü ise                          -> Denied (tatil)
 *   5. Zaman planı günü/aralığı uygun değilse  -> Denied (saat dışı)
 *   6. Aksi halde                              -> Allowed
 */
class AccessEngine {
 public:
  struct Result {
    bool allowed = false;
    String uid;
    String name;
    String reason;      // insan okunur neden
    AccessReason code;  // makine okunur neden
  };

  /** Kapı açıldı/istatistik güncellendi olayı (MQTT/HA için). */
  std::function<void(const AccessEngine::Result&)> onDecision;

  AccessEngine(UserManager& users, TimeSchedule& schedule, HolidayManager& holidays,
               GpioManager& gpio, NtpManager& ntp, AccessLog& log)
      : users_(users), schedule_(schedule), holidays_(holidays),
        gpio_(gpio), ntp_(ntp), log_(log) {}

  void begin();
  void loop();

  /** Bir UID'nin geçiş isteğini değerlendirir. */
  Result decide(const String& uid, bool viaExitButton = false);

 private:
  UserManager& users_;
  TimeSchedule& schedule_;
  HolidayManager& holidays_;
  GpioManager& gpio_;
  NtpManager& ntp_;
  AccessLog& log_;

  Result grant(const Result& r, User& u, AccessRecord& rec);
};
