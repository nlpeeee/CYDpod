# CY-11b: file-backed table wired into the probe, evidence-based admission

Compile only. No hardware, serial, SD or flashing. Nothing here is a hardware result. Branch `cydpod/cy-10`, parent `91e21cd`.

## Result in one paragraph

The probe now builds an `M4aSeekableSource` over the single open SD handle and calls `reader.begin(cb, &source)`, so `.m4a` tracks use the 1 KiB window table (cap 524,288 entries). The old 96/64 KiB guard is replaced by a post-open check computed from named constants. **On the CY-12a state (45,488 B free after the file is open) that check rejects an `.m4a`: the model needs 49,648 B, 4,160 B more than is free.** The task asked to show admission with at least 10 KiB margin; the numbers do not support that and I did not tune constants to make it so. The admitted-track margin on the measured state would be 6,080 B instead of 10,240 B. `ring` and `producer stack` were not shrunk (see below), so the shortfall is open for the coordinator/CY-11c.

## Source over the SD handle (`SdSource`)

`readAt(offset, dst, size)`: `saved = position()`, `seek(offset)`, `read(size)`, `seek(saved)`. A failed seek restores the position and returns `seekError`; a read shorter than `size` returns `shortRead`; a failed restore returns `seekError`. Offsets above `UINT32_MAX` are `seekError` (FAT32). The reader maps these to `source_seek_error` / `source_short_read` / `source_read_error`; the existing producer path then does `stopConsumers()`, `logTrack("skip")`, `ERR m4a <status>` and moves on (the CY-10c1 path, unchanged). The source is a member of `M4aFeed` (+8 B on a 32-bit target), so it lives exactly as long as the reader. The file is closed after `delete feed` (it was before), and every `readAt` restores the position, so the file position is never left moved. The feed starts at offset 0 on a freshly opened file, as the source offsets require. No second `File`, no second FatFs window.

`sourceReads` (atomic, reset per track) counts `readAt` calls; one refill per 256 frames, so a 9,127-frame file should show about 36 reads plus those of the first `begin()`.

## Evidence

| item | value | source |
|---|---|---|
| Helix decoder peak | 20,680 B | `wsl.exe -d Debian -- sh .tools/cy11-host.sh`: `PASS ... peak_decoder_heap=20680 live=0` |
| Helix largest single request | 20,560 B (`PSInfoBase`; `AACDecInfo` is the other 120 B) | scratch `.tools/cy11b-largest.cpp` with the same `helix_malloc` accounting: `peak=20680 largest=20560` |
| Reader whole peak (includes the 4,752 B reader object, 1,024 B window) | 13,048..13,560 B, 25 real files | CY-11a |
| Reader largest request | 4,752 B (the object), subsumed by the feed block below | CY-11a |
| `sizeof(M4aFeed)` | 9,144 B host layout (reader 4,752 + feed extra 4,392) | `.tools/cy11b-sizes.cpp`; the probe has `static_assert(sizeof(M4aFeed)<=9144)`, so the 32-bit target cannot exceed it |
| Safety margin | 10,240 B | task |
| Board, CY-12a (Bluetooth connected, no m4a playing) | after connect 53,824 free / 51,188 largest; **after the file is open 45,488 / 45,044**; min heap 36,520 | task |

## Thresholds (constants in `cydpod_probe.ino`)

```
kDecoderPeakBytes       = 20,680
kReaderPeakBytes        = 14,336   (13,560 rounded up to 14 KiB; includes the reader object)
m4aFeedExtra()          = sizeof(M4aFeed) - sizeof(M4aGuardedReader)   (<= 4,392)
kPlaybackMarginBytes    = 10,240
need_free    = 20,680 + 14,336 + 4,392 + 10,240      = 49,648   (computed in code from sizeof; <= 49,648)
need_largest = sizeof(M4aFeed) + kDecoderLargestRequest = 9,144 + 20,560 = 29,704
```

`need_free` is the whole new demand (decoder + reader + feed extra) plus the margin that must remain free. `need_largest` assumes the feed block (one `new`) is carved first from the largest block and Helix then needs its own 20,560 B contiguous block from what is left; the reader's internal requests are at most 4,752 B and live in smaller blocks. The check runs after `SD.open` (file buffers already paid, that is where 45,488/45,044 were measured) and before `new M4aFeed` / `decoder.begin()`.

Log line on rejection: `ERR m4a low_memory heap=<n> largest=<n> need=<free need> need_largest=<largest need>`, with `TRACK ev=skip ... m4a=low_memory`.

## On the measured board state

