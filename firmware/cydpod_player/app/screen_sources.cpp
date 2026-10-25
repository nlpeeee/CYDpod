// Player image only (firmware.h).
#ifndef CYD_UPLOADER

// ===========================================================================
// screen_sources.cpp -- choose what to play from
// ===========================================================================
//   [< Playlists                         ]
//   |  ♪  All songs                  | ^ |
//   |  ★  Favourites                 |   |
//   |  ▭  Cantopop                   |   |
//   |  ≡  Road trip                  | v |
//
// Opened from the list name in Library's header. The one playing from is lit;
// tapping another starts it (keeping the current song if it is in there) and
// goes back to Library to show it.

#include "ui.h"
#include "display.h"
#include "theme.h"
#include "playlist.h"

static const int ROW_H   = UI_ROW_H;
static const int ARROW_W = UI_PAGER_W;

static Rect listRect() { return { 0, UI_HEADER_H, tft.width() - ARROW_W, tft.height() - UI_HEADER_H }; }
static Rect upRect() {
  const Rect l = listRect();
  return { l.w, l.y, ARROW_W, l.h / 2 };
}
static Rect downRect() {
  const Rect l = listRect();
  return { l.w, l.y + l.h / 2, ARROW_W, l.h - l.h / 2 };
}

static int top = 0;
static int lit = -1;            // row under the finger (absolute index), or -2 up, -3 down

static int rows() { return max(1, listRect().h / ROW_H); }
static int clampTop(int t) { return constrain(t, 0, max(0, playlist_count() - rows())); }

static void drawRow(int row, bool pressed = false) {
  const Palette& p = theme();
  const Rect l = listRect();
  const Rect r = { l.x, l.y + row * ROW_H, l.w, ROW_H };
  const int i = top + row;
  const bool active = i == playlist_current();
  const uint16_t bg = ui_rowSurface(r, row, active, pressed);
  if (i >= playlist_count()) return;

  const uint16_t ink = (active || pressed) ? p.onControlActive : p.text;
  const SourceKind kind = playlist_kind(i);
  const Icon icon = kind == SRC_ALL ? ICON_NOTE : kind == SRC_FAVOURITES ? ICON_STAR
                  : kind == SRC_FOLDER ? ICON_FOLDER : ICON_LIST;
  ui_icon(icon, r.x + SP_L + ICON_SM / 2, r.cy(), ICON_SM, (active || pressed) ? ink : p.accent, bg);
  const int x = r.x + SP_L + ICON_SM + SP_M, room = r.x + r.w - SP_M - x;
  if (kind == SRC_ALL)             ui_label(T_ALL_SONGS, x, r.cy(), ML_DATUM, ink, bg, room);
  else if (kind == SRC_FAVOURITES) ui_label(T_FAVOURITES, x, r.cy(), ML_DATUM, ink, bg, room);
  else                             ui_text(playlist_name(i), x, r.cy(), ML_DATUM, ink, bg, room);
}

static void drawList() {
  for (int i = 0; i < rows(); i++) drawRow(i);
  const Rect l = listRect();
  const int used = rows() * ROW_H;
  if (used < l.h) tft.fillRect(l.x, l.y + used, l.w, l.h - used, theme().bg);
}

static void enter() {
  lit = -1;
  tft.fillScreen(theme().bg);
  ui_header(T_PLAYLISTS, String(playlist_count()));
  top = clampTop(playlist_current() - rows() / 2 + 1);
  drawList();
  ui_pager(upRect(), downRect());
}

static int rowAt(int x, int y) {
  const Rect l = listRect();
  if (!l.contains(x, y)) return -1;
  const int row = (y - l.y) / ROW_H;
  return (row < rows() && top + row < playlist_count()) ? row : -1;
}

static void touch(TouchEvent ev, int x, int y) {
  if (ev != TOUCH_TAP && ev != TOUCH_LONG_PRESS) return;
  if (UI_BACK_RECT.contains(x, y)) { ui_go(SCR_LIBRARY); return; }

  if (upRect().contains(x, y) || downRect().contains(x, y)) {
    const bool up = upRect().contains(x, y);
    const int before = top;
    if (ev == TOUCH_LONG_PRESS) top = up ? 0 : playlist_count();
    else top += up ? -(rows() - 1) : (rows() - 1);
    top = clampTop(top);
    if (top != before) drawList();
    return;
  }
  if (ev != TOUCH_TAP) return;
  const int row = rowAt(x, y);
  if (row < 0) return;
  if (top + row == playlist_current()) { ui_go(SCR_LIBRARY); return; }
  if (playlist_select(top + row)) ui_go(SCR_LIBRARY);
  else ui_toast(T_LIST_EMPTY);
}

static bool press(PressPhase phase, int x, int y) {
  if (phase == PRESS_DOWN) {
    const int row = rowAt(x, y);
    lit = row >= 0 ? row : upRect().contains(x, y) ? -2 : downRect().contains(x, y) ? -3 : -1;
    if (lit >= 0) drawRow(lit, true);
    else if (lit == -2) ui_button(upRect(), ICON_UP, false, true);
    else if (lit == -3) ui_button(downRect(), ICON_DOWN, false, true);
  } else if (phase == PRESS_UP && lit != -1) {
    if (lit >= 0) drawRow(lit);
    else ui_pager(upRect(), downRect());
    lit = -1;
  }
  return false;
}

const Screen SCREEN_SOURCES = { enter, nullptr, nullptr, touch, press };

#endif  // !CYD_UPLOADER
