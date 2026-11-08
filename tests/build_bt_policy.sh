#!/bin/sh
# CY-114: host tests for the Bluetooth connect policy (bt_reconnect_policy.h) and the pairing
# session (pairing_session.h). Pure logic, no hardware, no Arduino.
set -eu
cd "$(dirname "$0")/.."
OUT=tests/out/bt
mkdir -p "$OUT"
for t in bt_policy pairing_session bt_rate_policy; do
  g++ -O2 -std=c++17 -Wall -Wextra -Werror tests/${t}_test.cpp -o "$OUT/${t}_test"
  "$OUT/${t}_test"
done
