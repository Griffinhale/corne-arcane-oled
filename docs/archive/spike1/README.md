# Corne v3 RP2040 / Vial-QMK Working Notes

Date: 2026-07-10

This folder captures the known-good state and the path forward for OLED animation experiments on Griffin's Corne v3 split keyboard.

## Current known-good state

Hardware and OS:

- Keyboard: Corne v3 / `crkbd/rev1`-style split keyboard.
- Controllers: RP2040-class controllers, confirmed by `RPI-RP2` bootloader drive.
- Firmware format: `.uf2`.
- Current working build target: `crkbd/rev1` with `CONVERT_TO=rp2040_ce`.
- OS: Debian 13.
- Vial app: Linux AppImage installed at `~/.local/bin/vial`.
- QMK source repo: `~/src/vial-qmk`, branch `vial`.
- Stable working keymap: `keyboards/crkbd/keymaps/griffin`.
- OLED animation experiment keymap: `keyboards/crkbd/keymaps/griffin_anim`.
- Host-driven OLED experiment keymap: `keyboards/crkbd/keymaps/griffin_hostoled`.

Important behavior learned:

- The stock `crkbd/rev1` `-km default` build works over the TRRS split link.
- The stock `crkbd/rev1` `-km vial` build did not work correctly for this board as a split, even after EEPROM clear.
- The working Vial build was created by copying the working `default` keymap and adding minimal Vial support, rather than using the stock `vial` keymap directly.
- `qmk-animations` crab animation worked in `griffin_anim`.
- Vial changes apply live to the keyboard. “Save” in Vial generally means saving/exporting a layout file, not applying the change.

## Do not repeat these traps

- Do not hot-plug the TRRS cable. USB power off first.
- Do not judge the split by plugging USB directly into the right half if using `MASTER_LEFT`-style firmware. The direct-right test can make the right half act like the left half. The real test is USB into left half with TRRS connected.
- Do not `cat` a `.uf2` file. It is binary.
- Do not use the stock `-km vial` keymap as the baseline. Use `griffin` / `griffin_anim` derived from `default`.
- Do not use Atyu directly to manage this Corne. It is Satisfaction75-oriented and not set up for Debian, RP2040 converter flags, or split-left/split-right UF2 flashing.
- Do not mix Vial and custom Raw HID host-OLED code in the same branch unless deliberately solving that integration. Vial/VIA already use Raw HID.

## Stable commands

Compile stable firmware:

```bash
cd ~/src/vial-qmk
qmk clean
qmk compile -kb crkbd/rev1 -km griffin -e CONVERT_TO=rp2040_ce
```

Flash stable firmware, one half at a time:

```bash
cd ~/src/vial-qmk
qmk flash -kb crkbd/rev1 -km griffin -e CONVERT_TO=rp2040_ce -bl uf2-split-left
qmk flash -kb crkbd/rev1 -km griffin -e CONVERT_TO=rp2040_ce -bl uf2-split-right
```

Compile animation firmware:

```bash
cd ~/src/vial-qmk
qmk clean
qmk compile -kb crkbd/rev1 -km griffin_anim -e CONVERT_TO=rp2040_ce
```

Flash animation firmware:

```bash
cd ~/src/vial-qmk
qmk flash -kb crkbd/rev1 -km griffin_anim -e CONVERT_TO=rp2040_ce -bl uf2-split-left
qmk flash -kb crkbd/rev1 -km griffin_anim -e CONVERT_TO=rp2040_ce -bl uf2-split-right
```

## Normal reassembly order after flashing

1. USB unplugged.
2. TRRS connected firmly.
3. USB into left half only.
4. Wait a few seconds.
5. Test both halves.
6. Open Vial only after both halves type.

## Testing utilities

Monitor key events:

```bash
sudo keyd.rvaiya monitor
```

Inspect USB and bootloader state:

```bash
watch -n 0.25 'echo USB; lsusb | sort; echo; echo SERIAL; ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || true; echo; echo DRIVES; lsblk -o NAME,LABEL,SIZE,MODEL,MOUNTPOINTS'
```

