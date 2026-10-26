// Player image only (firmware.h).
#ifndef CYD_UPLOADER

#include "audio.h"
#include "command_queue.h"
#include "storage.h"
#include "settings.h"     // RepeatMode
#include "codec_track.h"
#include "small_file.h"

// ---------------------------------------------------------------------------
// Ring buffer: one producer (decode task), one consumer (audio_pull)
// ---------------------------------------------------------------------------
// 2048 stereo frames = 46 ms, 8 KB. 1024 was tried in stage 2 and underran
// ~80 times a second with a background playing. Not because Bluetooth asks
// for much at once -- it pulls 128 frames per call (audio_maxRequest()) -- but
// because the background's .anim reads share the SD card with this task, and
// 23 ms is not enough cushion to wait out them plus a frame decode. The same
// run showed background frames taking 300 ms instead of ~95.
static const uint32_t RB_FRAMES = 2048;
static int16_t*          rb = nullptr;
static volatile uint32_t rbHead = 0;          // producer only
static volatile uint32_t rbTail = 0;          // consumer only
// One short lock covers ring publication, consumer copies, commands and clock epochs.
// Never hold it across SD/decoder operations or waits.
static portMUX_TYPE stateLock = portMUX_INITIALIZER_UNLOCKED;

static inline uint32_t rbUsed() { return (rbHead - rbTail) & (RB_FRAMES - 1); }
static inline uint32_t rbFree() { return RB_FRAMES - 1 - rbUsed(); }

// ---------------------------------------------------------------------------
// Shared state
// ---------------------------------------------------------------------------
static volatile int      reqTrack  = -1;      // UI -> decode task
static volatile int32_t  reqSeekMs = -1;
static volatile uint32_t reqStartMs = 0, commandSerial = 0, seekGoalMs = 0;
static volatile bool seeking = false, canSeek = false;
static volatile bool decodeBlocked = false; // seek I/O failure: retain track/queue, await retry
static inline bool requestPending() { return reqTrack >= 0 || reqSeekMs >= 0; }
static volatile bool     playing   = true;
static uint32_t playIntentSerial=0; // loop task: explicit play/pause/selection intent
static volatile uint8_t  volumePct = 40;
static volatile uint8_t sleepGain=100;

static volatile int      curTrack  = 0;
static volatile uint32_t posBaseMs = 0;       // where in the track the ring started
static volatile uint32_t framesPlayed = 0;    // pulled since posBaseMs
static volatile uint32_t durationMs = 0;
static volatile uint32_t trackSerial = 0;
static volatile uint32_t underruns = 0;
static volatile bool     starting = false;    // opening a track: no underrun counting
static volatile uint32_t lastPullMs = 0;

static TaskHandle_t decodeHandle = nullptr;
static volatile bool startupComplete = false;
static portMUX_TYPE errorLock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t errorSerial = 0;
static int errorTrack = -1;
static char errorMessage[48] = {};
static void reportError(const char* message, int track) {
  portENTER_CRITICAL(&errorLock);
  snprintf(errorMessage, sizeof(errorMessage), "%s", message);
  errorTrack = track; ++errorSerial;
  portEXIT_CRITICAL(&errorLock);
}
uint32_t audio_error(char* message, size_t capacity, int* track) {
  portENTER_CRITICAL(&errorLock);
  if (message && capacity) snprintf(message, capacity, "%s", errorMessage);
  if (track) *track = errorTrack;
  const uint32_t serial = errorSerial;
  portEXIT_CRITICAL(&errorLock);
  return serial;
}
bool audio_ready() { return rb && decodeHandle; }

// ---------------------------------------------------------------------------
// Consumer
// ---------------------------------------------------------------------------
static volatile int32_t maxRequest = 0;
static volatile uint32_t pullCalls = 0;       // diagnostics: how often Bluetooth pulled, and how many frames it took
static volatile uint32_t pullFrames = 0;

