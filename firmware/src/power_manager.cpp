#include "power_manager.h"

#include <Update.h>
#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_sleep.h>

#include "app.h"
#include "board_config.h"
#include "core/power_policy.h"

namespace wat {

namespace {
constexpr uint32_t kBatteryPeriodMs = 5000;
constexpr uint32_t kSilentNtpTimeoutMs = 20000;
constexpr uint32_t kReadingShowMs = 3000;
constexpr uint32_t kSleepAfterWebRequestMs = 1000;  // let the HTTP reply leave
constexpr uint32_t kSplashMs = 600;
constexpr uint32_t kSleepRetryMs = 5000;
// A scheduled wake normally lasts seconds (4 pots x up to maxPumpSec). Past
// this, something is stuck: sleep anyway rather than drain the cell.
constexpr uint32_t kScheduledMaxAwakeMs = 45UL * 60 * 1000;

// Survive deep sleep; zeroed on power-on.
RTC_DATA_ATTR uint8_t g_alarms = 0;
RTC_DATA_ATTR uint32_t g_lastNtpEpoch = 0;
}  // namespace

const char *PowerManager::modeKey() const {
  switch (mode_) {
    case Mode::Scheduled:
      return "scheduled";
    case Mode::Session:
      return "session";
  }
  return "unknown";
}

uint8_t PowerManager::alarms() const { return g_alarms; }
bool PowerManager::noWaterAlarm() const { return g_alarms & kAlarmNoWater; }
bool PowerManager::lowBatteryAlarm() const { return g_alarms & kAlarmLowBattery; }

void PowerManager::setAlarms(uint8_t bits) {
  const uint8_t raised = static_cast<uint8_t>(bits & ~g_alarms);
  const uint8_t cleared = static_cast<uint8_t>(g_alarms & ~bits);
  if (raised) {
    app_->log.add(Event::AlarmRaised, LogLevel::Warn, kNoModule, raised, batteryMv_);
  }
  if (cleared) {
    app_->log.add(Event::AlarmCleared, LogLevel::Info, kNoModule, cleared, batteryMv_);
  }
  g_alarms = bits;
  app_->alarmLed.set(g_alarms != 0);
}

void PowerManager::raiseAlarm(uint8_t bits) { setAlarms(static_cast<uint8_t>(g_alarms | bits)); }

void PowerManager::sampleBattery() {
  // Under pump load the cell sags; the idle value is what the icon means.
  if (app_->pump.isOn()) {
    return;
  }
  batteryMv_ = app_->battery.readMillivolts(app_->config.batteryCalibration);
  lastBatteryAt_ = millis();
}

void PowerManager::evaluateAlarms() {
  const AppConfig &c = app_->config;
  const AlarmInputs in{c.batteryCheck, batteryMv_, c.batteryMinMv, c.waterCheck,
                       app_->water.waterPresent(c.waterPresentHigh)};
  setAlarms(alarmBits(wat::evaluateAlarms(in)));
}

void PowerManager::begin(App &app) {
  app_ = &app;
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (cause != ESP_SLEEP_WAKEUP_UNDEFINED) {
    app_->log.add(Event::Wake, LogLevel::Info, kNoModule, static_cast<int32_t>(cause));
  }
  sampleBattery();
  evaluateAlarms();

  // Timer = the daily check. Button, power-on and any reset = someone is
  // standing next to the board and wants the UI.
  if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    mode_ = Mode::Scheduled;
    const bool valid = app_->time.valid();
    if (!app_->config.wifiSsid.isEmpty() &&
        ntpSyncDue(valid, static_cast<uint32_t>(app_->time.now()), g_lastNtpEpoch, app_->config.ntpEveryDays)) {
      startSilentNtp();
    }
  } else {
    startSession();
  }
}

void PowerManager::startSession() {
  mode_ = Mode::Session;
  ntpActive_ = false;
  sleepRequested_ = false;
  timeoutLogged_ = false;
  app_->net.begin(*app_);
  if (!webStarted_) {
    app_->web.begin(*app_);
    webStarted_ = true;
  }
  awakeUntil_ = millis() + app_->config.wifiSessionMinutes * 60000UL;
  app_->log.add(Event::WifiSession, LogLevel::Info, kNoModule, 1, 0);
}

void PowerManager::startSilentNtp() {
  app_->net.begin(*app_, /*stationOnly=*/true);
  ntpActive_ = true;
  ntpStartedAt_ = millis();
}

void PowerManager::finishSilentNtp() {
  ntpActive_ = false;
  if (mode_ == Mode::Scheduled) {
    app_->net.shutdown();
  }
}

void PowerManager::onShortPress() {
  if (mode_ == Mode::Scheduled) {
    startSession();
    return;
  }
  sleepRequested_ = true;
  sleepRequestedAt_ = millis() - kSleepAfterWebRequestMs;
}

