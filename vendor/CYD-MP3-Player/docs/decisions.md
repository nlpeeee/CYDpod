# Project decisions

Choices that shaped the architecture, and why. Newest first. The measurements
behind them are in [stage0_results.md](stage0_results.md).

The constant behind almost all of them: the ESP32-2432S028R has **no PSRAM**.
With the Bluetooth stack (~80 KB) and the MP3 decoder (~33 KB) running, the
player has ~30 KB of heap to spare, and a Bluetooth connection briefly needs a
good part of that.

## 2026-10-02 — Home mode removed

Home mode (the WiFi image playing, run from a phone remote and Home Assistant
over MQTT) worked through the board's speaker, but the owner needs Bluetooth
output, and that cannot fit. A second, stripped attempt was measured on the
board with the JBL Charge 4. It had no title strips, an 8 KB audio ring, no DAC
task, Bluetooth without the AVRCP target, no mDNS, no hotspot and no file
manager, plus a custom partition table so the image fit at all (2.02 MB):

| Bluetooth Home mode, stripped | Free heap | Largest block | Cost |
|---|---|---|---|
| library + playlists | 155 KB | 110 KB | |
| audio up (8 KB ring) | 107 KB | 98 KB | 48 KB |
| `bt_begin()` (basic) | 26.5 KB | 19 KB | 80 KB |
| WiFi start | 23 KB | 14 KB | `wifi: failed to create task` |

WiFi needs ~50 KB to start and a speaker connection ~15 KB more: about 40 KB
short. The trims left (SD files, the AVRCP controller, server and MQTT buffers,
the decode stack) are worth 5–8 KB. Removing the display saves almost nothing,
since there is no framebuffer. A speaker-only Home mode was not wanted on its
own, so all of it went: `homemode.*`, `remote_page.h`, `mqtt.*`, the Setup row
and the settings behind it, and the player code built into the WiFi image.
The WiFi image is the uploader again (1.15 MB).

Kept from that work: the web passcode (it guards upload mode) and the playlist
editor. A board with PSRAM (ESP32-WROVER; not the S3, which has no Bluetooth
Classic) is the way back to it. The two entries below are the history.

## 2026-10-02 — Home mode cannot play to Bluetooth: measured, not assumed

Asked for: Home mode following **Audio out**, so the phone remote and Home
Assistant could drive a Bluetooth speaker. Tried on the board with the JBL:

| Home mode, Bluetooth linked in | Free heap | Largest block |
|---|---|---|
| before any output (library, titles, audio up) | 102 KB | 90 KB |
| after `bt_begin()` | 22 KB | — |
| after routing audio to Bluetooth | 15.5 KB | 5.9 KB |
| `WiFi.begin()` | `wifi: failed to create queue`, 17 s of retries, abort | |

Bluetooth A2DP takes ~80 KB, WiFi ~45–50 KB, and a Bluetooth connection briefly
~15 KB more: ~60 KB more than this image has. The code itself also outgrew its
1.9 MB slot (2.01 MB), which a custom partition table fixed, but memory is the
wall, and it is the same one that ruled out WiFi in the player (2026-09-17). Smaller
WiFi buffers or releasing BLE memory are worth ~15–20 KB together — not 60.

So Home mode stays on the board's speaker whatever Audio out says, and the
partition table stays `min_spiffs`. A board with PSRAM (an ESP32-WROVER, not
the S3: it has no Bluetooth Classic) would remove the limit.

## 2026-10-02 — Resume jumps to a real frame; a crash skips the song

Resuming at a time seeks to a byte worked out from the bitrate, which is almost
never a frame boundary. On one MP3 the bytes there looked enough like a header
that libhelix never returned from its decode loop. The decode task runs at
priority 5 on core 1, so nothing else on that core ran again, the watchdog reset
the board, and the saved resume point sent it straight back: a boot loop.
Measured on the board: the task's stack was fine (~2 KB of 5 KB used); it hung
right after the first read at the resume offset.

