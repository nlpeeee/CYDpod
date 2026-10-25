# CY-4: M4A rejection codes

`M4aGuardedReader` (`lib/cydpod_m4a/m4a_guarded_reader.h`) accepts only an AAC-LC, 44,100 Hz, 2-channel `mp4a` track whose `moov` precedes `mdat`. Anything else returns an `M4aStatus` from `write()`/`finish()` **before** the offending box reaches the vendored `M4AAudioDemuxer`, so no ADTS frame is delivered and no allocation exceeds the CY-3 caps. Moov-last playback is not implemented and files are never remuxed.

## New or changed codes

| `M4aStatus` (log name) | Trigger | User-visible meaning |
|---|---|---|
| `boxTooLarge` (`box_too_large`) | A finite declared box size exceeds `UINT32_MAX`, including a top-level 64-bit `mdat`. Checked before forwarding, explicitly using the ESP32 width on host builds too. | "Container box exceeds the supported 32-bit size." |
| `offsetOverflow` (`offset_overflow`) | A box end, held-header end or incoming write offset would overflow `uint64_t`. Box-end overflow is checked before the 32-bit size bound. | "Invalid container offset." |
| `moovAfterMdat` (`moov_after_mdat`) | A top-level `mdat` arrives before `moov` has been fully read (`ftyp mdat moov`). Replaces CY-3's `mdatBeforeMoov`. | "Not optimised for streaming: re-save with the index at the start of the file." Detected at the `mdat` header, so zero audio bytes are read. |
| `notMp4a` (`not_mp4a`) | The first sample entry in `stsd` is not `mp4a` and not a known video type (`alac`, `.mp3`, `ac-3`, `Opus`, `fLaC`, ...). Replaces CY-3's `unsupportedCodec` for this case. | "Unsupported audio format." |
| `notAacLc` (`not_aac_lc`) | `mp4a` entry whose esds objectTypeIndication is not `0x40` or whose AudioSpecificConfig object type is not 2: HE-AAC (5), HE-AACv2 (29), Main (1), LTP (4), MPEG-1 audio (`0x6B`). | "Unsupported AAC profile (only AAC-LC)." |
| `badSampleRate` (`bad_sample_rate`) | ASC sampling-frequency index is not 4 (44,100 Hz); includes the explicit-frequency escape (15). | "Unsupported sample rate (only 44.1 kHz)." |
| `badChannels` (`bad_channels`) | ASC channel configuration is not 2: mono, 3, 5.1, 7.1 and 0 (program config element). | "Unsupported channel layout (only stereo)." |
| `noAudioTrack` (`no_audio_track`) | `moov` was read (or the `mdat` arrived after it) without any `stsd`, so no track has a sample description. | "No audio track in this file." |
| `firstTrackNotAudio` (`first_track_not_audio`) | The first track's sample entry is a video type (`avc1 avc3 hvc1 hev1 mp4v vp08 vp09 av01 encv s263 jpeg dvh1`). | "First track is video; audio-after-video files are not supported." |

Check order inside `stsd` (`onStsdDone`): entry count/size, then entry type (`firstTrackNotAudio` / `notMp4a`), then esds structure (`badStsd`), then AAC type (`notAacLc`), rate (`badSampleRate`), channels (`badChannels`). The `stsd` minimum is now 16 bytes at the header (was 52) so short non-`mp4a` entries such as `.mp3` reach the codec check instead of failing as `badStsd`.

Unchanged CY-3 codes that matter here: `duplicateTrack` (a second `stsd`/`stsz`/`mdat`), `noSampleTable` (`stsd` seen but no `stsz`, or no `moov` at all), `badStsd` (malformed sample description).

CY-10a: codec callback names (`alac`, `mp4a`, `esds`, `ac-3`) outside the fully validated `stsd` payload are renamed to `free` before delivery. Their payload is skipped without invoking codec callbacks; this introduces no rejection code. Validated `mp4a`/`esds` inside `stsd` retain their existing behavior. Finite sizes through `UINT32_MAX` and open-ended (`size == 0`) top-level `mdat` remain supported; no large payload is allocated by the guard. See [R1/R5 regression evidence](cy10a-r1-r5.md).

## Track selection finding

The vendored reader does not select a track: it takes the first `stsd`/`stsz` it parses and has no handler-type (`hdlr`) check. The guard therefore does not skip a leading video track. Results (`tests/m4a_reject_test.cpp`):

- video first, audio second: `firstTrackNotAudio`
- audio first, video (or a second audio) second: `duplicateTrack`. The file is also rejected: the reader serves one track per instance and CY-3 treats a second table as a conflict.
- video only: `firstTrackNotAudio`; no trak at all (trak renamed `free`, with or without `mdat`): `noAudioTrack`

Supporting such files would need a track-selecting reader (`hdlr` plus a trak-scoped table); out of scope here.

## Evidence

`wsl.exe -d Debian -- sh tests/build_m4a_reject.sh`: 35 cases x 3 write-slice sizes (7 B, 1 KiB, whole file) in forked children, each asserting the exact status, 0 ADTS frames for rejections, peak heap within `kM4aHeapBudgetBytes` (48 KiB) and largest request within `kM4aTableBytes`. Cases are built in memory from `fixtures/stereo-faststart.m4a` (and the unmodified `stereo-moov-last.m4a`); fixtures are never written. Positive controls: the unmodified faststart file (131 frames) and, read-only, the 25 files in the user's `07 - Dance and Indie Crossover` folder: 23 accepted, the 2 over the table cap (`Galvanize`, `Loaded`) rejected as `too_many_samples` (CY-3 behaviour, unchanged).

None of this is hardware evidence; no device was touched.
