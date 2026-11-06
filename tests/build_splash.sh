#!/bin/sh
# LOGO-2: host test for the startup screen (splash_core.h + generated splash_logo.h); also writes the
# two progress strips docs/research/logo2-boot-{portrait,landscape}.png. No Arduino, no panel.
set -eu
cd "$(dirname "$0")/.."
OUT=tests/out/splash
mkdir -p "$OUT"
g++ -O2 -std=c++17 -Wall -Wextra -Werror -Ifirmware/cydpod_player/app -I.deps/sketchbook/libraries/TFT_eSPI tests/splash_test.cpp -o "$OUT/splash_test"
status=0
"$OUT/splash_test" "$OUT" > "$OUT/splash_test.log" || status=$?
cat "$OUT/splash_test.log"
if [ "$status" -eq 0 ]; then
  python3 -I tools/gen_splash.py --png "$OUT/logo2-boot-portrait.ppm" docs/research/logo2-boot-portrait.png
  python3 -I tools/gen_splash.py --png "$OUT/logo2-boot-landscape.ppm" docs/research/logo2-boot-landscape.png
fi
exit $status
