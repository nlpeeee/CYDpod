# CY-55 command-path fixes

2026-10-27. One owner; branch `cydpod/cmd-fixes` in `E:/dev/.worktrees/CYDpod/cy55-fixes`, based on `cydpod/review-fixes` at `eff499549773e6478aff5ba02bc667d4f97132ea`. All five CY-54 findings were re-verified against that base and confirmed; none was skipped. Compile and existing host suites only; no COM access, flashing, device/SD writes, dependency downloads, vendor edits or PairingSession changes.

## Changes and focused evidence

| Finding | Commit | Verified behavior and change | Existing host coverage |
| --- | --- | --- | --- |
| CY54-01 | `629278c` | Arduino println returns CRLF length; the old count check rejected a successful favourite save. Persist explicit LF with separate exact content/newline counts; shim println now returns the Arduino CRLF count. | Catalogue regression fails before the fix (`cy55-01-red.txt`, exit 1), passes after: add, copy a nonempty favourite, exact LF file bytes, short newline refusal, remove while copying, write-protection and unchanged RAM on refusal; catalogue/UI suites pass. |
| CY54-02 | `4ecee16` | AVRCP Play/Pause/Stop used conditional toggles, so idempotent Pause/Stop retained an old pairing resume claim. Explicit `audio_setPlaying` always increments user intent; internal pairing pause/resume preserves it. | Production audio tests cover idempotent Pause/Stop-equivalent setter calls, idempotent Play, internal transitions, and rejection of a stale resume claim; existing pure pairing tests pass. |
| CY54-03 | `1d1bc9b` | Exhausted open/begin/decode failure closed the file without publishing a reopen requirement. Publish `decodeBlocked` as decoder-needs-reopen, guarded by command generation; Play resets the failure streak and queues recovery. | Production audio regression fails before the fix (`cy55-03-red.txt`, exit 134), passes after: one-song queue, missing file, decoder-open read error and mid-stream read error; restore readability and Play reopens and emits nonzero PCM. |
| CY54-04 | `26206f4` | A newer discovered-device choice left deferred saved B active; timeout also left B pending and ran after deferred dispatch. Clear deferred intent on a replacement selection, cancel or failure; expire timeout before dispatching any deferred switch. | Firmware compile covers production Bluetooth; outer bt_tick/BtAppT selection integration is outside the existing host harness. |
| CY54-05 | this commit (branch tip) | Failed-seek Play could overwrite pending Next/Previous with the old track. A queued/in-flight open clears the previous failure gate and Play recovery refuses to supersede a pending command. | Deterministic producer pause in the existing audio shim holds the mailbox; both Next then Play and Previous then Play retain the track and serial. Regression fails before the fix (`cy55-05-red.txt`, exit 134) and passes after. |

No new validation suite was added. The existing catalogue and production audio tests were extended. Failed-seek fault injection waits for a filled paused ring so it targets seek reads rather than racing the initial decode read.

## Validation

`powershell -NoProfile -File tools/check.ps1` exited **0**, including all four firmware builds, pinned vendor inventories, the single-pool link check, existing pairing/speaker/catalogue/UI suites, codec/FIR/audio suites and synthetic media checks. Full output: `.tools/logs/cy55-check.txt`; per-image hashes: `.tools/logs/build-results.json`.

```text
Sketch uses 1902801 bytes (96%) of program storage space. Maximum is 1966080 bytes.
Global variables use 91208 bytes (27%) of dynamic memory, leaving 236472 bytes for local variables.
PASS single pool: 3ffc49c0 0000a000 b _ZN12_GLOBAL__N_1L9codecPoolE
PASS production audio: ... idempotent intent, pending skip retained on Play, terminal open/begin/decode retry; queue/play intent retained
PASS synthetic fixture matrix: 3 positive, 7 negative; report fixtures/verification.json
PASS: compile-only checks completed; no device commands were run.
```

Focused host runs also exited 0: `cmd.exe /d /c tests\build_catalog.cmd` (`cy55-01-green.txt`), `sh tests/build_codec_pool.sh` (`cy55-02-host.txt`), and `sh tests/build_audio_seek.sh` (`cy55-03-green.txt`, `cy55-05-green.txt`). WSL commands ran in Debian from `/mnt/e/dev/.worktrees/CYDpod/cy55-fixes` as directed by BUILD-CYDPOD. No new suite or device validation was introduced.

The host harness executes the production audio task, decoder, ring and mailbox, but stubs FreeRTOS/SD. It cannot execute AVRCP callback dispatch or production bt_tick/BtAppT integration, so CY54-02 dispatcher wiring and CY54-04 radio scheduling are compile/code-inspection evidence only. Existing pure PairingSession tests do not establish radio callback timing. No hardware behavior is claimed.

## Image

Flashable bundle: `E:/dev/.worktrees/CYDpod/cy55-fixes/.tools/images/codec-pool-v4/`.

- Application: `app.ino.bin`, **1,902,944 B**, SHA256 `8e654e4e010ca97516270b9e9842fee509a0ecf8415e22355c846c7ef8f1c413`; 63,136 B below the 1,966,080 B slot limit. Relative to codec-pool-v3: +416 B binary, unchanged 91,208 B static RAM and unchanged 40,960 B codec pool.
- Contains bootloader, partitions, boot_app0, application, ELF/map, full check log, focused test evidence, build-results JSON and pool-map evidence.
- `MANIFEST.json` pins the final branch tip, each packaged file and application/build-input source hashes; `MANIFEST.txt` records readable sizes, hashes and flash offsets. Every application source file is checked against the compiled staging copy before packaging.
- Nothing was flashed or published. The bundle remains in this worker's checkout; no sibling or main-checkout artifacts were modified.

Installed dependencies are reached through the same read-only `.deps` junction arrangement as CY-53. `.tools`, Arduino configuration, caches, logs and build output are private to this worktree. `vendor/`, `pairing_session.h` and `docs/research/build-baseline.md` were not changed or staged.
