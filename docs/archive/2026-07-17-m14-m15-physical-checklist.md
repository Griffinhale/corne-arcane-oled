# Corne Arcane 0.4 two-half physical acceptance record

Status: current world/combat baseline PASS, reported by the project owner on
2026-07-15. Unified 0.4 Vial handoff and persistence rerun PENDING. Exact timing
and stack measurements were not supplied; threshold results are recorded below.

For the 0.4 rerun, use the same
`artifacts/release/griffin_arcane-diagnostic.uf2` on both halves for
measurement, then repeat the final smoke test with
`griffin_arcane-release.uf2`. Never plug or unplug TRRS
while either half is USB-powered.

## Flash and identity

- [x] Disconnect USB, then disconnect TRRS.
- [x] Put the left controller in bootloader mode and copy
  `griffin_arcane-diagnostic.uf2`.
- [x] Remove USB, repeat for the right controller with the identical file, then
  reconnect TRRS and power the normal USB half.
- [x] Confirm both OLEDs leave the stale-link presentation and remain
  synchronized. A v8/v10 mixed pair must instead remain safely stale.

## Typing and collection

- [x] In a plain-text scratch document, exercise every physical key on all four
  layers, both thumb holds, rolls, chords, rapid alternation, and long holds.
  Confirm every expected host key event arrives exactly once and no spell logic
  consumes or rewrites typing.
- [x] Type independently on each half and confirm each wizard's ward/collection
  changes only from its own half. Simultaneous activity may affect clashes and
  civic intensity, never the other wizard's recipe identity.
- [x] Pause after a short sequence and confirm commit after roughly 520 ms.
  Hold a key beyond that interval and confirm collection stays open until
  release.
- [x] Sustain a sequence to ten seconds: confirm forced commit, no new spell
  input before full release, a protected wind-up, and collection resuming when
  the spell launches.
- [x] Take a direct unblocked hit during collection and confirm the recipe
  restarts. Confirm burn/status ticks alone do not repeatedly restart it.

## Spell and combat presentation

- [x] Observe all eight forms across the pair: projectile, singularity,
  fireball, beam, swarm, ground wave, chain, and conjure summon/trap.
- [x] Confirm right-origin beams mirror left-origin beams: small start, full
  blast, then fizzle, with no gap-side origin inversion.
- [x] Confirm swarms gather around their caster, launch one orb at a time, and
  can lose individual orbs while survivors continue.
- [x] Confirm ground waves stay below airborne projectiles; low/mid/high/roof,
  homing, area, and returning/healing paths remain visually distinct.
- [x] Compare deliberate/decelerating and frantic/accelerating casts; movement,
  cadence, and trails must visibly differ.
- [x] Confirm wards grow in vertical coverage and thickness with the sequence,
  lose strength when struck, do not refill until a new capacity threshold is
  crossed, and use the fracture animation when shattered. Confirm a tier-1 ward
  leaks one point from a magnitude-2 aligned hit and clears at launch.
- [x] Exercise a clash, singularity absorption/reflection, combine, detonation,
  healing, and pass-by. Confirm none accidentally displays the old right-side
  ward-deflection flash.
- [x] Observe a same-element combination cap at magnitude four, an exact-tie
  symmetric pulse, and an ember/frost symmetric pulse. Confirm tier-4 area ward
  coverage can absorb a pulse.
- [x] Time a damaged wizard: no HP should return before 30 seconds; the pip
  returns at the boundary, and an intervening damaging hit restarts the timer.

## Floors, focus, and the physical gap

- [x] Focus representative communication/default, browser/research, and
  terminal/build windows. Confirm Commons reads as table + board, Research as
  telescope/analyzer + specimen storage, and Workshop as forge + tool station.
- [x] Confirm the astral half shows curved/orb variants and the mechanical half
  squared/gear variants without obscuring residents, couriers, health, or roof
  combat. `SPECIAL` must never appear.
- [x] For each focus change, observe roughly 600 ms of source shutter, full
  brick/elevator wipe, target reveal, and settling dust/sparks. Change focus
  rapidly and confirm the animation restarts from the latest target without
  replaying queued rooms.
- [x] Let both OLEDs sleep, change focus, and confirm the focus event does not
  wake them. Wake with a physical key and confirm both snap directly to the
  current floor.
