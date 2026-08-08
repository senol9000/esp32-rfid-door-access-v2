#pragma once

#include <ArduinoJson.h>

/**
 * JSON dosya yardımcıları.
 *
 * - saveAtomic: önce .tmp dosyasına yazar, sonra rename ile hedefe taşır.
 *   Güç kesintisinde bozulmuş dosya oluşmasını önler.
 * - appendLine: JSONL (satır satır JSON) olarak log kayıtlarını ekler;
 *   tüm dosyayı yeniden yazmadan hızlı ekleme sağlar.
 */
namespace JsonFile {
  /** Dosyadan JSON okur. Dosya yoksa veya parse hatası olursa false döner. */
  bool load(const char* path, JsonDocument& doc);

  /** Belgeyi atomik olarak dosyaya yazar. */
  bool saveAtomic(const char* path, JsonDocument& doc);

  /** Belgeyi tek satır olarak dosyanın sonuna ekler (JSONL). */
  bool appendLine(const char* path, JsonDocument& doc);

  /** Dosya boyutunu byte cinsinden döner. */
  size_t fileSize(const char* path);
}
