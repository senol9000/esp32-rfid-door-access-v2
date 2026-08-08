#include "core/AccessEngine.h"
#include <time.h>

void AccessEngine::begin() {
  Serial.println("[ACC] Erişim karar motoru hazır.");
}

void AccessEngine::loop() {
  // Şu an için periyodik işlem yok; ileride otomatik kapatma vb. burada izlenebilir.
}

AccessEngine::Result AccessEngine::decide(const String& uid, bool viaExitButton) {
  Result r;
  r.uid = uid;
  r.code = AccessReason::None;
  (void)viaExitButton;  // exit butonu aynı karar akışını kullanır

  AccessRecord rec;
  rec.uid = uid;
  rec.door = 1;

  // 1. Kullanıcıyı bul
  User* u = users_.find(uid);
  if (!u) {
    r.name = "Bilinmiyor";
    r.code = AccessReason::CardNotFound;
    r.reason = "Kart bulunamadı";
    rec.name = r.name;
    rec.result = AccessResult::Denied;
    rec.reason = r.code;
    log_.add(rec);
    gpio_.beepDenied();
    if (onDecision) {
      onDecision(r);
    }
    return r;
  }

  r.name = u->fullName.isEmpty() ? u->cardName : u->fullName;
  rec.name = r.name;

  // 2. Kart aktif mi?
  if (!u->active) {
    r.code = AccessReason::CardInactive;
    r.reason = "Kart pasif";
    rec.result = AccessResult::Denied;
    rec.reason = r.code;
    log_.add(rec);
    gpio_.beepDenied();
    if (onDecision) {
      onDecision(r);
    }
    return r;
  }

  // 3. Admin her zaman geçebilir
  if (u->type == CardType::Admin) {
    return grant(r, *u, rec);
  }

  // 4. Tatil günü kontrolü
  if (holidays_.isTodayHoliday()) {
    r.code = AccessReason::Holiday;
    r.reason = "Tatil günü";
    rec.result = AccessResult::Denied;
    rec.reason = r.code;
    log_.add(rec);
    gpio_.beepDenied();
    if (onDecision) {
      onDecision(r);
    }
    return r;
  }

  // 5. Zaman planı kontrolü
  const time_t t = time(nullptr);
  struct tm tmv;
  localtime_r(&t, &tmv);

  // tm_wday: 0=Pazar..6=Cumartesi. Plan 0=Pazartesi..6=Pazar.
  const int planDay = (tmv.tm_wday + 6) % 7;
  const int minuteOfDay = tmv.tm_hour * 60 + tmv.tm_min;

  // NTP senkron değilken normal kartlar geçemez (güvenli taraf)
  if (t < 1600000000) {
    r.code = AccessReason::OutOfHours;
    r.reason = "Saat senkron değil";
    rec.result = AccessResult::Denied;
    rec.reason = r.code;
    log_.add(rec);
    gpio_.beepDenied();
    if (onDecision) {
      onDecision(r);
    }
    return r;
  }

  if (schedule_.has(uid)) {
    const WeeklySchedule& s = schedule_.get(uid);
    if (!schedule_.isAllowed(s, planDay, minuteOfDay)) {
      r.code = AccessReason::OutOfHours;
      r.reason = "Saat dışı";
      rec.result = AccessResult::Denied;
      rec.reason = r.code;
      log_.add(rec);
      gpio_.beepDenied();
      if (onDecision) {
        onDecision(r);
      }
      return r;
    }
  }
  // Zaman planı olmayan normal kartlar: kısıtlama yok (açık mod)

  // 6. İzin ver
  return grant(r, *u, rec);
}

AccessEngine::Result AccessEngine::grant(const Result& r, User& u, AccessRecord& rec) {
  Result out = r;
  out.allowed = true;
  out.code = AccessReason::Valid;
  out.reason = "İzin verildi";

  // Kapıyı aç (süre config'den alınır: 0 = varsayılan) + başarı melodisi
  gpio_.openDoor(0);
  gpio_.beepSuccess();

  // İstatistik güncelle (kullanıcı + log)
  users_.recordAccess(u, true);
  rec.name = out.name;
  rec.result = AccessResult::Allowed;
  rec.reason = out.code;
  log_.add(rec);

  if (onDecision) {
    onDecision(out);
  }
  return out;
}
