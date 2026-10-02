#include "base_io.h"

#include <driver/gpio.h>

#include "board_config.h"

namespace wat {

namespace {
constexpr uint8_t kBatterySamples = 16;
constexpr uint8_t kWaterSamples = 5;
constexpr uint32_t kDebounceMs = 40;
constexpr uint32_t kLongPressMs = 5000;
}  // namespace

void Pump::begin() {
  gpio_hold_dis(static_cast<gpio_num_t>(board::kPinPump));
  digitalWrite(board::kPinPump, LOW);
  pinMode(board::kPinPump, OUTPUT);
  digitalWrite(board::kPinPump, LOW);
  on_ = false;
}

void Pump::on() {
  digitalWrite(board::kPinPump, HIGH);
  on_ = true;
}

void Pump::off() {
  digitalWrite(board::kPinPump, LOW);
  on_ = false;
}

void Pump::holdOffForSleep() {
  off();
  gpio_hold_en(static_cast<gpio_num_t>(board::kPinPump));
}

void AlarmLed::begin(bool lit) {
  gpio_hold_dis(static_cast<gpio_num_t>(board::kPinAlarmLed));
  pinMode(board::kPinAlarmLed, OUTPUT);
  set(lit);
}

void AlarmLed::set(bool lit) {
  lit_ = lit;
  digitalWrite(board::kPinAlarmLed, lit ? HIGH : LOW);
}

void AlarmLed::holdForSleep() { gpio_hold_en(static_cast<gpio_num_t>(board::kPinAlarmLed)); }

void BatteryMonitor::begin() {
  // C3 at 11 dB: usable to ~2.5 V, enough for 4.2 V / 2.
  analogSetPinAttenuation(board::kPinBatteryAdc, ADC_11db);
}

uint16_t BatteryMonitor::readMillivolts(float calibration) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < kBatterySamples; ++i) {
    sum += analogReadMilliVolts(board::kPinBatteryAdc);
  }
  const float pinMv = static_cast<float>(sum) / kBatterySamples;
  return static_cast<uint16_t>(pinMv * board::kBatteryDividerRatio * calibration);
}

void WaterLevel::begin() { pinMode(board::kPinWaterLevel, INPUT); }

bool WaterLevel::waterPresent(bool presentHigh) {
  uint8_t high = 0;
  for (uint8_t i = 0; i < kWaterSamples; ++i) {
    high += digitalRead(board::kPinWaterLevel) == HIGH ? 1 : 0;
    delayMicroseconds(200);
  }
  const bool levelHigh = high * 2 > kWaterSamples;
  return levelHigh == presentHigh;
}

void StatusLed::begin() {
  gpio_hold_dis(static_cast<gpio_num_t>(board::kPinLed));
  pinMode(board::kPinLed, OUTPUT);
  write(false);
}

void StatusLed::write(bool lit) {
  digitalWrite(board::kPinLed, lit != board::kLedActiveLow ? HIGH : LOW);
}

void StatusLed::loop(Pattern pattern) {
  const uint32_t t = millis();
  switch (pattern) {
    case Pattern::Off:
      write(t % 5000 < 30);  // heartbeat
      return;
    case Pattern::Watering:
      write(true);
      return;
    case Pattern::AccessPoint:
      write(t % 2000 < 1000);
      return;
    case Pattern::Fault:
      write(t % 300 < 150);
      return;
  }
}

void StatusLed::offForSleep() {
  write(false);
  gpio_hold_en(static_cast<gpio_num_t>(board::kPinLed));
}

void ServiceButton::begin() {
  pinMode(board::kPinWakeButton, INPUT_PULLUP);
  pinMode(board::kPinBootButton, INPUT_PULLUP);
  if (isDown()) {
    wasDown_ = true;
    longFired_ = true;  // swallow the release of the wake press
    downSince_ = millis();
  }
}

bool ServiceButton::isDown() const {
  return digitalRead(board::kPinWakeButton) == LOW || digitalRead(board::kPinBootButton) == LOW;
}

ServiceButton::Press ServiceButton::poll() {
  const bool down = isDown();
  const uint32_t now = millis();
  if (down && !wasDown_) {
    wasDown_ = true;
    longFired_ = false;
    downSince_ = now;
    return Press::None;
  }
  if (down && !longFired_ && now - downSince_ >= kLongPressMs) {
    longFired_ = true;
    return Press::Long;
  }
  if (!down && wasDown_) {
    wasDown_ = false;
    if (!longFired_ && now - downSince_ >= kDebounceMs) {
      return Press::Short;
    }
  }
  return Press::None;
}

}  // namespace wat
