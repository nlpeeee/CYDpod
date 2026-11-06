# FLACX-HOST (v35 / 96e95b9)

Code/tests: `05dc50a`. Host only, no device operations. Independent C99 from
RFC 9639 / PLAN-FONIX; no copied decoder code, heap or mutable static storage.

API (pool integration): `flac_compact_init` now takes a caller-owned 2048-byte input window and walks metadata through a read-at callback (short reads
allowed). `decode_frame` publishes caller-owned interleaved stereo int16 only
on FRAME after CRC-8/16; mono duplicates L/R. `next` preserves the pool's 64 KiB
sync scan, 8 drops/call, 64/stream and damaged-tail EOF. `reset(state, offset,
next_sample)` restarts at a frame boundary, preserving STREAMINFO. `slice`
copies <=1152 frames, trims total and supports overlap.

|4096 profile storage|Bytes|
|---|---:|
|Input window|2048|
|History (32 int32)|128|
|Coefficients (32 int16)|64|
|17-bit side plane|512|
|Reader/frame/CRC counters|88|
|Reserved bookkeeping/seek allowance|528|
|**sizeof(state), caller-owned input excluded**|**1320**|
|Caller PCM block|16384|
|**Combined, compile-time asserted**|**19752**|

19,752 B includes PCM. Replaceable `flac_lpc` validates history, uses int32 iff
`2^(effective_bps-1)*sum(abs(coef)) <= INT32_MAX`, otherwise int64; residual add
stays wide and samples are range-checked. Orders through 32, QLP 1..15, shifts
0..15, Rice/Rice2/escapes, partitions through 8, wasted bits and all stereo modes.

Policy: 16-bit mono/stereo; 8000/11025/12000/16000/22050/24000/32000/44100/
48000 Hz (current firmware). Other depths/rates/channels reject;
blocks >4096 return distinct FLACX_BLOCK_SIZE (`flac_block_size`).

Final WSL `sh tests/build_flacx_host.sh`, log `tests/out/flacx-final.log`:
- PASS matrix 71 exact files (21 repo + 48 added + 2 owners): 68 default4096;
  three 4608 fixtures use the compatibility build, reject under production4096.
  PCM MD5 equals pinned decoder and encoder PCM.
- PASS LPC 32768 vectors x16 shifts; fast=18275, wide=14493.
- PASS ASan/UBSan: malformed/truncated/CRC/geometry/guarded buffers;
  split reads, reset, overlapping slices, bounded resync.
- Empty-first-partition/order32 matches encoder; current decoder emits zero PCM.
  This requested extension permits equality where RFC 9639 section 9.2.7 is strict.
- Host sanity: compact 644.23 vs current 498.03 us/frame; not ESP timing.

W328C command: `tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia`.
Log `.tools/logs/flacx-w328c-final.log`: PASS: compile-only checks completed;
PASS single pool (0000a000) twice. Static RAM unchanged: normal 95160 -> 95160 B,
wired 95168 -> 95168 B. Flash normal 1581945 -> 1581977, wired 1582085 -> 1582117
(+32 B/image vs retained int7-a v35 logs; attribution unproven). Xtensa nm:
compact object has no data/BSS; compact/LPC functions are discarded in both ELFs.

flacx-pool: replace int32 channels in flac_pool.h; supply 16 KiB frame scratch
in the codec union; adapt codec_track.cpp layout, retain the 4608 B slice sink.
Preserve seek/cancel/DIAG/resampling; avoid duplicate input/PCM. Integration
and hardware qualification remain later chunks.
