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
  <sub>Firmware, flashing tool and a 3D-printable case.</sub>
</p>

<p align="center">
  <img src="docs/assets/home-tiles.png" width="320" alt="Simulated CYDpod Home screen with Songs, Artists, Albums, Playlists, Search and Settings tiles.">
  <img src="docs/research/cy90-quiet-outline-icons/player-r1-s0-x4.png" width="320" alt="Simulated Now Playing screen with seek bar, transport buttons and volume.">
  <br><sub>Rendered by the test harness, not photos of a device.</sub>
</p>

## A music player that shouldn't fit, but does

The Cheap Yellow Display is a hobbyist favourite: an ESP32 with a 2.4&Prime; touchscreen and a microSD slot, for less than a takeaway lunch. It has no extra memory chip and no audio hardware. CYDpod turns it into a proper pocket music player anyway.

- 🎵 **Ten thousand songs.** Browse by Artist, Album, Song or Folder, on a chip with less memory than a single holiday photo.
- 🎧 **Four formats, decoded on the board.** MP3, FLAC, AAC and WAV, streamed straight to your Bluetooth speaker or headphones. Nothing else to buy.
- 🔎 **Search that forgives you.** An on-screen keyboard where &ldquo;beyonce&rdquo; finds Beyonc&eacute;.
- 👆 **Scrolling that feels like a phone.** Drag, flick, coast, and tap to stop.
- 📜 **Playlists that just work.** Drop in `.m3u` and `.m3u8` files, even ones made on another computer with a different folder layout, and CYDpod finds the songs. Lists open in a fraction of a second.
- 😴 **It sleeps like a phone, too.** Leave it alone and it dozes off, drawing too little for our USB meter to register. Touch the screen and it picks up exactly where you left off, then reconnects to your speaker in seconds.
- 🎨 **Make it yours.** Light, Black and High Contrast themes, in portrait or landscape.
- 🔒 **No app, no account, no cloud.** Your music lives on your card, and that's it.

<p>
  <img src="docs/research/cy91-theme-selector/player-black-r1.png" width="240" alt="Simulated Now Playing in the Black theme.">
  <img src="docs/research/cy91-theme-selector/player-hc-r1.png" width="240" alt="Simulated Now Playing in the High Contrast theme.">
  <img src="docs/research/cy92-headphone-limit.png" width="240" alt="Simulated Audio Output settings with the Headphone limit row.">
</p>

## New in 0.5.2

- **Sleep mode.** After five minutes of quiet, CYDpod goes to sleep, and wakes with a touch: same screen, same song, same spot.
- **Bluetooth that comes back fast.** It calls your speaker within moments of waking instead of after a long pause, and the radio now transmits at full strength.
- **Instant playlists.** Opening a list used to take about four seconds. Now it's quick enough that you won't notice.
- **Playlists beside your music.** Copy a folder with its playlist inside onto the card, and it just shows up.
- **A bigger volume slider** in portrait mode.

## Get started

You'll need:
- **A JC2432W328C board,** the capacitive-touch Cheap Yellow Display. Other CYD variants aren't supported.
- **A microSD card** (FAT32, up to 32 GB) with your music in a `/music` folder.
- **A Bluetooth speaker or headphones.**
- **A Windows PC and a USB cable** to install the firmware.

Grab the [latest release](https://github.com/nlpeeee/CYDpod/releases/tag/v0.5.2), then follow the [flashing guide](docs/firmware/README.md). It takes a couple of minutes, and the download includes everything you need. Want to build it yourself or dig into the details? The [firmware guide](docs/firmware/README.md) and the [release notes](docs/releases/0.5.2.md) have you covered.

## Thanks

CYDpod stands on the shoulders of:
- [CYD-MP3-Player](https://github.com/Garionhk/CYD-MP3-Player) by Garionhk, where it all started;
- MiniAAC;
- Helix MP3 (RealNetworks), via [arduino-libhelix](https://github.com/pschatzmann/arduino-libhelix);
- [miniflac](https://github.com/jprjr/miniflac) by John Regan;
- [ESP32-A2DP](https://github.com/pschatzmann/ESP32-A2DP) by Phil Schatzmann;
- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) by Bodmer.

The firmware combines components under several licences, including GPLv3 and the Helix RPSL/RCSL. The full notices are in [vendor/NOTICES.md](vendor/NOTICES.md). CYDpod's own licence hasn't been chosen yet.

<sub>An independent hobby project, not affiliated with Apple. &ldquo;iPod&rdquo; is a trademark of Apple Inc.</sub>
