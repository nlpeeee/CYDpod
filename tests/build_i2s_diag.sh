#!/bin/sh
# CY-75: pure scheduler/generator, normal and ASan+UBSan; no device access.
set -eu
cd "$(dirname "$0")/.."
OUT=tests/out/i2s_diag
mkdir -p "$OUT"
g++ -O2 -std=c++17 -Wall -Wextra -Werror tests/i2s_diag_test.cpp -o "$OUT/i2s_diag_test"
g++ -O1 -g -std=c++17 -fsanitize=address,undefined -fno-sanitize-recover=undefined tests/i2s_diag_test.cpp -o "$OUT/i2s_diag_test_san"
"$OUT/i2s_diag_test"
"$OUT/i2s_diag_test_san" > "$OUT/sanitized.txt"
tail -1 "$OUT/sanitized.txt"
