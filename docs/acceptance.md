# Corne Arcane 0.4 acceptance record

Status: end-to-end implementation, clean desktop/QMK verification, and physical
two-half acceptance complete. The project owner reported the complete hardware
checklist passing on 2026-07-15; exact diagnostic numbers were not supplied for
the repository record.

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

Resident work, inspect, and rest anchors align with the corresponding major
objects. Six exact city/floor scenes differ by at least 40 floor-band pixels for
every same-city occupation pair. `SPECIAL` remains reserved.

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
| Canonical M13 combat view | 19 |
| Existing host/civic projection, including marked aftermath | 6 |
| CRC-8 | 1 |

The M13 aftermath marker temporarily assigns the existing `shared_pres` and
`revision` bytes to two city kinds, world state, two animation phases, and
coherence. Ordinary M12 civic presentation resumes when no aftermath is active.
Raw HID remains the existing v2 32-byte host protocol.

## Resource measurements

Measured 2026-07-16 from clean builds in the configured Vial-QMK checkout with
`arm-none-eabi-gcc 14.2.1`. Flash is `.text + .data` from GNU size. Static RAM
is `.data + .bss + .ram0…ram7` and excludes the linker-created heap.

| Build | Flash | Static RAM | Delta from M12 |
|---|---:|---:|---:|
| M12 rollback | 68,076 B | 16,308 B | — |
| `griffin_arcane` Vial release | 81,896 B | 16,496 B | +13,820 B flash, +188 B RAM |
| `griffin_arcane` Vial diagnostic | 82,800 B | 16,568 B | +14,724 B flash, +260 B RAM |
| `griffin_arcane` release | 67,616 B | 13,664 B | −460 B flash, −2,644 B RAM |
| `griffin_arcane` diagnostic | 69,204 B | 13,780 B | +1,128 B flash, −2,528 B RAM |

The budgeted Vial release is 24 bytes below the 80 KiB target and 16,408 bytes
below the 96 KiB hard stop, preserving the required 16 KiB reserve. It is
flash-neutral and has zero static-RAM growth relative to accepted commit
`b7c6d8d`. Diagnostic builds add ChibiOS stack fill/checking, expose both halves'
timing/queue/split
telemetry over Raw HID, and keep stack high-water in debugger-visible
`duel_diag` state without changing their corresponding release image.

## Artifacts and hashes

Artifacts are under `artifacts/release/`.

| Artifact | SHA-256 |
|---|---|
| `m12-rollback.uf2` | `e0c91db0c4bfb916efeb7e99d6667ac1d905a484599c1bcf10fcecb94525bbd9` |
| `m12-rollback.elf` | `22b6ad4f498edd7ac846ad0f07e653411978d9cf52b2eca8ea5cf5a92484804a` |
| `griffin_arcane-release.uf2` | `968210c5726482d803bb8ad54a7abefdcd4dbf19655c44b7f8d0a36ac94544ba` |
| `griffin_arcane-release.elf` | `1e7b579953faaf834ab56a5af112a48f5dcb4e76e7849fa9e27203fb168e08e3` |
| `griffin_arcane-diagnostic.uf2` | `bca860c230d28e5a6d8a1762575036f099ebc7a1b4a6db4729c3b22bddf0bd56` |
| `griffin_arcane-diagnostic.elf` | `e9e99418dfff804f69bc7a5dba8c1b89bb717ca2cd1bc8bf38cebcd9c845bc04` |
| `griffin_arcane-release.uf2` | `5be2dcfa2a81b720eaa643f1c4550c1b54b87036b1117d7667569c40eb698ec8` |
| `griffin_arcane-release.elf` | `f119ae4aeb236bb114969692704d2aab15f30e509dfd9db84881891e4356f9c8` |
| `griffin_arcane-diagnostic.uf2` | `2b2c5b897e95b02a41883967bba961a000be65da3b1d0616f413b13cf9987d8c` |
| `griffin_arcane-diagnostic.elf` | `e564edd05dcf87dbc98615534fa4aaf23d823c68dac3fb1fee8429dfdaa0d826` |

The exact 251-scene M13 visual catalog is
`firmware/sim_test/golden/visual_current.hashes` (SHA-256
`2fa0707ca0dbf2637dcec38a14bd0d2fd6150a2449dba2e8637c6a14bab5690e`).
The frozen M12 visual golden remains
`8c99c437f6cfaa066aec99c375520e181690819c7f62e1c3ac036a900dad95cc`.

## Automated gates

- `make test`: frozen M11.5/M12 suites; M13 end-to-end mechanics, protocol,
  render, mirror, and convergence tests; both no-allocation symbol gates; and
  all 59 host tests.
- `make visual-test`: frozen M11.5/M12 visual suites plus 251 unique exact M13
  framebuffer scenes spanning both sides, four voices, every form, temporal
  trajectories, wards/statuses, reactions, outcomes, six occupation scenes,
  every floor/courier, floor/event, and floor/aftermath variant, representative
  civic lifecycle/density/QUIET/transition/scry compositions, four transition
  phases, and bilateral gap-cue timing.
- Deterministic steady, burst, and mixed-layer prose workloads produce no KO in
  their first 30 seconds and a first KO between 60 and 180 seconds.
- `make release-budget`: 80/96 KiB flash limits, 16 KiB reserve, and zero static-RAM
  growth against accepted M13 commit `b7c6d8d`.
- Clean Vial and Vial-free release/diagnostic QMK builds all link successfully
  for `crkbd/rev1`, `CONVERT_TO=rp2040_ce`; preserved rollback images remain.
- `git diff --check` passes.

## Physical acceptance

The project owner reported the complete two-half run in
`physical-checklist.md` passing on 2026-07-15: diagnostic and release
flashing, typing non-interference, spell/combat presentation, floors and gap
cues, civic aftermath, link/power recovery, diagnostic thresholds, and the M12
rollback/return-to-M13 sequence. The report establishes that housekeeping was
below 2 ms on both halves, render-plus-blit was below 5 ms, stack margin was
nonzero, and queue/protocol error counts stayed at zero. Exact numeric timing
and stack values were not supplied, so the checklist records threshold passes
rather than invented measurements.

The civic-layer parity pass has its own unchecked physical addendum in
`civic-parity-physical-addendum.md`. It does not inherit the July 15 sign-off.
