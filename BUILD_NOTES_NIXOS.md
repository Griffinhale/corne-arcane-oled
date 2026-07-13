# Corne Arcane OLED — Build Notes (NixOS) & Milestone Status

This project builds on **NixOS 26.05**, not the Debian 13 the `spike1/` docs assume.
Ignore the spike1 `apt` / Vial-AppImage / `keyd.rvaiya` steps here.

## Environment

- QMK tree: `~/src/vial-qmk` (branch `vial`, submodules initialized). Our work is
  on branch **`arcane-oled-m1`**.
- Animations source: `~/src/qmk-animations` (reference only; not used by M1).
- Toolchain: **user-scope, no sudo**. `qmk`, `python3`, and
  `arm-none-eabi-gcc` are available directly on `PATH`; the QMK tree no longer
  contains the older documented `shell.nix` entry point.
- Physical Corne (`4653:0001 foostan Corne`) is attached to this machine.
  Flashing needs no sudo (udisks2 automounts `RPI-RP2`).
- Durable system config lives in `./corne.nix`. Import it **directly from this
  checkout** so the relative packaged host sources remain available, then
  rebuild. **Not yet applied.**

## Keymaps

- `griffin` — stable Vial baseline (reconstructed from spike1 scripts). Do not
  experiment here.
- `griffin_anim` — firmware OLED experiments (Vial on). Its compiled default
  layout is captured from `../corne-arcane.vil`.
- `griffin_hostoled` — complete offline duel plus M8 custom Raw HID and M9's
  hybrid Archive renderer. Vial/VIA are off and it shares the same compiled
  default as `griffin_anim`.

## NixOS import and service

Add the checkout path directly to `/etc/nixos/configuration.nix`:

```nix
imports = [
  /home/griffin/dev/corne-arcane-oled/corne.nix
];
```

Then run the normal `sudo nixos-rebuild switch`. The module builds the daemon,
PyGObject/Gio runtime, and KWin bridge from `host/`, and enables the restarting
user service at `graphical-session.target`.

```bash
systemctl --user status corne-arcane-host
journalctl --user -u corne-arcane-host -f
systemctl --user restart corne-arcane-host

# Diagnostic fixed-scene override (automatic focus arbitration is disabled):
systemctl --user stop corne-arcane-host
corne-arcane-host --scene archive --verbose

# Temporary disable; set services.corne-arcane-host.enable = false for durable disable:
systemctl --user disable --now corne-arcane-host
```

## Build & flash

```bash
cd ~/src/vial-qmk
qmk compile -kb crkbd/rev1 -km griffin_anim -e CONVERT_TO=rp2040_ce
# one half at a time; never hot-plug TRRS:
qmk flash -kb crkbd/rev1 -km griffin_anim -e CONVERT_TO=rp2040_ce -bl uf2-split-left
qmk flash -kb crkbd/rev1 -km griffin_anim -e CONVERT_TO=rp2040_ce -bl uf2-split-right
```
Reassemble: USB into left half only + TRRS connected.

## Milestone status

- **M0–M7** ✅ flashed and verified on the physical Corne, including split
  simulation, recipes, lifecycle/medic replacement, VOID ward piercing, and the
  scry overlay. Typing remains unaffected.
- **M7.5 — Combat presentation and composition polish** ✅ flashed and accepted
  on both physical halves, including the captured Corne Arcane default layout.
- **M8 — Host heartbeat and semantic protocol** ✅ flashed and accepted on the
  physical Corne. Offline duel fallback, synchronized host state, scene class,
  notification count, timeout, and daemon restart all work. The isolated host
  keymap uses split snapshot v6/30 bytes; Vial/VIA remain off.
- **M9 — Application-aware Arcane Archive** is implemented and awaiting
  hardware verification. Browser focus, debounce, reconnect behavior, hybrid
  rendering, test preview scenarios, and NixOS startup are complete without a
  browser extension or protocol/state changes.
- **M10** is next after M9 hardware acceptance.

## Hardware notes learned this session

- OLED panels are landscape-mounted; treat as portrait via `OLED_ROTATION_270`
  on both halves (they are mounted the same way, so both use the same rotation).
- Per-half local input requires hooking **both** `matrix_scan_user` (master) and
  `matrix_slave_scan_user` (slave). `process_record_*` runs on the master only.
