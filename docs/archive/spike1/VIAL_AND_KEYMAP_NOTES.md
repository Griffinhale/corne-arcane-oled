# Vial and Keymap Notes

## What Vial can and cannot do

Vial can:

- Edit key assignments live.
- Change layers.
- Assign keycodes such as `KC_TRNS`, `LCtrl`, `Right Alt`, `Space`, etc.
- Save/export a layout file for backup.

Vial cannot:

- Change low-level split serial pins.
- Change RP2040 converter target.
- Fix a bad firmware build.
- Edit OLED C rendering code.
- Make a non-Vial firmware expose Vial config features.

## Vial persistence trap

Vial changes apply live. “Save” in the GUI usually means saving a backup/export file, not applying the layout to the keyboard.

Reflashing firmware does not always clear the dynamic keymap stored by VIA/Vial. EEPROM/persistent storage can survive a firmware write. If keymap state gets weird, use `QK_CLEAR_EEPROM` / `EE_CLR` or Bootmagic. Be careful: clearing EEPROM can wipe dynamic layout state and any handedness stored in EEPROM if using `EE_HANDS`.

## Your layout issues discovered

Original thumb cluster was roughly:

```text
Win | Layer | Space    Enter | Layer | Alt
```

Problem:

```text
Hitting Enter when intending Space
```

Preferred fix in Vial:

```text
Layer 0:
  current Enter thumb -> Space
  current Right Alt thumb -> Enter
```

Earlier Ctrl issue:

- Ctrl was on a layer: `Layer + outer-left-middle key`.
- Holding the layer changed what `C` and `V` sent.
- Fix is to set the `C` and `V` physical positions on that Ctrl layer to transparent.

Vial labels for transparent may appear as:

```text
Transparent
TRNS
KC_TRNS
_______
```

## Debian diagnostic tools

The Debian package is named `keyd`, but the command is:

```bash
sudo keyd.rvaiya monitor
```

Your Corne appeared as:

```text
foostan Corne   4653:0001:...  enter down/up
```

Useful for confirming physical keys without guessing.

## Vial/VIA Linux access

Rules used during setup included both Vial-specific and broad hidraw rules. Current firmware works with Vial, so this is less important now, but if the app stops seeing the board, check:

```bash
ls -l /dev/hidraw*
for h in /dev/hidraw*; do
  echo "=== $h ==="
  udevadm info -q property -n "$h" | /usr/bin/grep -E 'HID_NAME|ID_VENDOR_ID|ID_MODEL_ID|ID_SERIAL|DEVNAME' || true
done
```

## Vial app path

```bash
~/.local/bin/vial
```

Vial Web was also usable through Chromium, but the AppImage was installed and working.

