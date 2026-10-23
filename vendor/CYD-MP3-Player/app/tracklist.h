// ===========================================================================
// tracklist.h -- a page of the song list, shared by Library and the List skin
// ===========================================================================
// Rows are read from the library index as they are drawn and titles come from
// the pre-rendered strips, so a page costs a few SD reads and no lasting RAM.
#pragma once

#include "geometry.h"

// Rows are positions in the list being played (audio_queue*), so `top` is a
// position, while hits and topFor speak in library track indices.
//
// Draw the rows of `region` starting at position `top`; the playing track is lit.
void tracklist_draw(const Rect& region, int rowH, int top);
void tracklist_drawRow(const Rect& region, int rowH, int top, int row);

// Library track index under (x, y), or -1.
int  tracklist_hit(const Rect& region, int rowH, int top, int x, int y);

int  tracklist_rows(const Rect& region, int rowH);
// A `top` that puts `track` on the page, clamped to the list.
int  tracklist_topFor(const Rect& region, int rowH, int track);
int  tracklist_clampTop(const Rect& region, int rowH, int top);
