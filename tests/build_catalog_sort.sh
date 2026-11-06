#!/bin/sh
# CY-115: host test for catalog_sort.h (external merge sort of the catalogue orders). No Arduino, no SD.
set -eu
cd "$(dirname "$0")/.."
OUT=tests/out/catalog_sort
mkdir -p "$OUT"
g++ -O2 -std=c++17 -Wall -Wextra -Werror -Wno-misleading-indentation -Ifirmware/cydpod_player/app tests/catalog_sort_test.cpp -o "$OUT/catalog_sort_test"
status=0
"$OUT/catalog_sort_test" > "$OUT/catalog_sort_test.log" || status=$?
cat "$OUT/catalog_sort_test.log"
exit $status
