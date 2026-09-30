#!/usr/bin/env sh
# Materialize the current unified keymap in an existing Vial-QMK checkout.
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
src="$root/firmware"
qmk_root=${QMK_ROOT:-"$HOME/src/vial-qmk"}
dst=${1:-"$qmk_root/keyboards/crkbd/keymaps/griffin_arcane"}

# Refuse to invent a tree: a missing checkout would otherwise get a fake
# keymaps directory and a success message.
if [ ! -d "$qmk_root/quantum" ]; then
    echo "FAIL install_firmware: no Vial-QMK checkout at $qmk_root" >&2
    echo "Clone it (see docs/flashing.md) or set QMK_ROOT to the checkout." >&2
    exit 1
fi
if [ ! -d "$(dirname "$dst")" ]; then
    echo "FAIL install_firmware: $(dirname "$dst") does not exist" >&2
    exit 1
fi

mkdir -p "$dst"
rsync -a --delete \
    --exclude mechanics_runner --exclude visual_runner \
    --exclude gallery --exclude .noalloc.o \
    --exclude '*.o' --exclude '*.elf' --exclude '*.uf2' \
    "$src/" "$dst/"

echo "installed current firmware -> $dst"
