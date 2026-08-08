#include "storage/Storage.h"
#include <LittleFS.h>

bool Storage::begin() {
  if (!LittleFS.begin(true)) {
    Serial.println("[FS] HATA: LittleFS bağlanamadı.");
    return false;
  }
  Serial.printf("[FS] LittleFS hazır. Boş: %lu / %lu byte\n",
                LittleFS.totalBytes() - LittleFS.usedBytes(),
                LittleFS.totalBytes());
  return true;
}
