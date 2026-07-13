# Corne Arcane OLED

A firmware-driven "spell duel" that plays out across the two OLEDs of a Corne v3
(RP2040) split keyboard. Each physical half renders one wizard; they cast, ward,
and fall to each other's spells, all driven by your typing — never by streamed
frames. A later host daemon *enriches* the scene (application class,
notifications) over Raw HID but is never required for the keyboard to stay
coherent.

This repository is the **project archive**: the planning documents, a committed
snapshot of the firmware, the durable NixOS config, and the original spike
notes. The live firmware is developed in a separate QMK tree (see below).

## Layout of this repo

| Path | What it is |
| --- | --- |
| `firmware/` | **Committed snapshot** of the live keymap (`griffin_anim`): the hardware-agnostic duel engine (`sim/`), the QMK glue (`keymap.c`), and the host test rig (`sim_test/`). See `firmware/README.md` for the deep dive. |
| `host/` | M9's focus-aware Linux Raw HID daemon, private D-Bus/KWin bridge, Nix package, tests, and the isolated `griffin_hostoled` build override. |
| `Corne_Arcane_OLED_Implementation_Roadmap.docx` | Milestone plan **M0–M11** (the authoritative build order). |
| `Corne_Arcane_OLED_Design_Audit_Addendum.docx` | Scope guards, failure modes, and the simulation/presentation/**external-context** data-class boundary. |
| `Corne_Arcane_OLED_Build_Kickoff_Prompt.docx` | The original kickoff brief and stopping rules. |
| `BUILD_NOTES_NIXOS.md` | How this actually builds on NixOS 26.05 (supersedes the Debian steps in `spike1/`). |
| `corne.nix` | Durable NixOS module: qmk/vial toolchain, hidraw uaccess, packaged M9 daemon/bridge, and Plasma user service. Not yet applied. |
| `spike1/` | Original working notes + helper scripts from the first hardware spikes (Debian-era; some steps superseded by `BUILD_NOTES_NIXOS.md`). |
| `sync-firmware.sh` | Refresh the `firmware/` snapshot from the live QMK tree. |

## The firmware snapshot vs. the live tree

`firmware/` is a **real copy**, not a symlink, so this repo is self-contained and
pushable. The live source of truth is the QMK tree:

```
~/src/vial-qmk/keyboards/crkbd/keymaps/griffin_anim
```

After editing the live tree, refresh the committed snapshot and commit the diff:

```bash
./sync-firmware.sh          # rsyncs the live keymap into firmware/
git add -A && git commit -m "sync firmware snapshot"
```

## Keymaps

- **`griffin`** — stable Vial baseline. The recovery keymap; never experimented on.
- **`griffin_anim`** — the OLED duel (Vial **on**). Everything in `firmware/` here.
  Its compiled four-layer default is captured from `../corne-arcane.vil`.
- **`griffin_hostoled`** — the complete duel fallback plus M8 semantic Raw HID
  and M9 hybrid Archive renderer,
  using the same four-layer default. Vial/VIA are **off** because they cannot
  share QMK's single raw-HID interface with the custom daemon protocol.

## Milestone status

**M0–M8 are hardware-verified. M9 is implemented and awaiting hardware
verification.** Browser focus now selects a 200 ms-debounced hybrid Archive
scene through a privacy-bounded KWin bridge, while typing animates the archive
from existing synchronized duel state. Raw HID v1, split snapshot v6, combat,
and world hashes are unchanged. M10 is next after M9 hardware acceptance. Full
detail and the acceptance checklist live in `firmware/README.md`.

## Build & flash (NixOS, user-scope, no sudo)

```bash
cd ~/src/vial-qmk                # qmk + arm-none-eabi-gcc are on PATH
qmk compile -kb crkbd/rev1 -km griffin_anim -e CONVERT_TO=rp2040_ce
# Flash one half at a time — never hot-plug TRRS:
qmk flash -kb crkbd/rev1 -km griffin_anim -e CONVERT_TO=rp2040_ce -bl uf2-split-left
qmk flash -kb crkbd/rev1 -km griffin_anim -e CONVERT_TO=rp2040_ce -bl uf2-split-right
```

Reassemble: USB into the **left** half only, TRRS connected while unpowered.

Host tests (no keyboard needed): `cd firmware/sim_test && ./run_tests.sh`.
Daemon tests: `cd host && ./run_tests.sh`.

## Design invariants

- Keyboard output never waits on display logic; key-path hooks only record compact events.
- One authoritative fixed-tick simulation (master owns shared state); the firmware
  always retains a complete duel fallback, so the host daemon is enrichment, never a dependency.
- Deterministic sim: identical init + identical per-tick input/event streams produce
  bit-identical worlds. Cosmetics key off the render frame, never the sim tick.
