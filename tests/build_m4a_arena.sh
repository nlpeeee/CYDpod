#!/bin/sh
# CY-10b2 arena test (WSL: wsl.exe -d Debian -- sh tests/build_m4a_arena.sh [media-folder]).
# Builds against the overlay-patched copy of the vendored AudioTools (tests/out/, vendor/ is never modified), USE_ALLOCATOR=true as on the
# ESP32. Runs the arena test plainly and under -fsanitize=address,undefined, then the malformed-input suites (caps, reject) in arena mode:
# the same cases and expectations, with the peak taken from the arena and held to the documented live bound.
# Exit 77 = real-media part skipped. CYDPOD_ARENA_FAST=1 skips the sanitizer build.
set -eu
cd "$(dirname "$0")/.."
OUT=tests/out/m4a_arena; mkdir -p "$OUT/codec" "$OUT/codec-asan"
sh tests/apply_audiotools_overlay.sh "$OUT/audiotools"
HELIX=.deps/sketchbook/libraries/arduino-libhelix/src
for c in $HELIX/libhelix-aac/*.c; do
  o="$OUT/codec/$(basename "$c" .c).o"
  [ "$o" -nt "$c" ] || gcc -O2 -w -DUSE_DEFAULT_STDLIB -DCYDPOD_AAC_LC_ONLY -I$HELIX -c "$c" -o "$o"
done
FLAGS="-std=c++17 -w -fpermissive -DCYDPOD_AAC_LC_ONLY -DUSE_ALLOCATOR=true -Itests/host_shim -I$OUT/audiotools/src"
MEDIA="${1:-${CYDPOD_REAL_MEDIA-/mnt/e/Alternative Wedding - UK Nostalgia/Tracks/07 - Dance and Indie Crossover}}"
g++ -O1 $FLAGS -I$HELIX tests/m4a_arena_test.cpp "$OUT"/codec/*.o -o "$OUT/m4a_arena_test"
rc=0
"$OUT/m4a_arena_test" fixtures/stereo-faststart.m4a "$MEDIA" >/dev/null || rc=$?
if [ "$rc" != 0 ] && [ "$rc" != 77 ]; then echo "arena test FAILED ($rc)"; exit 1; fi
if [ -z "${CYDPOD_ARENA_FAST-}" ]; then
  SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer"
  # the codec objects are plain C and need no instrumentation; helix_malloc/free come from the test itself
  g++ -O1 -g $SAN $FLAGS -I$HELIX tests/m4a_arena_test.cpp "$OUT"/codec/*.o -o "$OUT/m4a_arena_test_asan"
  r2=0
  ASAN_OPTIONS=detect_leaks=1 "$OUT/m4a_arena_test_asan" fixtures/stereo-faststart.m4a "$MEDIA" >/dev/null || r2=$?
  if [ "$r2" != 0 ] && [ "$r2" != 77 ]; then echo "arena test under ASan/UBSan FAILED ($r2)"; exit 1; fi
  echo "arena test under -fsanitize=address,undefined: exit $r2"
fi
# malformed-input suites in arena mode (peak = arena high water, bound = kM4aArenaLiveBoundBytes)
for t in caps reject; do
  g++ -O1 $FLAGS -DCYDPOD_ARENA_RUN tests/m4a_${t}_test.cpp -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -o "$OUT/m4a_${t}_arena"
  "$OUT/m4a_${t}_arena" >/dev/null
  echo "$t suite in arena mode: PASS"
done
exit $rc
