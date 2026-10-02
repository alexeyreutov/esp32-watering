#include <unity.h>

#include <initializer_list>

#include "core/ads1115_regs.h"
#include "core/battery.h"
#include "core/event_codes.h"
#include "core/moisture.h"
#include "core/power_policy.h"
#include "core/schedule.h"
#include "core/watering_policy.h"

using namespace wat;

#define ASSERT_ENUM(expected, actual) TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual))

void setUp() {}
void tearDown() {}

// --- ADS1115 valve trick -----------------------------------------------------

void test_power_on_default_is_valve_off() {
  TEST_ASSERT_FALSE(ads::isValveOnConfig(ads::kPowerOnDefault));
}

void test_valve_configs() {
  TEST_ASSERT_TRUE(ads::isValveOnConfig(ads::valveOnConfig(ads::Channel::A0)));
  TEST_ASSERT_FALSE(ads::isValveOnConfig(ads::valveOffConfig()));
  TEST_ASSERT_FALSE(ads::isValveOnConfig(ads::singleShotConfig(ads::Channel::A0)));
  // Continuous mode, so the comparator keeps re-asserting.
  TEST_ASSERT_EQUAL_HEX16(0, ads::valveOnConfig(ads::Channel::A0) & ads::kModeSingle);
  TEST_ASSERT_EQUAL_HEX16(0x4280, ads::valveOnConfig(ads::Channel::A0));
  TEST_ASSERT_EQUAL_HEX16(0xC383, ads::singleShotConfig(ads::Channel::A0));
  TEST_ASSERT_EQUAL_HEX16(0xD383, ads::singleShotConfig(ads::Channel::A1));
}

void test_thresholds_avoid_conversion_ready_mode() {
  // Conversion-ready mode = Hi MSB 1 and Lo MSB 0. Both must have MSB set,
  // and Hi > Lo as signed values so every reading is "above Hi".
  TEST_ASSERT_TRUE(ads::kValveOnHiThresh & 0x8000);
  TEST_ASSERT_TRUE(ads::kValveOnLoThresh & 0x8000);
  TEST_ASSERT_TRUE(static_cast<int16_t>(ads::kValveOnHiThresh) > static_cast<int16_t>(ads::kValveOnLoThresh));
}

void test_config_match_ignores_os_bit() {
  TEST_ASSERT_TRUE(ads::configMatches(0x8583, 0x0583));
  TEST_ASSERT_FALSE(ads::configMatches(0x8582, 0x0583));
}

// --- moisture ------------------------------------------------------------------

void test_moisture_percent() {
  const Calibration c{17600, 7200};
  TEST_ASSERT_EQUAL_UINT8(0, moisturePercent(17600, c));
  TEST_ASSERT_EQUAL_UINT8(100, moisturePercent(7200, c));
  TEST_ASSERT_EQUAL_UINT8(50, moisturePercent(12400, c));
  TEST_ASSERT_EQUAL_UINT8(0, moisturePercent(20000, c));
  TEST_ASSERT_EQUAL_UINT8(100, moisturePercent(1000, c));
}

void test_moisture_inverted_probe() {
  const Calibration c{7200, 17600};
  TEST_ASSERT_EQUAL_UINT8(0, moisturePercent(7200, c));
  TEST_ASSERT_EQUAL_UINT8(100, moisturePercent(17600, c));
}

void test_calibration_too_narrow() {
  const Calibration c{10000, 9800};
  TEST_ASSERT_FALSE(calibrationValid(c));
  TEST_ASSERT_EQUAL_UINT8(0, moisturePercent(9900, c));
}

void test_median() {
  int16_t v[] = {5, -3, 100, 7, 6};
  TEST_ASSERT_EQUAL_INT16(6, medianOf(v, 5));
  TEST_ASSERT_EQUAL_INT32(2200, adsRawToMillivolts(17600));
}

// --- battery -------------------------------------------------------------------

void test_battery_curve() {
  TEST_ASSERT_EQUAL_UINT8(100, liIonPercent(4250));
  TEST_ASSERT_EQUAL_UINT8(0, liIonPercent(3200));
  TEST_ASSERT_EQUAL_UINT8(55, liIonPercent(3800));
  const uint8_t mid = liIonPercent(3725);
  TEST_ASSERT_TRUE(mid > 35 && mid < 45);
}

// --- schedule ------------------------------------------------------------------

void test_days_from_civil() {
  TEST_ASSERT_EQUAL_INT32(0, daysFromCivil(1970, 1, 1));
  TEST_ASSERT_EQUAL_INT32(20728, daysFromCivil(2026, 10, 2));
  TEST_ASSERT_EQUAL_INT32(19782, daysFromCivil(2024, 2, 29));
}

