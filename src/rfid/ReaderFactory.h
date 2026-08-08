#pragma once

#include <Arduino.h>
#include <memory>
#include "rfid/IRfidReader.h"
#include "config/Config.h"

/**
 * RFID okuyucu fabrikası.
 *
 * Config'deki tip değerine göre uygun okuyucu sınıfını runtime'da oluşturur.
 * "auto" modunda donanımı algılamaya çalışır: MFRC522 SPI yanıt veriyorsa
 * MFRC522, değilse HZ1050 kullanılır.
 *
 * Yeni bir okuyucu eklemek: sınıfı yaz, aşağıdaki createFor tipine ekle.
 */
class ReaderFactory {
 public:
  /** "auto", "hz1050" veya "mfrc522" için uygun okuyucuyu döndürür. */
  static std::unique_ptr<IRfidReader> create(const RfidConfig& cfg);

  /** Donanımı algılamayı dener; algılanan tip adını döndürür. */
  static String detect(const RfidConfig& cfg);
};