Two changes. Every jump (resume and seek) now moves forward to a header that is
followed by another one, the same test `computeDuration()` already trusts. And
after a panic or watchdog reset (`esp_reset_reason()`), start-up skips to the
next song from its beginning, so if the decoder ever wedges on some other
damaged file, the board resets once and moves on.

## 2026-10-02 — One 6-digit passcode and one session for every WiFi page

Before this, upload mode had no password: it was only reachable while the owner
had put the player in it. Home mode stays on the home network for hours, so the
pages now sit behind a passcode (`auth.cpp`).

- **6 digits, default 000000, no forced change.** The owner asked for it to be
  easy to start with. **Setup ▸ Web passcode** shows it in full and resets it:
  physical access is the recovery path, so nobody can be locked out.
- **One session at a time.** A sign-in while another browser holds the session
  is refused, rather than taking it over, so two people cannot drive the player
  at once. The session ends on Sign out or after 2 minutes without a request;
  the open pages ping, so it lasts as long as a page is open.
- **Backoff, not lockout.** Two free tries for typos, then 30 s doubling to
  15 min, global rather than per address (addresses are cheap on a hotspot).
  Counted in RAM: clearing it needs a restart, which needs the device.
- **A cookie on every route, including uploads.** Every handler is wrapped by
  `auth_guard()`. An upload's data callback cannot answer, so it checks the
  session itself and writes nothing without one.
- **The hotspot stays open.** WPA2 needs 8+ characters, so a 6-digit code cannot
  be the network key. MQTT is not behind it either: the broker has its own login.

## 2026-10-02 — Playlists are a queue of indices, names stay on the card

A playlist (`playlist.cpp`) is turned into a queue: library indices, one
`uint16_t` each, allocated once at boot before Bluetooth. `audio.cpp` keeps a
pointer to it, not a copy, and builds its play order from it, so shuffle and
repeat needed no new logic. The whole library is a null queue, which needs no table.

Folder and playlist names are listed in `/.sys/sources.txt` and read when drawn,
the same way the library index works. RAM holds none of them. Non-ASCII names
are pre-rendered with the song titles, and their checksum is part of the strips'
"done" stamp, so a new playlist is rendered even when the library has not changed.

Favourites are stored as paths, not indices: a re-index after new songs would
otherwise move every star onto another song. RAM keeps one CRC per favourite
(500 at most, 2 KB) so the star on each list row is a binary search, not an SD
read. `.m3u` entries are matched by a case-insensitive CRC of the resolved path,
in one pass over the index, with 4 bytes per entry held only while it runs.

## 2026-10-02 — Home mode is the WiFi image playing, not WiFi in the player

A phone remote and Home Assistant need WiFi, and WiFi cannot be linked into the
player beside Bluetooth (2026-09-17 entry below). So the WiFi image gained the
MP3 engine, the playlists and the speaker instead: `audio.cpp`, `speaker.cpp`
and `playlist.cpp` now build into both images. The cost is that Home mode is
speaker only. The WiFi image has room: it starts with ~280 KB of heap and no
Bluetooth to feed.

Its start mirrors the player's, and everything that needs a lot of heap happens
before WiFi: the library index, the playlists and the title strips (the font
needs ~85 KB while it renders). Settings::wifiMode, written by Setup just before
the restart, tells the image which mode to be. NVS, because the two images do
not share RTC memory layouts. A power cut still lands in the player.

MQTT is a ~200-line client (`mqtt.cpp`) rather than PubSubClient, so building
the project still needs only the libraries flash.md lists. QoS 0, one
connection, discovery for Home Assistant. HA has no MQTT media player entity, so
the player is a device made of plain entities. All three remotes (the device's
buttons, `/api/cmd`, MQTT) go through one function, `home_command()`.

## 2026-09-28 — The speaker is a second consumer, not a second player

**Context.** Everything about the sound was Bluetooth. `audio.cpp` decodes into
a 2048-frame ring and `onData()` in `bt.cpp` was the only thing that ever drained
it, so a board with nothing paired did not merely play to no one: the decode task
blocked in `pushFrame()` and the player sat still. Meanwhile `CYD_SPEAKER_PIN 26`
had been in `board.h` since stage 0, unused, with an amplifier and a pad behind it.

