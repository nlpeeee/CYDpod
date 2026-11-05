# CY-84 Headphones output switch failure (v20 -> v22)

## Cause
`output_switch` used to activate the destination route (`speaker_setRoute`) in the live process before saving and restarting. With Bluetooth and the decoder still holding heap, wired work-buffer/task allocation could fail, giving "Unable to change output. Try again." even though a fresh boot with the new route is valid (Bluetooth is gated off by the boot plan). Not reproduced physically; shown only by simulated allocation faults.

## Fix
Validate route (pure) -> pause and wait for pending track/seek/startup (5 s bound, no stale identity captured) -> capture exact resume -> park the old consumer (3 s bound, old task/work retained) -> flush preferences and save resume+route (route written last) -> restart paused. Any failure restores the old route and playback intent and leaves the persisted route unchanged. The destination is never started before restart.

## Host evidence (not hardware)
`tests/output_switch_test.cpp` builds the real `output_switch.cpp`, `speaker.cpp` and `settings.cpp` (plain and W328C): BT-to-Headphones under reserve/task-start failure, reverse, same route, invalid/unsupported board, every preference key and the second-write resume keys failing, pending/startup timeout and late settle, park timeout, paused-intent rollback. Reintroducing a preboot `speaker_setRoute` makes it fail. Run via `tests/build_wired_fade.sh` inside `tools/check.ps1`; default and `-Board jc2432w328c` both exit 0 ("PASS: compile-only checks completed").

## v22 artifacts (W328C, owner flashes)
Reserved `.tools/images/codec-pool-v22-jc2432w328c` (normal saved-route) and `-v22-wired-` companion, copied from the unchanged v12-named build output. Each: 7/7 files match manifest size+SHA-256, 4/4 individual regions byte-equal to their offsets in the merged 4 MiB image.

| Metric | v20 | v22 | Delta |
| --- | ---: | ---: | ---: |
| W328C program storage | 1,489,533 | 1,490,381 | +848 |
| W328C static RAM | 92,632 | 92,632 | 0 |
| W328C exported app | 1,489,680 | 1,490,528 | +848 |
| Codec pool | 40,960 | 40,960 | 0 |

Normal app SHA-256 `685a937c9bed9166a39dfb4cbfb0a90358ca1e13aab8cfcb04b287c18d2063c8`; manifest `33ad68a709ddcc80a75406ccecf91ca388c60ca0d272d23d40bb2fa9ac8785c9`. Wired companion app `b7ce95566f6d9b3a1c579760f370c87abd4d93f5b95e6f0fe436bd7a61bddea4`; manifest `4f4aa179cd804ace9e970db209de6ac191713ba4380fb48cd343114f44612c66`.

## Still needs the user's board
Select Headphones from Bluetooth (and back): expect the toast "Restarting...", a reboot paused at the same track/position, then Resume playing through the line/headphone path. Not claimed from host tests.

Owner-only flash (not run): identify the actual W328C first, manual download mode, then as in `cy66-qwerty-search.md` with `$img = '.tools/images/codec-pool-v22-jc2432w328c'` (regions 0x1000 bootloader, 0x8000 partitions, 0xe000 boot_app0, 0x10000 app; DIO 80 MHz 4MB, `--before no-reset --after no-reset`, write-flash then verify-flash).
