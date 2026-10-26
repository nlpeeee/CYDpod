#pragma once
#include <Arduino.h>
#include "library_core.h"
enum class CatalogKind : uint8_t { Artists, Albums, Songs, ArtistAlbums, ArtistSongs, AlbumSongs, ArtistAlbumSongs };
struct CatalogView { CatalogKind kind=CatalogKind::Songs; uint16_t group=0; };
struct CatalogEntry {
  String label, detail;
  CatalogView child;
  int track=-1;
  bool navigates=false;
};
// Admission floor for staging a queue or preview during playback: 12 KiB of free
// heap plus the staged bytes, and a contiguous block of the bytes plus 4 KiB.
// CY-37 measured 28.6 to 37.5 KB free while playing (lifetime minimum 24,540 B), so
// this admits staging in normal use and refuses only when the heap is really short.
// A refusal leaves the live queue and playback untouched.
inline bool catalog_canAllocate(size_t bytes) {
  return ESP.getFreeHeap() >= bytes + 12u*1024u && ESP.getMaxAllocHeap() >= bytes + 4096u;
}
// One serial line before and after each staging, so the real heap behaviour can be read next time.
inline void catalog_logStage(const char* what, const char* when, size_t bytes) {
  Serial.printf("stage: %s %s bytes %u free %u largest %u\n", what, when, (unsigned)bytes,
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
}
bool storage_catalogReady();
bool storage_catalogStale();
bool storage_catalogRebuilt();
int storage_skippedCount();  // files left out of the catalogue (path too long or over 2,000 tracks)
bool storage_refreshNextBoot();
int storage_browseCount(CatalogView view);
bool storage_browseEntry(CatalogView view,int row,CatalogEntry& entry);
int storage_browseTracks(CatalogView view,uint16_t* out,int capacity);
bool storage_trackInfo(int track,cydlib::Record& out);
// Title and artist only: lets row painting avoid a 712 B Record on the loop-task stack.
bool storage_trackSummary(int track,String& title,String& artist);
int storage_findTrack(const String& path);
void storage_forEachText(void (*fn)(const char*));

// Loop-task-only bounded search. At most eight 64 B keys per tick. A single
// search handle closes on completion/cancel/leave; restarting abandons the scan.
void storage_searchStart(const char* query);
void storage_searchTick();
void storage_searchCancel();
bool storage_searchBusy();
bool storage_searchBuilding();
bool storage_searchFailed();
int storage_searchProgress();
int storage_searchCount();
const uint16_t* storage_searchTracks();
