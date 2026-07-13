# Hardware and Bootloader Notes

## Keyboard identity

Linux showed the keyboard as:

```text
4653:0001 foostan Corne
```

That identifies the running QMK/Corne firmware, not the exact physical controller board. The exact printed controller model was not conclusively identified, but the bootloader behavior confirmed the controller class.

## Controller class

Confirmed bootloader:

```text
RPI-RP2
```

Meaning:

- Controller family: RP2040.
- Firmware file type: `.uf2`.
- Flashing method: enter RP2040 bootloader, then QMK copies/writes a `.uf2` file to the `RPI-RP2` drive.
- Working converter target so far: `CONVERT_TO=rp2040_ce`.

Uncertainty:

- The exact RP2040 board variant was not identified from silkscreen. If future weirdness appears, inspect the controller text. Possible alternatives include `elite_pi`, `helios`, `liatris`, `kb2040`, `sparkfun_pm2040`, or another Pro Micro-compatible RP2040 target.

## Split cable and jacks

Observed cable type:

```text
TRRS = 3 black rings / 4 metal contact sections
```

Known behavior:

- The split link went dead once, and fiddling/reseating the jack brought it back.
- That means the jack/cable path is suspect but not currently proven broken.
- Because `-km default` later brought the right half back over TRRS, the physical split path is probably functional enough.

Rule:

```text
Never plug or unplug TRRS while USB is connected.
```

Common TRRS split designs carry power over the cable. Sliding a TRRS plug through the jack while powered can short contacts. The machine is not moral; it will simply punish bad sequencing.

## Bootloader procedure

To enter bootloader on one half:

1. Disconnect TRRS if flashing only one half.
2. Plug USB into that half.
3. Press the tiny reset button near/under the OLED/controller area.
4. If one press does not show `RPI-RP2`, double-tap reset.
5. Confirm `RPI-RP2` appears in `lsblk` or the file manager.

Useful bootloader watcher:

```bash
watch -n 0.25 'echo USB; lsusb | sort; echo; echo DRIVES; lsblk -o NAME,LABEL,SIZE,MODEL,MOUNTPOINTS'
```

Useful kernel log watcher:

```bash
sudo dmesg -wH
```

## Flashing discipline

For this board, always flash halves separately:

```bash
qmk flash -kb crkbd/rev1 -km <keymap> -e CONVERT_TO=rp2040_ce -bl uf2-split-left
qmk flash -kb crkbd/rev1 -km <keymap> -e CONVERT_TO=rp2040_ce -bl uf2-split-right
```

Do not plug both halves into USB at the same time.

## Valid split test

The valid test for normal use is:

```text
USB into left half + TRRS connected -> both halves should send keys
```

A direct-right USB test is only a controller health test. Under `MASTER_LEFT`-style firmware, direct-right may report left-hand positions. That is expected and not by itself a failure.

