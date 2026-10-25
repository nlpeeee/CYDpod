#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
OUT=tests/out/pcm_bound
HELIX=.deps/sketchbook/libraries/arduino-libhelix/src
MEDIA='/mnt/e/Alternative Wedding - UK Nostalgia/Tracks/07 - Dance and Indie Crossover'
mkdir -p "$OUT/codec" "$OUT/sanitized"
for mode in codec sanitized; do
 flags='-O2'
 if [ "$mode" = sanitized ]; then flags='-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer'; fi
 for c in "$HELIX"/libhelix-aac/*.c; do
  # Helix's fixed-point C uses signed shifts; keep every other UB check and all ASan checks.
  # Set CY17_FULL_UBSAN=1 to reproduce the unmodified dependency's shift diagnostics.
  codecflags=''
  if [ "$mode" = sanitized ] && [ "${CY17_FULL_UBSAN-0}" != 1 ]; then codecflags='-fno-sanitize=shift'; fi
  gcc $flags $codecflags -w -DUSE_DEFAULT_STDLIB -DCYDPOD_AAC_LC_ONLY -I"$HELIX" -c "$c" -o "$OUT/$mode/$(basename "$c" .c).o"
 done
 g++ $flags -std=c++17 -w -fpermissive -DCYDPOD_AAC_LC_ONLY -Itests/host_shim -Ivendor/arduino-audio-tools/src -I"$HELIX" \
  tests/pcm_bound_test.cpp "$OUT/$mode/"*.o -o "$OUT/$mode/test"
 "$OUT/$mode/test" "$MEDIA" >"$OUT/$mode/vendor.log"
 g++ $flags -std=c++17 -DCYDPOD_AAC_LC_ONLY -I"$HELIX" tests/helix_decode.cpp "$OUT/$mode/"*.o -o "$OUT/$mode/decode"
 "$OUT/$mode/decode" --allocation-test
 for f in fixtures/stereo-*.aac; do
  "$OUT/$mode/decode" "$f" "$OUT/$(basename "$f").pcm"
 done
 # SHA256 of the unmodified 4096-short decoder's PCM, captured before CY-17.
 (cd "$OUT" && sha256sum -c <<'HASHES'
fdad95ada3b1812361d905b338ac6246b1a2af34537679c74e810c1d87ed8cac  stereo-128.aac.pcm
a9851157af71acc7413c739470907363f5f0cfe56daadb60fb368bc2408afedc  stereo-192.aac.pcm
544283e4eac79c5f90ecebf6c7a7a46b456077e15c1e699406f2afce27459360  stereo-256.aac.pcm
HASHES
 )
 for f in fixtures/reject-*.aac; do
  if "$OUT/$mode/decode" "$f" /dev/null; then echo 'FAIL unexpected acceptance'; exit 1; else [ "$?" -eq 4 ]; fi
 done
done
g++ -O2 -std=c++17 -pthread tests/audio_core_test.cpp -o "$OUT/audio_core"
"$OUT/audio_core"