- [x] Observe ordinary, homing/area, phase/void, returning/conjure, beam, and
  chain carriers crossing the desk gap in both directions. Confirm shrinking
  departure/growing arrival, paired edge trails or runes, and synchronized
  beam/chain flares communicate travel while the physical gap duration and hit
  timing remain unchanged.

## Civic aftermath

- [x] Complete a ten-second cast and confirm both cities coordinate: residents
  watch, world motes/wonder appear, and the sequence resolves into cheering.
- [x] Observe a roof fireball: roof detonation, resident panic, visible fire,
  firefighter movement/task hat, repair work, and eventual calm restoration.
- [x] Confirm a light direct-hit complaint is reachable.
- [x] Confirm residue inspection and repair task hats/marks are legible, remain
  inside the floor band, and do not cover the protected health region.
- [x] During each arc, verify both halves show the same phase despite their
  opposite viewpoints.

## Link, power, and diagnostics

- [x] While an aftermath is active, interrupt split connectivity only by first
  removing USB power. Reconnect safely and confirm current state converges
  without replaying a stale phase.
- [x] Exercise host sleep/wake and USB disconnect/reconnect; normal typing,
  duel state, and host civic presentation must recover.
- [x] After at least five minutes of mixed typing and maximal casts, run
  `corne-arcane-diagnostics` (or, from `host/`,
  `python3 -m arcane_host.diagnostics`) to read both diagnostic pages. Record
  the master and peer values below.
- [x] `peak_housekeeping_us < 2000` on both halves.
- [x] `peak_render_blit_us < 5000` on both halves.
- [x] Queue overflow remains `0`; protocol/malformed error counters do not grow
  under a healthy link.
- [x] With a debugger attached, inspect `duel_diag.stack_min_free_bytes` on each
  half after the same stress run; it must remain nonzero with reasonable margin.

| Measurement | Left | Right |
|---|---:|---:|
| Peak housekeeping (us) | PASS (<2,000; exact value not supplied) | PASS (<2,000; exact value not supplied) |
| Peak render + blit (us) | PASS (<5,000; exact value not supplied) | PASS (<5,000; exact value not supplied) |
| Minimum free stack (bytes) | PASS (nonzero; exact value not supplied) | PASS (nonzero; exact value not supplied) |
| Queue overflow | 0 | 0 |
| Split protocol errors | 0 | 0 |

## Release and rollback

- [x] Flash `griffin_arcane-release.uf2` to both halves using the same powered-TRRS safety
  sequence and repeat typing, beam symmetry, maximal aftermath, and reconnect
  smoke tests.
- [x] Preserve the recorded hashes from `acceptance.md` with the sign-off.
- [x] Finally flash the preserved historical recovery image to both halves and
  confirm it still boots; then return both halves to the accepted release.

Sign-off date: 2026-07-15  Tester: project owner (reported)  Result: PASS

## 0.4 Vial handoff and persistence rerun

The checks below are required for the unified `griffin_arcane` image and do not
inherit the earlier two-variant acceptance evidence.

- [ ] Export the current four-layer mapping before flashing the upgrade.
- [ ] Flash `artifacts/release/griffin_arcane-diagnostic.uf2` to both halves and
  confirm secure Vial unlock requires the configured physical combo.
- [ ] With `corne-arcane-host.service` active, launch `corne-arcane-vial`.
  Confirm the daemon stops before Vial opens and Vial receives no unsolicited
  daemon traffic. On Vial close, confirm the launcher restarts the daemon. If
  the daemon was inactive before launch, confirm it remains inactive afterward.
- [ ] While Vial is open, confirm normal typing and the complete offline OLED
  animation continue; only host semantic enrichment pauses.
- [ ] Change keys on each of the four layers, close Vial, and confirm the
  launcher restores the service and the daemon reconnects with a fresh session.
- [ ] Power-cycle the keyboard and confirm all four edited layers persist.
- [ ] Launch Vial while the daemon is already inactive, then close and crash
  Vial in separate runs. Confirm the launcher does not start a previously
  inactive service and does restore a previously active one on every exit.
- [ ] Remap a layer/scry key and confirm this can intentionally make its QMK
  layer gesture unavailable while physical-position incantation recognition is
  unchanged. Reset the dynamic keymap and confirm the compiled four-layer
  default returns.
- [ ] Repeat release flashing with
  `artifacts/release/griffin_arcane-release.uf2`, then repeat handoff, one edit,
  daemon restoration, and power-cycle persistence.

