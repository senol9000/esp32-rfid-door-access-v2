#include "lcd/LcdManager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "config/Config.h"

static const char* kLcdTag = "LCD";

// ---------------------------------------------------------------------------
// Genel API
// ---------------------------------------------------------------------------

void LcdManager::begin(AppConfig& cfg) {
  cfg_ = &cfg;
  statusL1_.clear();
  statusL2_.clear();
  overlayL1_.clear();
  overlayL2_.clear();
  overlayUntilMs_ = 0;
  drawnL1_.clear();
  drawnL2_.clear();

  if (!cfg_->lcd.enabled) {
    present_ = false;
    initialized_ = false;
    Serial.println("[LCD] Devre dışı (lcd.enabled=false).");
    return;
  }

  detect();
  if (present_) {
    showBoot();
    render();
  }
}

void LcdManager::loop() {
  // render() içerik değişmediyse hiç I2C trafiği üretmez; overlay süresi
  // dolduğunda bir kez duruma geri döner. Bu yüzden her döngüde ucuzdur.
  render();
}

void LcdManager::applyConfig(AppConfig& cfg) {
  cfg_ = &cfg;
  if (!cfg_->lcd.enabled) {
    present_ = false;
    initialized_ = false;
    return;
  }
  // Zaten algılanmış ve istenen ayarlarla uyuşuyorsa yeniden taramaya gerek yok.
  const bool same =
      (cfg_->lcd.sdaPin < 0 || cfg_->lcd.sdaPin == sda_) &&
      (cfg_->lcd.sclPin < 0 || cfg_->lcd.sclPin == scl_) &&
      (cfg_->lcd.address == 0 || cfg_->lcd.address == address_);
  if (present_ && initialized_ && same) {
    return;
  }
  detect();
}

// ---------------------------------------------------------------------------
// Otomatik algılama (pin + I2C adres taraması)
// ---------------------------------------------------------------------------

bool LcdManager::detect() {
  present_ = false;
  initialized_ = false;
  if (!cfg_ || !cfg_->lcd.enabled) {
    return false;
  }

  int sda = cfg_->lcd.sdaPin;
  int scl = cfg_->lcd.sclPin;
  uint8_t addr = 0;

  // En yaygın ESP32 I2C pin çiftleri; kullanımda olanlar atlanır.
  static const int kPairs[][2] = {
      {21, 22}, {18, 19}, {4, 5},   {16, 17}, {26, 27},
      {32, 33}, {23, 25}, {13, 14}, {19, 23}, {27, 14}};

  // 1) Kullanıcı açıkça pin belirtmişse önce onu dene.
  if (sda >= 0 && scl >= 0) {
    tryPinPair(sda, scl, addr);
  }

  // 2) Aday pin çiftlerini tara.
  if (addr == 0) {
    for (const auto& p : kPairs) {
      if (p[0] == p[1]) continue;
      if (isBusyPin(p[0]) || isBusyPin(p[1])) continue;
      if (tryPinPair(p[0], p[1], addr)) {
        sda = p[0];
        scl = p[1];
        break;
      }
    }
  }

  if (addr == 0) {
    Serial.printf("[%s] I2C LCD bulunamadı (tarama tamamlandı).\n", kLcdTag);
    return false;
  }

  // Bulunan değerleri yapılandırmaya yaz; sonraki açılışta hızlı başlar.
  sda_ = sda;
  scl_ = scl;
  address_ = addr;
  cfg_->lcd.sdaPin = sda;
  cfg_->lcd.sclPin = scl;
  cfg_->lcd.address = addr;
  ConfigManager::save(*cfg_);

  Wire.begin(sda_, scl_);
  Wire.setClock(100000);  // 100 kHz: uzun kablo/breadboard için kararlı
  initLcd();
  present_ = true;
  Serial.printf("[%s] LCD bulundu: SDA=%d SCL=%d adres=0x%02X\n", kLcdTag, sda_, scl_,
                address_);
  return true;
}

