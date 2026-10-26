# Classic Touch: live Bluetooth pairing and track seeking

2026-10-26. Planning only, following the user's findings: **Pair new causes a reset; track seeking does not function.** No application code, build, flash, serial port or device is changed by this planning task.

## Confirmed causes and baseline

Inspected source: CYDpod branch `cydpod/cy43-heap`, implementation `e5710ed`, report `09b4730`, prepared Classic Touch v5. The user has reported device behavior, but no new serial log or image identity was supplied; do not claim a measured v5 hardware pass.

| Finding | Source evidence | Consequence |
|---|---|---|
| Pair new resets | `screen_bt.cpp:195–196` calls `bt_requestPairing`; `bt.cpp:500–504` writes an RTC pairing flag and explicitly calls `ESP.restart()` | This action is intentionally implemented as a reboot, rather than evidence by itself of a crash. |
| Cancel can reset too | `screen_bt.cpp:189–192` calls `bt_cancelPairing` on Back during an unconnected pairing boot; `bt.cpp:507–510` also calls `ESP.restart()` | Both entry and cancellation need a live-state replacement. |
| Seeking is disabled in the audio engine | `audio.cpp:383` implements `audio_seekRelative(int)` as an empty function; decode task clears `reqSeekMs`; initial `reqStartMs` is discarded | UI wiring alone cannot fix it. Saved resume positions are read from settings but not honored by the decoder. |
| Active UI does not offer a seek target | `classic_screen.cpp::targetAt/press/touch` handles transport and volume, but not the progress bar; long presses act like taps | Both audio and Classic Touch controls must be implemented. Legacy `screen_player.cpp` comments still advertise seeking although its handler explicitly disables it. |
| Decoder public API has no seek | `vendor/miniaac/src/miniaac.h`, `M4aTrack` and MiniAAC README | A byte-position change on the SD File is insufficient: decoder sample/chunk counters, trimming and codec state must agree. |

Relevant existing components: `CydSource::connectNow/rediscover/chaseLastDevice`, `bt_tick`'s paging and deferred settings writes, source library discovery/connection callbacks, MiniAAC's bounded `stsz/stsc/stco/co64` table access and `times/edits` validation, Helix `AACFlushCodec`, and the existing ring-consumer handoff. Reuse these; do not write another Bluetooth stack, codec or container parser.

## Required behavior

- Pair new opens live discovery without restarting the device or reinitializing/releasing the Bluetooth controller. Show disconnecting/scanning/connecting/failed states and allow Back/Cancel. Keep existing bonds and saved devices until an explicitly requested Forget.
- Preserve track, queue, shuffle/repeat, volume and prior play/pause intent. Pause BT playback while handing off devices; resume that intent after a selected device connects. Cancel restores normal reconnect policy and offers/retries the former device without losing playback position. Do not silently select the old speaker during discovery.
- Seek within supported AAC-LC 44.1 kHz stereo M4A files by tapping or dragging the progress bar, and by holding Previous/Next for minus/plus 10 seconds. A short press retains its current track-navigation meaning.
- Seeking preserves pause state and queue. Clamp targets to the playable timeline after edit-list trimming. Empty/unknown-duration media has a disabled control and clear feedback. At the end, clamp to the last playable frame; normal completion occurs once playback reaches EOF, without advancing a paused queue.
- UI, Bluetooth callbacks and SD/decoder work remain separated. Seeking must not block the loop task or allocate a full-track PCM/table index. The latest seek supersedes older seeks; selecting another track cancels the old seek.
- Keep CY-42/CY-43 heap work, 2,048-frame PCM ring, SSP, AVRCP controls and loop stack. Proposed additional permanent seek state budget: **at most 512 B**, subject to measurement. No O(track duration) RAM growth. Free/largest heap targets remain 40 KiB/25 KiB, currently unverified on hardware.

## Implementation sequence

### 1. Replace reboot-based pairing with a live session

Deliverable: `bt.cpp`, `bt.h`, `screen_bt.cpp`, plus a testable pairing state model and host tests.

