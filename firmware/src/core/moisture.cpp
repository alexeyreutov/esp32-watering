#include "core/moisture.h"

namespace wat {

bool calibrationValid(const Calibration &c) {
  int32_t span = static_cast<int32_t>(c.dryRaw) - c.wetRaw;
  if (span < 0) {
    span = -span;
  }
  return span >= kMinCalibrationSpan;
}

uint8_t moisturePercent(int16_t raw, const Calibration &c) {
  if (!calibrationValid(c)) {
    return 0;
  }
  const int32_t num = static_cast<int32_t>(c.dryRaw) - raw;
  const int32_t den = static_cast<int32_t>(c.dryRaw) - c.wetRaw;
  int32_t pct = num * 100 / den;
  if (pct < 0) {
    pct = 0;
  }
  if (pct > 100) {
    pct = 100;
  }
  return static_cast<uint8_t>(pct);
}

int16_t medianOf(int16_t *values, uint8_t count) {
  if (count == 0) {
    return 0;
  }
  for (uint8_t i = 1; i < count; ++i) {
    const int16_t v = values[i];
    uint8_t j = i;
    while (j > 0 && values[j - 1] > v) {
      values[j] = values[j - 1];
      --j;
    }
    values[j] = v;
  }
  return values[count / 2];
}

}  // namespace wat
