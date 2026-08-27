# Corne Arcane beyond-0.4 candidate on NixOS

Import `./corne.nix` directly from this checkout. It supplies QMK tooling, the
`corne-arcane-host` service and commands, udev access for the keyboard's Raw HID
interface, and the wrapped `corne-arcane-vial` launcher. The unwrapped Vial
executable is deliberately absent from the normal system profile.

```nix
imports = [ /home/griffin/dev/corne-arcane-oled/corne.nix ];

services.corne-arcane-host = {
  enable = true;
  desktopNotifications = true;
  # pomodoroUnit = "pomodoro.timer";
  pomodoroDuration = 1500;
};
```

Apply the host configuration with the machine's usual NixOS deployment
workflow. Whether it is currently deployed is machine-local state.

## Firmware checkout

The expected Vial-QMK checkout is `~/src/vial-qmk`; override it with
`QMK_ROOT=/path/to/vial-qmk` when running repository scripts.

```bash
./host/install_firmware.sh
cd ~/src/vial-qmk
qmk compile -kb crkbd/rev1 -km griffin_arcane -e CONVERT_TO=rp2040_ce
qmk compile -kb crkbd/rev1 -km griffin_arcane \
  -e CONVERT_TO=rp2040_ce -e ARCANE_DIAGNOSTICS=yes
qmk compile -kb crkbd/rev1 -km griffin_arcane \
  -e CONVERT_TO=rp2040_ce -e ARCANE_HP=8
qmk compile -kb crkbd/rev1 -km griffin_arcane \
  -e CONVERT_TO=rp2040_ce -e ARCANE_HP=10
```

`griffin_arcane` contains the current v12 world, host semantics, secure Vial
support, OLED, RGB Matrix, and four persistent dynamic keymap layers.
`ARCANE_HP` exists only for the 8/10 physical pacing gate. `griffin` remains
the recovery image.

Use `make release-build` to produce neutral files under `artifacts/release/`
and `make release-budget` to enforce the candidate resource ceilings. Do not
flash either HP candidate before the 0.4 checklist is accepted or superseded.

## Device access

`corne.nix` takes the udev rule from the package, as `60-corne-arcane.rules`,
rather than writing it inline. `services.udev.extraRules` lands in
`99-local.rules`, and systemd consumes the `uaccess` tag from a match in
`73-seat-late.rules` that udev has already evaluated by then, so the inline rule
this file previously described granted nothing. Access came from
`qmk-udev-rules`' blanket hidraw rule instead, which
`hardware.keyboard.qmk.enable` still installs and which is also what covers the
RP2040 bootloader when flashing.

```bash
udevadm test /sys/class/hidraw/hidrawN   # rule matches at 60, uaccess then runs
getfacl /dev/hidrawN                     # the active user holds an ACL
```

## Host service and Vial handoff

```bash
systemctl --user status corne-arcane-host.service
corne-arcane-diagnostics
corne-arcane-vial
```

Do not start the raw `vial` binary while the daemon is active: Vial and the
daemon share QMK's single Raw HID endpoint, so the wrapped launcher stops the
daemon, hands off, and restores it on exit. The full handoff contract, the
`./result/bin/corne-arcane-vial` fallback for before the rebuilt profile is
active, and the older-generation caveat are in the top-level `README.md`
§Persistent Vial remapping. The keyboard keeps typing and simulating offline
while the daemon is paused.

The service retains its D-Bus, event-client, diagnostics, udev, and command
identities. The package version is declared in `host/pyproject.toml`.

## Flashing

Flashing needs the normal QMK udev rules. The flash-safety sequence — never
hot-plug TRRS, power down, separate the halves, flash the identical UF2 to each
controller, reconnect TRRS unpowered — and the recovery route are in the
top-level `README.md` §Flash safely and §Recovery; the detailed physical
verification and recovery steps are in `docs/physical-checklist.md`.
