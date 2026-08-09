#include "web/WebServerManager.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>
#include <time.h>
#include "config/Config.h"
#include "storage/Backup.h"

static const char* kContentTypeHtml = "text/html";
static const char* kContentTypeJson = "application/json";
static const char* kContentTypeCsv = "text/csv";
static const char* kPasswordMask = "********";

// Küçük yardımcı: JsonDocument'ı yanıt olarak gönderir.
static void sendJsonDoc(AsyncWebServerRequest* request, JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  request->send(200, kContentTypeJson, out);
}

static void sendJsonError(AsyncWebServerRequest* request, int code,
                          const char* msg) {
  JsonDocument doc;
  doc["error"] = msg;
  String out;
  serializeJson(doc, out);
  request->send(code, kContentTypeJson, out);
}

// Epoch zamanını "YYYY-MM-DD HH:MM:SS" biçiminde döndürür.
static String formatTs(time_t t) {
  if (t < 1600000000) {
    return "0000-00-00 00:00:00";
  }
  struct tm tmv;
  localtime_r(&t, &tmv);
  char buf[24];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
  return String(buf);
}

// CSV alanını tırnak ve virgülden kaçırır.
static String csvEsc(const String& v) {
  String out = v;
  out.replace("\"", "\"\"");
  return "\"" + out + "\"";
}

void WebServerManager::begin(AppConfig& cfg, WifiManager& wifi, UserManager& users,
                             AccessLog& accessLog, NtpManager& ntp, GpioManager& gpio,
                             RfidManager& rfid, TimeSchedule& schedule,
                             HolidayManager& holidays, EventLog& events,
                             MqttManager& mqtt, AuthManager& auth,
                             LcdManager& lcd) {
  cfg_ = &cfg;
  wifi_ = &wifi;
  users_ = &users;
  accessLog_ = &accessLog;
  ntp_ = &ntp;
  gpio_ = &gpio;
  rfid_ = &rfid;
  schedule_ = &schedule;
  holidays_ = &holidays;
  events_ = &events;
  mqtt_ = &mqtt;
  auth_ = &auth;
  lcd_ = &lcd;
  setupAuthMiddleware();
  setupRoutes();
  Serial.println("[WEB] HTTP sunucusu başlatıldı (port 80).");
}

void WebServerManager::loop() {
  if (restartAtMs_ != 0 && millis() >= restartAtMs_) {
    ESP.restart();
  }
}

void WebServerManager::setupAuthMiddleware() {
  // Tek sunucu geneli middleware: her istek buradan geçer.
  // - Genel (public) yollar ve güvenli metodlar doğrudan devam eder.
  // - Aksi halde X-Auth-Token başlığı doğrulanır; POST/PUT/DELETE için
  //   X-CSRF-Token da istenir. Kilit kapalıysa (şifre tanımsız) herkes geçer.
  server_.addMiddleware([this](AsyncWebServerRequest* request, ArMiddlewareNext next) {
    if (auth_ == nullptr || !auth_->isEnabled()) {
      next();
      return;
    }
    if (isPublicPath(request->url())) {
      next();
      return;
    }
    const String token = request->header("X-Auth-Token");
    if (!auth_->validateSession(token)) {
      String ip = "?";
      if (request->client()) {
        ip = request->client()->remoteIP().toString();
      }
      events_->add(EventType::Unauthorized,
                   "Yetkisiz erişim denemesi: " + request->url() + " (IP " + ip + ")");
      request->send(401, kContentTypeJson,
                    "{\"error\":\"Yetkisiz: oturum geçersiz\"}");
      return;
    }
    if (request->method() == HTTP_POST || request->method() == HTTP_PUT ||
        request->method() == HTTP_DELETE) {
      const String csrf = request->header("X-CSRF-Token");
      if (!auth_->validateCsrf(token, csrf)) {
        events_->add(EventType::Unauthorized,
                     "CSRF doğrulaması başarısız: " + request->url());
        request->send(403, kContentTypeJson,
                      "{\"error\":\"CSRF doğrulaması başarısız\"}");
        return;
      }
    }
    next();
  });
}

bool WebServerManager::isPublicPath(const String& url) const {
  // Statik panel ve kimlik doğrulama uç noktaları her zaman açık.
  if (url == "/" || url == "/index.html" || url.startsWith("/api/auth")) return true;
  // Kurulum modunda WiFi taraması gerekebilir (kilit henüz açılmamışsa).
  if (url == "/api/wifi/scan" && !auth_->isEnabled()) return true;
  return false;
}

void WebServerManager::setupRoutes() {
  server_.on("/", HTTP_GET, [this](AsyncWebServerRequest* r) { handleIndex(r); });
  server_.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* r) { handleStatus(r); });

  // Kimlik doğrulama uç noktaları
  server_.on("/api/auth/login", HTTP_POST, [this](AsyncWebServerRequest* r) { handleAuthLogin(r); });
  server_.on("/api/auth/logout", HTTP_POST, [this](AsyncWebServerRequest* r) { handleAuthLogout(r); });
  server_.on("/api/auth/status", HTTP_GET, [this](AsyncWebServerRequest* r) { handleAuthStatus(r); });

  // Yedekleme / geri yükleme
  server_.on("/api/backup", HTTP_GET, [this](AsyncWebServerRequest* r) { handleBackupGet(r); });
  server_.on("/api/backup", HTTP_POST, [this](AsyncWebServerRequest* r) { handleBackupPost(r); });

  // Firmware (OTA) güncellemesi
  server_.on("/api/ota/status", HTTP_GET, [this](AsyncWebServerRequest* r) { handleOtaStatus(r); });
  server_.on("/api/ota", HTTP_POST,
             [this](AsyncWebServerRequest* r) { handleOtaUpload(r); },
             [this](AsyncWebServerRequest* r, const String& filename, size_t index,
                    uint8_t* data, size_t len, bool final) {
               handleOtaUploadProgress(r, filename, index, data, len, final);
             });

  server_.on("/api/dashboard", HTTP_GET, [this](AsyncWebServerRequest* r) { handleDashboard(r); });
  server_.on("/api/config", HTTP_GET, [this](AsyncWebServerRequest* r) { handleConfigGet(r); });
  server_.on("/api/config", HTTP_POST, [this](AsyncWebServerRequest* r) { handleConfigPost(r); });
  server_.on("/api/wifi/scan", HTTP_GET, [this](AsyncWebServerRequest* r) { handleWifiScan(r); });
  server_.on("/api/restart", HTTP_POST, [this](AsyncWebServerRequest* r) { handleRestart(r); });

  server_.on("/api/users", HTTP_GET, [this](AsyncWebServerRequest* r) { handleUsersGet(r); });
  server_.on("/api/users", HTTP_POST, [this](AsyncWebServerRequest* r) { handleUserPost(r); });
  server_.on("/api/users/*", HTTP_PUT, [this](AsyncWebServerRequest* r) { handleUserPut(r); });
  server_.on("/api/users/*", HTTP_DELETE, [this](AsyncWebServerRequest* r) { handleUserDelete(r); });

  server_.on("/api/logs/access", HTTP_GET, [this](AsyncWebServerRequest* r) { handleLogsGet(r); });
  server_.on("/api/logs/access.csv", HTTP_GET, [this](AsyncWebServerRequest* r) { handleLogsCsv(r); });
  server_.on("/api/logs/clear", HTTP_POST, [this](AsyncWebServerRequest* r) { handleLogsClear(r); });

  server_.on("/api/logs/event", HTTP_GET, [this](AsyncWebServerRequest* r) { handleEventLogsGet(r); });
  server_.on("/api/logs/event.csv", HTTP_GET, [this](AsyncWebServerRequest* r) { handleEventLogsCsv(r); });
  server_.on("/api/logs/event/clear", HTTP_POST, [this](AsyncWebServerRequest* r) { handleEventLogsClear(r); });

  server_.on("/api/mqtt", HTTP_GET, [this](AsyncWebServerRequest* r) { handleMqttGet(r); });
  server_.on("/api/mqtt", HTTP_POST, [this](AsyncWebServerRequest* r) { handleMqttPost(r); });

  server_.on("/api/gpio", HTTP_GET, [this](AsyncWebServerRequest* r) { handleGpioGet(r); });
  server_.on("/api/gpio", HTTP_POST, [this](AsyncWebServerRequest* r) { handleGpioPost(r); });
  server_.on("/api/gpio/action", HTTP_POST, [this](AsyncWebServerRequest* r) { handleGpioAction(r); });

  // LCD (I2C 16x2) yapılandırması / algılama / test
  server_.on("/api/lcd", HTTP_GET, [this](AsyncWebServerRequest* r) { handleLcdGet(r); });
  server_.on("/api/lcd", HTTP_POST, [this](AsyncWebServerRequest* r) { handleLcdPost(r); });
  server_.on("/api/lcd/action", HTTP_POST, [this](AsyncWebServerRequest* r) { handleLcdAction(r); });

  server_.on("/api/rfid", HTTP_GET, [this](AsyncWebServerRequest* r) { handleRfidGet(r); });
  server_.on("/api/rfid", HTTP_POST, [this](AsyncWebServerRequest* r) { handleRfidPost(r); });
  server_.on("/api/rfid/enroll", HTTP_POST, [this](AsyncWebServerRequest* r) { handleRfidEnroll(r); });
  server_.on("/api/rfid/enroll", HTTP_GET, [this](AsyncWebServerRequest* r) { handleRfidEnroll(r); });

  server_.on("/api/schedule", HTTP_GET, [this](AsyncWebServerRequest* r) { handleScheduleGet(r); });
  server_.on("/api/schedule/*", HTTP_GET, [this](AsyncWebServerRequest* r) { handleScheduleGetOne(r); });
  server_.on("/api/schedule/*", HTTP_PUT, [this](AsyncWebServerRequest* r) { handleSchedulePut(r); });
  server_.on("/api/schedule/*", HTTP_DELETE, [this](AsyncWebServerRequest* r) { handleScheduleDelete(r); });

  server_.on("/api/holidays", HTTP_GET, [this](AsyncWebServerRequest* r) { handleHolidayGet(r); });
  server_.on("/api/holidays", HTTP_POST, [this](AsyncWebServerRequest* r) { handleHolidayPost(r); });
  server_.on("/api/holidays/*", HTTP_DELETE, [this](AsyncWebServerRequest* r) { handleHolidayDelete(r); });

  server_.onNotFound([this](AsyncWebServerRequest* r) { handleNotFound(r); });
  server_.begin();
}

