#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace cydpod {
struct Stereo { int16_t left, right; };
static_assert(sizeof(Stereo) == 4, "packed stereo PCM");

// Single producer/single consumer. Stop both endpoints before clear().
template <size_t N> class PcmRing {
 public:
  static_assert((N & (N - 1)) == 0 && N > 1, "power of two capacity");
  size_t size() const {
    uint32_t tail=tail_.load(std::memory_order_acquire);
    return std::min(size_t(head_.load(std::memory_order_acquire)-tail),N);
  }
  size_t push(const Stereo* src, size_t count) {
    const uint32_t head = head_.load(std::memory_order_relaxed);
    const uint32_t tail = tail_.load(std::memory_order_acquire);
    if (count > N - (head - tail)) count = N - (head - tail);
    for (size_t i = 0; i < count; ++i) data_[(head + i) & (N - 1)] = src[i];
    head_.store(head + count, std::memory_order_release);
    return count;
  }
  size_t pop(Stereo* dst, size_t count) {
    const uint32_t tail = tail_.load(std::memory_order_relaxed);
    const uint32_t head = head_.load(std::memory_order_acquire);
    if (count > head - tail) count = head - tail;
    for (size_t i = 0; i < count; ++i) dst[i] = data_[(tail + i) & (N - 1)];
    tail_.store(tail + count, std::memory_order_release);
    return count;
  }
  void clear() { head_.store(0); tail_.store(0); }
 private:
  Stereo data_[N]{};
  std::atomic<uint32_t> head_{0}, tail_{0};
};

// Header validation only; compressed AAC decoding remains Helix's job.
enum class HeaderResult { need_more, valid, unsupported, malformed };
inline HeaderResult adtsHeader(const uint8_t* p, size_t n, size_t& length) {
  if (n < 7) return HeaderResult::need_more;
  if (p[0] != 0xff || (p[1] & 0xf6) != 0xf0) return HeaderResult::malformed;
  length = ((p[3] & 3u) << 11) | (p[4] << 3) | (p[5] >> 5);
  if (length < ((p[1] & 1) ? 7u : 9u)) return HeaderResult::malformed;
  const unsigned profile = (p[2] >> 6) + 1;
  const unsigned rateIndex = (p[2] >> 2) & 15;
  const unsigned channels = ((p[2] & 1) << 2) | (p[3] >> 6);
  if (profile != 2 || rateIndex != 4 || channels != 2 || (p[6] & 3))
    return HeaderResult::unsupported;
  return HeaderResult::valid;
}
class GainRamp {
 public:
  void reset() { gain_=0; }
  void apply(Stereo* data,size_t count,unsigned percent,bool muted) {
    const unsigned desired=muted?0:std::min(percent,100u)*32768/100;
    for(size_t i=0;i<count;++i) {
      if(!desired) gain_=0;
      else if(gain_<desired) gain_=std::min(gain_+8,desired);
      else if(gain_>desired) gain_=std::max(gain_-std::min(gain_,8u),desired);
      data[i].left=static_cast<int16_t>(int32_t(data[i].left)*int32_t(gain_)/32768);
      data[i].right=static_cast<int16_t>(int32_t(data[i].right)*int32_t(gain_)/32768);
    }
  }
 private:
  unsigned gain_=0;
};
inline unsigned safeSavedLevel(unsigned saved, unsigned ceiling) {
  return saved > ceiling ? ceiling : saved;
}
// Discards an entire overlong line, including its valid-looking suffix.
template <size_t N> class LineBuffer {
 public:
  bool add(char c) {
    if (c == '\r') return false;
    if (c == '\n') {
      bool ready = !overflow_ && used_ > 0;
      bytes_[used_] = 0; used_ = 0; overflow_ = false; return ready;
    }
    if (used_ + 1 < N && !overflow_) bytes_[used_++] = c;
    else overflow_ = true;
    return false;
  }
  const char* text() const { return bytes_; }
 private:
  char bytes_[N]{}; size_t used_ = 0; bool overflow_ = false;
};
}
