#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p tests/out/playlist_queue
g++ -O2 -std=c++17 -DCYDPOD_CATALOG_HOST -Itests/catalog_shim -Ifirmware/cydpod_player/app tests/playlist_queue_test.cpp firmware/cydpod_player/app/playlist.cpp -o tests/out/playlist_queue/test
cd tests/out/playlist_queue
./test
