# CY-10c integrated verification and final probe identity

Verified 2026-10-25 on `cydpod/cy-10`, implementation tip
`c2aa3718a229cbd76a74a60f507aa951af78eb2a` (CY-10a and CY-10c1 through
CY-10c3). This is compile and host evidence only. No hardware, serial port,
SD card, flashing or esptool access occurred.

## Review disposition

Review IDs refer to `E:\dev\hq\docs\research\CY-7-review.md`.

| Item | Fix commit | Change and verification | Remaining scope |
| --- | --- | --- | --- |
| R3: ring reset races consumer | `9ddd1995ec324a1b4536e0a5457029750c9e7f5d` | Demux/queue error calls `stopConsumers()` before ring reset; source diff inspected and probe compile passed. Decoder-error cleanup also quiesces consumers. | Host reader suites do not execute ESP32 consumer concurrency; exercise error transitions on hardware. |
| R4: listings starve cancellation | `9ddd1995ec324a1b4536e0a5457029750c9e7f5d` | At most one pending/running listing, cancellation slot reserved; a full queue supersedes its oldest command for stop/play/output/pair. Source diff inspected and probe compile passed. | Run the documented listing/stop and burst-admission checks in the separately authorised hardware session. |
| R6: status publication data race | `9ddd1995ec324a1b4536e0a5457029750c9e7f5d` | Status is an atomic pointer to static text, loaded once per telemetry line; source diff inspected and probe compile passed with format diagnostics. | Concurrent target telemetry remains untested here. |
| R7: AudioTools inventory omitted | `0a5825b5f68670f0c3303eddedb475926a771389` | Full `tools/check.ps1` passed the exact file-set and SHA256 check for all 518 AudioTools files before compiling. Prior changed/missing/extra scratch-copy rejection evidence under both PowerShell versions is recorded in `cy6-firmware-wiring.md`. | No open inventory gap identified; mutation tests were not repeated in this integrated run. |
| R8: real-media harness cannot fail on mismatches | `c2aa3718a229cbd76a74a60f507aa951af78eb2a` | Committed `tests/real_media_expected.tsv` pins identities, statuses and frame counts; harness fails mismatches and missing/unexpected inputs. `build_m4a_real.sh` passed all 25 manifest entries: accepted 23, rejected 2, mismatches 0. | Original media remain local inputs; an unavailable folder is SKIP (77), never PASS. This run had the media and exited 0. |
| R2: interim low-memory admission guard only | `9ddd1995ec324a1b4536e0a5457029750c9e7f5d` | Before M4A reader creation, require internal free heap >=96 KiB and largest block >=64 KiB; otherwise skip with `low_memory`. Source diff inspected and probe compile passed. | **R2 remains open:** subsequent vendor allocations can still fail without recovery. Full fallible allocation/status propagation and fault injection are CY-10b1 through CY-10b3, planned after the AirPods test. The guard does not prove allocation safety. |

R1/R5 were fixed separately in CY-10a (`21da57c`); their regression suite also
passes below. These results do not establish actual AirPods Pro 2 playback,
on-target memory/stack headroom, reconnect, stereo line/headphone output, or
the one-hour/extended qualification gates. Follow `cy9-test-plan.md` for the
separately authorised hardware procedure and retain its remaining gates.

## Commands and retained output

All requested verification commands exited **0**. Raw outputs are ignored local
artifacts under `.tools/logs/`; the durable verdicts are recorded here.

