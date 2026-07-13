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
| `Corne_Arcane_OLED_Implementation_Roadmap.docx` | Milestone plan **M0–M11** (the authoritative build order). |
| `Corne_Arcane_OLED_Design_Audit_Addendum.docx` | Scope guards, failure modes, and the simulation/presentation/**external-context** data-class boundary. |
| `Corne_Arcane_OLED_Build_Kickoff_Prompt.docx` | The original kickoff brief and stopping rules. |
| `BUILD_NOTES_NIXOS.md` | How this actually builds on NixOS 26.05 (supersedes the Debian steps in `spike1/`). |
| `corne.nix` | Durable NixOS module: qmk/vial toolchain, non-root flashing, Vial hidraw uaccess. Not yet applied. |
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
- **`griffin_hostoled`** — reserved for M8+ Raw HID host work (Vial **off** — VIA/Vial
  and custom Raw HID cannot share QMK's single raw-HID interface). Not created yet.

## Milestone status

Hardware-verified through **M6**; **M6.5** and **M7** host-verified with a hardware
eyeball pending the next reflash. **M8 (host heartbeat + semantic protocol)** is
scoped but not started — it waits on the Vial layout being finalized so the static
layout can be captured before Vial is disabled on `griffin_hostoled`. Full
per-milestone detail lives in `firmware/README.md`.

## Build & flash (NixOS, user-scope, no sudo)

```bash
cd ~/src/vial-qmk && nix-shell   # qmk + python3 + arm-none-eabi-gcc
qmk compile -kb crkbd/rev1 -km griffin_anim -e CONVERT_TO=rp2040_ce
# Flash one half at a time — never hot-plug TRRS:
qmk flash -kb crkbd/rev1 -km griffin_anim -e CONVERT_TO=rp2040_ce -bl uf2-split-left
qmk flash -kb crkbd/rev1 -km griffin_anim -e CONVERT_TO=rp2040_ce -bl uf2-split-right
```

Reassemble: USB into the **left** half only, TRRS connected while unpowered.

Host tests (no keyboard needed): `cd firmware/sim_test && ./run_tests.sh`.

## Design invariants

- Keyboard output never waits on display logic; key-path hooks only record compact events.
- One authoritative fixed-tick simulation (master owns shared state); the firmware
  always retains a complete duel fallback, so the host daemon is enrichment, never a dependency.
- Deterministic sim: identical init + identical per-tick input/event streams produce
  bit-identical worlds. Cosmetics key off the render frame, never the sim tick.