void PowerManager::requestSleep() {
  sleepRequested_ = true;
  sleepRequestedAt_ = millis();
}

void PowerManager::keepAwake() {
  if (mode_ != Mode::Session) {
    return;
  }
  const uint32_t until = millis() + app_->config.wifiSessionMinutes * 60000UL;
  if (static_cast<int32_t>(until - awakeUntil_) > 0) {
    awakeUntil_ = until;
  }
}

int32_t PowerManager::secondsUntilSleep() const {
  if (mode_ != Mode::Session) {
    return -1;
  }
  const int32_t left = static_cast<int32_t>(awakeUntil_ - millis());
  return left > 0 ? left / 1000 : 0;
}

void PowerManager::loop() {
  const uint32_t now = millis();
  if (now - lastBatteryAt_ >= kBatteryPeriodMs) {
    sampleBattery();
  }
  if (!ntpNoted_ && app_->time.syncedSinceBoot()) {
    ntpNoted_ = true;
    g_lastNtpEpoch = static_cast<uint32_t>(app_->time.now());
  }
  if (ntpActive_ && (ntpNoted_ || now - ntpStartedAt_ >= kSilentNtpTimeoutMs)) {
    finishSilentNtp();
  }
  if (now - lastSleepAttemptAt_ < kSleepRetryMs) {
    return;  // a refused sleep (valve fault) is retried, not spun on
  }
  switch (mode_) {
    case Mode::Scheduled:
      loopScheduled();
      return;
    case Mode::Session:
      loopSession();
      return;
  }
}

void PowerManager::loopScheduled() {
  if (ntpActive_) {
    return;  // the clock may be about to change; decide afterwards
  }
  const bool valid = app_->time.valid();
  if (!valid && !noClockChecksQueued_) {
    noClockChecksQueued_ = true;
    app_->garden.checkAllNow();
  }
  const bool overdue = millis() > kScheduledMaxAwakeMs;
  const Garden &g = app_->garden;
  const bool readingOnScreen = g.lastMeasuredModule() >= 0 && millis() - g.lastMeasuredAtMs() < kReadingShowMs;
  if (!overdue && (g.checksPending() || !app_->controller.idle() || readingOnScreen || app_->button.isDown())) {
    return;
  }
  const uint32_t seconds = plannedSleepSeconds(valid, g.secondsUntilNextCheck(), overdue);
  if (seconds == 0) {
    return;  // next check within two minutes: cheaper to wait than to reboot
  }
  sleepNow(seconds);
}

void PowerManager::loopSession() {
  const uint32_t now = millis();
  const bool timedOut = static_cast<int32_t>(awakeUntil_ - now) <= 0;
  if (sleepRequested_) {
    if (now - sleepRequestedAt_ < kSleepAfterWebRequestMs) {
      return;
    }
  } else if (!timedOut) {
    return;
  } else if (!app_->controller.idle() || Update.isRunning() || app_->button.isDown()) {
    return;  // a timeout never cuts a watering run or an OTA short
  }
  if (Update.isRunning()) {
    return;
  }
  if (!timeoutLogged_) {
    timeoutLogged_ = true;
    app_->log.add(Event::WifiSession, LogLevel::Info, kNoModule, 0, sleepRequested_ ? 0 : 1);
  }
  const uint32_t seconds =
      plannedSleepSeconds(app_->time.valid(), app_->garden.secondsUntilNextCheck(), /*forced=*/true);
  sleepNow(seconds);
}

bool PowerManager::sleepNow(uint32_t seconds) {
  lastSleepAttemptAt_ = millis();
  if (!app_->controller.emergencyStop()) {
    // A valve that cannot be confirmed closed is worth more awake time than
    // the battery: the Fault state keeps retrying it.
    return false;
  }
  app_->log.add(Event::SleepEnter, LogLevel::Info, kNoModule, static_cast<int32_t>(seconds));
  app_->display.showSleepSplash();
  delay(kSplashMs);
  app_->display.powerOff();
  app_->net.shutdown();
  // Line pull-ups must hang off the switched rail too (docs/hardware/pins.md),
  // otherwise they back-power the unpowered ADS1115s through SDA/SCL.
  app_->bus.setPowered(false);

  // Held levels survive deep sleep: pump OFF, blue LED off, red LED as is.
  app_->pump.holdOffForSleep();
  app_->led.offForSleep();
  app_->alarmLed.holdForSleep();
  gpio_deep_sleep_hold_en();

  Serial.flush();
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(seconds) * 1000000ULL);
  esp_deep_sleep_enable_gpio_wakeup(1ULL << board::kPinWakeButton, ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_deep_sleep_start();
  return true;
}

}  // namespace wat
