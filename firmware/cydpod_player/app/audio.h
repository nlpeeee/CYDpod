// ===========================================================================
// audio.h -- M4A playback engine: SD -> MiniAAC -> 44.1 kHz stereo ring buffer
// ===========================================================================
// Proven in stage0/s03 (0 underruns over 4 minutes, clean audio):
//
//   decode task (core 1, prio 5)            Bluetooth task (core 0)
//   SD read -> MiniAAC -> ring ==> audio_pull() -> A2DP -> speaker
//
// audio_pull() is the ONLY thing the Bluetooth side calls; it copies out of
// the ring buffer and never touches SD or the decoder. Everything else here
// is a request from the UI that the decode task acts on at its next chance,
// so no UI call ever blocks on the card.
//
// Position is counted in frames actually handed to Bluetooth, not frames
// decoded, so the time readout matches what is heard and stays right through
// pause and radio stalls.
#pragma once

#include <Arduino.h>

// Seeded permutation of queue positions; no per-track storage or allocation.
// Six balanced Feistel rounds on the smallest power-of-four domain, cycle
// walked into [0,count). Swapping two outputs makes the selected entry first.
class AudioOrder {
  uint32_t count_ = 0, seed_ = 0, mask_ = 0, bits_ = 0;
  int first_ = -1, zero_ = -1;
  bool shuffled_ = false;
  uint32_t round(uint32_t x, unsigned r) const {
    x ^= seed_ + 0x9e3779b9u * (r + 1);
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    return (x ^ (x >> 16)) & mask_;
  }
  uint32_t permute(uint32_t x, bool inverse) const {
    uint32_t l = x >> bits_, r = x & mask_;
    for (unsigned i = 0; i < 6; ++i) {
      const uint32_t next = inverse ? r ^ round(l, 5 - i) : l ^ round(r, i);
      if (inverse) { r = l; l = next; } else { l = r; r = next; }
    }
    return (l << bits_) | r;
  }
  int raw(int value, bool inverse = false) const {
    uint32_t x = uint32_t(value);
    do { x = permute(x, inverse); } while (x >= count_);
    return int(x);
  }
  int swapFirst(int value) const {
    return value == first_ ? zero_ : value == zero_ ? first_ : value;
  }
public:
  void reset(int count, bool shuffled, int selected, uint32_t seed) {
    count_ = count > 0 ? uint32_t(count) : 0; seed_ = seed;
    shuffled_ = shuffled && count_ > 1;
    bits_ = 0;
    while ((uint64_t(1) << (2 * bits_)) < count_) ++bits_;
    mask_ = (uint32_t(1) << bits_) - 1;
    first_ = selected >= 0 && uint32_t(selected) < count_ ? selected : -1;
    zero_ = shuffled_ && first_ >= 0 ? raw(0) : -1;
  }
  int at(int pos) const {
    if (pos < 0 || uint32_t(pos) >= count_) return -1;
    return shuffled_ ? swapFirst(raw(pos)) : pos;
  }
  int position(int queuePos) const {
    if (queuePos < 0 || uint32_t(queuePos) >= count_) return -1;
    return shuffled_ ? raw(swapFirst(queuePos), true) : queuePos;
  }
};

void audio_begin(int startTrack, uint32_t startMs, uint8_t volume, bool shuffle, uint8_t repeat);
// Starts paused. After restoring the queue, release the decode task's boot barrier.
void audio_start();
bool audio_startupComplete();
// Allocation/task failures are recoverable UI errors.
bool audio_ready();
uint32_t audio_error(char* message, size_t capacity, int* track = nullptr);

// Call from loop(): moves on when a track ends, following shuffle and repeat.
void audio_tick();

// UI requests -- all return immediately. Call them from the loop task only
// (the play order they share is not locked).
void audio_playTrack(int index);
void audio_next();
void audio_prev();                  // restart, or previous in play order in the first 3 s
void audio_togglePause();
void audio_setPlaying(bool on);       // explicit intent, even when already in that state
// Internal pairing transitions preserve the user's intent serial.
void audio_pauseForPairing();
void audio_resumeAfterPairing();
void audio_seekRelative(int seconds);
bool audio_seekTo(uint32_t milliseconds); // coalesced request; decoder task owns all I/O
bool audio_canSeek();                    // supported M4A with known duration
bool audio_isSeeking();
void audio_setVolume(uint8_t percent);
void audio_setShuffle(bool on);        // reshuffles with the current track first
void audio_setRepeat(uint8_t mode);    // RepeatMode from settings.h
bool    audio_shuffle();
uint8_t audio_repeat();

// Play from a list of library indices (playlist.h) instead of every track.
// `tracks` is kept, not copied: nullptr means the whole library. The order is
// rebuilt around `keepTrack`; when that song is not in the list and `jump` is
// set, the list starts, otherwise the song plays on and next starts the list.
struct AudioQueueSource {
  const uint16_t* tracks = nullptr;
  void* context = nullptr;
  int (*at)(void*, int) = nullptr; // -1: unavailable; context remains caller-owned
};
void audio_setQueueSource(AudioQueueSource source, int n, int keepTrack, bool jump);
void audio_setQueue(const uint16_t* tracks, int n, int keepTrack, bool jump);
int  audio_queueLen();
int  audio_queueTrack(int pos);       // list position -> library index, or -1
int  audio_queuePos(int track);       // library index -> list position, or -1

bool     audio_isPlaying();
int      audio_currentTrack();
uint8_t  audio_volume();
uint32_t audio_positionMs();
uint32_t audio_durationMs();        // 0 when it could not be worked out
// Increments whenever a different track (or the same one, restarted) opens.
uint32_t audio_trackSerial();

// Called by the Bluetooth data callback: fills `frames` stereo frames
// (interleaved int16 L,R). Always fills the whole buffer (silence if needed).
void audio_pull(int16_t* stereo, int32_t frames);

// Diagnostics for the serial stat line.
uint32_t audio_underruns();
uint32_t audio_takeMaxReadMs();     // slowest m4a.next() (SD read + whole-frame decode) since last call
uint32_t audio_takeRingLow();       // CY-119: ring low-water in frames since last call (audio_ringFrames() = none measured)
uint32_t audio_ringFrames();
uint32_t audio_pullCalls();        // diagnostics: Bluetooth pulls so far, and frames it actually took from the ring
uint32_t audio_pullFrames();
int32_t  audio_maxRequest();       // largest frame count Bluetooth asked for at once
uint8_t  audio_bufferPct();
bool     audio_bufferLow();         // below half: background drawing backs off
uint32_t audio_decodeStackFree();

struct AudioCommands {int track;int32_t seekMs;uint32_t serial;bool opening,seeking,blocked;};
AudioCommands audio_commands();

// Temporary timer fade multiplier; does not alter or persist user volume.
void audio_setSleepGain(uint8_t percent);

uint32_t audio_playIntentSerial(); // explicit play/pause/selection intent, loop-task only

struct AudioPcmStats {uint32_t inputRate,maxPumpMs,yields,blockUs,maxBlockUs;};
AudioPcmStats audio_pcmStats();
