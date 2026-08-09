#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "auth/AuthManager.h"
#include "config/Config.h"
#include "core/UserManager.h"
#include "gpio/GpioManager.h"
#include "lcd/LcdManager.h"
#include "log/AccessLog.h"
#include "log/EventLog.h"
#include "mqtt/MqttManager.h"
#include "net/WifiManager.h"
#include "ntp/NtpManager.h"
#include "rfid/RfidManager.h"
#include "scheduler/HolidayManager.h"
#include "scheduler/TimeSchedule.h"

/**
 * Async web sunucusu.
 * - Statik paneli LittleFS'ten sunar.
 * - REST tabanlı /api uç noktaları: status, dashboard, config, wifi,
 *   users CRUD, access log sorgu/CSV/temizleme, event log, mqtt, gpio,
 *   rfid (kayıt modu dahil), schedule, holiday, restart.
 */
class WebServerManager {
 public:
  void begin(AppConfig& cfg, WifiManager& wifi, UserManager& users,
             AccessLog& accessLog, NtpManager& ntp, GpioManager& gpio,
             RfidManager& rfid, TimeSchedule& schedule, HolidayManager& holidays,
             EventLog& events, MqttManager& mqtt, AuthManager& auth,
             LcdManager& lcd);
  void loop();

 private:
  AsyncWebServer server_{80};
  AppConfig* cfg_ = nullptr;
  WifiManager* wifi_ = nullptr;
  UserManager* users_ = nullptr;
  AccessLog* accessLog_ = nullptr;
  NtpManager* ntp_ = nullptr;
  GpioManager* gpio_ = nullptr;
  RfidManager* rfid_ = nullptr;
  TimeSchedule* schedule_ = nullptr;
  HolidayManager* holidays_ = nullptr;
  EventLog* events_ = nullptr;
  MqttManager* mqtt_ = nullptr;
  AuthManager* auth_ = nullptr;
  LcdManager* lcd_ = nullptr;
  unsigned long restartAtMs_ = 0;

  // OTA durum bilgisi (ilerleme çubuğu için)
  volatile uint8_t otaProgress_ = 0;
  volatile bool otaRunning_ = false;

  void setupRoutes();
  void setupAuthMiddleware();
  bool isPublicPath(const String& url) const;
  bool isSafeMethod(WebRequestMethod method) const;

  // ESPAsyncWebServer 3.x, application/json gövdesini arg("plain")'e yazmaz.
  // onBody callback'i ile parçaları request->_tempObject'te toplarız.
  static void collectBody(AsyncWebServerRequest* request, uint8_t* data, size_t len,
                          size_t index, size_t total);
  static String readBody(AsyncWebServerRequest* request);

  void handleAuthLogin(AsyncWebServerRequest* request);
  void handleAuthLogout(AsyncWebServerRequest* request);
  void handleAuthStatus(AsyncWebServerRequest* request);
  void handleBackupGet(AsyncWebServerRequest* request);
  void handleBackupPost(AsyncWebServerRequest* request);
  void handleOtaUpload(AsyncWebServerRequest* request);
  void handleOtaUploadProgress(AsyncWebServerRequest* request, const String& filename,
                               size_t index, uint8_t* data, size_t len, bool final);
  void handleOtaStatus(AsyncWebServerRequest* request);
  void handleIndex(AsyncWebServerRequest* request);
  void handleStatus(AsyncWebServerRequest* request);
  void handleDashboard(AsyncWebServerRequest* request);
  void handleConfigGet(AsyncWebServerRequest* request);
  void handleConfigPost(AsyncWebServerRequest* request);
  void handleWifiScan(AsyncWebServerRequest* request);
  void handleRestart(AsyncWebServerRequest* request);
  void handleUsersGet(AsyncWebServerRequest* request);
  void handleUserPost(AsyncWebServerRequest* request);
  void handleUserPut(AsyncWebServerRequest* request);
  void handleUserDelete(AsyncWebServerRequest* request);
  void handleLogsGet(AsyncWebServerRequest* request);
  void handleLogsCsv(AsyncWebServerRequest* request);
  void handleLogsClear(AsyncWebServerRequest* request);
  void handleEventLogsGet(AsyncWebServerRequest* request);
  void handleEventLogsCsv(AsyncWebServerRequest* request);
  void handleEventLogsClear(AsyncWebServerRequest* request);
  void handleMqttGet(AsyncWebServerRequest* request);
  void handleMqttPost(AsyncWebServerRequest* request);
  void handleGpioGet(AsyncWebServerRequest* request);
  void handleGpioPost(AsyncWebServerRequest* request);
  void handleGpioAction(AsyncWebServerRequest* request);
  void handleLcdGet(AsyncWebServerRequest* request);
  void handleLcdPost(AsyncWebServerRequest* request);
  void handleLcdAction(AsyncWebServerRequest* request);
  void handleRfidGet(AsyncWebServerRequest* request);
  void handleRfidPost(AsyncWebServerRequest* request);
  void handleRfidEnroll(AsyncWebServerRequest* request);
  void handleScheduleGet(AsyncWebServerRequest* request);
  void handleScheduleGetOne(AsyncWebServerRequest* request);
  void handleSchedulePut(AsyncWebServerRequest* request);
  void handleScheduleDelete(AsyncWebServerRequest* request);
  void handleHolidayGet(AsyncWebServerRequest* request);
  void handleHolidayPost(AsyncWebServerRequest* request);
  void handleHolidayDelete(AsyncWebServerRequest* request);
  void handleNotFound(AsyncWebServerRequest* request);
};
