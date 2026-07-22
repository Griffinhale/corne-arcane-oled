# `griffin_arcane` firmware

This directory is the authoritative Vial-QMK keymap snapshot. Install it into a
checkout with `../host/install_firmware.sh`; it becomes
`keyboards/crkbd/keymaps/griffin_arcane`.

## Tasks

- Native mechanics, visual, and allocation checks: `make -C sim_test test`
- Full repository verification: `make -C .. test`
- Release and diagnostic images: `make -C .. release-build`
- Size and reserve gate: `make -C .. release-budget`

The simulation, split ownership, rendering layers, and dependency rules are in
[`../docs/architecture.md`](../docs/architecture.md). Formatting, golden review,
safe extraction, and release procedures are in
[`../docs/development.md`](../docs/development.md). Flash safety and rollback are
in [`../docs/physical-checklist.md`](../docs/physical-checklist.md).

Production Raw HID v2 and split snapshot v11 remain fixed 32-byte contracts.
Diagnostic builds additionally expose the separate three-page diagnostic v2
protocol and an 18-byte reverse split reply; consult
[`../docs/protocol-ledger.md`](../docs/protocol-ledger.md) before changing any
wire-facing type or bit allocation.
