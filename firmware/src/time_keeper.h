#pragma once

#include <Arduino.h>
#include <time.h>

namespace wat {

// There is no RTC battery on the board: after a power loss the clock starts
// at 1970 until NTP or the browser sets it. Deep sleep keeps the clock, but
// on the C3's internal RC oscillator (~1 %, i.e. ~15 min/day), so
// PowerManager re-syncs NTP on button sessions and every N days silently.
class TimeKeeper {
 public:
  struct Stamp {
    uint32_t value;  // epoch seconds, or uptime seconds if !valid
    bool valid;
  };

  struct LocalTime {
    int32_t day;  // days since 1970-01-01 in local time
    uint32_t secondOfDay;
  };

  void begin(const String &timezone, const String &ntpServer);

  bool valid() const;
  time_t now() const { return time(nullptr); }
  Stamp stamp() const;
  bool local(time_t t, LocalTime &out) const;
  // Epoch of `day` (local) at `secondOfDay` (local).
  time_t epochOf(int32_t day, uint32_t secondOfDay) const;
  String format(time_t t) const;

  void setFromBrowser(time_t epoch);
  void applyTimezone(const String &timezone);

  // True once after each NTP sync; the caller logs it.
  bool takeSyncedEvent();
  bool syncedSinceBoot() const;

 private:
  String ntpServer_;
};

}  // namespace wat
