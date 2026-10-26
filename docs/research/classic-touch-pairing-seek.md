# Classic Touch: live pairing and seeking, v7 ready

2026-10-26. Implemented the saved plan in `docs/specs/classic-touch-pairing-seek.md` following the user's “proceed”. Source `ba27e7f35a0535e7a93cdb5aba24471736045d1f` on `cydpod/pairing-seek`, in `E:/dev/.worktrees/CYDpod/cy43-heap`. Full compile/host checks pass. **No flash, erase, serial port, board or SD-card operation was performed.** Main checkout tracked source and its pre-existing build-baseline edit were not changed; vendor/core/dependencies remain pinned.

## Saved state and delivery

V5 rollback image and source history were saved before implementation under `.tools/backup/pre-pairing-seek/` (`classic-touch-ready-v5`, `source.bundle`). These are local source/image backups, not a device/NVS backup. The older CY-43 v4 backup remains available. Original v5 application SHA256 is `8ac3c01e93b1b2a2aa39986eea68c3b6bc5d6e04ba03b64fde8fb14aa85562e0`.

Recommended image: `.tools/images/classic-touch-ready-v7/`. It contains app, bootloader, partition table, boot_app0, ELF/map, source/file SHA256 manifest, check evidence and `flash-command.txt`. All source application files match the final build staging copy byte-for-byte. Binary: **1,377,312 B**, SHA256 `422b268c8e046c0afd44d01ebfceba52997c9db0443df031a4b767125b7f210b`, below the 1,966,080 B slot limit.

An earlier independent pairing-only comparison image was frozen as v6 from `e6c3fd9`. It intentionally retains v5 seeking behavior; later pairing-progress/immediate-cancel/inquiry-start edge fixes are included in v7. Use v7 for the complete fixes; v5 is the saved pre-change rollback. No image is claimed hardware-tested.

## Behavior implemented

- Pair new and Cancel use the existing Bluetooth event task/controller, without `ESP.restart`, stack stop/start or bond removal. Playback intent, track/queue and volume survive live discovery; prior BT play intent resumes on reconnection. Cancel reconnects the former address, including cancellation before disconnect finishes. Forget remains an explicit separate action.
- The pairing policy waits for disconnect and inquiry start/stop acknowledgments, suppresses normal saved-address paging during the live session, filters stale/unselected peers, and handles API failures/timeouts. Commands carry generations; address filtering cannot prove which same-address over-the-air attempt produced a late callback. Radio/peer timing remains a hardware check. Progress/failure and SSP codes are visible; settings persistence stays outside the small BT event stack.
- A hash-verified MiniAAC overlay is applied only to disposable staging. Existing M4A timing/edit validation and bounded `stsc/stco/co64/stsz` caches locate an audible-frame target. Helix flush and eight-packet preroll use the existing PCM storage. No full-track index, PCM copy or decoder allocation is added to firmware; ADTS explicitly rejects seeking. Corrupt lookup/mapping tables and source I/O failures fail cleanly.
- The decode task alone seeks/reads the File. A short shared critical section protects ring publication/copies and flush/clock handoff; SD, decoder calls and waits remain outside it. Requests coalesce, track changes supersede older seek work, repeated relative seeks use the latest requested goal, and seek waits yield/cancel. Paused/disconnected seek requires no callback acknowledgment. During seeking, pulls emit silence and do not count deliberate silence as an underrun.
- Saved-position resume now uses the seek path and starts paused. Existing crash-reset skip-to-next-at-zero guard is unchanged. Failures pause and report the error without skipping the queue; a subsequent supported seek can retry. Paused EOF does not advance. The progress bar supports tap/drag preview and one commit on release, with vertical/context/track cancellation and disabled unknown duration. Long Previous/Next seek minus/plus 10 seconds; short presses keep navigation. Gestures claim the calibration hold. The legacy player behavior/comments agree.

## Verification and memory

