# Glossary

Vocabulary used across the firmware, the documentation, and the scene names in
the visual catalog.

Terms about the world are marked with what decides them. **Authoritative**
state is decided by the master half and crosses the link. **Presentation** is
derived locally by each half and never crosses it.

For how the pieces fit together, see [`duel.md`](duel.md).

**Aftermath** (authoritative). What the city does after a spell resolves:
cheering, complaint, panic, fire, inspection, repair, or the wonder arc that
follows a maximum cast. Each runs on its own timer and drives the residents and
the room below.

**Affinity**. See *doctrine*.

**Almanac** (presentation). The scroll unrolled on each display by the *scry*
chord. Seven local readings, advancing while the chord is held, frozen and
rerolled on release.

**Attunement** (presentation). A local overlay drawn beneath the duel. Part of
the living world, not a combat rule.

**Authoritative**. Decided by the master half. Guarded by a world flag set on
one side only, so the slave has no code path that advances combat.

**Battlefield**. The single axis the duel happens on, running 0 at the left
wizard to 255 at the right. Spells travel along it.

**Bloom** (authoritative). A *signature*: an area, hybrid-payload spell of some
size. A bloom reaching its own residue detonates it.

**Canvas**. One 32x128 OLED's worth of pixels. A dumped catalog frame is 67
pixels wide because it holds both canvases with a separator between them.

**Civic**. Everything about the city instead of the duel: districts, rooms,
residents, couriers, aftermath.

**Complexity**. A 0 to 255 score compiled from a burst of typing: how many
keys, how many distinct ones, direction changes, layer changes, chords, and
rhythm changes. Drives spell magnitude, which forms are available, ward
capacity, and wind-up time.

**Courier** (presentation). One visitor occupying the active floor, derived
from the host's normalized notification summary. Kinds are beacon, messenger,
parcel, and sentinel. Notification count scales one visitor's density, never
the number of actors. No coordinates or sprites cross the link.

**Deflect**. The resolution where a spell meets a raised ward at the defender's
doorstep instead of landing.

**Descriptor**. The 24-bit compiled result of one incantation: form, element,
payload, trajectory, magnitude, status, interaction, tempo, and trend. Nothing
else about how you typed survives the compile.

**District**. One of eight areas of the city: Commons, Research, Workshop,
Observatory, Scriptorium, Studio, Arena, Undercroft. Each has a room, a
resident, and a distinct look in both architectural voices.

**Doctrine** (authoritative). A wizard's elemental affinity, cycling with the
*roster variant*: Force, Ember, Frost, Void. Breaks element ties and doubles
the weight of two spell forms.

**Doorstep**. The position just outside each wizard where arriving spells
resolve.

**Echo** (authoritative). A *signature*: an irregular, combining cast of some
magnitude.

**Element**. Force, Ember, Frost, or Void. Decided by the row you pressed most
during a burst: top row Frost, upper middle Force, lower middle Ember, thumbs
Void.

**Familiar** (authoritative). A *signature*: a returning Conjure. Also a
*field* kind.

**Field** (authoritative). A persistent effect on the battlefield: trap,
singularity, steam, rune, familiar, wall, or vortex. Exactly two slots exist
globally, so a third displaces one.

**Fizzle**. A spell dissipating at the doorstep of a wizard who is already
down.

**Form**. The shape a spell takes: projectile, fireball, swarm, ground wave,
beam, chain, singularity, or conjure. How many are available is gated by
complexity.

**Golden**. A committed hash of an exact framebuffer. The catalog holds 622 of
them, and they are reviewed as images instead of regenerated on failure.

**Half**. One side of the keyboard. The **master** is the half connected to
USB, and it runs the simulation. The **slave** renders what it is sent and
never recomputes the world.

**Incantation**. One burst of typing, from the first keypress until it commits.
The thing that gets compiled into a descriptor.

**Ingredient**. One keypress counted toward the *recipe tier*.

**Interaction**. How a spell meets things in its path: solid, phase, absorb, or
combine.

**Layer**. The normalized layer index the simulation sees. Not a keymap layer's
contents, only that it changed and how often.

**Lane**. The high, middle, or low band a spell travels in. A ward guards one
lane at a time.

**Magnitude**. A spell's size, 1 to 4, banded from complexity.

**Payload**. What a spell does on arrival: damage, heal, status, or hybrid.

**Pose**. A wizard's animation state: idle, casting, or recovering.

**Position**. A physical key location as `row * 6 + column`. The only thing
about a keystroke that enters the simulation. Never a keycode, never a
character.

**Projection**. A bounded, read-only view of world state prepared for rendering
or for the wire. Presentation code reads projections and never decides
mechanics.

**Rearm**. The lock applied after a forced ten-second commit. Cleared by
releasing every key.

**Recipe tier** (presentation). Short, medium, long, or saturated, from how
many ingredients a cast collected. Drives how the cast flash is drawn and
nothing else. It never changes damage or any combat rule.

**Residue** (authoritative). Elemental deposit left in a zone of the
battlefield, with an intensity that decays on a clock. Casting the same element
into a zone that holds it deepens it.

**Resident** (presentation). The figure in a district's room, whose state
follows the aftermath: normal, cheering, complaining, panicking, fighting a
fire, inspecting, repairing, watching a cast, or one of three diplomatic
states.

**Roster variant**. Which of four cosmetic wizards currently occupies a side.
Advances on each replacement and carries the *doctrine* affinity with it.

**Rune** (authoritative). A *signature*: a Conjure with a status payload along
the ground. Also a long-lived *field* kind.

**Scene**. One named case in the visual catalog, with a committed framebuffer
hash. There are 622.

**Scry**. The chord of both layer thumbs with no other key held, which unrolls
the *almanac*. Presentation only: it adds no wire state, does not pause combat,
and does not wake sleeping hardware.

**Side**. Left or right, as a world index. In effect names the side is the
defender, meaning the screen taking the hit.

**Signature**. A derived reading of a descriptor instead of extra bits in it:
rune, familiar, wall, vortex, echo, or bloom.

**Snapshot**. The 32-byte packet crossing the TRRS link each tick, carrying the
world's presentation projection. The slave validates version, ranges, reserved
bits, and CRC before drawing it.

**Stance** (authoritative). What a wizard does when you stop typing for three
seconds: meditate, study, or fortify. Any keypress ends it instantly. Pacing
and taunting are drawn locally and never cross the link.

**Status**. A condition carried by a spell: burning, frozen, disrupted, or
marked, following the element.

**Temperament** (authoritative). A 0 to 7 value, neutral at 4. Taking damage
heats a wizard, having a spell stopped cools one. Shifts form weights and
wind-up speed.

**Tempo**. The mean spacing between keypresses, as deliberate, flowing, rapid,
or frantic. Paired with **trend**: steady, accelerating, decelerating, or
irregular.

**Tick**. 40 ms. The simulation advances exactly once per tick and reads no
clock inside its mechanics.

**Trajectory**. The path a spell takes: ground, low, mid, high, roof,
returning, area, or homing.

**Voice**. One of the two architectural styles the city is drawn in: curved and
astral on the left canvas, squared and mechanical on the right.

**Ward**. A directional shield that grows as you type. Has a capacity and
strength up to 4 and a focused *lane* set by the row you last pressed. Any
keypress also shields that side briefly.

**Wind-up**. The delay between a spell committing and being released, 8 to 50
ticks in proportion to complexity.

**Wizard**. One of the two duellists. Has hit points, a ward, a stance, a
temperament, a doctrine, and a roster variant.

**Zone**. A division of the battlefield that can hold residue.
