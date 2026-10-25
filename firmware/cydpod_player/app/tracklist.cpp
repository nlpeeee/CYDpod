// Player image only (firmware.h).
#ifndef CYD_UPLOADER

#include "tracklist.h"
#include "display.h"
#include "theme.h"
#include "ui.h"
#include "audio.h"
#include "storage.h"
#include "titles.h"
#include "playlist.h"

int tracklist_rows(const Rect& region, int rowH) { return max(1, region.h / rowH); }

int tracklist_clampTop(const Rect& region, int rowH, int top) {
  return constrain(top, 0, max(0, audio_queueLen() - tracklist_rows(region, rowH)));
}

int tracklist_topFor(const Rect& region, int rowH, int track) {
  const int pos = max(0, audio_queuePos(track));
  return tracklist_clampTop(region, rowH, pos - tracklist_rows(region, rowH) / 2 + 1);
}

void tracklist_drawRow(const Rect& region, int rowH, int top, int row) {
  const Palette& p = theme();
  const int pos = top + row;
  const int track = audio_queueTrack(pos);
  const Rect r = { region.x, region.y + row * rowH, region.w, rowH };
  const bool playing = track >= 0 && track == audio_currentTrack();
  const uint16_t bg = ui_rowSurface(r, row, playing);
  if (track < 0) return;

  const uint16_t ink = playing ? p.onControlActive : p.text;
  ui_text(String(pos + 1), r.x + 30, r.cy(), MR_DATUM, playing ? ink : p.dim, bg, 0,
          TYPE_CAPTION);
  const bool fav = playlist_isFavourite(track);
  ui_text(storage_trackLabel(track), r.x + 38, r.cy(), ML_DATUM, ink, bg,
          r.w - 44 - (fav ? ICON_SM + SP_S : 0));
  if (fav)
    ui_icon(ICON_STAR, r.x + r.w - SP_S - ICON_SM / 2, r.cy(), ICON_SM,
            playing ? ink : p.accent, bg);
}

void tracklist_draw(const Rect& region, int rowH, int top) {
  for (int i = 0; i < tracklist_rows(region, rowH); i++) tracklist_drawRow(region, rowH, top, i);
  // Whatever is left under the last whole row.
  const int used = tracklist_rows(region, rowH) * rowH;
  if (used < region.h)
    tft.fillRect(region.x, region.y + used, region.w, region.h - used, theme().bg);
}

int tracklist_hit(const Rect& region, int rowH, int top, int x, int y) {
  if (!region.contains(x, y)) return -1;
  const int row = (y - region.y) / rowH;
  if (row >= tracklist_rows(region, rowH)) return -1;
  return audio_queueTrack(top + row);
}

#endif  // !CYD_UPLOADER
