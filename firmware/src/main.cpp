#include <Arduino.h>
#include <LittleFS.h>
#include <esp_system.h>

#include "app.h"

using namespace wat;

namespace {
App g_app;

StatusLed::Pattern ledPattern() {
  if (g_app.controller.state() == WateringController::State::Fault) {
    return StatusLed::Pattern::Fault;
  }
  if (!g_app.controller.idle()) {
    return StatusLed::Pattern::Watering;
  }
  if (g_app.net.apActive()) {
    return StatusLed::Pattern::AccessPoint;
  }
  return StatusLed::Pattern::Off;
}

void handleButton() {
  switch (g_app.button.poll()) {
    case ServiceButton::Press::None:
      return;
    case ServiceButton::Press::Short:
      g_app.power.onShortPress();
      return;
    case ServiceButton::Press::Long:
      // Forgotten or changed router password: back to the setup AP.
      g_app.log.add(Event::WifiReset, LogLevel::Warn);
      g_app.config.wifiSsid = "";
      g_app.config.wifiPassword = "";
      saveConfig(g_app.config);
      g_app.controller.emergencyStop();
      ESP.restart();
      return;
  }
}
}  // namespace

void setup() {
  // Pump first: nothing else may run while its pin is still floating (or
  // still held from deep sleep).
  g_app.pump.begin();
  g_app.led.begin();
  g_app.alarmLed.begin(g_app.power.alarms() != 0);
  Serial.begin(115200);

  if (!LittleFS.begin(/*formatOnFail=*/true)) {
    Serial.println("[fs] LittleFS mount failed - history and settings are not persistent");
  }
  loadConfig(g_app.config);

  g_app.time.begin(g_app.config.timezone, g_app.config.ntpServer);
  g_app.history.begin(g_app.time);
  g_app.log.begin(g_app.time);

  const esp_reset_reason_t reason = esp_reset_reason();
  g_app.log.add(Event::Boot, LogLevel::Info, kNoModule, static_cast<int32_t>(reason));
  if (consumePumpResetMarker()) {
    g_app.log.add(Event::ResetDuringPump, LogLevel::Error, kNoModule, static_cast<int32_t>(reason));
  }

  g_app.battery.begin();
  g_app.water.begin();
  g_app.button.begin();
  g_app.bus.begin();
  for (uint8_t i = 0; i < kMaxModules; ++i) {
    if (g_app.bus.valvesOpenAtBoot() & (1u << i)) {
      g_app.log.add(Event::ValveOpenAtBoot, LogLevel::Warn, i);
    }
  }

  g_app.controller.begin(g_app);
  g_app.garden.begin(g_app);
  // Decides the wake mode; starts Wi-Fi + web only for a button session.
  g_app.power.begin(g_app);
  g_app.display.begin(g_app);

  enableLoopWDT();
  Serial.printf("[main] ready, mode %s\n", g_app.power.modeKey());
}

void loop() {
  g_app.controller.loop();
  if (g_app.net.started()) {
    g_app.net.loop();
  }
  if (g_app.power.wifiSession()) {
    g_app.web.loop();
  }
  g_app.garden.loop();
  handleButton();
  g_app.led.loop(ledPattern());
  g_app.display.loop();
  if (g_app.time.takeSyncedEvent()) {
    g_app.log.add(Event::TimeSynced, LogLevel::Info, kNoModule, 0);
  }
  g_app.power.loop();
  delay(2);
}
