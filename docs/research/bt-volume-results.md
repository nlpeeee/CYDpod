# Bluetooth volume feedback candidate

Compiled and checked source: `2aa26295cdd227a185f9473f7bcc25bd2e194afd` on `cydpod/bt-a`. Implementation commits: `16ba252` and `9e73f69`. Contract: [speaker volume feedback](../specs/bt-volume-feedback.md).

CYDpod handled AVRCP pass-through keys but did not publish the speaker's absolute-volume reports into `audio_volume()`. The pinned source library also calls `set_volume()` from notification and response handlers: that updates its PCM gain and can send additional set-volume commands. Updating only the display callback would leave multiple gain owners and command echoes.

The CydSource adapter now intercepts these virtual handlers, re-arms notifications without echoing them, and records reports under a short lock. The loop publishes the receiver's 0–127 setting as the shared 0–100 volume; existing Now Playing painting follows it. Local changes send coalesced absolute-volume requests with one outstanding command, a 100 ms minimum interval, and a 1 s timeout without automatic retries. Receiver quantization is accepted, and pending newer local changes survive an older response. Absolute mode ignores additional relative volume keys from the same speaker press.

After a valid report, the receiver owns user volume. The library and audio_pull user-volume stages are bypassed; the PCM sleep fade still applies. Before a valid report and on unsupported receivers, the previous software/library gain path remains. Wired output retains software volume. Disconnect clears absolute support and pending requests. No vendor source, dependency pin, codec, ring size, core assignment or UI drawing code changed in this follow-up.

Evidence on final source:

- PASS Bluetooth volume: reports, mute, endpoints, quantization, coalescing, no echoes, timeout, disconnect, reconnect, clock wrap.
- PASS Bluetooth PCM gain: one volume owner, sleep fade, software fallback, no remote request echo.
- PASS: audio diagnostic window (atomic copy/reset, 64-bit totals, cadence, missing frames).
- PASS: compile-only checks completed; no device commands were run.

Host command: `wsl -d Debian --cd /mnt/e/dev/CYDpod --exec sh tests/build_bt_volume.sh`. Log `.tools/logs/bt-volume-host-final-gcc.log`. An initial attempt to build the GCC-based PCM test using MSVC could not link unused codec entry points; the existing GCC audio test ran successfully instead. The separate protocol test also passed under MSVC. The Classic UI suite was not repeated; its production drawing code is unchanged and the owner reported the previous redraw fix was much better.

Build command: Windows PowerShell 5.1 `tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia -LargePcmRing -BuildLabel bt-a`. Log `.tools/logs/bt-volume-w328c-guard-final.log`. Bluetooth sketch: 1,572,309 bytes; static RAM: 87,240 bytes. Wired sketch: 1,572,449 bytes; static RAM: 87,248 bytes. Static RAM increases by 48 bytes over bt-a-player, or 552 bytes over the matching 0.5 builds.

Package: `releases/candidates/bt-a-volume`. Earlier packages remain available. Board volume synchronization, unsupported-receiver behavior, listening quality, runtime heap and stack headroom are pending. Start the first comparison at low speaker volume: removing duplicate attenuation changes loudness at the same displayed percentage. Use the board procedure in the package README with both JBL Flip 3 and Xiaomi Sound Pocket before promotion. Issue #1 remains open until those observations confirm the fix.

Reference: [Espressif AVRCP API](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/bluetooth/esp_avrc.html) documents the absolute-volume request and notification APIs. Implementation was checked against the installed ESP-IDF headers and pinned ESP32-A2DP 1.8.12 source.
