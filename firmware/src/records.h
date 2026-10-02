#pragma once

#include <Arduino.h>

#include "core/event_codes.h"
#include "ring_store.h"
#include "time_keeper.h"

namespace wat {

// On-flash formats. Changing a struct requires bumping its magic in
// records.cpp - RingStore then starts the file over instead of misreading it.

enum HistoryFlags : uint8_t {
  kHistWatered = 1u << 0,      // wateredSec is valid
  kHistSensorError = 1u << 1,
  kHistSkippedWet = 1u << 2,
  kHistBlocked = 1u << 3,      // pump was blocked (battery / water / fault)
  kHistManual = 1u << 4,
  kHistUptimeStamp = 1u << 7,  // ts is seconds since boot, clock was unset
};

struct __attribute__((packed)) HistoryRecord {
  uint32_t ts;
  uint8_t module;
  uint8_t moisturePct;
  int16_t raw;
  uint16_t batteryMv;
  uint16_t wateredSec;
  uint8_t flags;
  uint8_t reserved[3];
};
static_assert(sizeof(HistoryRecord) == 16, "HistoryRecord is an on-flash format");

constexpr uint8_t kLogUptimeStamp = 0x80;  // OR'ed into LogRecord::level

struct __attribute__((packed)) LogRecord {
  uint32_t ts;
  uint16_t code;
  uint8_t level;
  uint8_t module;
  int32_t a;
  int32_t b;
};
static_assert(sizeof(LogRecord) == 16, "LogRecord is an on-flash format");

class History {
 public:
  bool begin(TimeKeeper &time);
  void add(uint8_t module, uint8_t pct, int16_t raw, uint16_t batteryMv, uint16_t wateredSec,
           uint8_t flags);
  void clear() { store_.clear(); }
  const RingStore &store() const { return store_; }

 private:
  RingStore store_;
  TimeKeeper *time_ = nullptr;
};

class EventLog {
 public:
  bool begin(TimeKeeper &time);
  void add(Event event, LogLevel level, uint8_t module = kNoModule, int32_t a = 0, int32_t b = 0);
  void clear() { store_.clear(); }
  const RingStore &store() const { return store_; }

 private:
  RingStore store_;
  TimeKeeper *time_ = nullptr;
};

}  // namespace wat
