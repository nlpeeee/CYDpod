# EQ-5BAND evidence

The base is `main` at v0.5.4 (9bc1e5e) on branch `claude/eq-5band-5fa70d`, and the specification is `docs/specs/eq-5band.md`. The checks below are host and compile-only. Nothing was flashed and no serial port was opened. Board behaviour and the measured CPU cost still need the owner's capture.

## Insertion point and headroom today

PCM flows from the decoder task (core 1, priority 5) into `pushFrames()`. From there it goes into the 6,144-frame ring, through `audio_pull()`, and out to BTC_TASK on core 0, the wired writer or the speaker. The EQ runs at the top of `pushFrames()` on core 1, outside the ring lock. That leaves `audio_pull()` and the Bluetooth task unchanged.

The repository and the local logs contain no board capture of `stat: cpu idle1=…`. The DIAG-CPU line exists, but no owner serial log with it has been checked in. Because I may not touch the board, I could not measure today's idle headroom on core 1. The existing evidence is indirect. In `playback-headroom.md`, FLAC block decodes peak at about 49 ms. The 139 ms ring absorbs those peaks, and the EQ adds a steady load rather than a burst. The owner should compare `idle1` in `stat: cpu` with the EQ off and with all five bands on. The new `stat: eq` line reports the EQ's own cycles directly.

## Host checks

The command was `wsl -d Debian sh tests/build_eq.sh`, with the log at `.tools/logs/eq-host.log`.

~~~
design: all +12 dB peak +14.47 dB pre-gain -15.47 dB
response all+12  worst 0.0018 dB, noise max error 1.97 LSB, mean 0.007 LSB
response all-12  worst 0.0015 dB, noise max error 1.01 LSB, mean -0.015 LSB
response mixed   worst 0.0022 dB, noise max error 1.65 LSB, mean 0.030 LSB
response smile   worst 0.0014 dB, noise max error 1.53 LSB, mean 0.013 LSB
headroom: full-scale sine sweep peak 29133 (-1.02 dBFS), clipped samples 0
headroom: square 60 Hz      ideal output beyond full scale  8632 samples, max |fixed - clipped ideal| 3.3 LSB
headroom: square 1 kHz      ideal output beyond full scale  6006 samples, max |fixed - clipped ideal| 1.3 LSB
headroom: white noise       ideal output beyond full scale     0 samples, max |fixed - clipped ideal| 2.0 LSB
headroom: alternating +-FS  ideal output beyond full scale     0 samples, max |fixed - clipped ideal| 1.6 LSB
headroom: DC +FS            ideal output beyond full scale     0 samples, max |fixed - clipped ideal| 2.1 LSB
headroom: impulses          ideal output beyond full scale     0 samples, max |fixed - clipped ideal| 15.4 LSB
seed: step across switch 194 LSB, typical step 175 LSB
PASS: eq core (response vs float, full-scale headroom, bit-exact bypass, band seeding)
~~~

The ASan/UBSan build of the same test printed the same PASS line.

- **Frequency response:** at 31 frequencies × 4 gain sets, the fixed-point response is within 0.05 dB of the double-precision RBJ cascade. The measured worst case is 0.0022 dB. Run sample by sample against a double DF1 on noise, the error stays at or below 2 LSB, with a mean within 0.03 LSB.
- **DC fix:** before the feedback bias (`y = (acc<<2)+2`) was added, the mean error was −3.5 LSB for every gain set. MULSH's floor bias passes through the 60 Hz band's 1/(1+a1+a2) ≈ 13,700. The bias brings it to about 0.
- **Overflow:** every gain of every band, plus 3,000 random combinations, passes the no-wrap bound. With every band at +12 dB, a full-scale 20 Hz–20 kHz sine sweep never clips. The ideal output of full-scale square waves exceeds full scale. In those cases the fixed-point output saturates and stays within 3.3 LSB of the clipped ideal, so it saturates rather than wrapping.
- **Bypass:** the flat setting produces no stages, and the output is bit-exact over 4,096 frames of noise including both extremes.

## Xtensa loop against the C++ reference

The command was `wsl -d Debian sh tests/build_eq_xtensa.sh`, with the log at `.tools/logs/eq-xtensa-sim.log`.

1. The pinned toolchain (`esp-x32` 2601, `xtensa-esp32-elf-g++`) assembles `eq_xtensa.h`.
2. objdump disassembles the linked section, `.iram1.cydeq_stage_xtensa`.
3. `tests/eq_xtensa_sim.py` interprets that machine code against 600 vector cases from `cydeq::stageRef`. These are 15 real design stages plus random sets that are unstable or out of range, so the clamp and wrap paths run. Each case covers 194 words plus the stored history.

