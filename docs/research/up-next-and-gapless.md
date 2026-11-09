# Up Next and gapless: checks

Date: 9 November 2026. Spec: `docs/specs/up-next-and-gapless.md`. Source: uncommitted on `main` after `af82d54` (0.5.5).

## Why tracks had a gap

Before this change, at EOF the decode task waited for the PCM ring to drain to zero (`audio.cpp`, EOF branch), then closed the file and signalled the end. The loop task then requested the next file, and the new open flushed the ring.

The listener heard silence for loop latency, plus the file open, plus the first decode and refill. MP3s added a further 26 ms frame of silence from the decoded Xing/Info tag frame, and kept the encoder delay (≈ 1105 samples with LAME/Lavc) and padding.

## Host checks (once each, on the final code)

| Suite | Result | Log |
| --- | --- | --- |
| `tests/build_audio_order.sh` | PASS. The existing order cases, plus `PASS up next: front/back order, list resumes, prev, Repeat One/Off, deferred end, cap 64, play now` and `PASS gapless carry: tail frames excluded from the clock, boundary gap measured, not an underrun` | `.tools/logs/upnext-audio-order.txt` |
| `tests/build_codec_pool.sh` (includes `build_audio_seek.sh`) | exit 0, 133 PASS lines. All 36 MP3 fixtures: `PASS MP3 bit-exact stock Helix (gapless: tag frame skipped, lead 1105, trimmed length)`. Example: 48000-2-vbr gives 48,000 frames = 43 × 1152 − 576 − 960. WAV/MP3 seek exact PCM and position on the trimmed timeline. `GAPLESS_OK: paused EOF keeps the song; natural end -> next track with 0 missing frames, 0 underruns, clock 58 ms after 20 pulls`, with the firmware's own line `stat: gap track 1 missing 0 frames (0 ms)` | `.tools/logs/upnext-codec-pool.txt` |
| `tests/build_catalog.cmd` (UI, Bluetooth and wired builds) | exit 0. `PASS Up Next: hold menu on songs and albums, Play next, Add to queue, Cancel, outside tap, short tap, Options count, page, Remove, Clear`. The layout golden is unchanged, and the reachable-page list now includes `Classic.UpNext` | `.tools/logs/upnext-catalog.txt` |

Test changes:
- `mp3_pool_test.cpp` now checks against a gapless reference. Stock Helix decodes every frame after the tag frame, and the reference is cut to the LAME window from the file's own tag. The test was previously frame-for-frame identical to stock.
- `audio_seek_test.cpp` gains host EQ stubs. Its link had been failing on `eq_bypass()` since 0.5.5 added EQ calls to `audio.cpp`, which this change didn't cause. It also gains the `--gapless` case.

## Firmware compile

Command:

```
tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia -LargePcmRing -PcmRingFrames 6144
```

Result: `PASS: compile-only checks completed; no device commands were run.` The trimmed Bluedroid was used (`check.ps1` refuses stock libraries without `-StockBluetoothLibs`).

| | 0.5.5 | This change | Δ |
| --- | --- | --- | --- |
| Static RAM | 80,852 B | 81,148 B | +296 B |
| Flash (sketch) | 1,327,885 B | 1,333,809 B | +5,924 B |

- The single codec pool is unchanged (`00007868`).
- Up Next uses 128 B. The rest of the static-RAM increase is the MP3 trim state, the gap counters and the menu's statics.

## Not yet confirmed on hardware

**Gapless on the board.**
- On host, the next file opens well inside the ring. On the board it depends on SD open and parse time against ~139 ms of ring.
- Listen to a continuous FLAC album and a LAME MP3 album over AirPods and wired output, and check that `stat: gap` reports 0 frames.
- `gapless off` gives the old behaviour for A/B listening.

**Hold menu on the CST820 panel.**
- Check the hold on song, album and artist rows, a long press on Settings rows (still acts as a tap, CT-05), and that the hold never triggers recalibration.

**Up Next and Sleep.**
- Check that Up Next survives screen-off and Sleep.

**Docs.** The owner has run the wired build on hardware, so the next release notes should drop "wired hardware" from the open list.
