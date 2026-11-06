# DIAG-FLAC

Base: 57c4649 (verified DIAG-CPU); code: 00ff2d3, branch cydpod/diag-flac.

FLAC readAt wrappers time the unchanged reads (including metadata/seek and
2048-byte fills); the codec magic read is charged only when it identifies FLAC.
next() retains its existing millisecond maximum and adds microsecond timing.
frames counts whole FLAC frames completed by next(), not its 1152-frame slices;
ringStartMin samples before those decodes. With no decode its printed value is 0.
nonIoMaxUs subtracts that call's SD sum using a lifetime producer total, so a
concurrent window reset cannot corrupt the subtraction. It includes compute,
preemption, yields and diagnostic overhead: NOT pure CPU time.

Window copies/resets and 64-bit totals use stateLock; identity persists.
Pull records all audio_pull callers (BT or selected wired writer), latching the
first caller name/core/priority once. burstFrames counts requested frames with
gaps <1000 us and segments at reporting boundaries; gap history spans windows.
missingFrames excludes intentional pause/seek silence. lockMaxUs is max(wait,hold).

Costs: 2 esp_timer_get_time calls/read; 3/pull, one inside the PCM critical
section. lockWaitUs includes entry timestamp overhead; lockHoldUs is an upper
bound including unlock and possible preemption before its outside end timestamp.
Counter publication uses a second short stateLock section; no new heap/tasks.
Hardware timer cost is unmeasured.

Serial additions (10 s; exact order):
```
stat: flac frames=<n> nextMaxUs=<n> sdSumUs=<n> sdMaxUs=<n> nonIoMaxUs=<n> readsGt10=<n> ringStartMin=<n> block=<n>
stat: pull task=<name> core=<n> prio=<n> gapMaxUs=<n> burstFrames=<n> lockWaitUs=<n> lockHoldUs=<n> missingFrames=<n>
stat: sched layout=A sdMaxUs=<n> nextMaxUs=<n> pullGapMaxUs=<n> lockMaxUs=<n>
```
Existing stat: audio line unchanged; diag: sd read max now uses FLAC SD-only
maximum and ends with ` (sd only)`.

Checks, retained in .tools/logs/diag-flac-{window,pool,owner,check}.log:
- WSL `sh tests/build_audio_diagnostics.sh`:
  PASS: audio diagnostic window (atomic copy/reset, 64-bit totals, cadence, missing frames)
- WSL `CYDPOD_TESTSAMPLES=/mnt/e/dev/CYDpod/testsamples sh tests/build_codec_pool.sh`:
  PASS FLAC pool: 22 decoded fixtures, 5 clean rejects, 110 seeks
- WSL `sh tests/flac_fast_smoke.sh` (owner read in place; FLAC-1 cached baseline):
  PASS owner FLAC PCM byte-exact against baseline
  MD5 ce03594d1410abf5bd5952ec2284a420 (7,177,128 samples/channel).
- `powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia`:
  PASS: compile-only checks completed; no device commands were run.
- Static RAM (`Global variables use`): player 95344 -> 95464 B; wired
  95352 -> 95472 B (+120 B each, <200 B); baseline diag-cpu/.tools/logs.
  Both images: PASS single pool (40960 B).

Diff review: same PCM operations, ring, reads and yields; forbidden files untouched.
No hardware timing, Bluetooth/AirPods or wired playback acceptance is claimed.
