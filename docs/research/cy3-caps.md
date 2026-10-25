# CY-3: allocation caps and error propagation for the M4A reader

Scope: host only (WSL g++ 14.2), no firmware, no hardware. The vendored AudioTools reader (`vendor/arduino-audio-tools`, pinned `e42af05`) is **unmodified**; the caps live in CYDpod-side code:

- `lib/cydpod_m4a/m4a_caps.h` — the named cap constants (one header).
- `lib/cydpod_m4a/m4a_guarded_reader.h` — `M4aGuardedReader`: a streaming box-header guard in front of `M4AAudioDemuxer` plus a capped sample-size buffer. Every failure is an `M4aStatus` return; nothing asserts or aborts.
- `tests/m4a_caps_test.cpp`, `tests/build_m4a_caps.sh` (synthetic malformed inputs), `tests/m4a_caps_real.cpp` (read-only run over real files), `tools/m4a_measure.py` (read-only measurement).

Why a guard and not only a bounded buffer: the reader's allocations are `SingleBuffer`/`Vector` members it owns (stsd buffer, frame buffer, ADTS `tmp`, parser level stack); only the sample-size table is injectable (`setSampleSizesBuffer`). The table backstop (`M4aCappedSizes`) is in place, but its overflow cannot be reported from inside the reader (it wraps `write()` in `assert`, so the backstop latches the error and swallows writes). The guard therefore validates the raw bytes before the reader sees them. No `patches/` mechanism was needed.

## Measurements (25 real files, read-only)

`07 - Dance and Indie Crossover`, all moov-first, AAC-LC 44.1 kHz stereo. Reproduce: `python -I tools/m4a_measure.py <dir>`.

| quantity | min | max |
|---|---|---|
| `stsz` sample count | 7,833 (07-07) | **18,100 (07-15 Loaded)** |
| largest frame size (bytes) | 1,162 | 1,340 (+7 ADTS = 1,347) |
| smallest frame size | 577 | 766 |
| `stsd` payload (bytes) | 83 | 115 (83/95/115 only; 115 = mp4a with esds + btrt) |
| `moov` box | 56,882 | 134,789 |
| `udta`/metadata box | 15,740 | 61,507 (iTunes `ilst` atoms with non-alphanumeric types, cover art) |
| container nesting depth | 5 | 5 |

Counts above 16,384: 07-12 (16,963) and 07-15 (18,100). Next: 07-10 (15,525), 07-13 (13,013).

## Caps (`m4a_caps.h`)

| constant | value | basis |
|---|---|---|
| `kM4aMaxStsdBytes` | 1,024 | only box the reader buffers (`onStsd` resizes from the declared size). Real max 115. Not 4 KiB: the reader's parse buffer is 2 KiB and an stsd that does not fit it whole is mis-read, so 1 KiB leaves slack |
| `kM4aMaxFrameBytes` | 2,048 incl. 7-byte ADTS (entry ≤ 2,041) | real max 1,347 |
| `kM4aMaxTableEntries` | **16,384** (not 2,048) | 2,048 entries ≈ 47 s of audio and rejects every real file |
| `kM4aMaxBoxDepth` | 8 | real files 5 |
| `kM4aHeapBudgetBytes` | 48 KiB | test budget: peak live heap incl. reader state with the table at its cap |

Heap cost on a no-PSRAM ESP32: the table is one contiguous `16,384 × 2 B = 32 KiB` block (`stsz_sample_size_t` is `uint16_t`). Measured host peak with a table exactly at the cap: 44,312 B live (largest single request 32,768 B); a rejected input peaks at ~9 KB (reader object + 2 KiB parse buffer + 2 KiB read buffer + 1 KiB extractor buffer). Guard object adds ~2.2 KB of fixed RAM (stsd copy 1 KiB, held bytes ≈1 KiB). Whether a 32 KiB contiguous block is available alongside Bluetooth + decoder on the real CYD is **not established** (no hardware work here); the cap is one constant to lower.

**Real files rejected by the 16,384 cap (clean `too_many_samples`):** `07-12 - The Chemical Brothers - Galvanize` (16,963 samples, ≈6:34) and `07-15 - Primal Scream - Loaded` (18,100, ≈7:00). The other 23 are accepted. `kM4aMaxTableEntries = 18,432` (36 KiB) would accept all 25; a longer-track guarantee needs the file-backed `BaseBuffer` route from the research note. Nothing is truncated: a file over the cap is refused.

