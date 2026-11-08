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
