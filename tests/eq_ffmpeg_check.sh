#!/bin/sh
# EQ-5BAND: independent check of the firmware EQ maths against FFmpeg's RBJ
# `equalizer` filter, plus A/B listening files. Run from Git Bash on Windows with
# ffmpeg on PATH and WSL Debian (for g++):
#   sh tests/eq_ffmpeg_check.sh [music-file]
# 1. Builds tests/eq_render.cpp (the C++ reference the Xtensa loop matches bit for bit).
# 2. For each preset and two extreme settings, renders -20 dBFS pink noise through
#    FFmpeg's equalizer chain (same internal gains, Q and output gain), the same design
#    in double precision, and the firmware fixed-point path (limiter off). FFmpeg vs
#    double checks the filter maths (expect 0-1 LSB); fixed vs double checks precision.
# 3. Writes tests/out/eq/ab/*.wav: bypass, then old (first shipped design) and new for
#    each preset, from [music-file] if given (any format FFmpeg reads), else the noise.
set -eu
cd "$(dirname "$0")/.."
OUT=tests/out/eq
mkdir -p "$OUT/ab"
WT=$(MSYS_NO_PATHCONV=1 wsl.exe -d Debian wslpath -a "$(pwd -W)" | tr -d '\r')
wsl_run() { MSYS_NO_PATHCONV=1 wsl.exe -d Debian --cd "$WT" -- "$@"; }
wsl_run g++ -O2 -std=c++17 -Wall -Wextra -Werror -Ifirmware/cydpod_player/app tests/eq_render.cpp -o "$OUT/eq_render"
RAW="-f s16le -ar 44100 -ac 2"
ffmpeg -v error -y -f lavfi -i "anoisesrc=color=pink:amplitude=0.1:duration=8:sample_rate=44100:seed=7" -ac 2 $RAW "$OUT/noise.raw"
status=0
compare() {  # label a b maxLSB
  line=$(wsl_run "$OUT/eq_render" compare "$2" "$3" "$4") || status=1
  printf '%-26s %s\n' "$1" "$(printf '%s' "$line" | tr -d '\r')"
}
check() {  # name g0..g4
  name=$1; shift
  filter=$(wsl_run "$OUT/eq_render" filter "$@" | tr -d '\r')
  ffmpeg -v error -y $RAW -i "$OUT/noise.raw" -af "$filter" -c:a pcm_s16le $RAW "$OUT/ff-$name.raw"
  wsl_run "$OUT/eq_render" double "$@" "$OUT/noise.raw" "$OUT/dbl-$name.raw"
  wsl_run "$OUT/eq_render" run "$@" "$OUT/noise.raw" "$OUT/cyd-$name.raw" nolimit
  compare "$name ffmpeg vs double" "$OUT/ff-$name.raw" "$OUT/dbl-$name.raw" 1
  compare "$name fixed vs double" "$OUT/cyd-$name.raw" "$OUT/dbl-$name.raw" 8
}
check bass 6 6 2 0 0
check vocal -2 0 4 4 2
check treble 0 0 2 6 6
check loudness 6 4 0 2 6
check all-12 -12 -12 -12 -12 -12
check smile 10 4 -6 4 10
if [ "$status" = 0 ]; then echo "PASS: FFmpeg's RBJ equalizer matches the design within 1 LSB, and the fixed-point path is within 8 LSB of it, on every setting"
else echo "FAIL: a comparison above exceeded its bound"; fi

# A/B listening files.
SRC="$OUT/noise.raw"
if [ $# -ge 1 ]; then ffmpeg -v error -y -i "$1" -t 60 $RAW "$OUT/music.raw"; SRC="$OUT/music.raw"; fi
ffmpeg -v error -y $RAW -i "$SRC" "$OUT/ab/0-bypass.wav"
for p in "bass 6 6 2 0 0 8 4 0 0 0" "vocal -2 0 4 4 2 -2 0 4 4 0" "treble 0 0 2 6 6 0 0 0 4 8" "loudness 6 4 0 2 6 6 2 0 2 6"; do
  set -- $p
  name=$1; new="$2 $3 $4 $5 $6"; old="$7 $8 $9 ${10} ${11}"
  wsl_run "$OUT/eq_render" legacy $old "$SRC" "$OUT/ab-$name-old.raw"
  wsl_run "$OUT/eq_render" run $new "$SRC" "$OUT/ab-$name-new.raw"
  ffmpeg -v error -y $RAW -i "$OUT/ab-$name-old.raw" "$OUT/ab/$name-old.wav"
  ffmpeg -v error -y $RAW -i "$OUT/ab-$name-new.raw" "$OUT/ab/$name-new.wav"
done
echo "A/B files: $OUT/ab (old = first shipped design and presets, new = this design)"
exit $status
