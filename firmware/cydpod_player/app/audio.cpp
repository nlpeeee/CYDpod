// Player image only (firmware.h).
#ifndef CYD_UPLOADER

#include "audio.h"
#include "command_queue.h"
#include "storage.h"
#include "settings.h"     // RepeatMode
#include "codec_track.h"
#include "small_file.h"
#include <esp_timer.h>

// ---------------------------------------------------------------------------
// Ring buffer: one producer (decode task), one consumer (audio_pull)
// ---------------------------------------------------------------------------
// The default 8 KiB holds ~46 ms. Compact FLAC can trial 16 KiB (~93 ms).
#ifdef CYDPOD_PCM_RING_4096
static constexpr uint32_t RB_FRAMES = 4096;
#else
static constexpr uint32_t RB_FRAMES = 2048;
#endif
static constexpr uint32_t PCM_BATCH_FRAMES = 128;
static_assert(sizeof(M4aStereo) == 2 * sizeof(int16_t), "packed stereo PCM");
static int16_t*          rb = nullptr;
static volatile uint32_t rbHead = 0;          // producer only
static volatile uint32_t rbTail = 0;          // consumer only
// One short lock covers ring publication, consumer copies, commands and clock epochs.
// Never hold it across SD/decoder operations or waits.
static portMUX_TYPE stateLock = portMUX_INITIALIZER_UNLOCKED;
static bool producerWaiting = false; // armed under stateLock only when the ring is full

// All diagnostics, including 64-bit SD totals, are copied/reset under stateLock.
static AudioDiagnosticWindow diagnostic;
static uint64_t flacSdTotalUs = 0; // lifetime total: window resets cannot split next() accounting
static int64_t pullPreviousUs = 0;
static uint32_t pullBurstFrames = 0;
static bool pullTaskLatched = false, flacOpen = false;
static_assert(sizeof(diagnostic) + sizeof(flacSdTotalUs) + sizeof(pullPreviousUs) +
              sizeof(pullBurstFrames) + sizeof(pullTaskLatched) + sizeof(flacOpen) < 200, "diagnostic RAM budget");
void audio_flacReadTime(uint32_t us) {
  portENTER_CRITICAL(&stateLock);
  auto& f = diagnostic.flac;
  f.active = true; f.sdSumUs += us; flacSdTotalUs += us;
  if (us > f.sdMaxUs) f.sdMaxUs = us;
  if (us > 10000) ++f.readsGt10;
  portEXIT_CRITICAL(&stateLock);
}
AudioDiagnosticWindow audio_takeDiagnosticWindow() {
  portENTER_CRITICAL(&stateLock);
  const AudioDiagnosticWindow result = diagnostic;
  diagnostic.flac = AudioFlacStats{}; diagnostic.flac.active = flacOpen;
  // Identity and preceding-call time survive; bursts are segmented at window boundaries.
  diagnostic.pull.gapMaxUs = diagnostic.pull.burstFrames = diagnostic.pull.lockWaitUs = 0;
  diagnostic.pull.lockHoldUs = diagnostic.pull.missingFrames = diagnostic.pull.calls = 0;
  pullBurstFrames = 0;
  portEXIT_CRITICAL(&stateLock);
  return result;
}

static inline uint32_t rbUsed() { return (rbHead - rbTail) & (RB_FRAMES - 1); }
static inline uint32_t rbFree() { return RB_FRAMES - 1 - rbUsed(); }

// ---------------------------------------------------------------------------
// Shared state
// ---------------------------------------------------------------------------
static volatile int      reqTrack  = -1;      // UI -> decode task
static volatile int32_t  reqSeekMs = -1;
static volatile uint32_t reqStartMs = 0, commandSerial = 0, seekGoalMs = 0;
static volatile bool seeking = false, canSeek = false;
static volatile bool decodeBlocked = false; // failed open/decode/seek: decoder needs reopen
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
static volatile bool     starting = false;    // opening a track: no underrun counting; clears on the first PULLED frame
static volatile bool     opening = false;     // decodeTask is inside openTrack/seek for a consumed request (CY-118)
static volatile uint32_t lastPullMs = 0;

static TaskHandle_t decodeHandle = nullptr;
static bool decoderReleased = false; // control task only, keeps pre-start requests behind the startup barrier
static void wakeDecoder() { if (decoderReleased && decodeHandle) xTaskNotifyGive(decodeHandle); }
static volatile bool startupComplete = false;
static volatile uint32_t pcmInputRate=0, pcmMaxPumpMs=0, pcmYields=0;
static volatile uint32_t pcmBlockUs=0, pcmMaxBlockUs=0;
static uint32_t pcmSinkWaitUs=0; // producer-only, reset for each PCM block
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
static volatile uint32_t ringLow = RB_FRAMES;  // CY-119: fewest frames left in the ring after a pull, since the last stat: audio line

