#include "core/battery.h"

namespace wat {

namespace {
struct CurvePoint {
  uint16_t mv;
  uint8_t pct;
};

constexpr CurvePoint kCurve[] = {
    {4200, 100}, {4100, 90}, {4000, 80}, {3900, 68}, {3800, 55}, {3750, 45},
    {3700, 35},  {3650, 25}, {3600, 15}, {3500, 7},  {3400, 3},  {3300, 0},
};
constexpr int kCurveSize = sizeof(kCurve) / sizeof(kCurve[0]);
}  // namespace

uint8_t liIonPercent(uint16_t millivolts) {
  if (millivolts >= kCurve[0].mv) {
    return 100;
  }
  if (millivolts <= kCurve[kCurveSize - 1].mv) {
    return 0;
  }
  for (int i = 1; i < kCurveSize; ++i) {
    if (millivolts >= kCurve[i].mv) {
      const CurvePoint hi = kCurve[i - 1];
      const CurvePoint lo = kCurve[i];
      const uint32_t span = hi.mv - lo.mv;
      const uint32_t offset = millivolts - lo.mv;
      return static_cast<uint8_t>(lo.pct + (hi.pct - lo.pct) * offset / span);
    }
  }
  return 0;
}

}  // namespace wat
