#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p tests/out/cpu_stats
g++ -O2 -std=c++17 -Wall -Wextra -Werror tests/cpu_stats_test.cpp -o tests/out/cpu_stats/cpu_stats_test
tests/out/cpu_stats/cpu_stats_test