1. Add explicit runtime states such as Normal, DisconnectingForPair, Scanning, ConnectingSelected and Cancelling, with bounded deadlines. Replace RTC/boot pairing semantics with a runtime pairing-session query. Audit one-shot `chaseStopped`, `reconnectRestored`, `waitingToPair` flags: they must work on every session, not only the first one after boot.
2. UI requests a pairing action and returns immediately. Serialize library state changes through its existing task/work mechanism; callbacks report events, and the loop applies settings/UI updates. Do not add competing unsynchronized writes to `s_a2d_state` from UI and callback tasks.
3. Suspend the app's 15-second saved-device paging and library auto-reconnect before disconnecting the current peer. Wait for the actual disconnected event before discovery. Stop any outstanding inquiry before connecting to a selected device. Check every start/cancel/connect API return and enforce timeouts.
4. Clear the discovery result list under its lock and make the device discoverable/connectable during pairing. Suppress saved-address/name auto-selection in `onSsid` for this session. Accept incoming connections according to explicit pairing policy, including peer-initiated car pairing.
5. Reject late events for a former peer/attempt before updating `last_connection`, the displayed name or persistent settings. Track session generation plus expected peer and phase; low-level events do not carry our generation, so generation alone is not sufficient validation.
6. Cancel stops discovery, waits for its stop acknowledgment, restores normal policy and reconnects the former peer when appropriate. Successful pairing updates the remembered device only after a confirmed connection. Failure keeps the UI usable and allows another attempt. Remove both pairing-related restart calls; keep unrelated Refresh Library restart behavior.

Tests: enter/cancel from disconnected, connecting and connected states; repeated Pair new; late old-peer connect/disconnect; selected-device timeout/authentication failure; inquiry already active; API refusal; peer-initiated connection; preserved bonds/settings and play intent. Assert zero restart/controller-release/start calls for pairing transitions. Existing host mocks cannot validate actual GAP/A2DP timing.

### 2. Add a bounded decoder seek primitive

Deliverable: a small staged MiniAAC overlay, application adapter API and host PCM/reference tests. Keep pristine `vendor/` and its inventories unchanged; record patch provenance/hash and apply the overlay to staging for the player, probe and seek tests consistently.

1. Define seek in **audible PCM frames**, relative to the trimmed track timeline; use 64-bit arithmetic for milliseconds/frame conversion. The current supported M4A timing validation already requires 1,024 PCM frames per AAC packet except a shortened final packet. Use that validated contract, not an assumption about arbitrary MP4 files.
2. Map the target plus `trim_start` to an AAC packet, locate its chunk with existing `stsc` mappings, resolve `stco/co64`, and sum bounded-page `stsz` entries within that chunk. Keep tables on SD and validate every count, offset and overflow. Large single-chunk files can require many size reads; make traversal cancellable/yielding rather than promising constant latency.
3. Reset the existing Helix codec state using its supported flush/reset path and restore raw-block parameters where required. Decode a tested amount of preroll to restore overlap/filter history; determine that amount from PCM comparison rather than guessing one packet is always enough. Discard preroll and pre-target samples, and preserve final edit-list trimming.
4. Update MiniAAC sample/chunk/decoded-frame counters and table-cache state together. Check the actual workspace size against `MINIAAC_WORKSPACE_BYTES` before adding fields: the investigation found only 48 B slack in the old arena. Account explicitly for any bound change.
5. Add `M4aTrack` seek support using the same workspace/output storage. ADTS must return an explicit unsupported-seek result until a bounded indexing design exists; do not claim that byte-seekable input implies timeline seek support. File scanning/decoding from the beginning may be used as a correctness reference, not the production seek strategy for late-track seeks.

Tests: seek to start, mid-packet, chunk boundary, near/exact end and backward repeatedly; edit-list priming/trimming, shortened final packet, variable/fixed sample sizes, `co64`, moov-first/last, multi-entry chunk mappings, long single-chunk files, damaged/out-of-range tables and I/O failures. Compare post-preroll PCM windows with sequential decoding and timing against a reference. Confirm allocation bounds and existing decoder/media regression checks.

### 3. Integrate seeking with the decode task and ring

Deliverable: `audio.cpp/audio.h`, `m4a_track.h`, resume handling and command/ring regression tests.

1. Add absolute and relative seek requests with an acknowledged command generation and target track identity. Publish them safely across tasks. Coalesce rapid drag requests so only the newest target is decoded; a track change wins over an obsolete seek.
2. The decode task alone owns seek, File I/O and decoder reset. Stop publishing old PCM, invalidate its pending output block, request and acknowledge a ring flush, and prime PCM from the new target. Do not let UI code assign both ring cursors while a BT/speaker callback may still be reading.
3. Preserve paused/routed/disconnected states. If no consumer is pulling, provide an explicit safe flush path; seeking while paused must not deadlock waiting for consumption. During seek, provide silence rather than stale PCM, and distinguish deliberate seeking from underruns.
4. Report seeking/completed/failed with requested and actual target plus elapsed time. Advance the playback-clock epoch with the new stream data; do not let a late flush reset the clock back to zero. On failure remain safely paused with an error/retry path rather than auto-advancing through the queue.
5. Honor `reqStartMs` through the same path for normal saved-position resume, retaining the crash-boot rule that skips the suspect track and starts the next one at zero. Preserve the user's paused startup behavior. Reset-to-start and Next/Previous must continue to work.

