# MiniAAC seek overlay

Pristine pinned `vendor/miniaac` remains untouched. `tools/stage_miniaac.py` verifies its inventory, verifies the normalized-LF overlay hash, then applies the patch to `.tools/staging/miniaac-patched`. Both maintained player and probe compile the staged library. No dependency downloads or core changes.

The extension seeks by existing bounded M4A sample/chunk tables, flushes Helix, and primes eight AAC-LC packets in the caller's existing PCM storage. It reuses an M4A-unused position field; workspace size and table caches do not grow. ADTS remains explicitly unseekable. Cooperative cancellation checks table walks and the bounded decoder prime. Host validation compares returned windows with sequential decoding; device timing, stack margin, ring behavior under actual A2DP load and listening remain hardware acceptance items.

Validation (from repository root; installed tools only):

```
powershell -NoProfile -File tools/check.ps1
wsl.exe -d Debian -- sh -c "cd /mnt/e/dev/.worktrees/CYDpod/cy43-heap; sh tests/build_miniaac_seek.sh"
wsl.exe -d Debian -- sh -c "cd /mnt/e/dev/.worktrees/CYDpod/cy43-heap; sh tests/build_audio_seek.sh"
wsl.exe -d Debian -- sh -c "cd /mnt/e/dev/.worktrees/CYDpod/cy43-heap; sh tests/build_miniaac_adapter.sh"
```

Use the current checkout path when running elsewhere. The seek fixture generator uses `ffmpeg` or an existing Windows `ffmpeg.exe` through WSL. Synthetic files stay in ignored `tests/out`. Audio-task tests use real production code and decoder with host SD/FreeRTOS stand-ins; they do not measure ESP task stack, radio behavior, or device latency. Optional `tests/miniaac_seek_real.py <read-only music folder> [limit]` compares seeks with sequential decoding without copying music. Tests use a host-only PCM reference; firmware does not allocate one.
