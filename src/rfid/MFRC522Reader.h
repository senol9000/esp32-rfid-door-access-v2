#pragma once

#include "rfid/IRfidReader.h"
#include "config/Config.h"
#include <MFRC522.h>

/**
 * MFRC522 RFID okuyucu (SPI).
 *
 * Standart RC522 modülü SPI üzerinden bağlanır. SS (SDA) ve RST pinleri
 * web üzerinden değiştirilebilir; SPI pinleri de ayarlanabilir.
 * Okunan UID hex (büyük harf) olarak normalize edilir.
 */
class MFRC522Reader final : public IRfidReader {
 public:
  explicit MFRC522Reader(const RfidConfig& cfg) : cfg_(&cfg) {}

  bool begin() override;
  bool available() override;
  String readUid() override;
  const char* name() const override { return "mfrc522"; }

 private:
  const RfidConfig* cfg_;
  bool started_ = false;
  String pendingUid_;
  MFRC522* mfrc_ = nullptr;

  static String bytesToHex(const byte* data, byte n);
};
