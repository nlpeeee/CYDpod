#!/bin/sh
set -eu
SAMPLES=${CYDPOD_TESTSAMPLES:-testsamples}
cd "$(dirname "$0")/.."
# build_miniaac_seek.sh prepares verified objects and generated fixtures first.
g++ -Wl,--wrap=xmp3_UnpackFrameHeader -O1 -std=c++17 -Wall -pthread -Ivendor/miniflac -Itests/audio_shim -Itests/catalog_shim -I.tools/staging/miniaac-patched/src -I.deps/sketchbook/libraries/arduino-libhelix/src tests/audio_seek_test.cpp tests/helix_host_alloc.cpp firmware/cydpod_player/app/codec_track.cpp firmware/cydpod_player/app/resampler.cpp tests/out/miniaac_seek/obj/*.o tests/out/miniaac_seek/mp3/*.o tests/out/miniaac_seek/flac_core.o -o tests/out/miniaac_seek/audio_test
tests/out/miniaac_seek/audio_test --output-resume
tests/out/miniaac_seek/audio_test

tests/out/miniaac_seek/audio_test "$(pwd)/tests/out/miniaac_seek/fixtures/seek.mp3"
# Read real user samples in place; never copy the music into fixtures.
if test -f "$SAMPLES"/ff-16b-2c-44100hz.wav; then
 tests/out/miniaac_seek/audio_test "$SAMPLES"/ff-16b-2c-44100hz.wav
 tests/out/miniaac_seek/audio_test "$SAMPLES"/ff-16b-2c-44100hz.mp3
else
 echo "SKIP optional real samples: $SAMPLES (set CYDPOD_TESTSAMPLES to read samples elsewhere)"
fi

tests/out/miniaac_seek/audio_test "$(pwd)/tests/out/codec_pool/48000-2.m4a" resample
if test -f "$SAMPLES"/Sample_BeeMoved_48kHz16bit.m4a; then
 tests/out/miniaac_seek/audio_test "$SAMPLES"/Sample_BeeMoved_48kHz16bit.m4a resample
 tests/out/miniaac_seek/audio_test "$SAMPLES"/Sample_BeeMoved_96kHz24bit.flac reject
else
 echo "SKIP optional real samples: $SAMPLES (set CYDPOD_TESTSAMPLES to read samples elsewhere)"
fi
