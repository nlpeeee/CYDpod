// ===========================================================================
// wired.h -- the wired earphone output: audio out through I2S to a PCM5100A
// ===========================================================================
// The third consumer of audio.cpp's ring (speaker.h has the other two). One
// writer task pulls 256 frames at a time with the same audio_pull() the
// on-board speaker uses, runs them through wired_fade.h (pop-free pause, seek,
// track change and route change, and a -6 dBFS ceiling) and hands them to the
// ESP-IDF i2s_std driver. The blocking write is the playback clock.
//
// Compiled only where board.h routes the I2S pins (CYD_WIRED_OUTPUT, the
// JC2432W328C); everywhere else this header is empty and OUT_WIRED is not
// selectable.
//
// The route, and the park/start handshake with the other consumers, belong to
// speaker_setRoute(): it calls these in order and never touches I2S itself.
#pragma once

#include "board.h"

#if CYD_WIRED_OUTPUT
#include <stdint.h>

bool wired_reserve();        // chunk buffer + fade state (about 1.7 KB); idempotent
void wired_release();        // frees them; refuses while the writer task exists
bool wired_startTask();      // creates the writer task (core 0, priority 5), unparked
void wired_kick();           // route == OUT_WIRED is now published: begin
bool wired_parked();         // the task is not using the channel or the buffer (true with no task)
void wired_deleteTask();     // only after wired_parked()
bool wired_active();         // the I2S channel is enabled right now

// Silences the output with the same ramp as a pause. Not used by the UI yet.
void wired_setMuted(bool muted);

struct WiredStats {
  bool     active;
  uint32_t sessions;     // times the channel was started
  uint32_t chunks;       // 256-frame chunks handed to the driver
  uint32_t frames;       // frames the driver accepted
  uint32_t underruns;    // empty-ring pulls the engine counted while we were the consumer
  uint32_t writeErrors;  // driver errors other than timeouts
  uint32_t stalls;       // write timeouts (DMA stayed full for 100 ms)
  uint32_t dropped;      // chunks abandoned after repeated failures
  uint32_t stackFree;    // writer task stack high-water mark, bytes
  uint32_t heapCost;     // free heap consumed from reserve to channel enabled, bytes
};
WiredStats wired_stats();
#endif
