#include "ring_store.h"

#include <LittleFS.h>

namespace wat {

namespace {
constexpr uint16_t kVersion = 1;
}  // namespace

File RingStore::openRead() const { return LittleFS.open(path_, "r"); }

bool RingStore::writeHeader(File &f) {
  const Header h{magic_, kVersion, recordSize_, capacity_, head_, count_};
  return f.seek(0) && f.write(reinterpret_cast<const uint8_t *>(&h), sizeof(h)) == sizeof(h);
}

bool RingStore::createEmpty() {
  head_ = 0;
  count_ = 0;
  File f = LittleFS.open(path_, "w");
  if (!f) {
    return false;
  }
  const bool ok = writeHeader(f);
  f.close();
  return ok;
}

bool RingStore::begin(const char *path, uint32_t magic, uint16_t recordSize, uint32_t capacity) {
  path_ = path;
  magic_ = magic;
  recordSize_ = recordSize;
  capacity_ = capacity;

  File f = openRead();
  if (f) {
    Header h{};
    const bool readOk = f.read(reinterpret_cast<uint8_t *>(&h), sizeof(h)) == sizeof(h);
    const size_t size = f.size();
    f.close();
    // A file with a different layout is from another firmware version; its
    // records cannot be reinterpreted, so it is started over.
    if (readOk && h.magic == magic && h.version == kVersion && h.recordSize == recordSize &&
        h.capacity == capacity && h.head < capacity && h.count <= capacity) {
      const uint32_t slotsOnDisk = (size - sizeof(Header)) / recordSize;
      if (slotsOnDisk >= (h.count < capacity ? h.head : capacity)) {
        head_ = h.head;
        count_ = h.count;
        return true;
      }
    }
    Serial.printf("[ring] %s has an unexpected layout, starting over\n", path);
  }
  return createEmpty();
}

bool RingStore::append(const void *record) {
  File f = LittleFS.open(path_, "r+");
  if (!f) {
    return false;
  }
  bool ok = f.seek(slotOffset(head_)) &&
            f.write(static_cast<const uint8_t *>(record), recordSize_) == recordSize_;
  if (ok) {
    head_ = (head_ + 1) % capacity_;
    if (count_ < capacity_) {
      ++count_;
    }
    ok = writeHeader(f);
  }
  f.close();
  return ok;
}

void RingStore::clear() { createEmpty(); }

}  // namespace wat
