# CY-6: guarded M4A reader wired into the probe (compile only)

Compile-only evidence. Nothing here was run on hardware, no serial port, SD card or flashing; the log values below are formats, not measurements.

## Build wiring (`tools/check.ps1`)
Only the `aac-probe` job changed: flags `-DCYDPOD_AAC_LC_ONLY -I<repo>/vendor/arduino-audio-tools/src -I<repo>/lib/cydpod_m4a`. `player` and `uploader` are unchanged (same sizes and hashes as before). `vendor/` is untouched.

## Probe behaviour (`firmware/cydpod_probe/cydpod_probe.ino`)
- A track whose name ends in `.m4a` (case-insensitive) is opened read-only; a fresh `M4aFeed` (one `M4aGuardedReader` + 4 KiB ADTS queue + 256 B chunk, heap allocated) is created per track and deleted at track end. The file is read in 256-byte chunks into `M4aGuardedReader::write()`; its ADTS frames are queued and handed to the unchanged `AdtsDecoder` → PCM ring path.
- `.aac` tracks take the original path (`file.read` straight into the decoder).
- On any `M4aStatus` error (or an ADTS queue overflow, state `adts_queue_overflow`) the track is skipped, `ERR m4a <status>` is printed and the producer waits for the next command. The probe has no playlist, so "next track" is the next `play` command.
- Producer task stack raised 6144 → 8192 bytes (the demux callback chain runs on it). Not measured; check `producer_stack` in `stats` on hardware.
- No Bluetooth, I2S or UI changes.

## Serial format
Per track (start, end on EOF, skip on error):
```
TRACK ev=start|end|skip name=<path> m4a=<status|na> heap=<free> largest=<block> min_heap=<since boot> min_sampled_largest=<min>
```
`m4a` is a `m4aStatusName()` string (`ok`, `moov_after_mdat`, `too_many_samples`, ...), `na` for `.aac`, `alloc` if the reader allocation failed. `heap`/`largest` are current free / largest free block and `min_heap` the minimum free since boot (`heap_caps_get_minimum_free_size`, internal 8-bit); all use `MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT`. `min_sampled_largest` is the existing `minLargest` sampler.
Periodic (every 5 s and on `stats`): the existing `METRIC ...` line, now starting `METRIC m4a=<status|na> heap=... min_heap=... largest=... min_sampled_largest=...`.

## Image size, static RAM, hash
| | bytes | % of 1,966,080 slot | static RAM | sha256 |
|---|---|---|---|---|
| previous probe | 1,189,936 | 60.5% | 61,224 | `35ec4065ba349757d39d9eee7f662249976ba281a9dc5e1765153f8ac7f233e4` |
| new probe | 1,222,320 | 62.2% | 61,528 | `90b74d03c7b273dbcb394b5f3ebe3c19d11c01434d640ac31dac6ed6dd3d02f3` |

Delta: +32,384 bytes of image, +304 bytes of static RAM (well under the 20 KB stop threshold; image fits the slot). The M4A reader's ~9.2 KB plus table (up to 32 KiB, see `cy5-state-reset.md`) is heap, not static, and still needs on-target measurement of `largest`/`min_heap` with a no-PSRAM Bluetooth session.
