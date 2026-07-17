# Corne Arcane 0.4 acceptance record

Status: combined M14 implementation and automated desktop/QMK acceptance are
complete. The prior world/mechanics two-half acceptance reported on 2026-07-15
remains the rollback baseline. M14 hardware acceptance, the unified Vial
handoff, EEPROM persistence, and release-image flash checks remain explicitly
pending in `physical-checklist.md`.

## Feature boundary

`griffin_arcane` is the only active feature firmware. The accepted current
Twin Cities and incantation implementation is unconditional, with a v10 split
protocol, v2 Raw HID host protocol, secure Vial remapping, OLED, and RGB Matrix.
Both halves must be flashed from the same artifact. Packet size, version,
bounds, and CRC are validated before acceptance; mixed split versions fall into
the stale-link presentation rather than interpreting one another's state.

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
| Observatory | stargazing instrument | nested dome, telescope, constellation | gear observatory, astrolabe, scope |

Resident work, inspect, and rest anchors align with the corresponding major
objects. The Observatory is selected only by an active Pomodoro, is always
QUIET, suppresses disposable couriers/events and energetic typing accents, and
keeps combat, alerts, health, scry, transitions, and authoritative aftermath.
Pomodoro completion returns to the current focus-derived floor; DND quiets the
ordinary focused floor without selecting Observatory.

A master-owned, host-independent 30-minute sky cycles through dawn (0:00–2:30),
day (2:30–22:30), dusk (22:30–25:00), and night (25:00–30:00). Its two bits
ride split `secondary` bits 3–4. A stale half continues locally and adopts the
master's current phase at reconnect without replay. Sparse sky pixels remain an
underlay beneath all protected presentation regions.

Each OLED derives a packed local typing ambience from its physically local
wizard. Live collection and compiled spell descriptions share one tempo/trend
classifier; launch and cancellation return to calm. Tempo bounds floor motes
and work accents while trend varies drift/cadence. Nothing about this ambience
crosses the split link.

The master also maintains a non-mechanical session diplomacy balance from −3
to +3. KO edges move it toward the surviving side, diplomatic-event weight is
`4 + 2 × abs(balance)`, and the existing event target selects proud,
receiving, or neutral resident poses. It never feeds combat state.

RGB Matrix is now entirely world-owned: all compiled layer-3 RGB controls and
all remapped RGB keycodes are no-ops, every LED is overridden through flags and
coordinates, and EEPROM lighting settings cannot disable the surface. Sleep,
stale-link, impact, shatter, prepared element, Observatory, and city baseline
priorities are pure-policy tested. DIM scales output to 25%; SLEEP is black;
only physical key activity wakes OLED and RGB.

The civic-layer parity pass exposes that same `(floor, action)` object anchor to
residents, local attunement, couriers, rare events, and authoritative aftermath.
All four courier cores now have Commons, Research, and Workshop treatments and
route between the established gap lift and their floor destination. All six
rare-event families likewise retain their deck, targeting, phase, and QUIET
semantics while adopting dispatch/research/workshop objects. Invalid floors
fall back to Commons. No timing, policy, allocation, packet, or mutable-state
contract changed.

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
  during the corresponding arc. Fire, inspection, repair, panic, complaint, and
  celebration marks resolve through the same floor object as the assigned task.
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
| Canonical current combat view | 19 |
| Existing host/civic projection, including marked aftermath | 6 |
| CRC-8 | 1 |

The aftermath marker temporarily assigns the existing `shared_pres` and
`revision` bytes to two city kinds, world state, and two animation phases.
Ordinary civic presentation resumes when no aftermath is active.
Raw HID remains the existing v2 32-byte host protocol. VIA routes unknown
commands to `raw_hid_receive_kb()`: `0xCA` daemon reports are echoed unchanged,
while VIA and Vial retain their standard command IDs. The authoritative bit
allocation and overlay precedence are recorded in `protocol-ledger.md`.

## Resource measurements

Measured 2026-07-16 from clean builds in the configured Vial-QMK checkout with
`arm-none-eabi-gcc 14.2.1`. Flash is `.text + .data` from GNU size. Static RAM
is `.data + .bss + .ram0…ram7` and excludes the linker-created heap.

