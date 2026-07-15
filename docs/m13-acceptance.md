# M13 Arcane Incantation Engine — acceptance record

Status: end-to-end implementation and clean desktop/QMK acceptance complete;
physical two-half execution remains pending.

## Feature boundary

`ARCANE_M13=yes` defines both `ARCANE_M13` and `ARCANE_M12`. Without M13, the
accepted M11.5/M12 types, v8 protocol, mechanics, renderer, and exact visual
goldens remain on their existing compile-time paths. M13 advances the split
protocol to v10, so both halves must be flashed from the same artifact. Packet
size, version, bounds, and CRC are validated before acceptance; mixed v8/v10
halves fall into the existing stale-link presentation rather than interpreting
one another's state.

## Implemented engine

- Each half independently interprets only physical key positions, normalized
  layers, level masks, holds, release transitions, and fixed-tick timing. It
  retains no keycodes, characters, words, complete input sequence, allocation,
  or persistent history. Ordinary QMK key output remains untouched.
- Collection closes after 520 ms with all keys released or is forced at ten
  seconds. A held key keeps the ordinary timeout open; a forced cast rearms
  only after release. Wind-up is 0.32–2.0 seconds, collection resumes at
  launch, and one prepared spell can wait behind the active one.
- Broad deliberate/flowing/rapid/frantic timing buckets plus decelerating,
  steady, accelerating, and irregular trends keep recipes repeatable while
  changing motion, launch cadence, trails, and visual fallout.
- The deterministic 24-bit descriptor selects form, element, payload,
  trajectory, 1–4 magnitude, one dominant status plus intensity/duration,
  interaction, tempo, trend, and cosmetic variance. Wizard roster affinity
  weights selection and presentation but never excludes a form.
- All eight forms are emitted by real physical-input recipes: projectile,
  singularity, roof fireball, beam, 3–6-orb swarm, ground wave, chain/arc, and
  conjure. Conjure produces returning summons or ground traps from trajectory.
- Projectiles support ground, low, middle, high, roof, returning/healing, area,
  and homing paths. Swarms gather around the caster and then launch survivors
  one-by-one. Beams build from a small line to a full blast and visibly fizzle.
- Combat uses 12 HP, at most four direct damage plus one delayed status damage,
  healing in either direction, four dominant statuses, interrupting direct
  impacts, tactical ward capacity, 0–4 consumable ward strength, coverage
  expansion, and a distinct shatter outcome. Spent ward strength does not
  refill inside an incantation; only a newly crossed capacity tier adds one.
- Spell magnitude and ward capacity use deliberately different thresholds:

  | Complexity | Spell magnitude | Ward capacity |
  |---:|---:|---:|
  | 0–47 | 1 | 1 after the first input |
  | 48–79 | 2 | 1 |
  | 80–111 | 2 | 2 |
  | 112–159 | 3 | 2 |
  | 160–191 | 3 | 3 |
  | 192–223 | 4 | 3 |
  | 224–255 | 4 | 4 |

  Direct interruption clears both ward strength and capacity. Strength remains
  through wind-up and clears with capacity at launch. Regeneration restores one
  HP exactly every 30 seconds below maximum, and every damaging contact or burn
  tick restarts the full timer.
- The interaction matrix supports lanes and phase-through, projectile clashes,
  beam carrying/destruction, singularity absorption and reflected power,
  compatible combination, incompatible detonation, chain jumps, traps, and
  individually destructible swarm orbs. Compatible same-element spells add
  magnitude up to four and the stronger carrier continues; magnitude ties use
  tempo then trend. Exact ties and ember/frost incompatibility create symmetric
  one-power area pulses through ordinary area/ward rules. Other equal neutral
  clashes still cancel harmlessly.

## Hardware-feedback presentation

The active occupation is now the largest floor silhouette, with both city
voices retained:

