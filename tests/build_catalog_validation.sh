#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
OUT=tests/out/catalog_validation
mkdir -p "$OUT"
g++ -O2 -std=c++17 -Wall -Wextra -Wno-misleading-indentation -DCYDPOD_CATALOG_HOST -Itests/catalog_shim -I.deps/sketchbook/libraries/TFT_eSPI -Ifirmware/cydpod_player/app tests/catalog_validation_test.cpp -o "$OUT/catalog_validation_test"
cd "$OUT"
./catalog_validation_test
