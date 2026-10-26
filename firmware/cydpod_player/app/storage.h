// ===========================================================================
// storage.h -- SD card: mount, fixed folders, the music library index
// ===========================================================================
//   /music   M4A AAC-LC files, subfolders allowed (two levels deep)
//   /bg      background GIFs, bg1.gif, bg2.gif, ...
//   /playlists  .m3u playlists (playlist.h)
//   /.sys    files the firmware owns: library index, title strips, frames
//
// Names starting with '.' are skipped everywhere: a Mac writes "._name.mp3"
// next to every real file.
//
// Rebuildable, versioned catalogue at /.sys/library-{a,b}.cat. Fixed on-card
// records and sorted ID/group views avoid a whole-library RAM offset/name table.
// See catalog.h for Artist/Album/Song browsing and library_core.h for the schema.
//
// All track lookups are safe from any task (the decode task and the UI both
// use them).
#pragma once

#include <Arduino.h>
#include <SD.h>

// Rebuild progress: a phase name plus done/total (total > 0: "done / total", total < 0:
// done is a percentage, total == 0: a bare count).
typedef void (*StorageProgressFn)(const char* phase, int done, int total);

bool storage_mount();                              // card + folders only; false if no card
bool storage_begin(StorageProgressFn progress);   // mount, then scan and index /music

int      storage_trackCount();
uint32_t storage_libraryCrc();         // changes whenever the track list does

String storage_trackPath(int i);
// Embedded M4A title, or file name without extension when missing.
String storage_trackLabel(int i);

// Read every entry in order with one pass over the index (boot-time jobs).
typedef void (*StorageEachFn)(int i, const String& path, const String& label);
void storage_forEach(StorageEachFn fn);
