#include "rfid/MFRC522Reader.h"
#include <SPI.h>

bool MFRC522Reader::begin() {
  if (started_) {
    return true;
  }
  if (cfg_->mfrc522SdaPin < 0) {
    return false;
  }

  // SPI veri yolu: pinler yapılandırılabilir (SDA/SS, RST, SCK, MISO, MOSI).
  SPI.begin(cfg_->mfrc522SckPin, cfg_->mfrc522MisoPin, cfg_->mfrc522MosiPin,
            cfg_->mfrc522SdaPin);

  mfrc_ = new MFRC522(cfg_->mfrc522SdaPin, cfg_->mfrc522RstPin);
  mfrc_->PCD_Init();
  mfrc_->PCD_DumpVersionToSerial();  // donanımı doğrula

  started_ = true;
  Serial.printf("[RFID] MFRC522 başlatıldı (SS=%d RST=%d SCK=%d MISO=%d MOSI=%d).\n",
                cfg_->mfrc522SdaPin, cfg_->mfrc522RstPin, cfg_->mfrc522SckPin,
                cfg_->mfrc522MisoPin, cfg_->mfrc522MosiPin);
  return true;
}

bool MFRC522Reader::available() {
  if (!started_ || mfrc_ == nullptr) {
    return false;
  }
  if (!pendingUid_.isEmpty()) {
    return true;
  }

  // Yeni kart var mı? (yalnızca A ve B anahtarları yok sayılır)
  if (!mfrc_->PICC_IsNewCardPresent() || !mfrc_->PICC_ReadCardSerial()) {
    return false;
  }

  pendingUid_ = bytesToHex(mfrc_->uid.uidByte, mfrc_->uid.size);
  Serial.printf("[RFID] MFRC522 UID: %s (tipe: %s)\n", pendingUid_.c_str(),
                mfrc_->PICC_GetTypeName(mfrc_->PICC_GetType(mfrc_->uid.sak)));

  // Kartı serbest bırak (halting), böylece aynı kart tekrar okunabilir.
  mfrc_->PICC_HaltA();
  mfrc_->PCD_StopCrypto1();
  return true;
}

String MFRC522Reader::readUid() {
  if (pendingUid_.isEmpty()) {
    return String();
  }
  const String uid = pendingUid_;
  pendingUid_ = "";
  return uid;
}

String MFRC522Reader::bytesToHex(const byte* data, byte n) {
  String s;
  s.reserve(n * 2);
  for (byte i = 0; i < n; i++) {
    if (data[i] < 0x10) {
      s += '0';
    }
    s += String(data[i], HEX);
  }
  s.toUpperCase();
  return s;
}
