#include "records.h"

namespace wat {

namespace {
constexpr uint32_t kHistoryMagic = 0x57484931;  // "WHI1"
constexpr uint32_t kLogMagic = 0x574C4731;      // "WLG1"
// 4 modules x 2 records a day ~ 1.4 years; 64 KiB of the 640 KiB data partition.
constexpr uint32_t kHistoryCapacity = 4096;
constexpr uint32_t kLogCapacity = 1024;
}  // namespace

bool History::begin(TimeKeeper &time) {
  time_ = &time;
  return store_.begin("/history.bin", kHistoryMagic, sizeof(HistoryRecord), kHistoryCapacity);
}

void History::add(uint8_t module, uint8_t pct, int16_t raw, uint16_t batteryMv, uint16_t wateredSec,
                  uint8_t flags) {
  const TimeKeeper::Stamp s = time_->stamp();
  HistoryRecord r{};
  r.ts = s.value;
  r.module = module;
  r.moisturePct = pct;
  r.raw = raw;
  r.batteryMv = batteryMv;
  r.wateredSec = wateredSec;
  r.flags = static_cast<uint8_t>(flags | (s.valid ? 0 : kHistUptimeStamp));
  if (!store_.append(&r)) {
    Serial.println("[history] append failed");
  }
}

bool EventLog::begin(TimeKeeper &time) {
  time_ = &time;
  return store_.begin("/events.bin", kLogMagic, sizeof(LogRecord), kLogCapacity);
}

void EventLog::add(Event event, LogLevel level, uint8_t module, int32_t a, int32_t b) {
  const TimeKeeper::Stamp s = time_->stamp();
  LogRecord r{};
  r.ts = s.value;
  r.code = static_cast<uint16_t>(event);
  r.level = static_cast<uint8_t>(static_cast<uint8_t>(level) | (s.valid ? 0 : kLogUptimeStamp));
  r.module = module;
  r.a = a;
  r.b = b;
  Serial.printf("[event] code=%u level=%u module=%d a=%ld b=%ld\n", r.code,
                static_cast<unsigned>(level), module == kNoModule ? -1 : module,
                static_cast<long>(a), static_cast<long>(b));
  if (!store_.append(&r)) {
    Serial.println("[event] append failed");
  }
}

}  // namespace wat