~~~
eq xtensa sim: cases 600 words 116400 mismatches 0
eq xtensa sim: per sample 21.0 instructions, 21.0 modelled cycles; per call 21 instructions, 22 modelled cycles
PASS: eq xtensa loop bit-exact with stageRef (assembled ESP32 code, instruction model)
~~~

I first tried WSL's `qemu-xtensa` 10.0.13, but none of its stock cores implements MUL32_HIGH (`mulsh`). dsp3400 also lacks MULL. I dropped that route. The simulator models only the instructions the loop uses, and it would stop on any other instruction. Its cycle model adds one bubble when an instruction uses a load or multiply result in the very next instruction. The schedule has no such adjacency, so the modelled count equals the instruction count. LX6 multiplier latency on the real part is not modelled beyond that.

On the board, `eq_begin()` runs the same comparison on the device itself: 256 frames, every band at +12 dB, assembly against C++. It prints the measured cycles per frame:

~~~
eq: selftest PASS mismatches 0  5 bands xtensa N cyc/frame (x.x% of 240 MHz core)  c++ M cyc/frame (y.y%)  kernel xtensa
~~~

During playback it prints `stat: eq … cycPerFrame … cpu …%` every report window.

## CPU cost (modelled, not measured)

| Part | Cycles per stereo frame |
|---|---:|
| One active band (2 channels × 21, plus the call overhead of 2 × 22 / 32 frames) | ≈ 43.4 |
| Five bands | ≈ 217 |
| int16↔int32 conversion and the chunk wrapper, compiled C++ (not modelled) | ~10–20, estimated |

At 44,100 frames/s on a 240 MHz core, one band costs about 0.8% and all five cost about 4.3–4.6%. Bypass costs a single compare per batch. This is roughly twice the owner's 2% figure. Reaching 2% would take about 10 cycles per sample per stage, which this register budget and per-stage saturation do not allow with 32-bit MULSH precision.

Two cheaper but lower-precision options are possible:

- 16-bit MAC16 coefficients. These are too coarse for the 60 Hz poles.
- Dropping the saturation instructions, which saves 3 of the 21 instructions. I did not take either.

The board self-test number replaces these estimates once the owner captures it.

## Size against v0.5.4

The command was `tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia -LargePcmRing -PcmRingFrames 6144 -BuildLabel eq5band`, run with the trimmed Bluedroid already active. It printed `PASS: compile-only checks completed; no device commands were run.` and `PASS single pool` (0x7868) for both images. The logs are `.tools/logs/codec-pool-v36-headroom-ring6144-eq5band{,-wired}-jc2432w328c.log` and `eq-5band-check.log`.

| Image | Static RAM v0.5.4 → EQ | Δ | Flash v0.5.4 → EQ | Δ |
|---|---|---:|---|---:|
| Bluetooth | 79,668 → 80,524 B | +856 B | 1,299,561 → 1,316,037 B | +16,476 B |
| Wired | 79,676 → 80,524 B | +848 B | 1,299,701 → 1,316,145 B | +16,444 B |

The static RAM breaks down as follows:

| Item | Size |
|---|---:|
| Published and active coefficients | 2 × 112 B |
| History | 165 B |
| int32 scratch | 256 B |
| int16 output | 128 B |
| Locks and statistics | the rest |

The EQ allocates no heap. The 98-byte loop lives in IRAM. Most of the flash growth is the double-precision `pow`, `sin`, `cos` and `log10` code used by the coefficient design on the loop task. Designing in float would cut that, at a small accuracy cost to the 60 Hz coefficients. Free heap after Bluetooth should drop by roughly the static RAM delta. The owner's `heap:` lines will confirm it.

## Not run or not established

- **Board checks:** audio quality, the self-test result, the measured cycles and `idle1` with the EQ on and off, Bluetooth and wired playback, and the Settings → Playback → Equalizer page on the panel.
- **Classic UI host suite under MSVC:** `tests/build_catalog.cmd` needs MSVC `cl`, which this machine lacks. The g++ build of the suite, described below, stands in for it.
- **Wired UI variant (`classic_ui_test_wired`):** not run on the host. The firmware compile covers the wired image.
- **Full host-media suites:** these were not rerun because `-SkipHostMedia` was set. The decoder and codec paths are unchanged apart from the `pushFrames` wrapper.

## Graphical Equalizer page and presets

This round replaced the list rows with a slider page (`Mode::Equalizer`). It also added a fifth Options row, Equalizer, and five presets in `cydeq::Presets`. The UI is described in the spec under Settings and UI.

**`tests/build_eq.sh` (WSL):** PASS. It now also checks that every preset designs, sits on the 2 dB grid, matches back to itself and cycles in order, and that Custom gains are detected and cycle to Bass. The line from `.tools/logs/eq-host.log`:

~~~
PASS: eq core (response vs float, full-scale headroom, bit-exact bypass, band seeding, presets)
~~~