void audio_pull(int16_t* out, int32_t frames) {
  if (!rb) { memset(out, 0, frames * 2 * sizeof(int16_t)); return; }
  lastPullMs = millis();
  pullCalls++;
  if (frames > maxRequest) maxRequest = frames;
  portENTER_CRITICAL(&stateLock);
  int32_t i = 0;
  if (playing && !seeking) {
    const int32_t gain = (int32_t)volumePct * volumePct * sleepGain / 100;   // perceptual-ish, /10000
    uint32_t tail = rbTail;
    const int32_t n = min<int32_t>(frames, (rbHead - tail) & (RB_FRAMES - 1));
    for (; i < n; i++) {
      out[i * 2]     = (int16_t)((int32_t)rb[tail * 2]     * gain / 10000);
      out[i * 2 + 1] = (int16_t)((int32_t)rb[tail * 2 + 1] * gain / 10000);
      tail = (tail + 1) & (RB_FRAMES - 1);
    }
    rbTail = tail;
    framesPlayed += n;
    pullFrames += n;
    if (n > 0) starting = false;
    if (n < frames && !starting && reqTrack < 0 && reqSeekMs < 0) underruns++;
  }
  portEXIT_CRITICAL(&stateLock);
  memset(out + i * 2, 0, (frames - i) * 2 * sizeof(int16_t));
}

// ---------------------------------------------------------------------------
// Producer: resampler and PCM callback
// ---------------------------------------------------------------------------
static volatile bool abortDecode = false;     // a request arrived mid-buffer

static void pushFrame(int16_t l, int16_t r) {
  for (;;) {
    portENTER_CRITICAL(&stateLock);
    if (requestPending()) { abortDecode = true; portEXIT_CRITICAL(&stateLock); return; }
    if (rbFree()) {
      const uint32_t h = rbHead;
      rb[h * 2] = l; rb[h * 2 + 1] = r;
      rbHead = (h + 1) & (RB_FRAMES - 1);
      portEXIT_CRITICAL(&stateLock); return;
    }
    portEXIT_CRITICAL(&stateLock);
    vTaskDelay(pdMS_TO_TICKS(4));
  }
}

static CodecTrack m4a;                        // stateless facade over the single 40 KiB pool
static File     file;

static void requestFlush() {
  portENTER_CRITICAL(&stateLock);
  rbTail = rbHead; framesPlayed = 0;
  portEXIT_CRITICAL(&stateLock);
}
struct SeekContext { uint32_t serial; };
static int seekHook(void* user) {
  const uint32_t serial = static_cast<SeekContext*>(user)->serial;
  if (commandSerial != serial) return 1;
  vTaskDelay(1); // yield long stsc/stsz traversals, also while paused/disconnected
  return commandSerial != serial;
}
static void finishCommand(uint32_t serial) {
  portENTER_CRITICAL(&stateLock);
  if (serial == commandSerial) seeking = false;
  portEXIT_CRITICAL(&stateLock);
}
static void seekTrack(uint32_t ms, uint32_t serial) {
  requestFlush();
  SeekContext context{serial};
  const uint32_t at = millis();
  Serial.printf("seek: starting track %d target %lu\n",int(curTrack),(unsigned long)ms);
  const miniaac_status rc = m4a.seek(ms, seekHook, &context);
  portENTER_CRITICAL(&stateLock);
  if (commandSerial != serial || rc == MINIAAC_CANCELLED){portEXIT_CRITICAL(&stateLock);return;}
  if (rc != MINIAAC_OK) {decodeBlocked = true; playing = false;}
  else {
    posBaseMs = uint32_t(min<uint64_t>(uint64_t(ms) * 44100 / 1000, m4a.totalFrames() - 1) * 1000 / 44100);
    framesPlayed = 0; starting = true;decodeBlocked = false;
  }
  portEXIT_CRITICAL(&stateLock);
  if (rc != MINIAAC_OK) {
    reportError(m4a.name(), curTrack);
    Serial.printf("seek: failed %s target %lu took %lu ms heap %u\n", m4a.name(), (unsigned long)ms, (unsigned long)(millis()-at), ESP.getFreeHeap());
  } else {
    Serial.printf("seek: track %d target %lu actual %lu took %lu ms heap %u\n",
                  int(curTrack), (unsigned long)ms, (unsigned long)posBaseMs,
                  (unsigned long)(millis() - at), ESP.getFreeHeap());
  }
  finishCommand(serial);
}

