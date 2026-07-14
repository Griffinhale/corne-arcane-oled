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
| `host/` | M10's privacy-redacted notification/focus daemon, event client, Zsh hook, private D-Bus/KWin bridge, Nix package, and tests. |
| `Corne_Arcane_OLED_Implementation_Roadmap.docx` | Milestone plan **M0–M11** (the authoritative build order). |
| `Corne_Arcane_OLED_Design_Audit_Addendum.docx` | Scope guards, failure modes, and the simulation/presentation/**external-context** data-class boundary. |
| `Corne_Arcane_OLED_Build_Kickoff_Prompt.docx` | The original kickoff brief and stopping rules. |
| `BUILD_NOTES_NIXOS.md` | How this actually builds on NixOS 26.05 (supersedes the Debian steps in `spike1/`). |
| `corne.nix` | Durable NixOS module: qmk/vial toolchain, hidraw uaccess, packaged M10 daemon/adapters, and Plasma user service. Not yet applied. |
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
- **`griffin_hostoled`** — the complete duel fallback plus M8 semantic Raw HID,
  M9 hybrid Archive renderer, and M10 notification sigils,
  using the same four-layer default. Vial/VIA are **off** because they cannot
  share QMK's single raw-HID interface with the custom daemon protocol.

## Milestone status

**M0–M9 are hardware-verified.** On the physical Corne, real KWin focus changes
now select the 200 ms-debounced hybrid Archive scene through the daemon and Raw
HID on both synchronized OLEDs, while non-browser focus returns both halves to
Duel. The mechanism is accepted; Archive visual refinement is deferred to the
polish milestone. **M10 is implemented and hardware-smoke-tested; its remaining
physical checks are tracked as the M11 entry gate. M11 is a desktop-verified,
physically flashed release candidate. M11.1 is now a desktop-verified resource
and hot-path release candidate; its physical acceptance is still pending.**
Both v7 M11 halves boot and type, the
packaged daemon's notifications reach the physical OLEDs, persistent alerts
survive USB disconnect/reconnect, and the five-minute synchronized OLED sleep
has been observed. Raw HID is v2/32 bytes and the split snapshot is v7/31 bytes;
combat, `sim_world_t`, and world hashes remain unchanged. Full stress, timing,
gallery, and rollback acceptance remains tracked in `docs/m11-acceptance.md`.

## Build & flash the M11.1 candidate (NixOS, user-scope, no sudo)

```bash
cd ~/dev/corne-arcane-oled
./host/install_firmware.sh       # refreshes the isolated live keymap
cd ~/src/vial-qmk                # qmk + arm-none-eabi-gcc are on PATH
qmk compile -kb crkbd/rev1 -km griffin_hostoled -e CONVERT_TO=rp2040_ce
# Flash one half at a time — never hot-plug TRRS:
qmk flash -kb crkbd/rev1 -km griffin_hostoled -e CONVERT_TO=rp2040_ce -bl uf2-split-left
qmk flash -kb crkbd/rev1 -km griffin_hostoled -e CONVERT_TO=rp2040_ce -bl uf2-split-right
```

Reassemble: USB into the **left** half only, TRRS connected while unpowered.

Host tests (no keyboard needed): `cd firmware/sim_test && ./run_tests.sh`.
Daemon tests: `cd host && ./run_tests.sh`.

M11.1 gates: `make test`, `make visual-test`, `make gallery`, and `make budget`.
The measured hardening record and pending physical checklist are in
`docs/m11.1-acceptance.md`; inherited M11 evidence remains in
`docs/m11-acceptance.md`. M12 expansion remains gated in
`docs/m12-backlog.md`.

## Design invariants

- Keyboard output never waits on display logic; key-path hooks only record
  compact key-down events while held/release state is level-sampled.
- One authoritative fixed-tick simulation (master owns shared state); the firmware
  always retains a complete duel fallback, so the host daemon is enrichment, never a dependency.
- Deterministic sim: identical init + identical per-tick input/event streams produce
  bit-identical worlds. Outcome durations use a presentation-only wall clock,
  never the sim tick or the number of OLED callbacks.