void audio_pull(int16_t* out, int32_t frames) {
  lastPullMs = millis();
  pullCalls++;
  if (frames > maxRequest) maxRequest = frames;
#ifndef CYDPOD_LIGHT_AUDIO_DIAGNOSTICS
  const int64_t callAt = esp_timer_get_time();
#endif
  portENTER_CRITICAL(&stateLock);
#ifndef CYDPOD_LIGHT_AUDIO_DIAGNOSTICS
  const int64_t lockedAt = esp_timer_get_time();
#endif
  uint32_t missing = 0;
  int32_t gain = 0, copied = 0;
  bool wake = false;
  if (rb && playing && !seeking) {
    const int32_t volume = volumePct;
    gain = volume * volume * sleepGain / 100; // perceptual-ish, /10000
    const uint32_t tail = rbTail;
    copied = min<int32_t>(frames, rbUsed());
    const uint32_t first = min<uint32_t>(copied, RB_FRAMES - tail);
    memcpy(out, rb + tail * 2, first * 2 * sizeof(int16_t));
    if (first < uint32_t(copied))
      memcpy(out + first * 2, rb, (copied - first) * 2 * sizeof(int16_t));
    rbTail = (tail + copied) & (RB_FRAMES - 1);
    framesPlayed += copied;
    pullFrames += copied;
    if (copied > 0) {
      starting = false;
      wake = producerWaiting;
      producerWaiting = false;
    }
    const uint32_t left = rbUsed();
    if (!starting && framesPlayed >= RB_FRAMES && left < ringLow) ringLow = left;
    if (copied < frames && !starting && reqTrack < 0 && reqSeekMs < 0) underruns++;
    missing = uint32_t(frames - copied); // excludes intentional paused/seeking silence
  }
#ifdef CYDPOD_LIGHT_AUDIO_DIAGNOSTICS
  ++diagnostic.pull.calls;
  diagnostic.pull.missingFrames += missing;
  portEXIT_CRITICAL(&stateLock);
#else
  portEXIT_CRITICAL(&stateLock);
  // One timer read inside the PCM lock. Hold is an upper bound: includes unlock
  // and any preemption before this outside read; diagnostic publication is separate.
  const uint32_t holdUs = uint32_t(esp_timer_get_time() - lockedAt);
  portENTER_CRITICAL(&stateLock);
  auto& p = diagnostic.pull;
  if (!pullTaskLatched) {
    const char* name = pcTaskGetName(nullptr);
    for (unsigned k = 0; k + 1 < sizeof(p.task) && name[k]; ++k) p.task[k] = name[k];
    p.core = xPortGetCoreID(); p.prio = uxTaskPriorityGet(nullptr); pullTaskLatched = true;
  }
  const uint32_t gap = uint32_t(callAt - pullPreviousUs);
  if (pullPreviousUs && gap > p.gapMaxUs) p.gapMaxUs = gap;
  pullBurstFrames = pullPreviousUs && gap < 1000 ? pullBurstFrames + uint32_t(frames) : uint32_t(frames);
  if (pullBurstFrames > p.burstFrames) p.burstFrames = pullBurstFrames;
  pullPreviousUs = callAt; ++p.calls;
  const uint32_t waitUs = uint32_t(lockedAt - callAt);
  if (waitUs > p.lockWaitUs) p.lockWaitUs = waitUs;
  p.missingFrames += missing;
  if (holdUs > p.lockHoldUs) p.lockHoldUs = holdUs;
  portEXIT_CRITICAL(&stateLock);
#endif
  if (wake) xTaskNotifyGive(decodeHandle);
  for (int32_t i = 0; i < copied * 2; ++i)
    out[i] = int16_t(int32_t(out[i]) * gain / 10000);
  memset(out + copied * 2, 0, (frames - copied) * 2 * sizeof(int16_t));
}