void WebServerManager::handleIndex(AsyncWebServerRequest* request) {
  // LittleFS üzerindeki hazır panel sayfasını sun (data/index.html)
  // Stream: dosyayı bellekten okumak yerine doğrudan gönderir (büyük dosyalar için güvenli).
  if (LittleFS.exists("/index.html")) {
    request->send(LittleFS, "/index.html", kContentTypeHtml);
    return;
  }
  request->send(200, kContentTypeHtml,
                F("<html><body style='font-family:sans-serif;background:#0f172a;color:#e2e8f0'>"
                  "<h2>⚠️ Panel sayfası bulunamadı</h2>"
                  "<p>Lütfen <code>pio run -t uploadfs</code> ile dosya sistemini yükleyin.</p>"
                  "</body></html>"));
}

void WebServerManager::handleStatus(AsyncWebServerRequest* request) {
  JsonDocument doc;
  doc["mode"] = wifi_->isApMode() ? "ap" : "sta";
  doc["connected"] = wifi_->isStaConnected();
  doc["ip"] = wifi_->getIp();
  doc["ssid"] = wifi_->isApMode() ? wifi_->apSsid() : cfg_->wifi.ssid;
  doc["rssi"] = wifi_->isApMode() ? 0 : wifi_->getRssi();
  doc["uptime_ms"] = millis();
  doc["heap"] = ESP.getFreeHeap();
  doc["ntp_synced"] = ntp_->isSynced();
  doc["time"] = ntp_->currentDateTime();
  sendJsonDoc(request, doc);
}

void WebServerManager::handleDashboard(AsyncWebServerRequest* request) {
  JsonDocument doc;

  JsonObject users = doc["users"].to<JsonObject>();
  users["total"] = users_->count();
  users["admins"] = users_->countAdmins();
  users["active"] = users_->countActive();

  JsonObject access = doc["access"].to<JsonObject>();
  const size_t today = accessLog_->countToday();
  const size_t todayAllowed = accessLog_->countTodayAllowed();
  access["today"] = today;
  access["todayAllowed"] = todayAllowed;
  access["todayDenied"] = today > todayAllowed ? today - todayAllowed : 0;

  // Son 3 kayıt
  JsonArray last = doc["last"].to<JsonArray>();
  const std::deque<AccessRecord>& log = accessLog_->entries();
  for (auto it = log.rbegin(); it != log.rend() && last.size() < 3; ++it) {
    JsonObject o = last.add<JsonObject>();
    AccessLog::toJson(*it, o);
    o["ts_str"] = formatTs(it->timestamp);
  }

  JsonObject sys = doc["system"].to<JsonObject>();
  sys["app"] = APP_NAME;
  sys["company"] = APP_COMPANY;
  sys["version"] = APP_VERSION;
  sys["heap"] = ESP.getFreeHeap();
  sys["cpuFreq"] = ESP.getCpuFreqMHz();
  sys["flashSize"] = ESP.getFlashChipSize();
  sys["sketchSize"] = ESP.getSketchSize();
  sys["uptime_ms"] = millis();
  sys["ip"] = wifi_->getIp();
  sys["rssi"] = wifi_->isApMode() ? 0 : wifi_->getRssi();
  sys["ntp_synced"] = ntp_->isSynced();
  sys["time"] = ntp_->currentDateTime();

  JsonObject hw = doc["hardware"].to<JsonObject>();
  hw["doorOpen"] = gpio_->isDoorOpen();
  hw["doorOpenPhysical"] = gpio_->isDoorOpenPhysical();
  hw["exitPressed"] = gpio_->isExitButtonPressed();

  JsonObject rfid = doc["rfid"].to<JsonObject>();
  rfid["type"] = rfid_->activeType();
  rfid["reader"] = rfid_->readerName();
  rfid["ready"] = rfid_->isInitialized();

  JsonObject mqtt = doc["mqtt"].to<JsonObject>();
  mqtt["enabled"] = cfg_->mqtt.enabled;
  mqtt["connected"] = mqtt_->isConnected();

  doc["events"] = events_->count();
  doc["holidays"] = holidays_->count();

  sendJsonDoc(request, doc);
}

