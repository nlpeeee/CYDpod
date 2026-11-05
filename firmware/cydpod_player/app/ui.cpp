#include "ui.h"
#include "display.h"
#include "theme.h"
#include "titles.h"
#include "sleep_mode.h"

// Selected CY-87 C assets are memory-mapped flash on ESP32. No mutable asset cache.
#include "ui_icons.h"

namespace {
// Largest installed icon-control cell is the Player's 80 x 52 play target.
constexpr int IconRowPixels = 80;
// checkViewport clips in the driver's logical coordinate system, including a
// translated or screen-clipped viewport. setAddrWindow itself DOES NOT clip or
// add that origin, so do both explicitly before opening the single window.
bool iconClip(Rect& r){
  if(r.w<=0||r.h<=0||!tft.checkViewport(r.x,r.y,r.w,r.h))return false;
  while(!tft.checkViewport(r.x,r.y,1,r.h)){++r.x;--r.w;}
  while(!tft.checkViewport(r.x+r.w-1,r.y,1,r.h))--r.w;
  while(!tft.checkViewport(r.x,r.y,r.w,1)){++r.y;--r.h;}
  while(!tft.checkViewport(r.x,r.y+r.h-1,r.w,1))--r.h;
  return true;
}
// Integer RGB565 interpolation, rounded per channel; equal to CY-87 blend565.
uint16_t iconBlend(uint16_t fg,uint16_t bg,unsigned a){
  const unsigned b=15-a;
  return uint16_t((((((fg>>11)&31)*a+((bg>>11)&31)*b+7)/15)<<11)
    | (((((fg>>5)&63)*a+((bg>>5)&63)*b+7)/15)<<5)
    | (((fg&31)*a+(bg&31)*b+7)/15));
}
// All paint is resolved before a row goes to SPI. A standalone glyph uses its
// tight crop; a control streams its WHOLE original cell, including cleared
// corners, border, face, glyph and underline. No SPI primitive calls in the loop.
void iconStream(Rect cell,const cydicons::Mask& mask,int gx,int gy,uint16_t fg,
                uint16_t paper,uint16_t face,uint16_t edge,
                const cydicons::Face* shape,bool underline){
  Rect clip=cell;
  if(cell.w>IconRowPixels||!iconClip(clip))return;
  uint16_t row[IconRowPixels], blend[16]; // 160 + 32 bytes; bounded UI stack only
  for(unsigned a=0;a<16;++a)blend[a]=iconBlend(fg,face,a);
  tft.startWrite();
  tft.setAddrWindow(clip.x+tft.getOriginX(),clip.y+tft.getOriginY(),clip.w,clip.h);
  for(int y=clip.y;y<clip.y+clip.h;++y){
    const uint8_t* span=shape?cydicons::spans+shape->offset+4*(y-cell.y):nullptr;
    for(int col=0;col<clip.w;++col){
      const int x=clip.x+col,local=x-cell.x;
      uint16_t c=paper;
      if(span){
        if(local>=span[0]&&local<span[1])c=edge;
        if(local>=span[2]&&local<span[3])c=face;
      }
      const unsigned a=cydicons::alpha(mask,x-gx,y-gy);
      // Optical padding guarantees the glyph falls inside the flat face.
      if(a)c=blend[a];
      if(underline&&y==cell.y+cell.h-6&&x>=cell.cx()-3&&x<cell.cx()+3)c=fg;
      row[col]=c;
    }
    tft.pushColors(row,clip.w,true);
  }
  tft.endWrite();
}
}

void ui_icon(Icon icon,int cx,int cy,int size,uint16_t fg,uint16_t bg){
  const cydicons::Mask* mask=cydicons::find(icon,size);
  if(!mask)return;
  const int x=cx-size/2+mask->ox,y=cy-size/2+mask->oy;
  iconStream({x,y,mask->w,mask->h},*mask,x,y,fg,bg,bg,bg,nullptr,false);
}

// ---------------------------------------------------------------------------
// Faces
// ---------------------------------------------------------------------------
// Text buttons/dialogs retain their style.h raised face with a hairline outline and a
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

void ui_button(const Rect& r,Icon icon,bool active,bool pressed,int glyphSize){
  const Palette& p=theme();
  const cydicons::Mask* mask=cydicons::find(icon,glyphSize?glyphSize:min(r.w,r.h)*3/5);
  const cydicons::Face* shape=cydicons::face(r.w,r.h);
  if(!mask||!shape)return;
  const int size=glyphSize?glyphSize:min(r.w,r.h)*3/5;
  const uint16_t face=pressed?p.pressed:p.bg;
  const uint16_t ink=pressed?p.onControlActive:active?p.accent:p.text;
  iconStream(r,*mask,r.cx()-size/2+mask->ox,r.cy()-size/2+mask->oy,
             ink,p.bg,face,(active&&!pressed)?p.accent:p.outline,shape,active&&!pressed);
}

void ui_textButton(const Rect& r, Txt label, bool primary, bool pressed, int32_t surface) {
  const uint16_t face = ui_face(r, primary, pressed, surface < 0 ? theme().bg : (uint16_t)surface);
  ui_label(label, r.cx(), r.cy(), MC_DATUM, faceInk(primary, pressed), face, r.w - SP_XL);
}

// Favourite follows selected C: hollow off / solid on, filled active/pressed
// capsule on the header band. The whole 40 x 36 cell is one transfer.
void ui_iconToggle(const Rect& r,Icon icon,bool on,uint16_t surface,bool pressed){
  const Palette& p=theme();
  const cydicons::Mask* mask=cydicons::find(icon,ICON_MD,!on);
  if(!mask)return;
  const uint16_t face=pressed?p.pressed:on?p.btnActive:surface;
  iconStream(r,*mask,r.cx()-ICON_MD/2+mask->ox,r.cy()-ICON_MD/2+mask->oy,
             (on||pressed)?p.onControlActive:p.dim,surface,face,face,
             (on||pressed)?cydicons::face(r.w,r.h):nullptr,false);
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
