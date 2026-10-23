# CYD Bluetooth MP3 Player

Firmware that turns a **"cheap yellow display"** ESP32 board into a touch-screen
MP3 player: songs from the SD card, played through the **on-board speaker** or a
**Bluetooth speaker or headphones**, with playlists, an animated background,
song titles in any script, and a WiFi page for managing music and playlists
from a phone.

- **Plays MP3s from the SD card**, 44.1/48/32/22 kHz, mono or stereo, CBR or
  VBR. Resumes the song and position after a power cut.
- **Two ways out**: the board's own speaker pad (GPIO26, the built-in DAC —
  mono, no extra parts) or Bluetooth (A2DP). Pick one in **Setup ▸ Audio out**;
  it switches while a song is playing. On the speaker route the red LED lights
  while sound is playing: it is the amplifier's enable pin as well.
- **Touch controls**: play/pause, next, previous, volume (buttons, or drag the
  volume readout), seek (hold ⏮/⏭), tap the progress bar to jump, tap the time
  to show elapsed / remaining / both. Every control lights as it is pressed.
- **Playlists**: All songs, ★ Favourites, every folder in `/music`, and `.m3u`
  playlists. Shuffle and repeat (all / one / off) play within the list you pick.
  Hold any song to star it.
- **Playlist editor in the browser**: make and edit playlists and Favourites
  from a phone by searching your songs — no files to write by hand.
- **Passcode**: every WiFi page asks for a 6-digit passcode, one signed-in
  browser at a time, with a growing wait after wrong tries.
- **Speaker buttons work**: play/pause and next/previous on the speaker itself
  control the player. Switch the speaker off and on and it reconnects on its own.
- **Remembers the last 5 devices** — a speaker, headphones, a car — and moves
  between them with one tap. Works with car head units, which cannot be found
  by scanning once their pairing screen is closed.
- **Animated backgrounds** from any GIF, any size — cropped or shrunk to fit.
- **Looks**: 8 colour themes, 7 control styles and 3 layouts, mixed freely, in
  portrait or landscape either way up.
- **English or 繁體中文** for every menu and web page. Song titles show Chinese,
  Japanese and Latin text, anti-aliased.
- **Upload mode**: add, rename and delete songs, backgrounds and playlists from
  a browser, over your home WiFi or the player's own hotspot — no computer or
  card reader needed after the first setup.

