#include "ads1115.h"

namespace wat {

namespace {
constexpr uint32_t kConversionTimeoutMs = 50;
}  // namespace

void Ads1115::begin(TwoWire &wire, uint8_t address) {
  wire_ = &wire;
  address_ = address;
}

bool Ads1115::probe() {
  wire_->beginTransmission(address_);
  return wire_->endTransmission() == 0;
}

bool Ads1115::writeRegister(uint8_t reg, uint16_t value) {
  wire_->beginTransmission(address_);
  wire_->write(reg);
  wire_->write(static_cast<uint8_t>(value >> 8));
  wire_->write(static_cast<uint8_t>(value & 0xFF));
  return wire_->endTransmission() == 0;
}

bool Ads1115::readRegister(uint8_t reg, uint16_t &value) {
  wire_->beginTransmission(address_);
  wire_->write(reg);
  // STOP instead of repeated start: the pointer register survives it, and a
  // plain STOP is friendlier to a long, slow bus.
  if (wire_->endTransmission() != 0) {
    return false;
  }
  if (wire_->requestFrom(address_, static_cast<uint8_t>(2)) != 2) {
    return false;
  }
  const uint8_t hi = static_cast<uint8_t>(wire_->read());
  const uint8_t lo = static_cast<uint8_t>(wire_->read());
  value = static_cast<uint16_t>((hi << 8) | lo);
  return true;
}

bool Ads1115::writeVerified(uint8_t reg, uint16_t value) {
  if (!writeRegister(reg, value)) {
    return false;
  }
  uint16_t back = 0;
  if (!readRegister(reg, back)) {
    return false;
  }
  return reg == ads::kRegConfig ? ads::configMatches(back, value) : back == value;
}

bool Ads1115::readConfig(uint16_t &config) { return readRegister(ads::kRegConfig, config); }

bool Ads1115::readSingleEnded(ads::Channel channel, int16_t &raw) {
  if (!writeRegister(ads::kRegConfig, ads::singleShotConfig(channel))) {
    return false;
  }
  const uint32_t start = millis();
  delay(8);
  for (;;) {
    uint16_t cfg = 0;
    if (!readRegister(ads::kRegConfig, cfg)) {
      return false;
    }
    if (cfg & ads::kOs) {
      break;
    }
    if (millis() - start > kConversionTimeoutMs) {
      return false;
    }
    delay(1);
  }
  uint16_t value = 0;
  if (!readRegister(ads::kRegConversion, value)) {
    return false;
  }
  raw = static_cast<int16_t>(value);
  return true;
}

bool Ads1115::setValve(bool on) {
  if (!on) {
    return writeVerified(ads::kRegConfig, ads::valveOffConfig());
  }
  return writeVerified(ads::kRegLoThresh, ads::kValveOnLoThresh) &&
         writeVerified(ads::kRegHiThresh, ads::kValveOnHiThresh) &&
         writeVerified(ads::kRegConfig, ads::valveOnConfig(ads::Channel::A0));
}

bool Ads1115::readWhileValveOn(ads::Channel channel, int16_t &raw) {
  if (!writeVerified(ads::kRegConfig, ads::valveOnConfig(channel))) {
    return false;
  }
  // First result after a MUX change can belong to the old channel.
  delay(20);
  uint16_t value = 0;
  const bool ok = readRegister(ads::kRegConversion, value);
  const bool restored = writeVerified(ads::kRegConfig, ads::valveOnConfig(ads::Channel::A0));
  raw = static_cast<int16_t>(value);
  return ok && restored;
}

}  // namespace wat
