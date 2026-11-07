// Player image only (firmware.h).
#ifndef CYD_UPLOADER

#include "playlist.h"
#include "storage.h"
#include "audio.h"
#include "settings.h"
#include <SD.h>
#include "small_file.h"
#include <esp_rom_crc.h>
#include <vector>
#include <algorithm>

static const char* SOURCES_PATH = "/.sys/sources.txt";
static const char* SOURCE_OFFSETS = "/.sys/sources.idx";
static const char* FAV_PATH     = "/.sys/fav.txt";
static const char* FAV_BACKUP = "/.sys/fav.bak";
static bool favouriteFailed=false;
static const char* FAV_TMP      = "/.sys/fav.tmp";
static const char* M3U_DIR      = "/playlists";
static const char* KEYS_PATH    = "/.sys/paths.keys";
static const int   FIXED        = 2;          // All, Favourites
static const int   MAX_FAV      = 500;        // 2 KB of CRCs at the most
static const int   MAX_M3U      = 1000;       // entries read from one playlist

static PlaylistQueue queue;
static const char* QUEUE_FILES[] = {"/.sys/queue-a.ids", "/.sys/queue-b.ids"};
static const char* PREVIEW_FILES[] = {"/.sys/preview-a.ids", "/.sys/preview-b.ids"};
static File queueOut;
static uint16_t* previewOut = nullptr;
static bool queueOk = true;
static const char* stagedPath = nullptr;

int PlaylistQueue::queueAt(int index) {
  if (index < 0 || index >= count) return -1;
  if (!path) return index;
  if (index < first || index >= first + cached) {
    first = -1; cached = 0;
    const int start = index / 256 * 256, n = min(256, count - start);
    File f = openSmallFile(path);
    if (!f || !f.seek(start * sizeof(uint16_t)) ||
        f.read((uint8_t*)ids, n * sizeof(uint16_t)) != n * sizeof(uint16_t)) return -1;
    first = start; cached = n;
  }
  return ids[index - first];
}
int PlaylistQueue::queueFind(int track) {
  if (track < 0) return -1;
  for (int i = 0; i < count; ++i) { const int id = queueAt(i); if (id < 0) return -1; if (id == track) return i; }
  return -1;
}
static int queueRead(void* context, int pos) { return ((PlaylistQueue*)context)->queueAt(pos); }
static void bindQueue(int n, int keep, bool jump, bool identity = false) {
  queue.path = identity ? nullptr : stagedPath; queue.count = n; queue.first = -1; queue.cached = 0;
  audio_setQueueSource({nullptr, &queue, queueRead}, n, keep, jump);
}
static void putId(int pos, uint16_t id) {
  if (previewOut) { previewOut[pos] = id; return; }
  queueOk = queueOk && queueOut && queueOut.seek(pos * sizeof(id)) &&
            queueOut.write((const uint8_t*)&id, sizeof(id)) == sizeof(id);
}
static int getId(int pos) {
  if (previewOut) return previewOut[pos];
  uint16_t id;
  if (!queueOk || !queueOut.seek(pos * sizeof(id)) || queueOut.read((uint8_t*)&id, sizeof(id)) != sizeof(id)) { queueOk = false; return -1; }
  return id;
}
static bool stageQueue(const char* path = nullptr) {
  queueOk = true;
  stagedPath = path ? path : queue.path == QUEUE_FILES[0] ? QUEUE_FILES[1] : QUEUE_FILES[0];
  queueOut = openSmallFile(stagedPath, "w+");
  queueOk = bool(queueOut);
  return queueOk;
}
static int       queueCap = 0;
static int       sourceCount = FIXED;
static int       folderSources = 0, playlistSources = 0;
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
// Sources file: a "#sig" line, then "kind \t name \t path [\t first \t count]"
// per line, folders first. A folder's first/count is its run of tracks in
// library order (the scan is depth-first, so a folder's songs are contiguous);
// count 0 means "not one run: scan for it".
// ---------------------------------------------------------------------------
static bool readSource(int i, SourceKind& kind, String& name, String& path, int* first = nullptr, int* count = nullptr) {
  if (i < FIXED || i >= sourceCount) return false;
  File index = openSmallFile(SOURCE_OFFSETS);
  uint32_t offset = 0;
  const bool found = index && index.seek((i-FIXED)*sizeof(offset)) && index.read((uint8_t*)&offset,sizeof(offset)) == sizeof(offset);
  index.close();
  if (!found) return false;
  File f = openSmallFile(SOURCES_PATH);
  if (!f) return false;
  if (!f.seek(offset)) { f.close(); return false; }
  String line = f.readStringUntil('\n');
  f.close();
  const int a = line.indexOf('\t'), b = a < 0 ? -1 : line.indexOf('\t', a + 1);
  if (b < 0) return false;
  kind = (SourceKind)line.substring(0, a).toInt();
  name = line.substring(a + 1, b);
  const int c = line.indexOf('\t', b + 1), d = c < 0 ? -1 : line.indexOf('\t', c + 1);
  path = c < 0 ? line.substring(b + 1) : line.substring(b + 1, c);
  if (first) *first = d > c ? line.substring(c + 1, d).toInt() : 0;
  if (count) *count = d > c ? line.substring(d + 1).toInt() : 0;
  return true;
}

