#pragma once

#include "base_io.h"
#include "config.h"
#include "display.h"
#include "garden.h"
#include "module_bus.h"
#include "network.h"
#include "power_manager.h"
#include "records.h"
#include "time_keeper.h"
#include "watering_controller.h"
#include "web_ui.h"

namespace wat {

// Everything the subsystems share. One instance, owned by main.cpp; each
// subsystem gets it in begin() instead of reaching for globals.
struct App {
  AppConfig config;

  Pump pump;
  BatteryMonitor battery;
  WaterLevel water;
  StatusLed led;
  AlarmLed alarmLed;
  ServiceButton button;
  Display display;
  ModuleBus bus;

  TimeKeeper time;
  History history;
  EventLog log;

  WateringController controller;
  Garden garden;
  Network net;
  WebUi web;
  PowerManager power;
};

}  // namespace wat
