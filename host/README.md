# Corne Arcane host package 0.4.0

The optional host package sends bounded, privacy-redacted desktop semantics to
`griffin_arcane`. Firmware remains fully functional when it is absent.

Public commands and identities remain `corne-arcane-host`,
`corne-arcane-event`, `corne-arcane-diagnostics`, `corne-arcane-vial`,
`io.github.Griffinhale.CorneArcane`, and `corne-arcane-host.service`.

## Tasks

- Run host tests: `./run_tests.sh`
- Exercise one offline exchange: `python -m arcane_host.daemon --dry-run --once --session 1`
- Observe physical acceptance metrics: `corne-arcane-diagnostics --observe 300 --json`
- Launch Vial safely: `corne-arcane-vial`
- Install on NixOS: import `../corne.nix`

Diagnostics stop and later restore an active host service for the whole query or
observation window. They leave an inactive service inactive and fail if its
state cannot be determined. `--no-service-handoff` bypasses that protection for
deliberate development use.

The daemon, D-Bus, Raw HID, privacy, reconnect, and Vial handoff architecture
is documented in [`../docs/architecture.md`](../docs/architecture.md). Build,
lint, and test conventions are in
[`../docs/development.md`](../docs/development.md).
