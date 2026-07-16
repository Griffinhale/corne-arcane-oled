#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <crab|demon|music-bars> [keymap]" >&2
  echo "Example: $0 demon griffin_anim" >&2
  exit 2
fi

ANIM="$1"
KEYMAP="${2:-griffin_anim}"
QMK_HOME="${QMK_HOME:-$HOME/src/vial-qmk}"
ANIM_SRC="${ANIM_SRC:-$HOME/src/qmk-animations/animations}"
KEYMAP_DIR="$QMK_HOME/keyboards/crkbd/keymaps/$KEYMAP"

case "$ANIM" in
  crab|demon|music-bars) ;;
  *) echo "Known simple animations: crab, demon, music-bars" >&2; exit 2 ;;
esac

[[ -d "$KEYMAP_DIR" ]] || { echo "Missing keymap dir: $KEYMAP_DIR" >&2; exit 1; }
[[ -f "$ANIM_SRC/animation-utils.c" ]] || { echo "Missing $ANIM_SRC/animation-utils.c" >&2; exit 1; }
[[ -f "$ANIM_SRC/$ANIM.c" ]] || { echo "Missing $ANIM_SRC/$ANIM.c" >&2; exit 1; }

cp "$ANIM_SRC/animation-utils.c" "$KEYMAP_DIR/"
cp "$ANIM_SRC/$ANIM.c" "$KEYMAP_DIR/"

python3 - "$KEYMAP_DIR/keymap.c" "$ANIM" <<'PY'
from pathlib import Path
import re
import sys

path = Path(sys.argv[1])
anim = sys.argv[2]
s = path.read_text()

pattern = r'#include "(?:crab|demon|music-bars)\.c"'
replacement = f'#include "{anim}.c"'

if re.search(pattern, s):
    s = re.sub(pattern, replacement, s)
else:
    block = f'''

#ifdef OLED_ENABLE

#define ANIM_INVERT false
#define ANIM_RENDER_WPM true
#define FAST_TYPE_WPM 45

#include "{anim}.c"

void oled_render_logo(void) {{
    oled_render_anim();
}}

#endif
'''
    s += block

path.write_text(s)
print(f"Now using {anim}.c in {path}")
PY

echo "Sanity check:"
grep -nE '#include "(crab|demon|music-bars)\.c"|oled_render_logo|oled_task_user' "$KEYMAP_DIR/keymap.c" || true
