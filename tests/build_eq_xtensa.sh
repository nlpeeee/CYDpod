#!/bin/sh
# EQ-5BAND: assemble the ESP32 EQ stage loop (eq_xtensa.h) with the pinned toolchain,
# disassemble it, and interpret that machine code (tests/eq_xtensa_sim.py) against
# vectors from the C++ reference. Run from WSL; the toolchain is the Windows build
# under .deps, reached through WSL interop. Stock qemu-xtensa cores lack MUL32_HIGH
# (mulsh), so they cannot run this loop. Instruction counts here are not timings.
set -eu
cd "$(dirname "$0")/.."
OUT=tests/out/eq
mkdir -p "$OUT"
TC=$(ls -d .deps/arduino/packages/esp32/tools/esp-x32/*/bin | head -1)
"$TC/xtensa-esp32-elf-g++.exe" -O2 -std=gnu++17 -mlongcalls -c -Ifirmware/cydpod_player/app tests/eq_xtensa_obj.cpp -o "$OUT/eq_xtensa.o"
"$TC/xtensa-esp32-elf-objdump.exe" -d -j .iram1.cydeq_stage_xtensa "$OUT/eq_xtensa.o" | tr -d '\r' > "$OUT/eq_xtensa.dis"
g++ -O2 -std=c++17 -Wall -Wextra -Werror -Ifirmware/cydpod_player/app tests/eq_vectors.cpp -o "$OUT/eq_vectors"
"$OUT/eq_vectors" "$OUT/eq_vectors.txt"
python3 -I tests/eq_xtensa_sim.py "$OUT/eq_xtensa.dis" "$OUT/eq_vectors.txt"
