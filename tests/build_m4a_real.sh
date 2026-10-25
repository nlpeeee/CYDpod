#!/bin/sh
# WSL Debian: sh tests/build_m4a_real.sh [media-folder [manifest.tsv]]
# CYDPOD_REAL_MEDIA overrides the default folder; exit 77 means SKIP, not PASS.
# Repeatable negative check after a positive run (must exit 1):
# sed 's/9985$/9986/' tests/real_media_expected.tsv > tests/out/m4a_real/negative.tsv
# sh tests/build_m4a_real.sh "$CYDPOD_REAL_MEDIA" tests/out/m4a_real/negative.tsv
set -eu
cd "$(dirname "$0")/.."
OUT=tests/out/m4a_real
mkdir -p "$OUT"
g++ -O1 -std=c++17 -w -fpermissive -DCYDPOD_AAC_LC_ONLY \
  -Itests/host_shim -Ivendor/arduino-audio-tools/src \
  tests/m4a_caps_real.cpp -o "$OUT/m4a_caps_real"
if [ $# -eq 0 ]; then
  set -- "${CYDPOD_REAL_MEDIA-/mnt/e/Alternative Wedding - UK Nostalgia/Tracks/07 - Dance and Indie Crossover}"
fi
# Vendor parser traces go to stdout; verdicts and failures remain on stderr.
exec "$OUT/m4a_caps_real" "$@" >/dev/null