static void closeTrack() {
  canSeek = false;
  m4a.end();                                  // before the File goes
  if (file) file.close();
}

// Decode task is the sole owner of this File and decoder, including seeks.
static bool openTrack(int index) {
  const int count = storage_trackCount();
  if (count == 0) return false;
  index = ((index % count) + count) % count;

  starting = true;
  closeTrack();
  requestFlush();

  canSeek = false; decodeBlocked = false;
  curTrack = index;
  durationMs = 0;
  posBaseMs = 0;
  framesPlayed = 0;
  const String path = storage_trackPath(index);
  file = openSmallFile(path);
  if (!file) {
    reportError("cannot_open", index);
    Serial.printf("ERR m4a cannot_open %s\n", path.c_str());
    trackSerial++;
    return false;
  }
  if (m4a.begin(file) != MINIAAC_OK) {
    reportError(m4a.name(), index);
    Serial.printf("ERR m4a %s %s\n", m4a.name(), path.c_str());
    file.close();
    trackSerial++;
    return false;
  }
  durationMs = (uint32_t)(m4a.totalFrames() * 1000 / 44100);
  canSeek = m4a.seekable();
  trackSerial++;
  Serial.printf("audio: [%d/%d] %s  %u KB  %lu:%02lu  heap %u\n", index + 1, count, path.c_str(),
                (unsigned)(file.size() / 1024), durationMs / 60000, (durationMs / 1000) % 60,
                ESP.getFreeHeap());
  return true;
}

// ---------------------------------------------------------------------------
// Decode task
// ---------------------------------------------------------------------------
// It opens what it is asked to open and reports when a track has run out; it
// never decides what plays next. Play order (shuffle, repeat) is the UI task's
// business in audio_tick(), so the order table needs no locking.
static volatile uint32_t endedCommand=0, failedCommand=0;
static volatile uint32_t endedSerial = 0;     // bumped when a track finishes
static volatile uint32_t failSerial  = 0;     // bumped when a track will not open
static volatile uint32_t diagMaxReadMs = 0;   // diagnostics: slowest SD read

static void decodeTask(void*) {
  // All permanent audio allocations and playlist restoration precede BT.
  // The loop task releases this barrier only after playlist_restore().
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  if (reqTrack < 0) startupComplete = true; // empty library
  for (;;) {
    abortDecode = false;
    portENTER_CRITICAL(&stateLock);
    const int t = reqTrack;
    const int32_t seek = reqSeekMs;
    const uint32_t startMs = reqStartMs, serial = commandSerial;
    reqTrack = -1; reqSeekMs = -1;
    portEXIT_CRITICAL(&stateLock);
    if (t >= 0) {
      Serial.printf("cmd: track %d consumed serial %lu\n",t,(unsigned long)serial);
      const bool opened = openTrack(t);
      if (serial == commandSerial) {
        if (!opened) { failedCommand=serial; failSerial++; finishCommand(serial); }
        else if (startMs && canSeek) seekTrack(startMs, serial);
        else finishCommand(serial);
      }
      startupComplete = true;
      continue;
    }
    if (seek >= 0) { seekTrack(uint32_t(seek), serial); continue; }
    if (!file || decodeBlocked || seeking) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }

    if (m4a.pending() == 0) {
      const uint32_t r0 = millis();
      const CodecTrack::Pull pull = m4a.next();
      { const uint32_t took = millis() - r0; if (took > diagMaxReadMs) diagMaxReadMs = took; }
      if (pull == CodecTrack::Pull::error) {
        reportError(m4a.name(), curTrack);
        Serial.printf("ERR m4a %s %s\n", m4a.name(), storage_trackPath(curTrack).c_str());
        closeTrack();
        failedCommand=serial; failSerial++;
        continue;
      }
      if (pull == CodecTrack::Pull::eof) {
        // End of track: let what is buffered play out, then say so.
        while ((rbUsed() > 0 || !playing) && !requestPending()) vTaskDelay(pdMS_TO_TICKS(10));
        if (!requestPending()) {
          closeTrack();
          endedCommand=serial; endedSerial++;
        }
        continue;
      }
    }
    while (m4a.pending() > 0 && !abortDecode) {
      const M4aStereo* f = m4a.pcm();
      pushFrame(f->l, f->r);
      if (abortDecode) break;
      m4a.consume(1);
    }
  }
}

