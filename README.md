# Corne Arcane OLED beyond-0.4 candidate

Corne Arcane is a deterministic spell-duel world for a Corne v3 RP2040 split
keyboard. A fixed-tick (25 Hz) combat simulation runs on the master half:
key combinations cast spells against enemy creatures, with elemental residue,
field effects, and aftermath tracked across duels. Both OLEDs render live
combat state from a compressed 32-byte split snapshot — the slave half never
recomputes world state. The keyboard is complete offline; an optional Linux
daemon adds privacy-redacted focus, notification, terminal, and repository
semantics.

The source tree contains the unflashed split-v12/Raw-HID-v3 candidate. The
accepted 0.4 artifacts and `griffin` recovery image remain the rollback
authority; see
[`docs/beyond-0.4-candidate.md`](docs/beyond-0.4-candidate.md) before hardware
work.

## Start here

- Build, test, format, or review goldens: [`docs/development.md`](docs/development.md)
- Understand data flow and invariants: [`docs/architecture.md`](docs/architecture.md)
- Review current acceptance and resource use: [`docs/acceptance.md`](docs/acceptance.md)
- Flash both halves or test recovery: [`docs/physical-checklist.md`](docs/physical-checklist.md)
- Inspect the 32-byte wire layouts: [`docs/protocol-ledger.md`](docs/protocol-ledger.md)
- Explore deferred product work: [`docs/backlog.md`](docs/backlog.md)

The active firmware identity is `griffin_arcane`; `griffin` is the stable
recovery image. `firmware/` is the source of truth, and
`host/install_firmware.sh` copies it into a Vial-QMK checkout.

## Building the firmware

The firmware is a QMK keymap, not a standalone tree. It builds against a
[Vial-QMK](https://github.com/vial-kb/vial-qmk) checkout pinned to the
revision in [`VIAL_QMK_REVISION`](VIAL_QMK_REVISION):

```bash
git clone https://github.com/vial-kb/vial-qmk ~/src/vial-qmk
git -C ~/src/vial-qmk checkout "$(cat VIAL_QMK_REVISION)"
./host/install_firmware.sh        # syncs firmware/ into the QMK tree
make release-build                # UF2 + ELF + map, with resource budgets
```

The native simulation tests (mechanics, allocation, and the golden
visual-hash suite) need no QMK checkout or hardware — `make test` runs them
directly. Dev tooling is pinned in `requirements-dev.txt`.

## Common commands

```bash
make lint
make test
make hp-gate
make mechanics-hp-candidates
make release-build
make release-budget
make hygiene
```

Launch Vial through `corne-arcane-vial`, which safely hands the shared Raw HID
endpoint away from the daemon and restores the prior service state. Never
connect or disconnect TRRS while either half is USB-powered; flash the same UF2
to each unpowered, separated half.

Historical plans and evidence live under `docs/archive/` and are not current
implementation guidance.

## License

[GPLv2](LICENSE). The firmware is a derivative work of
[Vial-QMK](https://github.com/vial-kb/vial-qmk) (itself derived from
[QMK Firmware](https://github.com/qmk/qmk_firmware)), so the whole firmware —
including the custom spell-duel simulation and rendering modules — is
licensed under the GNU General Public License v2.