void WebServerManager::handleConfigGet(AsyncWebServerRequest* request) {
  JsonDocument doc;
  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["ssid"] = cfg_->wifi.ssid;
  wifi["password"] = cfg_->wifi.password.isEmpty() ? "" : kPasswordMask;
  wifi["hostname"] = cfg_->wifi.hostname;

  JsonObject ntp = doc["ntp"].to<JsonObject>();
  ntp["server"] = cfg_->ntp.server;
  ntp["utcOffsetMin"] = cfg_->ntp.utcOffsetMin;
  ntp["autoDst"] = cfg_->ntp.autoDst;

  JsonObject gpio = doc["gpio"].to<JsonObject>();
  gpio["relayPin"] = cfg_->gpio.relayPin;
  gpio["relayActiveLow"] = cfg_->gpio.relayActiveLow;
  gpio["relayActiveMs"] = cfg_->gpio.relayActiveMs;
  gpio["buzzerPin"] = cfg_->gpio.buzzerPin;
  gpio["statusLedPin"] = cfg_->gpio.statusLedPin;
  gpio["doorSensorPin"] = cfg_->gpio.doorSensorPin;
  gpio["doorSensorActiveLow"] = cfg_->gpio.doorSensorActiveLow;
  gpio["exitButtonPin"] = cfg_->gpio.exitButtonPin;

  JsonObject rfid = doc["rfid"].to<JsonObject>();
  rfid["type"] = cfg_->rfid.type;
  rfid["hz1050RxPin"] = cfg_->rfid.hz1050RxPin;
  rfid["hz1050TxPin"] = cfg_->rfid.hz1050TxPin;
  rfid["hz1050Baud"] = cfg_->rfid.hz1050Baud;
  rfid["mfrc522SckPin"] = cfg_->rfid.mfrc522SckPin;
  rfid["mfrc522MisoPin"] = cfg_->rfid.mfrc522MisoPin;
  rfid["mfrc522MosiPin"] = cfg_->rfid.mfrc522MosiPin;
  rfid["mfrc522SdaPin"] = cfg_->rfid.mfrc522SdaPin;
  rfid["mfrc522RstPin"] = cfg_->rfid.mfrc522RstPin;

  JsonObject mqtt = doc["mqtt"].to<JsonObject>();
  mqtt["enabled"] = cfg_->mqtt.enabled;
  mqtt["server"] = cfg_->mqtt.server;
  mqtt["port"] = cfg_->mqtt.port;
  mqtt["username"] = cfg_->mqtt.username;
  mqtt["password"] = cfg_->mqtt.password.isEmpty() ? "" : kPasswordMask;
  mqtt["clientId"] = cfg_->mqtt.clientId;
  mqtt["topicPrefix"] = cfg_->mqtt.topicPrefix;
  mqtt["discoveryPrefix"] = cfg_->mqtt.discoveryPrefix;

  JsonObject auth = doc["auth"].to<JsonObject>();
  auth["enabled"] = cfg_->auth.enabled;
  auth["username"] = cfg_->auth.username;
  auth["password"] = cfg_->auth.passwordHash.isEmpty() ? "" : kPasswordMask;
  auth["sessionTimeoutMin"] = cfg_->auth.sessionTimeoutMin;

  JsonObject lcd = doc["lcd"].to<JsonObject>();
  lcd["enabled"] = cfg_->lcd.enabled;
  lcd["sdaPin"] = cfg_->lcd.sdaPin;
  lcd["sclPin"] = cfg_->lcd.sclPin;
  lcd["address"] = cfg_->lcd.address;
  lcd["cols"] = cfg_->lcd.cols;
  lcd["rows"] = cfg_->lcd.rows;
  sendJsonDoc(request, doc);
}

void WebServerManager::handleConfigPost(AsyncWebServerRequest* request) {
  const String body = request->hasArg("plain") ? request->arg("plain") : "";
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    sendJsonError(request, 400, "Invalid JSON");
    return;
  }

  if (doc["wifi"].is<JsonObject>()) {
    JsonObject w = doc["wifi"];
    if (w["ssid"].is<const char*>()) {
      cfg_->wifi.ssid = w["ssid"].as<const char*>();
    }
    if (w["password"].is<const char*>()) {
      const String p = w["password"].as<const char*>();
      // Maske ile gelen şifre, mevcut şifrenin korunduğunu belirtir
      if (!p.isEmpty() && p != kPasswordMask) {
        cfg_->wifi.password = p;
      }
    }
    if (w["hostname"].is<const char*>()) {
      const String hn = w["hostname"].as<const char*>();
      if (!hn.isEmpty()) {
        cfg_->wifi.hostname = hn;
      }
    }
  }
  if (doc["ntp"].is<JsonObject>()) {
    JsonObject n = doc["ntp"];
    if (n["server"].is<const char*>()) {
      cfg_->ntp.server = n["server"].as<const char*>();
    }
    if (n["utcOffsetMin"].is<int>()) {
      cfg_->ntp.utcOffsetMin = n["utcOffsetMin"].as<int>();
    }
    if (n["autoDst"].is<bool>()) {
      cfg_->ntp.autoDst = n["autoDst"].as<bool>();
    }
  }
  if (doc["gpio"].is<JsonObject>()) {
    JsonObject g = doc["gpio"];
    if (g["relayPin"].is<int>()) {
      cfg_->gpio.relayPin = g["relayPin"].as<int>();
    }
    if (g["relayActiveLow"].is<bool>()) {
      cfg_->gpio.relayActiveLow = g["relayActiveLow"].as<bool>();
    }
    if (g["relayActiveMs"].is<int>()) {
      cfg_->gpio.relayActiveMs = g["relayActiveMs"].as<int>();
    }
    if (g["buzzerPin"].is<int>()) {
      cfg_->gpio.buzzerPin = g["buzzerPin"].as<int>();
    }
    if (g["statusLedPin"].is<int>()) {
      cfg_->gpio.statusLedPin = g["statusLedPin"].as<int>();
    }
    if (g["doorSensorPin"].is<int>()) {
      cfg_->gpio.doorSensorPin = g["doorSensorPin"].as<int>();
    }
    if (g["doorSensorActiveLow"].is<bool>()) {
      cfg_->gpio.doorSensorActiveLow = g["doorSensorActiveLow"].as<bool>();
    }
    if (g["exitButtonPin"].is<int>()) {
      cfg_->gpio.exitButtonPin = g["exitButtonPin"].as<int>();
    }
  }
  if (doc["rfid"].is<JsonObject>()) {
    JsonObject f = doc["rfid"];
    if (f["type"].is<const char*>()) {
      cfg_->rfid.type = f["type"].as<const char*>();
    }
    if (f["hz1050RxPin"].is<int>()) cfg_->rfid.hz1050RxPin = f["hz1050RxPin"].as<int>();
    if (f["hz1050TxPin"].is<int>()) cfg_->rfid.hz1050TxPin = f["hz1050TxPin"].as<int>();
    if (f["hz1050Baud"].is<int>()) {
      const long b = f["hz1050Baud"].as<int>();
      if (b == 9600 || b == 19200) cfg_->rfid.hz1050Baud = b;
    }
    if (f["mfrc522SckPin"].is<int>()) cfg_->rfid.mfrc522SckPin = f["mfrc522SckPin"].as<int>();
    if (f["mfrc522MisoPin"].is<int>()) cfg_->rfid.mfrc522MisoPin = f["mfrc522MisoPin"].as<int>();
    if (f["mfrc522MosiPin"].is<int>()) cfg_->rfid.mfrc522MosiPin = f["mfrc522MosiPin"].as<int>();
    if (f["mfrc522SdaPin"].is<int>()) cfg_->rfid.mfrc522SdaPin = f["mfrc522SdaPin"].as<int>();
    if (f["mfrc522RstPin"].is<int>()) cfg_->rfid.mfrc522RstPin = f["mfrc522RstPin"].as<int>();
  }
  if (doc["mqtt"].is<JsonObject>()) {
    JsonObject m = doc["mqtt"];
    if (m["enabled"].is<bool>()) cfg_->mqtt.enabled = m["enabled"].as<bool>();
    if (m["server"].is<const char*>()) cfg_->mqtt.server = m["server"].as<const char*>();
    if (m["port"].is<int>()) cfg_->mqtt.port = m["port"].as<int>();
    if (m["username"].is<const char*>()) cfg_->mqtt.username = m["username"].as<const char*>();
    if (m["password"].is<const char*>()) {
      const String p = m["password"].as<const char*>();
      if (!p.isEmpty() && p != kPasswordMask) {
        cfg_->mqtt.password = p;
      }
    }
    if (m["clientId"].is<const char*>()) {
      const String c = m["clientId"].as<const char*>();
      if (!c.isEmpty()) cfg_->mqtt.clientId = c;
    }
    if (m["topicPrefix"].is<const char*>()) {
      const String tp = m["topicPrefix"].as<const char*>();
      if (!tp.isEmpty()) cfg_->mqtt.topicPrefix = tp;
    }
    if (m["discoveryPrefix"].is<const char*>()) {
      const String dp = m["discoveryPrefix"].as<const char*>();
      if (!dp.isEmpty()) cfg_->mqtt.discoveryPrefix = dp;
    }
  }

  // Güvenlik (auth) ayarları. Şifre alanı maske ile gelirse mevcut hash korunur.
  if (doc["auth"].is<JsonObject>()) {
    JsonObject a = doc["auth"];
    if (a["enabled"].is<bool>()) cfg_->auth.enabled = a["enabled"].as<bool>();
    if (a["username"].is<const char*>()) {
      const String u = a["username"].as<const char*>();
      if (!u.isEmpty()) cfg_->auth.username = u;
    }
    if (a["password"].is<const char*>()) {
      const String p = a["password"].as<const char*>();
      if (!p.isEmpty() && p != kPasswordMask) {
        cfg_->auth.passwordHash = AuthManager::hashPassword(p);
      }
    }
    if (a["sessionTimeoutMin"].is<int>()) {
      const int t = a["sessionTimeoutMin"].as<int>();
      if (t >= 5 && t <= 1440) cfg_->auth.sessionTimeoutMin = t;
    }
  }

  // LCD ayarları: -1/0 değerler "otomatik algıla" anlamına gelir.
  if (doc["lcd"].is<JsonObject>()) {
    JsonObject l = doc["lcd"];
    if (l["enabled"].is<bool>()) cfg_->lcd.enabled = l["enabled"].as<bool>();
    if (l["sdaPin"].is<int>()) cfg_->lcd.sdaPin = l["sdaPin"].as<int>();
    if (l["sclPin"].is<int>()) cfg_->lcd.sclPin = l["sclPin"].as<int>();
    if (l["address"].is<int>()) cfg_->lcd.address = l["address"].as<int>();
    if (l["cols"].is<int>()) cfg_->lcd.cols = l["cols"].as<int>();
    if (l["rows"].is<int>()) cfg_->lcd.rows = l["rows"].as<int>();
  }

  if (!ConfigManager::save(*cfg_)) {
    sendJsonError(request, 500, "Config save failed");
    return;
  }
  request->send(200, kContentTypeJson, "{\"ok\":true}");

  // Yeni ayarlarla yeniden bağlan, saat dilimini uygula, GPIO pinlerini yeniden kur
  wifi_->applyConfig();
  ntp_->applyConfig();
  gpio_->applyConfig(cfg_->gpio);
  rfid_->applyConfig(*cfg_);
  mqtt_->applyConfig(*cfg_);
  lcd_->applyConfig(*cfg_);
  if (events_) {
    events_->add(EventType::ConfigSave, "Yapılandırma web üzerinden kaydedildi");
  }
}

