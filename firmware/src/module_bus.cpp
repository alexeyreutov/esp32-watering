#include "module_bus.h"

#include <Wire.h>

#include "board_config.h"

namespace wat {

namespace {
constexpr uint8_t kMoistureSamples = 5;
constexpr uint8_t kValveRetries = 3;
}  // namespace

void ModuleBus::recoverBus() {
  // A slave reset mid-byte (brown-out on a long line) can hold SDA low
  // forever. Up to 9 clocks let it finish the byte, then a STOP frees the bus.
  pinMode(board::kPinI2cSda, INPUT_PULLUP);
  pinMode(board::kPinI2cScl, OUTPUT_OPEN_DRAIN);
  for (int i = 0; i < 9 && digitalRead(board::kPinI2cSda) == LOW; ++i) {
    digitalWrite(board::kPinI2cScl, LOW);
    delayMicroseconds(50);
    digitalWrite(board::kPinI2cScl, HIGH);
    delayMicroseconds(50);
  }
  pinMode(board::kPinI2cSda, OUTPUT_OPEN_DRAIN);
  digitalWrite(board::kPinI2cSda, LOW);
  delayMicroseconds(50);
  digitalWrite(board::kPinI2cSda, HIGH);
  delayMicroseconds(50);
}

void ModuleBus::initWire() {
  Wire.end();
  recoverBus();
  Wire.begin(board::kPinI2cSda, board::kPinI2cScl, board::kI2cHz);
  Wire.setTimeOut(board::kI2cTimeoutMs);
}

void ModuleBus::begin() {
  if (board::kPinBusPower >= 0) {
    digitalWrite(board::kPinBusPower, board::kBusPowerActiveHigh ? LOW : HIGH);
    pinMode(board::kPinBusPower, OUTPUT);
    powered_ = false;
  }
  setPowered(true);
  initWire();
  for (uint8_t i = 0; i < kMaxModules; ++i) {
    devices_[i].begin(Wire, ads::kBaseAddress + i);
    uint16_t cfg = 0;
    if (devices_[i].readConfig(cfg) && ads::isValveOnConfig(cfg)) {
      openAtBoot_ |= static_cast<uint8_t>(1u << i);
    }
  }
  closeAllValves();
}

void ModuleBus::setPowered(bool on) {
  if (board::kPinBusPower < 0) {
    powered_ = true;
    return;
  }
  if (on == powered_) {
    return;
  }
  digitalWrite(board::kPinBusPower, on == board::kBusPowerActiveHigh ? HIGH : LOW);
  powered_ = on;
  if (on) {
    delay(board::kBusSettleMs);
  } else {
    openIndex_ = -1;
  }
}

bool ModuleBus::probe(uint8_t index) {
  return powered_ && index < kMaxModules && devices_[index].probe();
}

bool ModuleBus::readMoisture(uint8_t index, int16_t &raw) {
  // Valve current on the shared ground shifts the probe reading.
  if (index >= kMaxModules || !powered_ || openIndex_ >= 0) {
    return false;
  }
  int16_t samples[kMoistureSamples];
  uint8_t ok = 0;
  for (uint8_t i = 0; i < kMoistureSamples; ++i) {
    int16_t v = 0;
    if (devices_[index].readSingleEnded(ads::Channel::A0, v)) {
      samples[ok++] = v;
    }
  }
  if (ok < (kMoistureSamples + 1) / 2) {
    return false;
  }
  raw = medianOf(samples, ok);
  return true;
}

bool ModuleBus::openValve(uint8_t index) {
  if (index >= kMaxModules || !powered_ || openIndex_ >= 0) {
    return false;
  }
  if (devices_[index].setValve(true)) {
    openIndex_ = static_cast<int8_t>(index);
    return true;
  }
  // Half-written state (thresholds set, config not) must not stay around.
  devices_[index].setValve(false);
  return false;
}

bool ModuleBus::closeValve(uint8_t index) {
  if (index >= kMaxModules) {
    return false;
  }
  if (!powered_) {
    return true;
  }
  for (uint8_t attempt = 0; attempt < kValveRetries; ++attempt) {
    if (devices_[index].setValve(false)) {
      if (openIndex_ == static_cast<int8_t>(index)) {
        openIndex_ = -1;
      }
      return true;
    }
    initWire();
    delay(20);
  }
  return false;
}

bool ModuleBus::closeAllValves() {
  if (!powered_) {
    openIndex_ = -1;
    return true;
  }
  bool ok = true;
  for (uint8_t i = 0; i < kMaxModules; ++i) {
    if (devices_[i].setValve(false)) {
      continue;
    }
    // Silence on the bus means "not fitted" for unused addresses; only a
    // device that answers but refuses the write is a real fault.
    if (devices_[i].probe() || openIndex_ == static_cast<int8_t>(i)) {
      ok = closeValve(i) && ok;
    }
  }
  if (ok) {
    openIndex_ = -1;
  }
  return ok;
}

bool ModuleBus::readLineMillivolts(uint8_t index, uint16_t &mv) {
  if (index >= kMaxModules || openIndex_ != static_cast<int8_t>(index)) {
    return false;
  }
  int16_t raw = 0;
  if (!devices_[index].readWhileValveOn(ads::Channel::A1, raw)) {
    return false;
  }
  const int32_t pinMv = adsRawToMillivolts(raw);
  mv = static_cast<uint16_t>(pinMv > 0 ? pinMv * board::kLineDividerRatio : 0);
  return true;
}

}  // namespace wat
