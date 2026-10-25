#!/bin/sh
# CY-10b3 allocation-failure sweep (WSL: wsl.exe -d Debian -- sh tests/build_m4a_alloc_fail.sh [real-file.m4a]).
# Builds against the overlay-patched AudioTools (tests/out/, vendor/ is never modified), USE_ALLOCATOR=true as on the ESP32, with the
# global malloc family wrapped (-Wl,--wrap) so allocations that bypass the arena hook are counted and can be failed too.
# Runs the sweep plainly and under -fsanitize=address,undefined for the fixture, a rejected file (moov-last) and one real file (read-only).
# Exit 77 = no real file (everything else passed). CYDPOD_ALLOC_FAIL_FAST=1 skips the sanitizer build.
set -eu
cd "$(dirname "$0")/.."
OUT=tests/out/m4a_alloc_fail; mkdir -p "$OUT/codec"
sh tests/apply_audiotools_overlay.sh "$OUT/audiotools"
HELIX=.deps/sketchbook/libraries/arduino-libhelix/src
for c in $HELIX/libhelix-aac/*.c; do
  o="$OUT/codec/$(basename "$c" .c).o"
  [ "$o" -nt "$c" ] || gcc -O2 -w -DUSE_DEFAULT_STDLIB -DCYDPOD_AAC_LC_ONLY -I$HELIX -c "$c" -o "$o"
done
FLAGS="-std=c++17 -w -fpermissive -DCYDPOD_AAC_LC_ONLY -DUSE_ALLOCATOR=true -Itests/host_shim -I$OUT/audiotools/src"
WRAP="-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free"
REAL="${1:-${CYDPOD_REAL_FILE-/mnt/e/Alternative Wedding - UK Nostalgia/Tracks/07 - Dance and Indie Crossover/07-01 - Deee-Lite - Groove Is in the Heart.m4a}}"
[ -f "$REAL" ] || REAL=
g++ -O1 $FLAGS -I$HELIX tests/m4a_alloc_fail_test.cpp "$OUT"/codec/*.o $WRAP -o "$OUT/m4a_alloc_fail_test"
rc=0
# the vendored parser traces to stdout; verdicts and failures are on stderr
"$OUT/m4a_alloc_fail_test" fixtures/stereo-faststart.m4a fixtures/stereo-moov-last.m4a ${REAL:+"$REAL"} >/dev/null || rc=$?
if [ "$rc" != 0 ] && [ "$rc" != 77 ]; then echo "alloc-fail sweep FAILED ($rc)"; exit 1; fi
if [ -z "${CYDPOD_ALLOC_FAIL_FAST-}" ]; then
  SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer"
  g++ -O1 -g $SAN $FLAGS -I$HELIX tests/m4a_alloc_fail_test.cpp "$OUT"/codec/*.o $WRAP -o "$OUT/m4a_alloc_fail_test_asan"
  r2=0
  ASAN_OPTIONS=detect_leaks=1 "$OUT/m4a_alloc_fail_test_asan" fixtures/stereo-faststart.m4a fixtures/stereo-moov-last.m4a ${REAL:+"$REAL"} >/dev/null || r2=$?
  if [ "$r2" != 0 ] && [ "$r2" != 77 ]; then echo "alloc-fail sweep under ASan/UBSan FAILED ($r2)"; exit 1; fi
  echo "alloc-fail sweep under -fsanitize=address,undefined: exit $r2"
fi
exit $rc
