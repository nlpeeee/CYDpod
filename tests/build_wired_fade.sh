#!/bin/sh
# CY-71: host tests for the wired route: wired_fade.h (gain ramps, ceiling, dither)
set -eu
cd "$(dirname "$0")/.."
OUT=tests/out/wired
mkdir -p "$OUT"
for t in wired_fade; do
  g++ -O2 -std=c++17 -Wall -Wextra -Werror tests/${t}_test.cpp -o "$OUT/${t}_test"
  g++ -O1 -g -std=c++17 -fsanitize=address,undefined -fno-sanitize-recover=undefined tests/${t}_test.cpp -o "$OUT/${t}_test_san"
  "$OUT/${t}_test"
  "$OUT/${t}_test_san" | tail -1
done
