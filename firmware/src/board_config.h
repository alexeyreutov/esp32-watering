#pragma once

#include <stdint.h>

// ESP32-C3 board with on-board 0.42" OLED (72x40, I2C 0x3C on GPIO5/GPIO6,
// 01Space "ESP32-C3-0.42LCD" and clones). Pinout differs from the plain
// SuperMini - do not copy pins between the two.
//
// Constraints that shaped this table:
//   - only GPIO0..GPIO5 can wake the C3 from deep sleep -> button on GPIO3;
//   - GPIO5/GPIO6 are taken by the OLED; the C3 has ONE hardware I2C, which
//     goes to the long module line, so the OLED runs on software I2C;
//   - GPIO2/8/9 are strapping pins (must not be pulled low at reset);
//   - GPIO21 (TX) toggles during the ROM boot log - nothing that moves water.
// Wiring: docs/hardware/pins.md + wiring.md - keep all three in sync.
namespace wat::board {

constexpr int kPinBatteryAdc = 0;     // B+ via 100k/100k divider, 100 nF to GND
constexpr int kPinWaterLevel = 1;     // optical level sensor output (5 V -> divider)
constexpr int kPinBusPower = 2;       // P-MOSFET gate of the line 3V3 switch, 10k pull-up
constexpr int kPinWakeButton = 3;     // to GND, 10k pull-up to 3V3; deep-sleep wake
constexpr int kPinAlarmLed = 4;       // red LED via 1k..2.2k to GND, active HIGH
constexpr int kPinOledSda = 5;        // on-board OLED, software I2C
constexpr int kPinOledScl = 6;        // on-board OLED, software I2C
constexpr int kPinI2cSda = 7;         // module line
constexpr int kPinLed = 8;            // on-board blue LED, active LOW
constexpr int kPinBootButton = 9;     // on-board BOOT ("BOO"), usable after reset (cannot wake)
constexpr int kPinI2cScl = 10;        // module line
constexpr int kPinPump = 20;          // RX pad; MOSFET module input, 10k pull-down

constexpr bool kLedActiveLow = true;
// P-MOSFET: LOW = line powered. The pull-up keeps the line off during reset
// and deep sleep, and it also satisfies GPIO2's strapping requirement.
constexpr bool kBusPowerActiveHigh = false;

// The panel controller differs between sellers: SSD1306 (most) or SH1106
// ("WISE"). Wrong choice = picture shifted by a few pixels, see README.
#ifndef WAT_OLED_SH1106
#define WAT_OLED_SH1106 0
#endif

// Long cable + 3.3 V pull-ups: slow I2C on purpose. 400 pF is the I2C limit;
// a typical multicore cable is 50-100 pF/m, so the margin comes from speed.
constexpr uint32_t kI2cHz = 20000;
constexpr uint16_t kI2cTimeoutMs = 50;

constexpr float kBatteryDividerRatio = 2.0f;  // (100k + 100k) / 100k
constexpr float kLineDividerRatio = 2.0f;     // module A1: 100k/100k from the 5 V line

constexpr uint16_t kBusSettleMs = 400;  // after switching line power: sensors + ADS POR

constexpr bool kLimitWifiTxPower = false;

}  // namespace wat::board