// ---------------------------------------------------------------------------
// Play order (UI task only)
// ---------------------------------------------------------------------------
static uint16_t* order = nullptr;             // play position -> track index
static int       orderPos = 0;                // -1: playing a song outside the queue
static int       orderLen = 0;
// The list being played (playlist.h), in list order. nullptr: every track.
static const uint16_t* queue = nullptr;

static int queueAt(int pos) { return queue ? queue[pos] : pos; }

static int queueFind(int track) {
  if (!queue) return (track >= 0 && track < orderLen) ? track : -1;
  for (int i = 0; i < orderLen; i++)
    if (queue[i] == track) return i;
  return -1;
}
static bool      shuffleOn = false;
static uint8_t   repeatMode = REPEAT_ALL;
static uint32_t  seenEnded = 0, seenFail = 0;
static int       failStreak = 0;

// The queue in list order, or shuffled with `firstTrack` first. A first track
// outside the queue leaves orderPos at -1: next then starts the list.
static void buildOrder(bool shuffled, int firstTrack) {
  for (int i = 0; i < orderLen; i++) order[i] = queueAt(i);
  const int first = queueFind(firstTrack);
  if (!shuffled || orderLen < 2) {
    orderPos = first;
    return;
  }
  if (first > 0) {
    order[first] = order[0];
    order[0] = firstTrack;
  }
  const int from = first >= 0 ? 1 : 0;           // keep the first in place
  for (int i = orderLen - 1; i > from; i--) {
    const int j = from + esp_random() % (i - from + 1);
    const uint16_t tmp = order[i];
    order[i] = order[j];
    order[j] = tmp;
  }
  orderPos = first >= 0 ? 0 : -1;
}

static void request(int track, uint32_t startMs = 0) {
  portENTER_CRITICAL(&stateLock);
  const int superseded=reqTrack;
  reqStartMs = startMs; seekGoalMs = startMs; reqSeekMs = -1; reqTrack = track;
  ++commandSerial; seeking = true; canSeek = false;
  const uint32_t serial=commandSerial;
  portEXIT_CRITICAL(&stateLock);
  if(superseded>=0)Serial.printf("cmd: track %d superseded by track %d (latest wins)\n",superseded,track);
  Serial.printf("cmd: track %d queued serial %lu start %lu\n",track,(unsigned long)serial,(unsigned long)startMs);
}

