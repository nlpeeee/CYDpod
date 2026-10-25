#!/bin/sh
# Builds and runs the faststart .m4a host demux+decode test with g++ (run under WSL: wsl.exe -d Debian -- sh tests/build_m4a_host.sh).
set -e
cd "$(dirname "$0")/.."
OUT=tests/out/m4a; mkdir -p "$OUT/codec"
HELIX=.deps/sketchbook/libraries/arduino-libhelix/src
for c in $HELIX/libhelix-aac/*.c; do
  o="$OUT/codec/$(basename "$c" .c).o"
  [ "$o" -nt "$c" ] || gcc -O2 -w -DUSE_DEFAULT_STDLIB -DCYDPOD_AAC_LC_ONLY -I$HELIX -c "$c" -o "$o"
done
g++ -O1 -std=c++17 -w -fpermissive -DCYDPOD_AAC_LC_ONLY -Itests/host_shim -Ivendor/arduino-audio-tools/src \
  -I$HELIX tests/m4a_host_test.cpp "$OUT"/codec/*.o -o "$OUT/m4a_host_test"
"$OUT/m4a_host_test" fixtures/stereo-faststart.m4a fixtures/stereo-192.aac
