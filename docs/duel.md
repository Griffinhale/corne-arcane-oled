# The duel

This is what the simulation does with your typing. The README says "the way you
type casts spells". This page says how.

Every number here is a constant in
[`firmware/sim/duel_model.h`](../firmware/sim/duel_model.h) or
[`duel_incantation.h`](../firmware/sim/duel_incantation.h), and the rules are in
[`duel_incantation.c`](../firmware/sim/duel_incantation.c) and
[`duel_combat.c`](../firmware/sim/duel_combat.c). Those files are the source of
truth and this page is a reading of them. Tuning values change more readily
than the shapes around them.

One tick is 40 ms. The simulation runs at 25 Hz, so durations below are given
in ticks and in seconds.

## What the simulation sees

A key press enters the simulation as a **position**, meaning a row and a column
on one half. There is no keycode, no character, and no layer-resolved meaning
beyond a small layer index. Positions are packed as `row * 6 + column` into a
24-bit mask per half.

Nothing filters this down from richer data. The function signature is the
boundary: `duel_incantation.c` accepts positions, level masks, layer indices,
and tick counts, and has no parameter through which a keycode could arrive.

So the shape of your typing is the whole input. What you wrote is invisible.
How you moved across the board is not.

## One cast, end to end

```text
   INC_IDLE
      │  first keydown
      v
   INC_COLLECTING ──── 13 ticks (0.52 s) with no key held ──> commit
      │                                                          │
      │  250 ticks (10 s) of continuous typing ──> forced commit │
      v                                                          v
   INC_WINDUP  (8 to 50 ticks, from complexity)            descriptor compiled
      │
      │  windup expires
      v
   spell released at your doorstep, crosses the battlefield
      │
      v
   resolution at the defender's door: deflected, or impact
      │
      v
   residue, fields, aftermath in the city
```

If a spell of yours is still in flight when the wind-up ends, the new one waits
in `INC_PREPARED` and launches the moment the battlefield is clear.

## Collecting

From the first keydown until commit, each press contributes to a running
descriptor. The collector never stores what you typed. It accumulates counters:

| Recorded | Meaning |
| --- | --- |
| `key_count` | how many presses |
| `seen_pos` | how many distinct positions |
| `row_hist` | presses per row, and which row was used most recently |
| `turns` | changes of horizontal direction, and row changes |
| `column_drift` | net travel inward or outward across columns |
| `layer_transitions` | how often the layer changed |
| `overlap_peak` | the most keys held down at once |
| `gap_*` | the spacing between presses: mean, min, max, first, last |
| `rhythm_changes` | how often that spacing changed category |
| `hash` | an FNV-1a rolling hash of the position, layer, and timing tokens |

The hash is what makes two differently shaped bursts of the same complexity
produce different spells. It hashes positions and timing, never content.

## Committing

A spell commits when you stop.

Thirteen ticks (0.52 s) with no key held is the ordinary case. Pause, and what
you just typed becomes a spell.

Two hundred and fifty ticks (10 s) of unbroken typing forces a commit instead.
This is the longest incantation the system will collect, and it is treated as a
civic event, with both towers running a coordinated wonder arc. After a forced
commit the wizard is locked until you release every key.

So a burst of typing followed by a beat is one cast, and continuous typing is
one very large cast every ten seconds.

## Compiling the descriptor

Commit compiles the counters into a 24-bit descriptor holding form, element,
payload, trajectory, magnitude, status, interaction, tempo, and trend. Nothing
else about the cast survives.

### Complexity

Almost everything keys off one score, capped at 255:

| Contribution | Weight | Capped at |
| --- | --- | --- |
| presses | x2 | 64 |
| distinct positions | x3 | 16 |
| turns | x2 | 16 |
| layer changes | x4 | 8 |
| simultaneous keys beyond the first | x8 | 4 |
| rhythm changes | x3 | 8 |

Typing more is worth less than typing differently. Chords and layer changes are
the densest sources of complexity available.

### Element: the row you favour

The element is the row you pressed most during the burst.

| Row | Element |
| --- | --- |
| top | Frost |
| upper middle | Force |
| lower middle | Ember |
| thumbs | Void |

Ties break toward your wizard's doctrine affinity, which cycles with the roster
variant each time a wizard is replaced: Force, Ember, Frost, Void.

### Form: unlocked by complexity

Eight forms exist. How many are eligible depends on complexity, so a short
burst can only ever be a projectile, and the exotic tail needs sustained,
varied typing.

| Complexity | Forms eligible |
| --- | --- |
| under 32 | Projectile only |
| 32 to 47 | plus Fireball and Swarm |
| 48 to 75 | plus Ground wave |
| 76 to 103 | plus Beam |
| 104 to 131 | plus Chain |
| 132 to 159 | plus Singularity |
| 160 and up | plus Conjure, so all eight |

Among the eligible forms the choice is a weighted draw from the hash. Weights
double for the forms your doctrine prefers, and double again for temperament. A
hot wizard leans to Fireball and Chain, a cool one to Conjure and Singularity.

### Magnitude, payload, and the rest

**Magnitude** is complexity in four bands. Under 48 is 1, under 112 is 2, under
192 is 3, and above that 4.

**Payload** is decided in order, first match winning:

| Condition | Payload |
| --- | --- |
| 3 or more layer changes | Hybrid |
| keys held long, or 3 or more held at once | Status |
| strong inward drift, or one hash draw in eight | Heal |
| otherwise | Damage |

