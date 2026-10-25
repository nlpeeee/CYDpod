# CYD device identification (COM10, read-only)

Date: 2026-10-25. Permission: user-scoped, COM10 only, read-only ID commands. No other port opened. No write/erase/flash/eFuse/flash-content operations. Tool: pinned esptool 5.3.0 (`.deps/arduino/packages/esp32/tools/esptool_py/5.3.0/esptool.exe`).
Note: esptool uploads its RAM stub for these commands (nothing written to flash) and hard-resets via RTS afterwards.

## Commands and output

All three exited 0. Commands: `esptool.exe --port COM10 <cmd>`.

### `chip-id`

```
esptool v5.3.0
Serial port COM10:
Connecting....
Detecting chip type... ESP32
Connected to ESP32 on COM10:
Chip type:          ESP32-D0WD-V3 (revision v3.1)
Features:           Wi-Fi, BT, Dual Core + LP Core, 240MHz, Vref calibration in eFuse, Coding Scheme None
Crystal frequency:  40MHz
MAC:                ec:64:c9:89:d9:a4

Uploading stub flasher...
Running stub flasher...
Stub flasher running.

Warning: ESP32 has no chip ID. Reading MAC address instead.
MAC:                ec:64:c9:89:d9:a4

Hard resetting via RTS pin...
exit=0
```

### `flash-id`

```
esptool v5.3.0
Serial port COM10:
Connecting....
Detecting chip type... ESP32
Connected to ESP32 on COM10:
Chip type:          ESP32-D0WD-V3 (revision v3.1)
Features:           Wi-Fi, BT, Dual Core + LP Core, 240MHz, Vref calibration in eFuse, Coding Scheme None
Crystal frequency:  40MHz
MAC:                ec:64:c9:89:d9:a4

Uploading stub flasher...
Running stub flasher...
Stub flasher running.

Flash Memory Information:
=========================
Manufacturer: 5e
Device: 4016
Detected flash size: 4MB
Flash voltage set by a strapping pin: 3.3V

Hard resetting via RTS pin...
exit=0
```

### `read-mac`

```
esptool v5.3.0
Serial port COM10:
Connecting.........
Detecting chip type... ESP32
Connected to ESP32 on COM10:
Chip type:          ESP32-D0WD-V3 (revision v3.1)
Features:           Wi-Fi, BT, Dual Core + LP Core, 240MHz, Vref calibration in eFuse, Coding Scheme None
Crystal frequency:  40MHz
MAC:                ec:64:c9:89:d9:a4

Uploading stub flasher...
Running stub flasher...
Stub flasher running.

MAC:                ec:64:c9:89:d9:a4

Hard resetting via RTS pin...
exit=0
```

## Comparison with CYDpod build profile (`dependencies.lock.json`)

Profile: original ESP32, 4 MB flash, no PSRAM, `esp32:esp32:esp32:PartitionScheme=min_spiffs,PSRAM=disabled,FlashSize=4M`.

| Item | Profile | Device | Verdict |
|---|---|---|---|
| Chip | original ESP32 | ESP32-D0WD-V3 rev v3.1 (dual core, 240 MHz) | match |
| Flash | 4 MB | 4MB (mfr 0x5e, device 0x4016), 3.3 V | match |
| PSRAM | none | no PSRAM in features line | match (ESP32-D0WD has no embedded PSRAM) |
| Partition scheme | min_spiffs | not probed (would need flash read) | n/a, build-time choice; 4 MB fits |

**Verdict: MATCH.** Note: flash manufacturer byte 0x5e is Zbit/XTX-family style ID reported by esptool; size detected from device ID 0x16 = 4 MB.

## Board identity

- Silkscreen `ESP32-2432S028` (common "CYD"), dual USB (micro + USB-C), CH340C bridge (VID_1A86 PID_7523). COM6/COM7 are also CH340, so COM10 is identified only by user confirmation; this ID run confirms COM10 is an ESP32-D0WD-V3 with 4 MB flash, consistent with that board.
- XPT2046 resistive touch, LTK8002D amp with `SPEAK` connector, TF slot, RGB LED: from photo, not probed electrically.
- Chip/flash/no-PSRAM fully consistent with an ESP32-2432S028 and the CYDpod profile.

Still unconfirmed:
- Display panel driver (ST7789 vs ILI9341; vendored config assumes ST7789 with inversion, matching the dual-USB revision) - needs front-of-board check or a display test.
- Whether a microSD card is inserted (no SD access performed).
- Flash partition table / current firmware contents (not read by design).