**Decision.** `audio_pull()` was already transport-agnostic, so the speaker is an
output module (`speaker.cpp`) and nothing in `audio.cpp` changed. It owns a writer
task that pulls 256 frames, folds them to 8-bit mono and hands them to the DAC;
`dac_continuous_write()` blocks on the DMA, which makes it the playback clock in
exactly the way the A2DP callback is.

Two consequences follow from the hardware and one from the ring:

- **`dac_continuous`, not I2S built-in-DAC mode.** That mode claims both DAC
  pins, and GPIO25 (DAC0) is this board's bit-banged touch clock. A channel mask
  drives GPIO26 alone.
- **Mono, 8 bits, dithered.** That is what the pad is. The volume in
  `audio_pull()` is applied in 16 bits, so at the default 40% only about five
  bits survive the truncation; the error is carried into the next sample, which
  costs two adds and removes the buzz that plain truncation leaves on quiet
  passages.
- **One consumer at a time, by parking rather than locking.** The ring has a
  single tail pointer. A route change sets the route to a value neither side
  matches, waits for the speaker task to say it is out and 50 ms for any A2DP
  callback to return, and only then starts the other side. A lock would have had
  to be taken by the A2DP task on every 128 frames.

**Consequences.** Switching the route does **not** stop and restart the A2DP
stack -- on the speaker route the link stays up and carries silence. The ~80 KB
is committed at boot either way, and the entry below is the reason: this code
leans on the library's internals, and tearing it down for a menu tap is a far
bigger risk than 44 KB/s of zeroes. The cost is that choosing the speaker frees
no heap.

## 2026-09-28 — The amplifier has an enable, and it is the red LED's pin

**Context.** The entry below shipped a speaker route that made no sound at all.
Not quiet, not distorted: silent, including from the DAC's own hardware cosine
oscillator, which uses no CPU, no DMA and no clock configuration. That result
was read as proof the firmware was innocent and the hardware was at fault, and
the search went to the amplifier, the pad and the speaker. All three were fine —
a board photo confirmed a populated 8002E with the documented resistor values,
and a correctly wired speaker across the bridge-tied output, which a replacement
speaker did nothing to change.

**What it actually was.** The 8002E is held in shutdown unless **GPIO4** is
driven LOW, and floating counts as shutdown. GPIO4 is also `CYD_RGB_R_PIN`.
`cydRgbLedOff()` drives it HIGH, on the first line of `setup()`, to stop the red
LED glowing — so the player muted its own amplifier on every boot, and nothing
downstream of that could ever have made a noise.

Found by holding a tone on GPIO26 and driving every other pin in turn, bisecting
on "was anything audible in this batch" rather than on when: nineteen pins HIGH
(nothing), then LOW in halves, down to GPIO4 sounding three times out of three
where GPIO16 sounded none out of three. The earlier pin sweep could not have
found it — a square wave on a shutdown line only toggles mute, silently.

**Decision.** `board.h` names the pin (`CYD_AMP_ENABLE_PIN`) and offers
`cydAmpEnable()` / `cydAmpDisable()`. `cydRgbLedOff()` is left driving GPIO4
HIGH, because amplifier-off is the right state at boot. `speaker.cpp` takes the
pin when the DAC starts and returns it when the DAC stops.

Enabling around the **DAC**, not around the route, is the deliberate part. The
two pins are the same pin at the same polarity, so there is no arrangement in
which the amplifier runs and the red LED is dark; the trade is unavoidable and
the only question is how much of the time it is paid. Following the DAC means
the LED is lit while sound is actually playing and dark otherwise, and never
comes on at all on the Bluetooth route.

The start and stop edges are treated as a pair. The DAC rests at 0 V until
something is written to it, so `dacStart()` writes one chunk of mid-rail *before*
enabling the amplifier — otherwise the first sample is a step from 0 V to 128 and
a thump through the speaker. `dacStop()` keeps the existing ramp down to 0 V and
then mutes, so the teardown is inaudible too.

**Consequences.** The red LED is a playback indicator on this board whether or
not that was wanted. Boards sold under this part number vary, so a different unit
that is silent should have this pin suspected first, and the walk re-run, before
anything in the firmware is doubted.

