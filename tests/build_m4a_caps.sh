#!/bin/sh
# Builds and runs the M4A caps/malformed-input host test (run under WSL: wsl.exe -d Debian -- sh tests/build_m4a_caps.sh).
# Optional: pass .m4a files to also run the guarded reader over them (read-only).
set -e
cd "$(dirname "$0")/.."
OUT=tests/out/m4a_caps; mkdir -p "$OUT"
FLAGS="-O1 -std=c++17 -w -fpermissive -Itests/host_shim -Ivendor/arduino-audio-tools/src"
g++ $FLAGS -DCYDPOD_AAC_LC_ONLY tests/m4a_caps_test.cpp -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -o "$OUT/m4a_caps_test"
"$OUT/m4a_caps_test" >/dev/null
if [ $# -gt 0 ]; then
  g++ $FLAGS tests/m4a_caps_real.cpp -o "$OUT/m4a_caps_real"
  "$OUT/m4a_caps_real" "$@" >/dev/null
fi
