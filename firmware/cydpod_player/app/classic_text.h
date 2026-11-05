#pragma once
// Classic Touch text: bitmap faces drawn at the size they were designed for.
//
// The Now Playing and mini-player labels used to nearest-neighbour sample the 16 px font 2
// bitmap down to 8..22 px, which drops source rows and columns (stems, counters, the gap
// between neighbouring glyphs). Both faces below are the Adafruit GFX FreeSans bitmaps that
// ship with the pinned TFT_eSPI library (Fonts/GFXFF), drawn 1:1 with no scaling, from flash,
// with no heap, no cache and no file access. ASCII only; other text goes to the UTF-8 strips.
#include <stddef.h>
#include <stdint.h>
namespace cydtext {
enum class Face : uint8_t { Title, TitleCompact, Label }; // FreeSansBold 12 pt, FreeSansBold 9 pt, FreeSans 9 pt
constexpr int HAlignLeft=0,HAlignCentre=1,HAlignRight=2;
bool ascii(const char* s);
// Height of a capital: the vertical reference for centring, so text sits on its optical middle.
int capHeight(Face face);
// Sum of glyph advances; control and non-ASCII bytes measure as a space.
int width(Face face,const char* s);
// Copies s to out and, if it is wider than maxW, drops whole glyphs and appends "...".
// Returns the width of out; out is empty when not even the ellipsis fits.
int fit(Face face,const char* s,char* out,size_t capacity,int maxW);
// Draws the glyph runs of s (already fitted) with the cap-height centre at cy, transparently.
// hAlign: 0 x is the left edge, 1 centre, 2 right edge. Returns the drawn width.
int draw(Face face,const char* s,int x,int cy,int hAlign,uint16_t fg);
}