## 2026-09-28 — The DAC's DMA writes must be 4-byte aligned

**Context.** While chasing the silence above, the diagnostic sketches reported
`dac_continuous` running at 24255 Hz against a requested 44100, and stalling
outright on first use with writes timing out. That looked like a clock fault,
then like a channel-allocation fault, and neither was real.

**What it actually was.** The write length must be a multiple of 4 — the DAC's
DMA works in 32-bit words — and `dac_continuous_write()` reports nothing when it
is not. Measured by sweeping the length against a fixed `buf_size`:

| Write length | Result |
|---|---|
| multiple of 4 — 256, 436, 440, 444, 512, 1024, 3072 | full rate |
| even, not a multiple of 4 — 442, 446 | **exactly half rate** |
| odd — 441, 443 | DMA stalls, writes time out, deadlock |
| greater than `desc_num * buf_size` — 3074 > 3072 | deadlock |

Independent of `chan_mode`, `clk_src`, `buf_size` and the requested frequency.
`buf_size` must be aligned as well. Every anomaly traced to one line in a test
sketch: `static uint8_t buf[441]`, chosen as "exactly 10 cycles of 1 kHz at
44100" without noticing 441 is odd.

**Decision.** Nothing in `speaker.cpp` changes — `CHUNK` is 256, which is aligned
and inside the buffer, and was correct throughout. The rule is recorded as a
comment above `CHUNK` instead, because it is now load-bearing: the half-rate case
is the dangerous one, since everything plays an octave low and nothing reports
it.

**Consequences.** `CHUNK` cannot be tuned freely. Any new value must be a
multiple of 4 and no larger than `desc_num * buf_size`.

## 2026-09-20 — The player owns the Bluetooth connection, not the library

**Context.** Everything worked with a speaker and nothing worked with a car.
A speaker answers every inquiry, so the library's model -- discover, then
connect to what discovery accepted -- was never tested against a device that
announces itself once and goes quiet. Against a car head unit it failed at
every step: a tap was recorded and waited for a second sighting that never
came; the peer address was taken from the last discovery rather than from the
connection, so the car's audio played while the screen named the speaker; the
board stopped being connectable ten seconds after boot, so the car could not
open the link either; and any disconnection made the library page the device
just left, which beat the one the owner had asked for.

**Decision.** The connection is ours. A tap pages the device immediately,
through the library's own path so its state machine stays in step. The peer
address comes from the A2DP connection event, and is written back to the
library so its start-up reconnect agrees with ours. A known device is paged by
address every 15 s rather than scanned for. The board stays connectable, and a
connection the peer opens is put into a state the library will act on. Chasing
the previously connected device is start-up behaviour only, and one attempt.

**Consequences.** A subclass of `BluetoothA2DPSource` reaches four protected
members (`peer_bd_addr`, `s_peer_bdname`, `s_a2d_state`, `discovery_active`)
and overrides `app_a2d_callback` and `app_gap_callback`. That is a dependency
on the library's internals, and a version bump may need it revisited -- the
alternative was a fork. Addresses and names are stored together, five of them,
because they had drifted apart once and stranded the player on a device that
was not there.

## 2026-09-18 — A release is checked slot by slot

**Context.** A merged image went out of the build with the uploader in app0 and
app1 empty. Flashed, the board booted only into upload mode: the uploader
points the next boot at app0 as it starts, which was itself, so neither "Done"
nor a reset could leave. `make_release.sh` passed it, because its check only
asked whether the uploader was somewhere in the file. Being a full 4 MB image,
it also erased the settings and the saved speaker.

**Decision.** The script checks each slot: app0 must contain a log line only
the player has and not the uploader's, app1 the reverse. It deletes the image
if either fails, and refuses a version that differs from `FIRMWARE_VERSION`.

## 2026-09-18 — The look is data: tokens, styles, components

**Context.** Layouts were already data (a new skin cost no drawing code), but
the shape of a button was hard-coded in `ui_button()`, so a new look meant new
C++, and screens that wanted a different control re-drew their own. Sizes were
literals: the same list row was 30, 35 and 29 px on three screens.