void WebServerManager::handleWifiScan(AsyncWebServerRequest* request) {
  const int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    JsonDocument doc;
    doc["scanning"] = true;
    sendJsonDoc(request, doc);
    return;
  }
  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true);  // arka planda tarama başlat
    JsonDocument doc;
    doc["scanning"] = true;
    sendJsonDoc(request, doc);
    return;
  }

  JsonDocument doc;
  JsonArray networks = doc["networks"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    JsonObject net = networks.add<JsonObject>();
    net["ssid"] = WiFi.SSID(i);
    net["rssi"] = WiFi.RSSI(i);
    net["open"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
  }
  WiFi.scanDelete();
  sendJsonDoc(request, doc);
}

void WebServerManager::handleRestart(AsyncWebServerRequest* request) {
  request->send(200, kContentTypeJson, "{\"ok\":true}");
  if (events_) {
    events_->add(EventType::Restart, "Web panelinden yeniden başlatma istendi");
  }
  // Yanıtın gönderilmesi için 500ms bekleyip yeniden başlat
  restartAtMs_ = millis() + 500;
}

// ---------------------------------------------------------------------------
// Kimlik doğrulama (auth) API
// ---------------------------------------------------------------------------

void WebServerManager::handleAuthLogin(AsyncWebServerRequest* request) {
  const String body = request->hasArg("plain") ? request->arg("plain") : "";
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    sendJsonError(request, 400, "Invalid JSON");
    return;
  }
  const String username = doc["username"] | "";
  const String password = doc["password"] | "";

  // Reverse proxy arkasında gerçek istemci IP'si X-Forwarded-For'dan gelir.
  String clientIp;
  const String xff = request->header("X-Forwarded-For");
  if (!xff.isEmpty()) {
    int comma = xff.indexOf(',');
    clientIp = comma > 0 ? xff.substring(0, comma) : xff;
    clientIp.trim();
  }
  if (clientIp.isEmpty() && request->client()) {
    clientIp = request->client()->remoteIP().toString();
  }

  // Rate limit: aynı IP için pencere başına maksimum deneme
  if (!auth_->isLoginAllowed(clientIp)) {
    events_->add(EventType::Unauthorized, "Giriş denemesi sınırı aşıldı (IP " + clientIp + ")");
    sendJsonError(request, 429, "Too many login attempts");
    return;
  }

  if (!auth_->authenticate(username, password)) {
    auth_->recordLoginFail(clientIp);
    events_->add(EventType::Unauthorized, "Başarısız giriş denemesi (IP " + clientIp + ")");
    sendJsonError(request, 401, "Invalid credentials");
    return;
  }

  auth_->resetLoginFails(clientIp);
  const String token = auth_->createSession();
  const String csrf = auth_->getCsrf(token);
  events_->add(EventType::Login, "Web girişi: " + username + " (IP " + clientIp + ")");

  JsonDocument resp;
  resp["token"] = token;
  resp["csrf"] = csrf;
  resp["username"] = username;
  sendJsonDoc(request, resp);
}

void WebServerManager::handleAuthLogout(AsyncWebServerRequest* request) {
  const String token = request->header("X-Auth-Token");
  if (!token.isEmpty()) {
    auth_->invalidate(token);
  }
  if (events_) {
    events_->add(EventType::Logout, "Web çıkışı yapıldı");
  }
  request->send(200, kContentTypeJson, "{\"ok\":true}");
}

void WebServerManager::handleAuthStatus(AsyncWebServerRequest* request) {
  const String token = request->header("X-Auth-Token");
  JsonDocument doc;
  doc["enabled"] = auth_->isEnabled();
  doc["authenticated"] = auth_->validateSession(token);
  doc["username"] = cfg_->auth.username;
  sendJsonDoc(request, doc);
}

// ---------------------------------------------------------------------------
// Yedekleme / geri yükleme API
// ---------------------------------------------------------------------------

void WebServerManager::handleBackupGet(AsyncWebServerRequest* request) {
  JsonDocument doc;
  Backup::exportAll(*cfg_, *users_, *schedule_, *holidays_, *accessLog_, *events_, doc);
  String out;
  serializeJsonPretty(doc, out);
  AsyncWebServerResponse* response =
      request->beginResponse(200, "application/json", out);
  response->addHeader("Content-Disposition", "attachment; filename=esp32door-backup.json");
  request->send(response);
}

void WebServerManager::handleBackupPost(AsyncWebServerRequest* request) {
  const String body = request->hasArg("plain") ? request->arg("plain") : "";
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    events_->add(EventType::JsonParseError, "Yedek dosyası parse edilemedi");
    sendJsonError(request, 400, "Invalid JSON");
    return;
  }
  if (!Backup::restoreAll(*cfg_, *users_, *schedule_, *holidays_, *accessLog_,
                          *events_, doc)) {
    sendJsonError(request, 400, "Backup restore failed");
    return;
  }
  events_->add(EventType::ConfigRestore, "Yedekten geri yükleme yapıldı");
  request->send(200, kContentTypeJson, "{\"ok\":true}");

  // Yeni yapılandırma ile sistem servislerini yeniden başlat
  wifi_->applyConfig();
  ntp_->applyConfig();
  gpio_->applyConfig(cfg_->gpio);
  rfid_->applyConfig(*cfg_);
  mqtt_->applyConfig(*cfg_);
  restartAtMs_ = millis() + 1000;
}

