#pragma once

#include <Arduino.h>

#include "config.h"

namespace wat {

struct App;

struct ModuleRuntime {
  bool present = false;
  bool everSeen = false;
  bool readingOk = false;
  int16_t raw = 0;
  uint8_t pct = 0;
  uint32_t lastMeasureTs = 0;  // epoch, 0 = unknown
  uint32_t lastWaterTs = 0;
  uint16_t lastWaterSec = 0;
};

// Module presence, moisture readings and the daily schedule.
// The schedule decides; WateringController executes.
class Garden {
 public:
  void begin(App &app);
  void loop();

  // Synchronous (~0.1 s per module). Refuses while any valve is open.
  bool measure(uint8_t module, bool record, uint8_t extraFlags = 0);
  void measureAll(bool record);
  bool calibrate(uint8_t module, bool wet, String &error);
  void noteWatered(uint8_t module, uint16_t seconds);

  const ModuleRuntime &runtime(uint8_t module) const { return runtime_[module]; }
  // Epoch of the next scheduled check, 0 if unknown (clock unset / off).
  uint32_t nextCheckEpoch(uint8_t module) const;
  // Seconds until the earliest scheduled check, or -1 if nothing scheduled.
  int32_t secondsUntilNextCheck() const;
  bool anyCheckDue() const;

  // Clock unknown (no NTP since power loss): check every scheduled module
  // once, one after another, ignoring time of day. The timer wake that calls
  // this is the only "daily" signal left.
  void checkAllNow();
  bool checksPending() const { return pendingMask_ != 0; }

  // For the display: which module was measured last and when (millis).
  int8_t lastMeasuredModule() const { return lastMeasured_; }
  uint32_t lastMeasuredAtMs() const { return lastMeasuredAt_; }

 private:
  void probeAll();
  void runScheduledChecks();
  void runPendingChecks();
  bool checkSpacingElapsed() const;
  void runCheck(uint8_t module, int32_t today, bool persist);
  bool scheduled(uint8_t module) const;
  void saveLastRun(uint8_t module);

  App *app_ = nullptr;
  ModuleRuntime runtime_[kMaxModules];
  int32_t lastRunDay_[kMaxModules];
  uint32_t lastProbeAt_ = 0;
  uint32_t lastScheduleAt_ = 0;
  bool timeInvalidLogged_ = false;
  bool probed_ = false;
  uint8_t pendingMask_ = 0;
  int8_t lastMeasured_ = -1;
  uint32_t lastMeasuredAt_ = 0;
};

}  // namespace wat
