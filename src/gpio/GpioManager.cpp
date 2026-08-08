#include "gpio/GpioManager.h"

static const char* kTag = "GPIO";

// ---------------------------------------------------------------------------
// Buzzer melodileri: (onMs, offMs) adım dizileri — non-blocking çalınır.
// ---------------------------------------------------------------------------
// Başarılı kart: çift kısa bip (bip-bip)
static const GpioManager::MelodyStep kMelodySuccess[] = {
    {120, 80}, {120, 0},
};
// Reddedilen kart: üç hızlı kısa bip (bip-bip-bip)
static const GpioManager::MelodyStep kMelodyDenied[] = {
    {90, 70}, {90, 70}, {90, 0},
};
// Manuel kapı açma: uzun tek bip
static const GpioManager::MelodyStep kMelodyOpen[] = {
    {400, 0},
};

void GpioManager::begin(const GpioConfig& cfg) {
  cfg_ = &cfg;

  // Röle çıkışı
  if (cfg.relayPin >= 0) {
    pinMode(cfg.relayPin, OUTPUT);
    // Başlangıçta pasif durumda olmalı
    digitalWrite(cfg.relayPin, activeValue(cfg.relayActiveLow));
  }

  // Buzzer (pasif buzzer; dijital çıkış)
  if (cfg.buzzerPin >= 0) {
    pinMode(cfg.buzzerPin, OUTPUT);
    digitalWrite(cfg.buzzerPin, LOW);
  }

  // Durum LED'i (kart üzerindeki GPIO2 genelde yerleşik LED'dir)
  if (cfg.statusLedPin >= 0) {
    pinMode(cfg.statusLedPin, OUTPUT);
    digitalWrite(cfg.statusLedPin, LOW);
  }

  // Kapı sensörü (manyetik kontak) - giriş, pull-up iç direnç
  if (cfg.doorSensorPin >= 0) {
    pinMode(cfg.doorSensorPin, INPUT_PULLUP);
  }

  // Çıkış butonu - giriş, pull-up iç direnç. Buton GND'ye basılır => LOW = basılı.
  if (cfg.exitButtonPin >= 0) {
    pinMode(cfg.exitButtonPin, INPUT_PULLUP);
  }

  Serial.printf("[%s] Relay=%d ActiveLow=%d Dur=%dms | Buzzer=%d LED=%d Door=%d Exit=%d\n",
                kTag, cfg.relayPin, cfg.relayActiveLow, cfg.relayActiveMs,
                cfg.buzzerPin, cfg.statusLedPin, cfg.doorSensorPin, cfg.exitButtonPin);
}

void GpioManager::applyConfig(const GpioConfig& cfg) {
  begin(cfg);
}

void GpioManager::openDoor(int durationMs) {
  if (!cfg_) {
    return;
  }
  // Negatif veya 0 değer geldiğinde yapılandırmadaki süreyi kullan.
  if (durationMs <= 0) {
    durationMs = cfg_->relayActiveMs;
  }
  if (durationMs <= 0) {
    return;
  }
  doorOpen_ = true;
  doorOpenUntilMs_ = millis() + durationMs;
  updateRelayOutput();
  Serial.printf("[%s] Kapı açıldı (%d ms).\n", kTag, durationMs);
}

void GpioManager::loop() {
  const unsigned long now = millis();

  // Röle zaman aşımı: süre dolduysa kapat
  if (doorOpen_ && now >= doorOpenUntilMs_) {
    doorOpen_ = false;
    updateRelayOutput();
    Serial.printf("[%s] Röle süre doldu, kapı kapatıldı.\n", kTag);
  }

  // Buzzer melodisi: adım adım ilerle (bip açık/süre → bekle → sonraki adım)
  if (melodyRunning_ && melody_ != nullptr) {
    const unsigned long stepElapsed = now - melodyStepStartMs_;
    const MelodyStep& step = melody_[melodyIdx_];
    const bool shouldBeep = stepElapsed < step.onMs;
    if (shouldBeep != buzzerOn_) {
      buzzerOn_ = shouldBeep;
      updateBuzzerOutput();
    }
    if (stepElapsed >= (unsigned long)(step.onMs + step.offMs)) {
      melodyIdx_++;
      if (melodyIdx_ >= melodyLen_) {
        stopMelody();
      } else {
        melodyStepStartMs_ = now;
      }
    }
  }

  // LED zaman aşımı
  if (ledOn_ && now >= ledUntilMs_) {
    ledOn_ = false;
    updateLedOutput();
  }

  // Çıkış butonu debounce: basılı kaldığı sürece kapıyı açık tutmayı tetikle.
  // Exit butonu basılıyken kapı açık kalır; bırakınca kapanır (varsayılan davranış).
  const bool rawPressed = isExitButtonPressed();
  if (cfg_ && cfg_->exitButtonPin >= 0) {
    if (rawPressed != exitLastRaw_) {
      exitLastRaw_ = rawPressed;
      exitDebounceMs_ = now;
    } else if (rawPressed && (now - exitDebounceMs_) >= kExitDebounceWindowMs) {
      if (!exitWasPressed_) {
        exitWasPressed_ = true;
        openDoor(cfg_->relayActiveMs);  // basılı kaldıkça röleyi tetikle
        beepOpen();                     // çıkış butonuyla açma melodisi
      }
    } else if (!rawPressed) {
      exitWasPressed_ = false;
    }
  }
}

