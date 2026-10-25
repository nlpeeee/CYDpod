# CY-9b Bluetooth listening test: blocked at pairing

2026-10-25, branch `cydpod/cy-10`, starting HEAD
`e17a8a1c87ceab632e3af8f63f91fd0b0eefe613`. **Session outcome: failed**:
both permitted AirPods pairing attempts timed out, so zero of five tracks
were played. No third attempt, playback, or unmute was sent. COM10 was closed
normally after final `mute`, `stop`, `stats`; final telemetry confirms muted,
empty ring, zero decoded frames and no connection.

The user identified and authorized COM10 for the actual CYD and identified
the intended headphones as AirPods Pro 2. The flashed image identity comes
from [CY-10d](cy10d-smoke-test.md): 1,223,712 bytes, SHA256
`0e0f02ceb782ef9e958dc34f60e9dd0de7724c206320eb3924f9094a9b097aff`.
No flash, erase, esptool, other port, SD write/format, wired output, or
volume above 3 was used in this session. The first received metric showed
the inherited idle level 10, still muted; volume 3 was sent before pairing.
The advertised Bluetooth name, headphone firmware and connection identity
could not be independently verified because no connection occurred.

## Commands and evidence

The checked-in helper defaults to ASCII serial encoding. Scratch copy
`.tools/cy9b-session.ps1` sets `SerialPort.Encoding` to UTF-8 and reads its
command script as UTF-8. The U+2019 name encoded as
`4E-65-69-6C-E2-80-99-73-20-41-69-72-50-6F-64-73-20-50-72-6F`.
The copy also supports an emergency host mute flag; it was never triggered.
No checked-in helper or firmware was edited. Scratch scripts and logs remain
ignored local evidence, outside Git.

Exact executed schedules were dry-run first, with exit 0 and the explicit
`no port opened` output:

```powershell
powershell -NoProfile -File .tools/cy9b-session.ps1 -Port COM10 -Script .tools/cy9b-pair1.txt -LogPath .tools/logs/cy9b-pair1.txt -DurationSeconds 75 -DryRun
powershell -NoProfile -File .tools/cy9b-session.ps1 -Port COM10 -Script .tools/cy9b-pair2-final.txt -LogPath .tools/logs/cy9b-pair2.txt -DurationSeconds 75 -DryRun
powershell -NoProfile -File .tools/cy9b-session.ps1 -Port COM10 -Script .tools/cy9b-playback.txt -LogPath .tools/logs/cy9b-playback.txt -DurationSeconds 380 -DryRun
```

Dry-run output: `.tools/logs/cy9b-{pair1,pair2-final,playback}-dryrun.txt`.
The five-track schedule copies `tools/cyd_probe_session.cy9b.txt`, adding
two final stats commands; **it was never executed**.

Before each real pairing session, Start-Transcript captured the console,
the required action line was printed, and `Start-Sleep -Seconds 20` ran.
The same action prompt was also sent to the coordinator through Orca.
The two console transcripts are `.tools/logs/cy9b-pair{1,2}-console.txt`.

```text
USER ACTION: put the AirPods in pairing mode now
USER ACTION: put the AirPods in pairing mode again
```

Real runner invocations, each exiting 0 (runner completion, not pairing success):

```powershell
& .tools/cy9b-session.ps1 -Port COM10 -Script .tools/cy9b-pair1.txt -LogPath .tools/logs/cy9b-pair1.txt -DurationSeconds 75
& .tools/cy9b-session.ps1 -Port COM10 -Script .tools/cy9b-pair2-final.txt -LogPath .tools/logs/cy9b-pair2.txt -DurationSeconds 75
powershell -NoProfile -File tools/cyd_probe_session.ps1 -SummaryLog .tools/logs/cy9b-pair1.txt
powershell -NoProfile -File tools/cyd_probe_session.ps1 -SummaryLog .tools/logs/cy9b-pair2.txt
```

First schedule: `mute`, `output bt`, `volume 3`, straight-apostrophe `pair`,
`WAIT 60`, `stats`, `mute`, `stop`, `stats`. Second schedule omits `output bt`
to avoid restarting discovery with the old peer name; it sends `mute`,
`volume 3`, U+2019 `pair`, `WAIT 60`, `stats`, `mute`, `stop`, `stats`.
Every command has the helper's default one-second spacing and initial
two-second settling delay. Both pairing windows retained more than 60 seconds
after the pair TX; the firmware itself timed out after approximately 30 seconds.

| Raw timestamped log | SHA256 |
| --- | --- |
| `.tools/logs/cy9b-pair1.txt` | `5317828575a7ef3f6c5f2dd6f7bb96002a476706c8586d2cc47ec8b01bfeecf0` |
| `.tools/logs/cy9b-pair2.txt` | `6ed4d850dfc30c1663ab4d09b546b6ba15282835d21fec4dfe15578189a0f61c` |

## Pairing result and all error/output lines

All times below are UTC. First pair TX was 13:33:37.2426660; second was
13:35:31.7422588. All 34 METRIC samples across both logs report
`connected=0 muted=1 ring=0 decoded=0`. There are no TRACK lines and no
successful connection lines. `OUTPUT ready` establishes initialization only.

