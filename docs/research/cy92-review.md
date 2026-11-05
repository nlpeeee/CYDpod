# CY-92 wired Headphone limit review

Target: `cydpod/headphone-limit` 4257b23; base: `cydpod/theme-selector` 557f2b4.
Read-only implementation review against CY-92; no firmware changes.

## Findings

**No findings (0).** No correctness or specification gaps identified in the reviewed changes.

## Reviewed behavior

- `settings.h:48`, `settings.cpp:28,121,148`: absent, wrong-type/size and invalid nonzero bytes select On; valid 0 selects Off. The pinned Preferences implementation stores bool as U8. The dedicated setter commits only `hpLimit` before publishing to Settings; failure leaves the displayed/applied preference unchanged and reports an error. It cannot overwrite `colorTheme` or the persisted route, including forced-wired builds.
- `classic_screen.cpp:277,309,633`: the additional row exists only with wired output. It uses the existing tolerant-release/cancellation path, returns before route switching, and publishes only after a successful save. Configuration while Bluetooth is selected does not alter Bluetooth PCM/AVRCP, route or restart behavior.
- `wired.cpp:73-85,172,229,252,294`: all mailbox accesses use the same ESP32 cross-core portMUX critical section. The writer snapshots once per chunk; requests coalesce. This is a chunk-level lock, not a lock-free path; CY-92 prohibits new locks per sample. No lock or allocation was added inside sample processing, and no decode-task reader of `headphoneLimit` was introduced.
- `wired.cpp:172,195,245-261`: stream startup resets to the saved ceiling before PCM; reserve publishes the loaded preference. Only the writer mutates the active Fade. The UI mailbox carries a value, not a Work pointer. Mid-slew route teardown retains the current gain/target during the existing held-sample drain; a subsequent stream resets to the latest preference. Existing park/delete/free ownership is preserved.
- `wired_fade.h:56-59,93-99,143-149,158-166`: On=4125 Q15 and Off=16423 Q15 match rounded -18/-6 dBFS. One Q15 unit/frame crosses 12,298 units in 278.866 ms at 44.1 kHz, after mailbox observation and before existing DMA latency. Reversal continues from current gain; a downward slew reaches the lower ceiling after settling. L/R use identical gain and the existing multiply. Full-open constant full-scale PCM changes at most one output LSB/frame from the ceiling slew; signal/ramp changes retain their existing bounds. Maximum magnitude product plus rounding is bounded by 538,165,248, within int32. Zero remains zero and first nonzero startup samples respect the selected ceiling.

## Check

Run once on unchanged reviewed code (no sanitizers or firmware builds):

```text
wsl.exe -d Debian -- sh -c 'cd /mnt/e/dev/.worktrees/CYDpod/cy92-review && mkdir -p .tools/logs tests/out/wired && g++ -O2 -std=c++17 -Wall -Wextra -Werror tests/wired_fade_test.cpp -o tests/out/wired/wired_fade_test && tests/out/wired/wired_fade_test > .tools/logs/cy92-review-fade.log 2>&1'
HOST_CHECK_EXIT=0
PASS CY-92 ceiling: Q15 On=4125 Off=16423, 12298 frames, 1 Q15/frame, startup, PCM, both legs and reversal; Fade=680 Work=1704 (+4 B each)
WIRED_FADE_OK
```

Log: `.tools/logs/cy92-review-fade.log` (ignored).
UI/persistence evidence was inspected, not rerun. Hardware loudness, pops and playback stability remain owner validation; host checks do not establish hardware success. No open implementation decisions.