| Build | Flash | Static RAM | Reserve below 96 KiB |
|---|---:|---:|---:|
| `griffin_arcane` release | 72,164 B | 13,480 B | 26,140 B |
| `griffin_arcane` diagnostic | 73,288 B | 13,608 B | 25,016 B |

Both secure Vial images are below the 81,896-byte flash ceiling, the
16,496-byte static-RAM ceiling, the 96 KiB hard stop, and the required 16 KiB
reserve. Diagnostic firmware adds ChibiOS stack fill/checking and the later
metrics reply without changing packet layouts or typing behavior.

Relative to the accepted pre-M14 baseline, release grew 2,520 bytes flash and
16 bytes static RAM; diagnostic grew 2,188 bytes flash and 32 bytes static RAM.
The budget script enforces the milestone's +8,192-byte flash and +512-byte RAM
limits separately for each image as well as all absolute ceilings.

## Artifacts and hashes

Artifacts are under `artifacts/release/`.

| Artifact | SHA-256 |
|---|---|
| `griffin_arcane-release.uf2` | `313303a89c594030e23f7564c924c0b970e023911aa80f9169385b0e98c49a8d` |
| `griffin_arcane-release.elf` | `54f4ace7ce171391b937477bdc5cbea72109c22c88e5587ec066d18505f184f9` |
| `griffin_arcane-diagnostic.uf2` | `c10b54c4cf4f08ef9ddbdfa5c879afc88734f20e9bf59c4a9314f5d96ef8aa67` |
| `griffin_arcane-diagnostic.elf` | `a7c2fc3c611b4fdd0071bb432abf6d36181d539dc87fdf29c6d81d0fe004dd13` |

The exact 279-scene current visual catalog is
`firmware/sim_test/golden/visual_current.hashes` (SHA-256
`85e454592a84ab05171959a449bb3d262141d85c5d63eecc4cfbd1d113a5fc44`).
The 28 deliberately reviewed additions cover the four-floor/four-sky matrix,
four typing tempos/trends, three diplomacy targets, and seven Observatory
aftermath anchors; existing scenes were rebaselined for the sky underlay.

## Automated gates

- `make test`: consolidated end-to-end mechanics, v10 split protocol, v2 host
  protocol, render, mirror, convergence, no-allocation, daemon echo/reconnect,
  diagnostics, privacy, and safe-launcher tests.
- `make visual-test`: 279 unique exact current
  framebuffer scenes spanning both sides, four voices, every form, temporal
  trajectories, wards/statuses, reactions, outcomes, six occupation scenes,
  every floor/courier, floor/event, and floor/aftermath variant, representative
  civic lifecycle/density/QUIET/transition/scry compositions, four transition
  phases, and bilateral gap-cue timing.
- Deterministic steady, burst, and mixed-layer prose workloads produce no KO in
  their first 30 seconds and a first KO between 60 and 180 seconds.
- `make release-budget`: fixed flash/RAM ceilings, milestone growth ceilings,
  96 KiB hard stop, and 16 KiB reserve for both images.
- Clean unified secure-Vial release and diagnostic builds link successfully for
  `crkbd/rev1`, `CONVERT_TO=rp2040_ce`.
- `make hygiene` rejects retired build variables, production keymap names, and
  milestone-prefixed active identifiers or paths outside `docs/archive/`.
- `git diff --check` passes.

## Physical acceptance

The project owner reported the current world/combat two-half run in
`physical-checklist.md` passing on 2026-07-15: diagnostic and release
flashing, typing non-interference, spell/combat presentation, floors and gap
cues, civic aftermath, link/power recovery, and diagnostic thresholds. The
report establishes that housekeeping was
below 2 ms on both halves, render-plus-blit was below 5 ms, stack margin was
nonzero, and queue/protocol error counts stayed at zero. Exact numeric timing
and stack values were not supplied, so the checklist records threshold passes
rather than invented measurements.

The M14 Observatory, full sky cycle, local ambience, diplomacy, RGB priorities,
entropy timing, reconnect convergence, and the civic/Vial ownership/persistence
checks remain unchecked in their physical records. Automated tests cannot
claim flashing, desk-distance readability, real timing/stack headroom, secure
physical unlock, power-cycle persistence, or visible offline animation during
a real GUI handoff.
