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
- **Classic UI host suite:** `tests/classic_ui_test.cpp` gained an `eq_apply` stub and the new `Classic.Equalizer` page in its reachable-page set. Its runner (`tests/build_catalog.cmd`) needs MSVC `cl`, which this machine lacks, so it was not run. The firmware compile covers `classic_screen.cpp`.
- **Full host-media suites:** these were not rerun because `-SkipHostMedia` was set. The decoder and codec paths are unchanged apart from the `pushFrames` wrapper.