// ---------------------------------------------------------------------------
// Firmware (OTA) API
// ---------------------------------------------------------------------------

void WebServerManager::handleOtaStatus(AsyncWebServerRequest* request) {
  JsonDocument doc;
  doc["running"] = otaRunning_;
  doc["progress"] = otaProgress_;
  doc["ready"] = Update.isFinished();
  sendJsonDoc(request, doc);
}

void WebServerManager::handleOtaUpload(AsyncWebServerRequest* request) {
  // Upload tamamlandığında çağrılır.
  if (Update.hasError()) {
    const String err = String(Update.errorString());
    events_->add(EventType::FirmwareUpdate, "Firmware güncelleme HATASI: " + err);
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = err;
    sendJsonDoc(request, doc);
    return;
  }
  if (!Update.end(true)) {
    const String err = String(Update.errorString());
    events_->add(EventType::FirmwareUpdate, "Firmware güncelleme tamamlanamadı: " + err);
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = err;
    sendJsonDoc(request, doc);
    return;
  }
  events_->add(EventType::FirmwareUpdate, "Firmware güncellendi; cihaz yeniden başlatılıyor");
  JsonDocument doc;
  doc["ok"] = true;
  sendJsonDoc(request, doc);
  restartAtMs_ = millis() + 1000;
}

void WebServerManager::handleOtaUploadProgress(AsyncWebServerRequest* request,
                                               const String& filename, size_t index,
                                               uint8_t* data, size_t len, bool final) {
  if (!index) {
    // İlk parça: güncellemeyi başlat
    otaRunning_ = true;
    otaProgress_ = 0;
    Serial.printf("[WEB] OTA başladı: %s (%u byte)\n", filename.c_str(),
                  request->contentLength());
    if (!Update.begin(request->contentLength())) {
      Serial.printf("[WEB] OTA başlatılamadı: %s\n", Update.errorString());
      otaRunning_ = false;
      return;
    }
  }
  const size_t written = Update.write(data, len);
  if (written != len) {
    Serial.printf("[WEB] OTA yazma hatası (%u/%u)\n", (unsigned)written, (unsigned)len);
  }
  // İlerleme yüzdesi (toplam boyut biliniyorsa)
  if (Update.size() > 0) {
    otaProgress_ = (uint8_t)((Update.progress() * 100) / Update.size());
  }
  if (final) {
    otaRunning_ = false;
    Serial.printf("[WEB] OTA tamamlandı (%u%%)\n", otaProgress_);
  }
}

// ---------------------------------------------------------------------------
// Kullanıcı yönetimi API
// ---------------------------------------------------------------------------

void WebServerManager::handleUsersGet(AsyncWebServerRequest* request) {
  JsonDocument doc;
  JsonArray arr = doc["users"].to<JsonArray>();
  for (const User& u : users_->all()) {
    JsonObject obj = arr.add<JsonObject>();
    UserManager::toJson(u, obj);
  }
  doc["count"] = users_->count();
  sendJsonDoc(request, doc);
}

void WebServerManager::handleUserPost(AsyncWebServerRequest* request) {
  const String body = request->hasArg("plain") ? request->arg("plain") : "";
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    sendJsonError(request, 400, "Invalid JSON");
    return;
  }

  User u = UserManager::fromJson(doc.as<JsonObject>());
  if (u.uid.isEmpty()) {
    sendJsonError(request, 400, "uid is required");
    return;
  }
  if (users_->find(u.uid) != nullptr) {
    sendJsonError(request, 409, "uid already exists");
    return;
  }
  u.createdAt = (uint32_t)time(nullptr);
  if (!users_->add(u)) {
    sendJsonError(request, 500, "save failed");
    return;
  }
  if (events_) {
    events_->add(EventType::UserAdded, "Kullanıcı eklendi: " + u.fullName);
  }
  JsonDocument res;
  res["ok"] = true;
  res["uid"] = u.uid;
  sendJsonDoc(request, res);
}

void WebServerManager::handleUserPut(AsyncWebServerRequest* request) {
  // UID, URL'den manuel olarak ayrıştırılır (/api/users/<uid>)
  const String url = request->url();
  const int lastSlash = url.lastIndexOf('/');
  const String uid = lastSlash >= 0 ? url.substring(lastSlash + 1) : "";
  if (uid.isEmpty() || users_->find(uid) == nullptr) {
    sendJsonError(request, 404, "user not found");
    return;
  }

  const String body = request->hasArg("plain") ? request->arg("plain") : "";
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    sendJsonError(request, 400, "Invalid JSON");
    return;
  }

  User u = UserManager::fromJson(doc.as<JsonObject>());
  // UID'yi yol parametresinden koru (gövdendeki değer farklı olsa bile).
  u.uid = uid;
  if (!users_->update(u)) {
    sendJsonError(request, 500, "save failed");
    return;
  }
  request->send(200, kContentTypeJson, "{\"ok\":true}");
}

void WebServerManager::handleUserDelete(AsyncWebServerRequest* request) {
  // UID, URL'den manuel olarak ayrıştırılır (/api/users/<uid>)
  const String url = request->url();
  const int lastSlash = url.lastIndexOf('/');
  const String uid = lastSlash >= 0 ? url.substring(lastSlash + 1) : "";
  const User* u = users_->find(uid);
  if (uid.isEmpty() || u == nullptr) {
    sendJsonError(request, 404, "user not found");
    return;
  }
  const String removedName = u->fullName;
  if (!users_->remove(uid)) {
    sendJsonError(request, 500, "delete failed");
    return;
  }
  if (events_) {
    events_->add(EventType::UserDeleted, "Kullanıcı silindi: " + removedName);
  }
  request->send(200, kContentTypeJson, "{\"ok\":true}");
}

// ---------------------------------------------------------------------------
// Erişim log API
// ---------------------------------------------------------------------------

// Sorgu parametrelerinden sonuç tipi filtresi: all (varsayılan), allowed, denied
static bool resultFilterMatches(AccessResult r, const String& filter) {
  if (filter.isEmpty() || filter == "all") {
    return true;
  }
  if (filter == "allowed") {
    return r == AccessResult::Allowed;
  }
  if (filter == "denied") {
    return r == AccessResult::Denied;
  }
  return true;
}

void WebServerManager::handleLogsGet(AsyncWebServerRequest* request) {
  const String resultF =
      request->hasParam("result") ? request->getParam("result")->value() : "";
  const String q = request->hasParam("q") ? request->getParam("q")->value() : "";
  const String limitS =
      request->hasParam("limit") ? request->getParam("limit")->value() : "100";
  const String sinceS =
      request->hasParam("since") ? request->getParam("since")->value() : "";
  const String untilS =
      request->hasParam("until") ? request->getParam("until")->value() : "";

  int limit = limitS.toInt();
  if (limit <= 0 || limit > 500) {
    limit = 100;
  }
  const uint32_t since = sinceS.toInt();
  const uint32_t until = untilS.toInt();

  JsonDocument doc;
  JsonArray arr = doc["entries"].to<JsonArray>();
  size_t total = 0;
  const std::deque<AccessRecord>& log = accessLog_->entries();

  // En yeni kayıttan eskiye doğru tara; sınır dolunca dur.
  for (auto it = log.rbegin(); it != log.rend() && arr.size() < (size_t)limit; ++it) {
    const AccessRecord& r = *it;
    if (!resultFilterMatches(r.result, resultF)) {
      continue;
    }
    if (!q.isEmpty()) {
      if (!r.uid.startsWith(q) && r.name.indexOf(q) < 0) {
        continue;
      }
    }
    if (since && r.timestamp < since) {
      continue;
    }
    if (until && r.timestamp > until) {
      continue;
    }
    total++;
    JsonObject o = arr.add<JsonObject>();
    AccessLog::toJson(r, o);
    o["ts_str"] = formatTs(r.timestamp);
  }

  doc["count"] = arr.size();
  doc["total"] = total;
  sendJsonDoc(request, doc);
}

