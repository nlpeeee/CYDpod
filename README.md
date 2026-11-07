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
  <sub>Firmware, flashing tool, Windows build kit and a 3D-printable case.</sub>
</p>

<p align="center">
  <img src="docs/assets/home-tiles.png" width="320" alt="Simulated CYDpod Home screen with Songs, Artists, Albums, Playlists, Search and Settings tiles.">
  <img src="docs/research/cy90-quiet-outline-icons/player-r1-s0-x4.png" width="320" alt="Simulated Now Playing screen with seek bar, transport buttons and volume.">
  <br><sub>Rendered by the test harness, not photos of a device.</sub>
</p>

## What is this?

There's a little ESP32 board with a 2.4&Prime; touchscreen and a microSD slot that costs less than a sandwich. CYDpod turns it into a pocket music player. Fill a card with music, then browse by Artist, Album or Song, and play to a Bluetooth speaker or headphones. Or wire up a DAC if you like soldering.

No app, no account, no cloud. Your music stays on your card. Wi-Fi is only used by an optional upload page.

## New in 0.5.2

- 😴 **It sleeps.** After 5 minutes without music it goes into a light sleep that draws too little for our USB meter to register. Touch the screen and it carries on exactly where it was: same screen, same song, same position. It never sleeps while playing, and when the sleep timer runs out it sleeps straight away.
- ⚡ **Bluetooth comes back fast.** After a wake (or at power-on) it starts calling your speaker half a second after Bluetooth starts, not 10 s. The radio also transmits at the chip's full +9 dBm.
- 📜 **Playlists open instantly.** A playlist used to freeze the screen for about 4 s while it read every song in the library. Now the player keeps a tiny fingerprint of every file location, so a list opens in a fraction of a second.
- 📂 **Playlists live beside your music.** Copy a folder onto the card with its `.m3u8` inside and it shows up as a playlist with every song found. The `/playlists` folder still works too.
- 🎚️ **A volume slider you can hit.** In portrait, the slider now spans the whole bottom of Now Playing.

Full details, plus what is and isn't confirmed on hardware yet, are in the [release notes](docs/releases/0.5.2.md).

## What it can do

