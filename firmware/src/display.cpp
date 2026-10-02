#include "display.h"

#include <U8g2lib.h>

#include "app.h"
#include "board_config.h"
#include "core/battery.h"

namespace wat {

namespace {
constexpr uint32_t kRenderPeriodMs = 500;
constexpr uint32_t kReadingShowMs = 3000;
constexpr int kWidth = 72;
constexpr int kBarHeight = 10;

// Software I2C: the only hardware I2C controller drives the module line,
// and a stuck line must not take the screen down with it.
#if WAT_OLED_SH1106
U8G2_SH1106_72X40_WISE_F_SW_I2C g_oled(U8G2_R0, board::kPinOledScl, board::kPinOledSda);
#else
U8G2_SSD1306_72X40_ER_F_SW_I2C g_oled(U8G2_R0, board::kPinOledScl, board::kPinOledSda);
#endif
}  // namespace

void Display::begin(App &app) {
  app_ = &app;
  g_oled.begin();
  g_oled.setPowerSave(0);
  g_oled.setContrast(160);
  render();
}

void Display::loop() {
  const uint32_t now = millis();
  if (now - lastRenderAt_ < kRenderPeriodMs) {
    return;
  }
  lastRenderAt_ = now;
  render();
}

void Display::showSleepSplash() {
  splash_ = true;
  render();
}

void Display::powerOff() {
  g_oled.clearBuffer();
  g_oled.sendBuffer();
  g_oled.setPowerSave(1);
}

void Display::drawWifi(int x, int y, bool solid) {
  // Three arcs over a dot; (x, y) = bottom centre.
  g_oled.drawDisc(x, y, 1);
  if (!solid && (millis() / 500) % 2) {
    return;  // blinking while connecting
  }
  for (int r = 4; r <= 10; r += 3) {
    g_oled.drawCircle(x, y, r, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
  }
}

void Display::drawBattery(int x, int y, uint8_t pct, bool empty) {
  g_oled.drawFrame(x, y, 14, 8);
  g_oled.drawBox(x + 14, y + 2, 2, 4);
  if (empty) {
    if ((millis() / 500) % 2) {
      g_oled.drawLine(x + 3, y + 1, x + 10, y + 6);
    }
    return;
  }
  const int w = 10 * pct / 100;
  if (w > 0) {
    g_oled.drawBox(x + 2, y + 2, w, 4);
  }
}

void Display::drawDrop(int x, int y, bool crossed) {
  // (x, y) = top of the drop, ~7x9 px.
  g_oled.drawTriangle(x, y, x - 3, y + 5, x + 3, y + 5);
  g_oled.drawDisc(x, y + 5, 3);
  if (crossed) {
    g_oled.setDrawColor(2);
    g_oled.drawLine(x - 4, y, x + 4, y + 9);
    g_oled.drawLine(x - 4, y + 1, x + 4, y + 10);
    g_oled.setDrawColor(1);
  }
}

void Display::drawTopBar() {
  const PowerManager &p = app_->power;
  if (p.wifiSession()) {
    const bool sta = app_->net.stationConnected();
    const bool ap = app_->net.apActive();
    drawWifi(6, 9, sta || ap);
    if (ap && !sta) {
      g_oled.setFont(u8g2_font_4x6_tr);
      g_oled.drawStr(12, 9, "AP");
    }
  }
  if (p.noWaterAlarm()) {
    drawDrop(26, 0, true);
  }
  const uint16_t mv = p.batteryMillivolts();
  const uint8_t pct = liIonPercent(mv);
  const bool empty = p.lowBatteryAlarm();
  char buf[6];
  snprintf(buf, sizeof(buf), "%u%%", static_cast<unsigned>(pct));
  g_oled.setFont(u8g2_font_4x6_tr);
  g_oled.drawStr(kWidth - 18 - g_oled.getStrWidth(buf), 7, buf);
  drawBattery(kWidth - 16, 0, pct, empty);
}

void Display::drawPotValue(uint8_t pot, const char *big) {
  char label[4];
  snprintf(label, sizeof(label), "#%u", static_cast<unsigned>(pot));
  g_oled.setFont(u8g2_font_6x10_tr);
  g_oled.drawStr(0, 26, label);
  g_oled.setFont(u8g2_font_logisoso18_tr);
  g_oled.drawStr(kWidth - g_oled.getStrWidth(big), 39, big);
}

void Display::render() {
  g_oled.clearBuffer();
  if (splash_) {
    g_oled.setFont(u8g2_font_logisoso18_tr);
    g_oled.drawStr(16, 30, "zZz");
    g_oled.sendBuffer();
    return;
  }
  drawTopBar();

  const WateringController &c = app_->controller;
  const Garden &g = app_->garden;
  char big[8];
  if (c.activeModule() >= 0) {
    const uint8_t pot = static_cast<uint8_t>(c.activeModule() + 1);
    snprintf(big, sizeof(big), "%lus", static_cast<unsigned long>(c.remainingSeconds()));
    drawPotValue(pot, big);
    drawDrop(19, 17, false);
  } else if (g.lastMeasuredModule() >= 0 && millis() - g.lastMeasuredAtMs() < kReadingShowMs) {
    const uint8_t m = static_cast<uint8_t>(g.lastMeasuredModule());
    const ModuleRuntime &r = g.runtime(m);
    if (r.readingOk) {
      snprintf(big, sizeof(big), "%u%%", static_cast<unsigned>(r.pct));
    } else {
      snprintf(big, sizeof(big), "--");
    }
    drawPotValue(static_cast<uint8_t>(m + 1), big);
  } else if (c.state() == WateringController::State::Fault) {
    g_oled.setFont(u8g2_font_logisoso18_tr);
    g_oled.drawStr(4, 39, "ERR!");
  } else {
    TimeKeeper::LocalTime lt{};
    if (app_->time.local(app_->time.now(), lt)) {
      snprintf(big, sizeof(big), "%02lu:%02lu", static_cast<unsigned long>(lt.secondOfDay / 3600),
               static_cast<unsigned long>(lt.secondOfDay / 60 % 60));
    } else {
      snprintf(big, sizeof(big), "--:--");
    }
    g_oled.setFont(u8g2_font_logisoso18_tr);
    g_oled.drawStr((kWidth - g_oled.getStrWidth(big)) / 2, 39, big);
  }
  g_oled.sendBuffer();
}

}  // namespace wat
