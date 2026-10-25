# CY-6: guarded M4A reader wired into the probe (compile only)

Compile-only evidence. Nothing here was run on hardware, no serial port, SD card or flashing; the log values below are formats, not measurements.

## Build wiring (`tools/check.ps1`)
Only the `aac-probe` job changed: flags `-DCYDPOD_AAC_LC_ONLY -I<repo>/vendor/arduino-audio-tools/src -I<repo>/lib/cydpod_m4a`. `player` and `uploader` are unchanged (same sizes and hashes as before). `vendor/` is untouched.

## Probe behaviour (`firmware/cydpod_probe/cydpod_probe.ino`)
- A track whose name ends in `.m4a` (case-insensitive) is opened read-only; a fresh `M4aFeed` (one `M4aGuardedReader` + 4 KiB ADTS queue + 256 B chunk, heap allocated) is created per track and deleted at track end. The file is read in 256-byte chunks into `M4aGuardedReader::write()`; its ADTS frames are queued and handed to the unchanged `AdtsDecoder` → PCM ring path.
- `.aac` tracks take the original path (`file.read` straight into the decoder).
- On any `M4aStatus` error (or an ADTS queue overflow, state `adts_queue_overflow`) the track is skipped, `ERR m4a <status>` is printed and the producer waits for the next command. Error/skip cleanup uses `stopConsumers()` to mute, disable and quiesce both consumers before clearing the PCM ring; only that path resets the ring. The probe has no playlist, so "next track" is the next `play` command.
- CY-11b: the reader uses the file-backed 1 KiB sample-size table (`SdSource`, save/restore position on the same `File`; `source_*` statuses end the track via the existing skip path, `sourceReads` counts refills). After the file is open and before the reader/decoder are allocated, internal 8-bit free heap must be at least `m4aNeedFree()` = 20,680 + 14,336 + feed extra (<= 4,392) + 2,048 = 41,456 B (provisional margin since CY-13a; was 10,240 = 49,648 B) and the largest block at least `m4aNeedLargest()` = sizeof(M4aFeed) + 20,560 = 29,704 B (derivation: `cy11b-admission.md`). Otherwise the track is skipped with `m4a=low_memory` and `ERR m4a low_memory heap=<n> largest=<n> need=<n> need_largest=<n>`. On the CY-12a state (45,488 free after open) this admits with 4,032 B slack (CY-13a); the hardware playback floor is unmeasured. This admission guard cannot guarantee later allocations succeed; recoverable allocation throughout the reader remains follow-up work. `.aac` admission is unchanged.
- Producer task stack raised 6144 → 8192 bytes (the demux callback chain runs on it). Not measured; check `producer_stack` in `stats` on hardware.
- No Bluetooth, I2S or UI changes.

## Serial format
Read-only directory listing (`ls` defaults to `/Music`, or `ls /dir`):
```
LS name=<name> size=<bytes> kind=file|dir
LS done count=<n> truncated=0|1
```
The producer lists only when idle; a queued listing waits for the track to stop or end without interrupting or muting it. At most one listing may be queued or running; further listings, or a listing without two free queue slots, return `ERR ls busy`. `stop`, `play`, `output` and `pair` always have admission: if the four-slot queue is full, the oldest pending command is discarded with `ERR pending command superseded` and `queue_rejected` increments. Listing rejection also increments that counter. These commands cancel current playback immediately through the epoch and mute flags; producer cleanup and queued commands then run in order. Send `stop` first for prompt listing. It reads FatFs metadata on the probe's sole FAT volume (`0:`), skips dot-prefixed and FAT hidden/system entries, prints at most 64 entries and 96 bytes per name, and sets `truncated=1` for either cap (or a read error, also logged as `ERR ls read`). `ERR ls open` means the directory could not be opened. Names are basenames; append an untruncated file name to the listed directory for `play`. The summariser selects only `TRACK`/`METRIC`/`ERR` lines, so `LS` needs no parser or fixture changes.

