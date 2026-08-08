#include "net/WifiManager.h"

static const unsigned long kStaTimeoutMs = 20000;  // STA bağlantı denemesi zaman aşımı

void WifiManager::begin(AppConfig& cfg) {
  cfg_ = &cfg;
  WiFi.persistent(false);  // NVS yazma aşınmasını önler; ayarlar JSON'da tutulur
  if (cfg.wifi.ssid.isEmpty()) {
    startAp();
  } else {
    startSta();
  }
}

void WifiManager::startSta() {
  mode_ = Mode::Sta;
  Serial.printf("[NET] STA moduna geçiliyor. SSID=%s\n", cfg_->wifi.ssid.c_str());

  WiFi.mode(WIFI_STA);
  if (!cfg_->wifi.hostname.isEmpty()) {
    WiFi.setHostname(cfg_->wifi.hostname.c_str());
  }
  WiFi.begin(cfg_->wifi.ssid.c_str(), cfg_->wifi.password.c_str());
  staStartedMs_ = millis();
  wasConnected_ = false;
}

void WifiManager::startAp() {
  mode_ = Mode::Ap;

  // SSID'ye MAC'in son 3 baytını ekleyerek benzersiz bir isim oluştur
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char suffix[7];
  snprintf(suffix, sizeof(suffix), "%02X%02X%02X", mac[3], mac[4], mac[5]);
  apSsid_ = "ESP32-Door-" + String(suffix);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                    IPAddress(255, 255, 255, 0));
  WiFi.softAP(apSsid_.c_str());

  // Captive portal: tüm DNS sorgularını kendi IP'mize yönlendir
  dns_.start(53, "*", IPAddress(192, 168, 4, 1));

  Serial.printf("[NET] AP modu aktif. SSID=%s IP=192.168.4.1\n", apSsid_.c_str());
}

void WifiManager::loop() {
  if (mode_ == Mode::Ap) {
    dns_.processNextRequest();
    return;
  }

  if (mode_ != Mode::Sta) {
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!wasConnected_) {
      wasConnected_ = true;
      Serial.printf("[NET] Bağlandı! IP=%s RSSI=%d dBm\n",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
    }
    return;
  }

  // Bağlantı kopmuş; zaman aşımı dolduysa AP moduna düş
  if (wasConnected_) {
    Serial.println("[NET] Bağlantı koptu, yeniden bağlanılıyor...");
  }
  wasConnected_ = false;
  if (millis() - staStartedMs_ > kStaTimeoutMs) {
    Serial.println("[NET] STA zaman aşımı, AP moduna geçiliyor.");
    startAp();
  }
}

void WifiManager::applyConfig() {
  if (mode_ == Mode::Ap) {
    dns_.stop();
  } else if (mode_ == Mode::Sta) {
    WiFi.disconnect();
  }

  if (cfg_->wifi.ssid.isEmpty()) {
    startAp();
  } else {
    startSta();
  }
}

String WifiManager::getIp() const {
  if (mode_ == Mode::Sta) {
    return WiFi.localIP().toString();
  }
  if (mode_ == Mode::Ap) {
    return WiFi.softAPIP().toString();
  }
  return String("0.0.0.0");
}
