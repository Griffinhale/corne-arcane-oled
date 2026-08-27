# Corne Arcane host package

The optional host package sends bounded, privacy-redacted desktop and browser
activity semantics to `griffin_arcane`. Firmware remains fully functional when
it is absent.

Public commands and identities remain `corne-arcane-host`,
`corne-arcane-event`, `corne-arcane-diagnostics`, `corne-arcane-vial`,
`io.github.Griffinhale.CorneArcane`, and `corne-arcane-host.service`.
`corne-arcane-focus-x11` is new and opt-in.

Raw HID v3 retains the 32-byte report/eight-byte payload and adds secondary
activity values for scroll, tab selection, and page events. The generic method
is usable without Firefox:

```bash
corne-arcane-event browser scroll 1
```

The Observatory ritual uses a 1,500-second Pomodoro by default. Configure it
with `--pomodoro-duration SECONDS`, or `services.corne-arcane-host.pomodoroDuration`
when importing `corne.nix`.

## Installing

The install layout is defined once, in `Makefile`, and driven by both
`package.nix` and `../debian/rules`, so the two platforms cannot drift apart.

On NixOS, import [`../corne.nix`](../corne.nix). It supplies the package, the
udev rule, and the user service.

On Debian or Ubuntu, build the package from a checkout:

```bash
sudo apt install devscripts debhelper python3-gi dbus-user-session
dpkg-buildpackage -b -uc -us
sudo apt install ../corne-arcane-host_*.deb
systemctl --user enable --now corne-arcane-host.service
```

`dbus-user-session` is required, not optional: the daemon is a D-Bus-activated
user service and the diagnostics and Vial handoff both use `systemctl --user`.
It is present by default on desktop installs and absent on minimal ones.

To install without building a package, `make install PREFIX=/usr` places the
same layout directly. Unplug and replug the keyboard afterwards so the udev
rule applies.

Debian has no `vial` package -- Vial ships as an AppImage -- so set
`CORNE_ARCANE_VIAL_BIN` to its path before using `corne-arcane-vial`. Nix pins
this automatically. `CORNE_ARCANE_SYSTEMCTL`, `CORNE_ARCANE_SERVICE`, and
`CORNE_ARCANE_KWIN_SCRIPT` already default correctly on Debian.

## Focus producers

Focus semantics need something to report the active window. Without a producer
everything else still works and focus simply stays at its default.

- KWin: loaded automatically, and requires Plasma 6. The script uses
  `workspace.windowActivated`, which Plasma 5 does not provide.
- GNOME Shell: opt-in, and requires Shell 45 or newer. The extension uses the
  ESM extension API, which GNOME 44 and older cannot load at all.
- Plain X11: enable `corne-arcane-focus-x11.service`, which needs `xprop` from
  `x11-utils`. Use this on XFCE, Cinnamon, i3, or Plasma 5.
- Other Wayland compositors have no producer yet.

## Optional adapters

Nothing below is auto-enabled by the package.

- Zsh: source `share/corne-arcane/zsh/corne-arcane.zsh`.
- Bash: source `share/corne-arcane/bash/corne-arcane.bash`.
- Fish: source or link
  `share/corne-arcane/fish/conf.d/corne-arcane.fish` from Fish's `conf.d`.
- GNOME: explicitly install or link
  `share/gnome-shell/extensions/corne-arcane-focus@griffinhale.github.io`, then
  enable that UUID through GNOME Extensions.
- Firefox: explicitly install the extension assets under
  `share/corne-arcane/firefox`. The packaged native-messaging manifest invokes
  `corne-arcane-browser-bridge`.

Shell hooks report only monotonic duration, integer status, and normalized
repository state. GNOME reports only application/desktop identifiers, and the
X11 producer reads only `WM_CLASS`; neither can reach a window title. Firefox
sends exactly event kind and intensity; it never reads or sends URLs, titles,
content, history, forms, referrers, or typed text. An absent bus, denied
permission, missing native host, or extension restart disables only that
adapter.

## Tasks

- Run host tests: `./run_tests.sh`
- Exercise one offline exchange: `python -m arcane_host.daemon --dry-run --once --session 1`
- Check the Debian layout: `make install DESTDIR=/tmp/stage PREFIX=/usr`
- Observe physical acceptance metrics: `corne-arcane-diagnostics --observe 300 --json`
- Launch Vial safely: `corne-arcane-vial`

Diagnostics stop and later restore an active host service for a query or
observation window. They leave an inactive service inactive and fail if its
state cannot be determined. `--no-service-handoff` bypasses that protection
only for deliberate development use.

The architecture is documented in
[`../docs/architecture.md`](../docs/architecture.md); build and test conventions
are in [`../docs/development.md`](../docs/development.md).
