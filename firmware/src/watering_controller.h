#pragma once

#include <Arduino.h>

#include "core/event_codes.h"
#include "core/watering_policy.h"

namespace wat {

struct App;

// True if the previous reset happened while the pump was running.
bool consumePumpResetMarker();

struct WateringJob {
  uint8_t module;
  uint16_t seconds;
  JobKind kind;
  uint8_t moisturePct;  // reading that triggered the job, for history
};

// Non-blocking valve + pump sequence, one module at a time:
//   open valve -> lead delay -> pump on -> run (watch water/battery) ->
//   pump off -> tail delay -> close valve (verified).
// The valve opens before and closes after the pump so the pump never pushes
// against a closed line.
class WateringController {
 public:
  enum class State : uint8_t { Idle, ValveLead, Pumping, PumpTail, Fault };

  void begin(App &app);
  void loop();

  bool enqueue(const WateringJob &job);
  void stop();
  // Synchronous: pump off and all valves closed before returning. For OTA,
  // sleep and reboot.
  bool emergencyStop();

  bool idle() const { return state_ == State::Idle && queueCount_ == 0; }
  State state() const { return state_; }
  const char *stateKey() const;
  int8_t activeModule() const { return state_ == State::Idle || state_ == State::Fault ? -1 : job_.module; }
  JobKind activeKind() const { return job_.kind; }
  uint32_t remainingSeconds() const;
  uint8_t queued() const { return queueCount_; }
  PumpBlock lastBlock() const { return lastBlock_; }
  bool hasModuleQueued(uint8_t module) const;

 private:
  void startNext();
  void beginTail(uint8_t abortReason);
  void finishTail();
  void monitorWhilePumping();
  PumpConditions conditions(bool underLoad) const;
  void enter(State s);

  App *app_ = nullptr;
  State state_ = State::Idle;
  uint32_t stateSince_ = 0;
  WateringJob job_{};
  uint32_t pumpStartedAt_ = 0;
  uint16_t actualSeconds_ = 0;
  uint8_t abortReason_ = 0;

  static constexpr uint8_t kQueueSize = 8;
  WateringJob queue_[kQueueSize]{};
  uint8_t queueHead_ = 0;
  uint8_t queueCount_ = 0;

  uint32_t lastMonitorAt_ = 0;
  uint32_t noWaterSince_ = 0;
  uint32_t lowBatterySince_ = 0;
  uint32_t lastFaultRetryAt_ = 0;
  PumpBlock lastBlock_ = PumpBlock::None;
  bool stopRequested_ = false;
};

}  // namespace wat
