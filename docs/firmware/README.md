# docs/firmware: release artefacts (not committed until a release)

GitHub Pages serves `/docs`, so the flash image the **Install** button writes has to live here. It is 4 MB, so it is **not kept in Git between releases**.

`tools/make_site_release.ps1` fills this folder in one command:

```powershell
powershell -NoProfile -File tools/make_site_release.ps1            # newest E:\flash\v*\cydpod-v*-w328c.bin
powershell -NoProfile -File tools/make_site_release.ps1 -Source E:\flash\v33\cydpod-v33-w328c.bin
```

It checks the image (4,194,304 bytes, ESP image headers at 0x1000 and 0x10000), then:

| Output | Path |
| --- | --- |
| Merged image, flashed at offset 0 | `docs/firmware/cydpod-v<N>-w328c.bin` (older `cydpod-v*-w328c.bin` files are removed) |
| Four separate regions for the keep-settings flash | `docs/firmware/regions/` (`app.ino.bootloader.bin` 0x1000, `app.ino.partitions.bin` 0x8000, `boot_app0.bin` 0xe000, `app.ino.bin` 0x10000), copied from the image's folder when present |
| ESP Web Tools manifest | `docs/flash/manifest.json` (name, version, chipFamily `ESP32`, one part at offset 0) |
| Version, size, SHA256, date text | `docs/index.html` and `README.md` (the `<!--rel:...-->` markers) |

Then `git add docs/firmware docs/flash docs/index.html README.md`, commit and push. Do not hand-edit the version text.

Flashing the single image at offset 0 erases saved settings (speaker bond, volume, theme, Wi-Fi, last track); the SD card is untouched. The four-region flash keeps them.
