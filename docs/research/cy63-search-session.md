# CY-63: one Search query session with 150 ms edit debounce

Branch `cydpod/search-session`, worktree `E:\dev\.worktrees\CYDpod\cy63-search-session`, exact base `cydpod/ui-retire` `b9ccd5f42654a140c040f4cf69564aea03781d99` (CY-81 final code `b5d4729` plus its report). Code/tests commit: `feeab656f58f54dd73d1e0abaa43f1d2afef3db8`. Ownership is limited to `classic_screen.cpp`, the existing UI harness and host SD counters, and this report. CY-81 retained Settings, migration, Home/chrome/mini-player/Player behaviour, and integrated CY-64 Search folding are preserved; its documented follow-ups remain separate.

## Session behaviour

Edits update the existing 32-byte text buffer and repaint the existing query field immediately. They schedule only the newest text for 150 ms after the input timestamp, using the existing tick and signed deadline subtraction across millis rollover. Production timestamps edits with `millis()` because input precedes tick and a prior loop can have stalled; the host uses its existing fake tick clock. No new task, timer, sleep, input-handler file open, per-track payload or persistent allocation was introduced.

Done makes pending work due for the next tick, including an unchanged error retry, so it bypasses the 150 ms idle wait without opening storage in the touch handler. Search entry, keyboard entry, Home Search, redraw and unchanged Done no longer unconditionally start a query. The one fixed session distinguishes Empty, Active (both busy and completed), Cancelled and Error. Exact unchanged applied text retains cursor, count, IDs and the existing open scan through Keyboard/Search navigation; edits reverted before application also retain it. Empty Delete/Clear do not repaint or reschedule, and empty Done does not open or scan storage. Clearing actual applied text cancels it once through tick.

Leaving both Search pages still cancels the backend query; reentry queues a restart while the backend retains its partial index construction. A newer pending edit keeps its 150 ms deadline across leave/reentry. Redraw never retries an error; Edit then Done retries even unchanged text, and a real edit also retries after settling. Backend start, tick, storage format, matching, ranking, 50-ID cap and partial-build implementation are unchanged.

While an edit is pending, obsolete backend hits stay private: effective UI count is zero, scanning pauses, the existing status region shows `...`/`Searching...`, and result activation requires a current applied session and a row track matching that result ID. Starting a new query clears backend hits through the existing API before further scanning. Queue data is untouched until explicit valid selection; the test compares every committed queue entry and the selected track.

## Geometry and drawing

There are no rectangle or hit-target changes, so no golden update/geometry commit is required. `classic_layout.h` and `tests/golden/classic_layout.txt` are unchanged. The existing geometry/golden and navigation checks still cover portrait and both landscape orientations (plus the flipped portrait).

| Region | 240 x 320 portrait, before = after | 320 x 240 landscape (rotations 1/3), before = after |
| --- | --- | --- |
| Header | (0, 0, 240, 36) | (0, 0, 320, 36) |
| Query strip | (0, 36, 240, 32) | (0, 36, 320, 32) |
| Letter keys | 6 columns, 40 x 40, y=76 | 8 columns, 40 x 40, y=76, existing A-P/Q-Z pages |
| Done hit target | (160, 276, 80, 44) | (160, 196, 80, 44), existing fixed action-row hit map |
| Search result rows | (0, 36+40i, 210, 40), 6 visible | (0, 36+40i, 290, 40), 4 visible |

Dirty-row scheduling remains one row per tick, with the existing audio-buffer condition and 120 ms fallback untouched. Pending editing adds no result-label reads or new periodic full-screen repaint. Query/status painting uses its existing bounded strip and progress updates; identical pending status is not repainted on each tick. This reduces redundant scans/opens by reasoning, but makes no measured `stat: buf`, physical latency or drop-free playback claim.

## Host acceptance

The existing `tests/classic_ui_test.cpp` fake-clock harness drives real press/touch hooks and explicit ticks without its usual multi-second pump hiding debounce boundaries. `tests/catalog_shim/SD.h` adds host-only counters for open attempts, read/write calls and actual regular-file closes; production storage APIs are unchanged.

Acceptance covers immediate echo without I/O; rapid `song` edits with no work at 149 ms and one final start at 150 ms; wrapped deadlines; Done applying on the next tick before idle; unchanged busy/completed navigation retaining cursor/count/IDs without reopening; reverted edits; empty edits/navigation doing zero storage work; leaving Search and Keyboard; off-page inactivity; 40% cold-index cancellation resuming at 80%; preserving a pending deadline across reentry; injected read failure, no redraw retry and unchanged Done recovery; private obsolete hits; no queue changes before selection; and exact full intended queue on valid selection. The catalogue suite still covers the existing 1,000-track case and Search folding, and all previous transport, touch, Settings/migration/navigation and render regressions remain in the required default check.

