#include "garden.h"

#include <Preferences.h>

#include "app.h"
#include "core/schedule.h"

namespace wat {

namespace {
constexpr uint32_t kProbePeriodMs = 30000;
constexpr uint32_t kSchedulePeriodMs = 1000;
constexpr const char *kNvsNamespace = "garden";
// Each pot's reading stays on the OLED this long before the next check.
constexpr uint32_t kCheckSpacingMs = 2500;

String lastRunKey(uint8_t module) { return String("lr") + module; }

DailySlot slotOf(const ModuleConfig &m) { return {m.minuteOfDay, m.everyNDays}; }
}  // namespace

void Garden::begin(App &app) {
  app_ = &app;
  Preferences prefs;
  const bool open = prefs.begin(kNvsNamespace, /*readOnly=*/true);
  for (uint8_t i = 0; i < kMaxModules; ++i) {
    lastRunDay_[i] = open ? prefs.getInt(lastRunKey(i).c_str(), kNeverRan) : kNeverRan;
  }
  if (open) {
    prefs.end();
  }
  probeAll();
  lastProbeAt_ = millis();
}

void Garden::saveLastRun(uint8_t module) {
  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, /*readOnly=*/false)) {
    Serial.println("[garden] NVS open failed - a reboot may repeat today's check");
    return;
  }
  prefs.putInt(lastRunKey(module).c_str(), lastRunDay_[module]);
  prefs.end();
}

bool Garden::scheduled(uint8_t module) const {
  const ModuleConfig &m = app_->config.modules[module];
  return m.enabled && m.mode != ModuleMode::Off;
}

void Garden::probeAll() {
  for (uint8_t i = 0; i < kMaxModules; ++i) {
    ModuleRuntime &r = runtime_[i];
    const bool present = app_->bus.probe(i);
    // First probe after boot: only a missing *enabled* module is news.
    const bool changed = probed_ ? present != r.present : !present && app_->config.modules[i].enabled;
    if (changed && (r.everSeen || app_->config.modules[i].enabled)) {
      app_->log.add(present ? Event::ModuleOnline : Event::ModuleOffline,
                    present ? LogLevel::Info : LogLevel::Warn, i);
    }
    r.present = present;
    r.everSeen = r.everSeen || present;
  }
  probed_ = true;
}

bool Garden::measure(uint8_t module, bool record, uint8_t extraFlags) {
  if (module >= kMaxModules || app_->bus.openValveIndex() >= 0) {
    return false;
  }
  app_->bus.setPowered(true);
  ModuleRuntime &r = runtime_[module];
  const ModuleConfig &m = app_->config.modules[module];
  int16_t raw = 0;
  r.readingOk = app_->bus.readMoisture(module, raw);
  r.present = r.readingOk || app_->bus.probe(module);
  r.everSeen = r.everSeen || r.present;
  const TimeKeeper::Stamp s = app_->time.stamp();
  r.lastMeasureTs = s.valid ? s.value : 0;
  if (r.readingOk) {
    r.raw = raw;
    r.pct = moisturePercent(raw, m.calibration());
  }
  lastMeasured_ = static_cast<int8_t>(module);
  lastMeasuredAt_ = millis();
  if (record) {
    const uint16_t batteryMv = app_->battery.readMillivolts(app_->config.batteryCalibration);
    if (r.readingOk) {
      app_->log.add(Event::Measured, LogLevel::Info, module, r.raw, r.pct);
      app_->history.add(module, r.pct, r.raw, batteryMv, 0, extraFlags);
    } else {
      app_->log.add(Event::SensorError, LogLevel::Warn, module);
      app_->history.add(module, 0, 0, batteryMv, 0, static_cast<uint8_t>(extraFlags | kHistSensorError));
    }
  }
  return r.readingOk;
}

void Garden::measureAll(bool record) {
  for (uint8_t i = 0; i < kMaxModules; ++i) {
    if (app_->config.modules[i].enabled || runtime_[i].present) {
      measure(i, record, kHistManual);
    }
  }
}

bool Garden::calibrate(uint8_t module, bool wet, String &error) {
  if (module >= kMaxModules) {
    error = "Нет такого модуля";
    return false;
  }
  if (!measure(module, false)) {
    error = "Не удалось прочитать датчик (модуль не отвечает или идёт полив)";
    return false;
  }
  ModuleConfig &m = app_->config.modules[module];
  (wet ? m.wetRaw : m.dryRaw) = runtime_[module].raw;
  runtime_[module].pct = moisturePercent(runtime_[module].raw, m.calibration());
  app_->log.add(Event::Calibrated, LogLevel::Info, module, wet ? 1 : 0, runtime_[module].raw);
  if (!saveConfig(app_->config)) {
    error = "Калибровка применена, но не сохранилась во flash";
    return false;
  }
  return true;
}

void Garden::noteWatered(uint8_t module, uint16_t seconds) {
  const TimeKeeper::Stamp s = app_->time.stamp();
  runtime_[module].lastWaterTs = s.valid ? s.value : 0;
  runtime_[module].lastWaterSec = seconds;
}

