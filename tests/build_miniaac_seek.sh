#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
python3 -I tools/stage_miniaac.py
OUT=tests/out/miniaac_seek
SRC=.tools/staging/miniaac-patched/src
mkdir -p "$OUT/obj"
for c in "$SRC/miniaac.c" "$SRC"/helix/*.c; do
  gcc -Os -fwrapv -fno-strict-aliasing -std=c99 -I"$SRC" -c "$c" -o "$OUT/obj/$(basename "$c" .c).o"
done
g++ -O1 -std=c++17 -Wall -Wextra -I"$SRC" tests/miniaac_seek_test.cpp "$OUT/obj/"*.o -o "$OUT/test"
python3 -I tests/miniaac_seek_fixtures.py "$OUT/fixtures"
"$OUT/test" "$OUT/fixtures/"*.m4a "$OUT/fixtures/raw.aac" "$OUT/fixtures/"*.bad
