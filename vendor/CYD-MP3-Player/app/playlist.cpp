// Player image only (firmware.h).
#ifndef CYD_UPLOADER

#include "playlist.h"
#include "storage.h"
#include "audio.h"
#include "settings.h"
#include <SD.h>
#include <esp_rom_crc.h>
#include <vector>
#include <algorithm>

static const char* SOURCES_PATH = "/.sys/sources.txt";
static const char* FAV_PATH     = "/.sys/fav.txt";
static const char* FAV_TMP      = "/.sys/fav.tmp";
static const char* M3U_DIR      = "/playlists";
static const int   FIXED        = 2;          // All, Favourites
static const int   MAX_FAV      = 500;        // 2 KB of CRCs at the most
static const int   MAX_M3U      = 1000;       // entries read from one playlist

static uint16_t* queue = nullptr;             // library indices, list order
static int       queueCap = 0;
static int       sourceCount = FIXED;
static int       current = 0;
static uint32_t  namesCrc = 0;
static std::vector<uint32_t> favs;            // sorted path keys

// Case-blind: FAT is, and a playlist typed on a PC often is not exact.
static uint32_t pathKey(const String& path) {
  String low = path;
  low.toLowerCase();
  return esp_rom_crc32_le(0, (const uint8_t*)low.c_str(), low.length());
}

static bool isFav(uint32_t key) { return std::binary_search(favs.begin(), favs.end(), key); }

// ---------------------------------------------------------------------------
// Sources file: "kind \t name \t path" per line, folders first
// ---------------------------------------------------------------------------
static bool readSource(int i, SourceKind& kind, String& name, String& path) {
  if (i < FIXED || i >= sourceCount) return false;
  File f = SD.open(SOURCES_PATH);
  if (!f) return false;
  String line;
  for (int n = 0; n <= i - FIXED && f.available(); n++) line = f.readStringUntil('\n');
  f.close();
  const int a = line.indexOf('\t'), b = a < 0 ? -1 : line.indexOf('\t', a + 1);
  if (b < 0) return false;
  kind = (SourceKind)line.substring(0, a).toInt();
  name = line.substring(a + 1, b);
  path = line.substring(b + 1);
  return true;
}

// Boot-time folder collection, shared with the storage_forEach callback.
static std::vector<String>* folders = nullptr;

static void collectFolders(int, const String& path, const String&) {
  // "/music/A/B/song.mp3" -> "A/B" and "A"
  if (!path.startsWith("/music/")) return;
  const String rel = path.substring(7);
  for (int s = rel.indexOf('/'); s > 0; s = rel.indexOf('/', s + 1)) {
    const String dir = rel.substring(0, s);
    if (std::find(folders->begin(), folders->end(), dir) == folders->end())
      folders->push_back(dir);
  }
}

static bool isPlaylistFile(const String& name) {
  String low = name;
  low.toLowerCase();
  return name[0] != '.' && (low.endsWith(".m3u") || low.endsWith(".m3u8"));
}

static String baseName(const String& path) {
  String n = path.substring(path.lastIndexOf('/') + 1);
  const int dot = n.lastIndexOf('.');
  return dot > 0 ? n.substring(0, dot) : n;
}

static void loadFavourites() {
  favs.clear();
  File f = SD.open(FAV_PATH);
  if (!f) return;
  while (f.available() && (int)favs.size() < MAX_FAV) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length()) favs.push_back(pathKey(line));
  }
  f.close();
  std::sort(favs.begin(), favs.end());
  favs.erase(std::unique(favs.begin(), favs.end()), favs.end());
  favs.shrink_to_fit();
}

// List every folder and playlist into the sources file.
static void scanSources() {
  std::vector<String> dirs;
  folders = &dirs;
  storage_forEach(collectFolders);
  folders = nullptr;
  std::sort(dirs.begin(), dirs.end());

  std::vector<String> lists;
  File d = SD.open(M3U_DIR);
  if (d && d.isDirectory()) {
    for (File f = d.openNextFile(); f; f = d.openNextFile()) {
      if (!f.isDirectory() && isPlaylistFile(f.name())) lists.push_back(String(M3U_DIR) + "/" + f.name());
      f.close();
    }
  }
  if (d) d.close();
  std::sort(lists.begin(), lists.end());

  SD.remove(SOURCES_PATH);
  File out = SD.open(SOURCES_PATH, FILE_WRITE);
  namesCrc = 0;
  sourceCount = FIXED;
  if (out) {
    auto put = [&](SourceKind kind, const String& name, const String& path) {
      out.printf("%u\t%s\t%s\n", kind, name.c_str(), path.c_str());
      namesCrc = esp_rom_crc32_le(namesCrc, (const uint8_t*)name.c_str(), name.length());
      sourceCount++;
    };
    for (const String& dir : dirs) put(SRC_FOLDER, dir, "/music/" + dir);
    for (const String& p : lists) put(SRC_M3U, baseName(p), p);
    out.close();
  }
  Serial.printf("playlist: %u folders, %u playlists, %u favourites\n", (unsigned)dirs.size(),
                (unsigned)lists.size(), (unsigned)favs.size());
}

