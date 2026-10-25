// CY-11a: the vendored reader's sample-size table (BaseBuffer<uint16_t>, injected with setSampleSizesBuffer) backed by the file.
// The vendor's onStsz resizes the table, writes every entry and later reads them back once, in order (currentSampleSize() caches the
// last index, so a repeat of the same index never reaches read()). In file mode this class swallows the writes (the guard has already
// validated every raw entry in the streaming scan) and serves read()/peek() from a kM4aWindowBytes (1,024 B = 256 raw entries) window
// that is refilled from a seekable source at the recorded stsz data offset. Without a source it is the old resident table.
#pragma once
#include <algorithm>
#include <cstring>
#include "m4a_caps.h"
#include "AudioTools/CoreAudio/Buffers.h"

namespace cydpod {
enum class M4aSourceResult : uint8_t { ok, readError, shortRead, seekError, badEntry };

// Positional read of the file the reader is being fed from. offset is the same stream offset the guard counts (0 = first byte fed to
// M4aGuardedReader::write). A short read must return shortRead and a failed seek seekError. If the implementation shares the
// streaming handle it must restore that handle's position, also on failure. The caller keeps the source alive until the next begin().
struct M4aSeekableSource {
  virtual ~M4aSeekableSource() = default;
  virtual M4aSourceResult readAt(uint64_t offset, uint8_t* dst, size_t size) = 0;
};

class M4aCappedSizes : public audio_tools::SingleBuffer<uint16_t> {
 public:
  bool overflow = false;
  M4aSourceResult error = M4aSourceResult::ok;  // first source failure; sticky until reset()
  void source(M4aSeekableSource* s) { src = s; }
  void tableOffset(uint64_t o) { offset = o; }  // stream offset of the first raw 32-bit entry
  void residentLimit(size_t n) { resLimit = n; }  // host comparisons only
  void hardLimit(size_t n) { hardCap = n; }  // arena mode: the resident table must fit the proven arena bound; residentLimit() cannot exceed it
  bool fileBacked() const { return src != nullptr; }

  bool resize(size_t n) override {
    if (n > (src ? kM4aMaxTableEntries : std::min(resLimit, hardCap))) { overflow = true; return false; }
    count = n; index = valid = 0;
    return src ? true : SingleBuffer::resize(n);
  }
  bool write(uint16_t v) override { return overflow || src ? true : SingleBuffer::write(v); }
  void reset() override {  // the vendor's begin() clears the table: forget the entries as well as the position
    if (!src) SingleBuffer::reset();
    count = index = valid = 0; error = M4aSourceResult::ok;
  }
  bool peek(uint16_t& v) override {
    if (!src) return SingleBuffer::peek(v);
    v = 0;
    if (error != M4aSourceResult::ok || index >= count) return false;
    if (!valid || index < start || index >= start + valid) {
      valid = 0;
      size_t entries = std::min(kM4aWindowEntries, count - index);
      M4aSourceResult r = src->readAt(offset + 4ull * index, window, entries * 4);
      if (r != M4aSourceResult::ok) { error = r; return false; }
      start = index; valid = entries;
    }
    const uint8_t* p = window + (index - start) * 4;
    uint32_t raw = uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
    // The guard validated this entry while streaming; a changed or corrupt source must not reach the reader.
    if (!raw || raw > kM4aMaxFrameBytes - kM4aAdtsHeaderBytes) { error = M4aSourceResult::badEntry; valid = 0; return false; }
    v = uint16_t(raw); return true;
  }
  bool read(uint16_t& v) override {
    if (!src) return SingleBuffer::read(v);
    if (!peek(v)) return false;
    ++index; return true;
  }
  int available() override { return src ? int(count - index) : SingleBuffer::available(); }
  int availableForWrite() override { return src ? 0 : SingleBuffer::availableForWrite(); }
  uint16_t* address() override { return src ? nullptr : SingleBuffer::address(); }
  size_t size() override { return src ? count : SingleBuffer::size(); }

 private:
  M4aSeekableSource* src = nullptr;
  uint64_t offset = 0;
  size_t hardCap = SIZE_MAX, resLimit = kM4aResidentEntries, count = 0, index = 0, start = 0, valid = 0;
  uint8_t window[kM4aWindowBytes];
};
}  // namespace cydpod
