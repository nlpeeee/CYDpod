# CY-11a: file-backed sample-size table (1 KiB window)

Host-only chunk. No hardware, serial, SD or flashing. Hardware success is not claimed.

## Design

- `lib/cydpod_m4a/m4a_file_sizes.h` now holds `M4aCappedSizes` (moved out of the guard header) and the source interface `M4aSeekableSource::readAt(offset, dst, size)` returning `ok / readError / shortRead / seekError / badEntry`.
- Same seam as before: `M4aGuardedReader` owns the table and injects it with `demux.setSampleSizesBuffer(sizes)`; `begin()` still rebuilds demuxer and table in place (CY-5 reset and primer unchanged). `begin(cb, source = nullptr)`: with a source the table is file-backed, without one it is the old resident table (kept for the host comparison and for firmware until 11b).
- File mode: `resize(n)` records the count (cap check), `write()` is swallowed (the guard's streaming scan has already validated every raw entry, so the vendor's `assert(write)` is satisfied), `read()/peek()` serve entries from a 1,024 B window (256 raw 32-bit entries) refilled from `tableOffset + 4*index`. The guard sets `tableOffset` to the stream offset of the first raw entry when it sees the `stsz` header.
- Sequential reads refill once per 256 frames (about 5.9 s of audio). Repeated reads of one index stay inside the window (vendor `currentSampleSize()` also caches). `reset()` (vendor `begin()` clears the table) drops count, window and error, so the rewind between `begin()` calls re-reads from entry 0.
- Each entry read back is re-checked (non-zero, <= 2041). A changed or corrupt source gives `badFrameSize` instead of a bad frame size reaching the reader.
- Source failures latch in the table and become statuses after the vendor write returns: `source_read_error`, `source_short_read`, `source_seek_error` (appended to `M4aStatus`; existing values unchanged). Status is sticky; `begin()` makes the reader reusable. The vendor logs "No sample size defined" at that point; it does not loop or abort.
- The file-backed path does not truncate: counts above the cap are refused.

## Limits

| constant | old | new |
|---|---|---|
| `kM4aMaxTableEntries` (scan cap, file-backed) | 16,384 | **524,288** (2 MiB of table in the file; 3.38 h at 44.1 kHz / 1024 per frame) |
| `kM4aResidentEntries` (no source, `tableOverflow` above) | n/a | 16,384 (unchanged resident behaviour) |
| `kM4aWindowEntries` / `kM4aWindowBytes` | n/a | 256 / 1,024 |

Box length vs count agreement (`payload == 12 + 4*count`), count 0, non-zero fixed size, per-entry range, and the 64-bit/`UINT32_MAX` box checks are unchanged.

## Changed expectations (deliberate)

- `tests/real_media_expected.tsv`: `07-12` (16,963) and `07-15` (18,100) change from `too_many_samples/0` to `ok/16963` and `ok/18100`; real-media result 23 accepted + 2 rejected becomes **25 accepted**. `tests/m4a_caps_real.cpp` now runs with a file source.
- `tests/m4a_caps_test.cpp`: "table exactly at cap" now uses the resident cap (16,384, ok); "table cap+1" becomes the resident case, status `table_overflow` (was `too_many_samples`); new "file table count cap+1" (524,289) gives `too_many_samples`. `0xFFFFFFFF` stays `too_many_samples`.
- CY-3 note: in `cy3-caps.md` the 16,384 rejection of `07-12`/`07-15` applies to the resident path only; this note supersedes it for the file-backed path.
- `tests/host_shim/Arduino.h`: added `Print::print(int/unsigned/long)`. Without it the vendor logger's `print(line)` turned the int into a `const char*` and segfaulted on the first `LOGE`, which only the new source-failure cases reach.

## Measured (host, `tests/build_m4a_filetable.sh`, glibc `malloc_usable_size`)

- Whole-reader heap peak, 4-frame synthetic: 9,704 B before, 10,792 B after (+1,088 B: the 1,024 B window plus ~152 B bookkeeping, minus the old 4-entry table). `sizeof(M4aGuardedReader)` 3,576 to 4,752 B, which is also the largest single request in both cases; `sizeof(M4aCappedSizes)` 1,176 B.
- File-backed peak is identical for 10, 1,000 and 18,100 entries (10,792 B). Resident at 1,000 entries is 12,696 B; the old 32 KiB table request is gone.
- 25 real files: whole-reader peak 13,048..13,560 B (includes the vendor's per-frame buffers, which depend on frame size), largest single request 4,752 B, largest source read 1,024 B.
- The resident table footprint is therefore at most 1,024 B plus bookkeeping, independent of track length. The old worst case was 32,768 B.

## Test evidence

`tests/m4a_filetable_test.cpp` (all pass): (a) fixture 131 frames / 134,144 samples per channel, ADTS and PCM hash identical to the resident table; (b) all 25 real files (read-only folder): frames equal manifest, ADTS bytes and FNV-1a PCM hash equal the resident-table run (limit raised for the long two), source shares the streaming `FILE*`; (c) injected read/short/seek failures at refills 0/2/3 give the right status and exactly the frames before the failing refill, sticky status, reader reusable; truncated source; changed entries (0, 2042, 70000); table-object unit checks (repeat reads, reset, rewind); (d) cap exactly at 524,288 ok, 524,289 refused, count/length mismatches, memory figures above. Other suites pass: host, caps, reject (105 runs), sequence, r1r5, real (25/25). `tools/check.ps1` exits 0 (log `.tools/logs/cy11a-check.txt`).

Probe image (`aac-probe`): 1,224,528 B app bin (slot 1,966,080), sha256 `a31743320080bb41d7e1e0b22d22959271711745550bd4b9a744766b01e11cc5`; static RAM 61,552 B (was 61,536, +16). Versus the task baseline (1,223,712 B, 61,536 B static): +816 B image, +16 B static RAM; well inside the slot and the 2 KB static limit. The probe still uses the resident table (no source), so no behavioural change there.

## Notes on the earlier partial file

The Codex draft was kept in spirit and fixed: it redefined `M4aCappedSizes` (conflict with the guard), referenced an undefined `kM4aResidentEntries`, never set the table offset, had no status mapping, left the table's count after `reset()` (stale entries after a rewind), `availableForWrite()` returned count, and `peek()` reported an out-of-range entry as a read error.

## What CY-11b must do

- Provide the source over the SD `File`. Recommended: the single streaming handle with save/restore of position (`position()`, `seek(off)`, read, `seek(saved)`), zero extra RAM; each refill is one seek-away/back per 256 frames. A second handle would cost another FatFs window (4,096 B) plus the `File` object, so avoid it. The source offsets are the stream offsets fed to `write()` (offset 0 = first byte of the file), so the feeder must start at file offset 0.
- Pass the source to `begin(cb, &source)`; the source must outlive the track. Map new statuses in admission/UI.
- Account +1,176 B for the table object (window included) in the heap budget, and remove the 32 KiB table from the admission thresholds.
- Fault-inject on the device path (SD read failure mid-track) is hardware work for 11c.
