#pragma once

#include <Arduino.h>
#include "Types.h"
#include "auth/AuthManager.h"
#include "config/Config.h"
#include "core/AccessEngine.h"
#include "core/UserManager.h"
#include "gpio/GpioManager.h"
#include "lcd/LcdManager.h"
#include "log/AccessLog.h"
#include "log/EventLog.h"
#include "mqtt/MqttManager.h"
#include "net/WifiManager.h"
#include "net/PingWatchdog.h"
#include "ntp/NtpManager.h"
#include "rfid/RfidManager.h"
#include "scheduler/HolidayManager.h"
#include "scheduler/TimeSchedule.h"
#include "web/WebServerManager.h"

class App {
 public:
  App()
      : access_(users_, schedule_, holidays_, gpio_, ntp_, accessLog_) {}

  void begin();
  void loop();

 private:
  AppConfig config_;
  WifiManager wifi_;
  NtpManager ntp_;
  PingWatchdog watchdog_;
  UserManager users_;
  AccessLog accessLog_;
  EventLog events_;
  GpioManager gpio_;
  LcdManager lcd_;
  MqttManager mqtt_;
  RfidManager rfid_;
  TimeSchedule schedule_;
  HolidayManager holidays_;
  AccessEngine access_;
  AuthManager auth_;
  WebServerManager web_;
  unsigned long lastTickMs_ = 0;

  // Olay logu için WiFi/NTP durum geçiş takibi
  bool wasWifiConnected_ = false;
  bool wasNtpSynced_ = false;
  unsigned long lastWifiCheckMs_ = 0;
};
