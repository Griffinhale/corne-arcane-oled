# Corne Arcane on NixOS

Import `./corne.nix` directly from this checkout. It supplies QMK tooling, the
`corne-arcane-host` service and commands, udev access for the keyboard's Raw HID
interface, and the wrapped `corne-arcane-vial` launcher. The unwrapped Vial
executable is deliberately absent from the normal system profile.

```nix
imports = [ /path/to/corne-arcane-oled/corne.nix ];

services.corne-arcane-host = {
  enable = true;
  desktopNotifications = true;
  # pomodoroUnit = "pomodoro.timer";
  pomodoroDuration = 1500;
  # x11FocusProducer = true;   # sessions without KWin or GNOME Shell
};
```

`x11FocusProducer` decides whether anything reports the focused window on a
session that has no compositor bridge -- Cinnamon, XFCE, i3, Plasma 5. Leave it
off under KWin or GNOME Shell, which report focus from inside the compositor.
Leaving it off where it is needed is not a partial failure: nothing calls
`ReportActiveWindow` at all, so focus never leaves its default and every window
presents as the same district regardless of what is in front of you. The
producer ships with the package either way; the option only decides whether the
user unit is declared, because NixOS builds user units from module definitions
instead of from the package's unit directory.

```bash
systemctl --user status corne-arcane-focus-x11.service
corne-arcane-focus-x11 --verbose   # prints each identity and what it matched
```

Apply it with `sudo nixos-rebuild switch`, or however the machine is normally
deployed.

## Firmware checkout

The expected Vial-QMK checkout is `~/src/vial-qmk`; override it with
`QMK_ROOT=/path/to/vial-qmk` when running repository scripts.

```bash
make release-build    # syncs firmware/ into the QMK tree and builds both images
make release-budget   # checks flash and RAM against the resource ceilings
```

The images land in `artifacts/release/`. Flash `griffin_arcane-release.uf2`;
`griffin_arcane-diagnostic.uf2` is the same firmware with the diagnostics
build flag on. The `griffin_arcane` keymap carries the duel, host semantics,
secure Vial support, OLED, RGB Matrix, and four dynamic keymap layers.
`griffin` remains the recovery image.

## Device access

`corne.nix` installs the package's udev rule as `60-corne-arcane.rules` rather
than through `services.udev.extraRules`. Rules there land in `99-local.rules`,
after `73-seat-late.rules` has already acted on the `uaccess` tag, so they
would grant nothing. `hardware.keyboard.qmk.enable` installs `qmk-udev-rules`,
which covers the RP2040 bootloader when flashing.

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
daemon, hands off, and restores it on exit. The keyboard keeps typing and
simulating offline while the daemon is paused. The commands and their options
are in [`host/README.md`](host/README.md).

## Flashing

Flashing needs the normal QMK udev rules, which `hardware.keyboard.qmk.enable`
installs. The full sequence (never hot-plug TRRS, power down, separate the
halves, hold BOOT to reach the RP2040 bootloader, copy the identical UF2 to each
controller, reconnect TRRS unpowered) is in
[`docs/flashing.md`](docs/flashing.md).