| Check | Result / evidence |
|---|---|
| `powershell -NoProfile -File tools/check.ps1` | PASS all four firmware builds, pristine inventories, catalogue/navigation/Classic UI, speaker lifecycle, host media checks; `.tools/logs/pairing-seek-ready-check.txt` |
| `sh tests/build_miniaac_seek.sh` in WSL | PASS front/tail moov, co64, fixed/variable sample sizes, multi-run mappings, backward/mid-packet/end/clamped seek, continuation, pre/post-flush cancellation/retry, injected I/O failure, damaged tables and ADTS rejection; `.tools/logs/seek-host.txt` |
| Production audio task with host SD/RTOS stand-ins | PASS saved resume, pause/disconnected seek, relative/absolute coalescing, exact first post-seek PCM, stale-ring removal, track supersession, paused EOF, concurrent pulls, failure/retry and queue/play intent; `.tools/logs/audio-seek-host.txt` |
| Pairing policy / real Classic UI host code | PASS 20 cycles reusing policy state, inquiry acknowledgments, all transport API failures, cancel/retry, peer filtering; tap/drag/cancel/unknown duration/start/end, landscape geometry, both long transport holds and calibration claim; in final check log |
| Five representative local music files, read-only | PASS seek/continuation comparisons with sequential reference; first returned multi-sample windows bit-exact (`worst_snr=300` is the test's no-error sentinel, not a measured 300 dB); `.tools/logs/seek-real.txt` |
| Existing adapter regression on patched library | PASS 25 real files, expected block/frame counts and failure/reuse tests; `.tools/logs/seek-adapter.txt` |

Generated ordinary seek windows exceed 58 dB SNR against sequential decoding; the repeated fixed-size-packet fixture exceeds 48 dB. The 20-minute fixture has 52,920,000 audible frames. A deliberately worst-case single variable-size chunk required up to 817 source reads / 209,991 B per tested seek, while the ordinary long fixture needed at most 47 reads / 13,163 B. Reads remain bounded individually and table walks are cancellable; **constant or sub-second SD latency is not promised**. Test PCM/reference vectors exist only in host tests. Host elapsed times are not ESP/SD/radio latency.

Compiler static RAM: **75,424 B**, **+88 B versus v5** (all pairing, seek and UI state combined), within the proposed 512 B additional persistent seek-state budget. The MiniAAC 22,528 B arena and complete `m4a` 26,656 B object remain unchanged. Keep the 8,192 B PCM ring and 4,096 B decode task stack. Task high-water and actual audio throughput after the new short critical sections still require device measurement.

The former v5 446-track arithmetic free-heap forecast of 46,408 B becomes **46,320 B** after the measured static increase, before radio/UI transient variation. This is a forecast, not connected-stream free heap. Largest block remains unknown; **40 KiB free / 25 KiB largest is still unverified**. No hardware memory or audio pass is inferred from host tests.

## Prepared flash command

The previous missing-bootloader error is avoided by packaging all four required files and using an absolute image/boot_app path. This is prepared only; the existing flash script targets COM10 and verifies four regions without erase. Run when the board is available:

```powershell
Set-Location 'E:\dev\CYDpod'
powershell -NoProfile -File tools/cy11c_flash.ps1 -ImageDir "E:\dev\.worktrees\CYDpod\cy43-heap\.tools\images\classic-touch-ready-v7" -Tag classicv7 -Base app.ino -BootApp "E:\dev\.worktrees\CYDpod\cy43-heap\.tools\images\classic-touch-ready-v7\boot_app0.bin"
```

## Remaining hardware acceptance

Confirm the installed image, keep a v5 comparison and exclusive board session, then test Pair new/Cancel while connected, paused, discovering and connecting. Repeat at least 20 new-device/cancel/switch cycles with Xiaomi and another available peer; record boot banners/reset reason/backtrace if any reset remains. Host policy tests do not establish actual Bluedroid callback ordering, bonds, SSP or peer interoperability.

Seek known content to 25/50/90%, backward and near end, both paused and playing, on BT and speaker; test long single-chunk files, rapid seek then Next/Back/output changes, normal resume and crash-guard resume. Listen for stale samples/clicks and compare audible position; decoded-frame accuracy does not remove A2DP output latency. Eight-packet history passed the tested media, not every possible AAC encoder/content.

Capture each boot-stage heap line, `heap: after audio startup`, `bt: started (live pairing)`, connection/audio/media acknowledgments, `seek: starting`, completed target/actual/elapsed and failed lines, plus `stat:` free/min/largest, loop/decode stack, ring/underruns, pulls/frames and SD latency. Require advancing audio and stable heap through 30 minutes of seeking/browsing/route changes. Measure the retained 4 KiB decode stack on seek/error paths and whether largest block reaches 25 KiB; timing or memory shortfalls remain open until measured.
