#!/bin/sh
set -eu
OUT=tests/out/flacx
mkdir -p "$OUT"
# The current firmware decoder stays pinned to this worktree's v35 source and
# byte-identical vendor inventory. stage_flac applies only its existing overlay.
python3 -I tools/stage_flac.py "$OUT/staged" --legacy
gcc -DCYDPOD_FLAC_LEGACY -O2 -fwrapv -fno-strict-aliasing -I"$OUT/staged" -c firmware/cydpod_player/app/flac_core.c -o "$OUT/reference.o"
gcc -std=c99 -pedantic -Wall -Wextra -Werror -O2 -ffunction-sections -fdata-sections -c firmware/cydpod_player/app/flac_compact.c -o "$OUT/compact.o"
gcc -std=c99 -pedantic -Wall -Wextra -Werror -O2 -DFLAC_COMPACT_MAX_BLOCK=4608 -c firmware/cydpod_player/app/flac_compact.c -o "$OUT/compact4608.o"
g++ -DCYDPOD_FLAC_LEGACY -std=c++17 -O2 -Ivendor/miniflac -Itests/audio_shim tests/flacx_host_test.cpp "$OUT/compact.o" "$OUT/reference.o" -o "$OUT/host"
g++ -DCYDPOD_FLAC_LEGACY -std=c++17 -O2 -DFLAC_COMPACT_MAX_BLOCK=4608 -Ivendor/miniflac -Itests/audio_shim tests/flacx_host_test.cpp "$OUT/compact4608.o" "$OUT/reference.o" -o "$OUT/host4608"
if nm "$OUT/compact.o" | awk '$2 ~ /^[bBdD]$/ {bad=1; print} END {exit bad}'; then
    echo 'PASS compact object: no .data/.bss symbols'
else
    exit 1
fi
python3 -I tests/flacx_fixtures.py
if [ "${FLACX_SANITIZE:-1}" = 1 ]; then
    # Instrument the new pure-C implementation and its harness. The pinned old
    # decoder remains the independent oracle; its known arithmetic is unchanged.
    gcc -std=c99 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -c firmware/cydpod_player/app/flac_compact.c -o "$OUT/sanitize.o"
    g++ -DCYDPOD_FLAC_LEGACY -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -Ivendor/miniflac -Itests/audio_shim tests/flacx_host_test.cpp "$OUT/sanitize.o" "$OUT/reference.o" -o "$OUT/sanitize"
    "$OUT/sanitize" kernel
    "$OUT/sanitize" hostile tests/out/flac/44100-2-4096.flac
    "$OUT/sanitize" restart tests/out/flac/44100-2-4096.flac
    python3 -I tests/flacx_fixtures.py --sanitize
    echo 'PASS ASan/UBSan: malformed streams, truncation, CRC, geometry, guarded buffers'
fi