static void advance(bool trackFinished) {
  if (orderLen == 0) return;
  if (trackFinished && repeatMode == REPEAT_ONE) {
    request(curTrack);
    return;
  }
  int next = orderPos + 1;
  if (next >= orderLen) {
    if (trackFinished && repeatMode == REPEAT_OFF) {
      // The end of the list: load the first track, paused, ready to go again.
      playing = false;
      next = 0;
    } else if (shuffleOn) {
      buildOrder(true, order[esp_random() % orderLen]);   // a fresh shuffle
      next = 0;
    } else {
      next = 0;
    }
  }
  orderPos = next;
  request(order[orderPos]);
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------
void audio_begin(int startTrack, uint32_t startMs, uint8_t volume, bool shuffle, uint8_t repeat) {
  playing = false;
  rb = (int16_t*)malloc(RB_FRAMES * 2 * sizeof(int16_t));
  orderLen = storage_trackCount();
  // Sized for the whole library: a playlist's queue is never longer.
  order = (uint16_t*)malloc(max(1, orderLen) * sizeof(uint16_t));
  if (!rb || !order) {
    free(rb); free(order); rb = nullptr; order = nullptr; orderLen = 0;
    reportError("audio_memory", -1); return;
  }
  volumePct = min<uint8_t>(volume, 100);
  shuffleOn = shuffle;
  repeatMode = repeat % REPEAT_MODE_COUNT;
  if (orderLen > 0) {
    startTrack = constrain(startTrack, 0, orderLen - 1);
    buildOrder(shuffleOn, startTrack);
    request(startTrack, startMs);
  }
  // CY-37/v3 minimum free stack was >=3964 B on 6144 B: retain ~1.9 KiB observed margin.
  if (xTaskCreatePinnedToCore(decodeTask, "m4adec", 4096, nullptr, 5, &decodeHandle, 1) != pdPASS) {
    free(rb); free(order); rb = nullptr; order = nullptr; orderLen = 0;
    reportError("audio_task", -1);
  }
}

void audio_start() { if (decodeHandle) xTaskNotifyGive(decodeHandle); }
bool audio_startupComplete() { return !decodeHandle || startupComplete; }

void audio_tick() {
  if (endedSerial != seenEnded) {
    seenEnded = endedSerial;
    if(cyd::currentCompletion(endedCommand,commandSerial)){failStreak = 0;advance(true);}
    else Serial.println("cmd: eof ignored (superseded)");
  }
  if (failSerial != seenFail) {
    seenFail = failSerial;
    // Skip unreadable files, but stop rather than spin if none will open.
    if(cyd::currentCompletion(failedCommand,commandSerial)){
      if (++failStreak < orderLen) advance(false);else playing = false;
    }else Serial.println("cmd: failure ignored (superseded)");
  }
}

void audio_playTrack(int index) {
  if (!audio_ready() || orderLen == 0){Serial.println("cmd: play ignored (audio unavailable or empty queue)");return;}
  ++playIntentSerial;
  Serial.printf("cmd: play track %d accepted\n",index);
  index = constrain(index, 0, storage_trackCount() - 1);
  for (int i = 0; i < orderLen; i++)
    if (order[i] == index) { orderPos = i; break; }
  playing = true;
  failStreak = 0;
  request(index);
}

void audio_next() {
  if(!audio_ready()||!orderLen){Serial.println("cmd: next ignored (empty queue)");return;}
  Serial.println("cmd: next accepted");
  failStreak = 0;
  advance(false);
}

void audio_prev() {
  if (!audio_ready()||!orderLen){Serial.println("cmd: previous ignored (empty queue)");return;}
  Serial.println("cmd: previous accepted");
  failStreak = 0;
  if (!requestPending() && !seeking && audio_positionMs() > 3000) { request(curTrack); return; }
  orderPos = orderPos <= 0 ? orderLen - 1 : orderPos - 1;
  request(order[orderPos]);
}

void audio_togglePause() {
  if(!audio_ready()||!orderLen){Serial.println("cmd: play/pause ignored (audio unavailable or empty queue)");return;}
  ++playIntentSerial;
  // A failed seek closes the decode gate; Play must reopen rather than just light up.
  portENTER_CRITICAL(&stateLock);
  const bool retry=!playing&&decodeBlocked;
  playing=!playing;
  const int track=curTrack;
  portEXIT_CRITICAL(&stateLock);
  if(retry)request(track);
  Serial.printf("cmd: %s accepted%s\n",playing?"play":"pause",retry?" (retry after seek failure)":"");
}
void audio_setVolume(uint8_t p) { volumePct = min<uint8_t>(p, 100);Serial.printf("cmd: volume %u executed\n",unsigned(volumePct)); }

void audio_setSleepGain(uint8_t percent){sleepGain=min<uint8_t>(percent,100);}
bool audio_canSeek() { return canSeek && durationMs && reqTrack < 0; }
bool audio_isSeeking() { return seeking; }
bool audio_seekTo(uint32_t ms) {
  portENTER_CRITICAL(&stateLock);
  if (!canSeek || !durationMs || reqTrack >= 0) { portEXIT_CRITICAL(&stateLock); Serial.println("cmd: seek ignored (track opening or unsupported)");return false; }
  seekGoalMs = min<uint32_t>(ms, min<uint32_t>(durationMs - 1, INT32_MAX));
  reqSeekMs = int32_t(seekGoalMs);
  ++commandSerial; seeking = true;
  const uint32_t goal=seekGoalMs,serial=commandSerial;
  portEXIT_CRITICAL(&stateLock);
  Serial.printf("cmd: seek %lu queued serial %lu (latest wins)\n",(unsigned long)goal,(unsigned long)serial);
  return true;
}
void audio_seekRelative(int seconds) {
  portENTER_CRITICAL(&stateLock);
  const int64_t base = seeking ? seekGoalMs : posBaseMs + uint64_t(framesPlayed) * 1000 / 44100;
  portEXIT_CRITICAL(&stateLock);
  audio_seekTo(uint32_t(max<int64_t>(0, min<int64_t>(INT32_MAX, base + int64_t(seconds) * 1000))));
}

void audio_setShuffle(bool on) {
  Serial.printf("cmd: shuffle %s %s\n",on?"on":"off",orderLen?"executed":"ignored (empty queue)");
  if (on == shuffleOn || orderLen == 0) return;
  shuffleOn = on;
  const int wanted=reqTrack>=0?int(reqTrack):int(curTrack);
  buildOrder(on, wanted);
}

void audio_setQueue(const uint16_t* tracks, int n, int keepTrack, bool jump) {
  if (n <= 0 || !audio_ready()) return;
  queue = tracks;
  orderLen = min(n, storage_trackCount());
  buildOrder(shuffleOn, keepTrack);
  if (orderPos < 0 && jump) {
    orderPos = 0;
    failStreak = 0;
    request(order[0]);
  }
}

int audio_queueLen()             { return orderLen; }
int audio_queueTrack(int pos)    { return (pos >= 0 && pos < orderLen) ? queueAt(pos) : -1; }
int audio_queuePos(int track)    { return queueFind(track); }

void audio_setRepeat(uint8_t mode) { repeatMode = mode % REPEAT_MODE_COUNT;Serial.printf("cmd: repeat %u executed\n",unsigned(repeatMode)); }
bool    audio_shuffle()            { return shuffleOn; }
uint8_t audio_repeat()             { return repeatMode; }

uint32_t audio_playIntentSerial(){return playIntentSerial;}
bool     audio_isPlaying()    { return playing; }
int      audio_currentTrack() { return curTrack; }
uint8_t  audio_volume()       { return volumePct; }
uint32_t audio_durationMs()   { return durationMs; }
uint32_t audio_trackSerial()  { return trackSerial; }
uint32_t audio_underruns()    { return underruns; }
uint32_t audio_takeMaxReadMs() { const uint32_t v = diagMaxReadMs; diagMaxReadMs = 0; return v; }
int32_t  audio_maxRequest()   { return maxRequest; }
uint32_t audio_pullCalls()    { return pullCalls; }
uint32_t audio_pullFrames()   { return pullFrames; }
uint8_t  audio_bufferPct()    { return rbUsed() * 100 / RB_FRAMES; }
bool     audio_bufferLow()    { return playing && rbUsed() < RB_FRAMES / 2; }

uint32_t audio_positionMs() {
  portENTER_CRITICAL(&stateLock);
  const uint32_t ms = posBaseMs + (uint32_t)((uint64_t)framesPlayed * 1000 / 44100);
  const uint32_t duration = durationMs;
  portEXIT_CRITICAL(&stateLock);
  return (duration && ms > duration) ? duration : ms;
}

AudioCommands audio_commands(){
  portENTER_CRITICAL(&stateLock);
  AudioCommands out{int(reqTrack),int32_t(reqSeekMs),uint32_t(commandSerial),bool(starting),bool(seeking),bool(decodeBlocked)};
  portEXIT_CRITICAL(&stateLock);return out;
}
uint32_t audio_decodeStackFree() {
  return decodeHandle ? uxTaskGetStackHighWaterMark(decodeHandle) : 0;
}

#endif  // !CYD_UPLOADER