// Per-track path keys in library order, so building a list never rereads the
// 712 B catalogue records: the full path, its last two components
// ("/Dir/file") and "/file". About 12 B per track, read 32 tracks at a time.
struct TrackKeys { uint32_t full, tail, name; };
struct KeysHeader { char magic[8]; uint32_t library, count; };
static const char KEYS_MAGIC[8] = {'C','Y','D','K','E','Y','S','1'};
static bool keysOk = false;                 // /.sys/paths.keys matches the library
static TrackKeys keyChunk[32];              // one SD read's worth, never on the stack
static TrackKeys trackKeys(const String& path) {
  const int last = path.lastIndexOf('/');
  const int previous = last > 0 ? path.substring(0, last).lastIndexOf('/') : -1;
  return {pathKey(path), pathKey(path.substring(previous < 0 ? 0 : previous)), pathKey(path.substring(last < 0 ? 0 : last))};
}
static bool keysValid() {
  File f = openSmallFile(KEYS_PATH);
  KeysHeader h{};
  const bool ok = f && f.read((uint8_t*)&h, sizeof(h)) == sizeof(h) && !memcmp(h.magic, KEYS_MAGIC, 8) &&
    h.library == storage_libraryCrc() && int(h.count) == storage_trackCount() &&
    f.size() == sizeof(h) + h.count * sizeof(TrackKeys);
  f.close();
  return ok;
}

// Boot-time folder and key collection, shared with the storage_forEach callback.
struct FolderRange { String dir; int first, last; bool split; };
static std::vector<FolderRange>* folders = nullptr;
static File keysOut;
static int keysPending = 0, keysWritten = 0;
static bool keysWriteOk = false;
static void flushKeys() {
  if (keysPending && keysWriteOk)
    keysWriteOk = keysOut.write((const uint8_t*)keyChunk, keysPending * sizeof(TrackKeys)) == keysPending * sizeof(TrackKeys);
  keysWritten += keysPending; keysPending = 0;
}