void WebServerManager::handleLogsCsv(AsyncWebServerRequest* request) {
  String csv = F("id,tarih,uid,ad_soyad,kapi,sonuc,sebep,ip,rssi\r\n");
  const std::deque<AccessRecord>& log = accessLog_->entries();
  for (auto it = log.rbegin(); it != log.rend(); ++it) {
    const AccessRecord& r = *it;
    csv += String(r.id) + "," + csvEsc(formatTs(r.timestamp)) + "," +
           csvEsc(r.uid) + "," + csvEsc(r.name) + "," + String(r.door) + "," +
           AccessLog::resultName(r.result) + "," + AccessLog::reasonName(r.reason) +
           "," + csvEsc(r.ip) + "," + String(r.rssi) + "\r\n";
    if (csv.length() > 48000) {  // bellek güvenliği: 48KB üzerini kes
      break;
    }
  }
  AsyncWebServerResponse* response =
      request->beginResponse(200, kContentTypeCsv, csv);
  response->addHeader("Content-Disposition", "attachment; filename=access_log.csv");
  request->send(response);
}

void WebServerManager::handleLogsClear(AsyncWebServerRequest* request) {
  accessLog_->clear();
  request->send(200, kContentTypeJson, "{\"ok\":true}");
}

// ---------------------------------------------------------------------------
// Sistem olay (event) log API
// ---------------------------------------------------------------------------

void WebServerManager::handleEventLogsGet(AsyncWebServerRequest* request) {
  const String typeF =
      request->hasParam("type") ? request->getParam("type")->value() : "";
  const String q = request->hasParam("q") ? request->getParam("q")->value() : "";
  const String limitS =
      request->hasParam("limit") ? request->getParam("limit")->value() : "100";

  int limit = limitS.toInt();
  if (limit <= 0 || limit > 500) {
    limit = 100;
  }

  JsonDocument doc;
  JsonArray arr = doc["entries"].to<JsonArray>();
  size_t total = 0;
  const std::deque<EventRecord>& log = events_->entries();

  for (auto it = log.rbegin(); it != log.rend() && arr.size() < (size_t)limit; ++it) {
    const EventRecord& r = *it;
    if (!typeF.isEmpty() && String(EventLog::typeName(r.type)) != typeF) {
      continue;
    }
    if (!q.isEmpty() && r.message.indexOf(q) < 0) {
      continue;
    }
    total++;
    JsonObject o = arr.add<JsonObject>();
    EventLog::toJson(r, o);
    o["ts_str"] = formatTs(r.timestamp);
  }

  doc["count"] = arr.size();
  doc["total"] = total;
  sendJsonDoc(request, doc);
}

void WebServerManager::handleEventLogsCsv(AsyncWebServerRequest* request) {
  String csv = F("id,tarih,tip,mesaj\r\n");
  const std::deque<EventRecord>& log = events_->entries();
  for (auto it = log.rbegin(); it != log.rend(); ++it) {
    const EventRecord& r = *it;
    csv += String(r.id) + "," + csvEsc(formatTs(r.timestamp)) + "," +
           csvEsc(EventLog::typeName(r.type)) + "," + csvEsc(r.message) + "\r\n";
    if (csv.length() > 48000) {
      break;
    }
  }
  AsyncWebServerResponse* response =
      request->beginResponse(200, kContentTypeCsv, csv);
  response->addHeader("Content-Disposition", "attachment; filename=event_log.csv");
  request->send(response);
}

void WebServerManager::handleEventLogsClear(AsyncWebServerRequest* request) {
  events_->clear();
  request->send(200, kContentTypeJson, "{\"ok\":true}");
}

// ---------------------------------------------------------------------------
// MQTT API
// ---------------------------------------------------------------------------

void WebServerManager::handleMqttGet(AsyncWebServerRequest* request) {
  JsonDocument doc;
  JsonObject cfg = doc["config"].to<JsonObject>();
  cfg["enabled"] = cfg_->mqtt.enabled;
  cfg["server"] = cfg_->mqtt.server;
  cfg["port"] = cfg_->mqtt.port;
  cfg["username"] = cfg_->mqtt.username;
  cfg["password"] = cfg_->mqtt.password.isEmpty() ? "" : kPasswordMask;
  cfg["clientId"] = cfg_->mqtt.clientId;
  cfg["topicPrefix"] = cfg_->mqtt.topicPrefix;
  cfg["discoveryPrefix"] = cfg_->mqtt.discoveryPrefix;
  doc["connected"] = mqtt_->isConnected();
  doc["enabled"] = mqtt_->isEnabled();
  sendJsonDoc(request, doc);
}

void WebServerManager::handleMqttPost(AsyncWebServerRequest* request) {
  const String body = request->hasArg("plain") ? request->arg("plain") : "";
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    sendJsonError(request, 400, "Invalid JSON");
    return;
  }
  JsonObject m = doc.as<JsonObject>();
  if (m["enabled"].is<bool>()) cfg_->mqtt.enabled = m["enabled"].as<bool>();
  if (m["server"].is<const char*>()) cfg_->mqtt.server = m["server"].as<const char*>();
  if (m["port"].is<int>()) cfg_->mqtt.port = m["port"].as<int>();
  if (m["username"].is<const char*>()) cfg_->mqtt.username = m["username"].as<const char*>();
  if (m["password"].is<const char*>()) {
    const String p = m["password"].as<const char*>();
    if (!p.isEmpty() && p != kPasswordMask) {
      cfg_->mqtt.password = p;
    }
  }
  if (m["clientId"].is<const char*>()) {
    const String c = m["clientId"].as<const char*>();
    if (!c.isEmpty()) cfg_->mqtt.clientId = c;
  }
  if (m["topicPrefix"].is<const char*>()) {
    const String tp = m["topicPrefix"].as<const char*>();
    if (!tp.isEmpty()) cfg_->mqtt.topicPrefix = tp;
  }
  if (m["discoveryPrefix"].is<const char*>()) {
    const String dp = m["discoveryPrefix"].as<const char*>();
    if (!dp.isEmpty()) cfg_->mqtt.discoveryPrefix = dp;
  }

  if (!ConfigManager::save(*cfg_)) {
    sendJsonError(request, 500, "Config save failed");
    return;
  }
  mqtt_->applyConfig(*cfg_);
  if (events_) {
    events_->add(EventType::ConfigSave, "MQTT ayarları güncellendi");
  }
  request->send(200, kContentTypeJson, "{\"ok\":true}");
}

// ---------------------------------------------------------------------------
// RFID kayıt (enrollment) API
//   POST /api/rfid/enroll  {"enroll": true|false}
//   GET  /api/rfid/enroll  -> {enroll: bool, uid: "..."}
// ---------------------------------------------------------------------------

void WebServerManager::handleRfidEnroll(AsyncWebServerRequest* request) {
  if (request->method() == HTTP_GET) {
    JsonDocument doc;
    doc["enroll"] = rfid_->enrollMode();
    doc["uid"] = rfid_->takeEnrollUid();
    sendJsonDoc(request, doc);
    return;
  }

  const String body = request->hasArg("plain") ? request->arg("plain") : "";
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    sendJsonError(request, 400, "Invalid JSON");
    return;
  }
  const bool on = doc["enroll"] | false;
  rfid_->setEnrollMode(on);
  JsonDocument res;
  res["ok"] = true;
  res["enroll"] = on;
  sendJsonDoc(request, res);
}

// ---------------------------------------------------------------------------
// GPIO / Donanım API
// ---------------------------------------------------------------------------

