# M11.5 semantic host pipeline and adapters

The Linux daemon sends only normalized semantic fields to `griffin_hostoled`:
scene, saturated count, category, priority, age bucket, and critical
persistence. No framebuffer or notification text crosses Raw HID. The v2 wire
report remains 32 bytes; `HELLO`, `HEARTBEAT`, and `NOTIFY` are complete
absolute summaries, and only HELLO/HEARTBEAT refresh the 1.5-second firmware
liveness deadline.

Every input now converges through one immutable `SemanticState` containing the
resolved scene, absolute notification summary, and a revision. The daemon wakes
for focus settlement, policy expiry/age boundaries, adapter deadlines, HID
heartbeat/reconnect deadlines, and direct D-Bus changes. A one-second safety
deadline remains, but the former 25 ms polling loop is gone. Source changes
hidden by scene precedence do not increment the externally visible revision.

Scene precedence is explicit: command-line override, then DND/Pomodoro Focus,
then playing media Archive, then the settled application-focus scene.

## Privacy-bounded semantic adapters

- MPRIS aggregates all players. Playing selects Archive; a track change emits
  only a session-salted opaque event token—never title, artist, URL, or artwork.
- Freedesktop notification inhibition maps to deliberate Focus/DND state.
- An optional systemd user timer supplies active, one-minute warning,
  completion, and failure semantics using monotonic deadlines.
- NetworkManager maps offline, limited, online, and VPN state to bounded system
  or security summaries; interface names, SSIDs, addresses, and endpoints are
  never retained or transmitted.
- The Zsh/Git hook sends only `clean`, `dirty`, `operation`, or `completion`
  enums plus a completion-success bit. It never sends commands or paths.

Adapters read current state once at startup and then subscribe to D-Bus
property changes. A missing/denied service increments a bounded error counter
and disables only that source. Application package aliases resolve through one
profile registry before the session-salted focus hash, so KWin and notification
identities share focused-suppression and category policy.

Normal alerts aggregate for a fixed six seconds without repeat extension. A
ten-second start-to-start budget suppresses cooldown-only events instead of
replaying them. Non-transient critical alerts persist until their desktop
notification closes, `corne-arcane-event clear` is used, or the daemon context
expires. Policy storage and every counter are bounded.

## Install and operate

On Debian 13 with Nix, build and install the package from this checkout:

```bash
nix-build -E 'with import <nixpkgs> {}; callPackage ./host/package.nix {}'
nix profile add ./result
mkdir -p ~/.config/systemd/user
ln -sfn ~/.nix-profile/share/systemd/user/corne-arcane-host.service \
  ~/.config/systemd/user/corne-arcane-host.service
systemctl --user daemon-reload
systemctl --user enable --now corne-arcane-host
journalctl --user -u corne-arcane-host -f
```

The package supplies the user-systemd unit through the Nix profile's
`share/systemd/user` directory. The stable link under `~/.config/systemd/user`
also covers graphical sessions whose user manager started before the Nix
profile was added to `XDG_DATA_DIRS`. If replacing an older profile build,
remove its `corne-arcane-host` profile entry before adding `./result` again.

On NixOS, import `../corne.nix`. Set
`services.corne-arcane-host.desktopNotifications = false` to disable desktop
monitoring durably. Set a timer unit when desired:

```nix
services.corne-arcane-host.pomodoroUnit = "pomodoro.timer";
```

For one diagnostic run use:

```bash
corne-arcane-host --no-desktop-notifications --verbose
```

An `ARCANE_DIAGNOSTICS=yes` firmware pair also exposes its bounded timing and
split counters through a separate two-page Raw HID query. Release firmware does
not recognize or emit these reports. Read the current cumulative snapshot with:

```bash
corne-arcane-diagnostics
corne-arcane-diagnostics --json
```

The readout identifies adaptive versus fixed-80 ms cadence and reports master
queue/catch-up/error counters, housekeeping/render/split peaks, successful and
failed transfers, plus the slave's accepted sequence, snapshot age, peaks, and
stale/resync counters. Reflashing or power-cycling resets the cumulative values.

If session-bus `BecomeMonitor` is denied, only the desktop adapter disables
itself; focus, terminal completion, synthetic events, Raw HID, and offline Duel
fallback continue.

## Synthetic proof

The private session D-Bus Events interface is exposed through the packaged
client:

```bash
corne-arcane-event notify --category terminal --priority normal
corne-arcane-event notify --category security --priority critical --persistent
corne-arcane-event git dirty
corne-arcane-event git completion --failed
corne-arcane-event clear
```

The old daemon option `--notify N` remains a static `other/normal` diagnostic
override. `--dry-run --once --session 0x11223344 --scene archive` prints known
wire vectors without requiring D-Bus or a keyboard.

## Konsole/Zsh completion

Source the packaged hook from `.zshrc`:

```zsh
source /path/to/profile/share/corne-arcane/zsh/corne-arcane.zsh
```

The hook reads Linux uptime for monotonic elapsed time and reports only commands
lasting at least ten seconds. Its D-Bus client is detached with output
suppressed. It never transmits command text, paths, environment, or terminal
content. Successful commands become `terminal/low`;
nonzero exits become `terminal/normal`. Both are suppressed while a recognized
terminal (Konsole first) is focused. Other shells are deferred.

## Desktop privacy boundary

Freedesktop `Notify`, replies, and `NotificationClosed` are observed on a
separate monitor connection. Summary/body exist only long enough to compute a
per-daemon-session salted digest, then are discarded. Text, actions, icons,
URLs, and image data are never logged, persisted, or transmitted. Focused
source matching retains only salted application-identifier digests. Replacements
update their existing entry; closing a persistent notification clears it.
Low/normal focused-source alerts are suppressed, while critical alerts pass.

Run all host checks with `./run_tests.sh`. Do not run the daemon against
`griffin_anim`: Vial uses the same Raw HID interface. For rollback, stop the
service (Duel fallback returns within 1.5 seconds) and use commit `26c49a2` plus
its M9 daemon/firmware pair.

**M11.5 status (2026-07-14): behavioral hardware checks pass; numeric cadence
A/B and final release/rollback flashes remain.**
The 50-test host suite covers protocol and diagnostic vectors, deadline scheduling, alias
canonicalization, privacy retention, all five semantic mappings, multi-player
media aggregation, systemd timer deadlines, and NetworkManager/VPN composition.
The previously flashed v8 diagnostic pair passed the real-application, stress,
sleep/wake, reconnect, daemon-loss, and suspend/resume checks. The new readout
pair, fixed-cadence comparison, release flash, and rollback gate remain in
`../docs/m11.5-acceptance.md`.
