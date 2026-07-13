#!/usr/bin/env bash
set -euo pipefail

KEYMAP="${1:-griffin_anim}"
QMK_HOME="${QMK_HOME:-$HOME/src/vial-qmk}"
OUT_DIR="${OUT_DIR:-$HOME}"
STAMP="$(date +%Y%m%d-%H%M%S)"

cd "$QMK_HOME"

tar -czf "$OUT_DIR/corne-${KEYMAP}-${STAMP}.tar.gz" "keyboards/crkbd/keymaps/$KEYMAP"
echo "Wrote $OUT_DIR/corne-${KEYMAP}-${STAMP}.tar.gz"
