# BR/EDR transmit power: why the readback stayed at +3 dBm

Branch `cydpod/bluedroid-trim`. Source: ESP-IDF v5.5.4 (`.tools/esp-idf-5.5.4`), closed controller `components/bt/controller/lib_esp32/esp32/libbtdm_app.a` disassembled with the Arduino core's `xtensa-esp32-elf-objdump` (esp-x32 2601). No board was flashed for this note.

## Conclusion

The `esp_bredr_tx_power_get` readback was wrong because it ran too early, not because the set failed. `esp_bredr_tx_power_set` is asynchronous: it queues a message to the controller task and returns `ESP_OK` as soon as the message is queued. The levels change only when the controller task (core 0) handles the message. CYDpod read the levels back on the next line, so it always saw the old `4..5` (0..+3 dBm). It then set `txApplied`, so nothing ever read them again.

## Evidence (disassembly of `rf_espressif.o`, `arch_main.o`, `task.o`)

- `esp_bredr_tx_power_set` (`controller/esp32/bt.c:2013`) just calls `bredr_txpwr_set` and maps 0 → `ESP_OK`, -1 → `ESP_ERR_INVALID_ARG`, anything else → `ESP_ERR_INVALID_STATE`.
- `bredr_txpwr_set(min,max)`: rejects `min|max >= 8` or `max < min` (-1). Calls `sdk_config_get_bt_mode()` and returns -2 unless the Classic bit is set. Otherwise it puts `{min,max}` in a 2-byte buffer and returns `btdm_task_post(11, buf, 8, 1)`.
- `btdm_task_post`: allocates a copy, sends it to the controller task queue with `portMAX_DELAY`, and returns 0 once it is queued (-3 if the send fails). It does not wait for the message to be handled.
- `btdm_controller_task` (`arch_main.o`), on message 11: `bredr_txpwr_set_inter(buf[0], buf[1])`.
- `bredr_txpwr_set_inter`: stores `max` in `rwip_rf+44` (the RF driver's `txpwr_max`) and in `bredr_max_tx_pwr_id`, stores `min` in `bredr_min_tx_pwr_id`, and prints `Set BREDR TX POWER min %d max %d` when `g_bt_plf_log_level >= 3`.
- `bredr_txpwr_get`: plain loads of `bredr_min_tx_pwr_id` / `bredr_max_tx_pwr_id` (`.data` initial values 4 and 5). So the readback is reliable once the controller task has run. It just isn't synchronised with the set.

## The other questions

- **Is power adaptive?** Yes, within `[min, max]`. The RF hooks the controller installs in `r_rf_rw_init` include `rf_rw_txpwr_inc`, which steps a link's power index up to `bredr_max_tx_pwr_id`, and `rf_rw_txpwr_dec`, which steps it down to `bredr_min_tx_pwr_id`. Its `rf_rw_txpwr_max_set` sets a link straight to `bredr_max_tx_pwr_id`. These run for LMP power-control requests from the peer. So a speaker that asks for less power can take the link down to `min` (0 dBm with CYDpod's settings). The readback only ever shows the configured range, never a link's current level. To pin the level, `min` would have to equal `max`. That is a separate decision for the owner, and this change does not make it.
- **Does `btStartMode` start RF?** No. Arduino's `btStartMode` (`esp32-hal-bt.c`) only runs `esp_bt_controller_init` + `esp_bt_controller_enable`. RF activity starts with Bluedroid's page/inquiry/scan, after `bt_start()` returns. Calling from the `bt_start()` override is the right place, and the queued message is handled before Bluedroid starts.
- **sdkconfig / PHY cap?** No BR/EDR transmit-power option exists in `controller/esp32/Kconfig.in`. The trim build has `CONFIG_ESP_PHY_MAX_TX_POWER=20` (20 dBm), well above +9 dBm. `CONFIG_ESP_PHY_REDUCE_TX_POWER=y` relates to brownout and the PHY init data. Nothing here caps the controller's power index at +3. (This was not measured on hardware.)
- **Must it be called before `esp_bt_controller_enable`?** No. The set is a message to the controller task, so that task must exist and be running. The header documents the order "after `esp_bt_controller_enable`, before RF use", and the `bt_start()` override already follows it. The disassembly didn't settle what happens before enable, and CYDpod doesn't call it there. The globals stay in `.data`, so they survive the Sleep controller restart. `r_rf_rw_init` copies `bredr_max_tx_pwr_id` into `rwip_rf.txpwr_max` again at each init.
- **Changing the lever while connected:** the globals change for new links and for later power-control steps. A link that is already up keeps its current index until the peer asks for a change.

## Change

`applyTxPower()` (`firmware/cydpod_player/app/bt.cpp`): after a successful set, re-read up to 20 times with `delay(1)` until `max` equals the requested level, then log. This adds no new set path. It only stops the log reporting the stale value.

Board check: `bt: tx power rc=0 level min=4 max=6 (default 4..5, level 6 = +6 dBm)` with the lever on. If it still shows `max=5` after this change, the controller task did not handle message 11 within 20 ms. That would need its own investigation.

## Check

Run in worktree `cydpod/tx-power` (based on `main` at v0.5.3, `7ee579d`, where the trim libraries are the default), with Windows PowerShell 5.1: `tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia -LargePcmRing -PcmRingFrames 6144`. Log: `.tools/logs/txpwr-check.log`.

- `PASS: compile-only checks completed; no device commands were run.` 2 images built (player 1,297,673 B flash and 79,652 B globals; wired 1,297,817 B and 79,660 B).
- This is compile-only evidence. The board readback and any range or dropout effect still need the owner's test.
