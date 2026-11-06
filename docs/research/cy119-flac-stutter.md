# CY-119: FLAC stutter on v34 (2f49747)

Verdict: **never fixed; structural, modelled not measured.** One FLAC frame (4096 samples, 92.9 ms) is decoded inside one unyielding `m4a.next()`; the ring holds 46 ms. No hardware evidence of smooth FLAC exists (cy51c/57/58/37 notes are in no worktree). Measured = host/objdump; modelled = ESP32.

## Owner file (read in place): 44.1 kHz/16/2ch, 934 kbps, block 4096
- Measured: 1753 frames, **mean 10,845 B, p99 13,667, max 14,074 B** (0x0036FA; `0x36fa0a` was a misparse). 98.4% exceed one 2048 B fill (`flac_pool.h:9,85`): 5.3 reads/frame, up to 7.
- No resampler in the path (`codec_track.cpp:176`, `audio.cpp:303`).
- Host x86 -O1 (loaded machine, relative only): 749 us/frame mean, 1116 p99; time tracks bytes (corr 0.79); 626 kbps file 104 ns/frame vs 132 ns at 939 kbps.

## Cost model (modelled; 240 MHz, decode prio 5 core 1)
Xtensa `-Os` objdump of `flac_core.c`: bit-reader read/fill are not inlined, each read calls `__lshrdi3` 2-3 times (64-bit `val`), unary code is read **one bit per call** (`miniflac.h:5477`), LPC is int64 (~16 instr/tap). Estimate 1,500 (bottom-up) to 3,300 (host x4-6) cycles/frame = 6-14 us.

| Case | CPU | Burst/frame (decode + SD) | vs 46 ms |
|---|---|---|---|
| owner file, 20 MHz SD | 27-62% | 26-57 + 11-16 (5.3 x 2-3 ms) = **37-73 ms** | at/over |
| same, 4 MHz fallback | | SD alone ~27 ms | over |
| 1152 blocks | same | 7-16 + 3-4 | fine |
| 48k/16, 4608 | +8% FIR | 36-66 | at/over |
| MP3 320 kbps | ~25% | 8-12 + 1-3 | 3.5-4x margin |
| 88.2/96k 16-bit | | rejected, no FIR bank (`codec_track.cpp:65`) | |
| 24-bit | | rejected `flac_bits` (`flac_pool.h:63`) | |

## Ring and counters
Ring 2048 frames (`audio.cpp:20`); decode starts when the ring is full (`audio.cpp:273`), so 46 ms is the best case. `underruns` counts short pulls (`audio.cpp:106`), not ms. `diag: sd read max` (`app.ino:314`) is SD + whole-frame decode (mislabelled). `stat: pcm` times only the FIR.

## Ranked causes
1. Burst > runway: `audio.cpp:20,275`, `flac_pool.h:125-145`.
2. Slow miniflac build: `flac_core.c` -Os, 64-bit reader.
3. 2048 B SD fills inside the burst; silent 4 MHz fallback `storage.cpp:263`.
4. Ring identical for MP3 and FLAC.
5. Minor: per-frame critical section `audio.cpp:117`; NVS save/30 s `app.ino:245`; REV-1 C-3 (`flac_pool.h:112`, damaged files), C-4 (`:137`), C-1 (see `stacks dec`).
Ruled out: resampler, UI paint (prio 1; backs off `classic_screen.cpp:696`), SD sharing (own VSPI), BT (core 0).

## Owner capture (10 s lines during a stutter)
`sd: clock N Hz` (boot), `diag: sd read max`, `stat: audio in 44100 Hz ringLow L/2048 nextMax N ms underruns U` (new), `stat: heap ... stacks dec`. Confirmed if nextMax > ~46 and ringLow ~0; a small nextMax means SD/NVS stalls. Also: Bluetooth or wired, stutter while idle, same song as MP3.

## Change: instrumentation only (15 lines)
`audio.cpp` ring low-water, `app.ino` `stat: audio`, `storage.cpp` `sd: clock`. No fix: a bigger ring is a heap decision (+8 KB; heap tight at 10,000 tracks, cy115).

## Proposed chunks
A. Ring 4096 frames, or FLAC-only (+8 KB); check heap.
B. `flac_core.c` at -O2 with inlined bit reader (expect 1.5-2x); compare `nextMax`.
C. FLAC read chunk 4096 B; `requestPending()` in resync (C-3).

## Checks
- `check.ps1 -SkipProbe`: exit 0, `PASS: compile-only checks completed; no device commands were run.`
- `check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia`: exit 0, same line.