static void collectFolders(int i, const String& path, const String&) {
  if (keysOut) {
    if (i != keysWritten + keysPending) keysWriteOk = false;   // a record was unreadable
    keyChunk[keysPending++] = trackKeys(path);
    if (keysPending == 32) flushKeys();
  }
  // "/music/A/B/song.mp3" -> "A/B" and "A"
  if (!path.startsWith("/music/")) return;
  const String rel = path.substring(7);
  for (int s = rel.indexOf('/'); s > 0; s = rel.indexOf('/', s + 1)) {
    const String dir = rel.substring(0, s);
    auto f = std::find_if(folders->begin(), folders->end(), [&](const FolderRange& r) { return r.dir == dir; });
    if (f == folders->end()) folders->push_back({dir, i, i, false});
    else { if (i != f->last + 1) f->split = true; f->last = i; }
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
  if(!SD.exists(FAV_PATH)&&SD.exists(FAV_BACKUP)&&!storage_readOnly())SD.rename(FAV_BACKUP,FAV_PATH);
  File f = openSmallFile(FAV_PATH);
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

// Library CRC, track count and every playlist path: when none of them changed,
// the folders, names and keys written last boot are still right.
static uint32_t sourcesSignature(const std::vector<String>& paths) {
  uint32_t sig = storage_libraryCrc();
  const int32_t n = storage_trackCount();
  sig = esp_rom_crc32_le(sig, (const uint8_t*)&n, sizeof(n));
  for (const String& p : paths) sig = esp_rom_crc32_le(sig, (const uint8_t*)p.c_str(), p.length() + 1);
  return sig;
}
static bool reuseSources(uint32_t sig) {
  File f = openSmallFile(SOURCES_PATH);
  if (!f) return false;
  const String line = f.readStringUntil('\n');
  f.close();
  unsigned long seen = 0, names = 0; int dirs = -1, lists = -1;
  if (sscanf(line.c_str(), "#sig %lx %lx %d %d", &seen, &names, &dirs, &lists) != 4 ||
      uint32_t(seen) != sig || dirs < 0 || lists < 0) return false;
  File index = openSmallFile(SOURCE_OFFSETS);
  const bool indexOk = index && index.size() == uint32_t(dirs + lists) * sizeof(uint32_t);
  index.close();
  if (!indexOk || !keysValid()) return false;
  namesCrc = uint32_t(names); folderSources = dirs; playlistSources = lists;
  sourceCount = FIXED + dirs + lists; keysOk = true;
  return true;
}
// One library pass: folder runs, plus /.sys/paths.keys for list builds.
static void collectLibrary(std::vector<FolderRange>& dirs) {
  SD.remove(KEYS_PATH);
  keysOut = openSmallFile(KEYS_PATH, FILE_WRITE);
  KeysHeader h{};
  keysWriteOk = keysOut && keysOut.write((const uint8_t*)&h, sizeof(h)) == sizeof(h);
  keysPending = keysWritten = 0;
  folders = &dirs;
  storage_forEach(collectFolders);
  folders = nullptr;
  flushKeys();
  if (keysOut) keysOut.close();
  keysOut = File();
  keysWriteOk = keysWriteOk && keysWritten == storage_trackCount();
  if (keysWriteOk) {
    File f = openSmallFile(KEYS_PATH, "r+");
    memcpy(h.magic, KEYS_MAGIC, 8); h.library = storage_libraryCrc(); h.count = uint32_t(keysWritten);
    keysWriteOk = f && f.seek(0) && f.write((const uint8_t*)&h, sizeof(h)) == sizeof(h);
    f.close();
  }
  keysOk = keysWriteOk && keysValid();
  if (!keysOk) { SD.remove(KEYS_PATH); Serial.println("playlist: path keys unavailable, lists scan the library"); }
}

// List every folder and playlist into the sources file. Playlists come from
// /playlists and from anywhere in the /music tree the library scan reaches.
static void scanSources() {
  keysOk = false;
  if (storage_readOnly()) {                       // CT-07: sources.txt cannot be written
    sourceCount = FIXED; folderSources = playlistSources = 0; namesCrc = 0;
    storage_releasePlaylistFiles();
    return;
  }
  std::vector<String> paths;
  File d = openSmallFile(M3U_DIR);
  if (d && d.isDirectory()) {
    for (File f = d.openNextFile(); f; f = d.openNextFile()) {
      if (!f.isDirectory() && isPlaylistFile(f.name())) paths.push_back(String(M3U_DIR) + "/" + f.name());
      f.close();
    }
  }
  if (d) d.close();
  const size_t inMusic = storage_playlistFiles().size();
  for (const String& p : storage_playlistFiles()) paths.push_back(p);
  storage_releasePlaylistFiles();
  std::sort(paths.begin(), paths.end());
  const uint32_t sig = sourcesSignature(paths);
  if (reuseSources(sig)) {
    Serial.printf("playlist: %d folders, %d playlists (%u in /music), %u favourites, unchanged since last boot\n",
                  folderSources, playlistSources, (unsigned)inMusic, (unsigned)favs.size());
    return;
  }

  std::vector<FolderRange> dirs;
  collectLibrary(dirs);
  std::sort(dirs.begin(), dirs.end(), [](const FolderRange& a, const FolderRange& b) { return a.dir < b.dir; });

  // Shown by file name; two lists with the same name also show their folder.
  struct ListSource { String name, path; };
  std::vector<ListSource> lists;
  for (const String& p : paths) lists.push_back({baseName(p), p});
  std::sort(lists.begin(), lists.end(), [](const ListSource& a, const ListSource& b) {
    String x = a.name, y = b.name; x.toLowerCase(); y.toLowerCase();
    return x != y ? x < y : a.path < b.path;
  });
  std::vector<String> lowNames;
  for (const ListSource& l : lists) { String low = l.name; low.toLowerCase(); lowNames.push_back(low); }
  for (size_t i = 0; i < lists.size(); ++i) {
    const bool twin = (i > 0 && lowNames[i - 1] == lowNames[i]) || (i + 1 < lists.size() && lowNames[i + 1] == lowNames[i]);
    if (!twin) continue;
    const int last = lists[i].path.lastIndexOf('/');
    const String dir = lists[i].path.substring(0, last);
    lists[i].name += " (" + dir.substring(dir.lastIndexOf('/') + 1) + ")";
  }

  namesCrc = 0;
  for (const FolderRange& r : dirs) namesCrc = esp_rom_crc32_le(namesCrc, (const uint8_t*)r.dir.c_str(), r.dir.length());
  for (const ListSource& l : lists) namesCrc = esp_rom_crc32_le(namesCrc, (const uint8_t*)l.name.c_str(), l.name.length());

  SD.remove(SOURCES_PATH);
  SD.remove(SOURCE_OFFSETS);
  File out = openSmallFile(SOURCES_PATH, FILE_WRITE);
  File index = openSmallFile(SOURCE_OFFSETS, FILE_WRITE);
  sourceCount = FIXED;
  folderSources = playlistSources = 0;
  bool sourcesOk = out && index &&
    out.printf("#sig %08lx %08lx %d %d\n", (unsigned long)sig, (unsigned long)namesCrc, int(dirs.size()), int(lists.size())) > 0;
  auto put = [&](SourceKind kind, const String& name, const String& path, int first, int count) {
    if (!sourcesOk) return;
    const uint32_t offset = out.position();
    const bool line = kind == SRC_FOLDER
      ? out.printf("%u\t%s\t%s\t%d\t%d\n", kind, name.c_str(), path.c_str(), first, count) > 0
      : out.printf("%u\t%s\t%s\n", kind, name.c_str(), path.c_str()) > 0;
    if (!line || index.write((const uint8_t*)&offset, sizeof(offset)) != sizeof(offset)) { sourcesOk = false; return; }
    sourceCount++;
    if (kind == SRC_FOLDER) ++folderSources; else ++playlistSources;
  };
  for (const FolderRange& r : dirs) put(SRC_FOLDER, r.dir, "/music/" + r.dir, r.first, r.split ? 0 : r.last - r.first + 1);
  for (const ListSource& l : lists) put(SRC_M3U, l.name, l.path, 0, 0);
  out.close();
  index.close();
  if (!sourcesOk) {                               // never reuse a half-written file
    SD.remove(SOURCES_PATH); SD.remove(SOURCE_OFFSETS);
    sourceCount = FIXED; folderSources = playlistSources = 0;
  }
  Serial.printf("playlist: %u folders, %u playlists (%u in /music), %u favourites\n", (unsigned)dirs.size(),
                (unsigned)lists.size(), (unsigned)inMusic, (unsigned)favs.size());
}

void playlist_begin() {
  queueCap = max(1, storage_trackCount());

  loadFavourites();
  scanSources();
}

// ---------------------------------------------------------------------------
// Building a queue
// ---------------------------------------------------------------------------
static int    built = 0, visited = 0;
struct BuildTelemetry {
  uint32_t started = millis(), scanMs = 0;
  int records = 0;
  bool playing = audio_isPlaying(), ok = true;
};
static void checkedScan(StorageEachFn fn, BuildTelemetry& telemetry) {
  const uint32_t started = millis();
  visited = 0; storage_forEach(fn);
  telemetry.scanMs = millis() - started;
  telemetry.records = visited;
  if (visited != storage_trackCount()) queueOk = false;
}
// Every track's keys in library order: streamed from /.sys/paths.keys (18 KB
// for 1,500 tracks), or computed from the catalogue when the sidecar is missing.
typedef void (*KeyFn)(int i, const TrackKeys& keys);
static KeyFn keyFn = nullptr;
static void keysFromPath(int i, const String& path, const String&) { ++visited; keyFn(i, trackKeys(path)); }
static void scanKeys(KeyFn fn, BuildTelemetry& telemetry) {
  const uint32_t started = millis();
  visited = 0;
  const int n = storage_trackCount();
  if (keysOk) {
    File f = openSmallFile(KEYS_PATH);
    bool ok = f && f.seek(sizeof(KeysHeader));
    for (int i = 0; ok && i < n;) {
      const int m = min(32, n - i);
      ok = f.read((uint8_t*)keyChunk, m * sizeof(TrackKeys)) == m * sizeof(TrackKeys);
      for (int j = 0; ok && j < m; ++j) { ++visited; fn(i + j, keyChunk[j]); }
      i += m;
    }
    f.close();
  } else {
    keyFn = fn; storage_forEach(keysFromPath); keyFn = nullptr;
  }
  telemetry.scanMs = millis() - started;
  telemetry.records = visited;
  if (visited != n) queueOk = false;
}
static String folderPrefix;

static void addFavourite(int i, const TrackKeys& k) {
  if (built < queueCap && isFav(k.full)) putId(built++, i);
}

static void addFolder(int i, const String& path, const String&) {
  ++visited;
  if (built < queueCap && path.startsWith(folderPrefix)) putId(built++, i);
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

// One 4-byte key per entry: 22 suffix-hash bits and its 10-bit list position.
// Full hashes are checked after the index lookup; collisions cannot select a track.
static uint32_t* m3uKeys = nullptr;
static int m3uCount = 0;
static File m3uFile, m3uState;
static String m3uDir;
static PlaylistMatchStats matchStats;
// Per entry, on the card: where it resolves to (exact), its "/Dir/file" or
// "/file" key, and the best track so far. flags: 1 ambiguous, 2 entry has a folder.
struct M3uMatch { uint32_t exact, key; uint16_t id; uint8_t score, flags; };
static const char* M3U_STATE = "/.sys/m3u-match.tmp";
static uint32_t indexKey(uint32_t key) { return (key & 0x3FFFFF) << 10; }
// Drain long lines without growing a String past 258 bytes (255 plus BOM).
static String entryLine(File& f) {
  char bytes[259]; size_t n = 0; bool longer = false; uint8_t c;
  while (f.available()) {
    if (f.read(&c, 1) != 1) { queueOk = false; break; }
    if (c == '\n') break;
    if (c == '\r') continue;
    if (n < sizeof(bytes)-1) bytes[n++] = char(c); else longer = true;
  }
  bytes[n] = 0;
  String line(bytes);
  if (line.startsWith("\xEF\xBB\xBF")) line = line.substring(3);
  line.trim();
  if (longer && !line.isEmpty() && line[0] != '#') line += "xxxx";
  return line;
}
static bool matchRecord(int p, M3uMatch& record, bool write = false) {
  const size_t bytes = sizeof(record);
  const bool ok = m3uState.seek(p * bytes) &&
    (write ? m3uState.write((const uint8_t*)&record, bytes) : m3uState.read((uint8_t*)&record, bytes)) == bytes;
  if (!ok) queueOk = false;
  return ok;
}
// Scores: 7 the entry resolves to exactly this path; 2 the last folder and file
// agree; 1 a bare file name agrees (entries with a folder never match by name).
static void probeM3u(int i, uint32_t trackKey, uint32_t full, bool folder) {
  auto end = m3uKeys + m3uCount;
  const uint32_t key = indexKey(trackKey);
  for (auto hit = std::lower_bound(m3uKeys, end, key); queueOk && hit != end && (*hit & ~1023u) == key; ++hit) {
    const int p = *hit & 1023u; M3uMatch record;
    if (!matchRecord(p, record)) return;
    if (record.key != trackKey || bool(record.flags & 2) != folder) continue;  // 22-bit index collision
    const uint8_t score = record.exact == full ? 7 : folder ? 2 : 1;
    if (score > record.score) { record.id = i; record.score = score; record.flags &= ~1; }
    else if (score == record.score && record.id != i) record.flags |= 1;
    else continue;
    matchRecord(p, record, true);
  }
}
static void matchM3u(int i, const TrackKeys& k) {
  probeM3u(i, k.tail, k.full, true);
  probeM3u(i, k.name, k.full, false);
}
const PlaylistMatchStats& playlist_lastMatchStats() { return matchStats; }
static int buildM3u(const String& listPath, BuildTelemetry& telemetry) {
  telemetry.ok = false;
  matchStats = {};
  m3uFile = openSmallFile(listPath);
  if (!m3uFile) return 0;
  const int cap = min(queueCap, MAX_M3U);
  m3uKeys = (uint32_t*)malloc(cap * sizeof(uint32_t));
  m3uState = previewOut ? openSmallFile(M3U_STATE, "w+") : queueOut;
  if (!m3uKeys || !m3uState) { free(m3uKeys); m3uKeys = nullptr; m3uFile.close(); m3uState.close(); return 0; }
  m3uDir = listPath.substring(0, listPath.lastIndexOf('/'));
  m3uCount = 0;
  while (queueOk && m3uFile.available()) {
    String line = entryLine(m3uFile);
    if (line.isEmpty() || line[0] == '#' || line.indexOf("://") >= 0) continue;
    ++matchStats.entries;
    if (line.length() > 255) { ++matchStats.overlong; continue; }
    if (m3uCount >= cap) { ++matchStats.truncated; continue; }
    line.replace('\\', '/');
    if (line.length() > 1 && line[1] == ':') line = line.substring(2);
    // Index the last two components, or the bare file name.
    const String tail = normalise(line);
    const int last = tail.lastIndexOf('/');
    const int previous = last > 0 ? tail.substring(0, last).lastIndexOf('/') : -1;
    M3uMatch record{pathKey(resolveEntry(line, m3uDir)), pathKey(tail.substring(previous < 0 ? 0 : previous)),
                    0xFFFF, 0, uint8_t(last > 0 ? 2 : 0)};
    m3uKeys[m3uCount] = indexKey(record.key) | uint32_t(m3uCount);
    matchRecord(m3uCount++, record, true);
  }
  m3uFile.close();   // matching needs only the keys: one fewer open file during the scan
  std::sort(m3uKeys, m3uKeys + m3uCount);
  if (queueOk) scanKeys(matchM3u, telemetry);
  free(m3uKeys); m3uKeys = nullptr;
  int n = 0;
  for (int p = 0; queueOk && p < m3uCount; ++p) {
    M3uMatch record; if (!matchRecord(p, record)) break;
    const bool ambiguous = record.flags & 1;
    if (ambiguous) ++matchStats.ambiguous;
    if (!record.score || (record.score == 1 && ambiguous)) continue;
    putId(n++, record.id);
    if (record.score == 7) ++matchStats.exact;
    else if (record.score == 1) ++matchStats.byName;
    else ++matchStats.byTail;
  }
  m3uState.close(); if (previewOut) SD.remove(M3U_STATE);
  matchStats.missing = matchStats.entries - matchStats.exact - matchStats.byTail - matchStats.byName;
  telemetry.ok = queueOk;
  return n;
}

// Fill a source, then report once after all build I/O. All is an identity.
static int build(int i, bool* identity, const char* path = nullptr) {
  if (i == 0) { *identity = true; return storage_trackCount(); }
  BuildTelemetry telemetry;
  const char* role = previewOut || path ? "preview" : "queue";
  *identity = false;
  const bool staged = previewOut || stageQueue(path);
  SourceKind kind = SRC_FAVOURITES;
  String name, sourcePath;
  int first = 0, count = 0;
  const bool sourceOk = i == 1 || readSource(i, kind, name, sourcePath, &first, &count);
  telemetry.ok = staged && sourceOk;
  int n = 0;
  built = 0;
  if (kind == SRC_M3U) matchStats = {};
  if (telemetry.ok) {
    if (kind == SRC_FAVOURITES) {
      if (!favs.empty()) scanKeys(addFavourite, telemetry);
      n = built;
    } else if (kind == SRC_FOLDER && count > 0 && first >= 0 && first + count <= storage_trackCount()) {
      for (int j = 0; j < count && built < queueCap; ++j) putId(built++, first + j);   // one run, no scan
      telemetry.records = count;
      n = built;
    } else if (kind == SRC_FOLDER) {
      folderPrefix = sourcePath + "/";
      checkedScan(addFolder, telemetry);
      n = built;
    } else {
      n = buildM3u(sourcePath, telemetry);
    }
  }
  if (staged) queueOut.close();
  const uint32_t totalMs = millis() - telemetry.started;
  const char* status = telemetry.ok && queueOk ? "ok" : "fail";
  if (sourceOk && kind == SRC_M3U) {
    Serial.printf("cmd: playlist %s n=%lu found=%d missing=%lu ambiguous=%lu role=%s cache=miss scan=%lu records=%d total=%lu status=%s playing=%d\n",
      baseName(sourcePath).c_str(), (unsigned long)matchStats.entries, n,
      (unsigned long)matchStats.missing, (unsigned long)matchStats.ambiguous, role,
      (unsigned long)telemetry.scanMs, telemetry.records, (unsigned long)totalMs, status, int(telemetry.playing));
  } else if (sourceOk) {
    Serial.printf("cmd: source %s %d role=%s scan=%lu records=%d total=%lu status=%s playing=%d\n",
      kind == SRC_FAVOURITES ? "favourites" : "folder", i, role,
      (unsigned long)telemetry.scanMs, telemetry.records, (unsigned long)totalMs, status, int(telemetry.playing));
  }
  return queueOk ? n : 0;
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
int        playlist_sourceCount(SourceKind kind) { return kind == SRC_FOLDER ? folderSources : kind == SRC_M3U ? playlistSources : 0; }
int        playlist_sourceAt(SourceKind kind, int row) {
  if (row < 0 || row >= playlist_sourceCount(kind)) return -1;
  return FIXED + (kind == SRC_M3U ? folderSources : 0) + row;
}
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
  if (g_settings.srcKind == SRC_CATALOG && g_settings.catalogCrc == storage_libraryCrc() &&
      g_settings.catalogKind <= uint8_t(CatalogKind::ArtistAlbumSongs)) {
    CatalogView view{CatalogKind(g_settings.catalogKind), g_settings.catalogGroup};
    const int n = storage_browseCount(view);
    bool ok = stageQueue();
    for (int j = 0; ok && j < n; ++j) { CatalogEntry e; ok = storage_browseEntry(view, j, e) && e.track >= 0; if (ok) putId(j, e.track); }
    queueOut.close();
    if (ok && queueOk && n > 0) { current = -1; bindQueue(n, resumeTrack, false); return; }
  }
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
  if (found) bindQueue(n, resumeTrack, true);
  Serial.printf("playlist: source %d (%u), %d tracks\n", found, g_settings.srcKind, n);
}

bool playlist_select(int i) {
  if (i < 0 || i >= sourceCount) return false;
  bool identity;
  const int n = build(i, &identity);
  if (n == 0) {
    return false;
  }
  remember(i);
  bindQueue(n, audio_currentTrack(), true, identity);
  settings_save();
  return true;
}

int playlist_preview(int source, uint16_t* tracks, int capacity) {
  if (!tracks || source < 0 || source >= sourceCount || capacity < queueCap) return 0;
  // build() is UI-task-only. Its destination is temporarily caller-owned.
  previewOut = tracks;
  queueOk = true;
  bool identity = false;
  const int n = build(source, &identity);
  previewOut = nullptr;
  if (identity) for (int j = 0; j < n; ++j) tracks[j] = j;
  return n;
}

bool playlist_preview(int source, PlaylistQueue& preview) {
  if (source < 0 || source >= sourceCount) return false;
  const char* path = preview.path == PREVIEW_FILES[0] ? PREVIEW_FILES[1] : PREVIEW_FILES[0];
  bool identity = false;
  const int n = build(source, &identity, path);
  if (n <= 0) return false;
  preview.path = identity ? nullptr : path;
  preview.count = n; preview.first = -1; preview.cached = 0;
  return true;
}

bool playlist_playPreview(PlaylistQueue& preview, int row, int source) {
  const int n = preview.queueCount();
  if (!audio_ready() || n <= 0 || n > queueCap || row < 0 || row >= n) return false;
  if (!stageQueue()) return false;
  int selected = -1;
  for (int j = 0; queueOk && j < n; ++j) {
    const int id = preview.queueAt(j);
    if (id < 0 || id >= storage_trackCount()) { queueOk = false; break; }
    putId(j, id);
    if (j == row) selected = id;
  }
  queueOut.close();
  if (!queueOk || selected < 0) return false;
  remember(source);
  bindQueue(n, selected, false);
  audio_playTrack(selected);
  return true;
}

bool playlist_playTracks(const uint16_t* tracks, int n, int row, int source) {
  if (!audio_ready() || !tracks || n <= 0 || n > queueCap || row < 0 || row >= n) return false;
  for (int j = 0; j < n; ++j) if (tracks[j] >= storage_trackCount()) return false;
  const int selected = tracks[row];
  if (!stageQueue()) return false;
  for (int j = 0; j < n; ++j) putId(j, tracks[j]);
  queueOut.close();
  if (!queueOk) return false;
  if (source >= 0) remember(source);
  else { current = -1; g_settings.srcKind = SRC_CATALOG; }
  bindQueue(n, selected, false);
  audio_playTrack(selected);
  return true;
}

static bool memoryRefusal = false;
bool playlist_memoryRefused() { return memoryRefusal; }

bool playlist_playView(CatalogView view, int row) {
  memoryRefusal = false;
  const int n = storage_browseCount(view);
  if (n <= 0 || n > queueCap || row < 0 || row >= n) return false;
  if (!audio_ready() || !stageQueue()) return false;
  int selected = -1;
  catalog_logStage("queue", "before", 0);
  uint16_t ids[256];
  for (int first = 0; queueOk && first < n; first += 256) {
    const int wanted = min(256, n - first);
    if (storage_browseTrackIds(view, first, ids, wanted) != wanted) { queueOk = false; break; }
    for (int j = 0; queueOk && j < wanted; ++j) {
      if (ids[j] >= storage_trackCount()) { queueOk = false; break; }
      putId(first + j, ids[j]);
      if (first + j == row) selected = ids[j];
    }
  }
  queueOut.close();
  const bool ok = queueOk && selected >= 0;
  catalog_logStage("queue", ok ? "after" : "failed", 0);
  if (ok) {
    current = -1; g_settings.srcKind = SRC_CATALOG;
    bindQueue(n, selected, false);
    audio_playTrack(selected);
    g_settings.catalogKind = uint8_t(view.kind); g_settings.catalogGroup = view.group;
    g_settings.catalogCrc = storage_libraryCrc(); settings_save();
  }
  return ok;
}

bool playlist_isFavourite(int track) {
  return !favs.empty() && isFav(pathKey(storage_trackPath(track)));
}

bool playlist_favouritesFull() { return (int)favs.size() >= MAX_FAV; }

bool playlist_favouriteFailed(){return favouriteFailed;}
bool playlist_toggleFavourite(int track) {
  favouriteFailed=false;
  const String path=storage_trackPath(track);
  const uint32_t key=pathKey(path);const bool was=isFav(key);
  auto fail=[&](){favouriteFailed=true;Serial.println("cmd: favourite ignored (SD write or memory unavailable)");return was;};
  if(path.isEmpty()||storage_readOnly()||(!was&&playlist_favouritesFull()))return fail();
  if(!was&&favs.size()==favs.capacity()&&!catalog_canAllocate((favs.size()+1)*sizeof(uint32_t)*2))return fail();
  // Stage both add and remove before publishing RAM state. A refused/short
  // write used to light the favourite without ever saving it on the card.
  SD.remove(FAV_TMP);
  const bool existed=SD.exists(FAV_PATH);
  File in=existed?openSmallFile(FAV_PATH):File(),out=openSmallFile(FAV_TMP,FILE_WRITE);
  bool ok=out&&(!existed||in);
  while(ok&&in&&in.available()){
    const uint32_t before=in.position();
    String line=in.readStringUntil('\n');line.trim();
    if(in.position()<=before){ok=false;break;}
    if(line.length()&&(!was||pathKey(line)!=key))ok=out.print(line.c_str())==line.length()&&out.print("\n")==1;
  }
  if(ok&&!was)ok=out.print(path.c_str())==path.length()&&out.print("\n")==1;
  in.close();out.close();
  if(!ok){SD.remove(FAV_TMP);return fail();}
  if(SD.exists(FAV_BACKUP)&&!SD.remove(FAV_BACKUP))return fail();
  if(existed&&!SD.rename(FAV_PATH,FAV_BACKUP))return fail();
  if(!SD.rename(FAV_TMP,FAV_PATH)){
    if(existed)SD.rename(FAV_BACKUP,FAV_PATH);
    return fail();
  }
  if(existed)SD.remove(FAV_BACKUP);
  if(was)favs.erase(std::lower_bound(favs.begin(),favs.end(),key));
  else favs.insert(std::upper_bound(favs.begin(),favs.end(),key),key);
  Serial.printf("cmd: favourite %s executed\n",was?"removed":"added");

  // Playing from Favourites: the list follows, without cutting the song off.
  if (current == 1) {
    bool identity;
    const int n = build(1, &identity);
    if (!queueOk) Serial.println("queue: favourites SD read failed; previous queue retained");
    else if (n == 0) playlist_select(0);
    else bindQueue(n, audio_currentTrack(), false);
  }
  return !was;
}

void playlist_forEachName(void (*fn)(const char* text)) {
  File f = openSmallFile(SOURCES_PATH);
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
