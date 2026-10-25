#!/bin/sh
# Builds and runs the CY-11a file-backed sample-size table host test (WSL: wsl.exe -d Debian -- sh tests/build_m4a_filetable.sh [media-folder]).
# Part (b) reads the real files read-only from the folder (default CYDPOD_REAL_MEDIA or the usual path); without the folder only parts a, c, d
# run and the script exits 77. Compares the file-backed table with the resident table (old path, limit raised) per file.
set -e
cd "$(dirname "$0")/.."
OUT=tests/out/m4a_filetable; mkdir -p "$OUT/codec"
HELIX=.deps/sketchbook/libraries/arduino-libhelix/src
for c in $HELIX/libhelix-aac/*.c; do
  o="$OUT/codec/$(basename "$c" .c).o"
  [ "$o" -nt "$c" ] || gcc -O2 -w -DUSE_DEFAULT_STDLIB -DCYDPOD_AAC_LC_ONLY -I$HELIX -c "$c" -o "$o"
done
g++ -O1 -std=c++17 -w -fpermissive -DCYDPOD_AAC_LC_ONLY -Itests/host_shim -Ivendor/arduino-audio-tools/src -I$HELIX \
  tests/m4a_filetable_test.cpp "$OUT"/codec/*.o -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -o "$OUT/m4a_filetable_test"
if [ $# -eq 0 ]; then
  set -- "${CYDPOD_REAL_MEDIA-/mnt/e/Alternative Wedding - UK Nostalgia/Tracks/07 - Dance and Indie Crossover}"
fi
exec "$OUT/m4a_filetable_test" fixtures/stereo-faststart.m4a "$@" >/dev/null  # the reader logs to stdout; results go to stderr
