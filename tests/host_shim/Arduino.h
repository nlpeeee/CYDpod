// Minimal Arduino shim so the unmodified AudioTools M4A demuxer builds on a host g++.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <algorithm>
#define PSTR(s) (s)
#define PROGMEM
inline void delay(unsigned long) {}
inline unsigned long millis() { return 0; }
class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* b, size_t n) { size_t i = 0; while (i < n) { write(b[i]); ++i; } return n; }
  virtual int availableForWrite() { return 1024; }
  virtual void flush() {}
  size_t print(const char* s) { return write((const uint8_t*)s, strlen(s)); }
  size_t println(const char* s = "") { print(s); return write('\n'); }
};
class Stream : public Print {
 public:
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
  virtual size_t readBytes(uint8_t*, size_t) { return 0; }
  size_t readBytes(char* b, size_t n) { return readBytes((uint8_t*)b, n); }
};
struct HostSerial : Print {
  size_t write(uint8_t c) override { return fputc(c, stderr) == EOF ? 0 : 1; }
};
inline HostSerial Serial;
