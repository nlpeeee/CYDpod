# CY-99 fling / v27

Code: `6b72ba5` after `e05498a`; base `f496e9f`.
Wall-clock Fling, one viewport frame/pass; touch-down stops and becomes a drag, never a tap. Thumb follows and holds 550 ms. Hard edge clamp: soft translation needs exposed-area/clipping painting; no glow. Cache-only fast frames; below 200 px/s a no-frame pass may load one missing row with the existing ring guard. UTF-8 SD strips/Search resume after motion. `stat: scroll`: frames, last/max ms, claim-to-first-paint ms, flings, last fling underrun delta.

Checks on final code, logs `.tools/logs/cy99-{ui,core,default,jc2432w328c,export}.log`:
- WSL `sh .tools/cy98-ui.sh`, exit 0: `PASS CY-99 fling: start/finish, tap-to-stop/drag, hard edges, fake-clock frame independence, cache-only fast frames, thumb timeout, stats/underruns`. Exact-boundary reversal covered; pre-existing dialog gate excluded, no rebaseline.
- WSL g++ `tests/scroll_core_test.cpp -Ifirmware/cydpod_player/app` then executable, exit 0: `PASS all`. Initial missing -I corrected; core unchanged.
- `tools/check.ps1 -SkipProbe -SkipHostMedia`, then add `-Board jc2432w328c`, both exit 0: `PASS: compile-only checks completed; no device commands were run.`
- `powershell -File .tools/cy99-export.ps1`: `PASS V27_EXPORT` (all manifest sizes/hashes, four offsets).

Flash/static bytes vs CY-98b: 1497161 (+5496) / 91856 (+144); W328C 1538385 (+5528) / 93448 (+144). `PASS single pool`: 40960 B unchanged; no new heap/frame buffers.

Normal image: `.tools/images/codec-pool-v27-jc2432w328c/`.
File / offset / SHA256 (all seven files verified against MANIFEST.json):
- `app.ino.bin` / 0x10000 / `69c19597b3e5bb21da31ec654b05f23317ba297c2a1d5ea80ac5ac0bcadd030c`
- `app.ino.bootloader.bin` / 0x1000 / `f508dfe30f34c2490ec08caaa96f20dc2853f66a0a92f6fb759b205e82924f29`
- `app.ino.partitions.bin` / 0x8000 / `0a8b5720e7b77ff11f1462458c3a509dee79224e5279898f26d6a2e3ae0517b7`
- `boot_app0.bin` / 0xe000 / `f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0`

Owner only, not run: identify W328C as COM7; hold BOOT, tap RESET, release BOOT. Keep factory backup/v8 rollback:
```powershell
Set-Location E:\dev\.worktrees\CYDpod\cy98-viewport
$img = '.tools/images/codec-pool-v27-jc2432w328c'
$tool = '.\.deps\arduino\packages\esp32\tools\esptool_py\5.3.0\esptool.exe'
foreach ($op in @('write-flash','verify-flash')) {
  & $tool --chip esp32 --port COM7 --baud 460800 --before no-reset --after no-reset $op --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 "$img/app.ino.bootloader.bin" 0x8000 "$img/app.ino.partitions.bin" 0xe000 "$img/boot_app0.bin" 0x10000 "$img/app.ino.bin"
}
```
Press RESET. Feel-test drag, flick distance, tap-to-stop/continue dragging, both list ends, placeholder recovery and audio underruns while flinging. Hardware/audio acceptance remains owner work.
