# Stage 0 results — ESP32-2432S028R (ST7789), 2026-09-16

What the hardware actually did, measured on the owner's board. The app is built
on these numbers; anything here that turns out wrong should be corrected here
first.

## Board

- ESP32-D0WD-V3 rev 3.1, 4 MB flash, no PSRAM, CH340 USB-serial.
- **Uploads fail at 921600 baud** ("Failed uploading: exit status 2"). 115200
  works every time (~21 s for 385 KB). `tools/flash.sh` defaults to it.
- Opening the serial port resets the board (DTR/RTS), which also drops the
  Bluetooth connection.

## s01 — display + bit-banged touch + SD

| | Result |
|---|---|
| SPI layout | TFT on HSPI, **SD on VSPI**, touch **bit-banged** — all three work together |
| SD mount | 20 MHz first try, 1903 MB card |
| SD read | **873 KB/s** (320 kbps MP3 needs 40) |
| Rotation | 0–3 all draw correctly |
| Touch | works in every rotation after the axis fix below |

**Touch axes.** Raw X (command 0xD1) runs along the 240 px edge and
*decreases* left to right in portrait; raw Y (0x91) runs along the 320 px edge
and increases. The first version had X increasing, which put the touch dot on
the mirror-image side. Rough range, from the weather clock's stored calibration
on this unit: X 3896→176, Y 193→3782. XPT2046_Touchscreen names these axes the
other way round (it pipelines its SPI reads), which is why the clock's numbers
needed translating.

A Mac leaves `._name.mp3` / `._name.gif` metadata files on the card — the first
count said 26 MP3s and 6 GIFs, the real numbers were 13 and 3. Everything that
lists the card skips names starting with `.`.

## s02 — Bluetooth A2DP source, sine tone

- Scan → tap → connect to a JBL Flip 4: works. 44,100 frames/s, steady tone.
- Heap: 222 KB before Bluetooth, **~142 KB after `a2dp.start()`** (Bluetooth
  stack ≈ 80 KB), ~128 KB while connected.

## s03 — MP3 from SD to Bluetooth

- 4 minutes, 44.1 kHz / 192 kbps: **0 underruns**, 44,100 frames/s. Owner:
  clean sound, buttons respond quickly, including rapid track skips.
- First run left only **12 KB** heap at the low point. Tuned:

| Item | Cost | Notes |
|---|---|---|
| Bluetooth stack | 81 KB | fixed; `set_reset_ble(true)` freed nothing measurable |
| Helix MP3 decoder | 32.5 KB | fixed |
| Open file (FATFS) | ~7 KB each | `SD.begin(..., max_files)` kept small |
| PCM ring buffer | 32 → **16 KB** | 2048 frames = 46 ms; stayed 74–99 % full |
| Decode task stack | 8 → **4 KB** | ~2 KB actually used |
| Connecting / streaming | ~20 KB more | brief dip while the link comes up |

After tuning: **~51 KB free while playing, 37.7 KB minimum.**

## s04 — live GIF decoding beside playback: rejected

AnimatedGIF holds ~25 KB of LZW state. With it alongside Bluetooth and the MP3
decoder the heap fell to ~15 KB, and opening the GIF file would take another
~5 KB. Not a safe margin for an app that also needs a UI.

The owner's GIFs were also not the planned sizes: two 250×250 and one 498×373
(wider than the library's 480 px limit).

## s05 — convert once at boot, play converted frames: adopted

Before Bluetooth starts, each `/bg/bgN.gif` is decoded and scaled to cover
240×240 (or 240×320 for tall GIFs), centre-cropped, and the pixels each frame
paints are written to `/.sys/bgN.anim`. During playback only those spans are
read and pushed — ~2 KB of RAM.

| GIF | Source | Frames | .anim | Convert time |
|---|---|---|---|---|
| bg1 | 250×250 | 11 | 817 KB | 4.2 s |
| bg2 | 498×373 (shrunk) | 17 | 1468 KB | 10.0 s |
| bg3 | 250×250 | 39 | 2788 KB | 14.0 s |

- Converted once; later boots compare source size + timestamp and skip it.
- Heap before connecting: 48 KB (live decoding: 28 KB).
- Owner: animation smooth, music smooth, buttons fine, title band does not
  flicker (masked spans work).
- AnimatedGIF is vendored with `MAX_WIDTH 2048` so wide phone GIFs can be
  shrunk. `gif.inl` is renamed `gif_inl.h` — the Arduino builder does not copy
  `.inl` files out of a sketch's `src/`.

## Carry into the app

- **Reconnect by address, not by scan.** After the ESP32 resets, the JBL does
  not show up in discovery again until it is put back in pairing mode. The app
  must store the speaker's address and page it directly (library auto-reconnect
  with the saved address).
- Heap budget is tight: every new always-on buffer needs a line in this table.

---

# Later measurements (app, stages 1–5)

## Stage 2 — ring buffer at 1024 frames: rejected

| Ring | Underruns | Background frame time |
|---|---|---|
| 1024 (23 ms) | ~80 / s, while reading 99 % full | ~300 ms |
| 2048 (46 ms) | 0 | ~95 ms |

Bluetooth's largest single request: **128 frames**. The shortfall is waiting on
the SD card shared with the background reads, not request size.

## Stage 2 — stacks

Loop task used 2.8 KB of its default 8 KB (with calibration and title rendering
running in `setup()`); it now gets 5 KB. Decode task uses ~2.2 KB of 5 KB.

