# GH-1: GitHub page (README + Pages site)
Branch `cydpod/github-page` from v33 (436e05b). Docs and static site only; no firmware, no push, no device.

## Files
- `README.md` (replaced; the old text described the ESP32-2432S028), `docs/index.html` (one file, inline CSS, tiny JS), `docs/assets/` (`logo/` wordmarks and icon copied from `E:\dev\CYDpod\logo`, `home-tiles.png` cropped from `int1-render/sheet-r1.png`), `docs/flash/manifest.json`, `docs/firmware/README.md`, `tools/make_site_release.ps1`.
- Also added: `tools/check_site_links.py` (link, anchor and manifest check). Logos are under `docs/assets/logo/` (Pages serves only `/docs`), not `assets/logo/`.
- Renders: `gh1-desktop.png` (1280 px), `gh1-mobile.png` (390 px); lower sections inspected in slices, no overflow at 390.

## Checks
- `python -I tools/check_site_links.py`: `PASS site links, anchors and manifest` (41 references; the release-time `.bin` and `regions/` are allowed to be missing).
- `make_site_release.ps1` against `E:\flash\v33\cydpod-v33-w328c.bin` into a temp folder (Windows PowerShell 5.1 and pwsh 7): `RELEASE_OK ... sha256=333b5fc0...dc3b`, valid manifest (name, version, chipFamily ESP32, one part at offset 0), four region files copied, older `.bin` removed, version text idempotent. No binary committed.

## Preview, publish, release
- Preview: `python -m http.server` in `docs/` (the Install button needs https or localhost, and a release run first so the `.bin` exists).
- Publish: Settings > Pages > Deploy from branch, `main` `/docs`. Replace `OWNER`/repo name in `README.md` and the three `github.com/OWNER/CYDpod` links in `docs/index.html`.
- Release: `powershell -NoProfile -File tools/make_site_release.ps1`, then `git add docs/firmware docs/flash docs/index.html README.md`, commit, push. Version, size, SHA256 and file names come from `<!--rel:...-->` markers.

## Wording used
Hero tagline: "Up to ten thousand songs on a cheap yellow board." plus a footnote that 10,000 is host-tested and the first on-board scan is untimed. The brand sheet's "A thousand songs..." is not used.

## Could not verify / judgement calls
- ESP Web Tools 10 (`unpkg.com/esp-web-tools@10`): not run against a board; a 4 MB write at offset 0 and the erase-settings effect are inferred (INT-4).
- **Wi-Fi upload is unavailable after the single-file flash**: `firmware_uploaderPresent()` needs an image in OTA_1 (0x1f0000), which the merged file leaves 0xFF; the uploader also accepts `.mp3` names only. The page says so. Decision: ship an uploader image or leave it.
- Wired output: P6 pins are from `board.h`; DAC and amp advice (PCM5102A-class) comes from `cyd-hardware.md`; unverified on hardware. AirPods Pro 2: unverified (an early probe build did not pair).
- Brand sheet case-back text says ESP32-2432S028; not used. No case design exists in the repo, so none is listed.

## Open questions
Repo name and owner; licence for CYDpod's own code (GPLv3 and Helix terms bind the binary, which needs a source offer); tagline wording; board link (none used); add `docs/.nojekyll`; whether Wi-Fi upload ships; the W328C's USB chip (CP210x or CH340) is unrecorded.
