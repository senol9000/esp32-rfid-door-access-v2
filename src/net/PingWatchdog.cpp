#include "net/PingWatchdog.h"
#include "net/WifiManager.h"
#include "log/EventLog.h"
#include <esp_netif.h>

static const char* kTag = "WDG";

void PingWatchdog::begin(AppConfig& cfg, WifiManager& wifi, EventLog* events) {
  cfg_ = &cfg;
  wifi_ = &wifi;
  events_ = events;
  applyConfig();
}

void PingWatchdog::applyConfig() {
  enabled_ = cfg_->watchdog.enabled;
  online_ = true;
  consecutiveFails_ = 0;
  reconnects_ = 0;
  pingActive_ = false;
  pingDone_ = false;
  lastReplyOk_ = false;
  lastCheckMs_ = millis();
  Serial.printf("[%s] Yapılandırma uygulandı. enabled=%d target=%s interval=%ds\n", kTag,
                enabled_, cfg_->watchdog.target.c_str(), cfg_->watchdog.intervalSec);
}

bool PingWatchdog::resolveTarget(ip_addr_t& out) {
  // Hedef IP veya hostname olabilir. IPv4 string ise doğrudan parse et.
  esp_ip4_addr_t ip4;
  if (esp_netif_str_to_ip4(cfg_->watchdog.target.c_str(), &ip4) == ESP_OK) {
    out.type = ESP_IPADDR_TYPE_V4;
    out.u_addr.ip4.addr = ip4.addr;  // esp_ip4_addr_t -> lwip ip4_addr_t
    return true;
  }
  return false;
}

void PingWatchdog::loop() {
  if (!enabled_) {
    return;
  }

  // Yalnızca STA modunda ve WiFi bağlıyken ping at. AP kurulum modunda veya
  // bağlantı kopukken sayaçları koru; bağlantı kurulunca ping sürer.
  // (Sayaçlar yalnızca başarılı ping'de sıfırlanır — böylece watchdog'un
  // tetiklediği reconnect sırasında reboot sayacı kaybolmaz.)
  if (wifi_->isApMode() || !wifi_->isStaConnected()) {
    online_ = false;
    return;
  }

  // Bir ping oturumu beklemede. pingDone_ ping task'ı tarafından set edilir.
  if (pingActive_) {
    if (pingDone_) {
      handleResult();
    }
    return;
  }

  // Zaman gelmişse yeni bir ping oturumu başlat.
  const unsigned long now = millis();
  if (now - lastCheckMs_ >= (unsigned long)cfg_->watchdog.intervalSec * 1000UL) {
    lastCheckMs_ = now;
    startPing();
  }
}

void PingWatchdog::startPing() {
  ip_addr_t target;
  if (!resolveTarget(target)) {
    // Hedef çözülemedi (geçersiz IP). Başarısız say, bir sonraki aralığı bekle.
    Serial.printf("[%s] Hedef çözülemedi: %s\n", kTag, cfg_->watchdog.target.c_str());
    pingActive_ = false;
    return;
  }

  esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
  config.count = 1;                                    // Tek ping
  config.interval_ms = 1000;
  config.timeout_ms = (uint32_t)cfg_->watchdog.timeoutMs;
  config.target_addr = target;
  config.task_stack_size = 2048 + TASK_EXTRA_STACK_SIZE;
  config.task_prio = 2;
  config.interface = 0;  // Varsayılan ağ arayüzü

  esp_ping_callbacks_t cbs;
  memset(&cbs, 0, sizeof(cbs));
  cbs.cb_args = this;
  cbs.on_ping_success = onPingSuccess;
  cbs.on_ping_timeout = onPingTimeout;
  cbs.on_ping_end = onPingEnd;

  esp_ping_handle_t hdl;
  if (esp_ping_new_session(&config, &cbs, &hdl) != ESP_OK) {
    Serial.printf("[%s] Ping oturumu oluşturulamadı\n", kTag);
    return;
  }
  pingHandle_ = hdl;
  pingActive_ = true;
  pingDone_ = false;
  lastReplyOk_ = false;
  if (esp_ping_start(hdl) != ESP_OK) {
    Serial.printf("[%s] Ping başlatılamadı\n", kTag);
    stopSession();
  }
}

void PingWatchdog::stopSession() {
  if (pingHandle_ != nullptr) {
    esp_ping_stop(pingHandle_);
    esp_ping_delete_session(pingHandle_);
    pingHandle_ = nullptr;
  }
  pingActive_ = false;
  pingDone_ = false;
}

void PingWatchdog::handleResult() {
  // Oturum bitti; yanıt gelip gelmediğine göre sayaçları güncelle.
  const bool ok = lastReplyOk_;
  stopSession();

  if (ok) {
    if (consecutiveFails_ > 0) {
      Serial.printf("[%s] İnternet erişimi geri geldi (fail=%d->0)\n", kTag, consecutiveFails_);
    }
    consecutiveFails_ = 0;
    reconnects_ = 0;
    online_ = true;
    return;
  }

  consecutiveFails_++;
  online_ = false;
  Serial.printf("[%s] Ping yanıtı yok (%d. ardışık başarısızlık, hedef=%s)\n", kTag,
                consecutiveFails_, cfg_->watchdog.target.c_str());

  const int failThr = cfg_->watchdog.failCountBeforeReconnect;
  const int rebootThr = cfg_->watchdog.reconnectLimit;

  if (consecutiveFails_ >= failThr && reconnects_ < rebootThr) {
    // WiFi'yi yeniden bağlat. applyConfig mevcut moda göre STA/AP başlatır.
    reconnects_++;
    consecutiveFails_ = 0;
    Serial.printf("[%s] WiFi yeniden bağlanıyor (%d/%d)\n", kTag, reconnects_, rebootThr);
    if (events_) {
      events_->add(EventType::WatchdogWifiReset,
                   "Ping yanıtı yok — WiFi yeniden bağlanıyor (" + String(reconnects_) + "/" +
                       String(rebootThr) + ")");
    }
    wifi_->applyConfig();
    // Yeniden bağlanma süresince bekleyip sayacı sıfırlamak için aralığı güncelle.
    lastCheckMs_ = millis() + (unsigned long)cfg_->watchdog.intervalSec * 1000UL;
    return;
  }

  if (reconnects_ >= rebootThr) {
    Serial.printf("[%s] Bağlantı kurtarılamadı, cihaz yeniden başlatılıyor...\n", kTag);
    if (events_) {
      events_->add(EventType::WatchdogReboot,
                   "Ping watchdog — bağlantı kurtarılamadı, yeniden başlatılıyor");
    }
    // Seri çıktının taşınmasına kısa süre tanı ve yeniden başlat.
    vTaskDelay(pdMS_TO_TICKS(300));
    ESP.restart();
  }
}

void PingWatchdog::onPingSuccess(esp_ping_handle_t hdl, void* args) {
  auto* self = static_cast<PingWatchdog*>(args);
  self->lastReplyOk_ = true;
}

void PingWatchdog::onPingTimeout(esp_ping_handle_t hdl, void* args) {
  auto* self = static_cast<PingWatchdog*>(args);
  self->lastReplyOk_ = false;
}

void PingWatchdog::onPingEnd(esp_ping_handle_t hdl, void* args) {
  auto* self = static_cast<PingWatchdog*>(args);
  // Oturum sona erdi; sonuç App::loop içinde handleResult ile işlenir.
  self->pingDone_ = true;
}
