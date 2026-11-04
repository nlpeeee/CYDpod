// Minimal Arduino shim so firmware/cydpod_player/app/settings.cpp builds on a host g++ (CY-71).
#pragma once
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
using std::max;
using std::min;
#define constrain(a, lo, hi) ((a) < (lo) ? (lo) : ((a) > (hi) ? (hi) : (a)))
struct String {
  std::string s;
  String() {}
  String(const char* c) : s(c ? c : "") {}
  const char* c_str() const { return s.c_str(); }
  unsigned length() const { return (unsigned)s.size(); }
  bool isEmpty() const { return s.empty(); }
};
struct SerialStub {
  void printf(const char* f, ...) {
    va_list a;
    va_start(a, f);
    vfprintf(stderr, f, a);
    va_end(a);
  }
  void println(const char* s = "") { fprintf(stderr, "%s\n", s); }
};
inline SerialStub Serial;
