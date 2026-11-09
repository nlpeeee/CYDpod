# 5-band graphic equalizer

Status: implemented on branch `claude/eq-5band-5fa70d`, compile- and host-checked. The board has not been tested yet. Evidence is in `docs/research/eq-5band.md`.

## Owner requirement

The owner asked for fixed-point biquad IIR filters rather than floating point, an Xtensa assembly inner loop, gain staging and per-stage saturation so the output stays clean with all five bands boosted, and roughly 2% CPU at 44.1 kHz. This spec keeps everything except the 2% figure, which is a target. The board measures the actual cost: the boot self-test and `stat: eq` report it, and the research note records the modelled cost.

## Where it runs

PCM moves from the SD card through the decoder task (core 1, priority 5) and the codec pool into `pushFrames()` in `audio.cpp`. From there it goes into the 6,144-frame ring, then `audio_pull()`, and on to A2DP (BTC_TASK on core 0), the wired I2S writer or the speaker.

The EQ sits at the top of `pushFrames()` on the decoder task, outside the ring lock and before PCM is published.

- Every output route hears the same EQ. `audio_pull()` and the Bluetooth task, which is the latency-critical consumer on core 0, are unchanged.
- The work lands on core 1, which already absorbs the burstier decoder load. The ring's 139 ms of buffering covers a slower decode block.
- The decoder sees only 44.1 kHz stereo at this point, because resampled input also passes through `pushFrames()`. One coefficient set covers every source rate.
- Volume is applied after the EQ in `audio_pull()`. It only attenuates, so it never undoes the headroom.

A gain change reaches the ear after the PCM already in the ring has played, up to about 139 ms. That is acceptable for a settings control.

## Bands

| Band | Centre | Type |
|---|---:|---|
| 1 | 60 Hz | RBJ peaking |
| 2 | 250 Hz | RBJ peaking |
| 3 | 1 kHz | RBJ peaking |
| 4 | 4 kHz | RBJ peaking |
| 5 | 14 kHz | RBJ peaking |

- Q is 0.5 for every band. With two octaves between centres, a narrower Q leaves dips between the bands. The first release used Q 0.9: with every band at +12 dB it swung between +7.2 and +14.4 dB, and the owner heard that as phasey and uneven.
- Gain runs from −12 to +12 dB. The UI steps 2 dB, giving 13 positions. Stored values are whole dB, and anything outside the range is clamped on load.
- **Sliders are targets, not filter gains.** Neighbouring bands overlap, so `designBiquads()` solves for the internal filter gains, using the interaction-matrix least-squares method of Välimäki & Liski's accurate cascade graphic EQ.
  - The solve aims for the combined response to equal each slider at its centre, and the mean of the two neighbouring sliders at the geometric midpoint between them.
  - It starts from a 12 dB reference matrix, refines twice with each filter's actual response at its current gain, and clamps the result to ±18 dB.
- **Accuracy, from the host test:**

  | Setting | Response |
  |---|---|
  | All bands +12 dB | +11.0 to +12.7 dB from 60 Hz to 14 kHz |
  | All bands +6 dB | +5.6 to +6.3 dB |
  | Any single band at ±12 or +6 dB | Within 0.6 dB of its target at its centre; within 1 dB of 0 at the other centres |

  Settings that alternate between extremes, such as +12 −12 +12, are only approximated, because the bands are this broad.
- The cascade runs in ascending frequency, using only the bands whose internal gain is at least 0.05 dB. A lone slider can therefore activate its neighbours for small corrections of about 1–2 dB.

## Number formats and arithmetic

The C++ reference in `eq_core.h` and the Xtensa loop in `eq_xtensa.h` follow this contract bit for bit.

| Quantity | Format |
|---|---|
| Coefficients | Q3.29 int32 `{b0, b1, b2, −a1, −a2}`, normalised by a0. Internal gains reach ±18 dB, where b0 is about 2.7, beyond the range of Q2.30. |
| Sample in the cascade | int32, int16 full scale = 2^28 (`x << 13`) |
| Product | `mulsh(c, s) = (int64(c)·s) >> 32`, floored. This is one MULSH instruction. |
| Accumulator | int32, 2^25 per full scale: the sum of five products, plus 2 |
| Stage output | the accumulator clamped to ±(2^27 − 1), then `y = (acc << 3) + 4` |
| Output | `y` × output gain (below), then `(v + 2^12) >> 13`, saturated to int16 |

The structure is Direct Form I, with x1, x2, y1 and y2 history for each band and channel.

### Rounding

MULSH floors each of the five products, which biases each one by about −½ step. The +2 on the accumulator removes most of that bias. The +4 on the output, which is half an accumulator step, returns through −a1−a2 ≈ 1 and cancels the rest. Without this, the low band's DC gain of 1/(1+a1+a2) turned the bias into several LSB of offset. The host test now measures a mean error within ±0.25 LSB.

