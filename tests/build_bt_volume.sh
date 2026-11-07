#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p tests/out/bt-volume
g++ -O2 -std=c++17 tests/bt_volume_test.cpp -o tests/out/bt-volume/test
tests/out/bt-volume/test
sh tests/build_audio_diagnostics.sh
