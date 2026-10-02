#pragma once

#include <stdint.h>

// ADS1115 register map and config words. Pure constants so the valve trick
// (ALERT/RDY used as an open-drain output) is covered by native tests.
//
// Valve control idea:
//   - COMP_QUE = 11 (power-on default) -> ALERT/RDY is high-Z -> valve OFF.
//   - COMP_QUE = 00, traditional comparator, active-low, thresholds
//     Lo = 0x8000, Hi = 0x8001 -> every conversion is "above Hi" and nothing
//     is ever "below Lo", so the pin is held LOW -> valve ON.
//   Both thresholds keep MSB = 1: Hi.MSB = 1 together with Lo.MSB = 0 would
//   switch the pin into conversion-ready mode instead.
namespace wat::ads {

constexpr uint8_t kRegConversion = 0x00;
constexpr uint8_t kRegConfig = 0x01;
constexpr uint8_t kRegLoThresh = 0x02;
constexpr uint8_t kRegHiThresh = 0x03;

constexpr uint8_t kBaseAddress = 0x48;  // ADDR->GND; +1 VDD, +2 SDA, +3 SCL
constexpr uint8_t kAddressCount = 4;

enum class Channel : uint8_t { A0 = 0, A1 = 1, A2 = 2, A3 = 3 };

constexpr uint16_t kOs = 0x8000;  // write: start single shot; read: 1 = idle
constexpr uint16_t kPga4096 = 0x0200;  // +-4.096 V, 125 uV/LSB
constexpr uint16_t kModeSingle = 0x0100;
constexpr uint16_t kModeContinuous = 0x0000;
constexpr uint16_t kDr128 = 0x0080;  // 128 SPS, ~7.8 ms per conversion
constexpr uint16_t kCompQueAfterOne = 0x0000;
constexpr uint16_t kCompQueDisable = 0x0003;
constexpr uint16_t kCompQueMask = 0x0003;
constexpr uint16_t kPowerOnDefault = 0x8583;

constexpr uint16_t kValveOnLoThresh = 0x8000;
constexpr uint16_t kValveOnHiThresh = 0x8001;

constexpr uint16_t muxSingleEnded(Channel ch) {
  return static_cast<uint16_t>((0x4u + static_cast<uint8_t>(ch)) << 12);
}

// Single-shot read with the comparator disabled. Only valid while the valve
// of this module is closed - it releases ALERT/RDY.
constexpr uint16_t singleShotConfig(Channel ch) {
  return kOs | muxSingleEnded(ch) | kPga4096 | kModeSingle | kDr128 | kCompQueDisable;
}

// Continuous conversions keep the comparator re-evaluating, so the pin stays
// asserted. COMP_POL = 0 (active low), COMP_LAT = 0, COMP_MODE = 0.
constexpr uint16_t valveOnConfig(Channel ch) {
  return muxSingleEnded(ch) | kPga4096 | kModeContinuous | kDr128 | kCompQueAfterOne;
}

constexpr uint16_t valveOffConfig() {
  return muxSingleEnded(Channel::A0) | kPga4096 | kModeSingle | kDr128 | kCompQueDisable;
}

// OS reads back as a status bit, so it never matches what was written.
constexpr bool configMatches(uint16_t readBack, uint16_t written) {
  return (readBack & 0x7FFFu) == (written & 0x7FFFu);
}

constexpr bool isValveOnConfig(uint16_t cfg) {
  return (cfg & kCompQueMask) != kCompQueDisable;
}

}  // namespace wat::ads