// GpioConfig'i JSON'a yazar (config GET ile paylaşılan mantık).
static void gpioToJson(const GpioConfig& cfg, JsonObject o) {
  o["relayPin"] = cfg.relayPin;
  o["relayActiveLow"] = cfg.relayActiveLow;
  o["relayActiveMs"] = cfg.relayActiveMs;
  o["buzzerPin"] = cfg.buzzerPin;
  o["statusLedPin"] = cfg.statusLedPin;
  o["doorSensorPin"] = cfg.doorSensorPin;
  o["doorSensorActiveLow"] = cfg.doorSensorActiveLow;
  o["exitButtonPin"] = cfg.exitButtonPin;
}

void WebServerManager::handleGpioGet(AsyncWebServerRequest* request) {
  JsonDocument doc;
  gpioToJson(cfg_->gpio, doc["config"].to<JsonObject>());
  doc["state"] = gpio_->isDoorOpen() ? "open" : "closed";
  doc["doorOpenPhysical"] = gpio_->isDoorOpenPhysical();
  doc["exitPressed"] = gpio_->isExitButtonPressed();
  sendJsonDoc(request, doc);
}

void WebServerManager::handleGpioPost(AsyncWebServerRequest* request) {
  const String body = request->hasArg("plain") ? request->arg("plain") : "";
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    sendJsonError(request, 400, "Invalid JSON");
    return;
  }
  if (!doc.is<JsonObject>()) {
    sendJsonError(request, 400, "Invalid JSON");
    return;
  }
  JsonObject g = doc.as<JsonObject>();
  if (g["relayPin"].is<int>()) cfg_->gpio.relayPin = g["relayPin"].as<int>();
  if (g["relayActiveLow"].is<bool>()) cfg_->gpio.relayActiveLow = g["relayActiveLow"].as<bool>();
  if (g["relayActiveMs"].is<int>()) {
    const int v = g["relayActiveMs"].as<int>();
    // Yalnızca izin verilen süreler (500, 1000, 2000, 5000, 10000 ms)
    if (v == 500 || v == 1000 || v == 2000 || v == 5000 || v == 10000) {
      cfg_->gpio.relayActiveMs = v;
    }
  }
  if (g["buzzerPin"].is<int>()) cfg_->gpio.buzzerPin = g["buzzerPin"].as<int>();
  if (g["statusLedPin"].is<int>()) cfg_->gpio.statusLedPin = g["statusLedPin"].as<int>();
  if (g["doorSensorPin"].is<int>()) cfg_->gpio.doorSensorPin = g["doorSensorPin"].as<int>();
  if (g["doorSensorActiveLow"].is<bool>()) cfg_->gpio.doorSensorActiveLow = g["doorSensorActiveLow"].as<bool>();
  if (g["exitButtonPin"].is<int>()) cfg_->gpio.exitButtonPin = g["exitButtonPin"].as<int>();

  if (!ConfigManager::save(*cfg_)) {
    sendJsonError(request, 500, "Config save failed");
    return;
  }
  gpio_->applyConfig(cfg_->gpio);
  request->send(200, kContentTypeJson, "{\"ok\":true}");
}

// Test / MQTT için donanım aksiyonları: {"action":"open|close|beep|led"}
void WebServerManager::handleGpioAction(AsyncWebServerRequest* request) {
  const String body = request->hasArg("plain") ? request->arg("plain") : "";
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    sendJsonError(request, 400, "Invalid JSON");
    return;
  }
  const String action = doc["action"] | "";
  if (action == "open") {
    gpio_->openDoor(cfg_->gpio.relayActiveMs);
    gpio_->beepOpen();
  } else if (action == "close") {
    gpio_->closeDoorNow();
  } else if (action == "beep") {
    gpio_->beep();
  } else if (action == "melody") {
    // Melodi testi: {"action":"melody","melody":"success|denied|open"}
    const String melody = doc["melody"] | "open";
    if (melody == "success") {
      gpio_->beepSuccess();
    } else if (melody == "denied") {
      gpio_->beepDenied();
    } else {
      gpio_->beepOpen();
    }
  } else if (action == "led") {
    gpio_->blinkLed();
  } else {
    sendJsonError(request, 400, "unknown action");
    return;
  }
  request->send(200, kContentTypeJson, "{\"ok\":true}");
}

// ---------------------------------------------------------------------------
// LCD (I2C 16x2) API
// ---------------------------------------------------------------------------

void WebServerManager::handleLcdGet(AsyncWebServerRequest* request) {
  JsonDocument doc;
  JsonObject c = doc["config"].to<JsonObject>();
  c["enabled"] = cfg_->lcd.enabled;
  c["sdaPin"] = cfg_->lcd.sdaPin;
  c["sclPin"] = cfg_->lcd.sclPin;
  c["address"] = cfg_->lcd.address;
  c["cols"] = cfg_->lcd.cols;
  c["rows"] = cfg_->lcd.rows;
  doc["present"] = lcd_->isPresent();
  doc["detectedSda"] = lcd_->sdaPin();
  doc["detectedScl"] = lcd_->sclPin();
  doc["detectedAddress"] = lcd_->address();
  sendJsonDoc(request, doc);
}

void WebServerManager::handleLcdPost(AsyncWebServerRequest* request) {
  const String body = request->hasArg("plain") ? request->arg("plain") : "";
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    sendJsonError(request, 400, "Invalid JSON");
    return;
  }
  if (!doc.is<JsonObject>()) {
    sendJsonError(request, 400, "Invalid JSON");
    return;
  }
  JsonObject g = doc.as<JsonObject>();
  if (g["enabled"].is<bool>()) cfg_->lcd.enabled = g["enabled"].as<bool>();
  if (g["sdaPin"].is<int>()) cfg_->lcd.sdaPin = g["sdaPin"].as<int>();
  if (g["sclPin"].is<int>()) cfg_->lcd.sclPin = g["sclPin"].as<int>();
  if (g["address"].is<int>()) cfg_->lcd.address = g["address"].as<int>();
  if (g["cols"].is<int>()) cfg_->lcd.cols = g["cols"].as<int>();
  if (g["rows"].is<int>()) cfg_->lcd.rows = g["rows"].as<int>();

  if (!ConfigManager::save(*cfg_)) {
    sendJsonError(request, 500, "Config save failed");
    return;
  }
  lcd_->applyConfig(*cfg_);
  request->send(200, kContentTypeJson, "{\"ok\":true}");
}

// LCD aksiyonları: {"action":"scan|test|clear"}
// scan  -> pin/adres otomatik algılama (bulunan pinler kaydedilir)
// test  -> ekrana 3 sn test mesajı göster
// clear -> kalıcı mesajları temizle, durum satırlarına dön
void WebServerManager::handleLcdAction(AsyncWebServerRequest* request) {
  const String body = request->hasArg("plain") ? request->arg("plain") : "";
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    sendJsonError(request, 400, "Invalid JSON");
    return;
  }
  const String action = doc["action"] | "";
  if (action == "scan") {
    const bool found = lcd_->detect();
    JsonDocument out;
    out["ok"] = true;
    out["found"] = found;
    if (found) {
      out["sdaPin"] = lcd_->sdaPin();
      out["sclPin"] = lcd_->sclPin();
      out["address"] = lcd_->address();
      // Algılanan değerler kalıcı olarak kaydedilsin (sonraki açılış hızlı olsun).
      ConfigManager::save(*cfg_);
    }
    sendJsonDoc(request, out);
  } else if (action == "test") {
    if (!lcd_->isPresent()) {
      sendJsonError(request, 400, "LCD not detected");
      return;
    }
    lcd_->showTest();
    request->send(200, kContentTypeJson, "{\"ok\":true}");
  } else if (action == "clear") {
    lcd_->showBoot();
    request->send(200, kContentTypeJson, "{\"ok\":true}");
  } else {
    sendJsonError(request, 400, "unknown action");
  }
}