Host run over the real files (`sh tests/build_m4a_caps.sh <files>`): 23 ACCEPT with frame counts equal to the measured `stsz` counts (e.g. 9,985 frames / 9,343,945 ADTS bytes for 07-01), 2 REJECT as above.

## Malformed inputs (all pass; each run in a forked child, three write slicings: 7 bytes, 1024 bytes, whole)

Error returned / peak live heap / largest single request (whole-buffer slicing; budget 49,152 / 32,768):

| case | error | peak B | max req B |
|---|---|---|---|
| valid synthetic (positive, 4 frames) | ok | 9,704 | 3,672 |
| table exactly at cap (positive) | ok | 44,312 | 32,768 |
| table cap+1 | too_many_samples | 9,160 | 3,672 |
| stsz count 0xFFFFFFFF | too_many_samples | 9,160 | 3,672 |
| stsz count 0 / table shorter than count / 4-byte stsz | bad_stsz | 9,160 | 3,672 |
| stsz declared size 0x7FFFFFFF | bad_box_header | 9,160 | 3,672 |
| frame size 0 / 2,042 / 0x10064 (aliases 100 in uint16) | bad_frame_size | 9,160 | 3,672 |
| fixed-size stsz (0xFFFFFFFF, 2,041) | bad_stsz (unsupported) | 9,160 | 3,672 |
| stsd box size 4 GiB−1 / 1 GiB / 512 MiB | bad_box_header / bad_box_header / stsd_too_large | 9,160 | 3,672 |
| stsd payload cap+1 | stsd_too_large | 9,160 | 3,672 |
| stsd alac / 48 kHz | not_mp4a / bad_sample_rate (CY-4) | 9,160 | 3,672 |
| stsd truncated entry / tail that would loop the reader | bad_stsd | 9,160 | 3,672 |
| box size 4, child larger than parent, largesize on non-mdat | bad_box_header | 9,160 | 3,672 |
| mdat before moov (moov-last) | moov_after_mdat (CY-4) | 9,160 | 3,672 |
| 20,000 nested moov / depth cap+1 | too_deep | 9,160 | 3,672 |
| second stsz | duplicate_track | 9,160 | 3,672 |
| truncated in trak header / in stsz table | truncated | 9,704 | 3,672 |
| truncated inside mdat | ok (2 frames) | 9,704 | 3,672 |
| 5,000 nested udta; iTunes non-alnum atoms | ok (4 frames; neutralized) | 9,704 | 3,672 |
| table backstop `resize(4G)` with guard bypassed | latched, write swallowed | – | 0 |

The CY-2a test still passes (`tests/build_m4a_host.sh`: 131 frames, 134,144 samples per channel, PCM IDENTICAL).

## Vendored-reader defects found (not fixed; worked around or documented)

1. **Split box starts.** If a write ends inside the start of `stsd`/`stsz`/`mdat`: `stsz` header words are stored as table entries and hit `assert(sampleSizes.write)` (reproduced as a host abort with 7-byte writes); `stsd` is read from stale bytes; `mdat` never gets `setMaxSize`. Workaround: the guard delivers those box starts in one reader write (“zones”).
2. **Resync on non-alphanumeric box types** (iTunes `©nam` atoms): the parser scans payload bytes for look-alike boxes. Workaround: the guard renames every unneeded box (udta, meta, edts, dinf, non-alnum types, …) to `free` before forwarding, so the reader skips them by size.
3. **`parseString` over the stsd entry** advances by `size−8`, can loop forever on a size-8 look-alike and can invoke `onAlac` (`alac.data_size − 4` underflows). Workaround: the guard rejects entries whose second probe is not past the end / non-alphanumeric.
4. **`stsz_sample_size_t` is `uint16_t`**: a 32-bit entry is truncated, so 0x10064 reads as 100. The guard checks the raw 32-bit entry.
5. **`currentSampleSize()` keeps `static` cache state** that survives `begin()` and new instances (reproduced: the same truncated file gave 3 frames after earlier runs, 2 in a fresh process). **Open:** not fixable from CYDpod without a vendor patch; tests fork a process per run. Must be addressed before repeated-track playback.
6. Fixed-size `stsz` (nonzero sample size) is rejected: the reader cannot parse a 12-byte stsz and would allocate `uint32` sizes.
7. `assert()` around side effects (`assert(buffer.write(...))`) would do nothing under `NDEBUG`; the guard keeps inputs inside the asserted ranges.

Not done here (separate chunks): target compile/heap capture of `M4aGuardedReader` (no firmware change was allowed), moov-last support, file-backed tables for longer tracks.