### No accumulator wrap

- Stage outputs are bounded to |y| ≤ 2^30.
- `design()` rejects any stage whose |b0|+|b1|+|b2|+|a1|+|a2| reaches 16, which keeps |acc| < 2^31.
- Every single-band gain, 3,000 random combinations and the alternating extremes all pass the host test.

### Precision

- **Accumulator step:** 2^−10 of an int16 LSB.
- **Remaining error:** mostly Direct Form I truncation noise in the 60 Hz stage, whose poles sit close to the unit circle. On noise a single 60 Hz stage measures about 0.5 LSB RMS against double precision, which is low-frequency rumble around −93 dBFS. At 1 kHz and above the error is under 0.01 LSB.
- **Against an independent EQ:** FFmpeg's `equalizer` filter, which uses the same RBJ formula, matches the double-precision design to 0 LSB on every preset. The fixed-point path is within 6 LSB maximum and 1.5 LSB RMS of it. `tests/eq_ffmpeg_check.sh` runs this comparison.

## When coefficients are recomputed

`eq_apply(on, gains)` runs on the loop task at boot and on every Equalizer page change.

1. It solves the internal gains.
2. It designs the active biquads in double precision.
3. It evaluates the cascade on a log grid, using a cosine-only |H|² formula so each grid point costs one cosine.
4. It derives the scale and the output gain.
5. It quantises the coefficients.
6. It publishes the result under a small spinlock with a serial number. The serial line prints `eq: … stages … peak … level … design N us`.

The decoder compares that serial number once per `pushFrames()` call and adopts the new set at a chunk boundary. The decoder never designs anything and never waits on a design.

## Gain staging and limiting

The first release folded −(peak + 1 dB) into the cascade. That made every preset a net cut: Bass measured 6.8 dB quieter than bypass on pink noise, and the other presets 4–7.6 dB quieter. Users of Spotify's and iOS's EQs make the same complaint. Most phone EQs instead apply a boost as real gain and catch peaks with a limiter, and that is what this design now does.

- **Scale:** `min(1, 1/max|H|)` is folded into the first stage. On a sine no stage can reach full scale, whatever the sliders.
- **Output gain:** `10^(pre/20) / scale`, applied after the cascade.
  - `pre` is minus half of the pink-weighted average power boost from 100 Hz to 10 kHz, clamped to ≤ 0.
  - The overall level therefore rises by half the average boost. The boosted region is clearly louder than bypass, and the rest stays close to its old level.
  - **Bass (+6 +6 +2 0 0):** +4.3 dB at 60 and 250 Hz, +0.4 dB at 1 kHz, −1.7 dB at 14 kHz. Overall it is +1.7 dB.
  - **Measured on pink noise:** the new presets are 0.1–1.6 dB louder than bypass, where the old ones were 4–7.6 dB quieter.
- **Limiter:** a stereo-linked peak limiter on the output gain.
  - Threshold −1 dBFS, with instant attack and no look-ahead, so the gain falls on the sample that would exceed the threshold.
  - Release time constant about 150 ms.
  - Host test: with every band at +12 dB and full-scale sines, squares, noise, DC and impulses, the output peaks at exactly −1.00 dBFS. The Bass preset on −20 dBFS noise never limits.
  - `stat: eq … limited N` counts the frames it reduced in each window.
- **Glide:** the output gain follows its target with a time constant of about 10 ms. When a design replaces another, `State::retarget()` keeps scale × gain continuous before the glide starts.
  - Switching the EQ on starts at exactly unity overall gain and glides to the new level.
  - Changing a preset or moving a slider never steps the level.
- **Per-stage saturation:** each stage clamps its accumulator to ±4× full scale before shifting. It is now a safety net that clips rather than wraps; the limiter makes the output decision.

## Stereo

Left and right share coefficients and keep separate history. The loop runs one channel at a time over the interleaved int32 chunk with an 8-byte stride, so it needs no deinterleave copy. The limiter is linked across the two channels, so the stereo image does not shift.

## State changes

- **Track change or seek:** `requestFlush()` runs on the decoder and calls `eq_flush()`. History is cleared and the output gain is set straight to its target, so a new track never hears the previous track's ringing.
- **A band starting to run:** a band whose internal gain was 0, and so was not running, starts as the identity filter with x = y = its input history. That input history is the previous running band's output history, or the raw input. The host `seed` test measured a 178 LSB step across a design change, against a typical 360 LSB step.
- **Coefficient changes on running bands:** history is kept and the DF1 structure absorbs the change.
- **Turning the whole EQ on:** the overall gain starts at unity and glides (see above).

## Bypass

The EQ is bypassed when it is switched off or every band is at 0 dB. In bypass `design()` returns no stages, `eq_bypass()` returns true, and `pushFrames()` calls the original publication path with the decoder's own buffer.

- The PCM is bit-exact, and the host test checks this.
- The only cost is one volatile compare per batch of up to 128 frames.
- No filter, scratch copy or cycle counter runs.

