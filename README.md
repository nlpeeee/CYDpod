<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/logo/cydpod-wordmark-reversed.svg">
    <img src="docs/assets/logo/cydpod-wordmark.svg" alt="CYDpod" width="460">
  </picture>
</p>

<p align="center"><strong>Up to ten thousand songs on a cheap yellow board.</strong><br>
An iPod-style music player for the JC2432W328C &ldquo;Cheap Yellow Display&rdquo;.</p>

<p align="center">
  <a href="https://github.com/nlpeeee/CYDpod/releases/tag/v0.5.2"><strong>Download CYDpod 0.5.2</strong></a><br>
  <sub>JC2432W328C firmware, Windows build kit and enclosure files.</sub>
</p>

<p align="center">
  <img src="docs/assets/home-tiles.png" width="320" alt="Simulated CYDpod Home screen with Songs, Artists, Albums, Playlists, Search and Settings tiles.">
  <img src="docs/research/cy90-quiet-outline-icons/player-r1-s0-x4.png" width="320" alt="Simulated Now Playing screen with seek bar, transport buttons and volume.">
  <br><sub>Simulator renders from the test harness, not photos of a device.</sub>
</p>

## What it is

CYDpod is open firmware that turns a very cheap ESP32 touchscreen board into a pocket music player. It reads music from a microSD card and shows it the classic way: Artist, Album, Song. Sound goes to a Bluetooth speaker or headphones, or out of a wired I2S output you add. No cloud service, no account; Wi-Fi is used only by an optional upload page.

## What you need

**Required**

- **A JC2432W328C board**: the 2.4&Prime; ESP32 &ldquo;Cheap Yellow Display&rdquo; with *capacitive* touch (ST7789 panel, CST820 touch), 4 MB flash, microSD slot. Other CYD variants (for example the resistive ESP32-2432S028R) are **not supported**.
- **A microSD card**: FAT32, 32 GB or smaller (the tested setup). Music in `/music` (sub-folders fine), playlists in `/playlists`. The player keeps its catalogue cache in `/.sys`.
- **A USB data cable and a Windows computer** for the supplied esptool flashing executable.
- **An audio output.** *Bluetooth* speaker or headphones (pair in Settings &rsaquo; Bluetooth; **AirPods Pro 2 are not verified**). Or *wired*: an I2S DAC board on connector P6 (silkscreen: GND, IO16, IO4, IO17 = bit clock, data, word select), plus a headphone amplifier for headphones; a PCM5102A-class DAC is the documented example. Wired audio is **not yet verified on hardware**.

**Optional**

- A microSD card reader (the simplest way to copy music).
- An I2S DAC and headphone amplifier for the wired output.
- A phone or PC for the Wi-Fi upload page. It needs a separate uploader image that the single flash file does not include (see Known limits).

## What it can do

- **Formats:** MP3, FLAC (16-bit), M4A (AAC-LC), PCM WAV; other common sample rates are resampled to 44.1 kHz; HE-AAC is not supported.
- **Library:** built for 10,000 tracks (host-tested), cached on the card and validated quickly at boot.
- **Browse:** Artists, Albums, Songs, Folders, Favourites; Shuffle all on every list.
- **Search** with an on-screen keyboard; accents and case folded (&ldquo;beyonce&rdquo; finds Beyonc&eacute;).
- **M3U / M3U8 playlists** in `/playlists`, including playlists written for another folder layout (matched by path tail or file name).
- **Now Playing:** seek bar, volume, shuffle, repeat, favourite star; restores your last track, paused, at boot.
- **Themes:** Light, Black, High Contrast; portrait or landscape.
- **Phone-like scrolling:** drag, flick and coast, touch to stop, scroll thumb.
- **Bluetooth that behaves:** if your phone takes the speaker, the player does not page it back; discoverable only on the Pair screen.
- **Wired headphone limit:** switchable &minus;30 dBFS ceiling (off allows &minus;6 dBFS, much louder).
- **Wi-Fi upload page** (hotspot or your network, six-digit passcode, `000000` until changed): MP3s and playlists only; needs the uploader image.

<p>
  <img src="docs/research/cy91-theme-selector/player-black-r1.png" width="240" alt="Simulated Now Playing in the Black theme.">
  <img src="docs/research/cy91-theme-selector/player-hc-r1.png" width="240" alt="Simulated Now Playing in the High Contrast theme.">
  <img src="docs/research/cy92-headphone-limit.png" width="240" alt="Simulated Audio Output settings with the Headphone limit row.">
</p>

## Why it is worth building

1. **Cheap hardware:** one small ESP32 board with a screen and card slot; no extra DAC for Bluetooth.
2. **No cloud, no account:** files stay on your card.
3. **Open firmware:** source, dependency pins and third-party notices are in this repo.
4. **Bounded memory:** one fixed 30,824-byte codec pool shared by all four decoders, so playback does not depend on library size.
5. **Host-tested:** catalogue, sorting, playlists, queue, scrolling, Bluetooth policy and output switching have automated tests; hardware results are labelled separately.
6. **Flash the release:** the download includes the flashing tool and four matching flash regions.

## Flash 0.5.2

