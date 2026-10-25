// Player image only (firmware.h).
#ifndef CYD_UPLOADER

// ===========================================================================
// screen_library.cpp -- the song list, shuffle and repeat
// ===========================================================================
//   [< ★ Favourites 13 v   ][shuffle][repeat]
//   |  1  傳說 - 林子祥                ★ | ^ |
//   |  2  別人的歌                       |   |
//   |  ...the playing one lit            | v |
//
// Sized from the screen, so it works in either orientation: 6 rows landscape,
// 8 portrait. Opened by tapping the song title on the player. Shows the list
// being played (playlist.h); tap its name to pick another, hold a song to
// star or unstar it.

#include "ui.h"
#include "display.h"
#include "theme.h"
#include "audio.h"
#include "storage.h"
#include "settings.h"
#include "tracklist.h"
#include "playlist.h"

static const int ROW_H   = UI_ROW_H;
static const int ARROW_W = UI_PAGER_W;

static Rect listRect()    { return { 0, UI_HEADER_H, tft.width() - ARROW_W, tft.height() - UI_HEADER_H }; }
static Rect shuffleRect() { return { tft.width() - 98, 0, 49, UI_HEADER_H }; }
static Rect repeatRect()  { return { tft.width() - 49, 0, 49, UI_HEADER_H }; }
// The list's name: tap to choose another. Starts where ui_header's title does.
static Rect sourceRect()  { return { 44, 0, tft.width() - 98 - 44, UI_HEADER_H }; }
static Rect upRect() {
  const Rect l = listRect();
  return { l.w, l.y, ARROW_W, l.h / 2 };
}
static Rect downRect() {
  const Rect l = listRect();
  return { l.w, l.y + l.h / 2, ARROW_W, l.h - l.h / 2 };
}

static int top = 0;
static int shownTrack = -1;

// Under the finger: 0 shuffle, 1 repeat, 2 up, 3 down, 4 the list's name.
static int lit = -1;

static void drawHeaderButtons() {
  const uint16_t band = theme().band;
  ui_iconToggle(shuffleRect(), ICON_SHUFFLE, audio_shuffle(), band, lit == 0);
  const uint8_t rep = audio_repeat();
  ui_iconToggle(repeatRect(), rep == REPEAT_ONE ? ICON_REPEAT_ONE : ICON_REPEAT,
                rep != REPEAT_OFF, band, lit == 1);
}

// Icon, name and length of the list being played, and a mark saying it opens.
static void drawSource(bool pressed) {
  const Palette& p = theme();
  const Rect r = sourceRect();
  const uint16_t bg = pressed ? p.pressed : p.band;
  tft.fillRect(r.x, r.y, r.w, r.h, bg);
  const int src = playlist_current();
  const SourceKind kind = playlist_kind(src);
  int x = r.x;
  if (kind != SRC_ALL) {
    const Icon icon = kind == SRC_FAVOURITES ? ICON_STAR : kind == SRC_FOLDER ? ICON_FOLDER : ICON_LIST;
    ui_icon(icon, x + ICON_SM / 2, r.cy(), ICON_SM, p.accent, bg);
    x += ICON_SM + SP_S;
  }
  const String count = String(audio_queueLen());
  const int markX = r.x + r.w - SP_M;
  const int room = max(0, markX - SP_M - x - SP_M - tft.textWidth(count, ui_font(TYPE_BODY)));
  const int w = kind == SRC_ALL        ? ui_label(T_LIBRARY, x, r.cy(), ML_DATUM, p.text, bg, room)
              : kind == SRC_FAVOURITES ? ui_label(T_FAVOURITES, x, r.cy(), ML_DATUM, p.text, bg, room)
              : ui_text(playlist_name(src), x, r.cy(), ML_DATUM, p.text, bg, room);
  ui_text(count, x + w + SP_M, r.cy(), ML_DATUM, p.dim, bg);
  ui_icon(ICON_DOWN, markX, r.cy(), 10, p.dim, bg);
}

static void enter() {
  lit = -1;
  tft.fillScreen(theme().bg);
  ui_header(T_COUNT);                    // band and back arrow; the name is ours
  drawSource(false);
  drawHeaderButtons();
  top = tracklist_topFor(listRect(), ROW_H, audio_currentTrack());
  shownTrack = audio_currentTrack();
  tracklist_draw(listRect(), ROW_H, top);
  ui_pager(upRect(), downRect());
}