The host failure during test development was a fixture expectation: `x` matches `Fixture Artist`, so the no-match query is `z`. Cancellation progress is validated by no off-page I/O and subsequent 40% -> 80% build continuation, because the backend's public percentage switches from build cursor to scan cursor on cancellation. Neither required a storage implementation change.

## Hardware and remaining work

No device, serial, port, flash or user-media operation was performed. Compile/host lifecycle evidence is not physical Search responsiveness or audio qualification. The existing loop still performs synchronous index open/read/write/finalization and result-label I/O; CY-65 owns audio-aware admission and later owner-run latency/underrun/heap/stack qualification. Required AAC-LC 44,100 Hz stereo, actual AirPods Pro 2 and stereo headphone/line acceptance remain open hardware requirements.

Owner visual checklist: on Home > Search, type a burst and confirm each released character appears immediately with pending status; pause typing and observe the final query scan; press Done during a burst and see the newest results; switch Edit/Back/Done without changing text and confirm progress/results survive; leave Search during preparation and return; retry after an SD error; tap a valid hit and confirm the intended track/queue. Repeat in portrait and both landscapes while preserving the existing Home, mini-player, Player and Settings navigation. Hardware audio-health/latency measurements are still required.

## Linked RAM and flash

Baseline is the independently accepted CY-81 v18 table in `docs/research/cy81-retire-legacy.md`; current values are the final checks' compiler/linker output.

| Target / metric | v18 | CY-63 v19 | Delta |
| --- | ---: | ---: | ---: |
| Default program storage | 1,451,997 B | 1,452,649 B | +652 B |
| Default static RAM | 91,016 B | 91,056 B | +40 B |
| W328C program storage | 1,489,105 B | 1,489,825 B | +720 B |
| W328C static RAM | 92,592 B | 92,632 B | +40 B |
| W328C exported app image | 1,489,248 B | 1,489,968 B | +720 B |
| Codec pool (both) | 40,960 B | 40,960 B | 0 B |

`xtensa-esp32-elf-nm -S --defined-only` reports `searchSession` as `00000028 b` (40 B) in both ELFs, below the <=48 B session budget and <100 B chunk budget. The 32-byte applied query, 4-byte deadline, 1-byte lifecycle state and pending flag plus alignment account for the entire +40 B linked static delta. The existing edited query buffer is reused. No new persistent heap, per-track state or task stack exists; this is source/linker evidence, not measured runtime heap/stack qualification. Symbols are retained in `.tools/cy63-default-linked-symbols.txt` and `.tools/cy63-w328c-linked-symbols.txt`. The default app image is 1,452,800 B, SHA-256 `b074b763656775bcad7f790e19d8bdd15ca393952ca8831846b477cb805221ee`.

## Reserved v19 artifact and independent verification

Normal saved-route image: `E:\dev\.worktrees\CYDpod\cy63-search-session\.tools\images\codec-pool-v19-jc2432w328c`. The unchanged check script emits `codec-pool-v12-jc2432w328c`; its seven files and manifest were copied to the reserved v19 directory, then every actual file was independently measured and hashed against `MANIFEST.json` (7/7 PASS, retained `.tools/cy63-v19-verified.json`). The v19 directory did not exist and was not overwritten. Build ELF/log retain the script's v12 name under this worktree; nothing in another worktree was changed.

| File | Offset when individually flashed | Actual bytes | Verified SHA-256 |
| --- | --- | ---: | --- |
| `app.ino.bin` | 0x10000 | 1,489,968 | `4694f8376f299f292b812097f18db4151e00626bb10e8bede89961ee5887dcbc` |
| `app.ino.bootloader.bin` | 0x1000 | 25,024 | `f508dfe30f34c2490ec08caaa96f20dc2853f66a0a92f6fb759b205e82924f29` |
| `app.ino.elf` | not an individual region | 15,612,448 | `d6843189aa88356183d34ed6169639546a079e6b792095036c697a61a63c0adf` |
| `app.ino.map` | not an individual region | 19,559,741 | `a27b6df2ef8cf923d24520f40c5cbbb396d8c1a3072d5fb1513f67b462926bc9` |
| `app.ino.merged.bin` | not an individual region | 4,194,304 | `8f7eba5116fe014d3fda0feb714ba62ba84ef3825b1a43a90570af82586f78c3` |
| `app.ino.partitions.bin` | 0x8000 | 3,072 | `0a8b5720e7b77ff11f1462458c3a509dee79224e5279898f26d6a2e3ae0517b7` |
| `boot_app0.bin` | 0xe000 | 8,192 | `f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0` |

