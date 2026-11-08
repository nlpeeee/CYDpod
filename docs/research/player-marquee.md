# Now Playing marquee

Branch `cydpod/player-marquee`, based on tag `v0.5.3` (7ee579d).

## Change

On Now Playing, a title, artist or album too wide for its row used to end in "..." and stayed that way.
Now it scrolls through one full lap, then goes back to its "..." form. Only one label scrolls at a time:
the next overflowing label waits 1.5 s and then takes its turn. Labels that fit never move.

- Rate: 2 px every 60 ms (about 33 px/s), with a 32 px gap before the text repeats.
- ASCII labels: `cydtext::drawWindow` (classic_text.cpp) builds each scanline of the label's band in a
  640 B stack row from the flash FreeSans bitmaps. It sends the band through one `setAddrWindow` and one
  `pushColors` per row. Output is opaque, so nothing has to be erased first and the text does not flicker.
- UTF-8 labels: the existing `titles_draw(box, scrollX, gap, …)` marquee, using the single strip already
  in RAM. The strip is loaded once at the start of the label's turn. `playerText` keeps that strip while it
  scrolls, so the time labels drawn every second do not force SD reloads. If another UTF-8 draw replaces
  the strip (`titles_loadedKey()` changes), that label goes back to rest and the next one takes its turn.
- A step is skipped if `audio_bufferLow()` is true, a seek preview is showing or a finger is on the glass.
  Overlays and screen-off already stop the screen tick (`ui_tick`).
- Each band is cut at the midpoints between the title, artist and album rows, so it never paints over a
  neighbouring row.

## Cost

| | v0.5.3 | marquee | delta |
|---|---|---|---|
| Static RAM, Bluetooth image | 79,652 B | 79,668 B | +16 B |
| Static RAM, wired image | 79,660 B | 79,676 B | +16 B |
| Flash, Bluetooth image | — | 1,299,521 B | |
| Flash, wired image | — | 1,299,629 B | |

Heap: none for ASCII labels. UTF-8 labels use the strip allocation that `ui_text` already makes (≤3.3 KB).
Each step's bus traffic is one band of about 292 × 20 px (~12 KB) on HSPI. SD (VSPI) is read only when a
UTF-8 label's turn starts.

## Checks

- Host suite `tests/build_catalog.cmd`: exit 0, no FAIL lines (`.tools/logs/marquee-host-suite.log`).
  This includes the new `marqueeRegression` (`PASS Now Playing marquee: hold, scroll in band, ring-low
  pause, rest, short labels static`, run in rotations 1 and 0). `CY90_DIRTY_OK` now allows the marquee's
  text band in the time-tick check but still allows no icon cell: its fixture album
  "...Baby One More Time (Digital Deluxe Version)" overflows in landscape.
- Firmware compile-only, `tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia -LargePcmRing`:
  `PASS: compile-only checks completed; no device commands were run.` Both images built
  (`.tools/logs/marquee-fw-check.log`).

## Owner board run (2026-11-08)

- Image: `.tools/images/codec-pool-v36-headroom-ring6144-marquee-jc2432w328c`, built with
  `-LargePcmRing -PcmRingFrames 6144 -BuildLabel marquee` (the v0.5.3 ring). Static RAM 79,668 B (+16 B).
  All manifest hashes matched.
- Flashed to COM7, which the owner confirmed is the JC2432W328C (ESP32-D0WD-V3, MAC b4:a6:4a:a0:e1:88).
  esptool `write-flash` wrote all four regions and reported `Hash of data verified.` for each.
  The separate `verify-flash` pass failed to connect (`No serial data received`), because the board was
  still in the loader after `--after no-reset`. Log: `.tools/logs/marquee-flash-com7.log`.
- Owner verdict after use: "looks great". This is an informal owner run, not a timed measurement.
  Not separately recorded: the AirPods Pro 2 and wired continuity checks, UTF-8 labels, and each rotation.
