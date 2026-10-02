#pragma once

#include <Arduino.h>
#include <FS.h>

namespace wat {

// Fixed-size circular log of POD records in one LittleFS file.
// Layout: Header, then up to `capacity` records. The file only ever grows by
// appending at its end until it is full, then slots are overwritten in place,
// so it never has holes.
//
// Writes happen a few times a day (measurements, watering, events), so the
// header rewrite on each append is not a wear concern for LittleFS.
class RingStore {
 public:
  bool begin(const char *path, uint32_t magic, uint16_t recordSize, uint32_t capacity);
  bool append(const void *record);
  void clear();

  uint32_t count() const { return count_; }
  uint32_t capacity() const { return capacity_; }

  // Calls fn(record) oldest -> newest. Stops early if fn returns false.
  template <typename F>
  void forEach(F fn) const {
    uint8_t buf[64];
    if (recordSize_ > sizeof(buf) || count_ == 0) {
      return;
    }
    File f = openRead();
    if (!f) {
      return;
    }
    const uint32_t first = (head_ + capacity_ - count_) % capacity_;
    for (uint32_t i = 0; i < count_; ++i) {
      const uint32_t slot = (first + i) % capacity_;
      if (!f.seek(slotOffset(slot)) || f.read(buf, recordSize_) != recordSize_) {
        break;
      }
      if (!fn(static_cast<const void *>(buf))) {
        break;
      }
    }
    f.close();
  }

 private:
  struct Header {
    uint32_t magic;
    uint16_t version;
    uint16_t recordSize;
    uint32_t capacity;
    uint32_t head;
    uint32_t count;
  };

  File openRead() const;
  bool writeHeader(File &f);
  bool createEmpty();
  uint32_t slotOffset(uint32_t slot) const { return sizeof(Header) + slot * recordSize_; }

  String path_;
  uint32_t magic_ = 0;
  uint16_t recordSize_ = 0;
  uint32_t capacity_ = 0;
  uint32_t head_ = 0;
  uint32_t count_ = 0;
};

}  // namespace wat
