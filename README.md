<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/logo/cydpod-wordmark-reversed.svg">
    <img src="docs/assets/logo/cydpod-wordmark.svg" alt="CYDpod" width="460">
  </picture>
</p>

<p align="center"><strong>Your music. Your player. No strings attached.</strong><br>
A pocket music player you build yourself, from one cheap yellow board.</p>

<p align="center">
  <a href="https://github.com/nlpeeee/CYDpod/releases/tag/v0.5.5"><strong>Download CYDpod 0.5.5</strong></a>
</p>

<p align="center">
  <img src="docs/assets/home-tiles.png" width="320" alt="Simulated CYDpod Home screen with Songs, Artists, Albums, Playlists, Search and Settings tiles.">
  <img src="docs/research/cy90-quiet-outline-icons/player-r1-s0-x4.png" width="320" alt="Simulated Now Playing screen with seek bar, transport buttons and volume.">
  <br><sub>Rendered by the test harness, not photos of a device.</sub>
</p>

## Remember when music was simple?

You had a little player and a pocket full of songs. No subscription that could vanish your favourite album overnight, no ads between tracks, no app asking for your location. You pressed play, and it played.

CYDpod brings that back.

It starts with the Cheap Yellow Display, a tiny touchscreen computer that hobbyists love and that costs less than a takeaway lunch. Flash CYDpod onto it, slip in a memory card full of your music, and you have a player that's entirely yours: your whole collection, browsed the way it should be (by artist, album and song), streaming to your Bluetooth speaker or headphones.

## Why it's special

- **You own it.** No account, no cloud and no subscription. Nobody can take your music away, change the rules or track what you listen to.
- **It feels good to use.** Smooth, phone-like scrolling, a search that understands what you meant, and playlists that open the moment you tap them.
- **Make it sound your way.** A five-band equalizer with presets for bass, vocals, treble and loudness, tuned to stay clean even with every band turned up.
- **It respects your time.** Set it down and it quietly goes to sleep. Pick it up and it carries on exactly where you were.
- **It's made for tinkering.** Choose a theme, print the case, or dig into the code. It's your player, so make it yours.
- **Prefer wires?** Add a little DAC. The board has no headphone socket, but a thumbnail-sized audio board (a PCM5102A-style I2S DAC, a few dollars online) plugs into its spare connector and gives you a proper line-out for a hi-fi or powered speakers. Add a tiny headphone amp and it drives headphones too. The download includes a wired build, and the case has a version with room for the DAC inside.
- **It shouldn't be possible.** A board this small and this cheap was never meant to be a music player. Getting a full library, four audio formats and Bluetooth streaming onto it is the fun part.

<p align="center">
  <img src="docs/research/cy91-theme-selector/player-black-r1.png" width="240" alt="Simulated Now Playing in the Black theme.">
  <img src="docs/research/cy91-theme-selector/player-hc-r1.png" width="240" alt="Simulated Now Playing in the High Contrast theme.">
</p>

## Make your own

You'll need a **JC2432W328C** Cheap Yellow Display (the capacitive-touch one), a **microSD card** with your music, a **Bluetooth speaker or headphones** (or the little DAC for wired listening), and a **Windows PC** for a few minutes.

1. [Download the latest release](https://github.com/nlpeeee/CYDpod/releases/tag/v0.5.5).
2. Follow the [flashing guide](docs/firmware/README.md).
3. Press play.

For the details, see the [firmware guide](docs/firmware/README.md) and the [release notes](docs/releases/0.5.5.md).

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
