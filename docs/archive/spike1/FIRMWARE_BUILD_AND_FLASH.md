# Firmware Build and Flash Notes

## Repositories

Working firmware tree:

```text
~/src/vial-qmk
```

Branch observed:

```text
vial
```

Main keymaps:

```text
keyboards/crkbd/keymaps/griffin          stable Vial build
keyboards/crkbd/keymaps/griffin_anim     firmware OLED animation experiments
keyboards/crkbd/keymaps/griffin_hostoled host-driven OLED experiments, not Vial
```

## Target

```text
Keyboard:  crkbd/rev1
Converter: rp2040_ce
Bootloader: uf2-split-left / uf2-split-right
```

## Why stock Vial was abandoned

Observed path:

1. Stock `-km vial` compiled and flashed.
2. Vial could open.
3. Right half did not work as slave over TRRS.
4. EEPROM clear disconnected/reconnected but did not fix it.
5. Stock `-km default` made both halves work over TRRS.
6. A custom Vial keymap derived from `default` worked.

Working conclusion:

```text
Use default-derived minimal Vial keymap, not stock crkbd/keymaps/vial.
```

The stock Vial keymap had its own config/rules, including split serial and OLED/RGB settings. The working board path is not “stock Vial plus tweaks”; it is “default keymap plus minimal Vial enablement.”

## Rebuilding the working `griffin` keymap from scratch

Use this only if the `griffin` keymap is lost or poisoned. It recreates the structure that worked: copy `default`, add `vial.json`, add minimal Vial rules, avoid stock Vial's suspicious extras.

```bash
cd ~/src/vial-qmk

rm -rf keyboards/crkbd/keymaps/griffin
cp -a keyboards/crkbd/keymaps/default keyboards/crkbd/keymaps/griffin
cp keyboards/crkbd/keymaps/vial/vial.json keyboards/crkbd/keymaps/griffin/vial.json

python3 - <<'PY'
from pathlib import Path
import json
import re

km = Path('keyboards/crkbd/keymaps/griffin')

cfg = km / 'config.h'
s = cfg.read_text() if cfg.exists() else '#pragma once\n'
# Do not carry the stock Vial USE_SERIAL_PD2 override into this keymap.
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
# Avoid advertising stock qmk_rgblight while using the default-derived Corne config.
d.pop('lighting', None)
vj.write_text(json.dumps(d, indent=2) + '\n')
PY
```

Compile:

```bash
qmk clean
qmk compile -kb crkbd/rev1 -km griffin -e CONVERT_TO=rp2040_ce
```

Flash both halves:

```bash
qmk flash -kb crkbd/rev1 -km griffin -e CONVERT_TO=rp2040_ce -bl uf2-split-left
qmk flash -kb crkbd/rev1 -km griffin -e CONVERT_TO=rp2040_ce -bl uf2-split-right
```

## Compile and flash helper scripts

This deliverables folder includes:

```text
scripts/compile_keymap.sh
scripts/flash_keymap.sh
scripts/backup_keymap.sh
scripts/inspect_corne_usb.sh
scripts/switch_qmk_animation.sh
scripts/create_griffin_minimal_vial.sh
```

Copy them into a project repo if desired, or run them from this folder.

## Known warning signs

If compile artifact name says `vial`, you are compiling stock Vial:

```text
crkbd_rev1_vial_rp2040_ce.uf2
```

If compile artifact name says `griffin`, you are compiling the working custom keymap:

```text
crkbd_rev1_griffin_rp2040_ce.uf2
```

For animation branch:

```text
crkbd_rev1_griffin_anim_rp2040_ce.uf2
```

Always check the artifact name before flashing if something feels off.

