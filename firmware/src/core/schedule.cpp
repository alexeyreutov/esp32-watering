#include "core/schedule.h"

namespace wat {

namespace {
uint8_t period(const DailySlot &slot) { return slot.everyNDays == 0 ? 1 : slot.everyNDays; }
}  // namespace

int32_t daysFromCivil(int32_t year, uint32_t month, uint32_t day) {
  year -= month <= 2 ? 1 : 0;
  const int32_t era = (year >= 0 ? year : year - 399) / 400;
  const uint32_t yoe = static_cast<uint32_t>(year - era * 400);
  const uint32_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

bool isDue(const DailySlot &slot, int32_t today, uint32_t secondOfDay, int32_t lastRunDay,
           uint16_t catchUpMinutes) {
  if (slot.minuteOfDay >= 24 * 60) {
    return false;
  }
  if (lastRunDay != kNeverRan && today - lastRunDay < period(slot)) {
    return false;
  }
  const uint32_t slotSecond = static_cast<uint32_t>(slot.minuteOfDay) * 60;
  if (secondOfDay < slotSecond) {
    return false;
  }
  return secondOfDay - slotSecond <= static_cast<uint32_t>(catchUpMinutes) * 60;
}

uint32_t secondsUntilDue(const DailySlot &slot, int32_t today, uint32_t secondOfDay,
                         int32_t lastRunDay, uint16_t catchUpMinutes) {
  if (isDue(slot, today, secondOfDay, lastRunDay, catchUpMinutes)) {
    return 0;
  }
  const uint32_t slotSecond = static_cast<uint32_t>(slot.minuteOfDay % (24 * 60)) * 60;
  int32_t day = today;
  if (lastRunDay != kNeverRan && lastRunDay + period(slot) > day) {
    day = lastRunDay + period(slot);
  }
  if (day == today && secondOfDay >= slotSecond) {
    day = today + 1;
  }
  const int64_t delta = static_cast<int64_t>(day - today) * kSecondsPerDay +
                        static_cast<int64_t>(slotSecond) - static_cast<int64_t>(secondOfDay);
  return delta > 0 ? static_cast<uint32_t>(delta) : 0;
}

}  // namespace wat
