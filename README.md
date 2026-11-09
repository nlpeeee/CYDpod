<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/logo/cydpod-wordmark-reversed.svg">
    <img src="docs/assets/logo/cydpod-wordmark.svg" alt="CYDpod" width="460">
  </picture>
</p><p align="center">
  <strong>Your music. Your player. No strings attached.</strong>
</p><p align="center">
  A little touchscreen music player you build yourself from a Cheap Yellow Display.<br>
  Your music lives on a microSD card — not in an app, an account or a subscription.
</p><p align="center">
  <a href="https://github.com/nlpeeee/CYDpod/releases/latest"><strong>Download CYDpod</strong></a>
  &nbsp;·&nbsp;
  <a href="docs/firmware/README.md">Flashing guide</a>
  &nbsp;·&nbsp;
  <a href="docs/releases/0.6.0.md">What's new in 0.6.0</a>
  &nbsp;·&nbsp;
  <a href="https://www.youtube.com/watch?v=9bTcNbn6Pm0">Watch the demo</a>
</p><p align="center">
  <img src="docs/assets/beauty_01_hero.png" width="360" alt="Render of CYDpod in its case">
</p><h2>Remember when music was simple?</h2><p>A pocketful of albums. A play button. Nothing between you and the next track.</p><p>CYDpod brings that feeling to a low-cost ESP32 touchscreen. Put your music on a microSD card, browse it by artist, album or song, and listen on Bluetooth headphones or a speaker. No cloud, no ads, and no internet connection required to play your collection.</p><p>It's a proper little music player — and one you can open up, customise and make your own.</p><h2>What it can do</h2><ul>
  <li><strong>Play your own files.</strong> MP3, AAC, FLAC and WAV, straight from microSD.</li>
  <li><strong>Find what you want.</strong> Browse artists, albums, songs, folders, playlists and Favourites, or use search.</li>
  <li><strong>Keep the music flowing.</strong> Gapless playback for continuous albums, plus an <strong>Up Next</strong> queue. Hold a song, album or artist to play it next or add it to the queue.</li>
  <li>
  <strong>Listen your way.</strong>
  Stream to Bluetooth headphones or speakers,
  or enjoy wired audio through an external
  I²S DAC/headphone amplifier. Tested with the
  <a href="https://github.com/WeActStudio/WeActStudio.I2SEarphoneModuleV1">
    WeActStudio I²S Earphone Module V1
  </a>
  (PCM5100A, 3.5 mm stereo output,
  rated 160 mW into 32 Ω).
</li>

  <li><strong>Shape the sound.</strong> Five-band EQ with presets for bass, vocals, treble and loudness.</li>
  <li><strong>Make it yours.</strong> Smooth touchscreen controls, themes and sleep/resume behaviour, with the source there to tinker with.</li>
</ul><p>All of that runs on a Cheap Yellow Display — a small, inexpensive board that wasn't built to be a dedicated music player.</p><p align="center">
  <img src="docs/assets/home-tiles.png" width="310" alt="Simulated CYDpod home screen showing the library and settings tiles">
  <img src="docs/research/cy90-quiet-outline-icons/player-r1-s0-x4.png" width="310" alt="Simulated Now Playing screen with transport and volume controls">
</p><h2>Pick your board</h2><p>CYDpod supports two ESP32 touchscreen boards. <strong>Download the firmware for the board you actually have.</strong></p><table>
  <thead>
    <tr>
      <th>Board</th>
      <th>Touch</th>
      <th>Audio output</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><strong>JC2432W328C</strong></td>
      <td>Capacitive</td>
      <td>Bluetooth, or optional wired output through a PCM5102A-style I²S DAC</td>
    </tr>
    <tr>
      <td><strong>ESP32-2432S028R</strong><br>(generic 2.8-inch CYD)</td>
      <td>Resistive</td>
      <td>Bluetooth, or the board's speaker pad</td>
    </tr>
  </tbody>
</table><p>The optional external DAC is supported on the <strong>JC2432W328C only</strong>. It lets you connect wired audio equipment; choose <strong>Headphones</strong> under <strong>Settings → Audio Output</strong> when using it. The generic resistive board does not have this wired-DAC route.</p><h2>Make your own</h2><p>You'll need a supported board, a microSD card containing your music, a USB data cable and a Windows PC for flashing. For listening, use a Bluetooth speaker or headphones, or the wired-audio option supported by your board. The board also needs USB power or a suitable power setup.</p><ol>
  <li><a href="https://github.com/nlpeeee/CYDpod/releases/latest"><strong>Download the latest release</strong></a> and select the ZIP for your board.</li>
  <li><a href="docs/firmware/README.md"><strong>Follow the flashing guide</strong></a> to install the firmware.</li>
  <li><strong>Insert your music card</strong>, connect your audio output and press play.</li>
</ol><p>Want to build from source instead? The <a href="docs/firmware/README.md#build-from-source">firmware guide</a> covers the pinned build kit, tools and commands. See the <a href="docs/releases/0.6.0.md">0.6.0 release notes</a> for changes and known hardware issues.</p><h2>A couple of things to know</h2><p>Gapless playback depends on the microSD card being fast enough to open the next track in time. Bluetooth compatibility varies by device; current limitations are recorded in the <a href="docs/releases/0.6.0.md">release notes</a>. This is a DIY project, not an assembled, battery-powered product.</p><h2>Thanks</h2><p>CYDpod builds on work by:</p><ul>
  <li><a href="https://github.com/Garionhk/CYD-MP3-Player">CYD-MP3-Player</a> by Garionhk, where it began;</li>
  <li>MiniAAC;</li>
  <li>Helix MP3 (RealNetworks), via <a href="https://github.com/pschatzmann/arduino-libhelix">arduino-libhelix</a>;</li>
  <li>miniflac by John Regan;</li>
  <li><a href="https://github.com/pschatzmann/ESP32-A2DP">ESP32-A2DP</a> by Phil Schatzmann;</li>
  <li><a href="https://github.com/Bodmer/TFT_eSPI">TFT_eSPI</a> by Bodmer.</li>
</ul><p>The firmware includes third-party code under several licences, including GPLv3 and RealNetworks RPSL/RCSL terms; see <a href="vendor/NOTICES.md">third-party notices</a>. <strong>A licence for CYDpod's original code has not yet been declared.</strong> Existing third-party licence requirements continue to apply.</p><hr><p align="center"><sub>CYDpod is an independent hobby project, not affiliated with Apple. “iPod” is a trademark of Apple Inc.</sub></p>
