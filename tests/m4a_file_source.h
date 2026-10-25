// Host M4aSeekableSource implementations for the file-backed sample-size table tests.
#pragma once
#include <cstdio>
#include <vector>
#include "../lib/cydpod_m4a/m4a_file_sizes.h"

namespace cydpod {
// Reads through the same FILE* the test streams from, restoring its position (what the firmware does with its single SD handle).
struct FileSource : M4aSeekableSource {
  FILE* f; size_t reads = 0, bytes = 0, largest = 0;
  explicit FileSource(FILE* file) : f(file) {}
  M4aSourceResult readAt(uint64_t off, uint8_t* dst, size_t n) override {
    ++reads; bytes += n; largest = n > largest ? n : largest;
    off_t saved = ftello(f);
    if (saved < 0) return M4aSourceResult::seekError;
    M4aSourceResult r = M4aSourceResult::ok;
    if (fseeko(f, (off_t)off, SEEK_SET)) r = M4aSourceResult::seekError;
    else {
      size_t got = std::fread(dst, 1, n, f);
      if (got != n) r = std::ferror(f) ? M4aSourceResult::readError : M4aSourceResult::shortRead;
    }
    if (fseeko(f, saved, SEEK_SET) && r == M4aSourceResult::ok) r = M4aSourceResult::seekError;
    return r;
  }
};
// Memory-backed source with optional fault injection (fail the Nth call with a given result, or truncate the data).
struct MemSource : M4aSeekableSource {
  const std::vector<uint8_t>* d; size_t reads = 0, failAt = SIZE_MAX; M4aSourceResult failWith = M4aSourceResult::readError;
  explicit MemSource(const std::vector<uint8_t>& v) : d(&v) {}
  M4aSourceResult readAt(uint64_t off, uint8_t* dst, size_t n) override {
    if (reads++ == failAt) { std::memset(dst, 0xEE, n); return failWith; }
    if (off > d->size() || n > d->size() - off) return M4aSourceResult::shortRead;
    std::memcpy(dst, d->data() + off, n); return M4aSourceResult::ok;
  }
};
}  // namespace cydpod
