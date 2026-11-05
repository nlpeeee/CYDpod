// ===========================================================================
// ui.h -- drawing helpers shared by every screen, and the screen switcher
// ===========================================================================
// Icons are drawn from primitives (triangles, rects, circles) rather than
// bitmaps: they scale to any button size, take the theme's colours for free,
// and cost no flash for a second set when stage 4 adds larger layouts.
#pragma once

#include <Arduino.h>
#include "geometry.h"
#include "tokens.h"
#include "theme.h"
#include "style.h"
#include "touch.h"
#include "i18n.h"

enum Icon : uint8_t {
  ICON_NONE, ICON_PREV, ICON_PLAY, ICON_PAUSE, ICON_NEXT,
  ICON_GEAR, ICON_BLUETOOTH, ICON_BACK, ICON_NOTE,
  ICON_SHUFFLE, ICON_REPEAT, ICON_REPEAT_ONE, ICON_UP, ICON_DOWN,
  ICON_CHEVRON, ICON_SPEAKER, ICON_STAR, ICON_HEADPHONES
};

// Which face a piece of chrome is set in: the body face, or the 8 px caption
// face. Song titles are deliberately absent: they come from 18 px strips so
// Chinese and English keep the same height.
enum TypeRole : uint8_t { TYPE_BODY, TYPE_CAPTION };

// `size` is the icon's nominal height in px.
void ui_icon(Icon icon, int cx, int cy, int size, uint16_t fg, uint16_t bg);

// A button, icon centred (style.h). `active` is a control that is on (playing,
// selected); `pressed` is one with a finger on it right now.
void ui_button(const Rect& r, Icon icon, bool active = false, bool pressed = false);

// A word in a button face instead of an icon.
// `surface` is what the button sits on: the screen, or a dialog's panel.
void ui_textButton(const Rect& r, Txt label, bool primary = false, bool pressed = false,
                   int32_t surface = -1);

// A borderless toggle sitting on a header band: the face appears only when on.
void ui_iconToggle(const Rect& r, Icon icon, bool on, uint16_t surface, bool pressed = false);


// Text in any script. Pure ASCII is drawn with the built-in font; anything
// else from its pre-rendered strip (titles.h), falling back to the built-in
// font (which drops non-ASCII) when no strip exists. `datum` is a TFT_eSPI
// datum (TL/TC/TR/ML/MC/MR). maxW > 0 clips. Returns the width drawn.
int ui_text(const String& text, int x, int y, uint8_t datum, uint16_t fg, uint16_t bg,
            int maxW = 0, TypeRole role = TYPE_BODY);

// A UI string in the current language -- English when no Chinese strip exists.
int ui_label(Txt id, int x, int y, uint8_t datum, uint16_t fg, uint16_t bg, int maxW = 0,
             TypeRole role = TYPE_BODY);

// Header bar with a back arrow at the left and a title. Used by the Bluetooth
// page and the uploader; the Classic pages draw their own.
static const int UI_HEADER_H = 30;
static const Rect UI_BACK_RECT = { 0, 0, 56, UI_HEADER_H };
void ui_header(Txt title, const String& suffix = String());

// "1:23", or "1:02:03" past an hour; "--:--" for unknown.
String ui_time(uint32_t ms, bool known = true);

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------
// Two screens: the Classic pages (every list, Now Playing, Search and Settings
// are modes of one screen, classic_screen.cpp) and the Bluetooth pairing page.
enum ScreenId : uint8_t { SCR_BLUETOOTH, SCR_CLASSIC, SCR_COUNT };

// What a finger is doing right now, for feedback while it is down. Gestures
// (TouchEvent) are classified on release; this is everything before that.
enum PressPhase : uint8_t { PRESS_DOWN, PRESS_MOVE, PRESS_UP };

struct Screen {
  void (*enter)();
  void (*leave)();
  void (*tick)(uint32_t now);
  void (*touch)(TouchEvent ev, int x, int y);
  // Optional. Light what is under the finger on DOWN, follow a drag on MOVE,
  // restore on UP -- which always arrives before the gesture's event, so a
  // screen the tap switches to is never drawn over. Return true to claim the
  // press as a drag: its tap or long-press is then not delivered.
  bool (*press)(PressPhase phase, int x, int y);
};

extern const Screen SCREEN_BLUETOOTH;
extern const Screen SCREEN_CLASSIC;

void     ui_go(ScreenId id);          // leave the current screen, enter this one
ScreenId ui_current();
void     ui_tick(uint32_t now);
void     ui_touch(TouchEvent ev, int x, int y);
void     ui_redraw();                 // re-enter the current screen (after calibration)
// True while the press under way has been taken as a drag. A drag can easily
// outlast the 4 s that makes a hold mean "recalibrate", and must not.
bool     ui_pressClaimed();

// ---------------------------------------------------------------------------
// Overlays
// ---------------------------------------------------------------------------
// There is no framebuffer to restore from, so an overlay ends by redrawing the
// screen under it whole. While one is up the screen's tick is paused (audio
// and Bluetooth are ticked from the loop and carry on), so nothing is painted
// over it, and the next tap goes to the overlay.

// A short message across the bottom of the screen. Goes by itself after `ms`,
// or at the next tap, which it swallows.
void ui_toast(Txt text, uint32_t ms = 2500);
void ui_toast(const char* text, uint32_t ms = 2500);

// A question with Cancel and an action button. `onOk` runs if the owner
// confirms; either way the screen underneath is redrawn.
typedef void (*ConfirmFn)();
void ui_confirm(Txt title, Txt line1, Txt line2, Txt okLabel, ConfirmFn onOk);