Tests: seek playing/paused/disconnected and on each output; no stale PCM after acknowledgment; rapid forward/back requests; seek then track change; failed seek; EOF during seek; no consumer; correct clock and pause state; normal and crash-guard resume. Recheck high-water margin of the new 4 KiB decode stack.

### 4. Add Classic Touch seek controls

Deliverable: Classic player hit targets, gesture handling and UI tests; bring legacy comments/behavior into agreement.

1. Give the narrow progress line a usable touch area, respecting portrait/flipped/landscape geometry. Tap sets an absolute position; drag previews target time and commits once on release, with cancellation on leaving the gesture/context. Avoid issuing SD seeks for every touch sample.
2. Show seeking feedback and the acknowledged position, then return to elapsed/remaining display. A paused seek updates position without starting playback. Unknown/empty duration disables the interaction.
3. Distinguish short Previous/Next taps from long presses; a long press seeks by 10 seconds and must not also skip a track. Claim this gesture so it cannot trigger the four-second recalibration hold. Preserve unrelated list long-press behavior.

Tests: progress start/end/middle, drag release/cancel, pause retention, rapid taps, orientation transforms, empty library, unknown duration, long-press versus tap, calibration suppression and Back during seek. Keep the currently tested volume/queue/navigation behavior.

### 5. Validate and prepare separate reviewable images

First package the pairing change independently; then package seek support after its decoder/audio/UI checks pass. This lets hardware failures be attributed and permits rolling back seeking without losing the pairing fix. Do not dispatch workers or flash as part of this planning request.

- Use `E:/dev/hq/playbook/BUILD-CYDPOD.md` for commands. Work from the actual isolated implementation checkout; preserve v5 rollback and use absolute image paths in handoff commands to avoid the prior missing-file error from the main checkout.
- Run the repo's `powershell -NoProfile -File tools/check.ps1` after code changes, plus meaningful targeted seek/pairing tests. Keep inventories, source/staging identity, ELF/static RAM, binary sizes and SHA256 manifests. No source/vendor/core replacement or port operation is part of compile checks.
- In a later authorized, exclusive COM10 session, first record the installed image and v5 baseline. Confirm that Pair new/Cancel cause no boot banner/reset, preserve track state, and support repeated discovery/new-device/cancel/reconnect. Retain reset reasons/backtraces if any reset remains; do not assume every observed reset comes from the explicit old path.
- Exercise both available physical outputs, the Xiaomi speaker and a second available BT peer/car; actual AirPods interoperability remains a separate existing acceptance requirement and cannot be inferred from Xiaomi success. Test at least 20 pairing/cancel/switch cycles and 30 minutes of playback/seek use without downward heap trend or reset.
- Seek a known song to 25/50/90%, backward, near end and while paused; test a long supported M4A and rapid seeks followed by Next/Back/output switch. Confirm audible position against reference content, advancing frames/clock and correct EOF behavior.
- Proposed seek acceptance: first post-seek content within 50 ms of the requested audio position after trim; target completion under 1 s for ordinary tracks and under 2 s for the long-file fixture, measured and revised explicitly if storage layout prevents it. UI must stay responsive during longer traversal, allow cancellation and avoid presenting stale audio as a completed seek. BT output latency is measured separately from decoder-position accuracy.
- Capture `stat:` free/min/largest, decode/loop stacks, ring level, underruns, pulls/frames, SD latency and explicit pair/seek events. Retain the 40 KiB free / 25 KiB largest goal; establish the real baseline first and report any shortfall rather than claiming it from arithmetic. New persistent seek state must fit the stated budget and preserve useful stack margin on error paths.

## Completion gate and remaining uncertainty

Done means live pairing and cancellation without reboot, functional seek and resume on supported M4A, passing host/full compile checks, and separately recorded hardware results for connection/touch/audio/memory behavior. Do not describe planned timing, preroll correctness or heap thresholds as already proven.

The remaining technical risks are late Bluetooth events and reconnect races; codec history after random access; long single-chunk M4A seek latency; ring/clock synchronization while paused or disconnected; and decode-stack margin. They are addressed by separate tests and staged delivery rather than by a Bluetooth restart or decoding an entire song to reach a seek point.

Planning validation: checked the cited local source/API paths and confirmed the explicit restart/no-op handlers. Documentation-only gate: `git diff --cached --check`. No code changes or hardware tests are claimed by this plan.
