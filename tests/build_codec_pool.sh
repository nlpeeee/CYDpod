#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
# Reuse the existing MiniAAC host object/fixture preparation.
sh tests/build_miniaac_seek.sh
OUT=tests/out/miniaac_seek
MP=.deps/sketchbook/libraries/arduino-libhelix/src
mkdir -p "$OUT/mp3"
for c in "$MP"/libhelix-mp3/*.c; do
 gcc -Os -fwrapv -fno-strict-aliasing -I"$MP" -c "$c" -o "$OUT/mp3/$(basename "$c" .c).o"
done
gcc -Os -fwrapv -fno-strict-aliasing -I"$MP" -c firmware/cydpod_player/app/mp3_bits.c -o "$OUT/mp3/mp3_bits.o"
gcc -Os -fwrapv -fno-strict-aliasing -Ivendor/miniflac -c firmware/cydpod_player/app/flac_core.c -o "$OUT/flac_core.o"
CXXFLAGS="-Wl,--wrap=xmp3_UnpackFrameHeader -O1 -std=c++17 -Ivendor/miniflac -Itests/audio_shim -Itests/catalog_shim -I.tools/staging/miniaac-patched/src -I$MP"
g++ $CXXFLAGS tests/codec_pool_test.cpp tests/helix_host_alloc.cpp firmware/cydpod_player/app/codec_track.cpp firmware/cydpod_player/app/resampler.cpp "$OUT"/obj/*.o "$OUT"/mp3/*.o "$OUT/flac_core.o" -o "$OUT/codec_pool_test"
python3 -I tests/codec_pool_fixtures.py
"$OUT/codec_pool_test" tests/out/codec_pool/*.m4a tests/out/codec_pool/48000-tone.wav tests/out/codec_pool/48000-2-cbr.mp3
if test -f /mnt/e/dev/CYDpod/testsamples/Sample_BeeMoved_48kHz16bit.m4a; then
 "$OUT/codec_pool_test" /mnt/e/dev/CYDpod/testsamples/Sample_BeeMoved_48kHz16bit.m4a
fi
g++ $CXXFLAGS tests/mp3_pool_test.cpp tests/helix_host_alloc.cpp firmware/cydpod_player/app/codec_track.cpp firmware/cydpod_player/app/resampler.cpp "$OUT"/obj/*.o "$OUT"/mp3/*.o "$OUT/flac_core.o" -o "$OUT/mp3_pool_test"
"$OUT/mp3_pool_test" tests/out/codec_pool/*.mp3
python3 -I tests/flac_fixtures.py
g++ $CXXFLAGS -fsanitize=address,undefined -fno-sanitize-recover=undefined -Ivendor/miniflac -Ifirmware/cydpod_player/app tests/flac_pool_test.cpp tests/helix_host_alloc.cpp firmware/cydpod_player/app/codec_track.cpp firmware/cydpod_player/app/resampler.cpp "$OUT"/obj/*.o "$OUT"/mp3/*.o "$OUT/flac_core.o" -o "$OUT/flac_pool_test"
"$OUT/flac_pool_test"
python3 -I tools/generate_fir.py --check
g++ -O2 -std=c++17 -fsanitize=undefined -fno-sanitize-recover=undefined tests/resampler_test.cpp firmware/cydpod_player/app/resampler.cpp -o "$OUT/resampler_test"
"$OUT/resampler_test"
sh tests/build_audio_seek.sh
