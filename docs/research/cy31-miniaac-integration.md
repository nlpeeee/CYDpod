# CY-31: MiniAAC replaces the probe's M4A/AAC path

2026-10-25. Compile and WSL Debian host tests only. No serial port, SD card, flashing or esptool was used; the board still runs the CY-13c image. Branch `cydpod/miniaac` (from `cydpod/cy-10`).

Scope note (coordinator, mid-task): MiniAAC is validated by its author (FFmpeg comparisons, three SD tracks on the actual CYD), so its own host suite, corpus checks and decoder quality were **not** re-run here, and the planned 25-file PCM-hash comparison against the old path was dropped. What was tested is the new part: the adapter and the integration.

## What was imported

| Item | Value |
|---|---|
| Archive | `miniaac-1.0.0-hardware-tested.zip` from the user's MiniAAC Codex thread (`...\Codex\2026-10-25\i\outputs\`), read-only |
| Archive SHA256 | `deead1c51dfbcf9c9e4675a20aa9dbec3cc2e016841ab5541b7be6cb510e1540`, equal to its `.sha256` file; the unpacked tree also passes its own `SHA256SUMS` and is identical (`diff -r`) to the zip contents |
| Version | `library.properties` name MinimalAAC, 1.0.0 (the board-tested build). The loose, newer `miniaac\` tree (1.1, seeking) was **not** imported |
| Helix core | codec-helix `f6e55a307c7577c5e3dfe1bf424a61075235fa08`, local changes in `vendor/miniaac/UPSTREAM.md` and `patches/helix-minimal.patch` (minimal `ConfigHelix.h`, heap entry points disabled, one PCE kept, SBR fill detection) |
| Location | `vendor/miniaac/` (56 files, none edited), per-file hashes `tools/miniaac-files.sha256.json`, entry in `dependencies.lock.json`, row in `vendor/NOTICES.md`, licence copies `vendor/notices/MiniAAC-LICENSE-MIT.txt` and `MiniAAC-codec-helix-GPL-3.0.txt` |
| Gate | `tools/check.ps1` now runs `Test-FileInventory` for AudioTools and MiniAAC (every listed file present with the right SHA256, no unlisted file) before compiling |

Licences: MiniAAC's wrapper, parser, examples and tools are MIT; the Helix AAC core is under the codec-helix GPL-3.0 text with the RealNetworks RPSL/RCSL notices kept in the source headers. MIT does not relicense Helix, and the combined probe binary is **not** described as wholly MIT. (The archive's `README.md` and `docs/` describe version 1.0; the zip has no seek API, `tests/seek_test.c` or jog example.)

## Build wiring

`tools/check.ps1` compiles the probe with `--library vendor/miniaac` (the library has `library.properties` and `src/`; Arduino discovers it from `#include "miniaac.h"`) and flags `-DCYDPOD_AAC_LC_ONLY` only. AudioTools, arduino-libhelix and `lib/cydpod_m4a` are **no longer on the probe's build path** (the `Used library` list of the log shows SPI, SD, FS, Preferences, ESP32-A2DP, MinimalAAC). The retained AudioTools overlay patches are still applied to a staged copy so that they keep applying. `lib/cydpod_m4a/`, `adts_decoder.h`, their host tests and the AudioTools overlays stay in the repository unchanged and unused by the probe. `-Werror=format` is unchanged (`--warnings default`; pragma blocks in the .ino and the adapter).

## Adapter design (`firmware/cydpod_probe/miniaac_adapter.h`)

`cydpod::MiniAacTrack`, one **static** object (`miniPlayer`) holding the workspace and the PCM buffer; no heap use anywhere on the play path.