## M14 living-world physical acceptance — pending

These checks apply to the M14 artifacts recorded in `acceptance.md`; they do
not inherit the 2026-07-15 presentation evidence.

- [ ] Flash the diagnostic artifact to both halves and exercise every RGB
  priority on the correct half: city baseline, Observatory, prepared force /
  ember / frost / void, ward shatter, impact, stale link, DIM, and SLEEP.
- [ ] In Vial, map ordinary keys over the former eight layer-3 RGB positions
  and verify they type normally. Map RGB control keycodes elsewhere and verify
  they cannot toggle, recolor, animate, or change world brightness.
- [ ] Verify Observatory Astral/Mechanical architecture and the stargazing
  resident are legible at desk distance; combat, alerts, health, scry,
  transitions, and aftermath remain legible while couriers/events stay absent.
- [ ] Run or observe the complete 30-minute dawn/day/dusk/night cycle and its
  exact 2:30, 22:30, 25:00, and 30:00 boundaries without added animation churn.
- [ ] Type at distinct tempos independently on the two halves. Verify smoke,
  motes, and work accents follow only the local wizard, settle on launch or
  cancellation, and remain restrained in QUIET/Observatory.
- [ ] Produce repeated left and right KOs until diplomatic events appear.
  Verify bilateral proud/receiving/neutral poses reflect session advantage and
  never change duel outcomes.
- [ ] Let both displays sleep. Trigger host focus, Pomodoro, sky, world, and
  timer changes and verify none wakes OLED or RGB; a physical key wakes them.
- [ ] Interrupt split connectivity only while unpowered. Verify the stale half
  runs a local sky, reconnect adopts the current master sky without replay, and
  civic/combat state converges normally.
- [ ] Measure diagnostic housekeeping after boot and under typing/split/RGB
  stress. Confirm Pico session entropy initialization has not violated the
  2 ms housekeeping limit and stack headroom remains nonzero.
- [ ] Repeat the full Vial handoff/persistence section above, then flash the
  release artifact and repeat the essential RGB, Observatory, reconnect,
  sleep/non-wake, typing, and rollback smoke tests.

## M15 foundations-and-spires physical acceptance — pending M14 clearance

These checks apply to M15 artifacts (first built 2026-07-17, `make
release-build`; measured growth 6,704 B of the 8,192 allowance). They queue
behind the M14 record above — flashing stays gated until it completes.

- [ ] Verify the full-height wizard towers at desk distance: astral peak
  (taper/dome/finial) vs mechanical peak (crenellation/beacon mast), shaft
  windows reading the sky phase, the HP window tier (the shaft's lower two
  columns of 2x2 windows going dark from the top as damage lands — check
  the health ladder at 8/6/3/1/0 on both halves), stone-course bottom
  border, and the §2.0 weight-pass silhouettes (wizard mass, spell
  presence, ward arcs).
- [ ] Verify battlefield residue marks are legible at desk distance (marks
  are 1-4 px: force mound, ember column, frost shards, void pit), build
  during ordinary dueling, and fade on the ~45 s per-step clock.
- [ ] Observe the 16-step celestial arc across a sky phase (sun tucks behind
  the left tower at dawn, splits across the gap at midday, sets behind the
  right tower at dusk; moon at night).
- [ ] Trigger a host alert and verify the shaft banner (category glyph,
  priority rails, age accents, notification pips) is legible over lit
  windows and lifecycle traffic, and that scry replaces it with the panel
  summary.
- [ ] Leave both halves idle 3+ s and verify the Track B stances: STUDY on
  the balcony with the tome at full health, MEDITATE seated on the balcony
  when hurt and calm (ward hidden, restored instantly on a keydown),
  FORTIFY braced on deck against an opponent windup, and the PACE/TAUNT
  idle presentation on an unhurt neutral wizard. Balcony figures must be
  legible at desk distance.
- [ ] Judge KO cadence over a normal typing day after the HP 8 / 20 s regen
  retune plus temperament escalation — health now reads as the shaft's
  lower window tier going dark, not a pip column (backlog Q4: measured
  desktop floor is ~16 s to first blood in sustained steady prose — decide
  8 vs 10 HP here; the verdict feeds v12 tuning).
- [ ] Confirm doctrine texture: replacement wizards (variant cycle) visibly
  change element mix via affinity, and temperament shows in windup pace and
  form choice after damage swings.
