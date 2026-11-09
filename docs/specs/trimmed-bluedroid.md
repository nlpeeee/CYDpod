# Trimmed Bluedroid for CYDpod

Status: released in 0.5.3 (2026-11-08). Owner decision: start the memory work by trimming Bluedroid.

## Why

CYDpod's Bluetooth is ESP32-A2DP 1.8.12, a wrapper, running on Bluedroid, the ESP-IDF Bluetooth host stack. Bluedroid comes precompiled in Arduino-ESP32 3.3.10's `esp32-libs`. Starting it costs about 81 KB of the roughly 115 KB left after audio start-up.

Owner logs tie Bluetooth faults to low heap:
- the AVRCP volume link was missing when the largest free block was 4–6 KB;
- A2DP sending stalled for up to 0.87 s;
- the JBL was refused after a Xiaomi session.

The stock build carries features CYDpod never uses (`esp32-libs/3.3.10/sdkconfig`):
- a dual-mode controller;
- the BLE host (GATTS/GATTC/SMP/BLE 4.2/BluFi, with dynamic control-block memory);
- HFP client and AG;
- 8 KB BTC and BTU task stacks, of which the board's `stat: tasks` shows about 6.5 KB never used.

## Change

Only the Bluetooth options change, through `tools/bluedroid/defconfig.cydpod-bt`:

| Option | Stock | Trimmed |
|---|---|---|
| Controller mode | BTDM | `BTDM_CTRL_MODE_BR_EDR_ONLY` |
| `BT_BLE_ENABLED`, `BT_BLE_BLUFI_ENABLE`, `BT_BLE_42_FEATURES_SUPPORTED` | y | n |
| `BT_HFP_ENABLE` | y | n |
| `BT_BTC_TASK_STACK_SIZE`, `BT_BTU_TASK_STACK_SIZE` | 8192 | 4096 |
| `BT_SPP_ENABLED` | y | y (ESP32-A2DP's unused sink calls `esp_spp_init`) |

One source patch, `tools/bluedroid/patches/0001-avrc-ct-forward-volume-interim.patch`, applied to the trim build only: AVRCP's INTERIM reply to a volume-change registration carries the speaker's current absolute volume, the only way a source can read it, and stock Bluedroid ignores it. The patch forwards it as `ESP_AVRC_CT_CHANGE_NOTIFY_EVT` with bit 7 of the volume set (volumes are 0–127), so CYDpod can adopt it without re-registering (see [bt-volume-feedback.md](bt-volume-feedback.md)). No public header changes; firmware built on stock libraries just never sees it.

Unchanged: ESP32-A2DP, codecs, PCM ring, core assignments, and every non-Bluetooth option.

## Method

`tools/bluedroid/build-libs.ps1` builds ESP-IDF v5.5.4 (`735507283d`, the commit recorded in `esp32-libs/3.3.10/versions.txt`) twice, with the same pinned compiler (crosstool-NG esp-14.2.0_20260121) that ESP-IDF expects:
- **stock:** Espressif's shipped `sdkconfig` exactly;
- **trim:** the same file with the overlay applied.

Both builds pin their version strings (`version.txt` in the project and the ESP-IDF tree), so git state never shows up as a difference. The libraries, generated linker scripts and `sdkconfig.h` files that differ between the two builds are the complete set of artefacts the Bluetooth options affect. `tools/bluedroid/make-libs.py` copies Espressif's 3.3.10 package and replaces only those artefacts with the trimmed builds. Everything else stays Espressif's original binary.

The patched package is kept beside the stock one (`esp32-libs/3.3.10.stock`).

Status 0.5.3: accepted on the owner's board and made the default. `dependencies.lock.json` pins the overlay (`bluedroidOverlay`), the 0.5.3 build kit applies it, and `tools/check.ps1` refuses stock libraries unless `-StockBluetoothLibs` is passed.

## Pass criteria

- The sdkconfig diff contains only Bluetooth keys and the options derived from them.
- Bluetooth and wired images compile; the host suites pass.
- **Board:**
  - `bt: started` free heap at least 20 KB above 0.5.2;
  - while JBL streaming: largest free block at least 12 KB, `stat: vol link=1 avrc=1` on every connection, `pullGapMaxUs` under 100 ms in steady playback, `underruns 0`;
  - the JBL connects after a Xiaomi session without a power-off;
  - pairing, reconnect, Sleep/wake and the wired build still work.

## Risks

- Disabled features may leave unresolved references; this is checked at link time.
- The 4 KB task stacks are sized from playback. Pairing and discovery must be checked on the board too.
- This is a locally rebuilt Bluedroid. Its provenance (commits, overlay, artefact hashes) is recorded in `docs/research/bluedroid-trim.md`.
