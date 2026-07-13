#!/usr/bin/env sh
# Refresh the committed firmware/ snapshot from the live QMK keymap tree.
# The repo keeps a real copy (not the old symlink) so it is self-contained and
# pushable; run this after editing the live tree to update the snapshot, then
# commit the diff.
set -e
SRC="${1:-$HOME/src/vial-qmk/keyboards/crkbd/keymaps/griffin_anim}"
DST="$(cd "$(dirname "$0")" && pwd)/firmware"
if [ ! -d "$SRC" ]; then echo "live source not found: $SRC" >&2; exit 1; fi
rsync -a --delete \
  --exclude 'test_runner' --exclude 'preview' --exclude '.noalloc.o' \
  --exclude '*.o' --exclude '*.uf2' \
  "$SRC"/ "$DST"/
echo "synced $SRC -> $DST"
