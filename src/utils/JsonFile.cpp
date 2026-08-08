#include "utils/JsonFile.h"
#include <LittleFS.h>

bool JsonFile::load(const char* path, JsonDocument& doc) {
  if (!LittleFS.exists(path)) {
    return false;
  }
  File f = LittleFS.open(path, "r");
  if (!f) {
    return false;
  }
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  return !err;
}

bool JsonFile::saveAtomic(const char* path, JsonDocument& doc) {
  char tmp[64];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);

  File f = LittleFS.open(tmp, "w");
  if (!f) {
    return false;
  }
  const bool ok = serializeJson(doc, f) > 0;
  f.close();

  if (ok) {
    // Önce hedefi kaldır (LittleFS rename hedef varken başarısız olabilir).
    LittleFS.remove(path);
    if (!LittleFS.rename(tmp, path)) {
      return false;
    }
  } else {
    LittleFS.remove(tmp);
  }
  return ok;
}

bool JsonFile::appendLine(const char* path, JsonDocument& doc) {
  File f = LittleFS.open(path, "a");
  if (!f) {
    return false;
  }
  const bool ok = serializeJson(doc, f) > 0;
  if (ok) {
    f.write('\n');
  }
  f.close();
  return ok;
}

size_t JsonFile::fileSize(const char* path) {
  if (!LittleFS.exists(path)) {
    return 0;
  }
  File f = LittleFS.open(path, "r");
  if (!f) {
    return 0;
  }
  const size_t sz = f.size();
  f.close();
  return sz;
}
