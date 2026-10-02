#pragma once

#include <stdint.h>

namespace wat {

struct Calibration {
  int16_t dryRaw;  // probe in air
  int16_t wetRaw;  // probe in water
};

// Below this span (~62 mV at PGA +-4.096 V) the percentage is mostly noise.
constexpr int16_t kMinCalibrationSpan = 500;

bool calibrationValid(const Calibration &c);

// Capacitive probes read lower when wetter; the math also works if a probe is
// wired the other way round, as long as dry != wet.
uint8_t moisturePercent(int16_t raw, const Calibration &c);

// ADS1115 at PGA +-4.096 V: 125 uV per count.
constexpr int32_t adsRawToMillivolts(int16_t raw) {
  return (static_cast<int32_t>(raw) * 125) / 1000;
}

// Sorts `values` in place.
int16_t medianOf(int16_t *values, uint8_t count);

}  // namespace wat
