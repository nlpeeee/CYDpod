#!/bin/sh
# Builds and runs the CY-4 M4A rejection host test (run under WSL: wsl.exe -d Debian -- sh tests/build_m4a_reject.sh).
# Optional: pass .m4a files (read-only) as positive controls; the verdict per file is printed by m4a_caps_real.
set -e
cd "$(dirname "$0")/.."
OUT=tests/out/m4a_reject; mkdir -p "$OUT"
FLAGS="-O1 -std=c++17 -w -fpermissive -Itests/host_shim -Ivendor/arduino-audio-tools/src"
g++ $FLAGS -DCYDPOD_AAC_LC_ONLY tests/m4a_reject_test.cpp -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -o "$OUT/m4a_reject_test"
"$OUT/m4a_reject_test" >/dev/null  # the reader logs to stdout; results go to stderr
if [ $# -gt 0 ]; then
  g++ $FLAGS tests/m4a_caps_real.cpp -o "$OUT/m4a_caps_real"
  "$OUT/m4a_caps_real" "$@" >/dev/null  # per-file verdicts on stderr
fi
