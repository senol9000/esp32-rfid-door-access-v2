#include "rfid/RfidManager.h"
#include "rfid/ReaderFactory.h"

void RfidManager::begin(AppConfig& cfg, OnTagCallback onTag) {
  cfg_ = &cfg;
  onTag_ = onTag;
  createReader();
}

void RfidManager::createReader() {
  if (!cfg_) {
    return;
  }
  reader_ = ReaderFactory::create(cfg_->rfid);
  if (reader_ && reader_->begin()) {
    Serial.printf("[RFID] Okuyucu aktif: %s\n", reader_->name());
  } else {
    Serial.println("[RFID] HATA: Okuyucu başlatılamadı.");
    reader_.reset();
  }
}

void RfidManager::loop() {
  if (!reader_) {
    return;
  }
  if (!reader_->available()) {
    return;
  }
  const String uid = reader_->readUid();
  if (uid.isEmpty()) {
    return;
  }
  lastUid_ = uid;
  Serial.printf("[RFID] Kart okundu: %s\n", uid.c_str());

  // Kayıt modunda kart sadece yakalanır; kapı/karar akışı tetiklenmez.
  if (enrollMode_) {
    enrollUid_ = uid;
    Serial.println("[RFID] Kayıt modunda kart yakalandı.");
    return;
  }
  if (onTag_) {
    onTag_(uid);
  }
}

void RfidManager::setEnrollMode(bool on) {
  enrollMode_ = on;
  if (!on) {
    enrollUid_ = "";
  }
  Serial.printf("[RFID] Kayıt modu %s.\n", on ? "AÇIK" : "KAPALI");
}

String RfidManager::takeEnrollUid() {
  const String uid = enrollUid_;
  enrollUid_ = "";
  return uid;
}

void RfidManager::applyConfig(AppConfig& cfg) {
  cfg_ = &cfg;
  reader_.reset();
  createReader();
}

String RfidManager::activeType() const {
  return cfg_ ? cfg_->rfid.type : "auto";
}

const char* RfidManager::readerName() const {
  return reader_ ? reader_->name() : "yok";
}
