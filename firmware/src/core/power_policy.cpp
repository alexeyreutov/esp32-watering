#include "power_policy.h"

#include "battery.h"
#include "event_codes.h"

namespace wat {

Alarms evaluateAlarms(const AlarmInputs &in) {
  Alarms a{false, false};
  a.noWater = in.waterCheck && !in.waterPresent;
  a.lowBattery = in.batteryCheck && (in.batteryMv < kBatteryAbsentMv || in.batteryMv < in.batteryMinMv);
  return a;
}

uint8_t alarmBits(const Alarms &a) {
  return static_cast<uint8_t>((a.noWater ? kAlarmNoWater : 0) | (a.lowBattery ? kAlarmLowBattery : 0));
}

uint8_t alarmForBlock(PumpBlock block) {
  switch (block) {
    case PumpBlock::None:
    case PumpBlock::ValveFault:
      return 0;
    case PumpBlock::NoWater:
      return kAlarmNoWater;
    case PumpBlock::BatteryLow:
    case PumpBlock::BatteryUnknown:
      return kAlarmLowBattery;
  }
  return 0;
}

uint32_t plannedSleepSeconds(bool clockValid, int32_t secondsUntilNext, bool forced) {
  if (!clockValid || secondsUntilNext < 0) {
    return kMaxSleepSeconds;
  }
  const uint32_t next = static_cast<uint32_t>(secondsUntilNext);
  if (next <= kStayAwakeMaxSeconds) {
    if (!forced) {
      return 0;
    }
    return next > kMinSleepSeconds ? next : kMinSleepSeconds;
  }
  const uint32_t s = next - kWakeLeadSeconds;
  return s > kMaxSleepSeconds ? kMaxSleepSeconds : s;
}

bool ntpSyncDue(bool clockValid, uint32_t nowEpoch, uint32_t lastSyncEpoch, uint8_t everyDays) {
  if (!clockValid) {
    return true;
  }
  if (everyDays == 0) {
    return false;
  }
  if (lastSyncEpoch == 0 || nowEpoch < lastSyncEpoch) {
    return true;
  }
  return nowEpoch - lastSyncEpoch >= static_cast<uint32_t>(everyDays) * 86400UL;
}

}  // namespace wat