## Stage 5 — cost of linking WiFi

| Build | Static RAM (`Global variables`) | Heap at boot | Min heap, speaker connected |
|---|---|---|---|
| Player without WiFi | 53,336 B | 209 KB | ~16–30 KB, stable |
| Player with WiFi linked, never started | 74,176 B | 186 KB | ~12 KB, **reset loop** |

`nm` on the two ELFs: the difference is WiFi driver / supplicant / lwIP `.bss`
(`g_cnxMgr` 3.9 KB, `s_wifi_nvs` 1.3 KB, `dns_table`, `TxRxCxt`, `gWpaSm` …). The
upload page string (12 KB) is in `.flash.rodata`, not RAM. Hence two images
(docs/decisions.md).

| Image | Flash |
|---|---|
| Player | 1.29 MB (65 % of a 1.9 MB slot) |
| Uploader | 1.12 MB (56 %) |

Upload mode on the owner's board: music upload, GIF upload and the WiFi tab all
worked over the hotspot and the home network.

## Stage 6 — background reading vs the audio

A 104-frame GIF, converted, at 240x240:

| Approach | Underruns | Background |
|---|---|---|
| Whole frames, 114 KB each, up to 11 fps | 4 per 7 min, bursts of 26-40 seen earlier | 11 fps |
| Frames drawn in slices, pausing when audio low | **212 per 7 min** | 4 fps |
| Frame differencing + 350 KB/s read budget | 13 per 9 min, all at song changes | 7.6 fps, smooth |

Frame differencing (format 2) on the owner's four GIFs:

| GIF | Format 1 | Format 2 | Frames |
|---|---|---|---|
| bg1 | 817 KB | 716 KB | 11 |
| bg2 | 1468 KB | 1283 KB | 17 |
| bg3 | 2788 KB | 1356 KB | 39 |
| bg4 | — | 4537 KB | 104 |

Converting bg4 with the canvas allocated took the heap down to 10.5 KB, so the
allocation now stops with 48 KB free and falls back to whole frames.


---

# The on-board speaker (2026-09-28)

Measured with the standalone test project in `../CYD Speaker Test`, which carries
the full evidence trail in its `FINDINGS.md`. Two Stage 0 sketches preceded it,
`s06_dac_tone` and `s07_speaker_hunt`; **s06's original conclusion was wrong** and
its header has been corrected.

## The amplifier is enabled by GPIO4, active LOW

The speaker route shipped silent. Not quiet — silent, including from the DAC's own
hardware cosine oscillator, which uses no CPU, no DMA and no clock configuration.
s06 read that as proof the firmware was innocent and the hardware at fault.

The hardware was fine. A board photo showed a populated 8002E (1 W, 8 Ω,
bridge-tied load) with the documented resistor values — `203`/`103`/`472`, so not
the variant that ships R7/R8 as 0 Ω links and distorts — and a speaker correctly
wired across the output with neither side grounded. A replacement speaker changed
nothing.

GPIO4 holds the amplifier in shutdown unless driven LOW. Floating counts as
shutdown, so every test sketch was silent too. GPIO4 is also `CYD_RGB_R_PIN`, and
`cydRgbLedOff()` drives it HIGH in the first line of `setup()`.

Found by holding a tone on GPIO26 and driving each other pin in turn, bisecting on
"anything audible in this batch" rather than on when — the walk cannot be driven
interactively over a serial link from another machine, but a yes/no per batch needs
no timing judgement:

| Round | Pins | Audible |
|---|---|---|
| 19 HIGH | all drivable pins | no |
| 10 LOW | 25, 22, 27, 21, 17, 16, 4, 5, 0, 2 | **yes** |
| 5 LOW | 25, 22, 27, 21, 17 | no |
| 5 LOW | 16, 4, 5, 0, 2 | **yes** |
| 4 LOW | 16, 4, 16, 4 | **yes, alternating** |
| 3 LOW | 16, 16, 16 | no |
| 3 LOW | 4, 4, 4 | **yes, three of three** |

s07's sweep could never have found this: a square wave on a shutdown line toggles
mute, silently.

## `dac_continuous` write alignment

`buf_size` 512, `desc_num` 6, requested 44100 Hz, write length swept:

| Write length | Measured rate | Ratio |
|---|---|---|
| 256, 436, 440, 444, 448, 512, 768, 1024, 2048, 3072 | 44371 – 44790 | ~1.01 |
| 442, 446 | 22717, 22723 | **0.52** |
| 441, 443 | stalls, writes time out | deadlock |
| 3074 (> `desc_num * buf_size`) | — | deadlock |

32-bit DMA word alignment, independent of `chan_mode`, `clk_src` and frequency;
`buf_size` must be aligned too (441 failed as a `buf_size` as well). The half-rate
case is the dangerous one — everything plays an octave low and nothing reports it.

`speaker.cpp`'s `CHUNK` of 256 was always correct. The anomalies came from the
diagnostic's own `uint8_t buf[441]`, picked as "10 cycles of 1 kHz at 44100"
without noticing it is odd. With a 420-byte buffer at 1050 Hz: `319 writes, 0
failed, 44660 Hz`, every iteration, on both clock sources.

## Unrelated, still open

The board intermittently drops off USB — `[Errno 6] Device not configured`, then
the device node disappears. Seen three times, including before any of these changes
and while running stock sketches, so it is not the amplifier's current draw.
