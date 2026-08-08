#include "rfid/ReaderFactory.h"
#include "rfid/HZ1050Reader.h"
#include "rfid/MFRC522Reader.h"
#include <SPI.h>
#include <MFRC522.h>

std::unique_ptr<IRfidReader> ReaderFactory::create(const RfidConfig& cfg) {
  String type = cfg.type;
  type.toLowerCase();

  if (type == "hz1050") {
    return std::make_unique<HZ1050Reader>(cfg);
  }
  if (type == "mfrc522") {
    return std::make_unique<MFRC522Reader>(cfg);
  }

  // "auto" veya bilinmeyen tip: donanımı algılamayı dene.
  const String detected = detect(cfg);
  if (detected == "mfrc522") {
    return std::make_unique<MFRC522Reader>(cfg);
  }
  // Varsayılan: HZ1050 (UART okuyucu yaygın ve algılanması kolay).
  return std::make_unique<HZ1050Reader>(cfg);
}

String ReaderFactory::detect(const RfidConfig& cfg) {
  if (cfg.mfrc522SdaPin >= 0) {
    // MFRC522, PCD_VERSION komutuna yanıt verir; SPI hattı kurup deneriz.
    SPI.begin(cfg.mfrc522SckPin, cfg.mfrc522MisoPin, cfg.mfrc522MosiPin,
              cfg.mfrc522SdaPin);
    MFRC522 probe(cfg.mfrc522SdaPin, cfg.mfrc522RstPin);
    probe.PCD_Init();
    const byte version = probe.PCD_ReadRegister(MFRC522::VersionReg);
    // Geçerli MFRC522 sürümleri 0x90..0x92 (MF1xx), bazı klonlar 0x1x.
    if (version != 0x00 && version != 0xFF) {
      Serial.printf("[RFID] Algılama: MFRC522 (versiyon 0x%02X).\n", version);
      return "mfrc522";
    }
    Serial.printf("[RFID] Algılama: MFRC522 yanıt vermedi (0x%02X), HZ1050 kabul edildi.\n",
                  version);
  }
  return "hz1050";
}
