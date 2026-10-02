#include "time_keeper.h"

#include <esp_sntp.h>
#include <sys/time.h>

#include "core/schedule.h"

namespace wat {

namespace {
// 2024-01-01: any clock before that has never been set.
constexpr time_t kValidAfter = 1704067200;
volatile bool g_ntpSynced = false;
volatile bool g_ntpSyncedSinceBoot = false;

void onNtpSync(struct timeval *) {
  g_ntpSynced = true;
  g_ntpSyncedSinceBoot = true;
}
}  // namespace

void TimeKeeper::begin(const String &timezone, const String &ntpServer) {
  ntpServer_ = ntpServer;
  sntp_set_time_sync_notification_cb(onNtpSync);
  configTzTime(timezone.c_str(), ntpServer_.c_str());
}

void TimeKeeper::applyTimezone(const String &timezone) {
  setenv("TZ", timezone.c_str(), 1);
  tzset();
}

bool TimeKeeper::valid() const { return time(nullptr) > kValidAfter; }

TimeKeeper::Stamp TimeKeeper::stamp() const {
  if (valid()) {
    return {static_cast<uint32_t>(time(nullptr)), true};
  }
  return {static_cast<uint32_t>(millis() / 1000), false};
}

bool TimeKeeper::local(time_t t, LocalTime &out) const {
  if (t <= kValidAfter) {
    return false;
  }
  struct tm tm {};
  localtime_r(&t, &tm);
  out.day = daysFromCivil(tm.tm_year + 1900, static_cast<uint32_t>(tm.tm_mon + 1),
                          static_cast<uint32_t>(tm.tm_mday));
  out.secondOfDay = static_cast<uint32_t>(tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec);
  return true;
}

time_t TimeKeeper::epochOf(int32_t day, uint32_t secondOfDay) const {
  // Local midnight = UTC midnight minus the zone offset at that moment.
  const time_t utcMidnight = static_cast<time_t>(day) * 86400;
  struct tm tm {};
  localtime_r(&utcMidnight, &tm);
  const time_t asLocal = static_cast<time_t>(daysFromCivil(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday)) * 86400 +
                         tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
  const time_t offset = asLocal - utcMidnight;
  return utcMidnight - offset + secondOfDay;
}

String TimeKeeper::format(time_t t) const {
  struct tm tm {};
  localtime_r(&t, &tm);
  char buf[24];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
  return String(buf);
}

void TimeKeeper::setFromBrowser(time_t epoch) {
  struct timeval tv {};
  tv.tv_sec = epoch;
  settimeofday(&tv, nullptr);
}

bool TimeKeeper::syncedSinceBoot() const { return g_ntpSyncedSinceBoot; }

bool TimeKeeper::takeSyncedEvent() {
  if (!g_ntpSynced) {
    return false;
  }
  g_ntpSynced = false;
  return true;
}

}  // namespace wat
