#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p tests/out/audio_order
test -f .tools/staging/miniaac-patched/src/miniaac.h
g++ -O2 -std=c++17 -Wall -Wextra -ffunction-sections -fdata-sections -pthread -Itests/audio_shim -Itests/catalog_shim -I.tools/staging/miniaac-patched/src tests/audio_order_test.cpp -Wl,--gc-sections -Wl,--wrap=malloc -o tests/out/audio_order/test
tests/out/audio_order/test
