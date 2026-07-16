#!/usr/bin/env bash
set -euo pipefail

KEYMAP="${1:-griffin_anim}"
QMK_HOME="${QMK_HOME:-$HOME/src/vial-qmk}"
CONVERT_TO="${CONVERT_TO:-rp2040_ce}"

cd "$QMK_HOME"
qmk clean
qmk compile -kb crkbd/rev1 -km "$KEYMAP" -e CONVERT_TO="$CONVERT_TO"

echo
echo "Expected artifact pattern:"
ls -lh .build/crkbd_rev1_${KEYMAP}_${CONVERT_TO}.uf2 ./crkbd_rev1_${KEYMAP}_${CONVERT_TO}.uf2 2>/dev/null || true
