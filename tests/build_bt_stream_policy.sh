#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p tests/out/bt
g++ -O2 -std=c++17 -Wall -Wextra -Werror tests/bt_stream_policy_test.cpp -o tests/out/bt/bt_stream_policy_test
tests/out/bt/bt_stream_policy_test
