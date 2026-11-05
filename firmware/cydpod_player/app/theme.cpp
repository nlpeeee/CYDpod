#include "theme.h"

// RGB565, chosen on the panel rather than a monitor. This is the Classic Touch palette, the
// player's default and, since CY-81, only theme; the eight dark and coloured palettes that Setup
// used to cycle are gone. Field order is theme.h's.
static const Palette PALETTE = {
  0xFFFF, 0xD6BA, 0xF79E,
  0x1082, 0x5ACB, 0x9CD3,
  0x8C71, 0xE71C,
  0xE71C, 0x1082, 0x1B59, 0xFFFF, 0x3C5C,
  0x1B59, 0xFFFF,
  0xD6DB, 0x3C5B,
  0x0480, 0xBA80, 0xC000,
};

const Palette& theme() { return PALETTE; }
