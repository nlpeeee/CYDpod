#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
# Reuse the existing MiniAAC host object/fixture preparation.
sh tests/build_miniaac_seek.sh
OUT=tests/out/miniaac_seek
MP=.deps/sketchbook/libraries/arduino-libhelix/src
mkdir -p "$OUT/mp3"
for c in "$MP"/libhelix-mp3/*.c; do
 [ "$(basename "$c")" = bitstream.c ] && continue
 gcc -Os -fwrapv -fno-strict-aliasing -I"$MP" -c "$c" -o "$OUT/mp3/$(basename "$c" .c).o"
done
gcc -Os -fwrapv -fno-strict-aliasing -I"$MP" -c firmware/cydpod_player/app/mp3_bits.c -o "$OUT/mp3/bitstream.o"
CXXFLAGS="-O1 -std=c++17 -Itests/audio_shim -Itests/catalog_shim -I.tools/staging/miniaac-patched/src -I$MP"
g++ $CXXFLAGS tests/codec_pool_test.cpp tests/helix_host_alloc.cpp firmware/cydpod_player/app/codec_track.cpp firmware/cydpod_player/app/resampler.cpp "$OUT"/obj/*.o "$OUT"/mp3/*.o -o "$OUT/codec_pool_test"
python3 -I tests/codec_pool_fixtures.py
"$OUT/codec_pool_test" tests/out/codec_pool/*.m4a
g++ $CXXFLAGS tests/mp3_pool_test.cpp tests/helix_host_alloc.cpp firmware/cydpod_player/app/codec_track.cpp firmware/cydpod_player/app/resampler.cpp "$OUT"/obj/*.o "$OUT"/mp3/*.o -o "$OUT/mp3_pool_test"
"$OUT/mp3_pool_test" tests/out/codec_pool/*.mp3
g++ -O2 -std=c++17 tests/resampler_test.cpp firmware/cydpod_player/app/resampler.cpp -o "$OUT/resampler_test"
"$OUT/resampler_test"
sh tests/build_audio_seek.sh
