#!/bin/sh
# CY-71: host tests for the wired route: wired_fade.h (gain ramps, ceiling, dither),
# wired_pump.h (chunk accounting, bounded writes, final fade) and out_route.h (route
# numbering and fallback, built for a wired and a non-wired board). No hardware, no Arduino.
set -eu
cd "$(dirname "$0")/.."
OUT=tests/out/wired
mkdir -p "$OUT"
for t in wired_fade wired_pump; do
  g++ -O2 -std=c++17 -Wall -Wextra -Werror tests/${t}_test.cpp -o "$OUT/${t}_test"
  g++ -O1 -g -std=c++17 -fsanitize=address,undefined -fno-sanitize-recover=undefined tests/${t}_test.cpp -o "$OUT/${t}_test_san"
  "$OUT/${t}_test"
  # Run first, print after: a pipe into tail would hide a failing sanitizer run (no pipefail in sh).
  "$OUT/${t}_test_san" > "$OUT/${t}_san.log"
  tail -1 "$OUT/${t}_san.log"
done
g++ -O2 -std=c++17 -Wall -Wextra -Werror -DCYD_BOARD_JC2432W328C tests/out_route_test.cpp -o "$OUT/out_route_wired"
g++ -O2 -std=c++17 -Wall -Wextra -Werror tests/out_route_test.cpp -o "$OUT/out_route_plain"
"$OUT/out_route_wired"
"$OUT/out_route_plain"
# The Output setting through the real settings.cpp: persistence, speaker -> Bluetooth migration,
# and the forced-route test image leaving the saved choice alone.
INC="-Itests/settings_shim -Ifirmware/cydpod_player/app"
g++ -O2 -std=c++17 -Wall -Wextra -Werror $INC -DCYD_BOARD_JC2432W328C tests/settings_route_test.cpp -o "$OUT/settings_route_wired"
g++ -O2 -std=c++17 -Wall -Wextra -Werror $INC -DCYD_BOARD_JC2432W328C -DCYD_DEFAULT_OUT_ROUTE=2 tests/settings_route_test.cpp -o "$OUT/settings_route_forced"
g++ -O2 -std=c++17 -Wall -Wextra -Werror $INC tests/settings_route_test.cpp -o "$OUT/settings_route_plain"
"$OUT/settings_route_wired" 2>/dev/null
"$OUT/settings_route_forced" 2>/dev/null
"$OUT/settings_route_plain" 2>/dev/null

# CY-72 pure boot plans and restart ordering, both board variants.
g++ -O2 -std=c++17 -Wall -Wextra -Werror $INC -DCYD_BOARD_JC2432W328C tests/boot_plan_test.cpp -o "$OUT/boot_plan_wired"
g++ -O2 -std=c++17 -Wall -Wextra -Werror $INC tests/boot_plan_test.cpp -o "$OUT/boot_plan_plain"
"$OUT/boot_plan_wired"
"$OUT/boot_plan_plain"

# CY-84: exercise the production restart glue, not only template fake ops.
for board in plain wired; do
  FLAGS=""
  if [ "$board" = wired ]; then FLAGS="-DCYD_BOARD_JC2432W328C"; fi
  g++ -O2 -std=c++17 -Wall -Werror -Wno-misleading-indentation -Itests/output_switch_shim -Itests/settings_shim -Itests/speaker_shim -Ifirmware/cydpod_player/app $FLAGS tests/output_switch_test.cpp -o "$OUT/output_switch_$board"
  "$OUT/output_switch_$board"
done
