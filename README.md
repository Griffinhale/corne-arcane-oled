# Corne Arcane OLED 0.4

Corne Arcane is a deterministic spell-duel world for a Corne v3 (RP2040)
split keyboard. Each half renders locally from compact simulation state; normal
typing never depends on the host daemon. The daemon adds privacy-redacted focus,
notification, terminal, and repository semantics over QMK Raw HID.

Version 0.4 has two supported firmware identities:

- `griffin_arcane` is the current four-layer Vial firmware: OLED world, RGB
  Matrix, secure persistent remapping, split synchronization, diagnostics, and
  Raw HID host semantics in one image.
- `griffin` is the stable recovery firmware. Keep a known-good UF2 for both
  halves before changing the current image.

The current implementation is unconditional. Historical milestone build flags
and the former split production variants are not supported.

## Repository map

| Path | Purpose |
| --- | --- |
| `firmware/` | QMK keymap snapshot, deterministic simulation, Vial definition, and native test rig |
| `host/` | Semantic daemon, diagnostics, safe Vial launcher, Nix package, D-Bus/KWin integration, and tests |
| `docs/acceptance.md` | Current automated and physical acceptance record |
| `docs/physical-checklist.md` | Two-half release, Vial handoff, persistence, and recovery procedure |
| `docs/backlog.md` | Deferred product work |
| `docs/archive/` | Superseded milestone plans, records, and original planning documents |
| `corne.nix` | NixOS module for the toolchain, udev access, launcher, and user service |

`firmware/` is the source of truth. `host/install_firmware.sh` materializes it
as `keyboards/crkbd/keymaps/griffin_arcane` in a Vial-QMK checkout.

## Build and verify

```bash
make test
make visual-test
make release-build
make release-budget
make hygiene
git diff --check
```

The QMK build uses `crkbd/rev1` and `CONVERT_TO=rp2040_ce`. Release and
diagnostic artifacts are copied to `artifacts/release/` with neutral names.
The release gate is flash <= 81,896 bytes, static RAM <= 16,496 bytes, flash
below the 96 KiB hard stop, and at least 16 KiB flash reserve.

## Flash safely

Never connect or disconnect TRRS while either half is USB-powered. Power down,
separate the halves, and flash the same UF2 to each half individually:

```bash
cd ~/src/vial-qmk
qmk flash -kb crkbd/rev1 -km griffin_arcane \
  -e CONVERT_TO=rp2040_ce -bl uf2-split-left
qmk flash -kb crkbd/rev1 -km griffin_arcane \
  -e CONVERT_TO=rp2040_ce -bl uf2-split-right
```

Reconnect TRRS only while unpowered, then connect USB to the normal left half.
The split v10 packet is exactly 32 bytes; mixed firmware revisions safely fall
back to the stale-link presentation.

## Persistent Vial remapping

Launch Vial with `corne-arcane-vial`. The launcher records whether
`corne-arcane-host` is active, stops it, waits for its Raw HID handle to close,
runs Vial, and restores the service on every exit only when it was previously
active. While Vial owns Raw HID the complete offline world continues normally.

The secure physical unlock combo remains required. Four complete dynamic
layers are initialized from the compiled keymap and persist in EEPROM across
power cycles and daemon restarts. Export the mapping in Vial before a firmware
upgrade. Use Vial's reset command to restore the compiled four-layer default.
Remapping either physical layer/scry key can intentionally make that layer
gesture unavailable until the mapping is reset; incantation recognition still
uses physical matrix positions and is not changed by keycode remapping.

## Recovery

If configuration or flashing fails, power down, disconnect TRRS, and flash the
preserved `griffin` recovery UF2 to each half. User-generated and ignored UF2,
ELF, and rollback artifacts are intentionally not removed by repository tools.
See `docs/physical-checklist.md` for the full release and recovery sequence.

The hard invariants remain: no allocation or blocking I/O on the typing path,
deterministic fixed-tick mechanics, privacy-redacted host payloads, exact CRC
and bounds checks, physical-position incantations, and a fully functional
keyboard and OLED world without the daemon.
