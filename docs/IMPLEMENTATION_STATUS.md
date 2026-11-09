## 0.6.0 release checkpoint — 9 November 2026

Version 0.6.0 ships two boards, the JC2432W328C and the ESP32-2432S028R, and adds:
- Up Next: hold a song, album or artist for Play next / Add to queue (64 songs, RAM only);
- gapless playback: a natural track end opens the next file behind the playing tail, and MP3s skip the tag frame and trim the encoder delay and padding;
- a vivid `#FFC800` splash background.

`tools/release/Package-Release.ps1` now packages both boards. Host suites and both compiles PASS. The owner has tested 0.6.0 on both the JC2432W328C and the ESP32-2432S028R. After the release the splash and serial banner show the CYDpod version (`v0.6.0`) instead of the upstream `1.2.0`. See [0.6.0 release notes](releases/0.6.0.md).

## Wired-only image retired — 9 November 2026

The owner confirmed a wired I2S DAC on the 0.5.5 Bluetooth build through Settings → Audio Output → Headphones. The `cydpod-player-wired` job (CY-71, `-DCYD_DEFAULT_OUT_ROUTE=2`) is removed from `tools/check.ps1`; from the next release the player image is the only firmware for the JC2432W328C.

## 0.5.5 release checkpoint — 8 November 2026

Version 0.5.5 adds the 5-band fixed-point EQ (Equalizer page, presets, 0.6–0.7% of a core on the board) and the earbud work: Bluedroid patches 0002 (link counters, runtime SBC rate) and 0003 (full A2DP packets), an adaptive SBC rate that starts every link at 328 kbps, Play reconnecting earbuds that dropped the link, peer-suspend restarts and a congestion-only quiet UI. Firmware compiled from 010c6f6; compile and host suites PASS, owner runs on development builds. See [0.5.5 release notes](releases/0.5.5.md).

## 0.5.4 release checkpoint — 8 November 2026

Version 0.5.4 is 0.5.3 plus scrolling Now Playing labels (one overflowing title, artist or album at a time, +16 B static RAM) and a corrected transmit power readback: `esp_bredr_tx_power_set` is asynchronous, so CYDpod now waits up to ~20 ms before logging the level. Firmware compiled from 2a7b1a0 on the 0.5.3 trimmed Bluedroid; the release image itself is compile-checked, not yet board-run. See [0.5.4 release notes](releases/0.5.4.md).

## 0.5.3 release checkpoint — 8 November 2026

Version 0.5.3 makes a trimmed Bluedroid the default. It is rebuilt from ESP-IDF v5.5.4 without BLE, BLE Mesh, BluFi or HFP, runs the radio Classic only, and gives the BTC/BTU tasks 4 KB stacks; free heap after Bluetooth starts rises from about 34 KB to 84 KB. The release also adds robust speaker volume sync, adopts the speaker's own volume on connect (Bluedroid patch 0001), and enlarges the PCM ring to 6,144 frames. The exact board-tested candidate 4 firmware (6026ec7) is preserved. The +6 dBm transmit power still reads back +3 dBm and is under investigation. See [0.5.3 release notes](releases/0.5.3.md).

## 0.5.2 release checkpoint — 7 November 2026

Version 0.5.2 promotes the power candidate: idle levers and in-place light Sleep, fast Bluetooth reconnect after wake, +9 dBm transmit power, volume sync restarted after Sleep, playlists built from per-track path keys (including lists inside `/music`), one more indexed folder level and a full-width portrait volume slider. The exact candidate firmware (8c2ed33) is preserved. See [0.5.2 release notes](releases/0.5.2.md) for provenance, checks and what is not yet confirmed on hardware.

## 0.5.1 release checkpoint — 7 November 2026

The maintained checkout and source of truth are `E:\dev\CYDpod` and private `nlpeeee/CYDpod`. Version 0.5.1 consolidates candidate A’s Bluetooth update/menu controls, the Now Playing redraw fix and absolute-volume feedback with initial connection synchronization. The exact final candidate firmware is preserved. See [0.5.1 release notes](releases/0.5.1.md) for provenance, checks and remaining hardware measurements. Older entries below are historical and do not describe the current feature set.

Use `main` for ongoing work and release tags for snapshots. Temporary branches are merged and removed after release. Historical unused branch tips are archived in a verified local bundle before cleanup; branches attached to another worktree remain protected.

# CYDpod implementation checkpoint

Updated: 2026-10-25. The user approved portrait Classic Touch and required
Artist/Album/Song browsing, then authorized implementation while AFK.

## Current UI checkpoint

Implemented in the current `cydpod/miniaac` checkout: portrait Home/Music/Now
Playing, touch/drag/page navigation, Artist/Album/Song views, independent source
previews and queue commits, volume/options/settings, track-path persistence and
recoverable two-generation SD catalogue refresh. Existing display/touch/audio/
Bluetooth services and pinned vendor inputs are retained. See
[implementation evidence](research/classic-touch-ui-implementation.md) and
[the UI specification](specs/classic-touch-ui-plan.md).

