# Hardware and media intake

Date: 2026-10-23. Read-only host discovery; no device was opened, reset or flashed.

User-reported board marking: `ESP-32-2432SO28`, interpreted provisionally as ESP32-2432S028 family. User-required wireless sink: AirPods Pro 2. Required media: AAC-LC, 44,100 Hz, stereo. Container and bitrate remain unconfirmed.

Observed Windows serial inventory:

```text
COM8
USB Serial Port (COM8)
FTDIBUS\VID_0403+PID_6001+BG03TYFIA\0000
```

An FTDI serial device can be connected to many kinds of equipment. This inventory does not establish that it is a CYD or that flashing is appropriate. Identify the target through the user and then matched diagnostics before serial access.

Installed FFmpeg/ffprobe are available. Observed version:

```text
ffprobe version 8.1.1-essentials_build-www.gyan.dev
```

No representative user tracks were provided or searched for. Synthetic media can establish limited decoder behaviour; actual originals and device playback remain acceptance inputs.

Bring-up intake records should later add exact module/PCB/panel/touch identity, flash/PSRAM diagnostics, backed-up firmware details, SD card filesystem, actual DAC/amp board and headphones, confirmed GPIO ownership, and photographs/measurements where useful. Never infer the whole board profile solely from its retail family name or USB connector count.
