#!/bin/sh
# CY-10b1 allocator-hook test (run under WSL: wsl.exe -d Debian -- sh tests/build_m4a_hook.sh). Builds against a patched copy
# of the vendored AudioTools under tests/out/ (vendor/ is never modified). USE_ALLOCATOR is true, as on the ESP32 target.
set -eu
cd "$(dirname "$0")/.."
OUT=tests/out/m4a_hook; mkdir -p "$OUT"
sh tests/apply_audiotools_overlay.sh "$OUT/audiotools"
g++ -O1 -std=c++17 -w -fpermissive -DCYDPOD_AAC_LC_ONLY -DUSE_ALLOCATOR=true -Itests/host_shim -I"$OUT/audiotools/src" \
  tests/m4a_hook_test.cpp -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -o "$OUT/m4a_hook_test"
"$OUT/m4a_hook_test"
