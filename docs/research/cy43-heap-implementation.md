# CY-43 implementation: Classic Touch v5 ready to flash

2026-10-26. Direct user follow-up to the hq CY-43 investigation: preserve the current state, implement the recommended changes, prepare a flash image. **All compile/host checks pass; no flashing, serial access, SD-card access or push was performed.**

Implementation source commit `e5710ed1fb0d63d9c7e75a076dd65569b1a95cec` on `cydpod/cy43-heap`, based on CY-42 `3a4a617`. The build-path prerequisite is `ebe4d5d`. Isolated worktree: `E:/dev/.worktrees/CYDpod/cy43-heap`; the main `E:/dev/CYDpod` tracked source and its pre-existing `docs/research/build-baseline.md` edit are unchanged.

## Saved state

Before editing, saved `.tools/backup/pre-cy43/source.bundle` (complete `cydpod/miniaac` history through CY-42, `git bundle verify` passed), `uncommitted.patch`, the original uncommitted build-baseline document, and the complete `classic-touch-ready-v4` image directory. V4 app SHA256 is `a54a41f94de0b7d0956e5588d25ce444caa918105ff052378ec847176197ee76`. These are source/image rollback artifacts, not a fresh device flash or NVS backup.

## Changes

- `small_file.h` opens regular files with 512-byte stdio buffers before first I/O and reports resize refusal while retaining the usable default. Used for each audio track, source/index/M3U/favourites files and runtime title-strip reads. Directories are not resized. FatFs mount slots remain CY-42's four.
- Decode task stack 6,144 to 4,096 B; full 8,192-byte PCM ring and MiniAAC workspace/output retained. Prior hardware high-water values were free bytes, leaving about 1.9 KiB observed margin with the new budget; new hardware/error-path verification is pending.
- Speaker task, 1,024-byte stereo buffer and 256-byte mono buffer exist only on the speaker route. Output switching parks the consumer, waits for DAC ramp/stop, deletes the writer, then releases storage before BT. Allocation/park failures are reported and the UI persists a new route only after success. DAC startup failure is recoverable on switching back to BT. Stop timeout retains task/buffers for a safe retry.
- Animation read-ahead and scanline buffers (1,024 + 480 B) allocate only while active, with cleanup on allocation failure/stop. Classic Touch continues to stop backgrounds.
- Source previews release when covered by Player/other contexts or an external screen, rebuild on Back, retain navigation/scroll state, and preserve playback on failure. CJK strips release on ASCII/fallback drawing and Classic context exit. Row label capacities are unchanged.
- Decoder starts behind a notification barrier. Queue restoration finishes before initial track open; BT starts after that open completes. A slow open defers BT until completion without blocking the UI indefinitely. `heap: after audio startup ... largest ... ready ...` identifies the barrier.
- Loop/BTC/BTU stacks, SSP and AVRCP controls remain intact. No SDK, vendor codec or ring-size reduction was made.

The check script now resolves the installed `.deps` junction to its canonical path for Arduino CLI while keeping staging/cache/logs/images private to this worktree. The first build attempt could not discover the platform through the junction and attempted discovery-helper bootstrapping; using the existing canonical dependency path resolved it without installing/rebuilding a core. Orca had CYDpod registered as a folder context, so its create command produced no isolated checkout; this implementation uses a separate Git worktree instead.

## Verification

`powershell -NoProfile -File tools/check.ps1` **exit 0**. Full log `.tools/logs/cy43-check.txt`, copied into the v5 image directory. All four firmware images compile, pinned inventories/partition checks pass, catalogue/navigation tests pass with the four-file limit (host peak 3), and the synthetic media matrix passes (3 positive / 7 expected negative). Existing SDK AudioTools-not-included warnings and host conversion warnings are retained in the log; no compile failure remains.