void test_due_window() {
  const DailySlot slot{7 * 60, 1};
  const int32_t today = 20728;
  TEST_ASSERT_FALSE(isDue(slot, today, 7 * 3600 - 1, kNeverRan, 180));
  TEST_ASSERT_TRUE(isDue(slot, today, 7 * 3600, kNeverRan, 180));
  TEST_ASSERT_TRUE(isDue(slot, today, 10 * 3600, kNeverRan, 180));
  TEST_ASSERT_FALSE(isDue(slot, today, 10 * 3600 + 1, kNeverRan, 180));
  TEST_ASSERT_FALSE(isDue(slot, today, 8 * 3600, today, 180));
  TEST_ASSERT_TRUE(isDue(slot, today, 8 * 3600, today - 1, 180));
}

void test_due_every_n_days() {
  const DailySlot slot{6 * 60, 3};
  const int32_t today = 20728;
  TEST_ASSERT_FALSE(isDue(slot, today, 6 * 3600, today - 2, 60));
  TEST_ASSERT_TRUE(isDue(slot, today, 6 * 3600, today - 3, 60));
}

void test_seconds_until_due() {
  const DailySlot slot{7 * 60, 1};
  const int32_t today = 20728;
  TEST_ASSERT_EQUAL_UINT32(3600, secondsUntilDue(slot, today, 6 * 3600, kNeverRan, 180));
  TEST_ASSERT_EQUAL_UINT32(0, secondsUntilDue(slot, today, 8 * 3600, kNeverRan, 180));
  // Already ran today -> tomorrow 07:00.
  TEST_ASSERT_EQUAL_UINT32(23 * 3600, secondsUntilDue(slot, today, 8 * 3600, today, 180));
  // Window missed -> tomorrow.
  TEST_ASSERT_EQUAL_UINT32(12 * 3600, secondsUntilDue(slot, today, 19 * 3600, kNeverRan, 180));
  // Every 2 days, ran today -> day after tomorrow.
  const DailySlot slot2{7 * 60, 2};
  TEST_ASSERT_EQUAL_UINT32(47 * 3600, secondsUntilDue(slot2, today, 8 * 3600, today, 180));
}

// --- policy --------------------------------------------------------------------

void test_pump_block() {
  PumpConditions c{true, 3900, 3500, true, true, false};
  ASSERT_ENUM(PumpBlock::None, pumpBlockReason(c));
  c.batteryMv = 3400;
  ASSERT_ENUM(PumpBlock::BatteryLow, pumpBlockReason(c));
  c.batteryMv = 100;
  ASSERT_ENUM(PumpBlock::BatteryUnknown, pumpBlockReason(c));
  c.batteryCheck = false;
  ASSERT_ENUM(PumpBlock::None, pumpBlockReason(c));
  c.waterPresent = false;
  ASSERT_ENUM(PumpBlock::NoWater, pumpBlockReason(c));
  c.waterCheck = false;
  ASSERT_ENUM(PumpBlock::None, pumpBlockReason(c));
  c.valveFault = true;
  ASSERT_ENUM(PumpBlock::ValveFault, pumpBlockReason(c));
}

void test_decide_watering() {
  ASSERT_ENUM(WaterDecision::Water, decideWatering(ModuleMode::Moisture, true, true, 20, 35));
  ASSERT_ENUM(WaterDecision::SkipWet, decideWatering(ModuleMode::Moisture, true, true, 35, 35));
  ASSERT_ENUM(WaterDecision::SkipSensorError, decideWatering(ModuleMode::Moisture, false, true, 0, 35));
  ASSERT_ENUM(WaterDecision::SkipUncalibrated, decideWatering(ModuleMode::Moisture, true, false, 0, 35));
  ASSERT_ENUM(WaterDecision::Water, decideWatering(ModuleMode::Schedule, false, false, 0, 35));
  ASSERT_ENUM(WaterDecision::SkipOff, decideWatering(ModuleMode::Off, true, true, 0, 35));
}

void test_mode_keys_roundtrip() {
  for (ModuleMode m : {ModuleMode::Off, ModuleMode::Moisture, ModuleMode::Schedule}) {
    ModuleMode back = ModuleMode::Off;
    TEST_ASSERT_TRUE(moduleModeFromKey(moduleModeKey(m), back));
    ASSERT_ENUM(m, back);
  }
  ModuleMode dummy;
  TEST_ASSERT_FALSE(moduleModeFromKey("bogus", dummy));
}

