#include "core/watering_policy.h"

#include <string.h>

#include "core/battery.h"

namespace wat {

PumpBlock pumpBlockReason(const PumpConditions &c) {
  if (c.valveFault) {
    return PumpBlock::ValveFault;
  }
  if (c.waterCheck && !c.waterPresent) {
    return PumpBlock::NoWater;
  }
  if (c.batteryCheck) {
    if (c.batteryMv < kBatteryAbsentMv) {
      return PumpBlock::BatteryUnknown;
    }
    if (c.batteryMv < c.thresholdMv) {
      return PumpBlock::BatteryLow;
    }
  }
  return PumpBlock::None;
}

WaterDecision decideWatering(ModuleMode mode, bool readingOk, bool calibrated, uint8_t moisturePct,
                             uint8_t thresholdPct) {
  switch (mode) {
    case ModuleMode::Off:
      return WaterDecision::SkipOff;
    case ModuleMode::Schedule:
      return WaterDecision::Water;
    case ModuleMode::Moisture:
      if (!readingOk) {
        return WaterDecision::SkipSensorError;
      }
      if (!calibrated) {
        return WaterDecision::SkipUncalibrated;
      }
      return moisturePct < thresholdPct ? WaterDecision::Water : WaterDecision::SkipWet;
  }
  return WaterDecision::SkipOff;
}

const char *pumpBlockKey(PumpBlock block) {
  switch (block) {
    case PumpBlock::None:
      return "none";
    case PumpBlock::BatteryLow:
      return "battery_low";
    case PumpBlock::BatteryUnknown:
      return "battery_unknown";
    case PumpBlock::NoWater:
      return "no_water";
    case PumpBlock::ValveFault:
      return "valve_fault";
  }
  return "unknown";
}

const char *waterDecisionKey(WaterDecision decision) {
  switch (decision) {
    case WaterDecision::Water:
      return "water";
    case WaterDecision::SkipOff:
      return "off";
    case WaterDecision::SkipWet:
      return "wet";
    case WaterDecision::SkipSensorError:
      return "sensor_error";
    case WaterDecision::SkipUncalibrated:
      return "uncalibrated";
  }
  return "unknown";
}

const char *moduleModeKey(ModuleMode mode) {
  switch (mode) {
    case ModuleMode::Off:
      return "off";
    case ModuleMode::Moisture:
      return "moisture";
    case ModuleMode::Schedule:
      return "schedule";
  }
  return "off";
}

bool moduleModeFromKey(const char *key, ModuleMode &out) {
  if (key == nullptr) {
    return false;
  }
  if (strcmp(key, "off") == 0) {
    out = ModuleMode::Off;
  } else if (strcmp(key, "moisture") == 0) {
    out = ModuleMode::Moisture;
  } else if (strcmp(key, "schedule") == 0) {
    out = ModuleMode::Schedule;
  } else {
    return false;
  }
  return true;
}

}  // namespace wat
