#!/usr/bin/env sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
qmk_root=${QMK_ROOT:-"$HOME/src/vial-qmk"}
build="$qmk_root/.build"
anim="$qmk_root/keyboards/crkbd/keymaps/griffin_anim"
host="$qmk_root/keyboards/crkbd/keymaps/griffin_hostoled"
out="$root/artifacts/m13"

mkdir -p "$out"

sync_anim() {
    rsync -a --delete \
        --exclude test_runner --exclude preview --exclude visual_runner \
        --exclude gallery --exclude .noalloc.o \
        --exclude '*.o' --exclude '*.elf' --exclude '*.uf2' \
        "$root/firmware/" "$anim/"
}

stage() {
    keymap=$1
    name=$2
    base="$build/crkbd_rev1_${keymap}_rp2040_ce"
    cp "$base.elf" "$out/$name.elf"
    cp "$base.uf2" "$out/$name.uf2"
    if [ -f "$base.map" ]; then cp "$base.map" "$out/$name.map"; fi
}

build_one() {
    keymap=$1
    name=$2
    diagnostics=$3
    qmk clean
    if [ "$diagnostics" = yes ]; then
        qmk compile -kb crkbd/rev1 -km "$keymap" \
            -e CONVERT_TO=rp2040_ce -e ARCANE_M13=yes \
            -e ARCANE_DIAGNOSTICS=yes
    else
        qmk compile -kb crkbd/rev1 -km "$keymap" \
            -e CONVERT_TO=rp2040_ce -e ARCANE_M13=yes
    fi
    stage "$keymap" "$name"
}

cd "$qmk_root"
sync_anim
build_one griffin_anim griffin_anim-m13-vial-release no
build_one griffin_anim griffin_anim-m13-vial-diagnostic yes

"$root/host/install_firmware.sh" "$host"
build_one griffin_hostoled griffin_hostoled-m13-release no
build_one griffin_hostoled griffin_hostoled-m13-diagnostic yes

sha256sum "$out"/griffin_*-m13-*.elf "$out"/griffin_*-m13-*.uf2
