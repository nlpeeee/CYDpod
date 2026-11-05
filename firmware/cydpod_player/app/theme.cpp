#include "theme.h"
#include "settings.h"

// RGB565, chosen on the panel rather than a monitor. Field order is theme.h's. All three tables are
// const, so they stay in flash; the only RAM is Settings::colorTheme.
//
// Light is the Classic Touch palette exactly as v24 shipped it; the last three fields repeat the
// colours the mini-player line and the pressed tiles used before they became roles.
//
// Black and High Contrast start from the CY-87 illustrative tables and were then checked against
// the pairs the production screens really draw (docs/research/cy91-theme-selector.md):
//  - text and secondary text >= 4.5:1 on bg, band and the raised dialog face; disabled >= 3:1;
//  - ink on active, pressed and accent faces >= 4.5:1 (Black's accent is the one blue that clears
//    4.5:1 against both white ink and the black background);
//  - borders, bars and the mini-player line >= 3:1 (line track on band >= 2:1);
//  - status colours >= 4.5:1 on bg, so they are brighter than Light's.
// High Contrast's band is a 48-grey, not black, so key and tile faces stay visible; its pressed face
// is white with black ink, its active/accent yellow with black ink.
static const Palette PALETTES[THEME_COUNT] = {
  { // Light
    0xFFFF, 0xD6BA, 0xF79E,
    0x1082, 0x5ACB, 0x9CD3,
    0x8C71, 0xE71C,
    0xE71C, 0x1082, 0x1B59, 0xFFFF, 0x3C5C,
    0x1B59, 0xFFFF,
    0xD6DB, 0x3C5B,
    0x0480, 0xBA80, 0xC000,
    0x1082, 0x8C71, 0x1B59,
  },
  { // Black
    0x0000, 0x2104, 0x31A7,
    0xFFFF, 0xB597, 0x738E,
    0x7BCF, 0x31A7,
    0x31A7, 0xFFFF, 0x1BBB, 0xFFFF, 0x2272,
    0x1BBB, 0xFFFF,
    0x4A49, 0x6DBF,
    0x3EAC, 0xFDE5, 0xFB0B,
    0xFFFF, 0x5ACB, 0x6DBF,
  },
  { // High Contrast
    0x0000, 0x3186, 0x0000,
    0xFFFF, 0xD69A, 0x8C71,
    0xFFFF, 0x630C,
    0x0000, 0xFFFF, 0xFF00, 0x0000, 0xFFFF,
    0xFF00, 0x0000,
    0x630C, 0xFF00,
    0x57EF, 0xFD80, 0xFB6C,
    0x0000, 0x6B6D, 0xFF00,
  },
};

const char* themeName(uint8_t id) {
  static const char* const names[THEME_COUNT] = {"Light", "Black", "High Contrast"};
  return names[themeSanitize(id)];
}

const Palette& theme() { return PALETTES[themeSanitize(g_settings.colorTheme)]; }
