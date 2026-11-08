# Trimmed Bluedroid: build and results

Date: 2026-11-08.
- **Branch:** `cydpod/bluedroid-trim`, from `cydpod/volume-sync`.
- **Commits:** tools and spec `f4fd7dd`; firmware source unchanged since `291c62d` (0.5.2 plus the robust volume sync).
- **Spec:** [trimmed-bluedroid.md](../specs/trimmed-bluedroid.md).

## Build

1. **Sources:**
   - ESP-IDF v5.5.4 at `73550728` (`735507283d` in `esp32-libs/3.3.10/versions.txt`), cloned with all submodules into `.tools/esp-idf-5.5.4`;
   - compiler: the project's pinned crosstool-NG `esp-14.2.0_20260121`, the exact version that IDF v5.5.4's `tools.json` expects;
   - cmake and ninja from Visual Studio Build Tools 18;
   - Python 3.12 venv with IDF's `requirements.core.txt`.
2. **Two builds in one directory** (`tools/bluedroid/build-libs.ps1`): a stock build from Espressif's shipped `sdkconfig`, and a trim build from the same file plus `defconfig.cydpod-bt`.
   - The stock rebuild's `sdkconfig` matches Espressif's on every ESP-IDF key: 1,919 keys, 0 value differences. The 547 keys it lacks belong to components that aren't in ESP-IDF (Arduino, mDNS, LittleFS, Modbus, Insights and others). Their libraries stay Espressif's.
   - Building in one directory matters. In two directories, all 91 libraries differ because the build path is embedded in the debug information.
3. **Compare and patch** (`tools/bluedroid/make-libs.py`):
   - 91 libraries are identical, or differ only in `__DATE__`/`__TIME__` (`libesp_app_format`, `libesp_bootloader_format`); these stay Espressif's binaries;
   - 3 are replaced: `libbt.a` (Bluedroid), `libesp_hid.a` (BLE HID host) and `libprotocomm.a` (BLE provisioning transport);
   - 50 shipped libraries aren't built by ESP-IDF and are kept;
   - the linker scripts are identical.
   - **`sdkconfig.h` (dio_qspi and qio_qspi) and `sdkconfig`:** 101 Bluetooth defines removed (BLE, GATT, SMP, BLE Mesh, BluFi, HFP, BLE scan/adv, dual-mode), 2 added (`BTDM_CTRL_MODE_BR_EDR_ONLY`), 3 changed (`BT_BTC_TASK_STACK_SIZE` and `BT_BTU_TASK_STACK_SIZE` 8192 → 4096, `BTDM_CTRL_BLE_MAX_CONN_EFF` 3 → 0). The full list is in `.tools/logs/bluedroid-make-libs.json`.
   - `versions.txt` gains a `cydpod-bt:` line with the overlay's SHA-256 (`e3e955e9…39b5`).
4. **Switching:** `tools/bluedroid/use-libs.ps1 trim|stock` swaps the Arduino core's `esp32-libs/3.3.10`; stock is kept as `3.3.10.stock`.
   - The first trim build failed in `esp_ble_mesh/core/bluedroid_host/adapter.c`. Espressif's package enables BLE Mesh, which needs the BLE host, so the overlay also sets `CONFIG_BLE_MESH=n`.

## Firmware

`tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia -LargePcmRing -BuildLabel bluedroid-trim` PASS with the trimmed package (log `.tools/logs/bluedroid-trim-check.log`). No unresolved references.

| | Stock libs (volume-sync build) | Trimmed |
|---|---|---|
| Bluetooth sketch | 1,603,241 B | 1,296,885 B (−306,356) |
| Bluetooth static RAM | 88,440 B | 79,636 B (−8,804) |
| Wired sketch | 1,603,369 B | 1,297,013 B |
| Wired static RAM | 88,448 B | 79,644 B (−8,804) |

Static RAM falls by 8.8 KB: the BLE, mesh and GATT buffers are no longer linked. More heap should be freed when the stack starts: 8 KB from the BTC/BTU stacks, plus the BLE control blocks Bluedroid allocates at `esp_bluedroid_init`. Only the board can measure that.

The firmware's own source is unchanged since `291c62d`, whose volume, PCM and diagnostics host suites passed. Those suites don't use the ESP-IDF libraries, so they weren't run again. **None of this is hardware evidence.**

## Board checks (owner)