**Status** only exists on Status and Hybrid payloads, and follows the element.
Ember burns, Frost freezes, Void disrupts, Force marks.

**Trajectory** is mostly implied by form. Fireball arcs over the roof, Ground
wave travels the ground, Chain homes. Otherwise it follows the row and your
drift, and thumb-heavy or strongly inward casts return to the caster.

**Interaction** is Absorb for Singularity, Phase for anything Void, Combine
after six or more layer changes, and Solid otherwise.

**Tempo and trend** come from the gaps between presses. Mean spacing gives
Deliberate, Flowing, Rapid, or Frantic. The spread and direction of change
gives Steady, Accelerating, Decelerating, or Irregular. These drive
presentation and a few interactions, not damage.

### Wind-up

The compiled spell takes 8 ticks plus up to 42 more in proportion to
complexity, clamped to between 8 and 50 ticks (0.32 to 2 s). A big spell is
slow to release. Being frozen adds to it. A hot wizard is 2 ticks faster, a
cool one 2 slower.

## Wards

Your ward grows while you type, from the same complexity score:

| Complexity | Ward capacity |
| --- | --- |
| under 80 | 1 |
| 80 to 159 | 2 |
| 160 to 223 | 3 |
| 224 and up | 4 |

Capacity only ratchets up within a burst, and strength gains the difference,
capped at 4. Separately, any keydown shields that side for 10 ticks (0.4 s), so
an actively typing wizard is a defended one.

The ward also has a **focus lane**, set by the row you last pressed. The top
row guards the high lane and the lower middle guards low, while both the upper
middle and the thumbs guard the middle. A spell arriving in your focused lane
meets the ward. One arriving elsewhere is likelier to get through.

Typing is how you attack and how you defend at the same time, and the row that
arms your ward is the row that picks your element.

## Resolution

The battlefield is one axis from 0 at the left wizard to 255 at the right.
Spells spawn just outside their caster and travel on their trajectory.
Resolution is rule-based at the defender's doorstep. A raised ward means
**deflect**, and otherwise the spell continues to its impact threshold. A spell
arriving at a wizard who is already down **fizzles**.

Only the master half resolves anything. The `SIMF_AUTHORITATIVE` flag is set on
one side only, so the slave half structurally cannot decide a duel. It has no
code path that advances combat.

## What outlasts the cast

**Residue** collects in zones along the battlefield. Each zone holds one
element and an intensity that decays on a clock. Casting the same element into
a zone that already holds it deepens it, and a bloom-signature spell reaching
its own residue detonates it in an area pulse.

**Fields** are persistent effects on the battlefield: trap, singularity, steam,
rune, familiar, wall, vortex. There are exactly two slots globally, so a third
field displaces an existing one. Durations run from 50 ticks (2 s) for a
singularity to 150 ticks (6 s) for a rune or a wall.

**Signatures** are derived readings of a descriptor, not extra bits in it. A
Conjure with a status payload along the ground reads as a **rune**, a returning
Conjure as a **familiar**, a status Ground wave as a **wall**, a homing or
returning Singularity as a **vortex**, and irregular combining casts of some
size as an **echo** or a **bloom**.

**Aftermath** is what the city does about it. Fire, panic, complaint, repair,
inspection, cheering, and the wonder arc after a maximum cast each run on their
own timer, from 175 ticks (7 s) for a fire to 150 (6 s) for a maximum cast, and
drive what the residents in the room below are doing.

## Between casts

After 75 ticks (3 s) without typing, a stance opens. **Meditate** runs
regeneration at double speed. **Study** shifts your next cast to your doctrine
element, or gains it a magnitude if it was already aligned, and is consumed
once. **Fortify**, held for 50 ticks (2 s), grants a ward pip, once.

Any keypress of your own ends a stance immediately. Two further stances, pacing
and taunting, are drawn locally by the renderer and never cross the link.

**Temperament** is a 0 to 7 value starting at neutral. Taking damage heats you,
having your own spell stopped cools you, and a knockout steps you back toward
neutral. At 6 and above you are hot, at 2 and below cool, which shifts both
form weights and wind-up speed.

## Going down, and coming back

A wizard has 8 hit points and regains one every 500 ticks (exactly 20 s) below
maximum. At zero, a fixed arc runs with no input required: collapse (12 ticks),
down (25), a medic (25), and a replacement walking in (20), for 82 ticks or
3.28 s in total. The replacement returns at full health as the next of four
roster variants, which is also what rotates your doctrine affinity.

## Scry

Holding both layer thumbs with no other key held unrolls an **almanac** on each
display. Seven local readings unroll from the centre, advancing while the chord
is held, freezing and rerolling on release. It is presentation only. It adds no
state to either wire protocol, does not pause combat, and does not wake
sleeping hardware.

## Why it repeats exactly

Fixed 40 ms ticks, integer arithmetic, no allocation, no time reads inside the
mechanics, and a fixed phase order per tick. The same starting state and the
same ordered stream of inputs and events produce a bit-identical world.

That is what makes a catalog of 622 exact framebuffer hashes a usable test, and
it is why every figure in this documentation is regenerated from source instead
of captured.

See also [`glossary.md`](glossary.md) for the vocabulary,
[`architecture.md`](architecture.md) for how the halves divide the work, and
[`protocol-ledger.md`](protocol-ledger.md) for the exact bits that cross each
link.
