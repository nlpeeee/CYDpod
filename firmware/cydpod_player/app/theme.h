// ===========================================================================
// theme.h -- the colour palette
// ===========================================================================
// Screens never use a colour literal; they ask theme(). There are three flash-resident palettes
// (theme.cpp): Light (Classic Touch, the default), Black and High Contrast. The choice is one
// byte, Settings::colorTheme (NVS key "colorTheme"), read on every theme() call.
//
// The roles below are named for what they mean, not what they look like, so a
// palette can invert without a screen knowing. Read the surfaces as a stack:
//
//   bg             surface 0 -- the screen itself
//   band           surface 1 -- a panel, the header, an alternate row
//   surfaceRaised  surface 2 -- a face that has to read as lifted off the panel
//
// The control shape (style.h) decides which of them a given control is painted with.
#pragma once

#include <Arduino.h>

struct Palette {
  // Surfaces, lowest first.
  uint16_t bg, band, surfaceRaised;

  // Text on those surfaces. `disabled` is for a control that is present but
  // cannot be used -- dimmer than `dim`, which is merely secondary.
  uint16_t text, dim, disabled;

  // Lines. `outline` is a border you are meant to see; `outlineSubtle` is a
  // divider you are not supposed to notice.
  uint16_t outline, outlineSubtle;

  // Controls. `pressed` is the face under a finger, between `btn` and
  // `btnActive` in weight, so a tap is acknowledged before it lands.
  uint16_t btn, btnText, btnActive, onControlActive, pressed;

  // The highlight colour, and what stays legible on top of it.
  uint16_t accent, onAccent;

  // Progress bar.
  uint16_t barBg, bar;

  // Status.
  uint16_t good, warn, bad;

  // Ink on a `pressed` face drawn outside a control (tiles, the Player header, the mini-player,
  // the output chip). Controls use onControlActive; Light keeps `text` here, as it always did.
  uint16_t onPressed;

  // The mini-player's one-pixel-row progress line: the unfilled part, then the elapsed part.
  uint16_t miniTrack, miniFill;
};

enum ColorTheme : uint8_t { THEME_LIGHT = 0, THEME_BLACK = 1, THEME_HIGH_CONTRAST = 2, THEME_COUNT };

// Absent, erased or out-of-range stored values select Light.
inline uint8_t themeSanitize(uint8_t id) { return id < THEME_COUNT ? id : uint8_t(THEME_LIGHT); }
inline uint8_t themeNext(uint8_t id) { return uint8_t((themeSanitize(id) + 1) % THEME_COUNT); }
const char* themeName(uint8_t id);

const Palette& theme();
