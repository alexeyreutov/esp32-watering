#pragma once

#include <stdint.h>

#include "watering_policy.h"

namespace wat {

// Latched alarms: shown on the OLED, red LED held on through deep sleep.
struct AlarmInputs {
  bool batteryCheck;
  uint16_t batteryMv;  // idle (pump off) reading
  uint16_t batteryMinMv;
  bool waterCheck;
  bool waterPresent;
};

struct Alarms {
  bool noWater;
  bool lowBattery;
};

// Battery missing while the check is on counts as low: the pump is blocked
// in that case anyway, and the owner should see why.
Alarms evaluateAlarms(const AlarmInputs &in);
uint8_t alarmBits(const Alarms &a);  // kAlarm* from event_codes.h

// A pump block / abort found mid-run latches the matching alarm at once,
// without waiting for the next wake.
uint8_t alarmForBlock(PumpBlock block);

constexpr uint32_t kStayAwakeMaxSeconds = 120;  // closer than this: wait awake
constexpr uint32_t kWakeLeadSeconds = 20;       // RC drift margin
constexpr uint32_t kMinSleepSeconds = 30;
constexpr uint32_t kMaxSleepSeconds = 24UL * 3600;

// Seconds of deep sleep before the next check.
//   clockValid=false   -> one day: timer wakes are the only daily signal;
//   secondsUntilNext<0 -> nothing scheduled, still wake daily for alarms;
//   check within kStayAwakeMaxSeconds -> 0 (stay awake), unless `forced`
//   (the user asked to sleep), then at least kMinSleepSeconds.
uint32_t plannedSleepSeconds(bool clockValid, int32_t secondsUntilNext, bool forced);

// Silent NTP on a scheduled wake: clock unknown, or `everyDays` passed
// since the last sync. everyDays=0 disables (clock unknown still syncs).
bool ntpSyncDue(bool clockValid, uint32_t nowEpoch, uint32_t lastSyncEpoch, uint8_t everyDays);

}  // namespace wat