// ---------------------------------------------------------------------------
// Producer: resampler and PCM callback
// ---------------------------------------------------------------------------
// Called by the decoder with at most PCM_BATCH_FRAMES frames. Commands and
// publication share a lock, so a seek or track change can invalidate a batch.
static bool pushFrames(const M4aStereo* pcm, uint32_t count) {
  while (count) {
    portENTER_CRITICAL(&stateLock);
    if (requestPending()) {
      producerWaiting = false;
      portEXIT_CRITICAL(&stateLock);
      return false;
    }
    const uint32_t n = min(count, rbFree());
    if (n) {
      const uint32_t head = rbHead;
      const uint32_t first = min(n, RB_FRAMES - head);
      memcpy(rb + head * 2, pcm, first * sizeof(*pcm));
      if (first < n) memcpy(rb, pcm + first, (n - first) * sizeof(*pcm));
      rbHead = (head + n) & (RB_FRAMES - 1);
      producerWaiting = false;
      portEXIT_CRITICAL(&stateLock);
      pcm += n;
      count -= n;
      continue;
    }
    producerWaiting = true;
    portEXIT_CRITICAL(&stateLock);
    const uint32_t waitAt = micros();
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    pcmSinkWaitUs += micros() - waitAt;
  }
  return true;
}

struct PcmBatch {
  M4aStereo frames[PCM_BATCH_FRAMES];
  uint32_t used = 0;
  bool flush() {
    const uint32_t count = used;
    used = 0;
    return pushFrames(frames, count);
  }
  static bool sink(void* user, int16_t l, int16_t r) {
    auto& batch = *static_cast<PcmBatch*>(user);
    batch.frames[batch.used++] = {l, r};
    return batch.used < PCM_BATCH_FRAMES || batch.flush();
  }
};
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
static void requireReopen(uint32_t serial) {
  portENTER_CRITICAL(&stateLock);
  if (serial == commandSerial) decodeBlocked = true;
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
    posBaseMs = m4a.seekPositionMs();
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
  portENTER_CRITICAL(&stateLock); flacOpen = false; portEXIT_CRITICAL(&stateLock);
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

  canSeek = false; decodeBlocked = false; pcmInputRate=0;
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
  portENTER_CRITICAL(&stateLock);
  flacOpen = m4a.isFlac(); diagnostic.flac.active |= flacOpen;
  portEXIT_CRITICAL(&stateLock);
  durationMs = (uint32_t)(m4a.totalFrames() * 1000 / m4a.sampleRate());
  canSeek = m4a.seekable();pcmInputRate=m4a.sampleRate();
  Serial.printf("cmd: codec input %lu output 44100 seekable %d\n",(unsigned long)pcmInputRate,int(canSeek));
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
// business in audio_tick(), so the order state needs no locking.
static volatile uint32_t endedCommand=0, failedCommand=0;
static volatile uint32_t endedSerial = 0;     // bumped when a track finishes
static volatile uint32_t failSerial  = 0;     // bumped when a track will not open
static volatile uint32_t diagMaxReadMs = 0;   // diagnostics: slowest whole next()

static void decodeTask(void*) {
  // All permanent audio allocations and playlist restoration precede BT.
  // The loop task releases this barrier only after playlist_restore().
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  if (reqTrack < 0) startupComplete = true; // empty library
  PcmBatch batch;
  for (;;) {
    portENTER_CRITICAL(&stateLock);
    const int t = reqTrack;
    const int32_t seek = reqSeekMs;
    const uint32_t startMs = reqStartMs, serial = commandSerial;
    if (t >= 0) opening = true;  // in the same critical section: no gap in which the request is neither queued nor opening
    reqTrack = -1; reqSeekMs = -1;
    portEXIT_CRITICAL(&stateLock);
    if (t >= 0) {
      Serial.printf("cmd: track %d consumed serial %lu\n",t,(unsigned long)serial);
      const bool opened = openTrack(t);
      if (serial == commandSerial) {
        if (!opened) { requireReopen(serial); failedCommand=serial; failSerial++; finishCommand(serial); }
        else if (startMs && canSeek) seekTrack(startMs, serial);
        else finishCommand(serial);
      }
      startupComplete = true;
      opening = false;
      continue;
    }
    if (seek >= 0) {
      if (file) seekTrack(uint32_t(seek), serial);
      else { Serial.println("cmd: seek ignored (no open track)"); finishCommand(serial); } // raced with closeTrack()
      continue;
    }
    if (!file || decodeBlocked || seeking) { ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20)); continue; }

    if (m4a.pending() == 0) {
#ifndef CYDPOD_LIGHT_AUDIO_DIAGNOSTICS
      const bool flac = m4a.isFlac(), frameDue = m4a.flacNeedsFrame();
      portENTER_CRITICAL(&stateLock);
      const uint64_t sdBefore = flacSdTotalUs;
      const uint32_t ringStart = rbUsed();
      portEXIT_CRITICAL(&stateLock);
      const int64_t nextAt = esp_timer_get_time();
#endif
      const uint32_t r0 = millis();
      const CodecTrack::Pull pull = m4a.next();
      const uint32_t took = millis() - r0;
#ifndef CYDPOD_LIGHT_AUDIO_DIAGNOSTICS
      const uint32_t nextUs = uint32_t(esp_timer_get_time() - nextAt);
#endif
      portENTER_CRITICAL(&stateLock);
      if (took > diagMaxReadMs) diagMaxReadMs = took;
#ifndef CYDPOD_LIGHT_AUDIO_DIAGNOSTICS
      if (flac) {
        auto& f = diagnostic.flac; f.active = true;
        const uint64_t sdUs = flacSdTotalUs - sdBefore;
        const uint32_t nonIoUs = sdUs < nextUs ? nextUs - uint32_t(sdUs) : 0;
        if (nextUs > f.nextMaxUs) f.nextMaxUs = nextUs;
        if (nonIoUs > f.nonIoMaxUs) f.nonIoMaxUs = nonIoUs;
        if (frameDue && pull == CodecTrack::Pull::frames && m4a.pending()) {
          ++f.frames; f.block = m4a.flacBlockSize();
          if (ringStart < f.ringStartMin) f.ringStartMin = ringStart;
        }
      }
#endif
      portEXIT_CRITICAL(&stateLock);
      if (pull == CodecTrack::Pull::error) {
        reportError(m4a.name(), curTrack);
        Serial.printf("ERR m4a %s %s\n", m4a.name(), storage_trackPath(curTrack).c_str());
        closeTrack();
        requireReopen(serial);
        failedCommand=serial; failSerial++;
        continue;
      }
      if (pull == CodecTrack::Pull::eof) {
        if (!m4a.drain(PcmBatch::sink, &batch) || !batch.flush()) continue;
        // End of track: let what is buffered play out, then say so.
        while ((rbUsed() > 0 || !playing) && !requestPending()) ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
        if (!requestPending()) {
          closeTrack();
          endedCommand=serial; endedSerial++;
        }
        continue;
      }
    }
    if (m4a.sampleRate() == 44100) {
      while (m4a.pending()) {
        const uint32_t n = min<size_t>(m4a.pending(), PCM_BATCH_FRAMES);
        if (!pushFrames(m4a.pcm(), n)) break;
        m4a.consume(n);
      }
      continue;
    }

    const uint32_t pumpAt = millis(), pumpAtUs = micros();
    pcmSinkWaitUs = 0;
    while (m4a.pending()) {
      const M4aStereo* f = m4a.pcm();
      if (!m4a.output(f->l, f->r, PcmBatch::sink, &batch)) break;
      m4a.consume(1);
    }
    if (m4a.pending() || !batch.flush()) continue; // superseded by a command
    pcmBlockUs = micros() - pumpAtUs - pcmSinkWaitUs;
    if (pcmBlockUs > pcmMaxBlockUs) pcmMaxBlockUs = pcmBlockUs;
    const uint32_t took = millis() - pumpAt;
    if (took > pcmMaxPumpMs) pcmMaxPumpMs = took;
    // FIR work can be slower than the consumer, leaving no ring-space wait.
    // Sleep one tick so the lower-priority loop and idle can run on core 1.
    ++pcmYields;
    vTaskDelay(1);
  }
}