Download `CYDpod-0.5.2-firmware.zip` from the [0.5.2 release](https://github.com/nlpeeee/CYDpod/releases/tag/v0.5.2) and extract it. The Bluetooth firmware uses compact FLAC and a 4,096-frame PCM ring. The archive contains matching wired firmware, esptool 5.3.0, manifests and instructions.

Close the serial monitor and identify the board's COM port. Hold BOOT, tap RESET, then release BOOT. From the extracted archive, run the following PowerShell block. The four regions preserve saved settings and the Bluetooth bond.

```powershell
Set-Location .\firmware\bluetooth
$manifest = Get-Content .\MANIFEST.json -Raw | ConvertFrom-Json
foreach ($image in $manifest.files) {
    if ((Get-FileHash -LiteralPath $image.file -Algorithm SHA256).Hash -ne $image.sha256) {
        throw "Hash mismatch for $($image.file)"
    }
}
$port = Read-Host 'COM port for the JC2432W328C'
foreach ($op in 'write-flash','verify-flash') {
    & ..\..\flash-tools\esptool.exe --chip esp32 --port $port --baud 460800 --before no-reset --after no-reset $op --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 app.ino.bootloader.bin 0x8000 app.ino.partitions.bin 0xe000 boot_app0.bin 0x10000 app.ino.bin
    if ($LASTEXITCODE -ne 0) { throw "esptool $op failed" }
}
```

Tap RESET to boot. Use 115200 baud for the serial monitor. Start at low speaker volume: since 0.5.1 Bluetooth uses the speaker’s own volume scale and removes duplicate attenuation. Release checksums are in `SHA256SUMS.txt` on GitHub, and each firmware manifest records its source and file hashes.

## Release evidence

Version 0.5.2 adds idle power savings and an in-place Sleep, Bluetooth reconnects within seconds of waking, +9 dBm Bluetooth transmit power, playlists that open almost instantly (including `.m3u8` files kept beside the music), one more folder level and a full-width portrait volume slider. It keeps 0.5.1's ESP32-A2DP 1.8.12, compact FLAC, 4,096-frame ring and flick scrolling. The [release notes](docs/releases/0.5.2.md) distinguish owner reports and software checks from remaining hardware measurements. The 0.5.0 overnight shuffle report remains the long-run playback baseline.

## Known limits

- Wi-Fi upload requires a separate uploader image. The 0.5.2 player archive contains the Bluetooth and wired player builds.
- AirPods Pro 2 and wired audio have compile or host coverage, with hardware acceptance still pending.
- The touch controller's interrupt has not reached the ESP32 in any board log, so dark-screen taps and Sleep wakes rely on a safety poll (every 50 ms while asleep). Wake-to-sound timing and volume sync after Sleep are not yet measured.
- Timed flick latency, title coverage, and a zero-missing-frame scrolling comparison have not been recorded for 0.5. The [flick comparison](docs/research/flick-motion.md) gives the procedure.
- The v4 enclosure includes editable Blender assemblies and print exports. Physical fit and latch tolerances need a print trial.

## Build from source

The source of truth is [nlpeeee/CYDpod](https://github.com/nlpeeee/CYDpod). `main` contains 0.5.2, and `v0.5.2` identifies the release source. Use a fresh checkout with Windows x64, Git, Python 3.9 or later and Windows PowerShell 5.1.

Download `CYDpod-0.5.2-build-kit.zip` from the release and extract it to a separate folder. It contains the pinned Windows toolchain and `Restore-Toolchain.ps1`. Restore that kit into the checkout, then build the release configuration.

```powershell
git clone --branch v0.5.2 https://github.com/nlpeeee/CYDpod.git E:\dev\CYDpod
powershell -NoProfile -File E:\dev\CYDpod-build-kit\Restore-Toolchain.ps1 -Source E:\dev\CYDpod
Set-Location E:\dev\CYDpod
powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia -LargePcmRing -BuildLabel release-051
```

Images and manifests are written to `.tools/images`. The released flash binaries retain the exact final candidate bytes. Dependency pins and third-party inventories are in [dependencies.lock.json](dependencies.lock.json). The base kit was previously restored and compiled in a fresh checkout; the kit (unchanged since 0.5.1) overlays the pinned 1.8.12 library. Both release firmware variants compiled, and the overlay is checked against the compiled library inventory.

Local release packages and historical archives live under `E:\dev\CYDpod\releases\0.5.2` and are ignored by Git. GitHub Releases holds the downloadable 0.5.2 assets.

## Licences

Full notices: [vendor/NOTICES.md](vendor/NOTICES.md) and [vendor/notices/](vendor/notices/). The combined firmware binary is **not** labelled Apache or MIT: GPLv3 components and the Helix RPSL/RCSL terms apply, and codec patent questions are separate.

| Component | Licence |
| --- | --- |
| CYD-MP3-Player (application shell) | Apache-2.0 |
| AnimatedGIF (BitBank) | Apache-2.0 |
| ESP32-A2DP | Apache-2.0 |
| arduino-libhelix / Helix MP3 | GPLv3 wrapper, RealNetworks RPSL/RCSL codec |
| MiniAAC | MIT wrapper; Helix AAC core under GPLv3 / RPSL / RCSL |
| miniflac | 0BSD |
| TFT_eSPI | BSD two-clause (MIT ancestry, font notices kept) |
| arduino-audio-tools | GPLv3 (vendored, not wired into the build) |
| Arduino-ESP32 / ESP-IDF | LGPL-2.1 and Espressif/third-party notices |

CYDpod's own licence has not been chosen yet.

## Credits

[CYD-MP3-Player](https://github.com/Garionhk/CYD-MP3-Player) by Garionhk (the starting point); MiniAAC; Helix MP3 (RealNetworks) via [arduino-libhelix](https://github.com/pschatzmann/arduino-libhelix); [miniflac](https://github.com/jprjr/miniflac) by John Regan; [ESP32-A2DP](https://github.com/pschatzmann/ESP32-A2DP) by Phil Schatzmann; [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) by Bodmer; [ESP Web Tools](https://esphome.github.io/esp-web-tools/) by ESPHome for the browser flasher; type: Source Sans 3 and IBM Plex Mono (SIL OFL).

<sub>Independent hobby project, not affiliated with Apple. &ldquo;iPod&rdquo; is a trademark of Apple Inc.</sub>
