#pragma once

#include <Arduino.h>

/**
 * Dosya sistemi (LittleFS) başlatma yardımcıları.
 * Tüm modüller dosya erişimi için önce Storage::begin() çağrıldığından emin olmalıdır.
 */
class Storage {
 public:
  /** LittleFS dosya sistemini bağlar. Başarısız olursa formatlamayı dener. */
  static bool begin();
};
