# Initial Bluetooth volume synchronization

Firmware source: `ad3f72b13466a9e7e23a3babf20397e76ee0d19c`, on `cydpod/bt-a`. Follow-up to [the volume candidate](bt-volume-results.md).

The owner reported that steady volume feedback works well, but the displayed level and local-slider scaling are wrong after connect/reconnect until a speaker volume button is pressed. The earlier state machine enabled absolute gain ownership only after a valid change notification. It had no initial command exchange, and ignored command responses before that first notification. This leaves fallback volume and speaker volume as independent controls when no initial report arrives. The observation matches that path; an on-board trace of the event ordering remains pending.

After supported volume capabilities arrive, start one initial set-absolute-volume command using CYDpod's current volume if no valid report has already established synchronization. Its confirmed response enables absolute mode and publishes the receiver's actual, possibly rounded setting. Preserve a newer local slider target through that response. Do not let an older notification replace the requested initial level while confirmation is pending. Duplicate connected/capability events are idempotent. Disconnect resets initialization for the next connection. Unsupported receivers, rejected sends and initialization timeout retain fallback without automatic retries.

The command uses the existing coalescing/one-outstanding-request path. No changes to PCM code, gain curves, codecs, UI drawing, library pin, core assignment or ring size in this follow-up. This completes the handoff earlier; steady absolute-volume behavior remains the same.

Final checks:

- PASS initial Bluetooth volume: no initial report, confirmed handshake, mute, reconnect, duplicate events, pending slider, unsupported, timeout and failure fallback.
- PASS Bluetooth volume: reports, mute, endpoints, quantization, coalescing, no echoes, timeout, disconnect, reconnect, clock wrap.
- PASS Bluetooth PCM gain: one volume owner, sleep fade, software fallback, no remote request echo.
- PASS: audio diagnostic window (atomic copy/reset, 64-bit totals, cadence, missing frames).
- PASS: compile-only checks completed; no device commands were run.

Host command: `wsl -d Debian --cd /mnt/e/dev/CYDpod --exec sh tests/build_bt_volume.sh`. Log `.tools/logs/bt-volume-initial-host-final.log`. Build command: Windows PowerShell 5.1 `tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia -LargePcmRing -BuildLabel bt-a`. Log `.tools/logs/bt-volume-initial-w328c-final.log`. Run these sequentially: the host and firmware checks both restage MiniAAC. An initial overlapping run hit a transient missing-header error; the sequential firmware build passed.

Bluetooth sketch: 1,572,573 bytes, static RAM 87,240 bytes. Wired sketch: 1,572,713 bytes, static RAM 87,248 bytes. Static RAM is unchanged from bt-a-volume.

Package: `releases/candidates/bt-a-volume-init`, preserving previous packages. Before touching a speaker volume button, connect and adjust the CYDpod slider. Repeat after Disconnect/Connect, a CYDpod reboot and a receiver switch. Check initial mute and a local adjustment during setup. Capture `bt: initial volume sync target=N%`, `bt: set speaker volume raw=N rc=0` and `bt: speaker volume N% (absolute)` lines. A valid report received first can establish synchronization without the initialization command. Confirmation on the JBL Flip 3 and Xiaomi Sound Pocket remains pending; issue #1 stays open.

API reference: [Espressif AVRCP API](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/bluetooth/esp_avrc.html). Signatures and response fields were verified against the installed ESP-IDF 5.5.4 headers and pinned ESP32-A2DP source.
