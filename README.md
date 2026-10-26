# CYDpod

A CYD music player project for the original ESP32-2432S028 family, built around existing firmware and libraries. Required media is **AAC-LC, 44,100 Hz, stereo**. Required outputs are **AirPods Pro 2 over Bluetooth** and **external stereo DAC line/headphone output over I2S**.

The maintained player now implements a portrait classic iPod-inspired touch UI
with Artist/Album/Song browsing from a cached SD catalogue. Build and host UI
tests are available; actual AirPods, external stereo output, device memory/touch
performance and battery qualification remain pending. See the
[implementation evidence and limits](docs/research/classic-touch-ui-implementation.md).

## Build on Windows

Python and Windows PowerShell are required. Setup downloads pinned dependencies into this checkout; it does not change a global Arduino installation or connect to a board.

```powershell
powershell -File tools/setup.ps1
powershell -File tools/check.ps1
```

The check compiles the stock player/uploader, maintained CYDpod player and AAC probe, and runs catalogue, firmware UI and media host checks. It never uploads, erases, opens a serial monitor or chooses a COM port. Build logs and images are generated under ignored `.tools/`; dependencies are under ignored `.deps/`. Exact versions, hashes, the provisional board profile and app-slot sizes are in [dependencies.lock.json](dependencies.lock.json).

Host verification additionally requires FFmpeg/ffprobe and installed Visual Studio C++ Build Tools:

```powershell
python tools/media_check.py
```

This compiles/runs the shared ADTS reader and pinned LC-only Helix decoder against the synthetic fixtures, checks decoded stereo tones/reference output, and exercises rejection and buffer/parser behaviour. It does not access a device or validate AirPods playback. `python tools/media_check.py --generate` regenerates the synthetic fixtures with the installed FFmpeg version.

## Audio approach

```text
SD AAC file -> existing AAC decoder -> bounded 44.1-kHz stereo PCM buffer
                                      |                         |
                              A2DP source/SBC                 I2S DAC
                                      |                      /       \
                               AirPods Pro 2             line out   headphone amp
```

Helix handles AAC decoding. An M4A container needs a separate existing demuxer; the probe runbook declares the containers actually implemented and tested. AAC file decoding does not imply AAC Bluetooth transport. The initial Bluetooth path uses SBC after PCM decoding.

The external DAC harness is provisional: GPIO22 BCLK, GPIO27 LRCLK, GPIO17 data. GPIO17 shares the blue LED and is not assumed exposed on a convenient header; identify the board and verify its pickup/loading before wiring. A PCM5102A is a stereo line DAC and needs suitable headphone amplification for ordinary headphones. Selecting `line` or `headphone` in firmware does not create missing analog hardware.

## Layout and evidence

- [Player specification and work chunks](docs/specs/cydpod-plan.md)
- [Implementation checkpoint and pending hardware/media inputs](docs/IMPLEMENTATION_STATUS.md)
- [Existing-project comparison](docs/research/reuse-options.md)
- [Hardware/DAC/power research](docs/research/cyd-hardware.md)
- `vendor/CYD-MP3-Player/`: pinned upstream application and its notices.
- `firmware/cydpod_probe/`: small AAC/output feasibility sketch.
- `tests/`, `tools/media*`: host audio and fixture verification where available.

Use [the implementation checkpoint](docs/IMPLEMENTATION_STATUS.md) to resume work after interruption. Passing compile/host tests does not complete the actual-device acceptance gates in the specification. The serial target, original media files, precise CYD revision, DAC/amp and wired headphone load still need confirmation.

## Upstream and licensing

The starting application is [Garionhk/CYD-MP3-Player](https://github.com/Garionhk/CYD-MP3-Player). Audio dependencies include [Helix](https://github.com/pschatzmann/arduino-libhelix) and [ESP32-A2DP](https://github.com/pschatzmann/ESP32-A2DP). Preserve each dependency's license and file notices; the application shell's license does not replace the codec/wrapper/graphics licenses. Source versions and local compatibility patches are recorded in the dependency manifest and build report.
