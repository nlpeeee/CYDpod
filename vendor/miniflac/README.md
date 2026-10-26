# miniflac (vendored, unmodified)

Single-header FLAC decoder, licence 0BSD (`LICENSE`, copied from the header's own notice).
Author: John Regan (upstream https://github.com/jprjr/miniflac).

## Provenance (CY-51c, 2026-10-26)
- `miniflac.h` is byte-identical to the header vendored in `cowork-ai/go-miniflac`
  commit `25a101bc2f8bb8d8462be8e4dc403eeb75e0bfde`.
- Archive: https://codeload.github.com/cowork-ai/go-miniflac/tar.gz/25a101bc2f8bb8d8462be8e4dc403eeb75e0bfde
  (33,633 B, SHA256 `1d2fa5a6fda95044ee8c93586b714801c4c90a6817dff95966dec8e27c9fc400`, re-verified in CY-51c).
- `miniflac.h` SHA256 `d117703cf74eed2388710a4c5cb0a2d09c3360e1cfa7f8c7a95197080e3b27f0` (214,422 B).
- Why not jprjr/miniflac directly: on 2026-10-26 that repository returned 404 from the GitHub API, `git ls-remote` and
  codeload, so no upstream tag/SHA could be pinned. The header above is the copy CY-51a evaluated and measured.
  The binding's own licence (CC0) does not apply to the header; the header carries 0BSD.
- No local edits. `tools/miniflac-files.sha256.json` is verified by `tools/check.ps1`.
