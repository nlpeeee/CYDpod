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
// Keep the governing 20 KiB internal-heap floor and a 4 KiB contiguous reserve
// when staging a queue during playback. Failure leaves the live queue intact.
inline bool catalog_canAllocate(size_t bytes) {
  return ESP.getFreeHeap() >= bytes + 20u*1024u + 512u && ESP.getMaxAllocHeap() >= bytes + 4096u;
}
bool storage_catalogReady();
bool storage_catalogStale();
bool storage_catalogRebuilt();
bool storage_refreshNextBoot();
int storage_browseCount(CatalogView view);
bool storage_browseEntry(CatalogView view,int row,CatalogEntry& entry);
int storage_browseTracks(CatalogView view,uint16_t* out,int capacity);
bool storage_trackInfo(int track,cydlib::Record& out);
// Title and artist only: lets row painting avoid a 712 B Record on the loop-task stack.
bool storage_trackSummary(int track,String& title,String& artist);
int storage_findTrack(const String& path);
void storage_forEachText(void (*fn)(const char*));
