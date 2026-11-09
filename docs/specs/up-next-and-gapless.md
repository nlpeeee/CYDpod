# Up Next queue and gapless playback

Status: released in 0.6.0; host-checked and owner-tested on the JC2432W328C and the ESP32-2432S028R. Evidence: `docs/research/up-next-and-gapless.md`.

## Up Next

**Hold to queue.** Rest a finger on a row for 600 ms (`TOUCH_LONG_MS`) without dragging and a menu opens: **Play next**, **Add to queue** and **Cancel**. Tapping outside the menu also cancels. The hold claims the press, so lifting the finger does nothing and the hold never counts as a recalibration hold. A short tap still plays the row, as before.

| Row | Queues |
| --- | --- |
| A song (Songs, an album's or artist's songs, search results, a folder, playlist or Favourites) | that song |
| An album (Albums, or an artist's albums) | every song on the album, in album order |
| An artist (Artists), or an artist's **All Songs** row | every song by the artist |

- **Play next** puts the songs in front of anything already queued, keeping their order. **Add to queue** puts them at the end.
- Up Next holds 64 songs. A larger album or artist queues as many as fit and says so ("Queued N of M songs").

**Playing.** Queued songs play before the rest of the list. While one plays, the list's place stays on the song that was playing when it started, so the list carries on from there.
- **Next** takes the next queued song first.
- **Previous** from a queued song, within its first 3 s, returns to the list song it interrupted. After 3 s it restarts the song, as everywhere else.
- **Repeat One** keeps repeating the current song, even with songs queued.
- **Repeat Off:** queued songs still play when the list ends, then the player stops at the first song, paused.
- **Shuffle** and starting another list leave Up Next alone.

**Up Next page.** Open it from Now Playing → Options → **Up Next**; its row shows how many songs are queued.
- Row 0, **Clear Up Next**, asks before emptying the queue.
- Tapping a song plays it now and drops it from the queue.
- Holding a song offers **Play now** and **Remove**.

**Lifetime.** Up Next lives in RAM: 128 B, used by the loop task only. It survives screen-off and Sleep, which resume in place, and is cleared by a restart (including Refresh Library and output switches that restart).

API: `audio.h` `audio_upNextAdd/Len/At/Remove/Clear/Play`, `audio_playingUpNext`.

## Gapless

**Hand-off.** At a natural track end the decode task finishes the file, publishes every frame (including the resampler tail) and signals the end immediately, instead of waiting for the PCM ring (6,144 frames, ~139 ms) to drain.
- The loop task requests the next song as a *continuous* request. It does not flush the ring or reset the EQ, so the old tail keeps playing while the next file opens behind it.
- This applies to the next list song, the next Up Next song and Repeat One.
- While paused at the end, the hand-off waits for Play, so the song on screen is still the one to be heard.

**Hard requests.** Next, Previous, picking a song, seeking and Up Next → Play now flush the ring at once, as before. Repeat Off's end-of-list stop waits for the last tail to play out before loading the first song, paused.

**Clock.** `carryFrames` counts the old tail that is still in the ring when the new file opens. `audio_pull()` spends pulled frames on it before advancing the new track's position, so the readout stays on what is heard. The title and `audio_trackSerial()` switch when the new file opens, up to ~139 ms before its first sample is heard.

**Encoder padding.**
- FLAC and WAV have none.
- AAC/M4A is trimmed by MiniAAC's edit list (`edts/elst`) handling, which predates this change.
- MP3 now:
  - skips the Xing/Info tag frame, which held no audio and used to decode as a 26 ms frame of silence;
  - reads the LAME/Lavf/Lavc tag's encoder delay and padding, and plays only decoded frames `[delay+529, total−padding+529)`;
  - trims the end only when the Xing frame count is present;
  - seeks, positions and duration use the same trimmed timeline.

**Diagnostics.**
- `stat: gap track N missing F frames (M ms)` follows each gapless boundary: the silence inserted between the old tail and the new track's first frame. 0 is gapless.
- Boundary silence is reported there, not as an underrun.
- `gapless on|off` switches the hand-off until reboot, for A/B listening. `gapless` alone prints the state.

## Not covered

- An open longer than the ring's ~139 ms still leaves a short gap, which `stat: gap` shows.
- A song that fails to open after a gapless hand-off cuts the old tail when the player skips to the next one.
- MP3s without a LAME/Lavf/Lavc tag keep their encoder delay (about 50 ms).
