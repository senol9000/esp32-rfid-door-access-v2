#pragma once

#include <Arduino.h>

enum class ReaderType : uint8_t {
  Auto = 0,
  HZ1050 = 1,
  MFRC522 = 2
};

struct AppState {
  bool wifiConnected = false;
  bool mqttConnected = false;
  bool ntpSynced = false;
  String ipAddress;
};