- **Plays** MP3, FLAC (16-bit), M4A (AAC-LC) and WAV. Other sample rates are resampled to 44.1 kHz. (HE-AAC isn't supported.)
- **Big libraries:** built and tested for 10,000 tracks, in folders up to three levels deep. The index lives on the card and is checked quickly at boot.
- **Browse** Artists, Albums, Songs, Folders and Favourites, with Shuffle all on every list.
- **Search** with an on-screen keyboard. It ignores case and accents, so &ldquo;beyonce&rdquo; finds Beyonc&eacute;.
- **M3U/M3U8 playlists**, even ones written for another computer's folder layout. Songs are matched by path, by their last folder and file name, or by file name alone.
- **Now Playing:** seek bar, volume, shuffle, repeat and a favourite star. After a restart it comes back paused on your last song.
- **Phone-like scrolling:** drag, flick, coast, and touch to stop.
- **Themes:** Light, Black and High Contrast, in portrait or landscape.
- **Polite Bluetooth:** if your phone takes the speaker, CYDpod doesn't grab it back, and it is only discoverable on the Pair screen.
- **Wired headphone limit:** a switchable &minus;30 dBFS ceiling. Turning it off allows &minus;6 dBFS, which is much louder.
- **Wi-Fi upload page:** hotspot or your own network, with a six-digit passcode (`000000` until you change it). It takes MP3s and playlists, and needs the separate uploader image.

<p>
  <img src="docs/research/cy91-theme-selector/player-black-r1.png" width="240" alt="Simulated Now Playing in the Black theme.">
  <img src="docs/research/cy91-theme-selector/player-hc-r1.png" width="240" alt="Simulated Now Playing in the High Contrast theme.">
  <img src="docs/research/cy92-headphone-limit.png" width="240" alt="Simulated Audio Output settings with the Headphone limit row.">
</p>

## What you need

- **A JC2432W328C board.** This is the 2.4&Prime; ESP32 Cheap Yellow Display with *capacitive* touch (ST7789 panel, CST820 touch), 4 MB flash and a microSD slot. Other CYD variants, such as the resistive ESP32-2432S028R, are **not supported**.
- **A microSD card,** FAT32, 32 GB or smaller. Put music in `/music`. Playlists can go next to the music or in `/playlists`. The player keeps its own files in `/.sys`.
- **A USB data cable and a Windows PC** to run the bundled flashing tool.
- **Something to listen on:**
  - **Bluetooth** speakers and headphones pair in Settings &rsaquo; Bluetooth. AirPods Pro 2 haven't been tested yet.
  - **Wired** needs an I2S DAC (a PCM5102A-class board is the documented example) on connector P6: GND, IO16 bit clock, IO4 data, IO17 word select. Add a headphone amp if you're driving headphones. Wired audio hasn't been tested on hardware yet.

A card reader is the easiest way to copy music across.

## Flash it

1. Download `CYDpod-0.5.2-firmware.zip` from the [release](https://github.com/nlpeeee/CYDpod/releases/tag/v0.5.2) and unzip it. It contains the Bluetooth and wired builds plus esptool.
2. Close any serial monitor, and find the board's COM port (unplug it and plug it back in to see which port appears).
3. Hold **BOOT**, tap **RESET**, then let go of **BOOT**.
4. Run this from the unzipped folder in PowerShell. It checks every file's hash, then writes and verifies the four flash regions. Your settings and Bluetooth pairing survive.

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

5. Tap **RESET** and enjoy. Start with the speaker turned down: Bluetooth volume follows the speaker's own scale, so it can be louder than you expect. The serial log runs at 115200 baud if you're curious.

## Honest limits

- **Untested hardware:** AirPods Pro 2 and wired audio build and pass host tests, but haven't been tried on a real board.
- **Touch wake:** the touch chip's &ldquo;I've been touched&rdquo; signal hasn't shown up in any board log yet, so waking from a dark screen or from Sleep relies on a quick poll (every 50 ms while asleep). It works, but it isn't the low-power path it was designed to use.
- **Not yet timed:** how fast sound returns after a wake, volume sync after Sleep, and the range gain from the stronger radio.
- **Wi-Fi upload** needs the separate uploader image, which isn't in the player download.
- **The case** (v4: Blender files plus STL/3MF) hasn't been test-printed for fit.

## Build it yourself

You'll need Windows x64, Git, Python 3.9+ and Windows PowerShell 5.1.
1. Grab `CYDpod-0.5.2-build-kit.zip` from the release and unzip it somewhere outside the checkout. It holds the pinned toolchain, so no downloads are needed.
2. Restore it into a fresh clone and build:

```powershell
git clone --branch v0.5.2 https://github.com/nlpeeee/CYDpod.git E:\dev\CYDpod
powershell -NoProfile -File E:\dev\CYDpod-build-kit\Restore-Toolchain.ps1 -Source E:\dev\CYDpod
Set-Location E:\dev\CYDpod
powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia -LargePcmRing -BuildLabel release-052
```

Images land in `.tools/images`. A rebuild can differ by a few bytes from the released binaries, so flash those for an exact match. Every dependency is pinned in [dependencies.lock.json](dependencies.lock.json).

**Under the hood:**
- All four decoders share one fixed 30,824-byte memory pool, so playback doesn't depend on how big your library is.
- The catalogue, sorting, playlists, queue, scrolling, Bluetooth rules and output switching all have automated host tests. Hardware results are always reported separately.

## Licences

Full notices are in [vendor/NOTICES.md](vendor/NOTICES.md) and [vendor/notices/](vendor/notices/). The combined firmware is **not** Apache or MIT: GPLv3 components and the Helix RPSL/RCSL terms apply. Codec patents are a separate question.

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

CYDpod's own licence hasn't been chosen yet.

## Thanks

These projects made CYDpod possible:
- [CYD-MP3-Player](https://github.com/Garionhk/CYD-MP3-Player) by Garionhk, where it all started;
- MiniAAC;
- Helix MP3 (RealNetworks), via [arduino-libhelix](https://github.com/pschatzmann/arduino-libhelix);
- [miniflac](https://github.com/jprjr/miniflac) by John Regan;
- [ESP32-A2DP](https://github.com/pschatzmann/ESP32-A2DP) by Phil Schatzmann;
- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) by Bodmer;
- the Source Sans 3 and IBM Plex Mono typefaces (SIL OFL).

<sub>An independent hobby project, not affiliated with Apple. &ldquo;iPod&rdquo; is a trademark of Apple Inc.</sub>