// ---------------------------------------------------------------------------
// RFID / Okuyucu API
// ---------------------------------------------------------------------------

void WebServerManager::handleRfidGet(AsyncWebServerRequest* request) {
  JsonDocument doc;
  JsonObject cfg = doc["config"].to<JsonObject>();
  cfg["type"] = cfg_->rfid.type;
  cfg["hz1050RxPin"] = cfg_->rfid.hz1050RxPin;
  cfg["hz1050TxPin"] = cfg_->rfid.hz1050TxPin;
  cfg["hz1050Baud"] = cfg_->rfid.hz1050Baud;
  cfg["mfrc522SckPin"] = cfg_->rfid.mfrc522SckPin;
  cfg["mfrc522MisoPin"] = cfg_->rfid.mfrc522MisoPin;
  cfg["mfrc522MosiPin"] = cfg_->rfid.mfrc522MosiPin;
  cfg["mfrc522SdaPin"] = cfg_->rfid.mfrc522SdaPin;
  cfg["mfrc522RstPin"] = cfg_->rfid.mfrc522RstPin;
  doc["active"] = rfid_->readerName();
  doc["ready"] = rfid_->isInitialized();
  doc["enroll"] = rfid_->enrollMode();
  doc["lastUid"] = rfid_->lastUid();
  sendJsonDoc(request, doc);
}

void WebServerManager::handleRfidPost(AsyncWebServerRequest* request) {
  const String body = request->hasArg("plain") ? request->arg("plain") : "";
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    sendJsonError(request, 400, "Invalid JSON");
    return;
  }
  JsonObject f = doc.as<JsonObject>();
  if (f["type"].is<const char*>()) {
    const String t = f["type"].as<const char*>();
    if (t == "auto" || t == "hz1050" || t == "mfrc522") {
      cfg_->rfid.type = t;
    }
  }
  if (f["hz1050RxPin"].is<int>()) cfg_->rfid.hz1050RxPin = f["hz1050RxPin"].as<int>();
  if (f["hz1050TxPin"].is<int>()) cfg_->rfid.hz1050TxPin = f["hz1050TxPin"].as<int>();
  if (f["hz1050Baud"].is<int>()) {
    const long b = f["hz1050Baud"].as<int>();
    if (b == 9600 || b == 19200) cfg_->rfid.hz1050Baud = b;
  }
  if (f["mfrc522SckPin"].is<int>()) cfg_->rfid.mfrc522SckPin = f["mfrc522SckPin"].as<int>();
  if (f["mfrc522MisoPin"].is<int>()) cfg_->rfid.mfrc522MisoPin = f["mfrc522MisoPin"].as<int>();
  if (f["mfrc522MosiPin"].is<int>()) cfg_->rfid.mfrc522MosiPin = f["mfrc522MosiPin"].as<int>();
  if (f["mfrc522SdaPin"].is<int>()) cfg_->rfid.mfrc522SdaPin = f["mfrc522SdaPin"].as<int>();
  if (f["mfrc522RstPin"].is<int>()) cfg_->rfid.mfrc522RstPin = f["mfrc522RstPin"].as<int>();

  if (!ConfigManager::save(*cfg_)) {
    sendJsonError(request, 500, "Config save failed");
    return;
  }
  // Okuyucuyu yeni ayarlarla hemen yeniden başlat
  rfid_->applyConfig(*cfg_);
  request->send(200, kContentTypeJson, "{\"ok\":true}");
}

// ---------------------------------------------------------------------------
// Zaman planı (haftalık geçiş takvimi) API
// ---------------------------------------------------------------------------

void WebServerManager::handleScheduleGet(AsyncWebServerRequest* request) {
  JsonDocument doc;
  JsonArray arr = doc["schedules"].to<JsonArray>();
  for (const auto& kv : schedule_->all()) {
    JsonObject o = arr.add<JsonObject>();
    TimeSchedule::toJson(kv.second, o);
  }
  doc["count"] = schedule_->all().size();
  sendJsonDoc(request, doc);
}

// GET /api/schedule/<uid> - tek kullanıcının planı
void WebServerManager::handleScheduleGetOne(AsyncWebServerRequest* request) {
  const String url = request->url();
  const int lastSlash = url.lastIndexOf('/');
  const String uid = lastSlash >= 0 ? url.substring(lastSlash + 1) : "";
  if (uid.isEmpty()) {
    sendJsonError(request, 400, "uid is required");
    return;
  }
  JsonDocument doc;
  JsonObject o = doc["schedule"].to<JsonObject>();
  TimeSchedule::toJson(schedule_->get(uid), o);
  sendJsonDoc(request, doc);
}

// UID yol parametresi olarak gelir: PUT /api/schedule/<uid>
void WebServerManager::handleSchedulePut(AsyncWebServerRequest* request) {
  const String url = request->url();
  const int lastSlash = url.lastIndexOf('/');
  const String uid = lastSlash >= 0 ? url.substring(lastSlash + 1) : "";
  if (uid.isEmpty()) {
    sendJsonError(request, 400, "uid is required");
    return;
  }
  const String body = request->hasArg("plain") ? request->arg("plain") : "";
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    sendJsonError(request, 400, "Invalid JSON");
    return;
  }
  WeeklySchedule s = TimeSchedule::fromJson(doc.as<JsonObject>());
  s.uid = uid;  // yol parametresi her zaman geçerli
  schedule_->set(s);
  request->send(200, kContentTypeJson, "{\"ok\":true}");
}

void WebServerManager::handleScheduleDelete(AsyncWebServerRequest* request) {
  const String url = request->url();
  const int lastSlash = url.lastIndexOf('/');
  const String uid = lastSlash >= 0 ? url.substring(lastSlash + 1) : "";
  if (uid.isEmpty()) {
    sendJsonError(request, 400, "uid is required");
    return;
  }
  schedule_->remove(uid);
  request->send(200, kContentTypeJson, "{\"ok\":true}");
}

// ---------------------------------------------------------------------------
// Tatil günleri API
// ---------------------------------------------------------------------------

void WebServerManager::handleHolidayGet(AsyncWebServerRequest* request) {
  JsonDocument doc;
  JsonArray arr = doc["holidays"].to<JsonArray>();
  for (const auto& kv : holidays_->all()) {
    JsonObject o = arr.add<JsonObject>();
    o["date"] = kv.first;
    o["name"] = kv.second;
  }
  doc["count"] = holidays_->count();
  sendJsonDoc(request, doc);
}

void WebServerManager::handleHolidayPost(AsyncWebServerRequest* request) {
  const String body = request->hasArg("plain") ? request->arg("plain") : "";
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    sendJsonError(request, 400, "Invalid JSON");
    return;
  }
  const String date = doc["date"] | "";
  const String name = doc["name"] | "";
  if (date.isEmpty()) {
    sendJsonError(request, 400, "date is required");
    return;
  }
  if (!holidays_->add(date, name)) {
    sendJsonError(request, 400, "invalid date (YYYY-MM-DD)");
    return;
  }
  request->send(200, kContentTypeJson, "{\"ok\":true}");
}

// DELETE /api/holidays/<YYYY-MM-DD>
void WebServerManager::handleHolidayDelete(AsyncWebServerRequest* request) {
  const String url = request->url();
  const int lastSlash = url.lastIndexOf('/');
  const String date = lastSlash >= 0 ? url.substring(lastSlash + 1) : "";
  if (date.isEmpty()) {
    sendJsonError(request, 400, "date is required");
    return;
  }
  holidays_->remove(date);
  request->send(200, kContentTypeJson, "{\"ok\":true}");
}

void WebServerManager::handleNotFound(AsyncWebServerRequest* request) {
  // Captive portal yok: bilinmeyen yollar 404 döndürür, otomatik yönlendirme yapılmaz.
  (void)request;
  request->send(404, "text/plain", "Not Found");
}
