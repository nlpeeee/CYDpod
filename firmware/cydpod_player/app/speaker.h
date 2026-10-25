// ===========================================================================
// speaker.h -- the on-board speaker: audio out through the GPIO26 DAC
// ===========================================================================
// The second consumer of audio.cpp's ring. Bluetooth pulls from it in its own
// task (bt.cpp); this pulls from it in ours, converts to 8-bit mono and hands
// it to the DAC's DMA. Which of the two is pulling is the route, below.
//
// Why the dac_continuous driver and not I2S built-in-DAC mode: that mode owns
// both DAC pins, and GPIO25 (DAC0) is this board's touch clock (board.h).
// dac_continuous takes a channel mask, so GPIO26 can be driven alone.
//
// Mono, 8 bits. That is what the pad on the board is: one small amplifier and
// one speaker. The volume is already applied in 16 bits inside audio_pull(),
// so what is left here is the truncation, which is dithered.
//
// THE AMPLIFIER HAS AN ENABLE, AND IT IS THE RED LED'S PIN. GPIO4 must be
// driven LOW or the 8002E stays in shutdown and nothing is audible, whatever
// the DAC is doing -- floating counts as shutdown too. It is also
// CYD_RGB_R_PIN, active LOW, so the red LED is lit exactly when the amplifier
// is on. This file enables it when the DAC starts and returns it to shutdown
// when the DAC stops, which keeps the LED off on the Bluetooth route and while
// nothing is playing. See board.h for how this was found.
#pragma once

#include <Arduino.h>

// Stored in Settings::outRoute, so the values are persisted: do not renumber.
enum OutRoute : uint8_t { OUT_SPEAKER = 0, OUT_BT = 1, OUT_ROUTE_COUNT = 2 };

void    speaker_begin(uint8_t route);
uint8_t speaker_route();
void    speaker_setRoute(uint8_t route);   // takes ~50 ms: the handoff below
bool    speaker_active();                  // the DAC is running right now

// Times the writer task found the DMA still full after half a second. Should
// stay at 0; it is in the 10 s stat line next to audio_underruns().
uint32_t speaker_stalls();
