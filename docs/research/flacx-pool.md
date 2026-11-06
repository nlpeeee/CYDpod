# FLACX-POOL

Base `ee07f8a`; no-ff host merge `54baa19`; checked code `f13dc29`.
Compact init/next/slice; no heap, new task or mutable static decoder storage. MP3/AAC/WAV, FIR and the 128-frame BT pull path are unchanged.

|Pool storage (ESP32)|Bytes|
|---|---:|
|Codec union: max(MP3 23856, AAC 22576, FLAC 17760)|23856|
|Shared stereo PCM slice (1152 frames)|4608|
|Shared input window|2048|
|FIR|272|
|Other fields/alignment|40|
|**Exact pool (sizeof + nm assertions)**|**30824**|

FLAC member: state 1320 (history128 + coefficients64 + side512 + book616),
interleaved block16384, wrapper/seek56. Input is caller-owned, excluded from
state; state+input+block remains 19752. MP3 now dominates, so further shrinking
FLAC cannot reduce this pool. Previous pool40960; **gain10136 B**.

API changes: init takes the shared 2048-byte window; metadata records SEEKTABLE;
set_cooperate checks pending commands/yields at refill/channel/recovery;
header_at probes CRC-8 using 16 stack bytes without overwriting the sync scan;
resync exposes the existing bounded/cancellable scan. Seek retains table/bisect/scan and resets with
`flac_compact_reset(offset,next_sample)`. CRC gates publication; tail/error
policy and 16-bit mono/stereo rates remain. Blocks >4096 use the existing reject
path and print exactly `flac: reject block=<n> max=4096`; 24-bit still rejects.
DIAG unchanged: SD timed in readAt, whole next timed, nonIo=next-SD.

Rollback: `CYDPOD_FLAC_LEGACY` (`tools/check.ps1 -FlacLegacy`) selects the old
core/reader and 40960-byte pool. Default staging excludes legacy source/header; nm checks its absence.

Checks on code commit, logs `.tools/logs/flacx-pool-{full,w328c}.log`:
- Full `tools/check.ps1 -SkipProbe`: exit0, every suite PASS.
- PASS FLAC pool: 22 decoded fixtures, 5 clean rejects, 110 seeks; ASan/UBSan.
  Includes 19 original supported positives +3 production-cap variants; the
  three historical 4608 fixtures now reject. Added compact seek/input/slice and
  STREAMINFO/frame4608 rejects; corrupt/truncated resync and cancel pass.
- PASS matrix 71 exact files, LPC32768 vectors x16 shifts, split input,
  restart, CRC gate and ASan/UBSan. Three historical4608 cases use host-only
  compatibility; production accepts68. Encoder oracle gap remains documented.
- PASS both owner originals read in place: full production-pool PCM equals
  legacy PCM (`cmp`); generated positives also equal encoder PCM.
- W328C: exit0; PASS single pool (00007868) twice; PASS: compile-only checks completed

W328C sizes vs retained `ee07f8a` logs (normal / wired):
RAM: 95464 -> 85328 / 95472 -> 85336 B (-10136 each). Flash: 1585685 -> 1562049 (-23636) / 1585809 -> 1562189 (-23620) B.
Host time: compact634.00 vs legacy511.67 us/frame; **no speedup claim**.
Owner must capture BT/wired `stat: flac` nextMaxUs/nonIoMaxUs (also SD/ring),
`stat: heap` minimum/largest, stack and underruns on this build. Device speed,
heap floor and playback acceptance remain unmeasured; no device operations.
