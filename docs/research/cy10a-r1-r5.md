# CY-10a: gate codec callbacks and bound 64-bit box sizes

2026-10-25, based on `cydpod/cy-9c` (`d0aa57f`). Host and compile-only evidence; no hardware, serial, SD card, flashing, downloads or installs. Vendor and firmware sources are unchanged. The pre-existing `build-baseline.md` edit is excluded from this commit.

## Changes and status codes

The streaming guard renames `alac`, `mp4a`, `esds` and `ac-3` headers outside the validated `stsd` payload to `free`, keeping their size and skipping their payload. The guard consumes `stsd` in a separate mode and validates its entry and nested ESDS positions/sizes before forwarding the whole payload; those validated callbacks remain enabled. No new R1 rejection status is needed.

Finite declared box sizes above `UINT32_MAX` return `boxTooLarge` (`box_too_large`) before reaching the vendor parser. This intentionally bounds the complete box, rather than just its payload, identically on ESP32 and 64-bit hosts. A finite box end that would wrap `uint64_t` returns `offsetOverflow` (`offset_overflow`), checked before the width bound. Incoming-write and held-header offset additions also check against `UINT64_MAX` before addition. Existing top-level open-ended `mdat` remains supported.

## Tests first: before fix

Added `tests/m4a_review_r1r5_test.cpp` and `tests/build_m4a_r1r5.sh` before changing the reader. They reuse the caps harness's builders and allocation wrappers, run each input in a forked child with three write sizes (7, 1024, whole input), and verify exact status, frame count, every frame's ADTS LC/44.1-kHz/stereo fields, peak live heap <=49,152 B and largest allocation <=32,768 B. Child termination/missing results count as failure. The timeout prevents an unsafe vendor callback from hanging the suite.

Command: `wsl.exe -d Debian -- sh tests/build_m4a_r1r5.sh`. Before-fix output (selected lines from `tests/out/r1r5-before.log`, ignored build evidence):

```text
FAIL R1 nested ALAC allocation slice=7 status=no_sample_table frames=0 LC/44.1/stereo=1 peak=74968 maxReq=65524
FAIL R1 nested ALAC allocation slice=1024 status=no_sample_table frames=0 LC/44.1/stereo=1 peak=74968 maxReq=65524
FAIL R1 nested ALAC allocation slice=48 status=no_sample_table frames=0 LC/44.1/stereo=1 peak=74968 maxReq=65524
FAIL R1 late ESDS override slice=1024 status=ok frames=4 LC/44.1/stereo=0 peak=9704 maxReq=3672
FAIL R1 late ESDS override slice=624 status=ok frames=4 LC/44.1/stereo=0 peak=9704 maxReq=3672
FAIL R5 payload 4 GiB slice=1024 status=ok frames=1 LC/44.1/stereo=1 peak=9704 maxReq=3672
FAIL R5 size UINT32_MAX+1 slice=1024 status=ok frames=0 LC/44.1/stereo=1 peak=9704 maxReq=3672
FAIL R5 end overflow slice=1024 status=ok frames=0 LC/44.1/stereo=1 peak=9704 maxReq=3672
FAIL R1/R5: 21 failures
32-bit multilib unavailable; explicit uint32_t truncation assertion used (see tests/out/m4a_r1r5/multilib.log)
```

Late ESDS at slice 7 and malformed standalone ESDS also terminated without a child result; the harness prints `SIZE_MAX` crash-marker allocation values for those cases, not measured allocations. Additional ALAC-name cases violated the ADTS configuration check. Positive controls passed before the fix. The 4-GiB payload was represented by its 16-byte header plus one frame, without allocating/copying a 4-GiB buffer.

## After fix

Same command exited 0; all 11 cases x 3 write sizes passed. Selected output:

