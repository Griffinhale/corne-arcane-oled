#!/usr/bin/env sh
# Materialize the accepted firmware fallback as the isolated host keymap.
set -eu
root="$(cd "$(dirname "$0")/.." && pwd)"
src="$root/firmware"
dst="${1:-$HOME/src/vial-qmk/keyboards/crkbd/keymaps/griffin_hostoled}"

if [ ! -d "$dst" ]; then
    echo "host keymap directory not found: $dst" >&2
    exit 1
fi

rsync -a --delete \
    --exclude test_runner --exclude preview --exclude .noalloc.o \
    --exclude '*.o' --exclude '*.uf2' \
    "$src/" "$dst/"
cp "$root/host/firmware/rules.mk" "$dst/rules.mk"
echo "installed M8 firmware -> $dst"
