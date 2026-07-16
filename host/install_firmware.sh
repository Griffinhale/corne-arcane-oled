#!/usr/bin/env sh
# Materialize the current unified keymap in an existing Vial-QMK checkout.
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
src="$root/firmware"
qmk_root=${QMK_ROOT:-"$HOME/src/vial-qmk"}
dst=${1:-"$qmk_root/keyboards/crkbd/keymaps/griffin_arcane"}

mkdir -p "$dst"
rsync -a --delete \
    --exclude mechanics_runner --exclude visual_runner \
    --exclude gallery --exclude .noalloc.o \
    --exclude '*.o' --exclude '*.elf' --exclude '*.uf2' \
    "$src/" "$dst/"

echo "installed current firmware -> $dst"
