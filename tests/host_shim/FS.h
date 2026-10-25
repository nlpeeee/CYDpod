// Host stand-in for the Arduino FS.h `File` (the subset miniaac_adapter.h uses), over a stdio FILE*.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
class File {
 public:
  File() = default;
  explicit File(FILE* f) : f_(f) {}
  ~File() { close(); }
  File(const File&) = delete;
  File& operator=(const File&) = delete;
  explicit operator bool() const { return f_ != nullptr; }
  size_t size() { long here = ftell(f_); fseek(f_, 0, SEEK_END); long end = ftell(f_); fseek(f_, here, SEEK_SET); return size_t(end); }
  size_t position() { return size_t(ftell(f_)); }
  bool seek(uint32_t pos) { return fseek(f_, long(pos), SEEK_SET) == 0; }
  size_t read(uint8_t* dst, size_t n) { return fread(dst, 1, n, f_); }
  void close() { if (f_) fclose(f_); f_ = nullptr; }
 private:
  FILE* f_ = nullptr;
};
