#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
OUT=tests/out/m4a_r1r5; mkdir -p "$OUT"
FLAGS="-O1 -std=c++17 -w -fpermissive -Itests/host_shim -Ivendor/arduino-audio-tools/src -DCYDPOD_AAC_LC_ONLY"
WRAP="-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free"
g++ $FLAGS tests/m4a_review_r1r5_test.cpp $WRAP -o "$OUT/review"
result=0
"$OUT/review" || result=1
if printf '#include <vector>\nint main() {}\n' | g++ -m32 -x c++ - -o "$OUT/multilib-probe" 2>"$OUT/multilib.log"; then
  g++ -m32 $FLAGS tests/m4a_review_r1r5_test.cpp $WRAP -o "$OUT/review32"
  "$OUT/review32" || result=1
else
  echo "32-bit multilib unavailable; explicit uint32_t truncation assertion used (see $OUT/multilib.log)"
fi
exit "$result"