void Garden::runCheck(uint8_t module, int32_t today, bool persist) {
  // Marked before the pump can run: if watering browns the board out, the
  // reboot must not water the same plant again in a loop.
  if (persist) {
    lastRunDay_[module] = today;
    saveLastRun(module);
  }

  const ModuleConfig &m = app_->config.modules[module];
  const bool ok = measure(module, false);
  const ModuleRuntime &r = runtime_[module];
  const WaterDecision decision =
      decideWatering(m.mode, ok, calibrationValid(m.calibration()), r.pct, m.thresholdPct);
  const uint16_t batteryMv = app_->battery.readMillivolts(app_->config.batteryCalibration);

  uint8_t flags = ok ? 0 : kHistSensorError;
  if (decision == WaterDecision::SkipWet) {
    flags |= kHistSkippedWet;
  }
  app_->history.add(module, ok ? r.pct : 0, ok ? r.raw : 0, batteryMv, 0, flags);
  if (ok) {
    app_->log.add(Event::Measured, LogLevel::Info, module, r.raw, r.pct);
  } else {
    app_->log.add(Event::SensorError, LogLevel::Warn, module);
  }

  if (decision != WaterDecision::Water) {
    app_->log.add(Event::WateringSkipped, LogLevel::Info, module, static_cast<int32_t>(decision), r.pct);
    return;
  }
  app_->controller.enqueue({module, m.durationSec, JobKind::Scheduled, ok ? r.pct : uint8_t{0}});
}

void Garden::runScheduledChecks() {
  TimeKeeper::LocalTime lt{};
  if (!app_->time.local(app_->time.now(), lt)) {
    bool anyScheduled = false;
    for (uint8_t i = 0; i < kMaxModules; ++i) {
      anyScheduled = anyScheduled || scheduled(i);
    }
    if (anyScheduled && !timeInvalidLogged_) {
      app_->log.add(Event::TimeInvalid, LogLevel::Warn);
      timeInvalidLogged_ = true;
    }
    return;
  }
  timeInvalidLogged_ = false;

  for (uint8_t i = 0; i < kMaxModules; ++i) {
    if (!scheduled(i)) {
      continue;
    }
    const ModuleConfig &m = app_->config.modules[i];
    if (!isDue(slotOf(m), lt.day, lt.secondOfDay, lastRunDay_[i], app_->config.catchUpMinutes)) {
      continue;
    }
    // Measuring next to an open valve reads the ground shift, not the soil;
    // the catch-up window gives room to wait for the current run to end.
    if (!app_->controller.idle() || !checkSpacingElapsed()) {
      return;
    }
    runCheck(i, lt.day, true);
  }
}

bool Garden::checkSpacingElapsed() const {
  return lastMeasured_ < 0 || millis() - lastMeasuredAt_ >= kCheckSpacingMs;
}

void Garden::checkAllNow() {
  pendingMask_ = 0;
  for (uint8_t i = 0; i < kMaxModules; ++i) {
    if (scheduled(i)) {
      pendingMask_ |= static_cast<uint8_t>(1u << i);
    }
  }
}

void Garden::runPendingChecks() {
  // Same rule as the scheduled path: never measure next to an open valve.
  if (pendingMask_ == 0 || !app_->controller.idle() || !checkSpacingElapsed()) {
    return;
  }
  for (uint8_t i = 0; i < kMaxModules; ++i) {
    if (pendingMask_ & (1u << i)) {
      pendingMask_ &= static_cast<uint8_t>(~(1u << i));
      runCheck(i, kNeverRan, false);
      return;
    }
  }
}

uint32_t Garden::nextCheckEpoch(uint8_t module) const {
  if (module >= kMaxModules || !scheduled(module)) {
    return 0;
  }
  const time_t now = app_->time.now();
  TimeKeeper::LocalTime lt{};
  if (!app_->time.local(now, lt)) {
    return 0;
  }
  const ModuleConfig &m = app_->config.modules[module];
  return static_cast<uint32_t>(now) +
         secondsUntilDue(slotOf(m), lt.day, lt.secondOfDay, lastRunDay_[module], app_->config.catchUpMinutes);
}

int32_t Garden::secondsUntilNextCheck() const {
  TimeKeeper::LocalTime lt{};
  if (!app_->time.local(app_->time.now(), lt)) {
    return -1;
  }
  int64_t best = -1;
  for (uint8_t i = 0; i < kMaxModules; ++i) {
    if (!scheduled(i)) {
      continue;
    }
    const ModuleConfig &m = app_->config.modules[i];
    const uint32_t s = secondsUntilDue(slotOf(m), lt.day, lt.secondOfDay, lastRunDay_[i], app_->config.catchUpMinutes);
    if (best < 0 || s < best) {
      best = s;
    }
  }
  return static_cast<int32_t>(best);
}

bool Garden::anyCheckDue() const { return secondsUntilNextCheck() == 0; }

void Garden::loop() {
  const uint32_t now = millis();
  if (now - lastScheduleAt_ >= kSchedulePeriodMs) {
    lastScheduleAt_ = now;
    runScheduledChecks();
    runPendingChecks();
  }
  if (now - lastProbeAt_ >= kProbePeriodMs && app_->controller.idle()) {
    lastProbeAt_ = now;
    probeAll();
  }
}

}  // namespace wat
