# INT-6 integration v34
Branch `cydpod/integration-v4` from `cydpod/integration-v3` 436e05b (v33). No device operations, no push.

## Merges (no-ff, **no conflicts**)
1. `cydpod/logo-splash` 9ea739b (LOGO-1; `app.ino`, `check.ps1` merged cleanly)
2. `cydpod/github-page` 587dae8 (GH-1; docs/tools only)
3. Export tag v33 -> v34 in `tools/check.ps1` (`codec-pool-v34-jc2432w328c`).
Not included: F5, uploader image, F3-F23.

## Checks (once each, final code tip)
| Check | Result |
|---|---|
| `check.ps1 -SkipProbe` (full host) | exit 0, 241 PASS lines, 0 FAIL; includes `PASS splash_test` and `PASS host stage: build_splash.sh`, media_check.py, all INT-4 suites |
| default `-SkipProbe -SkipHostMedia` | exit 0, `PASS: compile-only checks completed; no device commands were run.` |
| `-Board jc2432w328c` same flags | exit 0, same PASS line; `PASS single pool: ... 0000a000` |
| `tools/make_site_release.ps1` (temp dir) | `RELEASE_OK version=34 ... bytes=4194304`, manifest points to `cydpod-v34-w328c.bin` @ 0 |
| `tools/check_site_links.py` | `PASS site links, anchors and manifest` (repo copy; still reads v33 as committed, expected) |

## Size, W328C, v33 -> v34
Static RAM 95,056 -> 95,056 B (+0). Sketch flash 1,551,041 -> 1,566,861 B (+15,820). app.bin 1,551,184 -> 1,566,864 B (+15,680). Slot limit 1,966,080.

## Image (`E:\flash\v34`)
| File | Offset | Bytes | SHA256 |
|---|---|---:|---|
| app.ino.bootloader.bin | 0x1000 | 25024 | f508dfe30f34c2490ec08caaa96f20dc2853f66a0a92f6fb759b205e82924f29 |
| app.ino.partitions.bin | 0x8000 | 3072 | 0a8b5720e7b77ff11f1462458c3a509dee79224e5279898f26d6a2e3ae0517b7 |
| boot_app0.bin | 0xe000 | 8192 | f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0 |
| app.ino.bin | 0x10000 | 1566864 | e35611815184415935737d76b79842d0193b737978c36ca76de3aa414f5278f6 |

**v34 single file** `cydpod-v34-w328c.bin`: 4,194,304 B, SHA256 `3366e983e6ac6d421857cd8a990e6e8f28f7c9896f6ad906bd64a9217c8b13ad`. Four regions byte-identical at their offsets; 0 non-0xFF bytes elsewhere. Single-file flash erases settings (see `int6-flash.md`). Checklist: `int6-checklist.md`.
