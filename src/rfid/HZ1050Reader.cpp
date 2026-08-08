#include "rfid/HZ1050Reader.h"
#include <HardwareSerial.h>

bool HZ1050Reader::begin() {
  if (started_) {
    return true;
  }
  if (cfg_->hz1050RxPin < 0) {
    return false;
  }
  // Okuyucu ESP32'nin 3.3V TTL seri hattına bağlanır.
  // UART2 kullanılır; RX pini okuyucunun TX ucuna gider.
  const int rx = cfg_->hz1050RxPin;
  const int tx = cfg_->hz1050TxPin >= 0 ? cfg_->hz1050TxPin : -1;
  Serial2.begin(cfg_->hz1050Baud, SERIAL_8N1, rx, tx);
  started_ = true;
  Serial.printf("[RFID] HZ1050 başlatıldı (UART2 RX=%d TX=%d baud=%ld).\n",
                rx, tx, cfg_->hz1050Baud);
  return true;
}

bool HZ1050Reader::available() {
  if (!started_) {
    return false;
  }
  if (!pendingUid_.isEmpty()) {
    return true;
  }
  const unsigned long now = millis();

  while (Serial2.available() > 0) {
    const char c = (char)Serial2.read();
    if (line_.length() == 0) {
      lineStartMs_ = now;
      linePending_ = true;
    }
    if (c == '\n' || c == '\r') {
      if (line_.length() > 0) {
        parseAndStore(line_);
        line_ = "";
        linePending_ = false;
        return true;
      }
      line_ = "";  // boş satırı yoksay
      linePending_ = false;
      continue;
    }
    if (line_.length() < kMaxLine) {
      line_ += c;
    }
    linePending_ = true;
  }

  // Satır sonu karakteri gelmeden satır yarım kaldıysa ve zaman aşımı olduysa,
  // birikmiş veriyi yine de işle (bazı modeller CRLF göndermeyebilir).
  if (linePending_ && line_.length() > 0 && (now - lineStartMs_ > kLineTimeoutMs)) {
    parseAndStore(line_);
    line_ = "";
    linePending_ = false;
    return true;
  }
  return false;
}

String HZ1050Reader::readUid() {
  // Bekleyen UID'yi döndürür ve temizler.
  if (pendingUid_.isEmpty()) {
    return String();
  }
  const String uid = pendingUid_;
  pendingUid_ = "";
  return uid;
}

void HZ1050Reader::parseAndStore(const String& raw) {
  String clean;
  clean.reserve(raw.length());
  for (size_t i = 0; i < raw.length(); i++) {
    const char c = raw.charAt(i);
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
      clean += c;
    }
  }
  if (clean.length() == 0) {
    return;
  }

  // HZ1050 ondalık gönderir; hex olarak saklamak için dönüştür.
  bool allDigits = true;
  for (size_t i = 0; i < clean.length(); i++) {
    if (clean.charAt(i) < '0' || clean.charAt(i) > '9') {
      allDigits = false;
      break;
    }
  }
  String uid = allDigits ? decimalToHex(clean) : clean;
  uid.toUpperCase();
  Serial.printf("[RFID] HZ1050 UID: %s (ham: %s)\n", uid.c_str(), raw.c_str());
  // UID'yi interface'e iletmek için bir kuyruk olarak tek elemanlı tampon kullanıyoruz.
  // (Bu sınıf basit olduğundan bir sonraki available() çağrısına kadar saklanır.)
  pendingUid_ = uid;
}

String HZ1050Reader::decimalToHex(const String& dec) {
  uint64_t val = 0;
  for (size_t i = 0; i < dec.length(); i++) {
    val = val * 10 + (uint64_t)(dec.charAt(i) - '0');
  }
  char buf[17];
  snprintf(buf, sizeof(buf), "%llX", val);
  return String(buf);
}
