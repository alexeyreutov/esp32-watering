#pragma once

#include <Arduino.h>

#include "ads1115.h"
#include "config.h"

namespace wat {

// The chain of watering modules on one I2C bus. Module index i lives at
// ADS1115 address 0x48 + i; position in the chain does not matter.
//
// Invariant: at most one valve is open at a time, and it is only opened
// through openValve() so openValveIndex() is always truthful.
class ModuleBus {
 public:
  void begin();

  // Optional line power switch (board::kPinBusPower). Cutting power also
  // resets every ADS1115 to its power-on state, i.e. all valves closed.
  void setPowered(bool on);
  bool powered() const { return powered_; }

  bool probe(uint8_t index);
  bool readMoisture(uint8_t index, int16_t &raw);
  bool openValve(uint8_t index);
  bool closeValve(uint8_t index);
  // Closes every address, present or not. Returns false if a module that
  // answers could not be confirmed closed.
  bool closeAllValves();
  int8_t openValveIndex() const { return openIndex_; }
  bool readLineMillivolts(uint8_t index, uint16_t &mv);

  // Indices of modules found with ALERT asserted at boot (bit mask).
  uint8_t valvesOpenAtBoot() const { return openAtBoot_; }

 private:
  void recoverBus();
  void initWire();

  Ads1115 devices_[kMaxModules];
  int8_t openIndex_ = -1;
  bool powered_ = true;
  uint8_t openAtBoot_ = 0;
};

}  // namespace wat