**Classic UI suite:** MSVC is not installed, so I built `tests/classic_ui_test.cpp` and the same sources as `build_catalog.cmd` with WSL g++ 14.2. I used `-DCYDPOD_CATALOG_HOST` and the catalog shim, and made the fixture with the cmd file's own ffmpeg command. I ran it with `CYDPOD_UI_CONTINUE=1`. The log is `.tools/logs/eq-ui-gcc-run.log`.

The new regression passed:

~~~
PASS EQ-5BAND Equalizer page: Playback and Options entry, sliders, presets, Reset, On/Off, no swipe-back from sliders, all rotations
~~~

It covers the following in all four rotations:

- the page opens from Playback and from Options;
- the top of the 60 Hz track gives +12 dB and switches the EQ on, saves on release, and the page shows `+12`, `EQ On` and `Custom`;
- the bottom of the 1 kHz track gives −12 dB;
- Preset goes from Custom to Bass, then Vocal;
- Reset sets every band flat;
- On/Off switches the EQ off;
- a sideways drag across a slider does not navigate back.

The same g++ build fails 12 other assertions. These are the golden geometry and dialog tables, Refresh/Upload confirmations, BT-A disconnect, a refused Bluetooth cancel, and marquee hold. I built the PR's previous commit (2928669) the same way, in a temporary worktree I have since removed. It fails exactly the same 12 assertions. They come from the g++ build of an MSVC-targeted suite, not from this change. The MSVC run remains open.

**Compile:** `tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia -LargePcmRing -PcmRingFrames 6144 -BuildLabel eq5band-ui` passed for both images. It printed `PASS: compile-only checks completed; no device commands were run.` and `PASS single pool` (0x7868).

| Image | Static RAM | Flash | Δ flash vs first EQ build | Δ vs v0.5.4 |
|---|---:|---:|---:|---|
| Bluetooth | 80,524 B | 1,317,493 B | +1,456 B | +856 B RAM, +17,932 B flash |
| Wired | 80,524 B | 1,317,613 B | +1,468 B | +848 B RAM, +17,912 B flash |

Static RAM did not change from the first EQ build. The page keeps two bytes of press state, which fit in existing padding.

**Board (owner):**

- In portrait and landscape, drag each slider and check the knob, the fill and the gain label.
- Press On/Off, Preset and Reset.
- While a track plays, open the page from Now Playing → Options and hear the change.
- Confirm that a sideways drag on a slider never goes back.
- Read `stat: eq` with all five bands active.

## Sound-quality investigation and fix

### What the owner heard

With the EQ on, every preset sounded quieter and thinner than bypass, and the result was phasey or uneven. The owner asked me to check the maths against other EQs.

### Diagnosis

**The filter maths was correct.** `tests/eq_ffmpeg_check.sh` ran three comparisons:

- the same design through FFmpeg's `equalizer` filter, which uses the RBJ peaking formula with `t=q`, chained with the same Q, internal gains and output gain;
- the same design in double precision;
- the firmware fixed-point path.

FFmpeg and double precision agreed exactly, with **0 LSB difference** on all six settings: four presets, all bands at −12 dB, and a smile curve. The fixed-point path differs from double precision by at most 6 LSB, 0.3–1.5 LSB RMS, with a mean within 0.06 LSB. A per-stage probe traced almost all of that difference to Direct Form I truncation noise in the 60 Hz stage. On noise that stage measured 0.48–0.56 LSB RMS, which is low-frequency rumble around −93 dBFS, so it can't be heard. At 1 kHz the error is under 0.01 LSB, and at 4 kHz and above under 0.002 LSB.

**The problems were design choices in the first release.**

1. **Gain staging.** The cascade folded in −(peak + 1 dB), so every preset was a net cut and no frequency rose above bypass. Measured mean level on −20 dBFS pink noise, from `ffmpeg volumedetect` on the A/B files:

   | File | Mean level | Against bypass |
   |---|---:|---:|
   | Bypass | −37.2 dB | |
   | Bass, old | −44.0 dB | −6.8 dB |
   | Vocal, old | −41.4 dB | −4.2 dB |
   | Treble, old | −44.8 dB | −7.6 dB |
   | Loudness, old | −42.2 dB | −5.0 dB |

2. **Ripple between bands.** With Q 0.9 and two octaves between centres, all bands at +12 dB gave +7.2 to +14.4 dB across 60 Hz–14 kHz, a 7.3 dB swing. All bands at +6 dB gave a 3.7 dB swing. Vocal dipped 1.5 dB at 2 kHz, between its two boosts. A wider Q alone still leaves about 5 dB.
3. **Presets.** Bass put +8 dB at 60 Hz, below what a small Bluetooth speaker plays, so mostly the overall cut was audible.

### Changes