| Floor | Shared occupation | Astral-left voice | Mechanical-right voice |
|---|---|---|---|
| Commons | communal table + notice/mail board | tea orb, arched notice board | dispatch desk, cubbies, clock |
| Research | telescope/analyzer + specimen cabinet | orrery and star chart | probe, scope display, specimen cylinder |
| Workshop | forge + tool station | cauldron and reagent rack | anvil, gear press, hoist |

Resident work, inspect, and rest anchors align with the corresponding major
objects. Six exact city/floor scenes differ by at least 40 floor-band pixels for
every same-city occupation pair. `SPECIAL` remains reserved.

Host focus changes use a local 600 ms, four-phase transition: source-room
shutter, full brick/elevator wipe, target-room reveal, then settling dust and
sparks. One presentation byte packs source floor, phase, and active flag; the
civic byte remains the authoritative target. Rapid changes restart from the
latest target without a queue. Sleeping OLEDs do not wake for focus and snap to
the current target when a key wakes them. The transition never touches rooftop,
health, courier, or foundation protected regions and adds no wire state.

The battlefield dead zone and all collision timing remain unchanged. While a
carrier crosses coordinates 96–159, the renderer derives these edge cues from
the existing descriptor and progress only:

| Cue family | Inner-edge grammar |
|---|---|
| Projectile, fireball, ground wave | shrinking departure sparks, then growing destination motes |
| Swarm, homing, area | persistent bilateral edge trails |
| Phase/void, returning, conjure, reflected singularity | paired portal/rune handoff |
| Beam, chain | continuous bilateral carrier with synchronized edge flare |

Departure, midpoint, and arrival are exact-golden tested in both directions;
direction, lane, element styling, and mirroring are descriptor-derived and add
no simulation state.

## Authoritative civic aftermath

Aftermath is fixed-tick simulation state, not a disposable renderer flourish.
Each city carries a reaction kind, remaining time, intensity, resident task,
room state, and object flags; the world carries calm, wonder, crisis, or
recovery. The renderer derives bounded animation phases from that state.

- Residents can cheer, complain, panic, watch a maximal cast, fight fire,
  inspect residue, and repair damage. Their position and task-hat/mark change
  during the corresponding arc.
- Ten-second casts coordinate both cities through watch, wonder, and cheer.
- Roof fireballs detonate against the roof, disrupt the room, ignite an object,
  send residents through panic/firefighting/repair, and restore calm after the
  bounded recovery arc.
- Combination, collapse, residue, healing, detonation, complaint, and ward
  shatter have dedicated outcomes. Side-neutral outcomes cannot fall through
  to the old right-ward deflection animation.
- The authoritative phase projection survives packet loss, rejects corruption,
  and converges after reconnect or master-session restart.

## Wire contract

The packed v10 snapshot is exactly 32 bytes:

| Region | Bytes |
|---|---:|
| Header (`magic`, version, session, flags, sequence) | 6 |
| Canonical M13 combat view | 19 |
| Existing host/civic projection, including marked aftermath | 6 |
| CRC-8 | 1 |

The M13 aftermath marker temporarily assigns the existing `shared_pres` and
`revision` bytes to two city kinds, world state, two animation phases, and
coherence. Ordinary M12 civic presentation resumes when no aftermath is active.
Raw HID remains the existing v2 32-byte host protocol.

## Resource measurements

Measured 2026-07-15 from clean builds in the configured Vial-QMK checkout with
`arm-none-eabi-gcc 14.2.1`. Flash is `.text + .data` from GNU size. Static RAM
is `.data + .bss + .ram0…ram7` and excludes the linker-created heap.

| Build | Flash | Static RAM | Delta from M12 |
|---|---:|---:|---:|
| M12 rollback | 68,076 B | 16,308 B | — |
| `griffin_anim` Vial release | 80,972 B | 16,496 B | +12,896 B flash, +188 B RAM |
| `griffin_anim` Vial diagnostic | 81,868 B | 16,568 B | +13,792 B flash, +260 B RAM |
| `griffin_hostoled` release | 66,720 B | 13,664 B | −1,356 B flash, −2,644 B RAM |
| `griffin_hostoled` diagnostic | 68,252 B | 13,780 B | +176 B flash, −2,528 B RAM |

