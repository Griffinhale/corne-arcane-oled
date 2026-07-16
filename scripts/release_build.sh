#!/usr/bin/env sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
qmk_root=${QMK_ROOT:-"$HOME/src/vial-qmk"}
keymap="$qmk_root/keyboards/crkbd/keymaps/griffin_arcane"
build="$qmk_root/.build/crkbd_rev1_griffin_arcane_rp2040_ce"
out="$root/artifacts/release"

mkdir -p "$out"
"$root/host/install_firmware.sh" "$keymap"

stage() {
    name=$1
    cp "$build.elf" "$out/$name.elf"
    cp "$build.uf2" "$out/$name.uf2"
    if [ -f "$build.map" ]; then
        cp "$build.map" "$out/$name.map"
    fi
}

cd "$qmk_root"
qmk clean
qmk compile -kb crkbd/rev1 -km griffin_arcane -e CONVERT_TO=rp2040_ce
stage griffin_arcane-release

qmk clean
qmk compile -kb crkbd/rev1 -km griffin_arcane \
    -e CONVERT_TO=rp2040_ce -e ARCANE_DIAGNOSTICS=yes
stage griffin_arcane-diagnostic

sha256sum "$out"/griffin_arcane-*.elf "$out"/griffin_arcane-*.uf2
