# C1: pinned Windows compile baseline

Date: 2026-10-23. Target workspace: `E:\dev\CYDpod`, feature branch
`cydpod/bootstrap`. This work is compile-only: no port was selected, opened,
reset, erased or flashed, and no user device or SD card was touched. No Git
mutations were performed by the C1 worker.

## Source and dependencies

The application is the existing Garionhk player, not a reimplementation.
`vendor/CYD-MP3-Player` preserves all files of upstream snapshot
`404332e93974f4f70392b613c9f3dfb25e1b89b8` (version 1.2.0); no nested Git
metadata is present. Archive SHA256:
`b5e37def64bf1b71d243b8d270908c65343439bec03e5da08656db8f1cd7550a`.
`tools/upstream-files.sha256.json` records every original file hash and the
check rejects missing, changed or additional source files.

| Input | Pin | Local path |
| --- | --- | --- |
| Arduino CLI | 1.5.1; commit `01f3d4f2b`; archive SHA256 `fabe42e0eb04d00e776a66178299ff95a46c623dbc260f997e58fd514853dd40` | `.tools/arduino-cli/arduino-cli.exe` |
| Arduino-ESP32 | 3.3.10; Git tag `0d1440d1be38ab530d274fe87ee88565fe167392`; archive SHA256 `05cbb2cac185265b8b64f1dfa6982e4ae6d57db67f690a291f119a0b2b6aaab2` | `.deps/arduino/packages/esp32/hardware/esp32/3.3.10` |
| ESP-IDF SDK | v5.5.4, `735507283d`; SDK archive 3.3.10 | `.deps/arduino/packages/esp32/tools/esp32-libs/3.3.10` |
| Xtensa compiler | esp-x32 2601; GCC 14.2.0, 20260121 | `.deps/arduino/packages/esp32/tools/esp-x32/2601/bin` |
| ESP32-A2DP | 1.8.11, `b6da4744286ca15b6f1dee607c077a4747294dd4`; archive SHA256 `a8b73ecd3415d01c97540d029c90e8e996a136f44d1fd4dc93341f0335dbdfcf` | `.deps/sketchbook/libraries/ESP32-A2DP` |
| arduino-libhelix | 0.9.4, `6183305de92c6c8e445aa95116319b64963a6ad3`; archive SHA256 `841ea242f732aaa16e47a8d3576b68b9a7055f87aa6c15ec83d89b127c750212` | `.deps/sketchbook/libraries/arduino-libhelix` |
| TFT_eSPI | 2.5.43; official Arduino library archive SHA256 `277a05b523f839e37309fc4de4dfcf0910d4eb0895b7335d10f4940aaf8e17fc` | `.deps/sketchbook/libraries/TFT_eSPI` |

The root `dependencies.lock.json` records download URLs, hashes, source
commits, CLI executable hash, partition CSV hash and TFT/Helix configuration
hashes. Reduced official `tools/package_index.json` and
`tools/package_esp32_index.json` preserve exact Windows artifacts for all
core tools and CLI builtins. These include esp-x32/esp-rv32 2601, GDB
16.3_20250913, OpenOCD v0.12.0-esp32-20251215, esptool 5.3.0, mkspiffs
0.2.3, mklittlefs 4.0.2-db0513a and dfu-util 0.11.0-arduino5. The ESP32
package installs SDKs for other ESP32 families as declared by upstream; they
are not used to claim those boards support Classic Bluetooth.

The local core's `versions.txt` also identifies lib-builder `43a8f6d` and
Arduino component `6cb835025`. Download hashes pin the full artifacts, rather
than relying on those abbreviated provenance strings alone.

## Reproduction commands

From the repository root, on Windows with Python 3.9+ available as `python`:

```powershell
powershell -NoProfile -File tools/setup.ps1
powershell -NoProfile -File tools/check.ps1
```

`setup.ps1` checks the standard existing Arduino/PlatformIO user locations
before downloading. None were present on this machine: `%LOCALAPPDATA%\Arduino15`,
`%USERPROFILE%\Documents\Arduino`, `%USERPROFILE%\.arduino15` and
`%USERPROFILE%\.platformio`. Matching cached archives, if present, are copied
read-only into the local cache and verified. Global installations are never
modified. Python's standard-library downloader was used because Windows TLS
APIs failed under this restricted worker token; no Python package was installed.

Setup verifies pinned download SHA256 values, installs the core from committed
package indexes without `update-index`, and reconstructs isolated libraries
from verified archives. Downloads, CLI/toolchain/core, temporary files, build
caches, staging copies and firmware images remain under ignored `.tools/`
and `.deps/`. The upstream source backup remains in `vendor/`.

The common FQBN is:

```text
esp32:esp32:esp32:PartitionScheme=min_spiffs,PSRAM=disabled,FlashSize=4M
```