The four individual flash regions are bootloader 0x1000, partitions 0x8000, boot_app0 0xe000 and app 0x10000. ELF/map are debugging artifacts and the merged 4 MiB file is not an extra region. Bootloader/partitions/boot_app0 hashes match v18. Build artifacts, caches and downloads remain ignored and uncommitted.

**Owner-only flash entry, not executed by this worker.** CY-81 records that the board's factory backup exists. Identify the actual board/port before use; the saved W328C entry uses COM7. Enter manual download mode (hold BOOT, tap RESET, release BOOT), use `--before no-reset --after no-reset`, and write/verify only the four regions above (dio, 80m, 4MB, 460800 baud). Press RESET afterwards. Restore image: `codec-pool-v8-jc2432w328c` under the CY-71 worktree, with those same four offsets. No erase, NVS write or user-media operation is part of this entry.

The forced-Headphones companion was also copied unchanged to `.tools/images/codec-pool-v19-wired-jc2432w328c` and independently verified 7/7 against its own manifest. It is a separate forced-route image; the normal saved-route v19 image above is the primary deliverable.

## Final checks and retained evidence

The following required checks ran **sequentially**, default first, on the final code/tests in `feeab65`, both exit 0. The only subsequent tracked change is this report. Existing compiler/shim warnings are retained in the full logs; both finish with the PASS line below.

| Command | Result / retained log | SHA-256 of complete log |
| --- | --- | --- |
| `powershell -NoProfile -File tools/check.ps1` | exit 0; `.tools/check-default.txt` | `c256bb2b6914db08b13b480501e78e002bc6d0241c5acb394ec1fd6d2303a935` |
| `powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia` | exit 0; `.tools/check-jc.txt` | `7c3804ea2af83361c20a3b5cead132e4b6664f765ea1a08857897059cb81e907` |

Selected final default-check output (both normal and wired UI invocations include the CY-63 PASS):

```text
RENDER_INVENTORY_OK: 148 SVG renders, rotations 0/1/2/3, see render/inventory.tsv
PASS CY-63 Search session: immediate echo, 150 ms burst/rollover, tick-only Done, retained busy/completed cursor/count/no opens, empty no-I/O, cancel/resume, error retry, obsolete-hit and full queue isolation
NAV_GRAPH rotation 0: 17 pages, 18 page/scroll states
NAV_GRAPH rotation 1: 17 pages, 20 page/scroll states
NAV_GRAPH rotation 2: 17 pages, 18 page/scroll states
NAV_GRAPH rotation 3: 17 pages, 21 page/scroll states
HEADPHONES_UI_OK: zero bt_* calls across Bluetooth page, Classic and the Settings tree
RENDER_INVENTORY_OK: 160 SVG renders, rotations 0/1/2/3, see render/inventory.tsv
PASS CY-63 Search session: immediate echo, 150 ms burst/rollover, tick-only Done, retained busy/completed cursor/count/no opens, empty no-I/O, cancel/resume, error retry, obsolete-hit and full queue isolation
NAV_GRAPH rotation 0: 17 pages, 18 page/scroll states
NAV_GRAPH rotation 1: 17 pages, 20 page/scroll states
NAV_GRAPH rotation 2: 17 pages, 18 page/scroll states
NAV_GRAPH rotation 3: 17 pages, 21 page/scroll states
PASS: compile-only checks completed; no device commands were run.
```

Final W328C normal player compiler/linker evidence:

```text
Sketch uses 1489825 bytes (75%) of program storage space. Maximum is 1966080 bytes.
Global variables use 92632 bytes (28%) of dynamic memory, leaving 235048 bytes for local variables. Maximum is 327680 bytes.
PASS single pool: 3ffc4a40 0000a000 b _ZN12_GLOBAL__N_1L9codecPoolE
PASS: compile-only checks completed; no device commands were run.
```

The earlier dedicated `cmd.exe /d /c tests\build_catalog.cmd` also exited 0 (`.tools/cy63-host.txt`); the required final default check reran the same catalogue/UI suites after the final pending-reentry regression. Existing host codec, I2S tone/diagnostic, wired fade and Settings migration suites were exercised by the full default check. Layout/golden files remain unchanged. The final source matched the staged default source by SHA-256 `9ac43803c20fb421c2a8545fdd03bb3d8f2dc54975e306042806e2415d1efbd3` before the W328C build; neither code nor tests changed afterward.
