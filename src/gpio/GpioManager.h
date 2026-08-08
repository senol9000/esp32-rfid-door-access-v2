#pragma once

#include <Arduino.h>
#include "config/Config.h"

/**
 * Kapı donanım yöneticisi.
 *
 * Röle, buzzer, durum LED'i, kapı sensörü (manyetik kontak) ve içeriden
 * çıkış butonunu yönetir. Tüm davranışlar non-blocking'dir; hiçbir yerde
 * delay() kullanılmaz, zamanlamalar millis() tabanlı zaman damgalarıyla
 * yapılır (watchdog / FreeRTOS dostu).
 *
 * Röle mantığı:
 *  - relayActiveLow == false  => normalde LOW (pasif), aktifken HIGH (NO röle)
 *  - relayActiveLow == true   => normalde HIGH (pasif), aktifken LOW  (NC röle)
 *
 * Kapı sensörü:
 *  - doorSensorActiveLow == true  => kapı açıkken LOW okunur
 *  - doorSensorActiveLow == false => kapı açıkken HIGH okunur
 */
class GpioManager {
 public:
  /** Buzzer melodi adımı: onMs bip süresi, offMs sonraki adıma kadar bekleme. */
  struct MelodyStep {
    unsigned short onMs;
    unsigned short offMs;
  };

  /** GPIO pinlerini kurar ve pinMode atamalarını yapar. */
  void begin(const GpioConfig& cfg);

  /** Her döngüde çağrılır; zaman aşımı kontrollerini işler. */
  void loop();

  /** Yapılandırmayı günceller ve pinleri yeniden kurar. */
  void applyConfig(const GpioConfig& cfg);

  /**
   * Röleyi belirtilen süre boyunca aktif eder (kapıyı açar).
   * Daha önce aktif bir süre varsa yeniden başlatılır.
   */
  void openDoor(int durationMs);

  /** Röleyi anında pasif konuma getirir. */
  void closeDoorNow();

  /** Rölenin o anki aktif/pasif durumunu döndürür. */
  bool isDoorOpen() const;

  /** Buzzer'ı belirtilen süre (ms) boyunca çaldırır (tek bip). */
  void beep(int durationMs = 200);

  /** Başarılı kart (izin verildi) melodisi: çift kısa bip. */
  void beepSuccess();

  /** Reddedilen kart melodisi: üç hızlı kısa bip. */
  void beepDenied();

  /** Manuel kapı açma (web/MQTT open) melodisi: uzun tek bip. */
  void beepOpen();

  /** Durum LED'ini belirtilen süre boyunca yakıp söndürür. */
  void blinkLed(int durationMs = 500);

  /** Kapı sensörüne göre kapının fiziksel olarak açık olup olmadığı. */
  bool isDoorOpenPhysical() const;

  /** Çıkış butonunun o an basılı olup olmadığı. */
  bool isExitButtonPressed() const;

  /** GpioConfig'in geçerli pin seti ile karşılaştırılıp değiştiğini söyler. */
  bool configChanged(const GpioConfig& cfg) const;

 private:
  const GpioConfig* cfg_ = nullptr;

  // Röle durumu
  bool doorOpen_ = false;
  unsigned long doorOpenUntilMs_ = 0;

  // Buzzer durumu
  bool buzzerOn_ = false;

  // Buzzer melodi durumu (ardışık bip-on/bip-off adımları, non-blocking)
  const MelodyStep* melody_ = nullptr;
  int melodyLen_ = 0;
  int melodyIdx_ = 0;
  unsigned long melodyStepStartMs_ = 0;
  bool melodyRunning_ = false;

  // LED durumu
  bool ledOn_ = false;
  unsigned long ledUntilMs_ = 0;

  // Çıkış butonu debounce durumu
  bool exitLastRaw_ = false;
  bool exitWasPressed_ = false;
  unsigned long exitDebounceMs_ = 0;
  static const unsigned long kExitDebounceWindowMs = 60;  // 60 ms stabil okuma

  void updateRelayOutput();
  void updateBuzzerOutput();
  void updateLedOutput();
  void startMelody(const MelodyStep* steps, int len);
  void stopMelody();
  int  activeValue(bool activeLow) const;  // aktif mantığa göre HIGH/LOW döndürür
};
