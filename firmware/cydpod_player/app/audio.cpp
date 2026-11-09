// Player image only (firmware.h).
#ifndef CYD_UPLOADER

#include "audio.h"
#include "command_queue.h"
#include "storage.h"
#include "settings.h"     // RepeatMode
#include "codec_track.h"
#include "eq.h"
#include "small_file.h"
#include <esp_timer.h>

// ---------------------------------------------------------------------------
// Ring buffer: one producer (decode task), one consumer (audio_pull)
// ---------------------------------------------------------------------------
// The default 8 KiB holds ~46 ms. Compact FLAC can trial 16 KiB (~93 ms), or
// any size with CYDPOD_PCM_RING_FRAMES (6144 frames = 24 KiB, ~139 ms, once the
// trimmed Bluedroid freed the heap). Sizes need not be powers of two.
#if defined(CYDPOD_PCM_RING_FRAMES)
static constexpr uint32_t RB_FRAMES = CYDPOD_PCM_RING_FRAMES;
#elif defined(CYDPOD_PCM_RING_4096)
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

static_assert(RB_FRAMES >= 2 * PCM_BATCH_FRAMES, "PCM ring too small");
static inline uint32_t rbUsed() { const uint32_t h = rbHead, t = rbTail; return h >= t ? h - t : h + RB_FRAMES - t; }
static inline uint32_t rbAdvance(uint32_t i, uint32_t n) { i += n; return i >= RB_FRAMES ? i - RB_FRAMES : i; } // n < RB_FRAMES
static inline uint32_t rbFree() { return RB_FRAMES - 1 - rbUsed(); }

