#pragma once
#include <stddef.h>
#include <stdint.h>

// Factory CST820.cpp/.h: maxpill/JC2432W328 @9712396, factory capacitive demo.
// Burst 0x01..0x06: gesture, finger count, Xhi, Xlo, Yhi, Ylo.
// Count zero is authoritative even when release coordinates wrap to 0xfff.
// Gesture IDs are diagnostic only: the shared CY-62 classifier owns gestures.
namespace cst820 {
enum class State : uint8_t { Invalid, Released, Contact };
struct Report { State state = State::Invalid; uint16_t x = 0, y = 0; uint8_t gesture = 0; };
inline Report decode(const uint8_t* bytes, size_t length) {
  Report r;
  if (!bytes || length != 6) return r;
  r.gesture = bytes[0];
  if (bytes[1] == 0) { r.state = State::Released; return r; }
  if (bytes[1] != 1) return r;  // single-contact controller; corrupt count
  r.x = ((bytes[2] & 15) << 8) | bytes[3];
  r.y = ((bytes[4] & 15) << 8) | bytes[5];
  if (r.x >= 240 || r.y >= 320) return r; // no clamping glitches into a target
  r.state = State::Contact;
  return r;
}
inline void screenPoint(int px, int py, uint8_t rotation, int& sx, int& sy) {
  switch (rotation & 3) {
    case 0: sx = px; sy = py; break;
    case 1: sx = py; sy = 239 - px; break;
    case 2: sx = 239 - px; sy = 319 - py; break;
    default: sx = 319 - py; sy = px; break;
  }
}
} // namespace cst820
