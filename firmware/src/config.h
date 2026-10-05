#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "core/ads1115_regs.h"
#include "core/moisture.h"
#include "core/watering_policy.h"

namespace wat {

constexpr uint8_t kMaxModules = ads::kAddressCount;
constexpr size_t kMaxNameBytes = 40;

struct ModuleConfig {
  bool enabled = false;
  String name;
  ModuleMode mode = ModuleMode::Moisture;
  uint16_t minuteOfDay = 7 * 60;
  uint8_t everyNDays = 1;
  uint8_t thresholdPct = 35;
  uint16_t durationSec = 30;
  // Typical v1.2 capacitive probe on 3.3 V: ~2.2 V in air, ~0.9 V in water.
  int16_t dryRaw = 17600;
  int16_t wetRaw = 7200;
  bool lineSense = false;  // 100k/100k divider from the 5 V line fitted on A1

  Calibration calibration() const { return {dryRaw, wetRaw}; }
};

struct AppConfig {
  ModuleConfig modules[kMaxModules];

  String wifiSsid;
  String wifiPassword;
  String apPassword = "watering";
  String hostname = "watering";
  String timezone = "MSK-3";  // POSIX TZ string
  String ntpServer = "pool.ntp.org";
  String adminPassword;  // empty = no HTTP auth

  bool batteryCheck = true;
  uint16_t batteryMinMv = 3500;    // resting voltage needed to start the pump
  uint16_t batteryAbortMv = 3300;  // under load, sustained for 2 s -> stop
  float batteryCalibration = 1.0f;

  bool waterCheck = true;
  bool waterPresentHigh = true;  // pin level that means "water is there"

  uint16_t maxPumpSec = 300;
  uint16_t valveLeadMs = 300;  // valve opens before the pump starts
  uint16_t valveTailMs = 500;  // valve closes after the pump stops
  uint16_t catchUpMinutes = 180;

  // Button session: Wi-Fi stays on until a second press or this many
  // minutes without a press / web request - a forgotten session would
  // otherwise empty the 18650 in about a day.
  uint8_t wifiSessionMinutes = 10;
  // Silent NTP on a scheduled wake (station only, no AP, no icon); the RC
  // clock drifts ~15 min/day without it. 0 = only during button sessions.
  uint8_t ntpEveryDays = 7;
};

bool loadConfig(AppConfig &config);
bool saveConfig(const AppConfig &config);

// Secrets are reported as "...Set": true/false, never echoed back.
void configToJson(const AppConfig &config, JsonObject out);

// Partial update: only keys present in `in` are applied. Validation errors
// leave `config` untouched and fill `error` with a short Russian message.
bool configFromJson(JsonObjectConst in, AppConfig &config, String &error);

String formatMinuteOfDay(uint16_t minuteOfDay);
bool parseMinuteOfDay(const char *text, uint16_t &out);

}  // namespace wat
