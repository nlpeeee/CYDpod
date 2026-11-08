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

- Q is 0.9 for every band, about 1.5 octaves wide. Adjacent bands overlap, so the response is smooth with no notches between centres.
- Gain runs from −12 to +12 dB. The UI steps 2 dB, giving 13 positions. Stored values are whole dB, and anything outside the range is clamped on load.
- The cascade runs in ascending frequency, using only the bands whose gain is not zero.

## Number formats and arithmetic

The C++ reference in `eq_core.h` and the Xtensa loop in `eq_xtensa.h` follow this contract bit for bit.

| Quantity | Format |
|---|---|
| Coefficients | Q2.30 int32 `{b0, b1, b2, −a1, −a2}`, normalised by a0 |
| Sample in the cascade | int32, int16 full scale = 2^28 (`x << 13`) |
| Product | `mulsh(c, s) = (int64(c)·s) >> 32`, floored. This is one MULSH instruction. |
| Accumulator | int32, 2^26 per full scale: the sum of five products, plus 2 |
| Stage output | the accumulator clamped to ±(2^28 − 1), then `y = (acc << 2) + 2` |
| Output | `(y + 2^12) >> 13`, saturated to int16 |

The structure is Direct Form I, with x1, x2, y1 and y2 history for each band and channel.

### Rounding

MULSH floors each of the five products, which biases each one by about −½ step. The +2 on the accumulator removes most of that bias. The +2 on the output, which is half an accumulator step, returns through −a1−a2 ≈ 1 and cancels the rest.

This matters for the low bands. The DC gain of the 60 Hz band's feedback path is 1/(1+a1+a2), about 13,700. Without the cancellation, the host test measured about 3.5 LSB of DC offset.

### No accumulator wrap

- Stage outputs are bounded to |y| < 2^30.
- `design()` rejects any stage whose |b0|+|b1|+|b2|+|a1|+|a2| reaches 8, which keeps |acc| < 2^31.
- Every single-band gain, plus 3,000 random combinations, passes the host test. The largest sum is about 7.0, from the 60 Hz band.

### Precision

The accumulator step is 2^−11 of an int16 LSB. Coefficient quantisation at 2^−30 places the 60 Hz poles well within tolerance.

## When coefficients are recomputed

`eq_apply(on, gains)` runs on the loop task at boot and after each Equalizer row tap.

1. It designs the active biquads in double precision.
2. It evaluates the cascade's |H| at 160 log-spaced points from 20 Hz to 20 kHz, plus the band centres.
3. It computes the pre-gain and quantises the coefficients.
4. It publishes the result under a small spinlock with a serial number. The serial line prints `eq: … design N us`.

The decoder compares that serial number once per `pushFrames()` call and copies the new set at a chunk boundary. The decoder never designs anything and never waits on a design.

## Headroom and saturation

- **Pre-gain:** G = min(1, 10^(−1/20) / max|H|). It is folded into the first active stage's b coefficients, so it costs nothing at run time. The extra 1 dB margin covers wideband peaks. With all five bands at +12 dB, the cascade peak is +14.47 dB and the pre-gain is −15.47 dB. A full-scale sine at any frequency then leaves at about −1 dBFS. The host test measured −1.02 dBFS with no clipped samples.
- **Per-stage saturation:** each stage clamps its accumulator to ±4× full scale (+12 dB of internal headroom) before shifting. The final conversion saturates to int16. Saturation clips; it never wraps.
- **Limits of what headroom can promise:** a filter's peak gain on wideband input exceeds its sinusoidal peak, and so does a square wave's Gibbs overshoot. A full-scale square wave through five +12 dB bands has ideal output above full scale, and there the output saturates cleanly. In the host test that output stayed within 3.3 LSB of the clipped ideal. "No clipping" holds for full-scale sinusoids and for normal programme material with the 1 dB margin. It is not a guarantee for full-scale synthetic square waves.
- **Level change:** boosting bands lowers the overall level, so the EQ never needs a limiter. Turning bands up makes the other frequencies quieter rather than clipping.

## Stereo

Left and right share coefficients and keep separate history. The loop runs one channel at a time over the interleaved int32 chunk with an 8-byte stride, so it needs no deinterleave copy.

## State changes

- **Track change or seek:** `requestFlush()` runs on the decoder and calls `eq_flush()`. History is cleared, so a new track never hears the previous track's ringing.
- **A band switching on:** a band that was at 0 dB, and so not running, starts as the identity filter with x = y = its input history. That input history is the previous running band's output history, or the raw input. Turning a band on mid-song therefore gives no step. The host `seed` test measured a 194 LSB step against a typical 175 LSB.
- **Coefficient changes on running bands:** history is kept and the DF1 structure absorbs the change. A band leaving the cascade is at ±2 dB at that moment, so the discontinuity is small.
- **Turning the whole EQ on:** this starts every band from zero history. The pre-gain level step is inherent in turning the EQ on.

## Bypass

The EQ is bypassed when it is switched off or every band is at 0 dB. In bypass `design()` returns no stages, `eq_bypass()` returns true, and `pushFrames()` calls the original publication path with the decoder's own buffer.

- The PCM is bit-exact, and the host test checks this.
- The only cost is one volatile compare per batch of up to 128 frames.
- No filter, scratch copy or cycle counter runs.

A single band at 0 dB is also skipped, so the cost scales with the number of active bands.

## Implementation

- **`eq_core.h`:** pure C++ design, reference stage, history seeding and chunk processing. The host tests use it.
- **`eq_xtensa.h`:** `cydeq_stage_xtensa`, the windowed-ABI LX6 loop in IRAM. It uses MULSH, MIN/MAX and the zero-overhead LOOP, at 21 instructions per sample per stage. All 14 free registers are used: five coefficients, four history words, the clamp limit, the pointer and three temporaries. The history pointer is spilled to `sp+0` under `entry a1, 48`, the same layout GCC uses for leaf locals, which keeps it clear of the window save area.
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
  | Bass | +8 +4 0 0 0 |
  | Vocal | −2 0 +4 +4 0 |
  | Treble | 0 0 0 +4 +8 |
  | Loudness | +6 +2 0 +2 +6 |

  The Preset button cycles through this list in order. From Custom it goes to Bass. Any preset other than Flat switches the EQ on, and Flat leaves the switch as it is.
- **Serial:**
  - `cmd: eq toggle|preset|reset` for each button press.
  - `stat: eq on … bands … pregainCdB … kernel xtensa|c++ frames … cycPerFrame … cpu …%` every report window. This shows the measured cost on the decoder core.

## Not included

- A frequency-response curve drawing, user-saved presets, shelving bands, per-route EQ settings and a limiter.
- A board-measured cost of 2%. The modelled cost of the loop alone is about 0.85% of a 240 MHz core per active band, or about 4.3% for five bands. The board figures replace these estimates once the owner captures them.
