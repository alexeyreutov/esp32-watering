#pragma once

#include <stdint.h>

namespace wat {

enum class ModuleMode : uint8_t {
  Off = 0,
  Moisture = 1,  // water only if moisture < threshold
  Schedule = 2,  // water at the slot regardless of the reading
};

enum class PumpBlock : uint8_t {
  None = 0,
  BatteryLow = 1,
  BatteryUnknown = 2,  // divider reads ~0: no cell or broken wire
  NoWater = 3,
  ValveFault = 4,  // a valve could not be confirmed closed
};

struct PumpConditions {
  bool batteryCheck;
  uint16_t batteryMv;
  uint16_t thresholdMv;
  bool waterCheck;
  bool waterPresent;
  bool valveFault;
};

PumpBlock pumpBlockReason(const PumpConditions &c);

enum class WaterDecision : uint8_t {
  Water = 0,
  SkipOff = 1,
  SkipWet = 2,
  SkipSensorError = 3,
  SkipUncalibrated = 4,
};

WaterDecision decideWatering(ModuleMode mode, bool readingOk, bool calibrated, uint8_t moisturePct,
                             uint8_t thresholdPct);

// Stable machine keys for the web API; the UI owns the human text.
const char *pumpBlockKey(PumpBlock block);
const char *waterDecisionKey(WaterDecision decision);
const char *moduleModeKey(ModuleMode mode);
bool moduleModeFromKey(const char *key, ModuleMode &out);

}  // namespace wat
