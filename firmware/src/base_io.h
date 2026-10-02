#pragma once

#include <Arduino.h>

namespace wat {

class Pump {
 public:
  // Must run first in setup(): until then the MOSFET input only has its
  // pull-down resistor.
  void begin();
  void on();
  void off();
  bool isOn() const { return on_; }
  // Latches the pin LOW through deep sleep instead of leaving it to the
  // pull-down alone.
  void holdOffForSleep();

 private:
  bool on_ = false;
};

// Red LED: latched alarm (no water / low battery). Stays lit through deep
// sleep via GPIO hold; only a later wake that finds everything OK clears it.
class AlarmLed {
 public:
  void begin(bool lit);
  void set(bool lit);
  void holdForSleep();

 private:
  bool lit_ = false;
};

class BatteryMonitor {
 public:
  void begin();
  // Averaged B+ voltage in mV (divider and calibration applied).
  uint16_t readMillivolts(float calibration);
};

class WaterLevel {
 public:
  void begin();
  // Majority of several reads, so one noisy sample neither blocks nor
  // permits the pump.
  bool waterPresent(bool presentHigh);
};

class StatusLed {
 public:
  enum class Pattern : uint8_t { Off, Watering, AccessPoint, Fault };
  void begin();
  void loop(Pattern pattern);
  void offForSleep();

 private:
  void write(bool lit);
};

// Wake button (external, GPIO3) OR'ed with on-board BOOT.
class ServiceButton {
 public:
  enum class Press : uint8_t { None, Short, Long };
  // A press that woke the board is still held here; its release must not
  // count as the "go back to sleep" press.
  void begin();
  Press poll();
  bool isDown() const;

 private:
  bool wasDown_ = false;
  bool longFired_ = false;
  uint32_t downSince_ = 0;
};

}  // namespace wat