- **One handle.** `miniaac_source.read_at` is `readAt(user, offset, dst, n)` over the same open SD `File`: seek only if `file->position() != offset`, then `read`; any failure (offset above 4 GiB, seek failure, short read) returns 1, which MiniAAC maps to `io` and then keeps as a sticky error. The adapter is the only user of the handle while a track plays; the old save/restore of the position is not needed. The `File` must outlive `end()`; the producer calls `miniPlayer.end()` before `file.close()`. `reads` counts callbacks (the `source_reads` field of `TRACK`/`METRIC`).
- **Workspace.** Allocated once, statically, as `MINIAAC_WORKSPACE_BYTES` = 22,528 B (the library's own compile-time bound; `miniaac_workspace_size()` is 22,480 B on xtensa, so 48 B are spare), 8-byte aligned, plus the 4,096 B `int16_t pcm_[2048]`. `sizeof(miniPlayer)` in the ELF is `0x6820` = **26,656 B** in `.dram0.bss`. Static, not a startup allocation, because it removes fragmentation and failure modes: reopening per track reuses the same arena (MiniAAC resets all state on open).
- **PCM path.** `next()` calls `miniaac_decode_next` into `pcm_`; `pending()/pcm()/consume()` let the producer push into the unchanged `PcmRing<2048>` and keep the remainder when the ring is full, so a block trimmed by the edit list (fewer than 1,024 frames) or a partly accepted push needs no special case. The ring, `sinkTask`, `btData`, gain ramp and consumers are untouched. `decodedFrames` now counts decoded blocks.
- **Formats.** `.m4a` and `.aac` both go through `miniaac_open`, which decides by content (ADTS sync or M4A). Any other extension is therefore also content-detected (the old probe treated it as ADTS). The output chain is fixed at 44.1 kHz stereo (A2DP, I2S config), so after `miniaac_get_info` any other rate or channel count is rejected with `not_44100_stereo` (nothing resamples or mixes down). Rejection is clean: state `not_44100_stereo`, `TRACK ev=skip`, `ERR m4a not_44100_stereo`, file closed, next command served.
- **Status names.** `m4aState` and the `ERR m4a <name>` lines use MiniAAC's statuses lower-cased: `ok`, `eof`, `argument`, `memory`, `io`, `format`, `unsupported`, `frame_too_large`, `codec`, `output_too_small`, plus `not_44100_stereo` and the probe's `low_memory`. (`adts_queue_overflow`, `alloc`, `source_*` and the reader statuses no longer exist.) `m4a=` in `TRACK`/`METRIC` is now `ok` for `.aac` tracks as well; `na` is the idle value.
- **Kept as is:** commands, `ls` (producer only), CY-10c1 cancel/queue/epoch behaviour (the epoch is checked every loop pass; an error reports only if the epoch is unchanged), the atomic published status, the Bluetooth connect timeout, `TRACK ev=live|start|end|skip`, `METRIC` (same 17 fields), `logTrack`, `sampleLargest`, the 8 KiB producer stack (`xTaskCreatePinnedToCore(..., 8192, ...)`).
- **Removed from the play path:** `M4aFeed`, `M4aGuardedReader`, the arena and its `heap_caps_malloc` acquire/release, the allocator hook, `SdSource`, the 4 KiB ADTS queue, `AdtsDecoder`, the Helix allocating init/free, the `largest`-block admission test, `endsWithNoCase` (extension no longer chooses a path).

## Memory budget

Sources: `docs/research/cy17-pcm-buffer.md` (CY-17 image: static RAM 57,464 B; free heap after Bluetooth connect and file open 45,488 B on the CY-13a state, predicted 49,584 B with CY-17's +4,096 B), `cy10b-alloc-failure.md` / `cy11b-admission.md` (old track-time heap model), `cy13c-final-image.md` (boot free heap 148,876 B), this build's log and ELF.

| | CY-13c image (CY-17 decoder buffer) | MiniAAC image |
|---|---:|---:|
| Image | 1,226,960 B | **1,197,216 B** (slot 1,966,080 B) |
| Static RAM ("Global variables") | 57,464 B | **77,344 B** (+19,880 B) |
| `.dram0.bss` object | `decoder` (AdtsDecoder) 6,188 B | `miniPlayer` 26,656 B |
| Heap taken at each track | Helix 20,680 + arena block 16,392 + reader 4,344 + feed extra 4,376 = 45,792 B (largest block 45,672 B) | **0 B** |
| Admission rule | free ≥ 47,840 B and largest ≥ 45,672 B (= 45,792 + 2,048 margin) | free ≥ 0 + **10,240 B** |

Arithmetic against the measured board state, all on the free internal heap after Bluetooth connect and file open:

- Old image: 45,488 B measured (CY-13a/CY-12a state), 49,584 B with CY-17 (+4,096). Needed 47,840 B → at best +1,744 B slack over a 2,048 B margin; with the conservative 45,044 B largest block 628 B short.
- New image: static RAM grows by 19,880 B (read from the two builds' "Global variables" lines; this includes all other static differences, such as the removed libhelix and AudioTools statics), and nothing is allocated per track. Free heap at the same point is therefore predicted **49,584 − 19,880 = 29,704 B** (equivalently 45,488 + 4,096 − 19,880). The corresponding boot value is 148,876 − 19,880 = **128,996 B**.
- Requirement: 0 B + **10,240 B** margin. The margin is **19,464 B** above that (29,704 − 10,240) at the measured state, i.e. admission fits with a wide margin; even without CY-17's saving (45,488 − 19,880 = 25,608 B) it fits with 15,368 B to spare. No largest-block requirement exists any more since no allocation is made, so the unmeasured fragmentation after Bluetooth no longer decides admission. Unlike the old model the 10,240 B is a floor kept for what Bluetooth and SD allocate while playing, not a prediction of something this code takes.
- Component cross-check from MiniAAC's own report and the user's comparison: decode+container 26,576 B (22,480 workspace + 4,096 PCM) versus 51,956 B of the old path, saving 25,380 B. Here the equivalent static-versus-dynamic figure is 26,656 (`miniPlayer`) against 45,792 heap + 6,188 static = 51,980 → saving 25,324 B. The 19,880 B static growth is the same saving seen from the other side (old heap demand moved into .bss, minus the shrinkage of other statics).
- Honest limit: the 29,704 B figure is a prediction from the previous measurement and this build's static size. The true free heap while playing, with the Bluetooth stack under load, has not been measured on this image.
- **Stack.** The producer stays at 8,192 B. Evidence: MiniAAC's isolated SD benchmark used at most 2,336 B of a 4,096 B stack (1,760 B free) including the read callback and filesystem; compiler frames are 544 B for `miniaac_open` and 112 B for `miniaac_decode_next`; the CY-13c idle board reported `producer_stack` (high-water free) 4,584 of 8,192 B, so the task's own control code had already used 3.6 KiB at boot, which the benchmark's 2,336 B did not include; the old M4A path (guarded reader plus vendored demuxer) was deeper than MiniAAC's. Only the measured numbers above exist and the sum (control code high-water plus MiniAAC's open and decode chain plus the SD/FatFs callback) was not measured on this task, so I could not justify 4 KiB. Check `producer_stack` on hardware.

## Host tests

`wsl.exe -d Debian -- sh /mnt/e/dev/CYDpod/tests/build_miniaac_adapter.sh` (g++ 14.2 / gcc 14.2, Debian under WSL; reads the media read-only): compiles the MiniAAC C sources with its CMake flags and `tests/miniaac_adapter_test.cpp` (`-Wall -Wformat=2 -Werror=format`) against the real firmware header, with `tests/host_shim/FS.h` standing in for Arduino's `File`.

- All **25** real files in `E:\Alternative Wedding - UK Nostalgia\Tracks\07 - Dance and Indie Crossover\` decode through the adapter's read callbacks to `eof` with status `ok`, draining `pending()` in uneven slices (37 to 999 frames) like a nearly full ring. Decoded blocks versus the old path's frame counts in `tests/real_media_expected.tsv`: equal for 5 files (06 Room 5, 12 Galvanize, 14 Everybody in the Place, 21 Cassius 1999, 25 D.A.N.C.E.) and **2 fewer** for the other 20 (e.g. 07-01: 9,983 vs 9,985; 07-20: 10,665 vs 10,667). The difference is MiniAAC dropping blocks that lie wholly inside the edit-list trim (the old path emitted all `stsz` samples). PCM frame counts are within one block of the decoded blocks (for example 07-21: 9,552 blocks, 9,781,248 frames). The test fails if blocks are not within 0..2 of the manifest or the PCM count is not inside the block count.
- Also exercised: a file cut to 4,096 and to 64 bytes, a non-AAC file (the manifest) and a decode after those failures (adapter reusable), double `end()`, `next()` on a closed track (error, no crash). Source read callbacks per file are about one per decoded block (`reads` ≈ blocks), i.e. no read amplification.
- Not run, by instruction: MiniAAC's own suite (`tools/check.py`, `tests/validate.py`, corpus), the PCM comparison with the old Helix pin, and any quality judgement. The old suites for the retained reader files were not re-run (they test files this change does not touch).

`tools/check.ps1`: **PASS** (`.tools/logs/cy31-check.txt`): inventories (CYD-MP3-Player, AudioTools, MiniAAC), player 1,336,576 B and uploader 1,147,600 B unchanged, probe, synthetic fixtures and host media.

## Build result

| | Previous (CY-17/CY-13c) | CY-31 |
|---|---|---|
| `aac-probe` size | 1,226,960 B | **1,197,216 B** (−29,744 B) |
| SHA256 | `5b2b0b555020b6377c3aff0ee9b57e5734ad2a463b2a382f7d231de9d0b4697e` | **`f9cab1b1930a0bbe81d9896a3aff8b95c7e2168b8d1255691cda1857b6887c45`** |
| Static RAM | 57,464 B | **77,344 B** |
| Slot | 1,966,080 B | fits (60%) |

The pre-change image was copied to `.tools/images/cy13c-final-saved/` (ignored by git) so the CY-13c image stays available for restoring the board.

## Not tested on hardware (all of it)

Bluetooth playback and sound on the actual AirPods Pro 2 and the stereo line/headphone paths; the real free heap and `min_heap` during playback; `producer_stack` under the full control code; output switching with MiniAAC open (`output` while playing closes the track, the same flow as before but not run); stop/cancel races (`CY-10c1` logic unchanged, but untested with the new loop); underruns (MiniAAC measured 32.7 ms for its slowest read+decode call against a 23.2 ms block, so the 8 KiB / 2,048-frame ring, 46 ms, is just enough: watch `underrun_frames`); SD behaviour with `readAt` on the shared handle; `.aac` ADTS files on the card. Nothing here claims AAC-LC 44.1 kHz stereo acceptance on hardware.

## Flash and test by hand (separate authorised chunk; identify the actual CYD first, COM10 was the board in CY-11c)

```powershell
Set-Location E:\dev\CYDpod
$esp = '<esptool path used in docs/research/cy9-test-plan.md>'
$flashFiles = @('0x1000','.tools/images/aac-probe/cydpod_probe.ino.bootloader.bin',
 '0x8000','.tools/images/aac-probe/cydpod_probe.ino.partitions.bin',
 '0xe000','.tools/build/aac-probe/boot_app0.bin',
 '0x10000','.tools/images/aac-probe/cydpod_probe.ino.bin')
(Get-FileHash .tools/images/aac-probe/cydpod_probe.ino.bin).Hash   # expect F9CAB1B1...6887C45
& $esp --chip esp32 --port COM10 --baud 115200 write-flash --flash-mode dio --flash-freq 80m --flash-size 4MB @flashFiles
& $esp --chip esp32 --port COM10 --baud 115200 verify-flash --flash-mode dio --flash-freq 80m --flash-size 4MB @flashFiles
python -m serial.tools.miniterm COM10 115200
```

In the terminal: `ls /Music`, `output bt`, `volume 3`, `pair <speaker>`, `stats` (expect `connected=1`), `play /Music/<track>.m4a`, `unmute`. Record `TRACK ev=live` heap (prediction: about 29.7 kB free after open, no drop at `ev=live` because nothing is allocated), `min_heap` throughout (must stay above 10,240), `producer_stack`, `underrun_frames=0`, `source_reads` about the number of blocks, `ERR m4a ...` lines, then `play` a second and third track, `stop`, `output line` / `output hp` while playing. Expected rejections: 48 kHz or mono files print `ERR m4a not_44100_stereo`. To restore the previous image: the files in `.tools/images/cy13c-final-saved/` with the same offsets (hash `5b2b0b55...b4697e`).
