#pragma once

#include <Arduino.h>

namespace wat {

struct App;

// The board lives in deep sleep. Two kinds of wake:
//
//   Scheduled (timer): no Wi-Fi, no icon. Run due checks (pot number and
//     moisture on the OLED), water, sleep until the next check. Every
//     `ntpEveryDays` a silent station-only NTP sync rides along.
//   Button session (wake button, power-on, reset): Wi-Fi + web UI. A second
//     short press, the web "sleep" action or `wifiSessionMinutes` without
//     activity ends it. Never sleeps mid-watering on a timeout.
//
// Alarms (no water, low battery) live in RTC memory: evaluated with the pump
// off at every wake, raised immediately by pump blocks/aborts, cleared only
// by a wake that finds the condition gone. The red LED shows them and is
// held lit through deep sleep.
class PowerManager {
 public:
  enum class Mode : uint8_t { Scheduled, Session };

  // After battery, water, LEDs, time, garden and controller are up.
  void begin(App &app);
  void loop();

  Mode mode() const { return mode_; }
  bool wifiSession() const { return mode_ == Mode::Session; }
  const char *modeKey() const;

  void onShortPress();  // scheduled -> start session; session -> sleep
  void keepAwake();     // any web request
  void requestSleep();  // web "sleep now"
  // Seconds until a session ends on its own; -1 outside a session.
  int32_t secondsUntilSleep() const;

  uint8_t alarms() const;
  bool noWaterAlarm() const;
  bool lowBatteryAlarm() const;
  void raiseAlarm(uint8_t bits);
  uint16_t batteryMillivolts() const { return batteryMv_; }

 private:
  void evaluateAlarms();
  void setAlarms(uint8_t bits);
  void startSession();
  void startSilentNtp();
  void finishSilentNtp();
  void loopScheduled();
  void loopSession();
  bool sleepNow(uint32_t seconds);
  void sampleBattery();

  App *app_ = nullptr;
  Mode mode_ = Mode::Scheduled;
  bool webStarted_ = false;
  bool ntpActive_ = false;
  bool ntpNoted_ = false;
  bool noClockChecksQueued_ = false;
  bool sleepRequested_ = false;
  bool timeoutLogged_ = false;
  uint32_t sleepRequestedAt_ = 0;
  uint32_t ntpStartedAt_ = 0;
  uint32_t awakeUntil_ = 0;
  uint32_t lastBatteryAt_ = 0;
  uint32_t lastSleepAttemptAt_ = 0;
  uint16_t batteryMv_ = 0;
};

}  // namespace wat