```text
2026-10-25T13:33:35.2853571Z RX ERR pair NAME first
2026-10-25T13:33:35.2855472Z RX ERR output initialization
2026-10-25T13:33:37.7575999Z RX OUTPUT ready mode=0 muted=1; switching restarts track
2026-10-25T13:34:07.8630917Z RX ERR Bluetooth timeout; pair again
2026-10-25T13:35:31.7731869Z RX OUTPUT ready mode=0 muted=1; switching restarts track
2026-10-25T13:36:01.8611131Z RX ERR Bluetooth timeout; pair again
```

The initial output errors followed the required `output bt` before the first
peer was configured; subsequent pair initialization succeeded but connection
did not. Logs cannot distinguish a name mismatch, pairing-mode timing, radio
discovery or another interoperability problem. No firmware correction was tried.

## Track matrix and metric snapshots

| Planned exact track under `/Music/` | TRACK / m4a | Playback stats / result |
| --- | --- | --- |
| 07-20 - Daft Punk - Get Lucky.m4a | None | Not attempted: pairing failed |
| 07-21 - Cassius - Cassius 1999.m4a | None | Not attempted: pairing failed |
| 07-22 - Basement Jaxx - Red Alert.m4a | None | Not attempted: pairing failed |
| 07-23 - Basement Jaxx - Romeo.m4a | None | Not attempted: pairing failed |
| 07-24 - The Supermen Lovers - Starlight.m4a | None | Not attempted: pairing failed |

There are no per-track stats snapshots to report. Exact failure-state stats
after final stop in each session are:

| Field (bytes except counters/flags) | Attempt 1, 13:34:41.2755872 | Attempt 2, 13:36:35.7769209 |
| --- | ---: | ---: |
| m4a | na | na |
| heap | 70884 | 70884 |
| min_heap | 56988 | 56984 |
| largest | 57332 | 57332 |
| min_sampled_largest | 53236 | 53236 |
| producer_stack | 5812 | 5812 |
| sink_stack | 1996 | 1996 |
| loop_stack | 6448 | 6448 |
| underrun_frames | 0 | 0 |
| underrun_calls | 0 | 0 |
| decoded | 0 | 0 |
| connected | 0 | 0 |
| muted | 1 | 1 |
| level | 3 | 3 |
| ring | 0 | 0 |
| queue_rejected | 0 | 0 |

The final periodic sample at 13:36:38.1350053 repeats the second column's
values. Port disposal completed normally at session end.

The stock summariser exited 0 for each log; outputs are
`.tools/logs/cy9b-pair{1,2}-summary.txt`. Across both logs: current heap
57,860..144,936; lifetime minimum heap lowest 56,984; largest block
53,236..110,580; lowest sampled largest block 53,236. Lowest stack high-water
marks: producer 5,812, sink 1,996, loop 6,448 bytes, exceeding the 1,024-byte
floor by 4,788 / 972 / 5,424 bytes respectively. Both post-stop states have
heap 70,884 and largest 57,332; lifetime minimum heap decreased by 4 bytes
on the second pairing attempt. No progressive current-heap decline is shown.

## Pass rules and limits

| CY-9 rule | Result for this chunk |
| --- | --- |
| Pair actual AirPods, connected before playback | **Fail:** both names timed out, connected always 0 |
| Five verified originals, audible/clean/speed/stereo/no dropout | **Not tested:** no playback; media metadata/checksums not established here |
| Five starts and four successful transitions, no reset/hang | **Not tested:** zero starts/transitions |
| Natural EOF followed by another play | **Not tested** |
| Unsupported/malformed-file rejection and recovery | **Not tested** |
| Zero steady-state underruns and decoded increasing | **Not tested:** counters stayed zero because decoded stayed zero |
| Zero resets/watchdogs/allocation errors/queue rejections | Observed pass for idle/pairing only; no reset/panic/watchdog/alloc errors, queue_rejected=0 throughout |
| Heap/min_heap >=20,480; stacks >=1,024 | Observed pass for idle/pairing only; minima above |
| Largest block > largest remaining allocation +4,096 | **Unverified:** no full locked remaining-allocation bound established, no playback |
| No progressive post-stop heap/largest decline across five tracks | **Not tested:** equal pairing post-stop states are not five-track evidence |
| Reconnect, timeout recovery, ten reboot reconnects | **Not tested:** no initial connection, two-attempt cap reached |
| One-hour / 100-transition / eight-hour qualification | **Not tested** |
| Wired line/headphone and output switching | **Not tested:** no DAC attached; prohibited in this chunk |

There were no `low_memory` skips, `ls busy`, or resets in either session.
No listing was executed because playback preparation never began. Pairing
heap/largest are below the firmware's M4A admission thresholds (96/64 KiB),
but no play was sent and this is not evidence of an actual admission failure.
Audio quality is judged by the user and is **not established by these logs**;
this run supplied no listening interval.

## Repository check and close-out

`powershell -NoProfile -File tools/check.ps1` exited **0**, full output retained
in `.tools/logs/cy9b-check.txt`, ending:

```text
PASS synthetic fixture matrix: 3 positive, 7 negative; report fixtures/verification.json
PASS: compile-only checks completed; no device commands were run.
```

The compile-only result does not establish hardware playback success.
Only this report is staged for the requested commit; the pre-existing
`docs/research/build-baseline.md` modification is untouched. Scratch files
and raw logs remain ignored, no push was performed, and the board remains
muted and idle with COM10 closed. A new separately authorized chunk is needed
to investigate pairing; no more attempts were made under this dispatch.
