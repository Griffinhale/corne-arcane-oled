# Corne Arcane OLED — Build Notes (NixOS) & Milestone Status

The target host is **Debian 13 (trixie)** with the Nix package
manager, Plasma, and Wayland. `corne.nix` remains the declarative NixOS 26.05
deployment option; on Debian, build `host/package.nix` directly with Nix.
Ignore the older spike1 Vial-AppImage / `keyd.rvaiya` steps here.

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
- `griffin_hostoled` — complete offline duel plus custom Raw HID, M9's hybrid
  Archive renderer, and M10 notification sigils. Vial/VIA are off and it shares
  the same compiled default as `griffin_anim`.

## NixOS import and service

Add the checkout path directly to `/etc/nixos/configuration.nix`:

```nix
imports = [
  /home/griffin/dev/corne-arcane-oled/corne.nix
];
```

Then run the normal `sudo nixos-rebuild switch`. The module builds the daemon,
event client, Zsh hook, PyGObject/Gio runtime, and KWin bridge from `host/`, and
enables the restarting user service at `graphical-session.target`.

On the Debian/Nix-profile target, `host/package.nix` also installs a standalone
user unit. Link it from `~/.nix-profile/share/systemd/user` into
`~/.config/systemd/user`, then activate it with
`systemctl --user enable --now corne-arcane-host`. The explicit link handles
user managers that started before the Nix profile entered `XDG_DATA_DIRS`.

```bash
systemctl --user status corne-arcane-host
journalctl --user -u corne-arcane-host -f
systemctl --user restart corne-arcane-host

# Synthetic policy checks:
corne-arcane-event notify --category terminal --priority normal
corne-arcane-event notify --category security --priority critical --persistent
corne-arcane-event clear

# Diagnostic fixed-scene override (automatic focus arbitration is disabled):
systemctl --user stop corne-arcane-host
corne-arcane-host --scene archive --verbose

# Temporary disable; set services.corne-arcane-host.enable = false for durable disable:
systemctl --user disable --now corne-arcane-host
```

Source the completion hook from `.zshrc` using its installed profile path:

```zsh
source /run/current-system/sw/share/corne-arcane/zsh/corne-arcane.zsh
```

Set `services.corne-arcane-host.desktopNotifications = false` or pass
`--no-desktop-notifications` to disable only desktop notification monitoring.

## Build & flash M11

```bash
cd ~/dev/corne-arcane-oled
./host/install_firmware.sh
cd ~/src/vial-qmk
qmk compile -kb crkbd/rev1 -km griffin_hostoled -e CONVERT_TO=rp2040_ce
# one half at a time; never hot-plug TRRS:
qmk flash -kb crkbd/rev1 -km griffin_hostoled -e CONVERT_TO=rp2040_ce -bl uf2-split-left
qmk flash -kb crkbd/rev1 -km griffin_hostoled -e CONVERT_TO=rp2040_ce -bl uf2-split-right
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
- **M9 — Application-aware Arcane Archive** ✅ hardware-verified on Debian 13,
  Plasma/Wayland, and the physical Corne. Real application focus switches the
  daemon and both OLEDs between Archive and Duel correctly. The mechanism is
  accepted; further Archive appearance tuning is deferred to M11 polish.
- **M10 — Notification policy and adapters** ✅ implemented and host-verified;
  Raw HID v2/32 bytes, split v7/31 bytes, bounded policy, synthetic Events
  interface, Konsole/Zsh completion, and redacted Freedesktop monitoring are
  complete. Both halves have been flashed together and the packaged daemon's
  synthetic event path reaches the physical OLEDs. The remaining stress,
  desktop-adapter, terminal-hook, and recovery checks still gate full hardware
  acceptance.
- **M11 — Living Grimoire polish and release hardening** ✅ desktop-verified and
  flashed on both physical halves. Normal typing/notifications, persistent
  alert recovery across USB disconnect/reconnect, and five-minute synchronized
  OLED sleep are confirmed. Gallery review, full stress/timing measurements,
  suspend/resume, rollback, and final sign-off remain tracked in
  `docs/m11-acceptance.md`.

## Hardware notes learned this session

- OLED panels are landscape-mounted; treat as portrait via `OLED_ROTATION_270`
  on both halves (they are mounted the same way, so both use the same rotation).
- Per-half local input requires hooking **both** `matrix_scan_user` (master) and
  `matrix_slave_scan_user` (slave). `process_record_*` runs on the master only.