bool LcdManager::tryPinPair(int sda, int scl, uint8_t& outAddr) {
  Wire.begin(sda, scl);
  Wire.setClock(100000);
  // Öncelikli PCF8574 / PCF8574A adresleri (16x2 backback modülleri)
  static const uint8_t kPriority[] = {0x27, 0x3F, 0x20, 0x21, 0x22, 0x23,
                                      0x24, 0x25, 0x26, 0x38, 0x39, 0x3A,
                                      0x3B, 0x3C, 0x3D, 0x3E, 0x07, 0x08};
  for (uint8_t a : kPriority) {
    if (probeAddress(a)) {
      outAddr = a;
      return true;
    }
  }
  // Tam tarama (0x03..0x77)
  for (uint8_t a = 0x03; a <= 0x77; a++) {
    if (probeAddress(a)) {
      outAddr = a;
      return true;
    }
  }
  return false;
}

bool LcdManager::probeAddress(uint8_t addr) {
  Wire.beginTransmission(addr);
  Wire.write(0x00);
  return Wire.endTransmission() == 0;  // ACK alındıysa cihaz var
}

bool LcdManager::isBusyPin(int pin) const {
  if (pin < 0 || pin > 39) return true;
  // Strapping / özel kullanımlı pinler (flash, UART, ADC girişi vb.)
  static const int kReserved[] = {0, 1, 3, 6,  7,  8,  9,  10, 11,
                                  12, 15, 34, 35, 36, 37, 38, 39};
  for (int r : kReserved) {
    if (pin == r) return true;
  }
  if (!cfg_) return false;

  // GPIO yapılandırmasındaki pinler
  const GpioConfig& g = cfg_->gpio;
  const int used[] = {g.relayPin, g.buzzerPin, g.statusLedPin, g.doorSensorPin,
                      g.exitButtonPin};
  for (int u : used) {
    if (u >= 0 && u == pin) return true;
  }

  // RFID okuyucu pinleri (tip neyse o set; auto ise tümü korunur)
  const RfidConfig& r = cfg_->rfid;
  if (r.type == "hz1050") {
    if (pin == r.hz1050RxPin || pin == r.hz1050TxPin) return true;
  } else if (r.type == "mfrc522") {
    if (pin == r.mfrc522SckPin || pin == r.mfrc522MisoPin ||
        pin == r.mfrc522MosiPin || pin == r.mfrc522SdaPin ||
        pin == r.mfrc522RstPin)
      return true;
  } else {
    const int rp[] = {r.hz1050RxPin,  r.hz1050TxPin,   r.mfrc522SckPin,
                      r.mfrc522MisoPin, r.mfrc522MosiPin, r.mfrc522SdaPin,
                      r.mfrc522RstPin};
    for (int u : rp) {
      if (u >= 0 && u == pin) return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// HD44780 / PCF8574 sürücüsü (4-bit mod)
// ---------------------------------------------------------------------------

void LcdManager::writeNibble(uint8_t nibble, bool rs) {
  // PCF8574 çıkış düzeni: P7=BL, P6=E, P5=RW, P4=RS, P3..P0=D7..D4
  uint8_t out = (nibble << 4) | (rs ? 0x10 : 0x00) | (backlightOn_ ? 0x80 : 0x00);
  Wire.beginTransmission(address_);
  Wire.write(out | 0x40);  // E yüksek
  Wire.endTransmission();
  Wire.beginTransmission(address_);
  Wire.write(out & ~0x40);  // E düşük
  Wire.endTransmission();
}

void LcdManager::sendByte(uint8_t value, bool rs) {
  writeNibble(value >> 4, rs);
  writeNibble(value & 0x0F, rs);
}

void LcdManager::command(uint8_t cmd) { sendByte(cmd, false); }

void LcdManager::data(uint8_t d) { sendByte(d, true); }

void LcdManager::clearDisplay() {
  command(0x01);  // Clear display (~1.5 ms)
  vTaskDelay(pdMS_TO_TICKS(2));
}

void LcdManager::setCursor(uint8_t col, uint8_t row) {
  command(0x80 | (row == 1 ? 0x40 : 0x00) | (col & 0x0F));
}

void LcdManager::printText(const String& text) {
  String s = text;
  // Türkçe karakterleri ASCII karşılıklarına çevir (HD44780 ROM'u yok)
  s.replace("Ş", "S");
  s.replace("ş", "s");
  s.replace("İ", "I");
  s.replace("ı", "i");
  s.replace("Ğ", "G");
  s.replace("ğ", "g");
  s.replace("Ö", "O");
  s.replace("ö", "o");
  s.replace("Ü", "U");
  s.replace("ü", "u");
  s.replace("Ç", "C");
  s.replace("ç", "c");
  for (size_t i = 0; i < s.length(); i++) {
    const uint8_t c = (uint8_t)s[i];
    if (c >= 0x80) continue;  // UTF-8 devam baytlarını atla
    data(c);
  }
}

void LcdManager::initLcd() {
  // Standart 4-bit başlatma dizisi (HD44780 veri sayfası)
  vTaskDelay(pdMS_TO_TICKS(50));  // Güç açılış beklemesi
  writeNibble(0x03, false);
  vTaskDelay(pdMS_TO_TICKS(5));
  writeNibble(0x03, false);
  vTaskDelay(pdMS_TO_TICKS(5));
  writeNibble(0x03, false);
  vTaskDelay(pdMS_TO_TICKS(2));
  writeNibble(0x02, false);  // 4-bit moda geç
  vTaskDelay(pdMS_TO_TICKS(2));
  command(0x28);  // 2 satır, 5x8, 4-bit
  command(0x0C);  // Ekran açık, imleç kapalı, yanıp sönme yok
  command(0x06);  // Giriş modu: sağa, imleci oynatma
  clearDisplay();
  initialized_ = true;
}

// ---------------------------------------------------------------------------
// İçerik yönetimi
// ---------------------------------------------------------------------------

void LcdManager::setStatus(const String& line1, const String& line2) {
  statusL1_ = line1;
  statusL2_ = line2;
  // overlay süresi bitmişse bir sonraki render'da yeni durum çizilir
  if (millis() >= overlayUntilMs_) {
    render();
  }
}

void LcdManager::showAccess(const String& uid, const String& name, bool allowed) {
  if (!present_) return;
  overlayL1_ = allowed ? "ERISIM IZIN VERILDI" : "ERISIM REDDEDILDI";
  overlayL2_ = name.length() ? name : uid;
  overlayUntilMs_ = millis() + 4000;  // 4 sn göster
  render();
}

void LcdManager::showTest() {
  if (!present_) return;
  overlayL1_ = "LCD TEST";
  overlayL2_ = String("OK S") + sda_ + "/" + scl_ + " 0x" + String(address_, HEX);
  overlayUntilMs_ = millis() + 3000;
  render();
}

void LcdManager::showBoot() {
  if (!present_) return;
  overlayL1_ = "KAPI SISTEMI";
  overlayL2_ = "Baslatiliyor...";
  overlayUntilMs_ = millis() + 3000;
  render();
}

void LcdManager::render() {
  if (!present_ || !initialized_ || !cfg_) return;
  const bool overlay = millis() < overlayUntilMs_;
  String l1 = overlay ? overlayL1_ : statusL1_;
  String l2 = overlay ? overlayL2_ : statusL2_;
  if (l1 == drawnL1_ && l2 == drawnL2_) return;

  // Satırları sabit uzunluğa getir (önceki kalıntıları temizle)
  while ((int)l1.length() < cfg_->lcd.cols) l1 += ' ';
  while ((int)l2.length() < cfg_->lcd.cols) l2 += ' ';
  if ((int)l1.length() > cfg_->lcd.cols) l1 = l1.substring(0, cfg_->lcd.cols);
  if ((int)l2.length() > cfg_->lcd.cols) l2 = l2.substring(0, cfg_->lcd.cols);

  setCursor(0, 0);
  printText(l1);
  setCursor(0, 1);
  printText(l2);
  drawnL1_ = l1;
  drawnL2_ = l2;
}
