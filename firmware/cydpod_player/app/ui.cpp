#include "ui.h"
#include "display.h"
#include "theme.h"
#include "titles.h"
#include "sleep_mode.h"

// ---------------------------------------------------------------------------
// Icons
// ---------------------------------------------------------------------------
static void speaker(int cx, int cy, int s, uint16_t fg) {
  // Box plus cone, left of centre so a +/- fits on the right.
  const int bx = cx - s / 2, bh = s / 3;
  tft.fillRect(bx, cy - bh / 2, s / 5, bh, fg);
  tft.fillTriangle(bx + s / 5 - 1, cy - bh / 2, bx + s / 2, cy - s / 2,
                   bx + s / 5 - 1, cy + bh / 2, fg);
  tft.fillTriangle(bx + s / 5 - 1, cy + bh / 2, bx + s / 2, cy - s / 2,
                   bx + s / 2, cy + s / 2, fg);
}

void ui_icon(Icon icon, int cx, int cy, int s, uint16_t fg, uint16_t bg) {
  const int h = s / 2;
  switch (icon) {
    case ICON_PLAY:
      tft.fillTriangle(cx - h * 2 / 3, cy - h, cx - h * 2 / 3, cy + h, cx + h, cy, fg);
      break;
    case ICON_PAUSE:
      tft.fillRect(cx - h * 3 / 4, cy - h, h / 2 + 1, s, fg);
      tft.fillRect(cx + h / 4, cy - h, h / 2 + 1, s, fg);
      break;
    case ICON_NEXT:
      // A separated stop bar and one filled triangle; exact mirror of Previous.
      tft.fillTriangle(cx-h,cy-h,cx-h,cy+h,cx+h/2,cy,fg);
      tft.fillRect(cx+h-2,cy-h,max(3,s/7),s,fg);
      break;
    case ICON_PREV:
      tft.fillTriangle(cx+h,cy-h,cx+h,cy+h,cx-h/2,cy,fg);
      tft.fillRect(cx-h-max(3,s/7)+3,cy-h,max(3,s/7),s,fg);
      break;
    case ICON_GEAR: {
      const int r = h * 3 / 4;
      for (int i = 0; i < 8; i++) {
        const float a = i * PI / 4;
        const int tx = cx + cos(a) * r, ty = cy + sin(a) * r;
        tft.fillCircle(tx, ty, max(2, s / 9), fg);
      }
      tft.fillCircle(cx, cy, r, fg);
      tft.fillCircle(cx, cy, r / 2, bg);
      break;
    }
    case ICON_SPEAKER: {
      // The cone the volume buttons use, with two strokes of sound instead of
      // a +/-: this one says where the audio goes, not louder or quieter.
      speaker(cx - s / 6, cy, s, fg);
      const int wx = cx + s / 4;
      tft.drawLine(wx, cy - s / 6, wx, cy + s / 6, fg);
      tft.drawLine(wx + 3, cy - s / 3, wx + 3, cy + s / 3, fg);
      break;
    }
    case ICON_HEADPHONES: {
      // Band over the top, a cup at each end.
      const int rr = h * 3 / 4 + 1, ay = cy + h / 6;
      int px = cx - rr, py = ay;
      for (int i = 1; i <= 12; ++i) {
        const float a = PI + i * PI / 12;
        const int x = cx + (int)(cos(a) * rr), y = ay + (int)(sin(a) * rr);
        tft.drawLine(px, py, x, y, fg);
        tft.drawLine(px, py + 1, x, y + 1, fg);
        px = x; py = y;
      }
      const int cw = max(4, s / 5), ch = max(5, h * 2 / 3);
      tft.fillRoundRect(cx - rr - cw / 2, ay - 1, cw, ch, 2, fg);
      tft.fillRoundRect(cx + rr - cw / 2, ay - 1, cw, ch, 2, fg);
      break;
    }
    case ICON_BLUETOOTH: {
      // The rune: a vertical stroke with two chevrons on the right.
      const int t = cy - h, b = cy + h, x0 = cx - h / 2, x1 = cx + h / 2;
      tft.drawLine(cx, t, cx, b, fg);
      tft.drawLine(cx, t, x1, cy - h / 2, fg);
      tft.drawLine(x1, cy - h / 2, x0, cy + h / 2, fg);
      tft.drawLine(cx, b, x1, cy + h / 2, fg);
      tft.drawLine(x1, cy + h / 2, x0, cy - h / 2, fg);
      break;
    }
    case ICON_BACK:
      tft.fillTriangle(cx - h, cy, cx, cy - h, cx, cy + h, fg);
      tft.fillRect(cx, cy - h / 3, h, h * 2 / 3, fg);
      break;
    case ICON_NOTE:
      tft.fillCircle(cx - h / 2, cy + h * 2 / 3, h / 3, fg);
      tft.fillCircle(cx + h * 2 / 3, cy + h / 2, h / 3, fg);
      tft.fillRect(cx - h / 2 + h / 3 - 2, cy - h, 3, h * 5 / 3, fg);
      tft.fillRect(cx + h * 2 / 3 + h / 3 - 2, cy - h * 7 / 6, 3, h * 5 / 3, fg);
      tft.fillTriangle(cx - h / 2 + h / 3 - 2, cy - h, cx + h, cy - h * 7 / 6 - 1,
                       cx + h, cy - h * 2 / 3, fg);
      tft.fillTriangle(cx - h / 2 + h / 3 - 2, cy - h, cx - h / 2 + h / 3 - 2, cy - h / 2,
                       cx + h, cy - h * 2 / 3, fg);
      break;
    case ICON_SHUFFLE: {
      // Two crossing paths, arrowheads on the right.
      const int l = cx - h, r = cx + h - 3, t = cy - h / 2, b = cy + h / 2;
      tft.drawLine(l, t, r, b, fg);   tft.drawLine(l, t + 1, r, b + 1, fg);
      tft.drawLine(l, b, r, t, fg);   tft.drawLine(l, b + 1, r, t + 1, fg);
      tft.fillTriangle(r + 3, t, r - 2, t - 4, r - 2, t + 5, fg);
      tft.fillTriangle(r + 3, b + 1, r - 2, b - 4, r - 2, b + 5, fg);
      break;
    }
    case ICON_REPEAT:
    case ICON_REPEAT_ONE: {
      const int w = s, hh = s * 2 / 3;
      tft.drawRoundRect(cx - w / 2, cy - hh / 2, w, hh, hh / 3, fg);
      tft.drawRoundRect(cx - w / 2 + 1, cy - hh / 2 + 1, w - 2, hh - 2, hh / 3, fg);
      // Break the top edge with an arrowhead pointing right.
      tft.fillRect(cx - 2, cy - hh / 2 - 1, 5, 4, bg);
      tft.fillTriangle(cx + 4, cy - hh / 2 + 1, cx - 2, cy - hh / 2 - 4, cx - 2, cy - hh / 2 + 6, fg);
      if (icon == ICON_REPEAT_ONE) {
        tft.setTextColor(fg, bg);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("1", cx, cy + 1, 1);
      }
      break;
    }
    case ICON_UP:
      tft.fillTriangle(cx, cy - h / 2, cx - h, cy + h / 2, cx + h, cy + h / 2, fg);
      break;
    case ICON_DOWN:
      tft.fillTriangle(cx, cy + h / 2, cx - h, cy - h / 2, cx + h, cy - h / 2, fg);
      break;
    case ICON_CHEVRON:
      // A thin arrowhead: "this row opens something".
      tft.fillTriangle(cx - h / 3, cy - h, cx + h / 2, cy, cx - h / 3, cy + h, fg);
      tft.fillTriangle(cx - h / 3 - 3, cy - h, cx + h / 2 - 3, cy, cx - h / 3 - 3, cy + h, bg);
      break;
    case ICON_STAR: {
      // Five tips and the pentagon between them, a triangle each.
      int ox[5], oy[5], ix[5], iy[5];
      for (int i = 0; i < 5; i++) {
        const float a = -PI / 2 + i * 2 * PI / 5, b = a + PI / 5;
        ox[i] = cx + lroundf(cosf(a) * h);
        oy[i] = cy + lroundf(sinf(a) * h) + h / 8;
        ix[i] = cx + lroundf(cosf(b) * h * 0.42f);
        iy[i] = cy + lroundf(sinf(b) * h * 0.42f) + h / 8;
      }
      for (int i = 0; i < 5; i++) {
        const int j = (i + 4) % 5;              // the inner point before this tip
        tft.fillTriangle(ox[i], oy[i], ix[i], iy[i], ix[j], iy[j], fg);
        tft.fillTriangle(cx, cy + h / 8, ix[i], iy[i], ix[j], iy[j], fg);
      }
      break;
    }
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Faces
// ---------------------------------------------------------------------------
// The one control shape (style.h): a raised pill with a hairline outline and a
// 1 px highlight along its top edge. Every draw clears the touch rectangle to
// the surface colour first, so a control redrawn leaves nothing of the old
// shape behind.

static uint16_t faceInk(bool active, bool pressed) {
  const Palette& p = theme();
  return (active || pressed) ? p.onControlActive : p.btnText;
}

// Paint the face and hand back the colour that landed, so the caller can draw
// an icon or a word against it.
static uint16_t ui_face(const Rect& r, bool active, bool pressed, uint16_t surface) {
  const Palette& p = theme();
  const uint16_t face = pressed ? p.pressed : active ? p.btnActive : p.surfaceRaised;

  tft.fillRect(r.x, r.y, r.w, r.h, surface);

  const int x = r.x + STYLE_INSET, y = r.y + STYLE_INSET;
  const int w = r.w - STYLE_INSET * 2, h = r.h - STYLE_INSET * 2;
  if (w <= 0 || h <= 0) return face;

  tft.fillRoundRect(x, y, w, h, STYLE_RADIUS, face);
  for (int i = 0; i < STYLE_BORDER; i++)
    tft.drawRoundRect(x + i, y + i, w - i * 2, h - i * 2, STYLE_RADIUS, p.outline);
  if (!active && !pressed)
    tft.drawFastHLine(x + STYLE_RADIUS / 2, y, w - STYLE_RADIUS, p.outline);
  return face;
}

void ui_button(const Rect& r, Icon icon, bool active, bool pressed) {
  const uint16_t face = ui_face(r, active, pressed, theme().bg);
  ui_icon(icon, r.cx(), r.cy(), min(r.w, r.h) * 3 / 5, faceInk(active, pressed), face);
}

void ui_textButton(const Rect& r, Txt label, bool primary, bool pressed, int32_t surface) {
  const uint16_t face = ui_face(r, primary, pressed, surface < 0 ? theme().bg : (uint16_t)surface);
  ui_label(label, r.cx(), r.cy(), MC_DATUM, faceInk(primary, pressed), face, r.w - SP_XL);
}

// A toggle that lives on a header band rather than on the screen: off, it is
// nothing but its icon; on, the face appears under it.
void ui_iconToggle(const Rect& r, Icon icon, bool on, uint16_t surface, bool pressed) {
  const Palette& p = theme();
  tft.fillRect(r.x, r.y, r.w, r.h, surface);
  const uint16_t face = pressed ? p.pressed : on ? p.btnActive : surface;
  tft.fillRoundRect(r.x + 3, r.y + 3, r.w - 6, r.h - 6, STYLE_RADIUS, face);
  if (on)
    tft.drawRect(r.x + 3, r.y + 3, r.w - 6, r.h - 6, p.outline);
  ui_icon(icon, r.cx(), r.cy(), ICON_MD, (on || pressed) ? p.onControlActive : p.dim, face);
}


// Chrome face: font 2 (16 px, hand-drawn bitmaps) for body and caption alike. The 6x8 font 1 it
// replaces for captions is 7 px tall and ragged beside it; captions are told apart by colour.
// Song titles in other scripts come from strips (titles.h).
static uint8_t ui_font(TypeRole) { return 2; }

static bool isAscii(const String& s) {
  for (unsigned i = 0; i < s.length(); i++)
    if ((uint8_t)s[i] >= 0x80) return false;
  return true;
}

// Longest prefix of whole characters (never half of a UTF-8 sequence) that fits with "..." in maxW.
// Empty when not even the ellipsis fits.
String ui_ellipsize(const String& text, uint8_t font, int maxW) {
  String head = text;
  while (head.length()) {
    if (tft.textWidth(head + "...", font) <= maxW) return head + "...";
    unsigned n = head.length() - 1;
    while (n && ((uint8_t)head[n] & 0xC0) == 0x80) n--; // step back to the lead byte
    head = head.substring(0, n);
    while (head.length() && head[head.length() - 1] == ' ') head = head.substring(0, head.length() - 1);
  }
  return tft.textWidth(String("..."), font) <= maxW ? String("...") : String();
}

int ui_text(const String& text, int x, int y, uint8_t datum, uint16_t fg, uint16_t bg, int maxW,
            TypeRole role) {
  const int hAlign = datum % 3;              // 0 left, 1 centre, 2 right
  const int vAlign = datum / 3;              // 0 top, 1 middle (baseline datums unused)
  const uint8_t f = ui_font(role);
  if (!isAscii(text) && titles_load(text)) {
    int w = titles_width();
    if (maxW > 0) w = min(w, maxW);
    const int left = x - (hAlign == 1 ? w / 2 : hAlign == 2 ? w : 0);
    const int top = y - (vAlign == 1 ? TITLE_STRIP_H / 2 : 0);
    titles_draw({ left, top, w, TITLE_STRIP_H }, 0, 0, fg, bg);
    return w;
  }
  titles_release(); // ASCII/fallback draws must not retain an unrelated CJK strip
  tft.setTextColor(fg, bg);
  tft.setTextDatum(datum);
  const int fh = tft.fontHeight(f);
  if (maxW > 0 && tft.textWidth(text, f) > maxW) {
    // Whole glyphs plus "...": a clipped half glyph reads as a rendering fault.
    const String shown = ui_ellipsize(text, f, maxW);
    const int w = tft.textWidth(shown, f);
    const int left = x - (hAlign == 1 ? w / 2 : hAlign == 2 ? w : 0);
    const int top = y - (vAlign == 1 ? fh / 2 : 0);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(shown, left, top, f);
    return w;
  }
  tft.drawString(text, x, y, f);
  return tft.textWidth(text, f);
}

int ui_label(Txt id, int x, int y, uint8_t datum, uint16_t fg, uint16_t bg, int maxW,
             TypeRole role) {
  const String s = tr(id);
  // Chinese without its strip would draw as nothing: say it in English instead.
  if (!isAscii(s) && !titles_load(s)) return ui_text(tr_en(id), x, y, datum, fg, bg, maxW, role);
  return ui_text(s, x, y, datum, fg, bg, maxW, role);
}

void ui_header(Txt title, const String& suffix) {
  const Palette& p = theme();
  tft.fillRect(0, 0, tft.width(), UI_HEADER_H, p.band);
  ui_icon(ICON_BACK, 20, UI_HEADER_H / 2, 14, p.text, p.band);
  const int w = ui_label(title, 44, UI_HEADER_H / 2, ML_DATUM, p.text, p.band);
  if (suffix.length()) ui_text(suffix, 44 + w + 8, UI_HEADER_H / 2, ML_DATUM, p.dim, p.band);
}

String ui_time(uint32_t ms, bool known) {
  if (!known) return "--:--";
  const uint32_t s = ms / 1000;
  char b[16];
  if (s >= 3600) snprintf(b, sizeof(b), "%u:%02u:%02u", s / 3600, (s / 60) % 60, s % 60);
  else           snprintf(b, sizeof(b), "%u:%02u", s / 60, s % 60);
  return b;
}

#ifndef CYD_UPLOADER
#include "classic_screen.h"

// ---------------------------------------------------------------------------
// Screen switcher
// ---------------------------------------------------------------------------
static const Screen* SCREENS[SCR_COUNT] = { &SCREEN_BLUETOOTH, &SCREEN_CLASSIC };
static ScreenId current = SCR_CLASSIC;
static bool     entered = false;

enum Overlay : uint8_t { OV_NONE, OV_TOAST, OV_CONFIRM };
static Overlay  overlay = OV_NONE;

// The finger, as the press hooks see it (Screen::press).
static bool pressDown     = false;
static bool pressClaimed  = false;   // a hook took it as a drag: swallow its event
static bool pressOrphaned = false;   // the screen changed under it: ignore it
static int  pressX = 0, pressY = 0;

void ui_go(ScreenId id) {
  if (entered && SCREENS[current]->leave) SCREENS[current]->leave();
  // A screen can change from a tick (Bluetooth moves on once the speaker
  // pairs) with a finger still down. That press belonged to the old screen;
  // the new one never saw it land, so it hears nothing of it.
  if (pressDown) pressOrphaned = true;
  overlay = OV_NONE;                     // the new screen draws over it anyway
  current = id;
  entered = true;
  SCREENS[current]->enter();
}

ScreenId ui_current() { return current; }
void ui_redraw()      { SCREENS[current]->enter(); }

// ---------------------------------------------------------------------------
// Confirm dialog
// ---------------------------------------------------------------------------
static ConfirmFn confirmFn = nullptr;
static Txt       confirmOk = T_RESTART;
static Rect      cancelRect, okRect;

static void drawConfirmButtons(int pressed) {
  const uint16_t panel = theme().surfaceRaised;
  ui_textButton(cancelRect, T_CANCEL, false, pressed == 0, panel);
  ui_textButton(okRect, confirmOk, true, pressed == 1, panel);
}

void ui_confirm(Txt title, Txt line1, Txt line2, Txt okLabel, ConfirmFn onOk) {
  const Palette& p = theme();
  overlay   = OV_CONFIRM;
  confirmFn = onOk;
  confirmOk = okLabel;

  const int W = tft.width(), H = tft.height();
  const int w = min(W - SP_L * 2, 296), h = 150;
  const Rect r = { (W - w) / 2, (H - h) / 2, w, h };

  tft.fillRoundRect(r.x, r.y, r.w, r.h, STYLE_RADIUS, p.surfaceRaised);
  // A hairline: a panel must not melt into the screen behind.
  for (int i = 0; i < STYLE_BORDER; i++)
    tft.drawRoundRect(r.x + i, r.y + i, r.w - i * 2, r.h - i * 2, STYLE_RADIUS, p.outline);

  const int tx = r.x + SP_L, tw = r.w - SP_L * 2;
  ui_label(title, tx, r.y + 22, ML_DATUM, p.text, p.surfaceRaised, tw);
  if (line1 != T_COUNT) ui_label(line1, tx, r.y + 52, ML_DATUM, p.dim, p.surfaceRaised, tw);
  if (line2 != T_COUNT) ui_label(line2, tx, r.y + 74, ML_DATUM, p.dim, p.surfaceRaised, tw);

  const int bh = 44, by = r.y + r.h - SP_M - bh, bw = (r.w - SP_M * 3) / 2;
  cancelRect = { r.x + SP_M, by, bw, bh };
  okRect     = { r.x + SP_M * 2 + bw, by, bw, bh };
  drawConfirmButtons(-1);
}

static void closeOverlay() {
  overlay = OV_NONE;
  ui_redraw();
}

static void confirmTouch(TouchEvent ev, int x, int y) {
  if (ev != TOUCH_TAP) return;
  const bool ok = okRect.contains(x, y);
  if (!ok && !cancelRect.contains(x, y)) return;    // a stray tap is not an answer
  const ConfirmFn fn = ok ? confirmFn : nullptr;
  confirmFn = nullptr;
  overlay = OV_NONE;
  if (fn) fn();                          // may not return: the panel switch restarts
  ui_redraw();
}

static void confirmPress(PressPhase phase, int x, int y) {
  static int lit = -1;
  if (phase == PRESS_DOWN) {
    lit = cancelRect.contains(x, y) ? 0 : okRect.contains(x, y) ? 1 : -1;
    if (lit >= 0) drawConfirmButtons(lit);
  } else if (phase == PRESS_UP && lit >= 0) {
    lit = -1;
    drawConfirmButtons(-1);
  }
}

// ---------------------------------------------------------------------------
// Toast
// ---------------------------------------------------------------------------
static uint32_t toastUntil = 0;

void ui_toast(Txt text, uint32_t ms) { ui_toast(tr(text), ms); }

void ui_toast(const char* text, uint32_t ms) {
  const Palette& p = theme();
  const int W = tft.width(), H = tft.height();
  const Rect r = { SP_XL, H - SP_XL - 34, W - SP_XL * 2, 34 };
  // Inverted -- text colour as the face -- so it stands off any row under it.
  tft.fillRoundRect(r.x, r.y, r.w, r.h, STYLE_RADIUS, p.text);
  ui_text(String(text), r.cx(), r.cy(), MC_DATUM, p.bg, p.text, r.w - SP_XL);
  overlay = OV_TOAST;
  toastUntil = millis() + ms;
}

// ---------------------------------------------------------------------------
// The finger, between landing and release
// ---------------------------------------------------------------------------
static bool dispatchPress(PressPhase phase, int x, int y) {
  if (pressOrphaned || overlay == OV_TOAST) return false;
  if (overlay == OV_CONFIRM) { confirmPress(phase, x, y); return false; }
  const Screen* s = SCREENS[current];
  return s->press ? s->press(phase, x, y) : false;
}

// Bring the press hooks up to date with the finger. Returns true when a press
// has just ended that must not produce an event (a drag, or an orphan).
static bool syncPress() {
  if (touch_isDown()) {
    int x, y;
    if (!pressDown) {
      pressDown = true;
      pressClaimed = pressOrphaned = false;
      touch_position(x, y);              // where it landed: what a tap acts on
      pressX = x;
      pressY = y;
      pressClaimed |= dispatchPress(PRESS_DOWN, x, y);
    } else {
      touch_livePosition(x, y);
      if (x != pressX || y != pressY) {
        pressX = x;
        pressY = y;
        pressClaimed |= dispatchPress(PRESS_MOVE, x, y);
      }
    }
    return false;
  }
  if (!pressDown) return false;
  pressDown = false;
  dispatchPress(PRESS_UP, pressX, pressY);
  const bool swallow = pressClaimed || pressOrphaned;
  pressClaimed = pressOrphaned = false;
  return swallow;
}

bool ui_pressClaimed() { return pressDown && pressClaimed; }

static uint32_t uiNow=0;
void ui_tick(uint32_t now) {
  uiNow=now;sleep_tick(now,touch_isDown());
  if (!entered) return;
  // touch_poll() ran first this loop. If it saw a release that made an event,
  // ui_touch() has already synced; here we catch releases that made none.
  if(!sleep_touchBlocked())syncPress();
  if(sleep_screenOff())return;
  if (overlay == OV_TOAST && (int32_t)(now - toastUntil) >= 0) closeOverlay();
  if (overlay != OV_NONE) return;        // nothing may paint over an overlay
  if (SCREENS[current]->tick) SCREENS[current]->tick(now);
}

void ui_touch(TouchEvent ev, int x, int y) {
  sleep_tick(uiNow,touch_isDown());
  if (!entered||sleep_touchBlocked()) return;
  // Restore whatever the press lit BEFORE the event runs: the event may move
  // to another screen, which must not then be drawn over.
  if (syncPress()) return;
  if (overlay == OV_TOAST)   { closeOverlay(); return; }   // a tap dismisses it
  if (overlay == OV_CONFIRM) { confirmTouch(ev, x, y); return; }
  if (SCREENS[current]->touch) SCREENS[current]->touch(ev, x, y);
}

#endif  // !CYD_UPLOADER

#ifdef CYDPOD_CATALOG_HOST
#include <sstream>
std::string classic_dialog_geometry(){
  std::ostringstream out;
  for(unsigned rotation:{0u,1u,3u}){tft.setRotation(rotation);ui_confirm(T_RESTART,T_COUNT,T_COUNT,T_RESTART,nullptr);out<<"dialog rotation="<<rotation<<" cancel="<<cancelRect.x<<','<<cancelRect.y<<','<<cancelRect.w<<','<<cancelRect.h<<" ok="<<okRect.x<<','<<okRect.y<<','<<okRect.w<<','<<okRect.h<<'\n';}
  overlay=OV_NONE;tft.setRotation(0);return out.str();
}
#endif