// ---------------------------------------------------------------------------
// Shared state
// ---------------------------------------------------------------------------
static volatile int      reqTrack  = -1;      // UI -> decode task
static volatile bool     reqContinuous = false; // reqTrack follows a natural end: keep the old tail playing
static volatile int32_t  reqSeekMs = -1;
static volatile uint32_t reqStartMs = 0, commandSerial = 0, seekGoalMs = 0;
static volatile bool seeking = false, canSeek = false;
static volatile bool decodeBlocked = false; // failed open/decode/seek: decoder needs reopen
static inline bool requestPending() { return reqTrack >= 0 || reqSeekMs >= 0; }
static volatile bool     playing   = true;
static uint32_t playIntentSerial=0; // loop task: explicit play/pause/selection intent
static volatile uint8_t  volumePct = 40;
static volatile bool remoteVolume=false;
static uint32_t volumeRequestSerial=0;
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
// Gapless: a natural track end hands the next file to the decoder while the old
// tail is still in the ring. carryFrames counts those old frames after the new
// track opens, so the position clock only starts on the new track's own audio.
static volatile bool     gaplessOn = true;
static volatile uint32_t carryFrames = 0;
static volatile bool     gapArmed = false;    // EOF handed off; waiting for the new track's first frame
static volatile bool     gapOpened = false;   // ...and the next track has opened behind the old tail
static volatile uint32_t gapFrames = 0, gapLast = 0, gapSerial = 0;

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
    const int32_t volume = remoteVolume ? 100 : volumePct;
    gain = volume * volume * sleepGain / 100; // perceptual-ish, /10000
    const uint32_t tail = rbTail;
    copied = min<int32_t>(frames, rbUsed());
    const uint32_t first = min<uint32_t>(copied, RB_FRAMES - tail);
    memcpy(out, rb + tail * 2, first * 2 * sizeof(int16_t));
    if (first < uint32_t(copied))
      memcpy(out + first * 2, rb, (copied - first) * 2 * sizeof(int16_t));
    rbTail = rbAdvance(tail, copied);
    const uint32_t carry = carryFrames, carried = uint32_t(copied) < carry ? uint32_t(copied) : carry;
    carryFrames = carry - carried;
    framesPlayed += uint32_t(copied) - carried;
    pullFrames += copied;
    if (copied > 0) {
      wake = producerWaiting;
      producerWaiting = false;
    }
    if (uint32_t(copied) > carried) starting = false;
    const bool bridging = gapArmed; // a boundary's silence is reported as a gap, not an underrun
    if (gapArmed) {
      gapFrames += uint32_t(frames - copied);
      // Once the next track is open, the first frame past the old tail is its own.
      if (gapOpened && uint32_t(copied) > carried) { gapLast = gapFrames; gapArmed = gapOpened = false; ++gapSerial; }
    }
    const uint32_t left = rbUsed();
    if (!starting && framesPlayed >= RB_FRAMES && left < ringLow) ringLow = left;
    if (copied < frames && !starting && !bridging && reqTrack < 0 && reqSeekMs < 0) underruns++;
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
static bool publishFrames(const M4aStereo* pcm, uint32_t count) {
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
      rbHead = rbAdvance(head, n);
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

// EQ (eq.h) on the decoder task, outside the lock, before PCM enters the ring.
// Bypass (off or flat) publishes the decoder's PCM untouched.
static bool pushFrames(const M4aStereo* pcm, uint32_t count) {
  if (eq_bypass()) return publishFrames(pcm, count);
  while (count) {
    const uint32_t n = min(count, EqChunkFrames);
    const int16_t* out = eq_process(reinterpret_cast<const int16_t*>(pcm), n);
    if (!publishFrames(reinterpret_cast<const M4aStereo*>(out), n)) return false;
    pcm += n;
    count -= n;
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

static void requestFlush() { // decoder task only
  portENTER_CRITICAL(&stateLock);
  rbTail = rbHead; framesPlayed = 0; carryFrames = 0; gapArmed = gapOpened = false;
  portEXIT_CRITICAL(&stateLock);
  eq_flush();
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
static bool openTrack(int index, bool continuous) {
  const int count = storage_trackCount();
  if (count == 0) return false;
  index = ((index % count) + count) % count;

  starting = true;
  closeTrack();
  if (continuous) {
    // Gapless: the old tail plays on; the clock restarts after it.
    portENTER_CRITICAL(&stateLock);
    carryFrames = rbUsed(); framesPlayed = 0; posBaseMs = 0; gapOpened = gapArmed;
    portEXIT_CRITICAL(&stateLock);
  } else {
    requestFlush();
  }

  canSeek = false; decodeBlocked = false; pcmInputRate=0;
  curTrack = index;
  durationMs = 0;
  posBaseMs = 0;
  if (!continuous) framesPlayed = 0;
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
static volatile bool endedNatural=false;      // the end was handed off gaplessly: the ring still holds its tail
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
    const bool continuous = reqContinuous && !startMs;
    if (t >= 0) opening = true;  // in the same critical section: no gap in which the request is neither queued nor opening
    reqTrack = -1; reqSeekMs = -1; reqContinuous = false;
    portEXIT_CRITICAL(&stateLock);
    if (t >= 0) {
      Serial.printf("cmd: track %d consumed serial %lu\n",t,(unsigned long)serial);
      const bool opened = openTrack(t, continuous);
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
        if (gaplessOn) {
          // Gapless: every frame of this track is in the ring. Say so now, so the
          // next file opens while the tail is still playing -- but not while
          // paused: the song on screen stays the one that is still to be heard.
          while (!playing && !requestPending()) ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
          if (requestPending()) continue;
          closeTrack();
          portENTER_CRITICAL(&stateLock);
          const bool current = serial == commandSerial && !requestPending();
          if (current) { gapArmed = true; gapOpened = false; gapFrames = 0; endedCommand = serial; endedNatural = true; }
          portEXIT_CRITICAL(&stateLock);
          if (current) endedSerial++;
          continue;
        }
        // End of track: let what is buffered play out, then say so.
        while ((rbUsed() > 0 || !playing) && !requestPending()) ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
        if (!requestPending()) {
          closeTrack();
          endedCommand=serial; endedNatural=false; endedSerial++;
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
static uint32_t  seenEnded = 0, seenFail = 0, seenGap = 0;
static int       failStreak = 0;
// Repeat Off reached the end while the last track's tail is still in the ring:
// load the first track (paused) only once that tail has played out.
static bool      stopPending = false;

// Up Next: songs the owner queued with Play next / Add to queue, played before
// the rest of the list. RAM only, loop task only (like the play order). While
// one plays, orderPos stays on the list song that was playing when it started,
// so the list carries on after it.
static uint16_t  upNext[AUDIO_UP_NEXT_CAP];
static int       upHead = 0, upLen = 0;
static bool      playingUpNext = false;
static inline int upSlot(int i) { return (upHead + i) % AUDIO_UP_NEXT_CAP; }
static int upNextPop() {
  if (upLen <= 0) return -1;
  const int id = upNext[upHead];
  upHead = upSlot(1); --upLen;
  return id;
}

// The queue in list order, or shuffled with `firstTrack` first. A first track
// outside the queue leaves orderPos at -1: next then starts the list.
static void buildOrder(bool shuffled, int firstTrack) {
  const int first = queueFind(firstTrack);
  order.reset(orderLen, shuffled, first, esp_random());
  orderPos = order.position(first);
}

static void request(int track, uint32_t startMs = 0, bool continuous = false) {
  if (track < 0) return; // queueAt already published the unavailable-source error
  stopPending = false;
  portENTER_CRITICAL(&stateLock);
  const int superseded=reqTrack;
  reqStartMs = startMs; seekGoalMs = startMs; reqSeekMs = -1; reqTrack = track;
  // A gapless hand-off keeps the old tail audible; anything else silences it at once.
  reqContinuous = continuous && !startMs;
  ++commandSerial; seeking = !reqContinuous; canSeek = false;
  // A queued/in-flight open now owns recovery; an old failure must not redirect Play.
  decodeBlocked = false;
  const uint32_t serial=commandSerial;
  portEXIT_CRITICAL(&stateLock);
  wakeDecoder();
  if(superseded>=0)Serial.printf("cmd: track %d superseded by track %d (latest wins)\n",superseded,track);
  Serial.printf("cmd: track %d queued serial %lu start %lu%s\n",track,(unsigned long)serial,(unsigned long)startMs,continuous?" (gapless)":"");
}

// `natural`: the track ended by itself and its tail is still in the ring
// (gapless hand-off), so the next open must not cut it off.
static void advance(bool trackFinished, bool natural = false) {
  if (orderLen == 0) return;
  if (trackFinished && repeatMode == REPEAT_ONE) {
    request(curTrack, 0, natural);
    return;
  }
  const int queued = upNextPop();
  if (queued >= 0) {
    if (!playingUpNext) Serial.println("cmd: up next starts");
    playingUpNext = true;
    request(queued, 0, natural);
    return;
  }
  playingUpNext = false;
  int next = orderPos + 1;
  if (next >= orderLen) {
    if (trackFinished && repeatMode == REPEAT_OFF) {
      // The end of the list: load the first track, paused, ready to go again --
      // once the last track's tail has played out.
      if (natural && rbUsed() > 0) { stopPending = true; return; }
      playing = false;
      next = 0;
    } else {
      next = 0;
    }
  }
  orderPos = next;
  request(queueAt(order.at(orderPos)), 0, natural);
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
  volumePct = min<uint8_t>(volume, 100);remoteVolume=false;volumeRequestSerial=0;
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
    if(cyd::currentCompletion(endedCommand,commandSerial)){failStreak = 0;advance(true, endedNatural);}
    else Serial.println("cmd: eof ignored (superseded)");
  }
  if (stopPending && rbUsed() == 0) { stopPending = false; advance(true); }
  if (gapSerial != seenGap) {
    seenGap = gapSerial;
    const uint32_t frames = gapLast;
    Serial.printf("stat: gap track %d missing %lu frames (%lu ms)\n", int(curTrack),
                  (unsigned long)frames, (unsigned long)(uint64_t(frames) * 1000 / 44100));
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
  playingUpNext = false;
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
  if (playingUpNext && orderPos >= 0) {
    // Back from a queued song: the list song it interrupted.
    playingUpNext = false;
    request(queueAt(order.at(orderPos)));
    return;
  }
  playingUpNext = false;
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
void audio_setVolume(uint8_t p) {
  portENTER_CRITICAL(&stateLock);volumePct=min<uint8_t>(p,100);++volumeRequestSerial;portEXIT_CRITICAL(&stateLock);
  Serial.printf("cmd: volume %u executed\n",unsigned(volumePct));
}
void audio_setRemoteVolume(bool active,uint8_t percent){
  portENTER_CRITICAL(&stateLock);
  remoteVolume=active;if(active)volumePct=min<uint8_t>(percent,100);
  portEXIT_CRITICAL(&stateLock);
}
bool audio_remoteVolume(){return remoteVolume;}
uint32_t audio_volumeRequestSerial(){
  portENTER_CRITICAL(&stateLock);const uint32_t serial=volumeRequestSerial;portEXIT_CRITICAL(&stateLock);return serial;
}

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
  // During a queued song, rebuild around the list song it interrupted.
  const int base = playingUpNext && orderPos >= 0 ? queueAt(order.at(orderPos)) : -1;
  const int wanted = base >= 0 ? base : reqTrack >= 0 ? int(reqTrack) : int(curTrack);
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
  playingUpNext = false; // the new list owns the cursor; queued songs still play first
  buildOrder(shuffleOn, keepTrack);
  if (orderLen > 0 && orderPos < 0 && jump) {
    orderPos = 0;
    failStreak = 0;
    request(queueAt(order.at(0)));
  }
}

int audio_upNextAdd(const uint16_t* ids, int n, bool front) {
  if (!ids || n <= 0) return 0;
  const int count = storage_trackCount();
  // The first ids that fit, in order; invalid ids are skipped.
  uint16_t take[AUDIO_UP_NEXT_CAP];
  int added = 0;
  for (int i = 0; i < n && added < AUDIO_UP_NEXT_CAP - upLen; ++i)
    if (ids[i] < count) take[added++] = ids[i];
  if (front) {
    // Walk the block backwards onto the front so it keeps its own order.
    for (int i = added - 1; i >= 0; --i) {
      upHead = (upHead + AUDIO_UP_NEXT_CAP - 1) % AUDIO_UP_NEXT_CAP;
      upNext[upHead] = take[i];
    }
    upLen += added;
  } else {
    for (int i = 0; i < added; ++i) upNext[upSlot(upLen++)] = take[i];
  }
  Serial.printf("cmd: up next %s %d of %d (now %d)\n", front ? "front" : "back", added, n, upLen);
  return added;
}
int  audio_upNextLen() { return upLen; }
int  audio_upNextAt(int i) { return i >= 0 && i < upLen ? int(upNext[upSlot(i)]) : -1; }
bool audio_upNextRemove(int i) {
  if (i < 0 || i >= upLen) return false;
  for (int k = i; k + 1 < upLen; ++k) upNext[upSlot(k)] = upNext[upSlot(k + 1)];
  --upLen;
  return true;
}
void audio_upNextClear() { upHead = upLen = 0; Serial.println("cmd: up next cleared"); }
bool audio_playingUpNext() { return playingUpNext; }
bool audio_upNextPlay(int i) {
  const int id = audio_upNextAt(i);
  if (id < 0 || !audio_ready() || !orderLen) return false;
  audio_upNextRemove(i);
  ++playIntentSerial;
  playing = true; playingUpNext = true; failStreak = 0;
  request(id);
  return true;
}
void audio_setGapless(bool on) { gaplessOn = on; Serial.printf("cmd: gapless %s\n", on ? "on" : "off"); }
bool audio_gapless() { return gaplessOn; }

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
