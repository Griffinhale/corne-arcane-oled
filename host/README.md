# Corne Arcane host package 0.5.0 candidate

The optional host package sends bounded, privacy-redacted desktop and browser
activity semantics to `griffin_arcane`. Firmware remains fully functional when
it is absent.

Public commands and identities remain `corne-arcane-host`,
`corne-arcane-event`, `corne-arcane-diagnostics`, `corne-arcane-vial`,
`io.github.Griffinhale.CorneArcane`, and `corne-arcane-host.service`.

Raw HID v3 retains the 32-byte report/eight-byte payload and adds secondary
activity values for scroll, tab selection, and page events. The generic method
is usable without Firefox:

```bash
corne-arcane-event browser scroll 1
```

The Observatory ritual uses a 1,500-second Pomodoro by default. Configure it
with `--pomodoro-duration SECONDS`, or `services.corne-arcane-host.pomodoroDuration`
when importing `corne.nix`.

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
repository state. GNOME reports only application/desktop identifiers. Firefox
sends exactly event kind and intensity; it never reads or sends URLs, titles,
content, history, forms, referrers, or typed text. An absent bus, denied
permission, missing native host, or extension restart disables only that
adapter.

## Tasks

- Run host tests: `./run_tests.sh`
- Exercise one offline exchange: `python -m arcane_host.daemon --dry-run --once --session 1`
- Observe physical acceptance metrics: `corne-arcane-diagnostics --observe 300 --json`
- Launch Vial safely: `corne-arcane-vial`
- Install on NixOS: import `../corne.nix`

Diagnostics stop and later restore an active host service for a query or
observation window. They leave an inactive service inactive and fail if its
state cannot be determined. `--no-service-handoff` bypasses that protection
only for deliberate development use.

The architecture is documented in
[`../docs/architecture.md`](../docs/architecture.md); build and test conventions
are in [`../docs/development.md`](../docs/development.md).