Added/ran tests of the real speaker implementation with mocked RTOS/DAC: BT owns no writer/buffers; 25 route cycles; each buffer-allocation failure; task/DAC failure; a blocked driver write exceeding the park timeout; safe retry/cleanup. Firmware UI tests cover output-failure persistence, preview release, low-memory Back refusal, lazy rebuild, old scroll position and playback queue preservation. These are host checks, not evidence of actual speaker sound, scheduling, SD timing or stack safety on the device.

```text
PASS speaker lifecycle: BT has no allocations, 25 route cycles, both buffer failures, task/DAC failure, blocked-write timeout and safe retry
PASS CY-43: output failure preserves route; preview release, memory refusal, Back rebuild, scroll and queue preservation
PASS synthetic fixture matrix: 3 positive, 7 negative; report fixtures/verification.json
PASS: compile-only checks completed; no device commands were run.
```

## Image and memory evidence

Image set `.tools/images/classic-touch-ready-v5/`: app, bootloader, partitions, boot_app0, ELF, map, check log, build-results JSON and machine/text manifests. The manifest records `e5710ed1fb0d63d9c7e75a076dd65569b1a95cec` and hashes of every application source file; all match the compiled staging copy. Bootloader/partitions/boot_app0 hashes match saved v4 exactly.

| | Saved v4 | v5 |
|---|---:|---:|
| App binary file | 1,369,136 B | 1,371,504 B |
| Static RAM | 78,088 B | 75,336 B |
| `.dram0.data` | 25,030 B | 25,030 B |
| `.dram0.bss` | 53,056 B | 50,304 B |

App SHA256: `8ac3c01e93b1b2a2aa39986eea68c3b6bc5d6e04ba03b64fde8fb14aa85562e0`. Slot limit 1,966,080 B. Xtensa `size -A`/`nm -S -C --size-sort` confirm the old 1,024/256/1,024/480-byte speaker/animation arrays are pointers, while `m4a` remains 26,656 B and Rows/navigation retain 1,416/612 B.

Static recovery is **measured 2,752 B**. Additional open-track stdio recovery **computed 3,584 B**, decode stack **computed 2,048 B**, and BT-route writer stack **computed 3,072 B** give **11,456 B nominal additional recovery** over v4, excluding task-control overhead. This is 32 B below the investigation's 11,488 B gross payload sum, because pointers/new state/alignment remain. Preview/CJK/transient-file savings are conditional and not counted twice.

Using v2's prior 13,400 B steady reference, CY-42's 16,560 + 3,584 + measured static 1,408 and this package's 11,456 yields **46,408 B arithmetic free-heap forecast** at 446 tracks. This is not a successful-stream measurement; callbacks/track changes/UI can lower it. Largest block remains unknown; no 40 KiB / 25 KiB hardware pass is claimed. At 2,000 tracks, queue/order growth alone costs another 6,216 B; the same target is not promised.

## Flash and rollback commands (prepared only)

Run from `E:/dev/.worktrees/CYDpod/cy43-heap`; the script targets COM10 and writes/verifies the four image regions, without erase. Confirm exclusive board access at the later flash step.

```powershell
powershell -NoProfile -File tools/cy11c_flash.ps1 -ImageDir .tools/images/classic-touch-ready-v5 -Tag classicv5 -Base app.ino -BootApp .tools/images/classic-touch-ready-v5/boot_app0.bin
```

Rollback:

```powershell
powershell -NoProfile -File tools/cy11c_flash.ps1 -ImageDir .tools/backup/pre-cy43/classic-touch-ready-v4 -Tag classicv4-rollback -Base app.ino -BootApp .tools/backup/pre-cy43/classic-touch-ready-v4/boot_app0.bin
```

After flashing, capture boot stages and `heap: after audio startup`, then `stat:` while connected/playing/browsing, at track changes, and speaker-to-BT round trips. Check advancing pulls/frames and audible playback, heap/min/largest, decode/loop high-water, no `file: 512 B buffer refused`, no DAC/park/task-allocation failures. Exercise cached CJK titles, folder/favourite/M3U Back navigation and error paths. The 4 KiB decode stack, smaller audio stdio buffer and actual BT fragmentation are the main remaining hardware risks.
