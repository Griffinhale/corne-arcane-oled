# Corne Arcane 0.4 on NixOS

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
```

There are no milestone feature variables. `griffin_arcane` always contains the
current v10 world, host semantics, secure Vial support, OLED, RGB Matrix, and
four persistent dynamic keymap layers. `griffin` remains the recovery image.

Use `make release-build` to produce neutral files under `artifacts/release/`
and `make release-budget` to enforce the accepted resource ceilings.

## Host service and Vial handoff

```bash
systemctl --user status corne-arcane-host.service
corne-arcane-diagnostics
corne-arcane-vial
```

Before the rebuilt NixOS configuration is active, the checkout's build result
can be invoked directly from the repository root:

```bash
./result/bin/corne-arcane-vial
```

If the bare command is missing while `corne-arcane-host` exists, the active
profile is an older package generation; rebuild/apply the configuration that
imports this checkout rather than launching raw Vial alongside the daemon.

Do not start the raw `vial` binary while the daemon is active. The wrapped
launcher is the supported entry point because Vial and the daemon share QMK's
single Raw HID endpoint. The keyboard keeps typing and simulating offline while
the daemon is paused.

The service retains its D-Bus, event-client, diagnostics, udev, and command
identities. Package metadata is `0.4.0`.

## Flashing

Flashing needs the normal QMK udev rules. Never hot-plug TRRS. Power down,
disconnect the halves, flash the identical current UF2 to each controller, then
reconnect TRRS while unpowered. The detailed verification and recovery steps
are in `docs/physical-checklist.md`.