- **Interaction-corrected design** (Välimäki & Liski's accurate cascade graphic EQ), with Q 0.5.
  - All bands at +12 dB now span +11.0 to +12.7 dB (1.66 dB ripple). All at +6 dB span +5.56 to +6.29 dB (0.73 dB).
  - Any single band at ±12 or +6 dB lands within 0.6 dB of its target at its centre and within 1 dB of 0 at the other centres.
  - I tested Q 0.3–1.2 with the correction. Q 0.5 balances ripple against accuracy on single bands and alternating settings.
- **Q3.29 coefficients**, needed because internal gains now reach ±18 dB. The Xtensa loop changed only its clamp constant, output shift and bias.
- **Half-loudness-matched output gain**, a stereo-linked −1 dBFS limiter, and a 10 ms glide with `retarget()` continuity.
- **Presets retuned:**

  | Preset | Gains (dB) |
  |---|---|
  | Bass | +6 +6 +2 0 0 |
  | Vocal | −2 0 +4 +4 +2 |
  | Treble | 0 0 +2 +6 +6 |
  | Loudness | +6 +4 0 +2 +6 |

**New preset levels.** On the same noise, from `volumedetect`, and the designed net response from `eq_core_test`:

| Preset | Mean level | Against bypass | Net at 60 / 250 / 1k / 4k / 14k Hz (dB) |
|---|---:|---:|---|
| Bass | −36.0 dB | +1.2 dB | +4.3 +4.3 +0.4 −1.6 −1.7 |
| Vocal | −37.1 dB | +0.1 dB | −3.2 −1.2 +2.6 +2.7 +0.7 |
| Treble | −36.5 dB | +0.7 dB | −1.7 −1.6 +0.4 +4.5 +4.4 |
| Loudness | −35.6 dB | +1.6 dB | +4.5 +2.6 −1.2 +0.9 +4.7 |

### Checks

- **`tests/build_eq.sh` (WSL):** PASS, and the sanitizer build passes too. The log is `.tools/logs/eq-host.log`.

  ~~~
  curve: all +12 dB spans +11.02..+12.68 dB from 60 Hz to 14 kHz (ripple 1.66 dB)
  curve: all +6 dB spans +5.56..+6.29 dB (ripple 0.73 dB)
  response all+12  worst 0.0042 dB, noise max error 6.09 LSB, mean 0.050 LSB
  limiter: sine sweep        peak 29205 (-1.00 dBFS), frames limited 2594
  limiter: square 1 kHz      peak 29205 (-1.00 dBFS), frames limited 1997
  PASS: eq core (corrected curve, loudness staging, response vs float, limiter, bit-exact bypass, continuity, presets)
  ~~~

  Every limiter case peaks at or below −1.00 dBFS. The Bass preset on −20 dBFS noise never limits. Switching the EQ on starts at unity overall gain.
- **`tests/build_eq_xtensa.sh`:** PASS. The Q3.29 loop matches `stageRef` across 600 cases and 116,400 words, at 21 instructions per sample. The log is `.tools/logs/eq-xtensa-sim.log`.
- **`tests/eq_ffmpeg_check.sh`:** PASS, with the results given under Diagnosis. The log is `.tools/logs/eq-ffmpeg-check.log`.
- **Output stage cost:** compiled for Xtensa, it is about 45 instructions per frame. It calls only `__divsf3`, and only on limited frames, and it now converts float to int32 in hardware rather than through a float→int64 libcall. I estimate +0.8% of a 240 MHz core, so all five stages running costs roughly 5% in total. This is modelled, not measured.
- **Compile:** `tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia -LargePcmRing -PcmRingFrames 6144 -BuildLabel eq5band-fix` printed `PASS: compile-only checks completed; no device commands were run.` and `PASS single pool` (0x7868) for both images.

  | Image | Static RAM | Flash | Δ vs UI build | Δ vs v0.5.4 |
  |---|---:|---:|---|---|
  | Bluetooth | 80,572 B | 1,320,113 B | +48 B RAM, +2,620 B flash | +904 B RAM, +20,552 B flash |
  | Wired | 80,572 B | 1,320,249 B | +48 B RAM, +2,636 B flash | +896 B RAM, +20,548 B flash |

### Listening files

`sh tests/eq_ffmpeg_check.sh <music-file>` writes `tests/out/eq/ab/*.wav`: bypass, then old and new for each preset, from the first 60 s of any track FFmpeg can read. Without a file it uses −20 dBFS pink noise. Listening on headphones on a PC separates the EQ itself from the speaker.

### Still open, on the board

- the boot self-test, measured cycles per frame, and `stat: eq … limited N` during normal music;
- how the new presets sound on the JBL Flip 3 and on AirPods;
- underruns with all five stages running;
- `design N us` on the ESP32, because the corrected design does about 450 double-precision transcendental calls per slider step on the loop task.
