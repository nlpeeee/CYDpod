# FLAC-1 (CY-119 B/C)

Two commits: B decoder optimization, C cooperative resync/channel boundaries. No device operations/push.

- gprof baseline, owner file, -Os/-fno-inline: refill 46.9%, LPC function self 31.3%, Rice residual self 6.3%, frame decode 6.3%, other 9.2% (10 ms sampling; profiling perturbs timing). Optimized reader first; LPC stays int64 because accepted 17-bit side channels, 15-bit coefficients/order 32 and corrupt int32 intermediates do not prove int32 safety.
- Local `#pragma GCC optimize("O2")`; app-owned `flac_bits.h` overlay staged by `tools/stage_flac.py` for firmware/host. Vendor tree untouched; check verifies pinned inventory. Cached reads are inline/32-bit; refill assembles 32-bit words when requested, without CRC-changing prefetch; wide reads retain fallback.
- Xtensa -Os objdump: cached unary zero (>1 cached bits) 57 instructions plus three __lshrdi3 bodies -> 27 instructions/no calls. Actual Arduino object vs baseline with SDK C flags: reachable FLAC text/literals 14,117 -> 28,331 B (+14,214); data/BSS 0 -> 0. This verifies O2/reader code generation, not ESP32 speed.
- Host owner: 1,753 frames/7,177,128 stereo sample frames; mean decode+read 553.22 -> 517.92 us/frame (repeat 541.95 during build: 2-6.4% faster; noisy host only). Full PCM cmp exact, MD5 `ce03594d1410abf5bd5952ec2284a420` before/after. ffmpeg unavailable; pre-change PCM is the permitted reference. BeeMoved 96kHz24bit: REJECT flac_bits both times.
- Existing input is `PoolLayout::input[2048]`; 4096 B reads would enlarge it, so conditional quantum change omitted. No ring/buffer/heap growth; pool 40,960 B unchanged. Static RAM default 93,464 B / JC2432W328C 95,056 B; +0 B (decoder data/BSS and pool layout unchanged). Channel boundaries now taskYIELD; resync checks pending track/seek via audio_commands (same requestPending condition), yields per scan and propagates cancellation without error/EOF. Seek hooks share cancellation. CRC publication unchanged.

Checks/evidence (logs under .tools/logs):
- Before: flac-before.log, existing FLAC fixture/reject/truncation/corruption/seek suite passed.
- Final owner: flac-final-owner.log: PASS owner FLAC PCM byte-exact against baseline.
- `check.ps1 -SkipProbe`: exit 0, flac-final-default.log. PASS FLAC pool: 22 decoded fixtures, 5 clean rejects, 110 seeks. PASS FLAC resync cancellation before read + restart. PASS: compile-only checks completed; no device commands were run.
- `check.ps1 -SkipProbe -SkipHostMedia -Board jc2432w328c`: exit 0, flac-final-jc2432w328c.log. PASS: compile-only checks completed; no device commands were run.

Owner compare same song/output/SD: `stat: audio in 44100 Hz ringLow L/2048 nextMax N ms underruns U` before/after (keep low-water above zero, nextMax below ~46 ms); also sd clock and heap/stacks. Instrumentation untouched. Hardware smoothness remains unmeasured.