Package: `releases/candidates/bluedroid-trim`. Compare against the boot log of the volume-sync build (`bt: started … heap 33848`, `heap: after init 30040 min 25992 largest 21492`).
1. **Boot:** `bt: started … heap N` should be roughly 20 KB or more above 33,848.
2. **JBL:** connect and stream for 5 minutes. Check:
   - `stat: heap … min … largest` (target: largest at least 12 KB);
   - `stat: vol link=1 avrc=1`;
   - `pullGapMaxUs` under 100 ms;
   - `underruns 0`.
3. **Speaker switching:** switch to the Xiaomi, then back to the JBL, without a power-off. Both should connect, with `bt: remote control up`.
4. **Pairing:** pair a speaker from scratch (Bluetooth menu, Pair). This exercises the 4 KB BTC/BTU stacks in discovery and pairing; check `stat: tasks BTC_TASK=… BTU_TASK=…` still shows free stack.
5. **Sleep:** Sleep and wake (`pwr sleepnow`), and `pwr btcycle 5`.
6. **Wired:** if a DAC is fitted, the wired build plays.

## Board results (owner, 2026-11-08, candidate `cd6f5245`)

| | Volume-sync build (stock libs) | Trimmed |
|---|---|---|
| `bt: started … heap` | 33,848 | 83,896 (+50 KB) |
| Largest free block while streaming | 4–6 KB | 34–45 KB |
| Minimum free heap | about 5 KB | 46,676 (23,712 once, reconnecting to the JBL after Sleep) |
| `stat: vol link=1 avrc=1` | sometimes missing | every connection (JBL, Xiaomi, re-paired Xiaomi, after Sleep) |
| JBL → Xiaomi → JBL without power-off | JBL refused | both connect |

- Steady playback: `pullGapMaxUs` about 27 ms, `underruns 0`. Gaps above 100 ms only span a stream start, a speaker switch or a reconnect.
- Lowest free stack: `BTC_TASK` 2,464 B and `BTU_TASK` 2,544 B of 4,096, including a fresh pairing.
- Pairing (forget, pair), Sleep and wake (stack stopped, restarted, reconnected) pass.
- Found: after a fresh pairing the Xiaomi answered CYDpod's 25% set with 62%, while its next button press reported 31%. The owner chose to adopt the speaker's own level on connect instead; that needs patch 0001 (see the spec) and is a new candidate to check.

## Candidate 3: larger PCM ring (owner choice, 2026-11-08)

- Owner logs showed a gap when one track ended and the next began with the screen off: `ringLow 0/4096`, `underruns 10`. The 4,096-frame ring (16 KiB) holds about 93 ms. Opening the next file, plus the 20 ms loop cadence while the screen is off, can take longer than that.
- The owner chose a bigger buffer: 6,144 frames (24 KiB, about 139 ms), built with `tools/check.ps1 ... -LargePcmRing -PcmRingFrames 6144`. The ring indices now wrap without a power-of-two mask.
- It costs 8 KiB of heap, allocated at boot. On candidate 1 the lowest free heap was 46.7 KB while streaming and 23.7 KB once while reconnecting after Sleep. Expect about 8 KB less on both.
- Screen-wake dropouts are separate. CYDpod-side counters stayed clean through every wake (`missingFrames=0`, `underruns 0`, pull gap about 27 ms), so the cause is probably the radio link, and a bigger ring may not help them.
- Compile-only PASS (log `.tools/logs/bluedroid-trim3-check.log`): Bluetooth 1,297,537 B / 79,652 B static, wired 1,297,681 B / 79,660 B. The ring's index arithmetic was checked on the host for 4,096, 6,144 and 6,000 frames. Not yet run on the board.

## Candidate 3 on the board, and candidate 4 (2026-11-08)

Owner log (JBL Flip 3, candidate 3):
- Volume adoption works: on each connect `reports` counts up, `sends=0` and `reportWaits=0`, and the screen took the speaker's 38% and 25%. The `adopted` log line was missing, because the report arrived before the capability reply. Candidate 4 prints it in that case too.
- The ring held: `underruns 0` throughout, lowest fill 4,736/6,144.
- The JBL dropouts are radio-side. In steady playback the stack stopped pulling audio for 896 ms, then 207 ms (the second window includes a screen wake). Free heap fell by about 22 KB (`heap 38944 … largest 17396`) while Bluedroid queued packets it could not send. CYDpod's own counters stayed clean.
- The +9 dBm transmit power has never applied. `bt: tx power rc=0 … max=5` means +3 dBm, ESP-IDF's default. `esp_bredr_tx_power_set` only takes effect between controller enable and profile start, and CYDpod called it at STACK_UP, after the profiles. Candidate 4 calls it from a `bt_start()` override, right after the controller is enabled, on boot and on the Sleep restart.
- Check on the board: `bt: tx power … max=7`.