**Decision.** Three layers. `tokens.h` holds sizes. `theme` holds colour, widened
from 13 roles to 20 (surfaces, outlines, pressed, on-accent). `style` holds
shape — radius, inset, border, shadow, bevel, divider, bar form, fonts — as a
table row, chosen in Setup independently of Theme. Components in `ui.h` read
all three. Classic reproduces the old look pixel for pixel, as the fallback.

**Constraints that shaped it.** No framebuffer, so "frosted" is a raised surface
and a highlight line, not a blur, and overlays end by redrawing the screen.
Song titles stay 18 px in every style: they are pre-rendered strips shared by
Chinese and Latin text. A style's numeric font falls back when too tall for its
box or missing a glyph — font 7 has no "/", and at 48 px fits no time box.

**Touch.** Gestures are still classified on release; that code is unchanged.
Screens get an optional press hook for feedback and drags, fed by a new live
finger position. A drag is exempt from the 4 s recalibrate hold, which a slow
brightness adjustment would otherwise cross.

## 2026-09-17 — Player and uploader are separate firmware images

**Context.** Upload mode was first a restart flag inside one binary: boot with
the flag and start WiFi instead of Bluetooth. It worked as designed and broke
the player. Linking the WiFi stack costs ~21 KB of **static** RAM (`.bss` in
libnet80211, wpa_supplicant, lwIP — `g_cnxMgr` alone is 3.9 KB) whether WiFi is
ever started or not. Free heap at boot fell from 209 KB to 186 KB, the minimum
at Bluetooth connect fell to ~12 KB, and the board reset in a loop.

Making the web server objects lazy recovered nothing measurable; the cost is in
the driver's data, not in anything the sketch allocates.

**Decision.** Two images from one sketch. `min_spiffs` gives two 1.9 MB app
slots: the player in app0, the uploader (built with `-DCYD_UPLOADER`) in app1.
"Upload music" sets the boot partition to app1 and restarts; "Done" sets it
back. Player-only sources are wrapped in `#ifndef CYD_UPLOADER` and vice versa,
so neither image links the other's stack. Both share NVS and the SD card.

**Consequences.** Flashing is two writes (`tools/flash.sh` does both) and a
release is a merged image carrying both. The uploader points the next boot at
the player the moment it starts, so a crash or power cut in upload mode can
never strand the device there. A board flashed with only the player says so in
Setup instead of restarting into an empty partition.

## 2026-09-17 — Backgrounds store only what changes, and read at a budget

**Context.** One GIF converted to 114 KB per frame: at 11 fps that is 1.25 MB/s
of SD reads, more than the card delivers (~870 KB/s, stage 0). The decode task
shares that card, and the music dropped out.

Drawing each frame in slices that pause when the audio buffer is low was tried
first and was **worse** -- 212 underruns in 7 minutes against 4. Slicing does not
reduce the reading; it only breaks it into smaller pieces.

**Decision.** Two changes instead.

- The converter keeps a canvas of what the screen will show and writes only the
  pixels a frame actually changes (format version 2). Measured: 2788 KB -> 1356
  KB on a 39-frame GIF. The canvas is ~120 KB of small per-row allocations, made
  before Bluetooth starts and freed straight after; it stops allocating with
  48 KB of heap left and simply stores whole frames if it cannot fit.
- Playback paces frames so the background reads at most ~350 KB/s. A heavy
  animation plays slower rather than taking the card away from the music.

**Result on the owner's board:** 104-frame GIF at 7.6 fps, smooth, and no
underruns during steady playback (13 in 9 minutes, in two bursts at song
changes).

## 2026-09-16 — Song titles and Chinese menus are pre-rendered strips

**Context.** TFT_eSPI's built-in fonts are ASCII. A `.vlw` font covering Big5
and GB2312 level 1 is 1.8 MB (on the card, fine) but holds ~12 bytes of metrics
per glyph in RAM while loaded — ~85 KB, impossible beside Bluetooth.

