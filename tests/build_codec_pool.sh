#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
# Reuse the existing MiniAAC host object/fixture preparation.
sh tests/build_miniaac_seek.sh
OUT=tests/out/miniaac_seek
CXXFLAGS="-O1 -std=c++17 -Itests/audio_shim -Itests/catalog_shim -I.tools/staging/miniaac-patched/src"
g++ $CXXFLAGS tests/codec_pool_test.cpp firmware/cydpod_player/app/codec_track.cpp "$OUT"/obj/*.o -o "$OUT/codec_pool_test"
"$OUT/codec_pool_test"
sh tests/build_audio_seek.sh
