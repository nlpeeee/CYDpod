#!/bin/sh
# Stages a patched copy of the pristine vendored AudioTools src tree: apply_audiotools_overlay.sh <dest>.
# vendor/ and its inventory stay untouched; patches/audio-tools/*.patch are applied to the copy only.
set -eu
cd "$(dirname "$0")/.."
DEST=${1:?usage: apply_audiotools_overlay.sh <dest>}
rm -rf "$DEST"; mkdir -p "$DEST"
cp -r vendor/arduino-audio-tools/src "$DEST/"
for p in patches/audio-tools/*.patch; do (cd "$DEST" && patch -p1 --no-backup-if-mismatch -s) < "$p"; done