**Board: ESP32-2432S028R** (2.8", 240×320, resistive touch, micro-USB + USB-C).
Both panel versions sold under that name — **ST7789** and **ILI9341** — work with
the same firmware; see [Panel type](#panel-type).

[flash.md](flash.md) covers building, flashing and publishing releases.
[CHANGELOG.md](CHANGELOG.md) lists what changed in each version.
[docs/decisions.md](docs/decisions.md) explains why it is built the way it is, and
[docs/stage0_results.md](docs/stage0_results.md) has the hardware measurements
those decisions rest on.

---

## Contents

- [Quick start](#quick-start)
- [Using it](#using-it): [Player](#player) · [Library and playlists](#library-and-playlists) · [Setup](#setup)
- [Upload mode](#upload-mode) · [Web passcode](#web-passcode)
- [Panel type](#panel-type) · [Building](#building) · [Why two programs](#why-two-programs) · [Troubleshooting](#troubleshooting)

---

## Quick start

### 1. Prepare the SD card

A FAT32 card (tested with 2 GB and 32 GB). Create:

```
/music      your MP3s (folders allowed, two levels deep)
/bg         background GIFs named bg1.gif, bg2.gif, bg3.gif ...
/playlists  optional .m3u playlists (the player creates it if missing)
```

The player creates `/.sys` itself for its own files (library index, title
strips, converted backgrounds, Favourites). You can skip the card reader later
and add everything over WiFi — but the player needs at least one song to be
useful, so put a few on now.

### 2. Make the title font (once)

Song titles and Chinese menus are drawn from a font file on the card. It is
generated from a font already on your computer (the licence of those fonts does
not allow shipping one), so each owner makes their own:

```bash
python3 -m pip install Pillow
```

```bash
python3 tools/gen_cjk_font.py
```

This writes `sdcard/.sys/cjk16.vlw` (about 1.8 MB, 7,000+ Traditional and
Simplified characters, kana, Latin). Copy it to the card as `/.sys/cjk16.vlw`,
or upload it later from the **Title font** tab in upload mode. Without it the
player still works; titles show Latin letters only and menus stay in English.

### 3. Flash the firmware

Either flash a published release (one file, see [flash.md](flash.md)), or build
from source — see [Building](#building) below.

### 4. First boot

1. **Touch calibration.** Press and hold each corner target, then tap the centre.
   Once only; hold anywhere for 4 seconds to redo it later.
2. **"Preparing song titles"** and **"Preparing background"** screens. The first
   boot after adding songs, playlists or GIFs does some one-off work; later boots
   skip it.
3. **Sound.** It plays through the board's own speaker straight away — connect a
   small speaker to the CN1 / P3 pad if there is none fitted. For Bluetooth
   instead, set **Setup → Audio out** to Bluetooth, then put your speaker in
   pairing mode and tap its name in **Setup → Bluetooth speaker**. From then on
   it reconnects by itself, and it joins the list of remembered devices.
4. **WiFi (optional).** **Setup → Upload music**, join the `CYD-MP3-XXXX`
   hotspot from a phone, sign in with passcode **000000**, and save your home
   network on the **WiFi** tab. Upload mode then uses it.

---

## Using it

### Player

| Touch | Does |
|---|---|
| ⏯ ⏮ ⏭ | play/pause, previous (restart if more than 3 s in), next |
| hold ⏮ / ⏭ | seek back / forward 10 s |
| 🔉 🔊 | volume −/+ 5 % |
| drag the output / % readout sideways | volume, smoothly |
| progress bar | jump to that point |
| time | cycle elapsed · remaining · both |
| song title | open the Library |
| hold the song title | ★ star / unstar the song |
| ⚙ | Setup |
| hold anywhere 4 s | re-run touch calibration |

The `3/12` beside the time is the song's place in the list being played.

### Library and playlists

```
[< ★ Favourites 13 ▾   ][shuffle][repeat]
|  1  傳說 - 林子祥               ★ | ▲ |
|  2  別人的歌                       |   |
|  ...the playing one lit            | ▼ |
```

- **Tap a song** to play it; **hold it** to ★ star or unstar it.
- **▲ / ▼** page through; hold one to jump to the start or end.
- **Shuffle** and **repeat** (all → one → off) are at the top right.
- **The name at the top is the list being played.** Tap it to pick another:

| List | What is in it |
|---|---|
| All songs | the whole library |
| ★ Favourites | every song you starred (up to 500) |
| a folder | every song in that folder of `/music`, including its subfolders |
| a playlist | an `.m3u` / `.m3u8` file in `/playlists`, in its order |

Picking a list keeps the current song playing if it is in it, and starts the
list otherwise. Shuffle, repeat, next and previous all stay inside the list, and
the choice survives a restart. If that folder or playlist is deleted, the player
falls back to All songs.

The **List** layout puts the same list on the player screen, with its own ▲/▼
beside it and the mini-player underneath.

**Making playlists.** The easy way is the [Playlists tab](#upload-mode) in the
browser. Files from a PC work too — one song per line, `#` lines ignored:

```
#EXTM3U
Artist/song one.mp3
../music/Another Folder/song two.mp3
D:\Music\Artist\song three.mp3
```

An entry can be relative to `/music`, relative to the playlist, or a full PC
path containing `\Music\` — matching ignores upper and lower case. Songs the card
does not have are skipped (and shown in red in the editor).

### Setup

| Row | Values |
|---|---|
| Audio out | Speaker (the board's own, GPIO26) / Bluetooth — switches on the spot, mid-song |
| Bluetooth speaker | the devices connected before — tap one to connect or switch, **✕** to forget it; **Pair new** for a device not in the list |
| Upload music | restarts into [upload mode](#upload-mode) |
| Web passcode | shows the 6-digit [passcode](#web-passcode) for the WiFi pages; tap to reset it to 000000 |
| Theme | Dark, Light, Neon, Retro Amber, Ocean, Phosphor, Paper, Pop |
| Style | Classic, Frosted deck, Hi-fi console, Swiss grid, Cassette pop, Terminal, Paper mono — the shape of buttons, rows and the progress bar. Picking one also sets the theme it was designed for |
| Layout | Classic, Big buttons, List — where things sit |
| Background | off, bg1 … bgN |
| Orientation | landscape, landscape flipped, portrait, portrait flipped |
| Brightness | slider: drag along it, 10–100 % |
| Language | English / 中文 |
| Invert colours | for panels whose colours come out inverted |
| Panel type | ST7789 / ILI9341 — asks first, then switches and restarts |
| Calibrate touch | re-run the wizard |
| About | version, song count, free memory |

---

## Upload mode

**Setup → Upload music** restarts into the separate WiFi program (see
[Why two programs](#why-two-programs)).

- **No home WiFi saved:** the player starts its own open hotspot,
  `CYD-MP3-XXXX`. Join it from a phone or laptop; the page opens by itself, or
  browse to **http://192.168.4.1**.
- **Home WiFi saved:** it joins your network and shows its address, e.g.
  `http://192.168.0.42`. **Hotspot** on the screen switches to the hotspot
  instead, for when your network is out of reach.

Sign in with the [web passcode](#web-passcode). The page has five tabs:

- **Music** — upload MP3s (several at once, or drag and drop), rename, delete,
  make folders.
- **Playlists** — ★ Favourites and every playlist, each with ✎ edit and ✕
  delete. **New playlist** opens the editor:
  - name it, then search by title, artist or folder name;
  - **＋** adds a song, **Add all shown** adds every match (type a folder name
    to add a whole album);
  - **↑ ↓** reorder, **✕** removes; songs no longer on the card show in red;
  - **Save** — renaming a playlist renames its file.

  `.m3u` files from a PC can be uploaded here too.
- **Backgrounds** — thumbnails of your GIFs; upload or delete. Uploads are named
  bg1.gif, bg2.gif … automatically, and deleting one renumbers the rest.
- **Title font** — install or replace `cjk16.vlw`.
- **WiFi** — scan and save your home network (2.4 GHz only), and change the
  web passcode.

Tap **Done** on the page or the screen to go back to the player, which picks up
the changes as it starts. A power cut or reset during upload mode also returns
to the player.

### Web passcode

Every page of upload mode asks for a 6-digit passcode. It is **000000** until you change it; there is no need to change it
straight away.

- **Change it** on the **WiFi** tab: the current passcode, then the new one twice.
- **One browser at a time.** While one is signed in, another device with the
  right passcode is told the player is in use. It frees up on **Sign out**, or
  2 minutes after the signed-in page is closed (an open page keeps it alive).
- **Wrong tries:** two are free; the third makes you wait 30 s, then 1 min,
  2 min, 4 min … up to 15 min. A right passcode clears the count.
- **Forgot it?** **Setup ▸ Web passcode** on the player shows it, and tapping it
  resets it to 000000 — whoever holds the player can always get back in.
- The hotspot network itself stays open: WiFi passwords need at least 8
  characters, so the passcode guards the pages, not the network.

---

## Panel type

Boards sold as ESP32-2432S028R carry either an ST7789 or an ILI9341 display
controller behind identical glass. The firmware is built for the ST7789 and
drives the ILI9341 by sending its start-up sequence at run time.

If your screen is blank, mirrored or garbled on first boot, **hold a finger on
the screen while powering on**. On a new board the blue LED lights for 3 seconds
— press while it is lit — then blinks from 4 s; keep holding to 8 s and the
board switches controller and restarts. The same hold switches back.

If the screen is readable, **Setup → Panel type** does the same after asking.
Colours inverted? **Setup → Invert colours**.

---

## Building

Requires [arduino-cli](https://arduino.github.io/arduino-cli/) with the ESP32
core (tested with 3.3.10) and these libraries:

```bash
arduino-cli core install esp32:esp32
```

```bash
arduino-cli lib install TFT_eSPI
```

The Bluetooth and MP3 libraries are not in the Arduino library index; clone them
into your libraries folder:

```bash
cd ~/Documents/Arduino/libraries && git clone --depth 1 --branch v1.8.11 https://github.com/pschatzmann/ESP32-A2DP.git
```

```bash
cd ~/Documents/Arduino/libraries && git clone --depth 1 --branch v0.9.4 https://github.com/pschatzmann/arduino-libhelix.git
```

AnimatedGIF is vendored in `app/src/AnimatedGIF` (with a raised width limit), so
it does not need installing. WiFi and the web server come with the ESP32 core.

**Display config — the step that catches everyone.** TFT_eSPI is configured by a
header inside the library. Copy this project's template over it, and again after
every TFT_eSPI update:

```bash
cp config/User_Setup_2432S028R_ST7789.h.template ~/Documents/Arduino/libraries/TFT_eSPI/User_Setup.h
```

The build stops with an explanation if it is missing or wrong.

**Build and flash** (compiles both programs, flashes both, opens the serial
monitor; keeps your settings):

```bash
./tools/flash.sh app
```

Or just check everything compiles:

```bash
./tools/build_all.sh
```

**Where things are** in `app/`:

| File | What |
|---|---|
| `app.ino` | start-up for both programs, the player's loop |
| `audio.cpp` | SD → MP3 decoder → ring buffer, play order |
| `speaker.cpp`, `bt.cpp` | the two outputs: on-board DAC, Bluetooth A2DP |
| `playlist.cpp` | All songs / Favourites / folders / `.m3u` → the play queue |
| `storage.cpp`, `titles.cpp`, `background.cpp` | library index, title strips, converted GIFs |
| `screen_*.cpp`, `ui.cpp`, `layouts.cpp` | the player's screens and drawing |
| `uploadmode.cpp`, `upload_page.h` | the WiFi program: file manager, playlist editor |
| `auth.cpp` | the web passcode and session |

---

## Why two programs

The board has no PSRAM, so RAM is the constraint behind most of the design. The
Bluetooth stack takes ~80 KB and the MP3 decoder ~33 KB. Linking the WiFi stack
into the same program costs another ~21 KB of fixed memory *even if WiFi is never
turned on* — and that was enough to make the Bluetooth connection fail. So the
flash holds two programs, sharing settings and the SD card:

| Program | Has | Used for |
|---|---|---|
| **player** | Bluetooth, MP3, speaker, the full touch UI | normal use |
| **WiFi program** | WiFi, the web file manager and playlist editor — no Bluetooth, no playback | upload mode |

Setup switches between them with a restart, and any reset or power cut lands
back in the player.

The same limit rules out playing music while on WiFi. An earlier "Home mode"
(the player run from a phone or Home Assistant) was tried: through the board's
speaker it worked, but with Bluetooth audio it needed ~40 KB more memory than
the board has even with the screen and extras stripped, so it was removed. The
measurements are in [docs/decisions.md](docs/decisions.md).

The same constraint is why backgrounds and song titles are prepared at boot,
before Bluetooth starts, rather than while playing. The details and
measurements are in [docs/decisions.md](docs/decisions.md).

---

## Troubleshooting

| Symptom | Try |
|---|---|
| Upload fails at "Changing baud rate to 921600" | This board's USB chip cannot do it; `tools/flash.sh` already uses 115200 |
| "No SD card" | FAT32, fully inserted; exFAT cards will not mount |
| Titles show only Latin letters, menus stay English | `/.sys/cjk16.vlw` missing — see step 2 |
| No sound from the board | **Setup → Audio out** must say Speaker, and a speaker has to be on the CN1 / P3 pad. The red LED should light while a song plays — that pin is also the amplifier's enable, so a dark LED means the amplifier is muted |
| No sound, and the red LED stays dark while playing | The amplifier's enable is GPIO4 on this board, but boards sold under this part number vary. See `docs/stage0_results.md` for the pin walk that finds it on a different one |
| Speaker not found | **Setup → Audio out** must say Bluetooth; put the speaker in pairing mode, then **Setup → Bluetooth speaker → Pair new** |
| Speaker does not reconnect after power-on | Wait ~30 s — a saved device is called once every 15 s; if not, pairing mode once |
| "No answer - tap it to try again" | The device is off, out of range, or connected to something else |
| A car does not appear in the list | Open its "connect a device" screen first: a head unit is only findable while that is on screen. Once paired it is remembered, and a tap reaches it without that |
| Deleted device still connects | It should not — deleting also removes the pairing. If it returns, delete the player from the other device as well |
| A playlist is missing from the list on the player | Playlists and folders are read when the player starts; tap **Done** in upload mode (it restarts) |
| A playlist plays fewer songs than it lists | Its missing entries are skipped — open it in the Playlists tab: they show in red |
| Forgot the web passcode | **Setup ▸ Web passcode** shows it; tap to reset it to 000000 |
| "In use on another device" when signing in | Sign out on the other device, or wait 2 minutes after its page was closed |
| "Too many wrong tries" | Wait out the countdown (30 s, doubling to 15 min); the right passcode clears it |
| "Uploader not installed" in Setup | Only the player was flashed — use `tools/flash.sh app` or a release image |
| Always starts in upload mode, even after Done or a reset | The image flashed has no player in it (the uploader sits in the player's slot). Reflash with `tools/flash.sh app` or a release built by `tools/make_release.sh`, which checks for this |
| Started on the next song instead of resuming | The last run ended in a crash or watchdog reset, so the player skipped the song it was on rather than risk the same crash. Send the serial log if it keeps happening on one song |
| Audio drops out | Watch the serial log's `underruns` count; try background off to compare |

Serial log: 115200 baud. Every 10 s it prints free memory, audio buffer fill and
underruns, and background frame rate. Upload mode logs `upload:` and `auth:`
lines.
