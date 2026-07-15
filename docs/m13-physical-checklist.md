# M13 two-half physical acceptance checklist

Use the same `artifacts/m13/griffin_hostoled-m13-diagnostic.uf2` on both halves
for measurement, then repeat the final smoke test with
`griffin_hostoled-m13-release.uf2`. Never plug or unplug TRRS
while either half is USB-powered.

## Flash and identity

- [ ] Disconnect USB, then disconnect TRRS.
- [ ] Put the left controller in bootloader mode and copy
  `griffin_hostoled-m13-diagnostic.uf2`.
- [ ] Remove USB, repeat for the right controller with the identical file, then
  reconnect TRRS and power the normal USB half.
- [ ] Confirm both OLEDs leave the stale-link presentation and remain
  synchronized. A v8/v10 mixed pair must instead remain safely stale.

## Typing and collection

- [ ] In a plain-text scratch document, exercise every physical key on all four
  layers, both thumb holds, rolls, chords, rapid alternation, and long holds.
  Confirm every expected host key event arrives exactly once and no spell logic
  consumes or rewrites typing.
- [ ] Type independently on each half and confirm each wizard's ward/collection
  changes only from its own half. Simultaneous activity may affect clashes and
  civic intensity, never the other wizard's recipe identity.
- [ ] Pause after a short sequence and confirm commit after roughly 520 ms.
  Hold a key beyond that interval and confirm collection stays open until
  release.
- [ ] Sustain a sequence to ten seconds: confirm forced commit, no new spell
  input before full release, a protected wind-up, and collection resuming when
  the spell launches.
- [ ] Take a direct unblocked hit during collection and confirm the recipe
  restarts. Confirm burn/status ticks alone do not repeatedly restart it.

## Spell and combat presentation

- [ ] Observe all eight forms across the pair: projectile, singularity,
  fireball, beam, swarm, ground wave, chain, and conjure summon/trap.
- [ ] Confirm right-origin beams mirror left-origin beams: small start, full
  blast, then fizzle, with no gap-side origin inversion.
- [ ] Confirm swarms gather around their caster, launch one orb at a time, and
  can lose individual orbs while survivors continue.
- [ ] Confirm ground waves stay below airborne projectiles; low/mid/high/roof,
  homing, area, and returning/healing paths remain visually distinct.
- [ ] Compare deliberate/decelerating and frantic/accelerating casts; movement,
  cadence, and trails must visibly differ.
- [ ] Confirm wards grow in vertical coverage and thickness with the sequence,
  lose strength when struck, do not refill until a new capacity threshold is
  crossed, and use the fracture animation when shattered. Confirm a tier-1 ward
  leaks one point from a magnitude-2 aligned hit and clears at launch.
- [ ] Exercise a clash, singularity absorption/reflection, combine, detonation,
  healing, and pass-by. Confirm none accidentally displays the old right-side
  ward-deflection flash.
- [ ] Observe a same-element combination cap at magnitude four, an exact-tie
  symmetric pulse, and an ember/frost symmetric pulse. Confirm tier-4 area ward
  coverage can absorb a pulse.
- [ ] Time a damaged wizard: no HP should return before 30 seconds; the pip
  returns at the boundary, and an intervening damaging hit restarts the timer.

## Floors, focus, and the physical gap

- [ ] Focus representative communication/default, browser/research, and
  terminal/build windows. Confirm Commons reads as table + board, Research as
  telescope/analyzer + specimen storage, and Workshop as forge + tool station.
- [ ] Confirm the astral half shows curved/orb variants and the mechanical half
  squared/gear variants without obscuring residents, couriers, health, or roof
  combat. `SPECIAL` must never appear.
- [ ] For each focus change, observe roughly 600 ms of source shutter, full
  brick/elevator wipe, target reveal, and settling dust/sparks. Change focus
  rapidly and confirm the animation restarts from the latest target without
  replaying queued rooms.
- [ ] Let both OLEDs sleep, change focus, and confirm the focus event does not
  wake them. Wake with a physical key and confirm both snap directly to the
  current floor.
- [ ] Observe ordinary, homing/area, phase/void, returning/conjure, beam, and
  chain carriers crossing the desk gap in both directions. Confirm shrinking
  departure/growing arrival, paired edge trails or runes, and synchronized
  beam/chain flares communicate travel while the physical gap duration and hit
  timing remain unchanged.

## Civic aftermath

- [ ] Complete a ten-second cast and confirm both cities coordinate: residents
  watch, world motes/wonder appear, and the sequence resolves into cheering.
- [ ] Observe a roof fireball: roof detonation, resident panic, visible fire,
  firefighter movement/task hat, repair work, and eventual calm restoration.
- [ ] Confirm a light direct-hit complaint is reachable.
- [ ] Confirm residue inspection and repair task hats/marks are legible, remain
  inside the floor band, and do not cover the protected health region.
- [ ] During each arc, verify both halves show the same phase despite their
  opposite viewpoints.

## Link, power, and diagnostics

- [ ] While an aftermath is active, interrupt split connectivity only by first
  removing USB power. Reconnect safely and confirm current state converges
  without replaying a stale phase.
- [ ] Exercise host sleep/wake and USB disconnect/reconnect; normal typing,
  duel state, and host civic presentation must recover.
- [ ] After at least five minutes of mixed typing and maximal casts, run
  `corne-arcane-diagnostics` (or, from `host/`,
  `python3 -m arcane_host.diagnostics`) to read both diagnostic pages. Record
  the master and peer values below.
- [ ] `peak_housekeeping_us < 2000` on both halves.
- [ ] `peak_render_blit_us < 5000` on both halves.
- [ ] Queue overflow remains `0`; protocol/malformed error counters do not grow
  under a healthy link.
- [ ] With a debugger attached, inspect `duel_diag.stack_min_free_bytes` on each
  half after the same stress run; it must remain nonzero with reasonable margin.

| Measurement | Left | Right |
|---|---:|---:|
| Peak housekeeping (us) |  |  |
| Peak render + blit (us) |  |  |
| Minimum free stack (bytes) |  |  |
| Queue overflow |  |  |
| Split protocol errors |  |  |

## Release and rollback

- [ ] Flash `griffin_hostoled-m13-release.uf2` to both halves using the same powered-TRRS safety
  sequence and repeat typing, beam symmetry, maximal aftermath, and reconnect
  smoke tests.
- [ ] Preserve the recorded hashes from `m13-acceptance.md` with the sign-off.
- [ ] Finally flash `m12-rollback.uf2` to both halves and confirm the accepted
  M12 scene still boots; then return both halves to M13 release.

Sign-off date: __________  Tester: __________  Result: __________
