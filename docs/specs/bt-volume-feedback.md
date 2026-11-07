# Speaker volume feedback

Issue: https://github.com/nlpeeee/CYDpod/issues/1

Keep ESP32-A2DP pinned to 1.8.12 and retain candidate A's PCM ring, codecs, core assignments and Now Playing redraw fix. Intercept the existing source's AVRCP volume notification and response handlers in CydSource without modifying vendor code.

After a capable receiver connects, send one initial absolute-volume request seeded from CYDpod’s current volume and complete the handoff using the receiver’s response. Do not require a physical speaker-button press to initialize. Before confirmation retain fallback gain; preserve a newer local slider target while the first response is outstanding. Unsupported, failed or timed-out initialization must retain fallback without a retry loop. A valid notification received before initialization can establish the receiver’s current setting directly.

When a connected receiver reports valid absolute volume, its 0–127 setting becomes the shared 0–100 volume displayed by Now Playing. The receiver owns user gain; bypass the library and PCM user-volume attenuation in this mode. Retain the PCM sleep fade. Before a valid report, and on receivers without absolute-volume feedback, retain the existing software volume and library gain. Wired output keeps software volume.

Volume publications and command intentions cross the Bluetooth/loop boundary under a short lock. The loop updates shared audio/UI state. Speaker reports never generate set-volume commands. Local slider changes coalesce behind one outstanding request, at most once per 100 ms, with bounded timeout and no automatic retry loop. Apply the receiver's returned quantization; an older response must not replace a newer slider target. Ignore additional relative volume key events while absolute volume is active. Reset support and pending commands on disconnect; do not assume the next receiver supports the same mode.

Validation: run `sh tests/build_bt_volume.sh` with GCC and the W328C compile-only check with `-SkipProbe -SkipHostMedia -LargePcmRing`. Host checks must cover endpoints, percentage conversion, malformed reports, no echoes, quantization, coalescing, timeout, failure, reconnect and clock wrap, plus actual PCM gain, sleep fade and fallback.

The owner must validate both the JBL Flip 3 and Xiaomi Sound Pocket: change speaker volume while playing and paused, check Now Playing in portrait and landscape, drag the local slider, test mute, disconnect/reconnect and switch speakers. Capture `bt: speaker volume` and `bt: set speaker volume` lines alongside steady-playback missing-frame and memory diagnostics. Check one physical press produces one audible step. Check a receiver without absolute-volume reports retains software volume. Bluetooth gain now follows the speaker's own scale and removes previous duplicate attenuation, so start this candidate at a low speaker volume. Keep the previously accepted candidate until the listening pass succeeds.
