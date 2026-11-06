# PL-TEL: playlist build telemetry

Base: `96e95b9` (v35). Code/test commit: `e5d907c`.

- Extended the existing M3U build line, once after build I/O; added Favourites/Folder lines. Both preview APIs identify as preview; ordinary builds identify as queue. All identity builds stay silent.
- Stack-local telemetry snapshots `audio_isPlaying()` at build start. `millis()` times the existing scan and whole build; callbacks retain their existing visited increments. No added scan, heap allocation, persistent buffer or per-record logging. Counts API unchanged.
- Early M3U open/allocation/staging failures report fail with zero scan/records; short scans report actual visits and fail. Empty favourites report zero visits/scan and ok. Existing synchronous builder has no cancellation path, so cancel is not emitted.
- Diff review and the focused parity/failure test confirm unchanged results and queue/preview failure behavior. No hardware or audio-success claim.

Exact serial formats:

```text
cmd: playlist <name> n=<entries> found=<k> missing=<m> ambiguous=<a> role=<preview|queue> cache=miss scan=<ms> records=<n> total=<ms> status=<ok|fail> playing=<0|1>
cmd: source <favourites|folder> <source-index> role=<preview|queue> scan=<ms> records=<n> total=<ms> status=<ok|fail> playing=<0|1>
```

Checks run once on code/test commit `e5d907c` (this follow-up commit adds evidence only):

- WSL Debian: `sh tests/build_playlist_queue.sh`.
  `PASS telemetry exact fields/roles/status/playing; playlist records equal fixture library; partial scan/empty favourites/All identity`
  `PASS M3U real 25/25 and 175/175; exact/tail/name/order/ambiguity/missing/CRLF/BOM/UTF-8/overlong/truncation; one scan; key allocation <=4000 bytes`
  Existing queue/preview N=1000/2000/10000 and empty/one-track cases PASS. Log: `.tools/logs/pl-tel-host.log`.
- `powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia`
  `PASS: compile-only checks completed; no device commands were run.`
  Log: `.tools/logs/pl-tel-compile.log`; 2 images, 516.21 s.

Static RAM (no extra baseline compile; recorded INT-7 logs):

| W328C image | Baseline | PL-TEL | Delta |
|---|---:|---:|---:|
| player v35 | 95,176 B | 95,160 B | -16 B |
| wired (baseline v33-wired) | 95,184 B | 95,168 B | -16 B |

Each value is the build log's `Global variables use N bytes` line. Baselines: `int7/.tools/logs/codec-pool-v35-jc2432w328c.log` and `codec-pool-v33-wired-jc2432w328c.log`. No open implementation decisions; owner still needs actual SD/audio timing captures.
