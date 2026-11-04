// Host stand-in for the ESP32 Preferences (NVS): one process-wide store, so a "reboot" in a
// test keeps what the previous "boot" saved.
#pragma once
#include <Arduino.h>
#include <map>
#include <vector>
class Preferences {
 public:
  static std::map<std::string, std::vector<uint8_t>>& store() {
    static std::map<std::string, std::vector<uint8_t>> s;
    return s;
  }
  static void erase() { store().clear(); }
  static uint8_t peek(const char* k) { return store().at(k).at(0); }
  static void poke(const char* k, uint8_t v) { store()[k] = std::vector<uint8_t>{v}; }
  inline static std::string failKey;
  inline static bool failOpen=false;
  bool begin(const char*, bool) { return !failOpen; }
  void end() {}
  bool isKey(const char* k) { return store().count(k) != 0; }
  bool remove(const char* k) { return failKey != k && store().erase(k) != 0; }
  template <class T> T get(const char* k, T d) {
    auto it = store().find(k);
    if (it == store().end() || it->second.size() != sizeof(T)) return d;
    T v;
    memcpy(&v, it->second.data(), sizeof(T));
    return v;
  }
  template <class T> size_t put(const char* k, T v) {
    if (failKey == k) return 0;
    std::vector<uint8_t> b(sizeof(T));
    memcpy(b.data(), &v, sizeof(T));
    store()[k] = b;
    return sizeof(T);
  }
  uint8_t getUChar(const char* k, uint8_t d = 0) { return get<uint8_t>(k, d); }
  uint16_t getUShort(const char* k, uint16_t d = 0) { return get<uint16_t>(k, d); }
  uint32_t getULong(const char* k, uint32_t d = 0) { return get<uint32_t>(k, d); }
  bool getBool(const char* k, bool d = false) { return get<uint8_t>(k, d) != 0; }
  size_t putUChar(const char* k, uint8_t v) { return put<uint8_t>(k, v); }
  size_t putUShort(const char* k, uint16_t v) { return put<uint16_t>(k, v); }
  size_t putULong(const char* k, uint32_t v) { return put<uint32_t>(k, v); }
  size_t putBool(const char* k, bool v) { return put<uint8_t>(k, v); }
  String getString(const char* k, String d = String()) {
    auto it = store().find(k);
    if (it == store().end()) return d;
    String s;
    s.s.assign(it->second.begin(), it->second.end());
    return s;
  }
  size_t putString(const char* k, const String& v) { if(failKey == k)return 0; store()[k] = std::vector<uint8_t>(v.s.begin(), v.s.end()); return v.length(); }
  size_t getBytesLength(const char* k) {
    auto it = store().find(k);
    return it == store().end() ? 0 : it->second.size();
  }
  size_t getBytes(const char* k, void* buf, size_t len) {
    auto it = store().find(k);
    if (it == store().end()) return 0;
    size_t n = std::min(len, it->second.size());
    memcpy(buf, it->second.data(), n);
    return n;
  }
  void putBytes(const char* k, const void* buf, size_t len) {
    store()[k] = std::vector<uint8_t>((const uint8_t*)buf, (const uint8_t*)buf + len);
  }
};
