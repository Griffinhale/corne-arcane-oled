#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "Usage: $0 <keymap> <left|right>" >&2
  echo "Example: $0 griffin_anim left" >&2
  exit 2
fi

KEYMAP="$1"
SIDE="$2"
QMK_HOME="${QMK_HOME:-$HOME/src/vial-qmk}"
CONVERT_TO="${CONVERT_TO:-rp2040_ce}"

case "$SIDE" in
  left|right) ;;
  *) echo "side must be left or right" >&2; exit 2 ;;
esac

cd "$QMK_HOME"

echo "About to flash: keymap=$KEYMAP side=$SIDE converter=$CONVERT_TO"
echo "Physical sequence: USB unplugged, TRRS disconnected, plug ONLY the $SIDE half, enter RPI-RP2 bootloader."
read -r -p "Press Enter to start qmk flash... "

qmk flash -kb crkbd/rev1 -km "$KEYMAP" -e CONVERT_TO="$CONVERT_TO" -bl "uf2-split-$SIDE"