static void tick(uint32_t) {
  // The playing track moved (end of song): move the highlight with it.
  if (audio_currentTrack() != shownTrack) {
    const Rect l = listRect();
    const int rows = tracklist_rows(l, ROW_H);
    // Rows are list positions, not track numbers.
    const int old = audio_queuePos(shownTrack);
    shownTrack = audio_currentTrack();
    const int now = audio_queuePos(shownTrack);
    if (old >= top && old < top + rows) tracklist_drawRow(l, ROW_H, top, old - top);
    if (now >= top && now < top + rows) tracklist_drawRow(l, ROW_H, top, now - top);
  }
}

static void touch(TouchEvent ev, int x, int y) {
  if (ev != TOUCH_TAP && ev != TOUCH_LONG_PRESS) return;
  if (UI_BACK_RECT.contains(x, y)) { ui_go(SCR_PLAYER); return; }
  if (sourceRect().contains(x, y)) { ui_go(SCR_SOURCES); return; }

  if (shuffleRect().contains(x, y)) {
    audio_setShuffle(!audio_shuffle());
    g_settings.shuffle = audio_shuffle();
    settings_save();
    drawHeaderButtons();
    return;
  }
  if (repeatRect().contains(x, y)) {
    // All -> One -> Off -> All
    const uint8_t next = audio_repeat() == REPEAT_ALL ? REPEAT_ONE
                       : audio_repeat() == REPEAT_ONE ? REPEAT_OFF : REPEAT_ALL;
    audio_setRepeat(next);
    g_settings.repeat = next;
    settings_save();
    drawHeaderButtons();
    return;
  }
  const Rect l = listRect();
  // A long press on the arrows jumps to the very start / end.
  if (upRect().contains(x, y) || downRect().contains(x, y)) {
    const bool up = upRect().contains(x, y);
    const int rows = tracklist_rows(l, ROW_H);
    const int before = top;
    if (ev == TOUCH_LONG_PRESS) top = up ? 0 : audio_queueLen();
    else top += up ? -(rows - 1) : (rows - 1);
    top = tracklist_clampTop(l, ROW_H, top);
    if (top != before) tracklist_draw(l, ROW_H, top);
    return;
  }
  const int track = tracklist_hit(l, ROW_H, top, x, y);
  if (track < 0) return;
  if (ev == TOUCH_TAP) {
    audio_playTrack(track);
    ui_go(SCR_PLAYER);
    return;
  }
  // Hold: star or unstar. In Favourites that can change the list itself.
  const bool full = playlist_favouritesFull() && !playlist_isFavourite(track);
  const bool inFavourites = playlist_current() == 1;
  const bool on = playlist_toggleFavourite(track);
  if (inFavourites) {
    top = tracklist_clampTop(l, ROW_H, top);
    drawSource(false);
    tracklist_draw(l, ROW_H, top);
  } else {
    tracklist_drawRow(l, ROW_H, top, (y - l.y) / ROW_H);
  }
  ui_toast(full ? T_FAV_FULL : on ? T_FAV_ADDED : T_FAV_REMOVED, 1500);
}

static bool press(PressPhase phase, int x, int y) {
  const int was = lit;
  if (phase == PRESS_DOWN) {
    lit = shuffleRect().contains(x, y) ? 0 : repeatRect().contains(x, y) ? 1
        : upRect().contains(x, y) ? 2 : downRect().contains(x, y) ? 3
        : sourceRect().contains(x, y) ? 4 : -1;
  } else if (phase == PRESS_UP && lit >= 0) {
    lit = -1;
  } else {
    return false;
  }
  // Redraw whatever changed state; with lit back at -1 this is the restore.
  if (lit == 4 || was == 4) { drawSource(lit == 4); return false; }
  if (phase == PRESS_DOWN && lit < 0) return false;
  drawHeaderButtons();
  ui_button(upRect(), ICON_UP, false, lit == 2);
  ui_button(downRect(), ICON_DOWN, false, lit == 3);
  return false;
}

const Screen SCREEN_LIBRARY = { enter, nullptr, tick, touch, press };

#endif  // !CYD_UPLOADER