```text
PASS R1 nested ALAC allocation slice=7 status=no_sample_table frames=0 LC/44.1/stereo=1 peak=9704 maxReq=3672
PASS R1 nested ALAC allocation slice=1024 status=no_sample_table frames=0 LC/44.1/stereo=1 peak=9704 maxReq=3672
PASS R1 nested ALAC allocation slice=48 status=no_sample_table frames=0 LC/44.1/stereo=1 peak=9704 maxReq=3672
PASS R1 late ESDS override slice=7 status=ok frames=4 LC/44.1/stereo=1 peak=9704 maxReq=3672
PASS R1 late ESDS override slice=1024 status=ok frames=4 LC/44.1/stereo=1 peak=9704 maxReq=3672
PASS R1 late ESDS override slice=624 status=ok frames=4 LC/44.1/stereo=1 peak=9704 maxReq=3672
PASS R5 payload 4 GiB slice=1024 status=box_too_large frames=0 LC/44.1/stereo=1 peak=9160 maxReq=3672
PASS R5 size UINT32_MAX+1 slice=1024 status=box_too_large frames=0 LC/44.1/stereo=1 peak=9160 maxReq=3672
PASS R5 end overflow slice=1024 status=offset_overflow frames=0 LC/44.1/stereo=1 peak=9160 maxReq=3672
PASS R5 largest target size slice=1024 status=ok frames=1 LC/44.1/stereo=1 peak=9704 maxReq=3672
PASS positive slice=1024 status=ok frames=4 LC/44.1/stereo=1 peak=9704 maxReq=3672
PASS R1/R5: 0 failures
32-bit multilib unavailable; explicit uint32_t truncation assertion used (see tests/out/m4a_r1r5/multilib.log)
```

All four stray callback names are also tested before and after the sample description, including inside a retained `moov` container. Maximum measured live heap in this suite is 9,704 B; maximum request is 3,672 B. A declared total size of `UINT32_MAX` remains accepted (one supplied frame), and `UINT32_MAX+1` is rejected. The explicit assertion `uint32_t(0x100000000ull) == 0` documents target truncation. The optional `g++ -m32` probe failed because `bits/c++config.h` is missing; no 32-bit execution is claimed and nothing was installed.

## Existing checks

All four commands exited 0: `wsl.exe -d Debian -- sh tests/build_m4a_host.sh`, `build_m4a_caps.sh`, `build_m4a_reject.sh`, and `build_m4a_sequence.sh` (the latter three under `tests/` as well). Output:

```text
m4a  frames=131 samples_per_channel=134144 adts_bytes=73337
aac  frames=131 samples_per_channel=134144 adts_bytes=73337
pcm bytes IDENTICAL
PASS counts equal
PASS all malformed cases returned errors within caps
PASS all 105 runs
PASS every track equals its fresh-reader result
delete reader:                live 0 B (heap returns to baseline)
```

Read-only real-media check: `sh tests/build_m4a_caps.sh "/mnt/e/Alternative Wedding - UK Nostalgia/Tracks/07 - Dance and Indie Crossover/"*.m4a` in WSL Debian, exited 0. `tests/m4a_caps_real.cpp` accepted 23/25, rejected only Galvanize and Loaded as `too_many_samples`, and printed `rejected 2 of 25`; no originals were copied or modified. Per-file output is retained in ignored `tests/out/r1r5-real.log`.

`powershell -File tools/check.ps1` exited 0:

```text
Sketch uses 1222934 bytes (62%) of program storage space. Maximum is 1966080 bytes.
Global variables use 61528 bytes (18%) of dynamic memory, leaving 266152 bytes for local variables. Maximum is 327680 bytes.
PASS synthetic fixture matrix: 3 positive, 7 negative; report fixtures/verification.json
PASS: compile-only checks completed; no device commands were run.
```

`aac-probe` application binary: **1,223,088 bytes**, SHA256 **`1a40d486f1a15d3a0d59217d64f1db16e6a710a30dd924a33312d624cbe7aa0d`**, as recorded by `.tools/logs/build-results.json`; `.tools/logs/aac-probe.log` retains compiler output. Binary length includes image overhead beyond the sketch-use number. No generated `aac-prototype.md` delta remained after the check. `git diff --check` passed. Remaining review items and hardware acceptance are outside this chunk.