| Command | Output under `.tools/logs/` | Result |
| --- | --- | --- |
| `powershell -NoProfile -File tools/check.ps1` | `cy10c-check.txt`, `player.log`, `uploader.log`, `aac-probe.log`, `build-results.json` | Player/uploader/probe compile, pinned inventories, AAC allocation checks and host media pass; synthetic fixture matrix 3 positive, 7 negative. |
| `wsl.exe -d Debian -- sh tests/build_m4a_host.sh` | `cy10c-host-host.txt` | M4A and ADTS each 131 frames, 134144 samples/channel, 73337 ADTS bytes; PCM identical and counts equal. |
| `wsl.exe -d Debian -- sh tests/build_m4a_caps.sh` | `cy10c-host-caps.txt` | All malformed cases return errors within caps. |
| `wsl.exe -d Debian -- sh tests/build_m4a_reject.sh` | `cy10c-host-reject.txt` | All 105 runs pass. |
| `wsl.exe -d Debian -- sh tests/build_m4a_sequence.sh` | `cy10c-host-sequence.txt` | Every track equals its fresh-reader result; reader deletion returns live heap to 0 bytes. |
| `wsl.exe -d Debian -- sh tests/build_m4a_r1r5.sh` | `cy10c-host-r1r5.txt` | R1/R5: 0 failures; native 32-bit multilib unavailable, explicit uint32_t truncation assertion used. |
| `wsl.exe -d Debian -- sh tests/build_m4a_real.sh` | `cy10c-host-real.txt` | PASS accepted=23 rejected=2 expected=25 mismatches=0. |

Both Windows PowerShell **5.1.26100.9711** (`powershell`) and PowerShell
**7.6.6** (`pwsh`) ran these commands with `-NoProfile -File`, each exiting 0:

```text
tools/cyd_probe_session.ps1 -Port COM10 -Script tools/cyd_probe_session.sample.txt -LogPath .tools/tmp/cy10c-<shell>-dryrun.txt -DryRun
tools/cyd_probe_session.ps1 -SummaryLog tests/fixtures-log/probe-session.txt
```

Here `<shell>` is `powershell` or `pwsh`. Outputs are
`cy10c-<shell>-dryrun.txt`, `cy10c-<shell>-summary.txt`, and
`cy10c-<shell>-version.txt` under `.tools/logs/`. Both dry-runs printed five
plays and the final stop at 362 seconds; neither session LogPath was created.
Both summaries match the fabricated fixture: heap 60000..80000, largest
32000..50000, min_heap 54000..70000; underrun frames 0..12 and calls 0..2,
both first/last 0; statuses na=2, ok=2, moov_after_mdat=1, and two ERR lines.
This reset-containing fixture is parser evidence, not playback evidence.

Initial sandbox access denials for Git/WSL/Orca were resolved by approved
execution outside the sandbox. A shell-wrapped WSL batch lost variable
arguments; each suite was then invoked directly as listed above. Neither
setup issue was a product test failure; no implementation correction was made.

## Final image identity and resource gate

Image lengths and hashes below come from `.tools/logs/build-results.json`;
static RAM comes from the compiler output in `.tools/logs/aac-probe.log`
(the JSON has no static-RAM field). The actual final `.ino.bin` was independently
hashed and matches the JSON.

| Probe | Image bytes | Static RAM bytes | SHA256 |
| --- | ---: | ---: | --- |
| CY-9c | 1,222,880 | 61,528 | `480441396eb43594b6a0a25760a081b81080cd3c803a11a172de961b7037a79b` |
| CY-10a | 1,223,088 | 61,528 | `1a40d486f1a15d3a0d59217d64f1db16e6a710a30dd924a33312d624cbe7aa0d` |
| Final CY-10c | 1,223,712 | 61,536 | `0e0f02ceb782ef9e958dc34f60e9dd0de7724c206320eb3924f9094a9b097aff` |

Final image: `.tools/images/aac-probe/cydpod_probe.ino.bin`. Growth is
**832 bytes versus CY-9c**, **624 bytes versus CY-10a**, and **8 bytes static
RAM versus both**. It fits the 1,966,080-byte slot with **742,368 bytes spare**;
static RAM growth is below 2 KiB. Compiler program storage is 1,223,566 bytes;
the table reports the exported binary length, including image overhead.
Player remains 1,336,576 bytes / `c5a251f78e5bff1fd48f627590c3e6f2cec06f3c32842a8cc4315019a7a416ec`;
uploader remains 1,147,600 bytes / `81fff8a3d4f949f3d8ea0ae4688ce38fd9e3223c0628574c389319c752571bb9`.

`git diff --check` passed for the assigned documentation. The pre-existing
`docs/research/build-baseline.md` modification was left untouched and excluded
from the commit; no implementation or vendor files changed in this chunk.
