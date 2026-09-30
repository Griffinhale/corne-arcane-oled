# Flashing both halves

Both halves run the identical image. Nothing about this firmware is
handedness-specific, so there is one file and you copy it twice.

## Before you start

Save your Vial layout (see below) and have a recovery image on disk before the
first flash; [Recovery](#recovery) says how to build one. `griffin_arcane` is
this project's keymap.

Stop the host daemon if you run it, so it is not rediscovering the keyboard
while devices appear and disappear:

```bash
systemctl --user stop corne-arcane-host.service corne-arcane-focus-x11.service
```

## Your keymap and reflashing

**Flashing a new build resets the keymap to the default.** Any change you made
in Vial is lost. Vial-QMK gives every build a random build ID and stores it
next to the keymap on the keyboard. At startup the firmware compares the two,
and when they differ it treats the stored keymap as invalid and writes the
compiled default over it. That happens on every reflash, even of the same
source.

So save your layout before you flash and load it back after:

1. In Vial, **File → Save current layout** and keep the `.vil` file.
2. Flash both halves as described below.
3. In Vial, **File → Load saved layout** and pick that file.

The compiled default is committed as [`firmware/default.vil`](../firmware/default.vil).
Load it to get back to a clean layout without reflashing.

## Getting the image

There are two ways to get the same firmware, and they name the file
differently:

| Route | File | Where it comes from |
|---|---|---|
| Download | `corne_arcane.uf2` | a `fw-v*` release on the [releases page](https://github.com/Griffinhale/corne-arcane-oled/releases) |
| Local build | `artifacts/release/griffin_arcane-release.uf2` | `make release-build`, below |

Downloading is the short way. Check the file against the release's
`SHA256SUMS` before you flash it:

```bash
sha256sum --check --ignore-missing SHA256SUMS
```

If the releases page lists no release yet, build the image.

## Building the image

The firmware builds inside a [Vial-QMK](https://github.com/vial-kb/vial-qmk)
checkout at the revision in [`VIAL_QMK_REVISION`](../VIAL_QMK_REVISION). These
are the steps CI runs. Vial-QMK uses submodules, so clone it recursively:

```bash
git clone --recurse-submodules https://github.com/vial-kb/vial-qmk ~/src/vial-qmk
git -C ~/src/vial-qmk checkout --detach "$(cat VIAL_QMK_REVISION)"
git -C ~/src/vial-qmk submodule update --init --recursive
```

Vial-QMK's own installer sets up the ARM toolchain (`arm-none-eabi-gcc`) and
the system packages for your distribution:

```bash
~/src/vial-qmk/util/qmk_install.sh
```

The installer puts the Python side in with `pip install --user`, which recent
Debian and Ubuntu releases refuse as an externally managed environment, and it
never installs the `qmk` command itself. Use a virtual environment instead:

```bash
python3 -m venv ~/.venvs/qmk
~/.venvs/qmk/bin/python -m pip install qmk
~/.venvs/qmk/bin/python -m pip install -r ~/src/vial-qmk/requirements.txt
export PATH="$HOME/.venvs/qmk/bin:$PATH"
qmk --version && arm-none-eabi-gcc --version | head -1
```

`host/install_firmware.sh` copies the keymap into the checkout with `rsync`, so
install that too if your system lacks it. On NixOS, `corne.nix` supplies the
tooling; see [`BUILD_NOTES_NIXOS.md`](../BUILD_NOTES_NIXOS.md). If the checkout
is not at `~/src/vial-qmk`, set `QMK_ROOT` when you build.

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
3. Copy the image onto it: `corne_arcane.uf2` from a release, or
   `artifacts/release/griffin_arcane-release.uf2` from your own build.
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

## Updating

Update the firmware first, then the daemon. A newer daemon can send values
that older firmware rejects, and the displays then show the host as offline
while those values are in play. So flash both halves before upgrading the
daemon.

1. Stop the daemon, as in [Before you start](#before-you-start), and save your
   Vial layout: the flash resets it.
2. Get the new image. From a release, download the new `corne_arcane.uf2` and
   check it against `SHA256SUMS`. From source, pull, then check whether
   `VIAL_QMK_REVISION` changed. If it did, move the checkout to it:

   ```bash
   git pull
   git -C ~/src/vial-qmk fetch
   git -C ~/src/vial-qmk checkout --detach "$(cat VIAL_QMK_REVISION)"
   git -C ~/src/vial-qmk submodule update --init --recursive
   make release-build
   ```

   The build keeps the image it replaces as
   `artifacts/release/griffin_arcane-release.prev.uf2`. That is your way back
   if the new one misbehaves.
3. Flash both halves with [the sequence](#the-sequence) and load your layout
   back in Vial.
4. Upgrade the host package the way you installed it: a new `.deb`,
   `sudo make -C host install PREFIX=/usr`, or `nixos-rebuild` with `corne.nix`.
   Then start the daemon and run `corne-arcane-diagnostics`.

The `.deb` has its own version number. It follows the host package, not the
`fw-v*` firmware tags, so the two numbers will not match.

## Recovery

The recovery image is Vial-QMK's own `vial` keymap for this board. It has
none of this project's code, so it is a plain working keyboard. Build it once,
before your first flash, in the same pinned checkout:

```bash
cd ~/src/vial-qmk
qmk compile -kb crkbd/rev1 -km vial -e CONVERT_TO=rp2040_ce
cp crkbd_rev1_vial_rp2040_ce.uf2 ~/corne-recovery.uf2
```

If the board already runs firmware you trust, the UF2 you flashed it with works
just as well; keep a copy outside `artifacts/`. A rebuild keeps the image it
replaces as `griffin_arcane-release.prev.uf2`, but only one: the build after
that overwrites it.

To recover, flash that file to both halves using the sequence above. Having it
on disk before you change anything is what makes this a two-minute problem
instead of a bad evening.
