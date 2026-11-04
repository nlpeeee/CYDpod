#!/bin/sh
# CY-70: host test for the I2S test-tone generator (no hardware, no Arduino).
set -eu
cd "$(dirname "$0")/.."
OUT=tests/out/i2s_tone
mkdir -p "$OUT"
g++ -O2 -std=c++17 -Wall -Wextra -Werror tests/i2s_tone_test.cpp -o "$OUT/i2s_tone_test"
g++ -O1 -g -std=c++17 -fsanitize=address,undefined -fno-sanitize-recover=undefined tests/i2s_tone_test.cpp -o "$OUT/i2s_tone_test_san"
"$OUT/i2s_tone_test"
"$OUT/i2s_tone_test_san" | tail -1
