#pragma once

#include <Arduino.h>
#include <functional>
#include <memory>
#include "rfid/IRfidReader.h"
#include "config/Config.h"

/**
 * RFID okuyucu yöneticisi.
 *
 * ReaderFactory ile runtime'da okuyucu oluşturur, loop() içinde kart
 * taraması yapar ve yeni bir UID okunduğunda onTag callback'ini çağırır.
 * Okuyucu tipi web üzerinden değiştirilebilir (yeniden derleme gerekmez).
 */
class RfidManager {
 public:
  /** UID okunduğunda çağrılır. */
  using OnTagCallback = std::function<void(const String& uid)>;

  void begin(AppConfig& cfg, OnTagCallback onTag);
  void loop();

  /** Okuyucuyu yeni yapılandırmayla yeniden oluşturur. */
  void applyConfig(AppConfig& cfg);

  /** Etkin okuyucunun tip adı ("hz1050"/"mfrc522"/"auto"). */
  String activeType() const;

  /** Gerçekten başlatılmış okuyucu tipi. */
  const char* readerName() const;

  bool isInitialized() const { return reader_ != nullptr; }

  /** Son okunan UID (web paneli kart eklerken gösterir). */
  const String& lastUid() const { return lastUid_; }

  /**
   * Kayıt (enrollment) modunu açar/kapatır.
   * Açıkken yeni kart okumaları kapıyı tetiklemez, yalnızca takeEnrollUid()
   * ile alınmak üzere saklanır. Web panelindeki "Kart Ekle" akışı için.
   */
  void setEnrollMode(bool on);
  bool enrollMode() const { return enrollMode_; }

  /** Kayıt modunda okunan son UID'yi döndürür ve temizler. */
  String takeEnrollUid();

 private:
  AppConfig* cfg_ = nullptr;
  std::unique_ptr<IRfidReader> reader_;
  OnTagCallback onTag_;
  String lastUid_;
  bool enrollMode_ = false;
  String enrollUid_;

  void createReader();
};
