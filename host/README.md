# M10 notification policy and adapters

The Linux daemon sends only normalized semantic fields to `griffin_hostoled`:
scene, saturated count, category, priority, age bucket, and critical
persistence. No framebuffer or notification text crosses Raw HID. The v2 wire
report remains 32 bytes; `HELLO`, `HEARTBEAT`, and `NOTIFY` are complete
absolute summaries, and only HELLO/HEARTBEAT refresh the 1.5-second firmware
liveness deadline.

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
monitoring durably. For one diagnostic run use:

```bash
corne-arcane-host --no-desktop-notifications --verbose
```

If session-bus `BecomeMonitor` is denied, only the desktop adapter disables
itself; focus, terminal completion, synthetic events, Raw HID, and offline Duel
fallback continue.

## Synthetic proof

The private session D-Bus Events interface is exposed through the packaged
client:

```bash
corne-arcane-event notify --category terminal --priority normal
corne-arcane-event notify --category security --priority critical --persistent
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
suppressed. It never sends or reads command text, working directory,
environment, or terminal content. Successful commands become `terminal/low`;
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

**M10/M11 status (2026-07-13): physical recovery smoke-test passed.** Both
split-v7 halves boot/type, the packaged daemon's event path reaches the physical
OLEDs, and persistent notification state recovers after USB disconnect and
reconnect. Full desktop-adapter, terminal-hook, stress, suspend, and rollback
acceptance remains.
