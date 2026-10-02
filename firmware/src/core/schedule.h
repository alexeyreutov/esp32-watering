#pragma once

#include <stdint.h>

namespace wat {

constexpr int32_t kNeverRan = INT32_MIN;
constexpr uint32_t kSecondsPerDay = 86400;

struct DailySlot {
  uint16_t minuteOfDay;  // local time, 0..1439
  uint8_t everyNDays;    // 1 = daily; 0 is treated as 1
};

// Days since 1970-01-01 for a proleptic Gregorian date (H. Hinnant).
int32_t daysFromCivil(int32_t year, uint32_t month, uint32_t day);

// A slot is due once per period, starting at its time and for `catchUpMinutes`
// after it. The window lets a check that was missed by a reboot or a busy pump
// still run the same day, but not hours later in the middle of the night.
bool isDue(const DailySlot &slot, int32_t today, uint32_t secondOfDay, int32_t lastRunDay,
           uint16_t catchUpMinutes);

// Seconds until the slot becomes due (0 = due now).
uint32_t secondsUntilDue(const DailySlot &slot, int32_t today, uint32_t secondOfDay,
                         int32_t lastRunDay, uint16_t catchUpMinutes);

}  // namespace wat
