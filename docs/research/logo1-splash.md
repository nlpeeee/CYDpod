# LOGO-1: CYDpod wordmark startup screen

Commit d410738 on `cydpod/logo-splash` (from 9ed6331). No device operations.

## What changed
- `tools/gen_splash.py` (stdlib only): flattens `assets/logo/cydpod-wordmark-on-yellow.svg` (M/L/H/V/Q/A/Z), nonzero scanline fill at 8x8 supersampling, writes `app/splash_logo.h`. Assets copied to `assets/logo/` (SVGs, brand sheet, `tools/wordmark.py`).
- Data: 4-bit alpha (0 = yellow, 15 = ink), 2 px/byte, high nibble first, stride (w+1)/2. Portrait 192x57 = 5,472 B, landscape 256x76 = 9,728 B (80% of 240 / 320 wide; the SVG viewBox is tight, so the mask is the ink box).
- `app/splash_core.h`: driver-free renderer (`fill` + one `row` per mask line, 32 B colour table + <=512 B line buffer on the stack). `app/splash.cpp` binds it to TFT_eSPI (`fillScreen`, `pushImage` row by row, bytes swapped as in `titles.cpp`) and prints the version label (font 2, ink on yellow) 16 px above the bottom edge.
- `app.ino`: `centreMessage("CYD MP3", FIRMWARE_LABEL, ...)` replaced by `splash_show(FIRMWARE_LABEL)`. Rotation is already applied inside `display_begin()` (before the splash), and the splash reads `tft.width()/height()`: landscape mask when wider than tall (rotations 1, 3), portrait otherwise (0, 2).
- Colours: yellow #FFD21F computes to **0xFE83** with the repo's RGB565 truncation (`(g&0xFC)<<3`), not 0xFEA3 as the spec guessed (0xFEA3 is g=53, #FFD41F-ish). Ink #1D1D1F = 0x18E3. Blend is linear in RGB888, then to 565.
- Wordmark centre: horizontally centred, vertical centre at 45% of height (12 px above the middle on both orientations).

## Checks (once, final code commit)
- `wsl ... sh tests/build_splash.sh`: `PASS splash_test: background 0xFE83 exact, centred, margins >= 10%, ink present, rotations 0-3`. Boxes: portrait x 24..215 y 116..172, landscape x 32..287 y 70..145 (margins exactly 10% of width); 984 / 1,337 anti-aliased pixels. Registered in `tools/check.ps1` host stage (not run in smoke, which skips host media).
- `check.ps1 -SkipProbe -SkipHostMedia` exit 0; `check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia` exit 0 (`PASS: compile-only checks completed`, `PASS single pool` kept: codec pool 40,960 B untouched).

| Image | Flash before -> after | Static RAM |
|---|---|---|
| player (default) | 1,510,009 -> 1,525,665 (+15,656 B) | 93,464 -> 93,464 (+0) |
| player (W328C) | 1,551,057 -> 1,566,733 (+15,676 B) | 95,056 -> 95,056 (+0) |

Runtime stack while drawing: about 550 B. Draw time was not measured on hardware (about 15 K pixels pushed row by row; expected well under 80 ms).

## Review images
`docs/research/logo1-portrait.png` (240x320), `logo1-landscape.png` (320x240): host render of the fake framebuffer. The version label is drawn by the TFT font and is not in them.

## On the device
Yellow screen, black CYDpod wordmark (ring-and-dot "o") centred slightly above the middle, small version text near the bottom, in whichever orientation is saved. It stays up through backlight-on and boot work. Later messages overwrite it: `centreMessage` and `libraryProgress` start with `fillScreen(theme bg)`, so "Scanning/Preparing" progress and the "No SD card / read-only / stale" notices replace it with their own theme background; with a healthy card and nothing to rebuild the splash stays until the player screen is drawn. First-run touch calibration (`calibrate_run`) also draws over it. Not changed, as agreed.

## Open decisions
- Keep 0xFE83 (exact #FFD21F) or nudge green to 53 (0xFEA3) for a warmer yellow?
- Optional: draw the splash again after progress messages instead of the plain text screens.
