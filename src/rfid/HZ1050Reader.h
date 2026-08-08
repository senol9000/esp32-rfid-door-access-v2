#pragma once

#include "rfid/IRfidReader.h"
#include "config/Config.h"

/**
 * HZ1050 RFID okuyucu (UART).
 *
 * HZ1050, okunan kartın UID'sini seri hattan ASCII olarak gönderir.
 * Yaygın format: 10 haneli ondalık sayı + CR/LF (örn. "0009527911\r\n").
 * Bu sınıf satırı okur, ondalık ise hex'e çevirir (büyük harf), değilse
 * olduğu gibi normalize eder. Baud ve RX/TX pinleri web üzerinden ayarlanır.
 */
class HZ1050Reader final : public IRfidReader {
 public:
  explicit HZ1050Reader(const RfidConfig& cfg) : cfg_(&cfg) {}

  bool begin() override;
  bool available() override;
  String readUid() override;
  const char* name() const override { return "hz1050"; }

 private:
  const RfidConfig* cfg_;
  bool started_ = false;
  String line_;       // birikmiş satır verisi
  unsigned long lineStartMs_ = 0;
  bool linePending_ = false;
  String pendingUid_;  // parse edilip okunmayı bekleyen UID
  static const size_t kMaxLine = 32;          // HZ1050 UID satırı kısa olur
  static const unsigned long kLineTimeoutMs = 150;  // 150ms içinde satır sonu

  void parseAndStore(const String& raw);
  static String decimalToHex(const String& dec);
};
