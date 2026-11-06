#!/bin/sh
set -eu
OUT=tests/out/flac-fast
mkdir -p "$OUT"
SAMPLES=${CYDPOD_TESTSAMPLES:-/mnt/e/dev/CYDpod/testsamples}
python3 -I tools/stage_flac.py "$OUT/staged"
gcc -Os -fwrapv -fno-strict-aliasing -I"$OUT/staged" -c firmware/cydpod_player/app/flac_core.c -o "$OUT/after.o"
g++ -O1 -std=c++17 -Ivendor/miniflac -Itests/audio_shim tests/flac_bench.cpp "$OUT/after.o" -o "$OUT/after"
# Never compare a stale output if the owner file is unexpectedly rejected.
rm -f "$OUT/after.pcm"
"$OUT/after" "$SAMPLES/Arctic Monkeys - Suck It and See - 08 - Reckless Serenade.flac" "$OUT/after.pcm"
cmp "$OUT/before.pcm" "$OUT/after.pcm"
echo 'PASS owner FLAC PCM byte-exact against baseline'
md5sum "$OUT/after.pcm"
"$OUT/after" "$SAMPLES/Sample_BeeMoved_96kHz24bit.flac" /dev/null