void playlist_begin() {
  queueCap = max(1, storage_trackCount());
  queue = (uint16_t*)malloc(queueCap * sizeof(uint16_t));
  loadFavourites();
  scanSources();
}

// ---------------------------------------------------------------------------
// Building a queue
// ---------------------------------------------------------------------------
static int    built = 0;
static String folderPrefix;

static void addFavourite(int i, const String& path, const String&) {
  if (built < queueCap && isFav(pathKey(path))) queue[built++] = i;
}

static void addFolder(int i, const String& path, const String&) {
  if (built < queueCap && path.startsWith(folderPrefix)) queue[built++] = i;
}

// "/a/./b/../c" -> "/a/c"
static String normalise(const String& path) {
  std::vector<String> parts;
  int start = 0;
  while (start <= (int)path.length()) {
    int s = path.indexOf('/', start);
    if (s < 0) s = path.length();
    const String part = path.substring(start, s);
    if (part == "..") { if (!parts.empty()) parts.pop_back(); }
    else if (part.length() && part != ".") parts.push_back(part);
    start = s + 1;
  }
  String out;
  for (const String& p : parts) out += "/" + p;
  return out;
}

// Where an entry points on the card. PCs write every flavour: "song.mp3",
// "Artist\song.mp3", "../music/Artist/song.mp3", "D:\Music\Artist\song.mp3".
static String resolveEntry(String e, const String& listDir) {
  e.replace('\\', '/');
  if (e.length() > 1 && e[1] == ':') e = e.substring(2);        // drive letter
  String low = e;
  low.toLowerCase();
  if (e.startsWith("/")) {
    const int m = low.indexOf("/music/");
    return normalise(m > 0 ? e.substring(m) : e);
  }
  const String near = normalise(listDir + "/" + e);
  String nearLow = near;
  nearLow.toLowerCase();
  return nearLow.startsWith("/music/") ? near : normalise("/music/" + e);
}

static uint32_t* m3uKeys = nullptr;
static int       m3uCount = 0;

static void matchM3u(int i, const String& path, const String&) {
  const uint32_t k = pathKey(path);
  for (int p = 0; p < m3uCount; p++)
    if (m3uKeys[p] == k) queue[p] = i;
}

static int buildM3u(const String& listPath) {
  File f = SD.open(listPath);
  if (!f) return 0;
  const int cap = min(queueCap, MAX_M3U);
  m3uKeys = (uint32_t*)malloc(cap * sizeof(uint32_t));
  if (!m3uKeys) { f.close(); return 0; }
  const String dir = listPath.substring(0, listPath.lastIndexOf('/'));
  m3uCount = 0;
  while (f.available() && m3uCount < cap) {
    String line = f.readStringUntil('\n');
    if (line.startsWith("\xEF\xBB\xBF")) line = line.substring(3);   // UTF-8 BOM
    line.trim();
    if (line.isEmpty() || line[0] == '#' || line.indexOf("://") >= 0) continue;
    m3uKeys[m3uCount++] = pathKey(resolveEntry(line, dir));
  }
  f.close();
  for (int p = 0; p < m3uCount; p++) queue[p] = 0xFFFF;
  storage_forEach(matchM3u);
  free(m3uKeys);
  m3uKeys = nullptr;
  // Squeeze out the entries that matched nothing, keeping the playlist's order.
  int n = 0;
  for (int p = 0; p < m3uCount; p++)
    if (queue[p] != 0xFFFF) queue[n++] = queue[p];
  return n;
}

