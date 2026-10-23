// ===========================================================================
// s07_speaker_hunt -- which pin, if any, reaches the speaker?
// ===========================================================================
// SUPERSEDED. It was run, and every pin was silent -- correctly, and
// uselessly: the amplifier is held in shutdown unless GPIO4 is LOW, and this
// sketch leaves GPIO4 floating, which is shutdown. Nothing it could have put
// on any pin would have been audible.
//
// Worse, this sketch could not have found the answer even in principle. GPIO4
// IS in the list below -- but a square wave on a shutdown line just toggles
// mute, silently. Finding an enable needs a tone held on the OUTPUT pin while
// the candidate is held at a STEADY level, which is what ../CYD Speaker Test
// does and what this does not.
//
// The premise in the header below -- that s06 proved the firmware innocent --
// is false; see that sketch's own corrected header.
// ===========================================================================
// s06 established that the firmware is not the problem: the DAC's own
// hardware oscillator on GPIO26 -- no CPU, no DMA, no clock configuration --
// is silent. Either nothing is connected to GPIO26 on this board, or the pin
// that reaches the amplifier is a different one.
//
// board.h has said CYD_SPEAKER_PIN 26 since stage 0, but nothing ever tested
// it. Clone boards sold as "2432S028R" differ. So: put a loud square wave on
// every pin that could plausibly be it, one at a time, and say on serial
// which one is playing. Whichever pin makes a noise is the answer.
//
// A square wave, not a sine: twice the energy of a DAC sine at the same
// amplitude, and it swings the full 0-3.3 V rail, so it is the loudest thing
// this chip can put on a pin. Through an amplifier it will be unpleasant.
//
// Pins left out and why:
//   6-11    the SPI flash. Driving them crashes the board.
//   1, 3    the serial port this test reports on.
//   34-39   input only -- they cannot drive anything.
// Everything else is fair game: nothing else is initialised in this sketch,
// so the display, touch and SD pins are free to borrow for two seconds.
#include <Arduino.h>
#include "board.h"

static const int TONE_HZ  = 1000;
static const int PIN_MS   = 2000;

// Likely first, then the rest. 26 and 25 are the two DACs; 22 and 27 are the
// pins this board brings out and uses for nothing.
static const int PINS[] = {
  26, 25, 22, 27, 21, 17, 16, 4, 5, 0, 2, 12, 13, 14, 15, 18, 19, 23, 32, 33
};
static const int PIN_COUNT = sizeof(PINS) / sizeof(PINS[0]);

static void toneOn(int pin) {
  // 8-bit resolution, half duty = a square wave at full swing.
  ledcAttach(pin, TONE_HZ, 8);
  ledcWrite(pin, 128);
}

static void toneOff(int pin) {
  ledcWrite(pin, 0);
  ledcDetach(pin);
  pinMode(pin, INPUT);              // let it go: some of these belong to the display
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\ns07_speaker_hunt");
  cydPrintBanner();
  Serial.printf("\nboard.h claims the speaker is on GPIO%d.\n", CYD_SPEAKER_PIN);
  Serial.printf("Playing 1 kHz on %d pins, %d ms each. Listen for which one.\n\n",
                PIN_COUNT, PIN_MS);
}

void loop() {
  for (int i = 0; i < PIN_COUNT; i++) {
    Serial.printf("  GPIO%-2d  (%d of %d)\n", PINS[i], i + 1, PIN_COUNT);
    Serial.flush();                 // the line must be out before the noise starts
    toneOn(PINS[i]);
    delay(PIN_MS);
    toneOff(PINS[i]);
    delay(300);                     // a gap, so two pins in a row are distinguishable
  }
  Serial.println("--- sweep done, starting again ---\n");
  delay(1500);
}