// ---------------------------------------------------------------------------
// Play order (UI task only)
// ---------------------------------------------------------------------------
static AudioOrder order;                     // play position -> queue position
static int       orderPos = 0;                // -1: playing a song outside the queue
static int       orderLen = 0;
// The list being played (playlist.h), in list order. nullptr: every track.
static AudioQueueSource queue;

static int queueAt(int pos) {
  if (pos < 0 || pos >= orderLen) return -1;
  const int id = queue.at ? queue.at(queue.context, pos) : queue.tracks ? queue.tracks[pos] : pos;
  if (id < 0 || id >= storage_trackCount()) {
    playing = false; orderLen = 0;
    reportError("queue_unavailable", -1);
    Serial.println("queue: SD unavailable; playback stopped");
    return -1;
  }
  return id;
}
static int queueFind(int track) {
  if (track < 0) return -1;
  for (int i = 0; i < orderLen; ++i) if (queueAt(i) == track) return i;
  return -1;
}
static bool      shuffleOn = false;
static uint8_t   repeatMode = REPEAT_ALL;
static uint32_t  seenEnded = 0, seenFail = 0;
static int       failStreak = 0;

// The queue in list order, or shuffled with `firstTrack` first. A first track
// outside the queue leaves orderPos at -1: next then starts the list.
static void buildOrder(bool shuffled, int firstTrack) {
  const int first = queueFind(firstTrack);
  order.reset(orderLen, shuffled, first, esp_random());
  orderPos = order.position(first);
}

