#!/usr/bin/env bash
set -euo pipefail

QMK_HOME="${QMK_HOME:-$HOME/src/vial-qmk}"
KEYMAP="${1:-griffin}"

cd "$QMK_HOME"

if [[ -d "keyboards/crkbd/keymaps/$KEYMAP" ]]; then
  echo "Refusing to overwrite existing keymap: $KEYMAP" >&2
  echo "Move it aside first or choose another name." >&2
  exit 1
fi

cp -a keyboards/crkbd/keymaps/default "keyboards/crkbd/keymaps/$KEYMAP"
cp keyboards/crkbd/keymaps/vial/vial.json "keyboards/crkbd/keymaps/$KEYMAP/vial.json"

python3 - "$KEYMAP" <<'PY'
from pathlib import Path
import json
import re
import sys

keymap = sys.argv[1]
km = Path('keyboards/crkbd/keymaps') / keymap

cfg = km / 'config.h'
s = cfg.read_text() if cfg.exists() else '#pragma once\n'
s = '\n'.join(line for line in s.splitlines() if 'USE_SERIAL_PD2' not in line) + '\n'

adds = [
    '#define VIAL_KEYBOARD_UID {0x3B, 0x6B, 0xA0, 0x29, 0x80, 0x56, 0xED, 0xD1}',
    '#define VIAL_UNLOCK_COMBO_ROWS {0, 0}',
    '#define VIAL_UNLOCK_COMBO_COLS {0, 1}',
    '#undef DYNAMIC_KEYMAP_LAYER_COUNT',
    '#define DYNAMIC_KEYMAP_LAYER_COUNT 4',
    '#define TAPPING_TERM 180',
]
for line in adds:
    if line not in s:
        s += line + '\n'
cfg.write_text(s)

rules = km / 'rules.mk'
r = rules.read_text() if rules.exists() else ''
r = re.sub(r'^\s*(VIA_ENABLE|VIAL_ENABLE|LTO_ENABLE)\s*=.*\n?', '', r, flags=re.M)
r += '\nVIA_ENABLE = yes\nVIAL_ENABLE = yes\nLTO_ENABLE = yes\n'
rules.write_text(r)

vj = km / 'vial.json'
d = json.loads(vj.read_text())
d.pop('lighting', None)
vj.write_text(json.dumps(d, indent=2) + '\n')

print(f"Created keyboards/crkbd/keymaps/{keymap}")
PY