The repository check passes, including the actual maintained UI/catalogue code,
1,000 tracks, metadata/grouping, recovery and queue/input host tests. Final output
is `.tools/logs/classic-touch-final-check.txt`. No device/serial operation was
performed by this UI implementation task.

Hardware qualification remains open: actual AirPods Pro 2, external stereo
line/headphone output and integrated UI playback/memory/touch measurements.
Artwork and M4A seeking/position resume are not implemented. The stock uploader
remains MP3-only; load AAC through the SD card. Build artifacts do not establish
the firmware currently installed on the device.

The bootstrap details below are historical. Consult newer CY-13c/CY-33 reports
before hardware work. No historical dispatch lifecycle was changed in this task.

## Required result

Owned ESP32-2432S028 CYD playing AAC-LC at 44,100 Hz, two channels, from SD to actual AirPods Pro 2 or a selectable external stereo I2S DAC with line/headphone output. Preserve the original music files. Full scope and acceptance are in [the specification](specs/cydpod-plan.md).

## Historical bootstrap work (2026-10-23)

Implementation is on local branch `cydpod/bootstrap`. No hardware has been flashed or reset. No upstream branch has been pushed. There is no verified DAC harness or battery wiring yet.

The supervised implementation run is `run_3d02bc7cb725`. Inspect its durable state before starting replacement workers; absence of messages is not proof a worker stopped.

Close-out 2026-10-23: the C1 and C2/C4 worker terminals had exited (`worker-show` observation `exited`) without sending `worker_done`, so their dispatches still read `dispatched` in Orca. Their deliverables are present and pass the check below. Fencing those two dispatches with `worker-abandon` needs the user's approval and has not been done. The review dispatch completed normally.

| Work | Task | Dispatch | Ownership |
| --- | --- | --- | --- |
| C1 reproducible Windows build and pinned upstream import | `task_af3e28391fa4` | `ctx_3c13f192269b` | `tools/` except `tools/media*`, dependencies manifest, `.gitignore`, `vendor/`, `.tools/`, `.deps/`, build report |
| C2/C4 minimal AAC and selectable A2DP/I2S prototype | `task_3303aab70985` | `ctx_566440d8b120` | `firmware/`, `tests/`, `fixtures/`, `tools/media*`, AAC prototype report |
| Independent foundation correctness review | `task_f8e865656730` | `ctx_a2629ef9b464` | Read-only code review; writes only `docs/research/foundation-review.md` |

The coordinator owns root README/status/specification and orchestration. Future reviewers report findings without editing implementation workers' files. Workers must not flash devices or mutate Git state in these assignments.

## Historical intake list (superseded by newer device/media reports)

- Exact board display/touch revision and flash/PSRAM observations.
- Which serial port is the CYD. Initial discovery found only COM8 with FTDI identity; that is **not** an identification of the CYD. Do not flash it by assumption.
- A user-selected local folder with representative AAC originals, or equivalent supplied files. No music folders have been searched. Until then, test generated synthetic AAC fixtures and label them as such.
- Actual wired headphone/load and DAC/amp module choice. GPIO22/27/17 is provisional; GPIO17 may need a verified solder pickup and LED isolation.

## Historical bootstrap evidence

Planning/research/review checks passed; see [planning validation](research/planning-validation.md). Helix supports AAC-LC/stereo, but combined CYD memory/timing, actual containers and AirPods interoperability still need executable evidence. Compile success does not pass those hardware gates.

The first implementation deliverables are a safe compile-only check and a minimal probe. Do not advance to UI/artwork expansion by treating generated tones or a successful compile as the mandatory AAC-to-AirPods result.

Foundation evidence (C1 and host-side C2/C4): `powershell -File tools/check.ps1` passes with the explicit original-ESP32/4-MB/no-PSRAM profile. A rerun on 2026-10-23 ended `PASS synthetic fixture matrix: 3 positive, 7 negative` and `PASS: compile-only checks completed; no device commands were run.` Images: player 1,336,576 bytes, uploader 1,147,600 bytes, AAC probe 1,189,936 bytes. The probe SHA256 `35ec4065ba349757d39d9eee7f662249976ba281a9dc5e1765153f8ac7f233e4` matches the reviewed build, so the build reproduces. The pinned Helix LC-only decoder and shared ADTS reader decode synthetic AAC at 128/192/256 kbit/s with reference/tone checks and malformed-input rejection; this is host-only evidence. The [foundation review](research/foundation-review.md) found no open blocking defects. The prototype declares ADTS input only; M4A fixtures are retained for follow-up but are not claimed as supported.

## Usage-limit continuation

Orca's version-matched automation guide documents schedules targeting an existing workspace with session reuse. The running 1.4.237 runtime also advertises `agent-session.send-usage-limit-continuation.v1`. That capability alone does not prove a future retry is scheduled or enabled.

No scheduled continuation, account change, reset-credit purchase or keep-awake setting has been enabled. If an automatic continuation is later requested, use Orca's supported automation/continuation controls and record the exact schedule and target. The host must remain available. A usage limit is not task completion.

On continuation, inspect current files and durable worker state, collect pending messages, preserve worker ownership, then continue from recorded evidence. Do not launch duplicate editors because the previous chat was idle or usage-limited.