- free: 45,488 - (20,680 + 14,336 + 4,392) = **6,080 B** left after the track, versus the required 10,240. Shortfall **4,160 B**: **rejected**.
- largest: 45,044 - 29,704 = 15,340 B spare: passes. Fragmentation is not the blocker.
- It rejects any lower heap too: it admits only at heap >= 49,648. A state lower than measured by more than the (negative) slack is trivially rejected; there is no state below the measured one that admits.
- With the old 13,560 un-rounded peak the margin would be 6,856, still short by 3,384. The rounding is not what decides this.
- CY-11 predicted a 10,332 B remainder before unknown costs; that was computed from the 57,860 B Bluetooth-start figure with the file allowance and the ring/producer/LC-pcm savings (10,240 B) already assumed. The real post-open figure is 45,488, and none of those savings is in the image, so the prediction does not hold.

## Ring and stack

- `PcmRing<2048> pcmRing;` is a **global**, backed by `Stereo data_[N]` inside the object (`audio_core.h:26`). It is `.bss`, not heap. It is not allocated per track and does not change free heap *during* a session. The producer (8,192 B) and sink (3,072 B) stacks are allocated once in `setup()`, also before every METRIC. `decoder` (`AdtsDecoder`, 10,288 B incl. `pcm_[4096]`) is global too; only Helix's 20,680 B is heap.
- Correction to CY-11's wording: a smaller `.bss` does enlarge the heap region at the next boot one-for-one, so shrinking the ring (2,048 -> 1,024 frames, 4,096 B) would raise the boot heap by 4,096 B and nearly closes the 4,160 B shortfall. It is a build-time resident saving, not a per-track one.
- **Not changed.** Safe is not shown: the producer fills the ring in 1,024-frame pushes and sets `ready` at 1,024 frames, so a 1,024-frame ring would have `ready` only when full and 23 ms of buffering, with no underrun data from an `.m4a` run. Producer stack 8,192 has an idle high-water of 2,380 B used (CY-9b); decode and the file-backed refill (`File::seek` through VFS/FatFs) are unmeasured, so 6,144 is not shown safe either. Both are listed as options below, for CY-11c to decide with `producer_stack` and `underrun_*` data.
- Options that would close 4,160 B, none applied: ring to 1,024 frames (+4,096), `AdtsDecoder::pcm_` 4,096 -> 2,048 int16 (+4,096 if LC bound is proven), producer stack 8,192 -> 6,144 (+2,048), sink 3,072 -> 2,048 (+1,024). Either 4,096 B item alone is 64 B short; two together, or one plus the stack cut, is enough.

## Telemetry for CY-11c

- `TRACK ev=live ...` once per track right after the reader and decoder are allocated, then `ev=start` (same state). Both now end with `producer_stack=<bytes> source_reads=<n>`. `METRIC` gains `source_reads=<n>` at the end (the existing `producer_stack` stays in place). `-Werror=format` is still on and the build passes.
- Model check on hardware: `heap` at `ev=live` should be about `45,488 - (20,680 + reader live + 4,392)`; consumption actually measured = free after open - free at `ev=live`. If it is under 38,640 the constants can be tightened with evidence.

## Check

`powershell -File tools/check.ps1` exit 0 (`.tools/logs/cy11b-check.txt`). Host suites `tests/build_m4a_{host,caps,reject,sequence,r1r5,filetable,real}.sh` pass (real: accepted=25 mismatches=0). Session dry-run passes in `powershell` 5.1 and `pwsh`.

| | bytes | static RAM | sha256 |
|---|---|---|---|
| CY-11a probe | 1,224,528 | 61,552 | `a31743320080bb41d7e1e0b22d22959271711745550bd4b9a744766b01e11cc5` |
| CY-11b probe | 1,224,944 | 61,552 (+0) | `b25eec1eaf5d35daebbdb03339235c358d45708187e48627d81acbfb48e91121` |

+416 B image; 62.3% of the 1,966,080 B slot.

## Update CY-13a: provisional margin 2,048 B (status: hardware floor unmeasured)

`kPlaybackMarginBytes` is now **2048** (was 10,240). The rest of this document describes the CY-11b state at 10,240 and is kept as history.

```
need_free = 20,680 + 14,336 + 4,392 + 2,048 = 41,456   (45,488 measured free after open -> 4,032 B slack, admitted)
need_largest = 29,704 (unchanged; 45,044 largest -> 15,340 spare)
```

The 2,048 B is a provisional choice, not a measured floor: no track was played in CY-11c. If heap runs out during playback the board reboots; the allocation-failure fix (R2) is open. Image: `aac-probe` 1,224,944 B, static RAM 61,552 B, sha256 `002f811ffaedf48cee190f9dfb225ad013f8557a1f1bb687034d620d00a20914` (the CY-11b image `b25eec1e...` had the same size). See `cy13a-playable-build.md`.
