# `griffin_arcane` firmware

This directory is the committed source snapshot for the only active feature
keymap. `host/install_firmware.sh` copies it into a Vial-QMK checkout as
`keyboards/crkbd/keymaps/griffin_arcane`.

The build is always the accepted current world: deterministic incantations and
combat, Twin Cities civic rendering, v11 split snapshots, v2 host semantics,
OLED power policy, RGB Matrix, secure Vial, and four persistent dynamic layers.
There are no milestone feature switches or Vial-free production variant.

## Contracts

- QMK Raw HID and split reports remain fixed at 32 bytes with their existing
  versions, enum values, bounds checks, and CRCs.
- Host packets beginning with `0xCA` are handled by VIA's keyboard-level
  unknown-command hook. VIA/Vial command IDs are handled by VIA/Vial first.
- Vial sends the original host packet back as its immediate acknowledgement.
  Diagnostic builds later send a separate metrics response.
- The simulation is fixed-tick, integer-only, deterministic, allocation-free,
  and independent of host traffic. Both OLEDs retain the full offline world.
- Incantations read physical matrix positions, not remapped keycodes.

## Vial mapping

`config.h` fixes `DYNAMIC_KEYMAP_LAYER_COUNT` at four and keeps the physical
secure-unlock combo. Uninitialized or reset dynamic EEPROM is seeded from
`keymaps`; later edits persist across power cycles and firmware upgrades. Export
before upgrading, because Vial's reset operation is what restores the compiled
map.

Dynamic macros, QMK settings, tap dance, combos, key overrides, Caps Word,
layer lock, repeat keys, and encoder mapping are disabled in `rules.mk`.

## Tests

```bash
make -C sim_test test
make -C sim_test visual-test
make -C sim_test noalloc-check
```

The world and framebuffer hash baselines are under `sim_test/golden/`. Update a
golden only after reviewing the exact behavioral or visual change.

From the repository root, `make test`, `make visual-test`, `make release-build`,
and `make release-budget` run the complete gates. Release artifacts are written
under `artifacts/release/`.

## Build and flash

```bash
./host/install_firmware.sh
cd ~/src/vial-qmk
qmk compile -kb crkbd/rev1 -km griffin_arcane -e CONVERT_TO=rp2040_ce
qmk compile -kb crkbd/rev1 -km griffin_arcane \
  -e CONVERT_TO=rp2040_ce -e ARCANE_DIAGNOSTICS=yes
```

Flash the same UF2 to both halves separately. Never connect or disconnect TRRS
while either half is powered. Keep `griffin` and a known-good UF2 as the stable
recovery route.
