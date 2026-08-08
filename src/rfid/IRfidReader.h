#pragma once

#include <Arduino.h>

/**
 * RFID okuyucu arayüzü (plugin mimarisi).
 * Yeni bir okuyucu eklemek için bu arayüzü implement eden bir sınıf
 * yazmak ve ReaderFactory'e kaydetmek yeterlidir.
 */
class IRfidReader {
 public:
  virtual ~IRfidReader() = default;

  /** Okuyucuyu başlatır; başarılıysa true döner. */
  virtual bool begin() = 0;

  /** Yeni bir kart UID'si okunmaya hazır mı? */
  virtual bool available() = 0;

  /** Bekleyen UID'yi okur (hex, büyük harf). Okunacak kart yoksa boş döner. */
  virtual String readUid() = 0;

  /** Okuyucu tipinin adı ("hz1050", "mfrc522" ...). */
  virtual const char* name() const = 0;
};
