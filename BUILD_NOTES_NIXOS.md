# Corne Arcane OLED — Build Notes (NixOS) & Milestone Status

This project builds on **NixOS 26.05**, not the Debian 13 the `spike1/` docs assume.
Ignore the spike1 `apt` / Vial-AppImage / `keyd.rvaiya` steps here.

## Environment

- QMK tree: `~/src/vial-qmk` (branch `vial`, submodules initialized). Our work is
  on branch **`arcane-oled-m1`**.
- Animations source: `~/src/qmk-animations` (reference only; not used by M1).
- Toolchain: **user-scope, no sudo** via `~/src/vial-qmk/shell.nix` →
  `nix-shell` gives `qmk 1.2.0` + `python3` + `arm-none-eabi-gcc`.
- Physical Corne (`4653:0001 foostan Corne`) is attached to this machine.
  Flashing needs no sudo (udisks2 automounts `RPI-RP2`).
- Durable system config lives in `./corne.nix` — copy to
  `/etc/nixos/modules/corne.nix`, import it, `rebuild`. **Not yet applied.**

## Keymaps

- `griffin` — stable Vial baseline (reconstructed from spike1 scripts). Do not
  experiment here.
- `griffin_anim` — firmware OLED experiments (Vial on). **M1 lives here.**
- `griffin_hostoled` — reserved for later Raw HID host work (not created yet).

## Build & flash

```bash
cd ~/src/vial-qmk && nix-shell
qmk compile -kb crkbd/rev1 -km griffin_anim -e CONVERT_TO=rp2040_ce
# one half at a time; never hot-plug TRRS:
qmk flash -kb crkbd/rev1 -km griffin_anim -e CONVERT_TO=rp2040_ce -bl uf2-split-left
qmk flash -kb crkbd/rev1 -km griffin_anim -e CONVERT_TO=rp2040_ce -bl uf2-split-right
```
Reassemble: USB into left half only + TRRS connected.

## Milestone status

- **M0 — Baseline** ✅ reconstructed; `griffin` + `griffin_anim` compile clean;
  timestamped backups in `~/corne-*.tar.gz`.
- **M1 — Actor + physical-side proof** ✅ verified on hardware. Portrait wizard
  per half; each half reacts only to its own typing (cast pose); both upright,
  dueling toward the centre gap. Details: `keyboards/crkbd/keymaps/griffin_anim/README.md`.
- **M2 — Deterministic world loop** ⏭️ next. Fixed integer tick (~25 Hz), sim
  state separated from presentation state, bounded event queue consumed outside
  the key path, render from a stable snapshot (render cadence must not change
  outcomes).

## Hardware notes learned this session

- OLED panels are landscape-mounted; treat as portrait via `OLED_ROTATION_270`
  on both halves (they are mounted the same way, so both use the same rotation).
- Per-half local input requires hooking **both** `matrix_scan_user` (master) and
  `matrix_slave_scan_user` (slave). `process_record_*` runs on the master only.