Per track (start, end on EOF, skip on error):
```
TRACK ev=live|start|end|skip name=<path> m4a=<status|na> heap=<free> largest=<block> min_heap=<since boot> min_sampled_largest=<min> producer_stack=<bytes> source_reads=<n>
```
`ev=live` (CY-11b) is logged once per track right after reader and decoder are allocated, immediately before `ev=start`; its `heap`/`largest` are the post-allocation figures. `producer_stack` is the producer high-water mark in bytes; `source_reads` is the number of sample-size window refills this track.
`m4a` is a `m4aStatusName()` string (`ok`, `moov_after_mdat`, `too_many_samples`, ...), `na` for `.aac`, `alloc` if the reader allocation failed. `heap`/`largest` are current free / largest free block and `min_heap` the minimum free since boot (`heap_caps_get_minimum_free_size`, internal 8-bit); all use `MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT`. `min_sampled_largest` is the existing `minLargest` sampler.
Periodic (every 5 s and on `stats`; corrected by CY-9c):
```
METRIC m4a=<status|na> heap=<free> min_heap=<since boot> largest=<block> min_sampled_largest=<min> producer_stack=<bytes> sink_stack=<bytes> loop_stack=<bytes> ring=<frames> decoded=<frames> underrun_frames=<frames> underrun_calls=<calls> queue_rejected=<count> connected=0|1 muted=0|1 level=<percent> source_reads=<n>
```
The format has one string followed by 16 unsigned fields (`source_reads` added by CY-11b). CY-9c restored the missing `m4aState` argument; the earlier image panicked in this call before producing telemetry (see `cy9a-smoke-test.md`).
Status is now published as an atomic pointer to static-lifetime text, with one snapshot loaded per `TRACK`, `METRIC` or status-error line; existing log text is preserved.

## Image size, static RAM, hash
| | bytes | % of 1,966,080 slot | static RAM | sha256 |
|---|---|---|---|---|
| previous probe | 1,189,936 | 60.5% | 61,224 | `35ec4065ba349757d39d9eee7f662249976ba281a9dc5e1765153f8ac7f233e4` |
| new probe | 1,222,320 | 62.2% | 61,528 | `90b74d03c7b273dbcb394b5f3ebe3c19d11c01434d640ac31dac6ed6dd3d02f3` |

Delta: +32,384 bytes of image, +304 bytes of static RAM (well under the 20 KB stop threshold; image fits the slot). The M4A reader's ~9.2 KB plus table (up to 32 KiB, see `cy5-state-reset.md`) is heap, not static, and still needs on-target measurement of `largest`/`min_heap` with a no-PSRAM Bluetooth session.

## CY-8b listing verification (2026-10-25, offline only)

`powershell -NoProfile -File tools/check.ps1` exited 0, including player, uploader, probe and host media checks: `PASS synthetic fixture matrix: 3 positive, 7 negative` and `PASS: compile-only checks completed; no device commands were run.` Full output: `.tools/logs/cy8b-check.txt`; compiler output: `.tools/logs/aac-probe.log`; image metadata: `.tools/logs/build-results.json`.

The new probe binary is **1,222,880 bytes**, SHA256 `9a57cb67d323030de1e4e796c0b9ff440f06981393a66ad320de258d05154b4c`: +560 bytes against CY-6/CY-8, within the 1,966,080-byte slot. Compiler reports code 1,222,730 bytes and static RAM **61,528 bytes** (+0). Player/uploader image sizes and hashes are unchanged.

All four `wsl.exe -d Debian -- sh tests/build_m4a_{host,caps,reject,sequence}.sh` invocations exited 0: identical PCM/counts, malformed inputs within caps, all 105 rejection runs passed, and every sequence track equalled its fresh-reader result. Both Windows PowerShell 5.1 and `pwsh` session dry-runs with `-Port COM10 -Script tools/cyd_probe_session.sample.txt -LogPath .tools/logs/cy8b-dryrun.txt -DryRun` exited 0, scheduled `ls /Music` first at 2 seconds and final `stop` at 362 seconds, and created no log. Both `-SummaryLog tests/fixtures-log/probe-session.txt` runs exited 0 and retained the documented ranges, status counts (na=2, ok=2, moov_after_mdat=1), and two ERR lines. Source inspection confirms `LS` lines are excluded from the summariser's anchored telemetry selection. No hardware, port, card or flashing access occurred; on-device listing remains unverified.

## CY-9c corrected telemetry image (2026-10-25, compile only)

Current probe: **1,222,880 bytes**, SHA256 `480441396eb43594b6a0a25760a081b81080cd3c803a11a172de961b7037a79b`.
Compared with CY-8b/CY-9a (1,222,880 bytes, SHA256
`9a57cb67d323030de1e4e796c0b9ff440f06981393a66ad320de258d05154b4c`),
the image size delta is zero. Compiler code size is **1,222,738 bytes** (+8),
static RAM **61,528 bytes** (+0), leaving 266,152 bytes before runtime allocation.
Player/uploader sizes and hashes are unchanged. The corrected image still fits
its 1,966,080-byte app slot; compile evidence does not establish boot or playback.

The nine format calls, 16 METRIC argument positions, scoped format-error policy,
negative compile failure and passing checks are recorded in
[the CY-9c audit](cy9c-format-audit.md). No device operations were performed.

## CY-10c1 review fixes (2026-10-25, compile only)

