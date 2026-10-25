# CY-33: touchscreen player with M4A playback (MiniAAC)

Compile-only. Nothing here was flashed or heard; no hardware was touched.

## Image
`.tools/images/cydpod-player/app.ino.bin` from `firmware/cydpod_player/app` (job `cydpod-player` in `tools/check.ps1`).
- Size 1,347,600 B of the 1,966,080 B slot (1,347,453 B by the compiler; 618,480 B free)
- sha256 `a1b177b2927f44b87175130bb8b03cb5d1abe8a6868e6b2f768bd0050c74391e`
- Static RAM ("Global variables use") 76,632 B (stock player 56,176 B; the +20,456 B is MiniAAC's static 22.5 KB workspace and 4 KB PCM block, less the dropped 4 KB MP3 scratch and 1 KB read buffer)
- The player (1,336,576 B), uploader (1,147,600 B) and aac-probe (1,197,216 B) images are unchanged in size and hash.

## Free-heap estimate while playing (not measured)
- Measured earlier: about 53.8 KB free right after Bluetooth connects with no UI and the old image (CY-12a). The MiniAAC probe (static 77,344 B, about 0.7 KB more than this image) predicts about 29.7 KB free after connect and file open.
- The MP3 decoder's roughly 33 KB heap and its 4,096 B scan scratch are gone; MiniAAC needs no heap per track.
- Added against that: the UI/library (offset and order tables about 6 B per track, list caches, strip/background buffers) and the stock speaker task (3 KB stack). I estimate **about 20 to 27 KB free while playing**, above the stock project's own 15 KB guidance. Watch the `stat:` line (free and minimum heap every 10 s on serial).
- Unmeasured: actual heap with the UI and Bluetooth connected, the lifetime minimum, the `m4adec` stack high-water mark (the `stat:` line prints it), underruns with a background GIF running, and any sound at all.

## Known limits
No seeking (progress-bar taps and long-press +/-10 s do nothing); every M4A starts at 0, so the saved resume position is ignored. Titles are file names (no MP4 tags); no cover art. Total time shows when the file has a sample count, otherwise "--". AAC-LC 44.1 kHz stereo only. AirPods are untested (they did not pair in earlier probes); the wired DAC is not used (the stock on-board mono speaker route remains). No Wi-Fi. Songs are read from `/music` (FAT is case-insensitive, so your `/Music` works), two folders deep, up to 2,000 tracks.

## Flash and first use
Close any serial terminal first (the board is on COM10). The script does not erase, so NVS (calibration, saved devices) survives:

    cd E:\dev\CYDpod
    git switch cydpod/miniaac
    powershell -NoProfile -File tools/cy11c_flash.ps1 -ImageDir .tools/images/cydpod-player -Tag player -Base app.ino -BootApp .tools/build/cydpod-player/boot_app0.bin

It must print `FLASH_VERIFY_OK`. Then:
1. The board reboots into the player. The first start shows the touch calibration wizard (tap the targets), then "Reading song tags" while it builds the library from the `.m4a` files in `/music`.
2. Tap Setup, choose the Bluetooth output (the stock default is the on-board speaker), open the Bluetooth screen, put the speaker in pairing mode (Xiaomi Sound Pocket) and pair it. Keep the volume low.
3. Open the library (tap the title), pick a song and play. Pause/next/previous/shuffle/repeat/volume work as in the stock player.
4. For heap and errors, watch the serial output at 115200 baud (`python -m serial.tools.miniterm COM10 115200`): `audio:` lines per track, `stat:` every 10 s, `ERR m4a ...` for rejected files.
5. To get back to the earlier probe image: `powershell -NoProfile -File tools/cy11c_flash.ps1 -ImageDir .tools/images/aac-probe -Tag miniaac`.
