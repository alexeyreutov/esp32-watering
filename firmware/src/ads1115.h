#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "core/ads1115_regs.h"

namespace wat {

// Minimal ADS1115 driver. A library would hide the comparator registers that
// drive the valve, so registers are written directly (see core/ads1115_regs.h).
class Ads1115 {
 public:
  void begin(TwoWire &wire, uint8_t address);

  bool probe();
  uint8_t address() const { return address_; }

  // Comparator released (ALERT high-Z) -> valve off.
  bool readSingleEnded(ads::Channel channel, int16_t &raw);
  bool setValve(bool on);
  bool readConfig(uint16_t &config);
  // Reads a channel while keeping the valve energised (continuous mode).
  bool readWhileValveOn(ads::Channel channel, int16_t &raw);

 private:
  bool writeRegister(uint8_t reg, uint16_t value);
  bool readRegister(uint8_t reg, uint16_t &value);
  bool writeVerified(uint8_t reg, uint16_t value);

  TwoWire *wire_ = nullptr;
  uint8_t address_ = ads::kBaseAddress;
};

}  // namespace wat