Each actual CLI command is recorded in its UTF-8 log. Command shape, with
absolute workspace paths substituted by the script:

```text
arduino-cli --config-file .tools/arduino-cli.yaml compile --fqbn esp32:esp32:esp32:PartitionScheme=min_spiffs,PSRAM=disabled,FlashSize=4M --warnings none --build-path .tools/build/player --output-dir .tools/images/player .tools/staging/player/app
arduino-cli --config-file .tools/arduino-cli.yaml compile --fqbn esp32:esp32:esp32:PartitionScheme=min_spiffs,PSRAM=disabled,FlashSize=4M --warnings none --build-path .tools/build/uploader --output-dir .tools/images/uploader --build-property compiler.cpp.extra_flags=-DCYD_UPLOADER --build-property compiler.c.extra_flags=-DCYD_UPLOADER .tools/staging/uploader/app
arduino-cli --config-file .tools/arduino-cli.yaml compile --fqbn esp32:esp32:esp32:PartitionScheme=min_spiffs,PSRAM=disabled,FlashSize=4M --warnings none --build-path .tools/build/aac-probe --output-dir .tools/images/aac-probe --build-property compiler.cpp.extra_flags=-DCYDPOD_AAC_LC_ONLY --build-property compiler.c.extra_flags=-DCYDPOD_AAC_LC_ONLY .tools/staging/aac-probe/cydpod_probe
```

Separate build directories prevent cross-contamination of the two stock
programs. The core's savehex hooks also export beside the sketch, so checks
compile staging copies under `.tools/`, never the source directories. Initial
validation exposed this behavior; generated exports in the owned vendor tree
were removed before final validation.

Default check requires the AAC probe and runs `python tools/media_check.py`
after all compiles. Host tests require the existing MSVC Build Tools installation
used by `tests/build_host.cmd` and FFmpeg/ffprobe on PATH; these are test-harness
prerequisites, not the firmware toolchain and were not installed by C1.
`-SkipProbe -SkipHostMedia` explicitly requests only the stock compile baseline.
No check path accepts a serial port or invokes upload/monitor/erase commands.

## Configuration and slots

`tools/tft/User_Setup.h` is the pinned upstream ST7789 template, applied only
to the isolated TFT library: 240x320, MOSI13/MISO12/SCLK14/CS15/DC2, reset -1,
HSPI, 40 MHz. This is a provisional compile profile; the user's physical panel
and touch revision have not been identified. It is not board bring-up evidence.

The stock player sees unchanged Helix SBR behavior. The AAC probe alone uses
`CYDPOD_AAC_LC_ONLY`; the documented `ConfigHelix.h` guard suppresses the
upstream SBR define under this flag. Original v0.9.4 otherwise always enables
SBR on non-ESP8266 targets, so a command-line undefine alone is insufficient.
No AudioTools dependency was introduced for stock reproduction.

The exact `min_spiffs` layout is checked against the built partition CSV:

| Region | Offset | Size | Build definition |
| --- | --- | --- | --- |
| NVS | `0x9000` | `0x5000` | shared persistent data |
| otadata | `0xe000` | `0x2000` | core OTA selector |
| player `app0` / `ota_0` | `0x10000` | `0x1e0000` (1,966,080) | default; no `CYD_UPLOADER` |
| uploader `app1` / `ota_1` | `0x1f0000` | `0x1e0000` (1,966,080) | C and C++ `-DCYD_UPLOADER` |
| SPIFFS | `0x3d0000` | `0x20000` | unchanged upstream layout |
| coredump | `0x3f0000` | `0x10000` | unchanged upstream layout |

Each stock binary is checked for upstream's program-specific marker and absence
of the other marker: player `heap before audio + bt`, uploader `upload: hotspot`.
Both images are checked against slot capacity. No combined dual-image release
or flash procedure is supplied. Core-generated `.merged.bin` files contain a
single app at the core's default player address; they are not CYDpod dual-slot
release artifacts and must not be treated as an uploader installation package.

## Evidence and limits

Final command output, sizes and hashes are recorded below after validation.

License inventory and retained texts are in `vendor/NOTICES.md` and
`vendor/notices/`. The shell/AnimatedGIF Apache license does not remove GPL
wrapper and RealNetworks Helix terms, LGPL core terms or SDK/tool notices.
This is a provenance inventory, not an exhaustive distribution clearance.

Compilation proves build reproducibility and slot fit only. It does not prove
target heap/timing, SD playback, AirPods pairing/reconnect, stereo I2S electrical
behavior, headphone amplification, panel/touch compatibility or the required
one-hour hardware tests. Stock player is MP3-specific; the separate probe and
its owner's report cover synthetic ADTS AAC work. User AAC originals and M4A
container support remain unproven; no custom demuxer was added by C1.