void GpioManager::closeDoorNow() {
  doorOpen_ = false;
  updateRelayOutput();
}

bool GpioManager::isDoorOpen() const {
  return doorOpen_;
}

void GpioManager::beep(int durationMs) {
  // Tek adımlık melodi ile çal (mevcut melodi varsa üzerine yaz)
  static MelodyStep single[] = {{200, 0}};
  single[0].onMs = (unsigned short)(durationMs > 0 ? durationMs : 200);
  startMelody(single, 1);
}

void GpioManager::beepSuccess() {
  startMelody(kMelodySuccess, sizeof(kMelodySuccess) / sizeof(kMelodySuccess[0]));
}

void GpioManager::beepDenied() {
  startMelody(kMelodyDenied, sizeof(kMelodyDenied) / sizeof(kMelodyDenied[0]));
}

void GpioManager::beepOpen() {
  startMelody(kMelodyOpen, sizeof(kMelodyOpen) / sizeof(kMelodyOpen[0]));
}

void GpioManager::startMelody(const MelodyStep* steps, int len) {
  melody_ = steps;
  melodyLen_ = len;
  melodyIdx_ = 0;
  melodyStepStartMs_ = millis();
  melodyRunning_ = true;
  buzzerOn_ = true;
  updateBuzzerOutput();
}

void GpioManager::stopMelody() {
  melodyRunning_ = false;
  melody_ = nullptr;
  melodyLen_ = 0;
  melodyIdx_ = 0;
  buzzerOn_ = false;
  updateBuzzerOutput();
}

void GpioManager::blinkLed(int durationMs) {
  ledOn_ = true;
  ledUntilMs_ = millis() + durationMs;
  updateLedOutput();
}

bool GpioManager::isDoorOpenPhysical() const {
  if (cfg_ == nullptr || cfg_->doorSensorPin < 0) {
    return false;
  }
  const int raw = digitalRead(cfg_->doorSensorPin);
  return cfg_->doorSensorActiveLow ? (raw == LOW) : (raw == HIGH);
}

bool GpioManager::isExitButtonPressed() const {
  if (cfg_ == nullptr || cfg_->exitButtonPin < 0) {
    return false;
  }
  // Buton GND'ye bağlı olduğundan basılınca LOW okunur (pull-up ile).
  return digitalRead(cfg_->exitButtonPin) == LOW;
}

bool GpioManager::configChanged(const GpioConfig& cfg) const {
  if (cfg_ == nullptr) {
    return true;
  }
  return cfg.relayPin != cfg_->relayPin ||
         cfg.relayActiveLow != cfg_->relayActiveLow ||
         cfg.relayActiveMs != cfg_->relayActiveMs ||
         cfg.buzzerPin != cfg_->buzzerPin ||
         cfg.statusLedPin != cfg_->statusLedPin ||
         cfg.doorSensorPin != cfg_->doorSensorPin ||
         cfg.doorSensorActiveLow != cfg_->doorSensorActiveLow ||
         cfg.exitButtonPin != cfg_->exitButtonPin;
}

// ---------------------------------------------------------------------------
// İç yardımcılar
// ---------------------------------------------------------------------------

// Aktif mantığa göre röle sürüş seviyesini üretir.
// Aktif değilken röle "pasif" seviyede durur; aktifken ters seviyede.
void GpioManager::updateRelayOutput() {
  if (cfg_ == nullptr || cfg_->relayPin < 0) {
    return;
  }
  const int inactive = activeValue(cfg_->relayActiveLow);  // aktif değilken yazılacak seviye
  const int active = (inactive == HIGH) ? LOW : HIGH;
  digitalWrite(cfg_->relayPin, doorOpen_ ? active : inactive);
}

void GpioManager::updateBuzzerOutput() {
  if (cfg_ == nullptr || cfg_->buzzerPin < 0) {
    return;
  }
  digitalWrite(cfg_->buzzerPin, buzzerOn_ ? HIGH : LOW);
}

void GpioManager::updateLedOutput() {
  if (cfg_ == nullptr || cfg_->statusLedPin < 0) {
    return;
  }
  digitalWrite(cfg_->statusLedPin, ledOn_ ? HIGH : LOW);
}

// activeLow mantığına göre "pasif" seviyeyi döndürür.
// Örn: relayActiveLow=false (NO röle) => pasif seviye LOW; true (NC) => HIGH.
int GpioManager::activeValue(bool activeLow) const {
  return activeLow ? HIGH : LOW;
}