**Decision.** At boot, before Bluetooth, every song label and every Chinese UI
string without a cached strip is rendered once into an 18 px, 2-bit
anti-aliased bitmap at `/.sys/t/<crc32>.ttl`; the font is then unloaded. At run
time a strip (≤3.3 KB) is loaded and drawn in the theme's colours. Strips are
keyed by the text, so a changed ID3 title or a new UI string simply gets a new
one; a marker holding the library and UI-string checksums skips the whole check
when nothing changed.

The font is generated per owner by `tools/gen_cjk_font.py` from a system font,
because those fonts cannot be redistributed. Without it everything still works
in English/ASCII.

## 2026-09-16 — Library index on the card, not in RAM

A `String` per path is ~70 KB for a thousand songs. `/.sys/library.idx` holds
`path\ttitle\tartist` per line; RAM keeps a 4-byte offset per track. At boot
`/music` is walked and a CRC of paths and sizes is compared with the index
header; ID3 tags are only re-read when that changes.

ID3v1 is not read: Chinese v1 tags are usually GBK or Big5 with nothing saying
which, and a wrong guess is worse than the file name.

## 2026-09-16 — Play order belongs to the UI task

The decode task opens what it is asked to and reports "track ended". Shuffle,
repeat and "what is next" live in `audio_tick()` on the loop task, so the
order table is never shared between tasks and needs no locking.

## 2026-09-16 — Audio ring buffer stays at 2048 frames

Halving it to 1024 (8 KB saved) underran ~80×/s with a background playing. Not
because Bluetooth takes much at once — it pulls 128 frames per call — but
because the background's reads share the SD card with the decoder, and 23 ms is
too little cushion. 2048 (46 ms) runs at 0 underruns.

## 2026-09-16 — Bluetooth reconnects by address; pairing is a restart

After an ESP32 reset the JBL did not appear in discovery until put back in
pairing mode, so "scan for the saved name" would strand the owner every boot.
The A2DP library pages the saved address instead. During its discovery
fallback the saved address is also accepted, which is how a speaker switched on
later gets picked up.

The library does not restart discovery after a deliberate disconnect, so "Pair
new" restarts into a pairing boot (auto-reconnect off) — the exact path proven
in stage 0 — rather than fighting its state machine.

Speaker buttons (AVRCP pass-through) arrive on the Bluetooth task and are only
recorded there; `bt_tick()` acts on them from the loop task.

## 2026-09-16 — Backgrounds are converted once, not decoded live

AnimatedGIF holds ~25 KB of LZW state. Beside Bluetooth and the decoder that left
~10 KB. Instead, before Bluetooth starts, each `/bg/bgN.gif` is decoded,
scaled to cover 240×240 (or 240×320 for tall GIFs), centre-cropped, and the
pixels each frame paints are written to `/.sys/bgN.anim`. Playback reads spans
and pushes them — ~2 KB. Source size and timestamp in the header decide when to
reconvert. AnimatedGIF is vendored with `MAX_WIDTH 2048` so phone-sized GIFs can
be shrunk; its `gif.inl` is renamed because the Arduino builder does not copy
`.inl` files from a sketch's `src/`.

Text and buttons over a background are **masked** out of the spans, so they are
drawn once and never flicker, with no framebuffer.

The owner chose "accept every size" over "240×240 / 240×320 only" after the first
real GIFs turned out to be 250×250 and 498×373.

## 2026-09-16 — SD card on VSPI, touch bit-banged

The ESP32 has two free SPI hosts. The display has HSPI; the weather clock gave
VSPI to touch because it never used the SD slot. Streaming audio needs the card
on hardware SPI, so the XPT2046 (polled, happy at a few hundred kHz) is clocked
by hand. Touch calibration is stored in the panel's native portrait coordinates
so one calibration serves every orientation.

## 2026-09-16 — Reuse the weather clock's board handling

`config/board.h`, the runtime ST7789/ILI9341 switch, the boot-hold recovery
gesture, the NVS settings pattern and the touch calibration wizard all come from
the "CYD 2.8 Weather Clock Bus" project, where they were already proven on these
boards. The TFT_eSPI `User_Setup.h` templates and their compile-time
cross-checks came along too.
