#pragma once

#include <stdint.h>

namespace wat {

// Anything below this on the B+ divider means "no cell / broken divider",
// not "empty cell": a real Li-ion is long dead before 2.5 V.
constexpr uint16_t kBatteryAbsentMv = 2500;

// Resting-voltage curve for one Li-ion cell. Under pump load the voltage sags,
// so the percentage is only meaningful when the pump is off.
uint8_t liIonPercent(uint16_t millivolts);

}  // namespace wat
