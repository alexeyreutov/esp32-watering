#include "watering_controller.h"

#include <esp_attr.h>

#include "app.h"
#include "core/power_policy.h"

namespace wat {

namespace {
constexpr uint32_t kMonitorPeriodMs = 250;
constexpr uint32_t kNoWaterAbortMs = 2000;
constexpr uint32_t kLowBatteryAbortMs = 2000;
constexpr uint32_t kFaultRetryMs = 5000;
constexpr uint16_t kValveTestSeconds = 5;
constexpr uint32_t kPumpMarkerValue = 0x50554D50;  // "PUMP"

// Survives a reset but not a power cut. Set while the pump runs, so a boot
// that finds it set knows the reset (usually a brown-out) hit mid-watering.
RTC_NOINIT_ATTR uint32_t g_pumpMarker;
}  // namespace

bool consumePumpResetMarker() {
  const bool set = g_pumpMarker == kPumpMarkerValue;
  g_pumpMarker = 0;
  return set;
}

void WateringController::begin(App &app) { app_ = &app; }

void WateringController::enter(State s) {
  state_ = s;
  stateSince_ = millis();
}

const char *WateringController::stateKey() const {
  switch (state_) {
    case State::Idle:
      return "idle";
    case State::ValveLead:
      return "valve_lead";
    case State::Pumping:
      return "pumping";
    case State::PumpTail:
      return "pump_tail";
    case State::Fault:
      return "fault";
  }
  return "unknown";
}

uint32_t WateringController::remainingSeconds() const {
  if (state_ != State::Pumping) {
    return 0;
  }
  const uint32_t elapsed = (millis() - pumpStartedAt_) / 1000;
  return elapsed >= job_.seconds ? 0 : job_.seconds - elapsed;
}

bool WateringController::hasModuleQueued(uint8_t module) const {
  for (uint8_t i = 0; i < queueCount_; ++i) {
    if (queue_[(queueHead_ + i) % kQueueSize].module == module) {
      return true;
    }
  }
  return activeModule() == static_cast<int8_t>(module);
}

bool WateringController::enqueue(const WateringJob &job) {
  if (job.module >= kMaxModules || queueCount_ >= kQueueSize || state_ == State::Fault ||
      hasModuleQueued(job.module)) {
    return false;
  }
  WateringJob j = job;
  if (j.kind == JobKind::ValveTest) {
    j.seconds = kValveTestSeconds;
  }
  if (j.seconds == 0) {
    return false;
  }
  if (j.seconds > app_->config.maxPumpSec) {
    j.seconds = app_->config.maxPumpSec;
  }
  queue_[(queueHead_ + queueCount_) % kQueueSize] = j;
  ++queueCount_;
  return true;
}

void WateringController::stop() {
  queueCount_ = 0;
  if (state_ == State::ValveLead || state_ == State::Pumping) {
    stopRequested_ = true;
  }
}

bool WateringController::emergencyStop() {
  queueCount_ = 0;
  app_->pump.off();
  g_pumpMarker = 0;
  if (state_ == State::ValveLead || state_ == State::Pumping || state_ == State::PumpTail) {
    // Let the line depressurise a little before the valve slams shut.
    delay(app_->config.valveTailMs);
    if (state_ == State::Pumping) {
      app_->log.add(Event::WateringAborted, LogLevel::Warn, job_.module,
                    static_cast<int32_t>(AbortReason::Shutdown),
                    static_cast<int32_t>((millis() - pumpStartedAt_) / 1000));
    }
  }
  const bool closed = app_->bus.closeAllValves();
  enter(closed ? State::Idle : State::Fault);
  return closed;
}

PumpConditions WateringController::conditions(bool underLoad) const {
  const AppConfig &c = app_->config;
  PumpConditions p{};
  p.batteryCheck = c.batteryCheck;
  p.batteryMv = app_->battery.readMillivolts(c.batteryCalibration);
  p.thresholdMv = underLoad ? c.batteryAbortMv : c.batteryMinMv;
  p.waterCheck = c.waterCheck;
  p.waterPresent = app_->water.waterPresent(c.waterPresentHigh);
  p.valveFault = state_ == State::Fault;
  return p;
}

void WateringController::startNext() {
  job_ = queue_[queueHead_];
  queueHead_ = (queueHead_ + 1) % kQueueSize;
  --queueCount_;
  stopRequested_ = false;
  abortReason_ = 0;
  actualSeconds_ = 0;

  const uint8_t histManual = job_.kind == JobKind::Scheduled ? 0 : kHistManual;
  const PumpConditions cond = conditions(false);
  if (job_.kind != JobKind::ValveTest) {
    const PumpBlock block = pumpBlockReason(cond);
    lastBlock_ = block;
    if (block != PumpBlock::None) {
      app_->log.add(Event::PumpBlocked, LogLevel::Warn, job_.module, static_cast<int32_t>(block));
      app_->power.raiseAlarm(alarmForBlock(block));
      app_->history.add(job_.module, job_.moisturePct, 0, cond.batteryMv, 0,
                        static_cast<uint8_t>(kHistBlocked | histManual));
      return;
    }
  }

  app_->bus.setPowered(true);
  if (!app_->bus.openValve(job_.module)) {
    app_->log.add(Event::ValveOpenFailed, LogLevel::Error, job_.module);
    // openValve already tried to undo a half-written state; make sure.
    if (!app_->bus.closeValve(job_.module)) {
      app_->log.add(Event::ValveCloseFailed, LogLevel::Error, job_.module);
      enter(State::Fault);
    }
    return;
  }
  app_->log.add(Event::WateringStarted, LogLevel::Info, job_.module, job_.seconds,
                static_cast<int32_t>(job_.kind));
  enter(State::ValveLead);
}

void WateringController::beginTail(uint8_t abortReason) {
  app_->pump.off();
  g_pumpMarker = 0;
  abortReason_ = abortReason;
  if (state_ == State::Pumping) {
    actualSeconds_ = static_cast<uint16_t>((millis() - pumpStartedAt_ + 500) / 1000);
  }
  enter(State::PumpTail);
}

void WateringController::finishTail() {
  const uint8_t module = job_.module;
  // Line voltage is read just before closing: that is when the valve has
  // been drawing current longest and the drop over the cable is visible.
  if (app_->config.modules[module].lineSense) {
    uint16_t mv = 0;
    if (app_->bus.readLineMillivolts(module, mv)) {
      app_->log.add(Event::LineVoltage, LogLevel::Info, module, mv);
    }
  }
  if (!app_->bus.closeValve(module)) {
    app_->log.add(Event::ValveCloseFailed, LogLevel::Error, module);
    lastFaultRetryAt_ = millis();
    queueCount_ = 0;
    enter(State::Fault);
    return;
  }

  if (job_.kind == JobKind::ValveTest) {
    app_->log.add(Event::ValveTestDone, LogLevel::Info, module);
  } else {
    const uint16_t batteryMv = app_->battery.readMillivolts(app_->config.batteryCalibration);
    if (abortReason_ == 0) {
      app_->log.add(Event::WateringDone, LogLevel::Info, module, actualSeconds_);
    } else {
      app_->log.add(Event::WateringAborted, LogLevel::Warn, module, abortReason_, actualSeconds_);
    }
    if (actualSeconds_ > 0) {
      const uint8_t flags =
          static_cast<uint8_t>(kHistWatered | (job_.kind == JobKind::Scheduled ? 0 : kHistManual));
      app_->history.add(module, job_.moisturePct, 0, batteryMv, actualSeconds_, flags);
      app_->garden.noteWatered(module, actualSeconds_);
    }
  }
  enter(State::Idle);
}

void WateringController::monitorWhilePumping() {
  const uint32_t now = millis();
  if (now - lastMonitorAt_ < kMonitorPeriodMs) {
    return;
  }
  lastMonitorAt_ = now;
  const PumpConditions c = conditions(true);

  if (c.waterCheck && !c.waterPresent) {
    if (noWaterSince_ == 0) {
      noWaterSince_ = now;
    } else if (now - noWaterSince_ >= kNoWaterAbortMs) {
      lastBlock_ = PumpBlock::NoWater;
      app_->power.raiseAlarm(alarmForBlock(lastBlock_));
      beginTail(static_cast<uint8_t>(AbortReason::NoWater));
      return;
    }
  } else {
    noWaterSince_ = 0;
  }

  if (c.batteryCheck && c.batteryMv < c.thresholdMv) {
    if (lowBatterySince_ == 0) {
      lowBatterySince_ = now;
    } else if (now - lowBatterySince_ >= kLowBatteryAbortMs) {
      lastBlock_ = PumpBlock::BatteryLow;
      app_->power.raiseAlarm(alarmForBlock(lastBlock_));
      beginTail(static_cast<uint8_t>(AbortReason::BatteryLow));
      return;
    }
  } else {
    lowBatterySince_ = 0;
  }
}

void WateringController::loop() {
  const uint32_t now = millis();
  switch (state_) {
    case State::Idle:
      if (queueCount_ > 0) {
        startNext();
      }
      return;

    case State::ValveLead:
      if (stopRequested_) {
        beginTail(static_cast<uint8_t>(AbortReason::UserStop));
        return;
      }
      if (now - stateSince_ >= app_->config.valveLeadMs) {
        if (job_.kind != JobKind::ValveTest) {
          g_pumpMarker = kPumpMarkerValue;
          app_->pump.on();
        }
        pumpStartedAt_ = now;
        noWaterSince_ = 0;
        lowBatterySince_ = 0;
        lastMonitorAt_ = now;
        enter(State::Pumping);
      }
      return;

    case State::Pumping:
      if (stopRequested_) {
        beginTail(static_cast<uint8_t>(AbortReason::UserStop));
        return;
      }
      if (now - pumpStartedAt_ >= static_cast<uint32_t>(job_.seconds) * 1000) {
        beginTail(0);
        return;
      }
      if (job_.kind != JobKind::ValveTest) {
        monitorWhilePumping();
      }
      return;

    case State::PumpTail:
      if (now - stateSince_ >= app_->config.valveTailMs) {
        finishTail();
      }
      return;

    case State::Fault:
      app_->pump.off();
      if (now - lastFaultRetryAt_ >= kFaultRetryMs) {
        lastFaultRetryAt_ = now;
        if (app_->bus.closeAllValves()) {
          app_->log.add(Event::ValveRecovered, LogLevel::Info);
          enter(State::Idle);
        }
      }
      return;
  }
}

}  // namespace wat
