#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "config/Config.h"

/**
 * I2C 16x2 LCD (PCF8574 adaptörlü HD44780) yöneticisi.
 *
 * Özellikler:
 *  - Pin ve adres otomatik algılama: sdaPin/sclPin = -1 ve address = 0 iken
 *    aday pin çiftleri üzerinde I2C taraması yapar; PCF8574 ailesi (0x27/0x3F
 *    öncelikli) adreslerine ACK yoklayarak ekranı bulur.
 *  - Bulunan değerler /config.json'a yazılır; sonraki açılışta hızlı başlar.
 *  - Kullanımda olan GPIO'lar (röle, buzzer, LED, kapı sensörü, çıkış butonu,
 *    RFID pinleri) otomatik algılamada atlanır; çakışma oluşmaz.
 *  - Non-blocking: hiçbir yerde delay() kullanılmaz; init sırasında
 *    vTaskDelay ile zaman verilir (watchdog / FreeRTOS dostu).
 *
 * Harici kütüphane kullanılmaz: HD44780 4-bit mod protokolü PCF8574 üzerinden
 * Wire API ile doğrudan sürülür (flash bütçesini korur).
 */
class LcdManager {
 public:
  /** Yapılandırmayı uygular; otomatik algılama gerekirse çalıştırır. */
  void begin(AppConfig& cfg);

  /** Her döngüde çağrılır; zaman aşımı olan içeriklerin dönüşünü işler. */
  void loop();

  /** Yeni yapılandırmayı uygular (web panelinden değişiklik sonrası). */
  void applyConfig(AppConfig& cfg);

  /** Otomatik algılamayı yeniden çalıştırır (web "Otomatik Algıla" butonu). */
  bool detect();

  /** Ekran algılandı ve hazır mı? */
  bool isPresent() const { return present_; }

  /** Bulunan / ayarlanan pin ve adres bilgileri. */
  int sdaPin() const { return sda_; }
  int sclPin() const { return scl_; }
  uint8_t address() const { return address_; }

  /** Kalıcı durum satırları (App tarafından periyodik güncellenir). */
  void setStatus(const String& line1, const String& line2);

  /** Geçiş sonucu: belirli süre gösterilir, sonra durum satırlarına döner. */
  void showAccess(const String& uid, const String& name, bool allowed);

  /** Test ekranı: birkaç saniye gösterilir. */
  void showTest();

  /** Başlangıç mesajı. */
  void showBoot();

 private:
  AppConfig* cfg_ = nullptr;

  bool present_ = false;
  bool initialized_ = false;
  int sda_ = -1;
  int scl_ = -1;
  uint8_t address_ = 0x27;
  bool backlightOn_ = true;

  // Durum ve geçici (overlay) içerik
  String statusL1_, statusL2_;
  String overlayL1_, overlayL2_;
  unsigned long overlayUntilMs_ = 0;
  String drawnL1_, drawnL2_;

  // I2C taraması
  bool isBusyPin(int pin) const;
  bool probeAddress(uint8_t addr);
  bool tryPinPair(int sda, int scl, uint8_t& outAddr);

  // HD44780 sürücüsü
  void writeNibble(uint8_t nibble, bool rs);
  void sendByte(uint8_t value, bool rs);
  void command(uint8_t cmd);
  void data(uint8_t d);
  void clearDisplay();
  void setCursor(uint8_t col, uint8_t row);
  void printText(const String& text);
  void initLcd();
  void render();
};