The budgeted Vial release is 948 bytes below the 80 KiB target and 17,332 bytes
below the 96 KiB hard stop, preserving the required 16 KiB reserve. Diagnostic
builds add ChibiOS stack fill/checking, expose both halves' timing/queue/split
telemetry over Raw HID, and keep stack high-water in debugger-visible
`duel_diag` state without changing their corresponding release image.

## Artifacts and hashes

Artifacts are under `artifacts/m13/`.

| Artifact | SHA-256 |
|---|---|
| `m12-rollback.uf2` | `e0c91db0c4bfb916efeb7e99d6667ac1d905a484599c1bcf10fcecb94525bbd9` |
| `m12-rollback.elf` | `22b6ad4f498edd7ac846ad0f07e653411978d9cf52b2eca8ea5cf5a92484804a` |
| `griffin_anim-m13-vial-release.uf2` | `6553ded88b9d2b7d8d571b3bfd5edbc16dd50b5671149c08853d294f897ee79f` |
| `griffin_anim-m13-vial-release.elf` | `ff8119c014821c7d0bd4c6fa4ff6dfd3ee888b3e3c965b5c0cdc9a9ce87252d6` |
| `griffin_anim-m13-vial-diagnostic.uf2` | `501626cc18b576fa1d6340815d610461790f3c0b62e0bac3b5802cfd85990dfc` |
| `griffin_anim-m13-vial-diagnostic.elf` | `6bcef50ad055291516940c2f92ebb27ebba435d8a40a6558ad89a5c0ca462fe2` |
| `griffin_hostoled-m13-release.uf2` | `fe0ad7f2c4e5d0b74f246e12b398b52e52c740a9c87db05f7a604ea936da949b` |
| `griffin_hostoled-m13-release.elf` | `60e076d0a3eb002db63c361326964784253137fcea436491833837537c1e2ba2` |
| `griffin_hostoled-m13-diagnostic.uf2` | `f292dd0bd78a673917f76f09b72e9f436398c98e76beadb57391d6f6febdcfb5` |
| `griffin_hostoled-m13-diagnostic.elf` | `63cfce6d306263fc37f18565032edda8501189889ce85f571be6f4bce8115c0d` |

The exact 144-scene M13 visual catalog is
`firmware/sim_test/golden/visual_m13.hashes` (SHA-256
`d4250f57c480c4565d25e7e18d65ed9c9194fe86fa2dd7d91388e91a5b345092`).
The frozen M12 visual golden remains
`8c99c437f6cfaa066aec99c375520e181690819c7f62e1c3ac036a900dad95cc`.

## Automated gates

- `make test`: frozen M11.5/M12 suites; M13 end-to-end mechanics, protocol,
  render, mirror, and convergence tests; both no-allocation symbol gates; and
  all 59 host tests.
- `make visual-test`: frozen M11.5/M12 visual suites plus 144 unique exact M13
  framebuffer scenes spanning both sides, four voices, every form, temporal
  trajectories, wards/statuses, reactions, outcomes, six occupation scenes,
  four transition phases, and bilateral gap-cue timing.
- Deterministic steady, burst, and mixed-layer prose workloads produce no KO in
  their first 30 seconds and a first KO between 60 and 180 seconds.
- `make m13-budget`: 80/96 KiB flash limits, 16 KiB reserve, and 1 KiB maximum
  static-RAM delta against the clean preserved M12 ELF.
- Clean Vial and Vial-free release/diagnostic QMK builds all link successfully
  for `crkbd/rev1`, `CONVERT_TO=rp2040_ce`; preserved rollback images remain.
- `git diff --check` passes.

## Physical acceptance

No keyboard was flashed during this implementation session. The software and
artifacts are ready for the explicit two-half run in
`m13-physical-checklist.md`. Numeric `<2 ms` housekeeping, `<5 ms`
render-plus-blit, stack high-water, input non-interference, and physical OLED
legibility remain hardware observations and are deliberately not claimed here.