R3: all PCM resets now use `stopConsumers()`; late reader/ADTS-queue and decoder errors quiesce consumers before dropping buffered audio. R4: one outstanding listing and reserved capacity prevent listing starvation, while full-queue cancellation supersedes the oldest pending command. R6: status uses an atomic pointer and one snapshot per telemetry line. The interim R2 guard checks 96 KiB total / 64 KiB contiguous internal heap before reader allocation; it cannot guarantee subsequent allocations succeed.

`powershell -File tools/check.ps1` exited **0**, retaining output in `.tools/logs/cy10c1-check.txt`:

```text
Sketch uses 1223566 bytes (62%) of program storage space. Maximum is 1966080 bytes.
Global variables use 61536 bytes (18%) of dynamic memory, leaving 266144 bytes for local variables. Maximum is 327680 bytes.
PASS synthetic fixture matrix: 3 positive, 7 negative; report fixtures/verification.json
PASS: compile-only checks completed; no device commands were run.
```

| Probe | Image bytes | Static RAM bytes | SHA256 |
| --- | ---: | ---: | --- |
| Previous CY-10a | 1,223,088 | 61,528 | `1a40d486f1a15d3a0d59217d64f1db16e6a710a30dd924a33312d624cbe7aa0d` |
| CY-10c1 | 1,223,712 | 61,536 | `0e0f02ceb782ef9e958dc34f60e9dd0de7724c206320eb3924f9094a9b097aff` |

Image delta **+624 bytes**, fitting the 1,966,080-byte slot; static RAM delta **+8 bytes**, below the 2 KiB stop threshold. Actual binary SHA256 agrees with `.tools/logs/build-results.json`; player/uploader image identities are unchanged.

All five `wsl.exe -d Debian -- sh tests/build_m4a_{host,caps,reject,sequence,r1r5}.sh` suites exited **0**; individual outputs are `.tools/logs/cy10c1-{host,caps,reject,sequence,r1r5}.txt`. Results: identical PCM/counts, malformed cases within caps, all 105 rejection runs, every sequence track matching a fresh reader, and R1/R5 zero failures. Native 32-bit multilib was unavailable; that suite used its explicit uint32_t truncation assertion. `git diff --check` passed for the assigned files. These checks do not execute firmware concurrency or heap admission on target; hardware, serial, SD and flashing were not accessed.

## CY-10c2 AudioTools inventory check (review R7, 2026-10-25)

Before compiling, `tools/check.ps1` now checks all 518 paths and SHA256 values in
`tools/audiotools-files.sha256.json` against `vendor/arduino-audio-tools/`, then
compares every discovered file (including hidden files) with the exact listed
set. Missing files, changed bytes and extra files stop the check with the first
offending relative path and a distinct diagnostic. This also runs with
`-SkipProbe`; source integrity is required for every normal check invocation.

Scratch-copy tests extracted `Test-AudioToolsInventory` directly from the script's
PowerShell AST and copied both the vendor tree and inventory under `.tools/tmp/`.
Windows PowerShell **5.1.26100.9711** and PowerShell **7.6.6** each accepted the
intact copy (exit 0) and rejected each of these changes (exit 1):

| Scratch mutation | Diagnostic | Output under `.tools/logs/` |
| --- | --- | --- |
| Flip one byte in `library.properties` | `AudioTools inventory SHA256 mismatch: library.properties` | `cy10c2-inventory-changed.txt` |
| Delete `library.properties` | `AudioTools inventory missing file: library.properties` | `cy10c2-inventory-missing.txt` |
| Add `.r7-extra.txt` | `AudioTools inventory unexpected file: .r7-extra.txt` | `cy10c2-inventory-extra.txt` |

The local runner is `.tools/logs/cy10c2-inventory-test.ps1`; repeat with either
`powershell` or `pwsh`, `-NoProfile -File .tools/logs/cy10c2-inventory-test.ps1
-Case valid|changed|missing|extra` (choose one case). Runner, scratch copies and
raw logs are ignored build evidence; no real vendor files were modified.

`powershell -NoProfile -File tools/check.ps1` exited **0**; full output is
`.tools/logs/cy10c2-check.txt`. All three builds passed, followed by
`PASS synthetic fixture matrix: 3 positive, 7 negative` and
`PASS: compile-only checks completed; no device commands were run.` All three
image sizes and hashes equal the saved CY-10c1 build results; `aac-probe` remains
**1,223,712 bytes**, SHA256
`0e0f02ceb782ef9e958dc34f60e9dd0de7724c206320eb3924f9094a9b097aff`.
`git diff --check` passed for the two assigned files, and `git status --short --
vendor lib firmware` was empty. No hardware, serial, SD or flashing access occurred.
