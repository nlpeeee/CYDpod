// Generated from CY-87 option C by tools/cy87_icon_sets.py --export-c.
// Original icon shapes; TFT_eSPI face-span provenance retained in the generator.
#pragma once
#include <Arduino.h>
namespace cydicons {
struct Mask { uint32_t offset; uint8_t w,h,stride; int8_t ox,oy; uint8_t reserved[3]; };
struct Face { uint16_t offset; uint8_t w,h; };
static_assert(sizeof(Mask)==12, "flash mask metadata size");
extern const uint8_t coverage[];
extern const Mask masks[162];
extern const uint8_t spans[];
extern const Face faces[5];
const Mask* find(unsigned icon, int size, bool starOff=false);
const Face* face(int w,int h);
uint8_t alpha(const Mask& mask,int x,int y);
}
