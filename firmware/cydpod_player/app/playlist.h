// ===========================================================================
// playlist.h -- what the player plays from: all songs, ★ favourites, a folder
//               or an .m3u playlist
// ===========================================================================
// A "source" turns into a queue: library track indices in list order, which
// audio.cpp plays in order or shuffled (audio_setQueue). The queue is one
// uint16_t per track, allocated once at boot before Bluetooth takes the heap.
//
//   0          All songs                 the whole library, no queue table
//   1          Favourites                /.sys/fav.txt, one path per line
//   2..        folders under /music      every folder that holds songs,
//                                        including its subfolders' songs
//   then       /playlists/*.m3u, *.m3u8  entries relative to /music, to the
//                                        playlist's own folder, or absolute
//
// Folders and playlists are listed once at boot in /.sys/sources.txt, so RAM
// holds none of their names. Favourites are kept by PATH, not index, so a
// re-index after new songs arrive does not shuffle them onto other songs; RAM
// keeps a CRC per favourite to answer "is this one starred?".
#pragma once

#include <Arduino.h>

enum SourceKind : uint8_t { SRC_ALL = 0, SRC_FAVOURITES = 1, SRC_FOLDER = 2, SRC_M3U = 3 };

// Boot, after storage_begin() and before titles_prepareAll() (which renders
// the names) and Bluetooth (which takes the heap the queue needs).
void playlist_begin();

// After audio_begin(): bring back the source saved in g_settings, keeping
// `resumeTrack` playing when it belongs to it. A source that has gone falls
// back to All songs.
void playlist_restore(int resumeTrack);

int        playlist_count();              // All + Favourites + folders + playlists
SourceKind playlist_kind(int i);
String     playlist_name(int i);          // "" for All and Favourites: use tr()
int        playlist_current();

// Play from source i. Keeps the current song going when it is in the new
// list, otherwise starts the list. False (and nothing changes) when the
// source holds nothing playable.
bool playlist_select(int i);

bool playlist_isFavourite(int track);
// Star or unstar. Returns the new state; false also when the list is full.
bool playlist_toggleFavourite(int track);
bool playlist_favouritesFull();

// Every folder and playlist name, for pre-rendering, and a checksum of them so
// a new playlist is noticed even when the library has not changed.
void     playlist_forEachName(void (*fn)(const char* text));
uint32_t playlist_namesCrc();
