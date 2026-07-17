# Corne Arcane host package 0.4.0

The host package enriches `griffin_arcane` with privacy-redacted focus,
notification, terminal-completion, repository, and optional Pomodoro semantics.
It never sends titles, bodies, paths, commands, filenames, window titles, or
other plaintext content to the keyboard. Firmware remains complete without it.

The package retains these public identities:

- `corne-arcane-host`
- `corne-arcane-event`
- `corne-arcane-diagnostics`
- `io.github.Griffinhale.CorneArcane` on the session bus
- `corne-arcane-host.service`

It adds `corne-arcane-vial`, the only supported Vial entry point, because Vial
and the daemon share one Raw HID endpoint. The launcher's daemon-handoff
contract — stop the service, wait for its hidraw handle to close, run Vial, and
restore the service on any exit only if it was active before launch — is
documented in the top-level `README.md` §Persistent Vial remapping.

## Raw HID exchange

Every daemon write is a request/response exchange. The daemon sends one 32-byte
`0xCA` report, consumes VIA's exact echo, and only then schedules the next
heartbeat. A timeout, short read, or mismatch closes the device and starts a
fresh session after rediscovery.

Diagnostics use the same endpoint. The client first drains the exact VIA echo,
then waits for the later diagnostic response. A release build intentionally has
no later response; use firmware built with `ARCANE_DIAGNOSTICS=yes` for metrics.

## Tests

```bash
./run_tests.sh
```

The suite covers packet vectors, hidraw framing and discovery, exact echo
validation, timeout/mismatch reconnects, device renumbering, diagnostics echo
draining, malformed/stale traffic, privacy, service handoff, launcher crash and
signal recovery, and launch while the service is already disabled.

## NixOS

Import `../corne.nix`. The package version is `0.4.0`; the NixOS module installs
the wrapped launcher instead of the raw Vial executable and grants uaccess to
the keyboard's vendor Raw HID interface.
