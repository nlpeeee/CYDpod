# AirPods Pro 3: diagnostic build

Branch `cydpod/airpods-diag`, from `main` (v0.5.4). No board was flashed for this note.

## What the owner logs showed (8 November 2026)

Two serial logs streaming to "Nigepods" (AirPods Pro 3), with the Xiaomi Sound Pocket in the same session for comparison. The phone's Bluetooth was off for the second log.

| | Xiaomi | AirPods |
|---|---|---|
| PCM frames taken per 10 s | ~441,000 (real time) | 26,000–300,000 (6–68%) |
| `pullGapMaxUs` | ~27 ms | 0.5–8.7 s (up to 131 s across a reconnect) |
| Free heap while streaming | ~60 KB | 18–23 KB, largest block 16 KB |
| AVRCP volume replies | on time | late and timed out |

- CYDpod's own side was healthy: ring full, `underruns 0`, decoder and EQ idle.
- The heap drop of about 40 KB matches encoded packets backing up inside Bluedroid: the A2DP TX queue (27 packets) and the L2CAP channel. When that queue is full, Bluedroid reads less PCM (the long pull gaps) and drops packets.
- The AirPods themselves suspended the stream 3–8 s after several starts (`ev=AUDIO ... audio=SUSPEND` without a command from CYDpod), and the link then ended. Several reconnects failed at paging.
- `bt: tx power ... max=5` still reads +3 dBm, with the readback wait in place.
- AirPods have never been verified on CYDpod, so this is not known to be a regression from the trimmed Bluedroid.

## The diagnostic build

- `tools/bluedroid/diag/0002-a2dp-src-diagnostics.patch`, applied on top of patch 0001 to a trim-only rebuild (`build-libs.ps1 -Variants trim -ExtraPatches ... -OutSuffix -diag`). It adds counters, but no behaviour change unless `btd rate` is used:
  - `btc_a2dp_source.c`: TX queue drops and maximum length, ticks that read less PCM than due, the bitpool, bitrate, peer bitpool range, MTU and EDR flag at each encoder update, and an SBC bitrate override.
  - `bta_av_aact.c`: L2CAP backlog maximum, stalls, AV-list drops and AVDTP writes.
- The release libraries are unchanged. `tools/check.ps1 -DiagBluetoothLibs` builds against the diagnostic `libbt.a`, which was swapped in only for the compile and then restored (hash `e5288350…` again).
- `firmware/cydpod_player/app/bt_diag.cpp`:
  - logs `bt: link mode active|sniff interval N slots` on every mode change;
  - reads the RSSI delta every 2 s;
  - prints `stat: a2dlink ...` every 10 s while connected;
  - adds the serial commands below.

| Command | Effect |
|---|---|
| `btd` | Print the `stat: a2dlink` line now |
| `btd rate <kbps>` | SBC target bitrate, 100–400 (`0` = stock 328). Applies on the next connection |
| `btd txpwr <min> <max>` | BR/EDR power levels 0–7 (5 = +3, 6 = +6, 7 = +9 dBm), with readback |
| `btd pkt all\|basic\|2dh5\|3dh5\|<hex>` | ACL packet types for the current link (`basic` = no EDR) |
| `btd qos <slots>` | Poll interval for the current link (1 slot = 0.625 ms) |

## Check

`tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia -LargePcmRing -PcmRingFrames 6144 -DiagBluetoothLibs -BuildLabel airpods-diag`: `PASS: compile-only checks completed`, 2 images.
- Bluetooth: 1,303,617 B flash and 79,812 B static RAM.
- Wired: 1,303,757 B and 79,820 B.
- The ELF contains the patch symbols (`cydpod_av_writes`, `cydpod_a2d_src_txq_drops`, `cydpod_a2d_src_rate_kbps`).
- Boot files are identical to v0.5.4. Log: `.tools/logs/airpods-diag-check.log`.

## Board results

Pending.
