#!/bin/sh
# LOGO-1: host test for the startup screen (splash_core.h + generated splash_logo.h); also writes the
# two review PNGs docs/research/logo1-portrait.png and logo1-landscape.png. No Arduino, no panel.
set -eu
cd "$(dirname "$0")/.."
OUT=tests/out/splash
mkdir -p "$OUT"
g++ -O2 -std=c++17 -Wall -Wextra -Werror -Ifirmware/cydpod_player/app tests/splash_test.cpp -o "$OUT/splash_test"
status=0
"$OUT/splash_test" "$OUT" > "$OUT/splash_test.log" || status=$?
cat "$OUT/splash_test.log"
if [ "$status" -eq 0 ]; then
  python3 -I tools/gen_splash.py --png "$OUT/logo1-portrait.ppm" docs/research/logo1-portrait.png
  python3 -I tools/gen_splash.py --png "$OUT/logo1-landscape.ppm" docs/research/logo1-landscape.png
fi
exit $status
