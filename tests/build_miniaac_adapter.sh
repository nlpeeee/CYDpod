#!/bin/sh
# WSL Debian: sh tests/build_miniaac_adapter.sh [media-folder [manifest.tsv [max-files]]]
# Builds vendor/miniaac (C, the CMake flags of its own build) + the firmware adapter and runs tests/miniaac_adapter_test.cpp.
# CYDPOD_REAL_MEDIA overrides the default folder; exit 77 means SKIP, not PASS. Media is only read.
set -eu
cd "$(dirname "$0")/.."
python3 -I tools/stage_miniaac.py
SRC=.tools/staging/miniaac-patched/src
OUT=tests/out/miniaac_adapter
mkdir -p "$OUT/obj"
for c in "$SRC/miniaac.c" "$SRC"/helix/*.c; do
  gcc -Os -fwrapv -fno-strict-aliasing -ffunction-sections -fdata-sections -std=c99 -I"$SRC" \
    -c "$c" -o "$OUT/obj/$(basename "$c" .c).o"
done
g++ -O1 -std=c++17 -Wall -Wformat=2 -Werror=format -DCYDPOD_AAC_LC_ONLY -Itests/host_shim -Ifirmware/cydpod_probe -I"$SRC" \
  tests/miniaac_adapter_test.cpp "$OUT/obj/"*.o -o "$OUT/miniaac_adapter_test"
if [ $# -eq 0 ]; then
  set -- "${CYDPOD_REAL_MEDIA-/mnt/e/Alternative Wedding - UK Nostalgia/Tracks/07 - Dance and Indie Crossover}"
fi
exec "$OUT/miniaac_adapter_test" "$@"