// Fill `queue` for source i. Returns its length; for All, the track count
// with `*identity` set (no table needed).
static int build(int i, bool* identity) {
  *identity = false;
  if (i == 0) { *identity = true; return storage_trackCount(); }
  if (!queue) return 0;
  built = 0;
  if (i == 1) {
    if (!favs.empty()) storage_forEach(addFavourite);
    return built;
  }
  SourceKind kind;
  String name, path;
  if (!readSource(i, kind, name, path)) return 0;
  if (kind == SRC_FOLDER) {
    folderPrefix = path + "/";
    storage_forEach(addFolder);
    return built;
  }
  return buildM3u(path);
}

static uint32_t keyOf(int i) {
  SourceKind kind;
  String name, path;
  return readSource(i, kind, name, path) ? pathKey(path) : 0;
}

static void remember(int i) {
  current = i;
  g_settings.srcKind = playlist_kind(i);
  g_settings.srcKey = keyOf(i);
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------
int        playlist_count()   { return sourceCount; }
int        playlist_current() { return current; }

SourceKind playlist_kind(int i) {
  if (i <= 0) return SRC_ALL;
  if (i == 1) return SRC_FAVOURITES;
  SourceKind kind;
  String name, path;
  return readSource(i, kind, name, path) ? kind : SRC_ALL;
}

String playlist_name(int i) {
  SourceKind kind;
  String name, path;
  return readSource(i, kind, name, path) ? name : String();
}

void playlist_restore(int resumeTrack) {
  int found = 0;
  if (g_settings.srcKind == SRC_FAVOURITES) found = 1;
  else if (g_settings.srcKind != SRC_ALL) {
    for (int i = FIXED; i < sourceCount; i++)
      if (playlist_kind(i) == g_settings.srcKind && keyOf(i) == g_settings.srcKey) { found = i; break; }
  }
  bool identity;
  int n = found ? build(found, &identity) : 0;
  if (n == 0) {                                  // gone, or nothing in it now
    found = 0;
    n = build(0, &identity);
  }
  remember(found);
  if (found) audio_setQueue(queue, n, resumeTrack, true);
  Serial.printf("playlist: source %d (%u), %d tracks\n", found, g_settings.srcKind, n);
}

bool playlist_select(int i) {
  if (i < 0 || i >= sourceCount) return false;
  bool identity;
  const int n = build(i, &identity);
  if (n == 0) {
    // The table may hold half of the failed attempt: put the current one back.
    build(current, &identity);
    return false;
  }
  remember(i);
  audio_setQueue(identity ? nullptr : queue, n, audio_currentTrack(), true);
  settings_save();
  return true;
}

bool playlist_isFavourite(int track) {
  return !favs.empty() && isFav(pathKey(storage_trackPath(track)));
}

bool playlist_favouritesFull() { return (int)favs.size() >= MAX_FAV; }

bool playlist_toggleFavourite(int track) {
  const String path = storage_trackPath(track);
  if (path.isEmpty()) return false;
  const uint32_t key = pathKey(path);
  const bool was = isFav(key);
  if (was) {
    favs.erase(std::lower_bound(favs.begin(), favs.end(), key));
    // Rewrite the file without it.
    SD.remove(FAV_TMP);
    File in = SD.open(FAV_PATH), out = SD.open(FAV_TMP, FILE_WRITE);
    if (in && out) {
      while (in.available()) {
        String line = in.readStringUntil('\n');
        line.trim();
        if (line.length() && pathKey(line) != key) out.println(line);
      }
    }
    if (in) in.close();
    if (out) {
      out.close();
      SD.remove(FAV_PATH);
      SD.rename(FAV_TMP, FAV_PATH);
    }
  } else {
    if (playlist_favouritesFull()) return false;
    favs.insert(std::upper_bound(favs.begin(), favs.end(), key), key);
    File out = SD.open(FAV_PATH, FILE_APPEND);
    if (out) { out.println(path); out.close(); }
  }

  // Playing from Favourites: the list follows, without cutting the song off.
  if (current == 1) {
    bool identity;
    const int n = build(1, &identity);
    if (n == 0) playlist_select(0);
    else audio_setQueue(queue, n, audio_currentTrack(), false);
  }
  return !was;
}

void playlist_forEachName(void (*fn)(const char* text)) {
  File f = SD.open(SOURCES_PATH);
  if (!f) return;
  while (f.available()) {
    const String line = f.readStringUntil('\n');
    const int a = line.indexOf('\t'), b = a < 0 ? -1 : line.indexOf('\t', a + 1);
    if (b > a) fn(line.substring(a + 1, b).c_str());
  }
  f.close();
}

uint32_t playlist_namesCrc() { return namesCrc; }

#endif  // !CYD_UPLOADER
