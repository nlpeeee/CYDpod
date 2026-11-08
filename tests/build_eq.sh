#!/bin/sh
# EQ-5BAND: host tests for eq_core.h (docs/specs/eq-5band.md). No hardware, no Arduino.
set -eu
cd "$(dirname "$0")/.."
OUT=tests/out/eq
mkdir -p "$OUT"
INC=-Ifirmware/cydpod_player/app
g++ -O2 -std=c++17 -Wall -Wextra -Werror $INC tests/eq_core_test.cpp -o "$OUT/eq_core_test"
g++ -O1 -g -std=c++17 -fsanitize=address,undefined -fno-sanitize-recover=undefined $INC tests/eq_core_test.cpp -o "$OUT/eq_core_test_san"
"$OUT/eq_core_test"
# Run first, print after: a pipe into tail would hide a failing sanitizer run (no pipefail in sh).
"$OUT/eq_core_test_san" > "$OUT/eq_core_san.log"
tail -1 "$OUT/eq_core_san.log"
