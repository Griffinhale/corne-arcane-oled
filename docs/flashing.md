# Flashing both halves

Both halves run the identical image. Nothing about this firmware is
handedness-specific, so there is one file and you copy it twice.

## Before you start

Export your Vial layout and keep a known-good image reachable. `griffin` is the
stable recovery keymap in the Vial-QMK tree; `griffin_arcane` is this project.

Stop the host daemon if you run it, so it is not rediscovering the keyboard
while devices appear and disappear:

```bash
systemctl --user stop corne-arcane-host.service corne-arcane-focus-x11.service
```

## The one rule that breaks hardware

**Never connect or disconnect TRRS while either half is USB-powered.** Power
down, then change the cable between the halves. Everything else here is
recoverable; this one is not.

## Entering the bootloader

The controllers use the RP2040 bootloader, so a half in bootloader mode appears
as a small USB drive named `RPI-RP2`.

Two things that are true of many keyboards are *not* true of this one:

- **Double-tapping reset does nothing useful.** `RP2040_BOOTLOADER_DOUBLE_TAP_RESET`
  is not enabled, so reset just resets.
- **The `QK_BOOT` key is unreachable during flashing.** It sits on layer 3, and
  layer 3 is only reachable by holding two thumb keys that live on opposite
  halves. With TRRS disconnected, a lone half can only reach layer 1.

So use the controller's hardware **BOOT** button: hold BOOT while plugging in
USB, or hold BOOT, tap RESET, release RESET, then release BOOT.

## The sequence

```bash
make release-build        # writes artifacts/release/griffin_arcane-release.uf2
```

1. Unplug USB. **Disconnect TRRS.**
2. Hold BOOT on the first controller and plug in USB. `RPI-RP2` mounts.
3. Copy `artifacts/release/griffin_arcane-release.uf2` onto it.
4. Unplug USB.
5. Repeat steps 2–4 on the second controller with the **same file**.
6. Reconnect TRRS while both halves are unpowered.
7. Plug USB back into the half you normally use.

The board reboots itself the moment the copy lands, so the volume disappears
mid-write. A file manager or `cp` may report an I/O error or "device removed".
That is the normal RP2040 behaviour and does not mean the flash failed.

If the volume does not mount automatically, find it with `lsblk` and mount it by
hand. It is a small FAT filesystem labelled `RPI-RP2`.

## Which half is "left"

The half with the USB cable is the master, and this firmware treats the master
as the left half. Nothing is written to either controller to record handedness.

That has one visible consequence: the two halves draw in deliberately different
architectural voices, curved and astral on the left, squared and mechanical on
the right. Moving the cable to the other half swaps them. Keep the cable on
whichever half you normally use.

## Checking it worked

Both displays should leave stale-link mode and stay synchronized once TRRS is
reconnected and one half is powered. Then exercise every key on all four layers
and confirm each keystroke arrives exactly once. The simulation reads key
positions, and must never consume or rewrite ordinary typing.

If you run the host daemon, restart it and confirm the displays pick up focus
and notification state again:

```bash
systemctl --user start corne-arcane-host.service
corne-arcane-diagnostics
```

## Recovery

Flash the recovery keymap to both halves using the identical sequence above.
Keeping one known-good UF2 on disk before you change anything is what makes
this a two-minute problem instead of a bad evening.