A band whose internal gain is about 0 dB is also skipped, so the cost scales with the number of running stages.

## Implementation

- **`eq_core.h`:** pure C++ design (interaction-corrected), reference stage, output gain and limiter, history seeding and chunk processing. The host tests and `tests/eq_render.cpp` use it.
- **`eq_xtensa.h`:** `cydeq_stage_xtensa`, the windowed-ABI LX6 loop in IRAM. It uses MULSH, MIN/MAX and the zero-overhead LOOP, at 21 instructions per sample per stage. The output stage (gain glide, limiter and conversion) is C in `eq_core.h` and costs about 45 instructions per frame. It calls a software float divide only on frames the limiter reduces. All 14 free registers are used: five coefficients, four history words, the clamp limit, the pointer and three temporaries. The history pointer is spilled to `sp+0` under `entry a1, 48`, the same layout GCC uses for leaf locals, which keeps it clear of the window save area.
- **`eq.cpp`/`eq.h`:** publication, the decoder entry points and statistics.
  - It processes in 32-frame chunks through a 256-byte int32 scratch buffer and a 128-byte int16 output buffer.
  - It brackets each chunk with `esp_cpu_get_cycle_count()`.
  - `eq_begin()` runs the asm and C++ loops on 256 frames of noise with all bands at +12 dB. It compares them word for word and prints `eq: selftest PASS|FAIL … cyc/frame …`. If they ever differ, the C++ loop is used.
- **`audio.cpp`:** `pushFrames()` wraps the original `publishFrames()`, and `requestFlush()` clears the EQ history.

## Settings and UI

- **Storage:** `Settings::eqOn` (NVS `eqOn`, uchar, off by default) and `Settings::eqGain[5]` (NVS `eqGain`, 5-byte blob, flat by default). `settings_save()` saves them with the other keys. A blob of the wrong size is ignored and stays flat. Presets are not stored: the name shown is derived from the gains.
- **Navigation:** two rows open the Equalizer page, and both show On or Off.
  - Settings → Playback, fifth row.
  - Now Playing → Options, fifth row.

  In landscape, both lists show four rows, so the Equalizer row is reached by scrolling.
- **Equalizer page (`Mode::Equalizer`):** a browse page with the standard header and the mini-player. Its geometry lives in `classic_layout.h` (`eqControl`, `eqColumn`, `eqTrack`, `eqStepAt`/`eqStepY`), and paint and input share those rectangles.
  - **Buttons:** a strip of three under the header.
    - **EQ On/Off:** filled when on.
    - **Preset:** shows the matching preset name, or Custom.
    - **Reset:** sets every band flat.

    The buttons act on release, with the same press highlight as the Home tiles.
  - **Sliders:** five equal columns, one per band.
    - Each column shows the gain at the top (for example `+4`), a vertical track running from −12 dB at the bottom to +12 dB at the top with a 0 dB tick, a knob, and the band label (`60`, `250`, `1k`, `4k`, `14k`).
    - Pressing or dragging anywhere in a column sets that band to the nearest 2 dB step and repaints only that column.
    - When the EQ is off, the tracks are drawn in the disabled colour, but they still respond.
  - **Applying changes:**
    - Editing a band switches the EQ on.
    - `eq_apply()` runs only when the snapped value changes.
    - NVS is written once, on release.
    - A slider drag never triggers swipe-back.
- **Presets (`cydeq::Presets`):**

  | Preset | Gains, 60 Hz → 14 kHz (dB) |
  |---|---|
  | Flat | 0 0 0 0 0 |
  | Bass | +6 +6 +2 0 0 |
  | Vocal | −2 0 +4 +4 +2 |
  | Treble | 0 0 +2 +6 +6 |
  | Loudness | +6 +4 0 +2 +6 |

  These values were retuned for small speakers: Bass covers 60 and 250 Hz together, because a speaker such as the JBL Flip 3 barely reproduces 60 Hz.

  The Preset button cycles through this list in order. From Custom it goes to Bass. Any preset other than Flat switches the EQ on, and Flat leaves the switch as it is.
- **Serial:**
  - `cmd: eq toggle|preset|reset` for each button press.
  - `stat: eq on … bands … pregainCdB … kernel xtensa|c++ frames … cycPerFrame … cpu …%` every report window. This shows the measured cost on the decoder core.

## Not included

- A frequency-response curve drawing, user-saved presets, shelving bands, per-route EQ settings and a manual preamp.
- Volume-aware headroom, which would use spare headroom below 100% volume instead of the limiter. Volume is applied after the ring, so the cascade's int16 output cannot use that headroom.
- A board-measured cost of 2%. The modelled cost is about 0.8% of a 240 MHz core per running stage, plus about 0.8% for the output stage and limiter, or roughly 5% with all five stages running. The board figures from the self-test and `stat: eq` replace these estimates.