// --- Power: alarms, sleep planning, silent NTP --------------------------------

void test_alarms() {
  Alarms a = evaluateAlarms({true, 3900, 3500, true, true});
  TEST_ASSERT_FALSE(a.noWater);
  TEST_ASSERT_FALSE(a.lowBattery);
  a = evaluateAlarms({true, 3400, 3500, true, false});
  TEST_ASSERT_TRUE(a.noWater);
  TEST_ASSERT_TRUE(a.lowBattery);
  TEST_ASSERT_EQUAL_UINT8(kAlarmNoWater | kAlarmLowBattery, alarmBits(a));
  // Battery missing with the check on = low (pump is blocked anyway).
  TEST_ASSERT_TRUE(evaluateAlarms({true, 0, 3500, false, false}).lowBattery);
  // Checks off: no alarms whatever the readings.
  a = evaluateAlarms({false, 0, 3500, false, false});
  TEST_ASSERT_EQUAL_UINT8(0, alarmBits(a));
}

void test_alarm_for_block() {
  TEST_ASSERT_EQUAL_UINT8(0, alarmForBlock(PumpBlock::None));
  TEST_ASSERT_EQUAL_UINT8(0, alarmForBlock(PumpBlock::ValveFault));
  TEST_ASSERT_EQUAL_UINT8(kAlarmNoWater, alarmForBlock(PumpBlock::NoWater));
  TEST_ASSERT_EQUAL_UINT8(kAlarmLowBattery, alarmForBlock(PumpBlock::BatteryLow));
  TEST_ASSERT_EQUAL_UINT8(kAlarmLowBattery, alarmForBlock(PumpBlock::BatteryUnknown));
}

void test_planned_sleep() {
  TEST_ASSERT_EQUAL_UINT32(kMaxSleepSeconds, plannedSleepSeconds(false, 100, false));
  TEST_ASSERT_EQUAL_UINT32(kMaxSleepSeconds, plannedSleepSeconds(true, -1, false));
  // Close to a check: wait awake, unless the user asked to sleep.
  TEST_ASSERT_EQUAL_UINT32(0, plannedSleepSeconds(true, 0, false));
  TEST_ASSERT_EQUAL_UINT32(0, plannedSleepSeconds(true, 120, false));
  TEST_ASSERT_EQUAL_UINT32(kMinSleepSeconds, plannedSleepSeconds(true, 0, true));
  TEST_ASSERT_EQUAL_UINT32(90, plannedSleepSeconds(true, 90, true));
  TEST_ASSERT_EQUAL_UINT32(3600 - kWakeLeadSeconds, plannedSleepSeconds(true, 3600, false));
  TEST_ASSERT_EQUAL_UINT32(kMaxSleepSeconds, plannedSleepSeconds(true, 3 * 86400, false));
}

void test_ntp_due() {
  const uint32_t day = 86400;
  TEST_ASSERT_TRUE(ntpSyncDue(false, 0, 0, 0));  // clock unknown: always try
  TEST_ASSERT_FALSE(ntpSyncDue(true, 10 * day, 0, 0));
  TEST_ASSERT_TRUE(ntpSyncDue(true, 10 * day, 0, 7));
  TEST_ASSERT_FALSE(ntpSyncDue(true, 10 * day, 4 * day, 7));
  TEST_ASSERT_TRUE(ntpSyncDue(true, 11 * day, 4 * day, 7));
  TEST_ASSERT_TRUE(ntpSyncDue(true, 3 * day, 4 * day, 7));  // clock went back
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_power_on_default_is_valve_off);
  RUN_TEST(test_valve_configs);
  RUN_TEST(test_thresholds_avoid_conversion_ready_mode);
  RUN_TEST(test_config_match_ignores_os_bit);
  RUN_TEST(test_moisture_percent);
  RUN_TEST(test_moisture_inverted_probe);
  RUN_TEST(test_calibration_too_narrow);
  RUN_TEST(test_median);
  RUN_TEST(test_battery_curve);
  RUN_TEST(test_days_from_civil);
  RUN_TEST(test_due_window);
  RUN_TEST(test_due_every_n_days);
  RUN_TEST(test_seconds_until_due);
  RUN_TEST(test_pump_block);
  RUN_TEST(test_decide_watering);
  RUN_TEST(test_mode_keys_roundtrip);
  RUN_TEST(test_alarms);
  RUN_TEST(test_alarm_for_block);
  RUN_TEST(test_planned_sleep);
  RUN_TEST(test_ntp_due);
  return UNITY_END();
}
