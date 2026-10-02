#pragma once

#include <stdint.h>

// On-flash log format: codes are stored as numbers and rendered to text by the
// web UI (web/index.html, EVENT_TEXT). Never renumber - old records would
// silently change meaning. Append new codes at the end of a group.
namespace wat {

enum class LogLevel : uint8_t { Info = 0, Warn = 1, Error = 2 };

constexpr uint8_t kNoModule = 0xFF;

enum class Event : uint16_t {
  Boot = 1,             // a = esp_reset_reason()
  ResetDuringPump = 2,  // a = esp_reset_reason(); pump was on when the chip reset
  TimeSynced = 3,       // a = 0 NTP, 1 browser
  TimeInvalid = 4,      // scheduled checks are paused until time is known
  ConfigSaved = 5,
  WifiConnected = 6,  // a = RSSI
  WifiApStarted = 7,
  ValveOpenAtBoot = 8,  // module had ALERT asserted after reset; closed

  ModuleOnline = 10,
  ModuleOffline = 11,
  Measured = 12,     // a = raw, b = percent
  SensorError = 13,  // I2C read failed

  WateringSkipped = 20,   // a = WaterDecision, b = percent
  WateringStarted = 21,   // a = planned seconds, b = JobKind
  WateringDone = 22,      // a = actual seconds
  WateringAborted = 23,   // a = AbortReason, b = actual seconds
  PumpBlocked = 24,       // a = PumpBlock
  ValveOpenFailed = 25,
  ValveCloseFailed = 26,
  ValveRecovered = 27,
  LineVoltage = 28,       // a = mV at the module with the valve open
  ValveTestDone = 29,

  SleepEnter = 30,  // a = seconds
  Wake = 31,        // a = esp_sleep_wakeup_cause_t (4 timer, 7 GPIO button)
  OtaStarted = 32,
  OtaFinished = 33,
  OtaFailed = 34,
  Calibrated = 35,  // a = 0 dry / 1 wet, b = raw
  WifiReset = 36,
  AlarmRaised = 37,    // a = kAlarm* bit(s) newly set, b = battery mV
  AlarmCleared = 38,   // a = kAlarm* bit(s) cleared, b = battery mV
  WifiSession = 39,    // a = 1 started by button, 0 ended; b = 0 press, 1 timeout
};

constexpr uint8_t kAlarmNoWater = 1u << 0;
constexpr uint8_t kAlarmLowBattery = 1u << 1;

enum class AbortReason : uint8_t {
  UserStop = 1,
  NoWater = 2,
  BatteryLow = 3,
  Shutdown = 4,  // OTA, sleep or reboot requested mid-run
};

enum class JobKind : uint8_t {
  Scheduled = 0,
  Manual = 1,
  ValveTest = 2,  // valve only, pump stays off
};

}  // namespace wat
