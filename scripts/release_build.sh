#!/usr/bin/env sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
qmk_root=${QMK_ROOT:-"$HOME/src/vial-qmk"}
keymap="$qmk_root/keyboards/crkbd/keymaps/griffin_arcane"
build="$qmk_root/.build/crkbd_rev1_griffin_arcane_rp2040_ce"
out="$root/artifacts/release"
pin_file="$root/VIAL_QMK_REVISION"

expected_revision=$(tr -d '[:space:]' < "$pin_file")
actual_revision=$(git -C "$qmk_root" rev-parse HEAD 2>/dev/null) || {
    echo "FAIL release-build: QMK_ROOT is not a readable git checkout: $qmk_root" >&2
    exit 1
}
if [ "$actual_revision" != "$expected_revision" ] && [ "${ALLOW_UNPINNED_QMK:-0}" != 1 ]; then
    echo "FAIL release-build: Vial-QMK revision mismatch" >&2
    echo "  expected: $expected_revision (VIAL_QMK_REVISION)" >&2
    echo "  actual:   $actual_revision ($qmk_root)" >&2
    echo "Check out the pinned revision, intentionally update VIAL_QMK_REVISION, or use" >&2
    echo "ALLOW_UNPINNED_QMK=1 make release-build for a non-acceptance development build." >&2
    exit 1
fi

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

# Unflashed HP pacing candidates. These differ only by SIM_MAX_HP and its
# renderer geometry; physical A/B acceptance selects one later.
qmk clean
qmk compile -kb crkbd/rev1 -km griffin_arcane \
    -e CONVERT_TO=rp2040_ce -e ARCANE_HP=8
stage griffin_arcane-hp8-candidate

qmk clean
qmk compile -kb crkbd/rev1 -km griffin_arcane \
    -e CONVERT_TO=rp2040_ce -e ARCANE_HP=10
stage griffin_arcane-hp10-candidate

sha256sum "$out"/griffin_arcane-*.elf "$out"/griffin_arcane-*.uf2
