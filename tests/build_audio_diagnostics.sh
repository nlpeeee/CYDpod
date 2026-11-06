#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p tests/out/audio-diagnostics
g++ -O1 -std=c++17 -pthread -ffunction-sections -fdata-sections -Wl,--gc-sections -Itests/audio_shim -Itests/catalog_shim -Ivendor/miniaac/src tests/audio_diagnostics_test.cpp -o tests/out/audio-diagnostics/test
tests/out/audio-diagnostics/test