static void request(int track, uint32_t startMs = 0) {
  if (track < 0) return; // queueAt already published the unavailable-source error
  portENTER_CRITICAL(&stateLock);
  const int superseded=reqTrack;
  reqStartMs = startMs; seekGoalMs = startMs; reqSeekMs = -1; reqTrack = track;
  ++commandSerial; seeking = true; canSeek = false;
  // A queued/in-flight open now owns recovery; an old failure must not redirect Play.
  decodeBlocked = false;
  const uint32_t serial=commandSerial;
  portEXIT_CRITICAL(&stateLock);
  wakeDecoder();
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
    } else {
      next = 0;
    }
  }
  orderPos = next;
  request(queueAt(order.at(orderPos)));
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------
void audio_begin(int startTrack, uint32_t startMs, uint8_t volume, bool shuffle, uint8_t repeat) {
  playing = false;
  Serial.printf("cmd: audio config ring=%u flac=%s diagnostics=%s\n", unsigned(RB_FRAMES),
#ifdef CYDPOD_FLAC_LEGACY
                "legacy",
#else
                "compact",
#endif
#ifdef CYDPOD_LIGHT_AUDIO_DIAGNOSTICS
                "light");
#else
                "full");
#endif
  rb = (int16_t*)malloc(RB_FRAMES * 2 * sizeof(int16_t));
  orderLen = storage_trackCount();
  if (!rb) {
    orderLen = 0;
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
    free(rb); rb = nullptr; orderLen = 0;
    reportError("audio_task", -1);
  }
}

void audio_start() {
  decoderReleased = true;
  wakeDecoder();
}
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
  orderPos = order.position(queueFind(index));
  if (!orderLen) return; // a failed cursor read stopped playback
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
  request(queueAt(order.at(orderPos)));
}

static void setPlaying(bool on) {
  if(!audio_ready()||!orderLen){Serial.println("cmd: play/pause ignored (audio unavailable or empty queue)");return;}
  // Any terminal decoder failure needs an open, not just a change to the Play button.
  portENTER_CRITICAL(&stateLock);
  const bool retry=on&&!playing&&decodeBlocked&&!requestPending();
  playing=on;
  const int track=curTrack;
  portEXIT_CRITICAL(&stateLock);
  if(retry){failStreak=0;request(track);}
  Serial.printf("cmd: %s accepted%s\n",on?"play":"pause",retry?" (retry after decoder failure)":"");
}
void audio_setPlaying(bool on) { ++playIntentSerial; setPlaying(on); }
void audio_togglePause() { audio_setPlaying(!audio_isPlaying()); }
void audio_pauseForPairing() { setPlaying(false); }
void audio_resumeAfterPairing() { setPlaying(true); }
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
  wakeDecoder();
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
  audio_setQueueSource({tracks, nullptr, nullptr}, n, keepTrack, jump);
}

void audio_setQueueSource(AudioQueueSource source, int n, int keepTrack, bool jump) {
  if (n <= 0 || !audio_ready()) return;
  queue = source;
  orderLen = min(n, storage_trackCount());
  buildOrder(shuffleOn, keepTrack);
  if (orderLen > 0 && orderPos < 0 && jump) {
    orderPos = 0;
    failStreak = 0;
    request(queueAt(order.at(0)));
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
uint32_t audio_takeMaxReadMs() { portENTER_CRITICAL(&stateLock); const uint32_t v = diagMaxReadMs; diagMaxReadMs = 0; portEXIT_CRITICAL(&stateLock); return v; }
uint32_t audio_takeRingLow() {
  portENTER_CRITICAL(&stateLock);
  const uint32_t v = ringLow;
  ringLow = RB_FRAMES;
  portEXIT_CRITICAL(&stateLock);
  return v;
}
uint32_t audio_ringFrames() { return RB_FRAMES; }
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
  AudioCommands out{int(reqTrack),int32_t(reqSeekMs),uint32_t(commandSerial),bool(opening),bool(seeking),bool(decodeBlocked)};
  portEXIT_CRITICAL(&stateLock);return out;
}
uint32_t audio_decodeStackFree() {
  return decodeHandle ? uxTaskGetStackHighWaterMark(decodeHandle) : 0;
}

AudioPcmStats audio_pcmStats() { return {pcmInputRate,pcmMaxPumpMs,pcmYields,pcmBlockUs,pcmMaxBlockUs}; }

#endif  // !CYD_UPLOADER
