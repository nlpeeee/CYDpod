#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
# build_miniaac_seek.sh prepares verified objects and generated fixtures first.
g++ -O1 -std=c++17 -Wall -pthread -Itests/audio_shim -Itests/catalog_shim -I.tools/staging/miniaac-patched/src tests/audio_seek_test.cpp firmware/cydpod_player/app/codec_track.cpp tests/out/miniaac_seek/obj/*.o -o tests/out/miniaac_seek/audio_test
tests/out/miniaac_seek/audio_test
