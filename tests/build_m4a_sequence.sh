#!/bin/sh
# Builds and runs the CY-5 repeated/mixed-track host test (run under WSL: wsl.exe -d Debian -- sh tests/build_m4a_sequence.sh [extra.m4a ...]).
# Extra .m4a files are read-only and are played as additional tracks in sequence A.
# CYDPOD_SEQ_FLAGS="-DCYDPOD_NO_STATE_RESET" builds the pre-fix behaviour (for the before/after evidence).
set -e
cd "$(dirname "$0")/.."
OUT=tests/out/m4a; mkdir -p "$OUT/codec"
HELIX=.deps/sketchbook/libraries/arduino-libhelix/src
for c in $HELIX/libhelix-aac/*.c; do
  o="$OUT/codec/$(basename "$c" .c).o"
  [ "$o" -nt "$c" ] || gcc -O2 -w -DUSE_DEFAULT_STDLIB -DCYDPOD_AAC_LC_ONLY -I$HELIX -c "$c" -o "$o"
done
g++ -O1 -std=c++17 -w -fpermissive -DCYDPOD_AAC_LC_ONLY $CYDPOD_SEQ_FLAGS -Itests/host_shim -Ivendor/arduino-audio-tools/src \
  -I$HELIX tests/m4a_sequence_test.cpp "$OUT"/codec/*.o -o "$OUT/m4a_sequence_test"
"$OUT/m4a_sequence_test" "$@" >/dev/null  # the reader logs to stdout; results go to stderr
g++ -O1 -std=c++17 -w -fpermissive -DCYDPOD_AAC_LC_ONLY -Itests/host_shim -Ivendor/arduino-audio-tools/src tests/m4a_reset_cost.cpp \
  -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -o "$OUT/m4a_reset_cost"
"$OUT/m4a_reset_cost" >/dev/null  # heap cost of a fresh reader per track
