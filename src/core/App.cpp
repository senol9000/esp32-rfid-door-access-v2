#include "core/App.h"
#include "config/Config.h"
#include "storage/Storage.h"

static const char* kTag = "APP";

void App::begin() {
  Serial.begin(115200);
  Serial.println();
  Serial.printf("[%s] ESP32 Kapı Kontrol başlatılıyor...\n", kTag);

  if (!Storage::begin()) {
    Serial.printf("[%s] HATA: LittleFS bağlanamadı.\n", kTag);
    return;
  }

  if (!ConfigManager::load(config_)) {
    Serial.printf("[%s] Yapılandırma bulunamadı; kurulum moduna geçiliyor.\n", kTag);
  }

  wifi_.begin(config_);
  ntp_.begin(config_);
  users_.begin();
  accessLog_.begin();
  events_.begin();
  gpio_.begin(config_.gpio);
  lcd_.begin(config_);
  schedule_.begin();
  holidays_.begin();
  access_.begin();
  auth_.begin(config_);
  mqtt_.begin(config_, wifi_, ntp_, gpio_, events_);

  // Erişim karar sonuçlarını MQTT üzerinden yayınla (Home Assistant / izleme)
  // ve LCD ekranda göster.
  access_.onDecision = [this](const AccessEngine::Result& res) {
    mqtt_.publishAccess(res.allowed ? "allowed" : "denied", res.uid.c_str(),
                        res.name.c_str(), res.reason.c_str());
    lcd_.showAccess(res.uid, res.name, res.allowed);
  };

  // RFID okuyucuyu başlat; kart okunduğunda karar motoruna ilet.
  rfid_.begin(config_, [this](const String& uid) {
    access_.decide(uid);
  });

  web_.begin(config_, wifi_, users_, accessLog_, ntp_, gpio_, rfid_, schedule_,
             holidays_, events_, mqtt_, auth_, lcd_);

  events_.add(EventType::Boot, "Sistem açıldı");
  Serial.printf("[%s] Başlatma tamam. IP: %s\n", kTag, wifi_.getIp().c_str());
}

void App::loop() {
  wifi_.loop();
  web_.loop();
  users_.loop();
  accessLog_.loop();
  events_.loop();
  ntp_.loop();
  gpio_.loop();
  lcd_.loop();
  rfid_.loop();
  schedule_.loop();
  holidays_.loop();
  mqtt_.loop();
  auth_.loop();

  // WiFi/NTP durum geçişlerini olay loguna işle (2 saniyede bir kontrol).
  const unsigned long nowMs = millis();
  if (nowMs - lastWifiCheckMs_ >= 2000) {
    lastWifiCheckMs_ = nowMs;
    const bool conn = wifi_.isStaConnected();
    if (conn != wasWifiConnected_) {
      wasWifiConnected_ = conn;
      if (conn) {
        events_.add(EventType::WifiConnected,
                    "WiFi bağlandı: " + String(config_.wifi.ssid));
      } else {
        events_.add(EventType::WifiDisconnected, "WiFi bağlantısı koptu");
      }
    }
    const bool synced = ntp_.isSynced();
    if (synced != wasNtpSynced_) {
      wasNtpSynced_ = synced;
      if (synced) {
        events_.add(EventType::NtpSync, "NTP saat senkronu tamamlandı");
      }
    }

    // LCD durum satırlarını güncelle (kapı durumu + saat / WiFi).
    if (lcd_.isPresent()) {
      String l1 = "KAPI " + String(gpio_.isDoorOpen() ? "ACIK" : "KAPALI") + "  " +
                  (wifi_.isStaConnected() ? wifi_.getIp() : "AP");
      String l2 = synced ? ntp_.currentDateTime("%d.%m %H:%M") : "SAAT SENKRON YOK";
      lcd_.setStatus(l1, l2);
    }
  }

  const unsigned long now = millis();
  if (now - lastTickMs_ < 10000) {
    return;
  }
  lastTickMs_ = now;
  Serial.printf("[%s] uptime=%lus mode=%s ip=%s heap=%lu ntp=%s rfid=%s mqtt=%s\n", kTag,
                now / 1000, wifi_.isApMode() ? "AP" : "STA", wifi_.getIp().c_str(),
                (unsigned long)ESP.getFreeHeap(), ntp_.isSynced() ? "OK" : "yok",
                rfid_.readerName(), mqtt_.isConnected() ? "bagli" : "kapali");
}
