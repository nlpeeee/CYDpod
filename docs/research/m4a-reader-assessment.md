# M4A reader assessment

Reviewed 2026-10-23 against AudioTools commit `e42af0513a2273730431e8c45ac17a5fd9f38487`, the commit pinned in [the AAC prototype report](aac-prototype.md#exact-m4a-follow-up). Read-only source inspection; nothing was imported, compiled or run.

## Two existing readers

| | `M4AAudioDemuxer` (streaming; current plan) | `M4AAudioFileDemuxer` (seekable file) |
| --- | --- | --- |
| Sample-size table | Held in RAM: 2 bytes per AAC frame, about 303 KiB per hour at 44.1 kHz. That alone exceeds no-PSRAM headroom once Bluetooth is up, unless a file-backed `BaseBuffer` is substituted. | Not held. Sizes are read from `stsz` on the card in 256-entry blocks (1 KiB). |
| moov after mdat | Unsupported: mdat arrives before `stsz` and the extractor stops ("No sample size defined"). | Works, slowly: `parseFile()` streams the whole mdat through the parser in 1 KiB reads before reaching moov. |
| Seek and resume (A7) | No: input is forward-only. | Possible: frame N lies at mdat start plus the sum of the first N sizes, read from `stsz`. Needs a cached checkpoint table or a linear scan. |
| Dependencies | `Buffers.h`, `Vector`, logging macros | Also `Arduino.h`, `SD.h` `File` and `MultiDecoder`; a host test needs a `File` shim. |

## Defects in the shared code (both readers)

These come from reading the source and have not been reproduced. Each one needs a host test before it is fixed.

1. **Release builds with `NDEBUG` drop work.** `M4ACommonDemuxer.h:176` and `:662` wrap required writes in `assert(...)`, so a build that defines `NDEBUG` removes the write itself. In debug builds a failed write, and the length check at `:642`, abort the program instead of returning an error.
2. **Static cache outlives the track.** `currentSampleSize()` (`:327-348`) keeps `last_index` and `last_size` in function-local statics shared by every instance. After a reset, sample 0 of the next track can return the previous track's cached size. This affects the streaming reader only.
3. **No bounds on lengths from the file.**
   - `MP4Parser::parseString` (`MP4Parser.h:228-252`) reads type bytes before checking that they are inside `len`. A box size below 8 underflows.
   - `idx += box.size` skips only the payload, not the 8-byte header, so a second sibling box is misread.
   - `findBox` (`:255-274`) computes `len - 4` on `size_t` and never limits the found box to `len`.
   - `onMp4a` (`M4ACommonDemuxer.h:535-536`) subtracts 28 from `data_size` without checking it first.
   - `onStsd` (`:501`) and `onStsz` (`:652`) resize to sizes taken from the file without any cap.
4. **Unchecked tables and frame lengths.**
   - Both readers ignore `stco`/`stsc` and assume the audio frames sit back to back from the start of mdat. A chapter or text track interleaved in mdat would desynchronise playback silently.
   - The file reader's `stsz` and `mdat` callbacks keep the values from whichever track's box they saw last.
   - `stsz_sample_size_t` is `uint16_t`, so a frame size above 65,535 is truncated. ADTS lengths above 8,191 wrap. CYDpod's decoder rejects frames over 2,048 bytes, so the wrapped values are caught there, but only as a decode error.
5. **The profile check relies on CYDpod.** ESDS object type 5 (HE-AAC) is written into the ADTS profile field and overflows it. Only CYDpod's own `adtsHeader` check (profile must be 2) prevents a wrong decode.

## Recommendation

Use **`M4AAudioFileDemuxer`**, pinned at the same commit, with a small CYDpod patch set applied by `bootstrap.py` and recorded in the lock, as the Helix config patch is today. The patches would:

- replace `assert`-wrapped writes with checked returns;
- bound every length read from the file before reading or allocating;
- accept only a single AAC-LC 44.1 kHz stereo audio track, and reject the file if `stco` shows its first chunk anywhere other than the start of mdat;
- cap the metadata buffer at 4 KiB and the frame size at 2 KiB, as the prototype report already proposes.

Feed its ADTS frames to the existing `AdtsDecoder`, add a host `File` shim and extend `media_check.py` so that:

- the faststart M4A fixture becomes a positive case (same SNR and sample thresholds as ADTS);
- moov-last plays, with its startup cost measured;
- new malformed and oversized fixtures are rejected.

This replaces the streaming-demuxer plan in [the prototype report](aac-prototype.md#exact-m4a-follow-up). No MP4 parser is written from scratch.

Before choosing, it is worth running C0 on the real music: if the files are ADTS `.aac`, or all faststart single-track `.m4a`, the work and test matrix shrink accordingly.
