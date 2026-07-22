# Corne Arcane 0.4 acceptance record

Status: automated desktop, host, protocol, simulation, rendering, and firmware
acceptance pass. A two-half world/combat run was reported passing on 2026-07-15;
its evidence and the later unchecked hardware records are preserved under
`docs/archive/`. The unified Vial handoff, persistence, current visual additions,
and current release artifacts still require the physical checks in
`physical-checklist.md`.

## Current contract

- `griffin_arcane` is the only production firmware. Both halves use the same
  artifact; mixed split versions reject one another and show the stale-link
  presentation.
- Production Raw HID v2 and split snapshot v11 are exactly 32 bytes. The
  diagnostics-only v2 protocol uses three 32-byte pages and an 18-byte reverse
  split reply. Receivers validate version, length, enum ranges, reserved bits,
  and CRC before accepting state.
- Simulation is deterministic, fixed-tick, allocation-free, and authoritative
  on the master. Typing remains ordinary QMK output and is never retained as
  characters, words, or complete input sequences.
- Collection settles after 520 ms or forces at ten seconds. Wind-up is
  0.32–2.0 seconds. Combat uses 8 HP, and regeneration restores one HP exactly
  every 20 seconds below maximum; damage restarts the full regeneration timer.
- The master owns combat, aftermath, residue, sky, diplomacy, civic state, and
  split sequencing. A stale half presents safe local fallback state and adopts
  the next valid master snapshot without replay.
- Host reports contain bounded semantic enums and counters only. They contain
  no titles, URLs, application content, SSIDs, repository paths, or streamed
  pixels. Host state expires after 1.5 seconds without a valid report.
- OLED and RGB sleep are woken only by physical key activity. Focus, host,
  timer, and background-world changes do not wake them.
- Vial, diagnostics, and the daemon share one Raw HID endpoint. Vial and
  diagnostics stop and restore only a daemon that was active before their
  exclusive ownership window; unknown service state fails closed.

The detailed byte allocation is authoritative in `protocol-ledger.md`. The
current physical procedure is authoritative in `physical-checklist.md`.

## Resource measurements

Measured 2026-07-22 from clean builds in the configured Vial-QMK checkout with
`arm-none-eabi-gcc 14.2.1`. Flash is `.text + .data`; static RAM is
`.data + .bss + .ram0…ram7`.

| Build | Flash | Static RAM | Reserve below 96 KiB | Growth from image baseline |
|---|---:|---:|---:|---:|
| `griffin_arcane` release | 76,832 B | 13,504 B | 21,472 B | +7,188 B flash, +40 B RAM |
| `griffin_arcane` diagnostic | 78,184 B | 13,624 B | 20,120 B | +7,084 B flash, +48 B RAM |

Both images remain below the 81,896-byte flash ceiling, the 16,496-byte
static-RAM ceiling, the 96 KiB hard stop, the per-image +8,192-byte flash and
+512-byte RAM growth limits, and the required 16 KiB reserve.

## Artifacts and hashes

Artifacts are under `artifacts/release/`. These are the previously recorded
hashes and remain unchanged while physical acceptance is pending. Clean builds
embed variable QMK metadata, so newly generated candidate hashes must be
recorded from the exact files flashed and may replace this table only after the
signed checklist passes.

| Artifact | SHA-256 |
|---|---|
| `griffin_arcane-release.uf2` | `822a0e8b1ab6e598bd609f3358a4009f0ebcfadfc68bf0b417ce992017fe1d3f` |
| `griffin_arcane-release.elf` | `013dd528eb0cf4910e424f5920f2c8e1e33c29f66dbc31a66e0213bdc45f20b0` |
| `griffin_arcane-diagnostic.uf2` | `a0a360b296029e63dde0bb90955f9f49939ca36a55fd4809fc90098120243085` |
| `griffin_arcane-diagnostic.elf` | `125282dd3c2d852565da2e8b4d65a44de3299443327cc0bf70bceaa253840f89` |

The 363-scene golden catalog is
`firmware/sim_test/golden/visual_current.hashes`, SHA-256
`caedd8fe6e1617196c25842ce2f341ade8a11e6f365fe6a45032fc340ee650c9`.

## Automated gates

- `make test` passes the sanitizer-backed mechanics suites, all 363 exact
  framebuffer scenes, the no-allocation scan, and host tests.
- Deterministic workloads preserve their pinned mechanics and first-KO ranges;
  sustained steady prose reaches first blood near 16 seconds.
- `make release-build` verifies the repository-owned Vial-QMK pin, then builds
  release and diagnostic images for `crkbd/rev1` with
  `CONVERT_TO=rp2040_ce`.
- `make release-budget` enforces absolute flash/RAM ceilings, growth ceilings,
  the 96 KiB hard stop, and 16 KiB reserve.
- `make lint`, `make hygiene`, and `git diff --check` pass.
- PR/push CI repeats native verification on Ubuntu 24.04 with pinned Ruff and
  clang-format. Scheduled and manual firmware CI rebuilds the pinned checkout
  and retains build artifacts for 14 days without publishing or accepting
  them.

Automated tests cannot claim physical flashing, desk-distance readability,
real-device timing or stack headroom, secure unlock, power-cycle persistence,
or visible offline animation during a real GUI handoff. Those remain explicitly
unchecked in `physical-checklist.md`.
