#pragma once

#include <Arduino.h>

namespace wat {

struct App;

// On-board 72x40 OLED. Icons and digits only: at this size text does not
// fit, and the board is mostly looked at from a metre away.
//
// Screen priority (highest first):
//   sleep splash -> watering (#pot, drop, seconds left)
//   -> fresh reading (#pot, moisture %) -> status (time).
// The top bar is on every screen: Wi-Fi icon (button session only),
// alarm icons (crossed drop = no water, empty battery = low), battery %.
class Display {
 public:
  void begin(App &app);
  void loop();
  void showSleepSplash();
  void powerOff();  // SSD1306 sleep mode, ~10 uA

 private:
  void render();
  void drawTopBar();
  void drawWifi(int x, int y, bool solid);
  void drawBattery(int x, int y, uint8_t pct, bool empty);
  void drawDrop(int x, int y, bool crossed);
  void drawPotValue(uint8_t pot, const char *big);

  App *app_ = nullptr;
  uint32_t lastRenderAt_ = 0;
  bool splash_ = false;
};

}  // namespace wat
