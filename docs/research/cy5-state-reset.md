# CY-5: M4A reader state across repeated, mixed tracks

Scope: host only (WSL g++ 14.2), no firmware, no hardware. `vendor/` and `tools/audiotools-files.sha256.json` are untouched; no `patches/` mechanism was needed, so **CY-8 (firmware) needs no patch**, only the CYDpod-side `lib/cydpod_m4a/` files below.

## Finding

`SampleExtractor::currentSampleSize()` (`M4ACommonDemuxer.h:327`) declares `static size_t last_index, last_size` inside an inline member function: one pair for the whole program, shared by every reader instance, never touched by `begin()`. A lookup returns the cached size without reading the table when `sampleIndex == last_index`.

A new track starts at index 0. If the previous track's *last* lookup was also index 0, the first lookup of the new track returns the old track's frame size and does not consume the new table: the track is then read one table entry out of step and stops after one frame. That previous state happens when the previous track had a 1-entry table, or playback was aborted/truncated inside frame 0 (the guard accepts a truncated mdat). Any other previous track ends with `last_index = N-1 >= 1` and is harmless, which is why a plain faststart -> faststart sequence looks fine.

Second defect, found with the same test: a used `M4aGuardedReader` crashed (SIGSEGV in `SampleExtractor::write`, after `LOGE("No sample size defined")`) when `begin()` was called again on the same instance, because `M4AAudioDemuxer::begin()` alone does not make a used instance reusable. The CY-3 note "construct a new one per track" was the only protection.

## Reproduction (before the fix)

`tests/m4a_sequence_test.cpp`, `tests/build_m4a_sequence.sh`. Each track is compared (status, frame count, samples per channel, ADTS bytes + hash, PCM hash after Helix decode) with a reference run in a process forked **before anything was played** (a fork inherits the statics, so a later fork is not a clean reference). Sequences run in forked children so a crash is reported. Tracks: `stereo-192.aac` (decoded directly), faststart `.m4a` (131 frames, 134,144 samples/ch), in-memory variants of it (stsz cut to 40 or 1 entries, all ancestor box sizes and the mdat rewritten; and a mdat cut 5 bytes into frame 0 with the full table), `stereo-moov-last.m4a` (rejected). Build with `CYDPOD_SEQ_FLAGS=-DCYDPOD_NO_STATE_RESET` for the pre-fix behaviour.

Before (`FAILED 4 track comparisons`), identical for a new reader per track:

| sequence | track | result vs fresh |
|---|---|---|
| A: aac, m4a, 40-frame, m4a, moov-last, m4a | all six | equal (a 40-frame table ends at index 39, harmless; the rejected file never reaches the extractor) |
| B: m4a, **1-frame**, m4a, **aborted in frame 0**, m4a, 40-frame, m4a | m4a after the 1-frame track | **1 frame / 1,024 samples instead of 131 / 134,144; ADTS 72,778 B instead of 73,337** |
| B | m4a after the aborted track | **same: 1 frame instead of 131** |
| A, B reusing one reader through `begin()` | 40-frame / 1-frame track after the first | **SIGSEGV** |

Real files (read-only, `07 - Dance and Indie Crossover`) were played as extra tracks, alternating with the faststart fixture: 07-07 (7,833 frames) and 07-01 equal their fresh run; 07-12 is rejected `too_many_samples` (16,963 > cap) in the middle and the next valid track still plays correctly. None of them triggers the stale case, only index-0-ending tracks do.

## Fix (CYDpod side)

- `lib/cydpod_m4a/m4a_state_reset.h`: `m4aPrimeSampleSizeCache()`. The statics cannot be named, so a throwaway `SampleExtractor` subclass (its `sampleIndex` and `currentSampleSize()` are `protected`) sets index 1, adds one table entry and looks up once, which leaves `last_index == 1`. No new track's first lookup (index 0) can then hit the cache. The primer must not start at index 0: it would take the same stale hit (first attempt did and did not fix it).
- `M4aGuardedReader::begin()` is now a full reset: it destroys and placement-constructs the vendored `M4AAudioDemuxer` and `M4aCappedSizes` (fresh sample-size table, chunk offsets, buffers, counters, parser stack, `overflow` flag), clears the guard's scan state, then primes the cache. Calling `begin()` on a used instance is therefore equivalent to a new instance. `-DCYDPOD_NO_STATE_RESET` compiles this block out (tests only).

After: every track in sequences A and B equals its fresh-reader result for both modes (new reader per track, one reader reused via `begin()`), including the 1-frame and aborted tracks, the rejected moov-last in the middle and real files: `PASS every track equals its fresh-reader result`.

Residual: the cache is process-wide, so two readers *interleaved* in time (two tracks decoded concurrently) would still share it. The player decodes one track at a time; a second concurrent reader would need the vendor fix (a member instead of a function-static), i.e. a patch, which is out of scope here.

## Heap cost of a fresh reader per track (host, malloc wrapped; `tests/m4a_reset_cost.cpp`)

| step | bytes |
|---|---|
| `sizeof(M4aGuardedReader)` | 3,672 |
| `new M4aGuardedReader` (object + the reader's constructor allocations) | 5,048 live |
| `begin()` (rebuild + primer; primer peak alone +1,056, freed on return) | +4,112 live, 9,160 total live |
| `delete` | 0 live (returns to baseline) |
| sample-size table, allocated by the stream while parsing | up to 32 KiB (CY-3 cap), freed with the reader |

So per track: ~9.2 KB fixed + the table (peak 44,312 B at the cap, as measured in CY-3). Allocating a new reader per track means the 32 KiB table is freed and re-requested on every track change, a fragmentation risk on the no-PSRAM ESP32; reusing one reader via `begin()` has the same rebuild cost, so prefer keeping one long-lived reader object (stable 3.7 KB) and calling `begin()`. Neither was measured on hardware.
