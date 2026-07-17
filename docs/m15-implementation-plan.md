# M15 Foundations & Spires — implementation plan (DRAFT)

Grounded in a fresh read of the accepted 0.4 / M14 baseline. Every file:line
anchor verified against the current tree. Nothing here is implemented yet;
this document is the product/design record the backlog asked for before
coding ("Questions to settle before coding", `docs/backlog.md`).

**Scope decision (this session):** M15 combines four confirmed directions and
one gated stretch:

- **Track T — tuning pass** (ships first): flatten form eligibility and
  rebalance lifecycle pacing so exotic spells and roster churn appear from
  ordinary typing.
- **Track G — full-height tower geography**: reclaim the early-dev alert band
  (top) and the dead post-relocation band (bottom) for the main scene; give
  each tower a crown the wizard can actually use and foundations that record
  history. Retire the abstract "wonder wave".
- **Track A — battlefield residue**: session-scale elemental residue on the
  duel axis; spells interact with exchanges from the past, not only with
  simultaneous casts.
- **Track B — living wizards**: temperament drift, doctrine-bearing roster
  variants, and mechanical non-casting stances.
- **Track C (gated) — field objects**: persistent battlefield entities
  (traps, singularities, lingering residue clouds) leave the two in-flight
  spell slots so a new cast can coexist with them. Full second in-flight slot
  per side is explicitly rejected for this milestone (see §7).

Deferred by product decision: element-pair chemistry ("Noita mixing", old
Option D) waits for a possible companion app with a richer display; recorded
in `docs/backlog.md`.

Invariants unchanged: one authoritative master sim, deterministic fixed
ticks, no allocation or blocking I/O on the typing path, physical-position
incantations, privacy-redacted host payloads, no permanent progression (all
new state is session-scale RAM; EEPROM untouched), fully functional world
without the daemon.

---

## 0. Gate status

- 0.4 / M14 software acceptance is recorded in `docs/acceptance.md`; M14
  physical items in `docs/physical-checklist.md` are pending. M15 development
  can begin on desktop rails immediately; M15 **flashing** waits until the
  M14 physical record is either accepted or explicitly superseded.
- Recovery: the preserved `griffin` recovery UF2 plus the accepted M14
  release UF2 are the rollback pair. Both must exist before the first M15
  flash.
- Budget baseline: release 72,164 B flash / 13,480 B static RAM; hard gate
  flash <= 81,896 with the 96 KiB stop and 16 KiB reserve (README). M15
  adopts the same growth ceiling M14 used: **<= 8,192 B flash / <= 512 B
  static RAM per image**, measured against the M14 release artifacts.

---

## 1. Current screen geography and the two reclaimed bands

Portrait canvas 32x128 per half (`duel_draw.h:23`). Today:

| Band | Rows | Owner today |
|---|---|---|
| Alert region | y 1–15 | Protected since early host integration (`DUEL_ALERT_Y0/Y1`, duel_draw.h:29). Actual content: one 5x7 alert sigil in the outer corner plus priority pips (duel_draw.c:1232), stale-link glyph (duel_draw.c:1643), impact corner twitch. Empty most of the time. |
| Sky / rooftop combat | y 16–60 | Combat cluster raised by `DUEL_ROOF_DY = -17` (duel_draw.h:47); sky underlay, wizard, ward, spells, HP pips (y 46–57, duel_draw.c:1388). |
| Ceiling beam | y 61 | `DUEL_FLOOR_BEAM_Y`. |
| Tower floor | y 62–110 | Civic room: occupation furniture, resident, courier, rare events. |
| **Static filler band** | y 111–127 | Decorative texture only: session-seeded pavement (y 112–115) and foundation coursing (capstone y 117, masonry joints y 119–122) laid by the tail of `draw_floor` (duel_draw.c:521–550). No simulation meaning ever lands here. `DUEL_HEALTH_Y0/Y1 = 111–114` (duel_draw.h:31) is a legacy of the pre-relocation layout — HP pips moved up beside the wizard, and the constant's only remaining consumer is the protection test (`sim_test/mechanics_test.c:2213`). Debug builds sweep a 1-px odometer on y 127. 17 rows = 13% of the canvas carrying zero information. |

### 1.1 The "wonder wave" (retire)

Prime suspect for the wave that "pops up randomly":
`draw_floor` paints a moving dotted ripple under the beam
(`64 + ((x + civic_phase) & 3)`, duel_draw.c:363–365) whenever the aftermath
world state is `WORLD_WONDER`. WONDER is driven by `AFTER_MAX_CAST`, which
fires on **any forced incantation commit** — i.e. after 10 s of continuous
typing without a ~0.5 s pause (`INCANTATION_FORCE_COMMIT_TICKS = 250`,
duel_sim.h:225; `inc_commit(forced)`, duel_incantation.c:539–545). During
ordinary sustained typing this reads as a random abstract squiggle.

Two secondary candidates to check in the previewer before deletion, in case
one of them is the wave Griffin means:

- Dawn/dusk dotted horizons at y 55–57 (draw_sky, duel_draw.c:42–53) — a
  static wavy dotted line for a whole sky phase.
- Typing-ambience drift dots above the resident's work anchor
  (draw_typing_ambience, duel_draw.c:67–86).

**Verification step (first commit of Track G):** render the three candidates
in the sim_test previewer, confirm with Griffin which one(s) he means, then
retire. Plan of record: the WONDER ripple is replaced by the Track G
big-cast presentation (§2.3) so "wonder" is expressed by architecture and
residents instead of an abstract overlay; the aftermath *state machine* is
untouched.

### 1.2 Stale constants (retire)

- `DUEL_HEALTH_Y0/Y1` and its branch of the protection test: replaced by the
  new foundation band contract (§2.2).
- The `DUEL_ROOF_DY` comment's alert-band constraint ("draw_charge reaches
  cy-6, which must stay below DUEL_ALERT_Y1", duel_draw.h:44) is renegotiated
  by §2.1: the crown becomes part of the scene, and big-cast anticipation is
  *allowed* to enter it deliberately.

---

## 2. Track G — full-height tower geography

**Status: complete** (geography landed 2026-07-16, weight pass 2026-07-17):
tower + peaks + windows + balcony, deck + crenellations, stone-course
border, arcing celestial (4 coarse positions pending the v11 sub-phase),
alert banner on the shaft, scry alert summary relocated to the gap-side
strip (the peak art is asymmetric and the scry mirror contract requires
symmetric instrument cells), collection runes moved below the balcony
(their old rows became the slab), wonder wave retired, big-cast tower glow
in. `DUEL_ROOF_DY` was kept as the deck offset constant rather than
rebasing 33 coordinate sites — the alert-band constraint on it is gone,
which was the point. The §2.0 weight pass followed: the wizard gained a
head, a chunky bent-tip hat, a solid narrow-shouldered robe, and a 2-px
staff with an orb finial (launch coordinates unchanged); frost carriers
became solid-core stars, void a donut ring, ember a teardrop with a thick
near tail; trails render as a solid stub plus fading tempo dots; the ward
is a continuous parabolic double arc (2-3 px by strength) with end
anchors and a focus notch, same strength/focus/puncture grammar; impact
and detonate flourishes scale one presentation tier up. Full suite green;
goldens re-baselined (346 scenes) with pairwise uniqueness intact.

Each tower becomes one continuous structure from crown to foundation, in
city character (astral = curved, domed; mechanical = squared, riveted —
same split as the existing motifs, duel_draw.c:142–163).

### 2.0 Legibility requirements (gallery review, 2026-07)

Reviewing the rendered scenario gallery against the current firmware set
four requirements for this track (mockup:
`scratchpad gallery mockup-vs-current.png`, reproduced by
`mockup.py`; to be attached to the milestone record):

1. **The wizard's tower must exist.** Today the combat cluster floats in a
   void above the beam — the wizard, pips, and ward read as disconnected
   marks ("tower on top of tower"). Fix (design revision 2, per gallery
   review): a **half-width wizard's tower** — a ~14-px shaft on the outer
   side of each canvas rising from the main tower's roof (y 61) to a top
   full architectural peak near the top of the screen, with the wizard on
   a gap-side balcony partway up (rev 3: the peak stays complete in every
   state). Astral peaks in a dome + finial; mechanical in a crenellated cap
   + beacon mast; both get shaft windows that render lit at night. The
   main-tower roof keeps a thickened 2-px deck with crenellations on the
   open gap-side strip.
2. **Presentation weight pass on magic.** Carriers, wards, and one-shot FX
   are too thin to register at desk distance. Fix: 2-px spell cores with a
   solid 3x3 element head and fading trail; the ward becomes a continuous
   double arc with a focus notch (replacing the sparse dotted arc); impact/
   detonate flourishes scale up one tier. Applies to `incantation_draw_spell`,
   `draw_ward`, and `draw_local_fx`.
3. **One celestial body arcing across the desk.** The sky's scattered
   per-phase dots read as noise, and mirroring a sun to both halves reads
   as two suns. Fix (rev 4): a single small sun disc (r=2 + 4 rays) or moon
   crescent that **arcs across both screens through the cycle** — rising in
   the left half's sky strip, peaking near the gap, descending through the
   right half — so the pair of OLEDs reads as one panorama. Position is
   derived from sky phase plus a new 2-bit sub-phase on the wire (16 arc
   steps per full cycle; see §6). At most 3 stars at night. Dawn/dusk
   horizon dot-rows retire along with the wonder wave.
4. **The reclaimed glyph band must visibly belong to the scene.** The crown
   (§2.1) owns y 0–15 with a large silhouette; nothing in the band may read
   as reserved instrument space outside an active alert.

### 2.1 The wizard's tower and the upper band (y 0–61)

Revision 2 (supersedes the original thin-crown sketch after gallery
review; mockup `mockup2.py` / `mockup-v2.png`):

- **Structure** (rev 3): half-width shaft (outer side, ~x 0–14 in desk
  space) from the roof deck (y 61) rising into a **full architectural
  peak** near the top of the screen — astral: taper, dome, and finial;
  mechanical: crenellated cap with a beacon mast. Shaft windows draw as
  outlines by day and filled (lit) at night — the sky phase becomes legible
  from the architecture itself.
- **Two wizard stations** (rev 4). Ordinary dueling happens on the
  **rooftop deck** beside the shaft (feet on the 2-px deck at y 61, spell
  lanes and ward in the gap-side strip at roof altitude, roughly where the
  combat cluster sits today — `DUEL_ROOF_DY` still retires; the deck
  position becomes explicit). The **gap-side balcony** partway up the shaft
  (slab at y ~36, corbel-supported) is the elevated station: big casts and
  the calm Track B stances (MEDITATE/STUDY) restage there, so looking over
  mid-work tells you at a glance whether your wizard is fighting or
  studying. The peak is never occupied in any state. The upper strip above
  the balcony stays open sky for the celestial arc's apex.
- **The alert moves into the world**: the sigil corner is retired; a host
  alert becomes a **bell on the astral turret / lamp on the mechanical
  mast** — category selects the instrument glyph (reusing the 5x7
  `alert_glyphs` bitmaps, duel_draw.c:1202, repositioned), priority sets its
  swing/blink cadence, persistent alerts keep it visibly tolling/lit. Scry
  keeps its richer normalized panel exactly as today (duel_draw.c:1328–1342).
- **Big-cast use**: on a forced commit / magnitude-4 windup, a charge halo
  radiates around the balcony wizard, the peak finial/beacon flares, and
  motes rise past the shaft windows — the whole tower lights up. Presentation only (derived from
  `inc_state == INC_WINDUP && rearm_lock` plus windup progress already on
  the wire, duel_view.c:44–49); it replaces the wonder wave as the
  "civic-scale event" visual, and residents already look up
  (`RESIDENT_WATCH_CAST`).
- **Open design item**: the HP pips currently float beside the wizard; with
  the shaft available they could become lit shaft windows going dark as
  damage lands (8 slots after the §4.4 retune) — diegetic health. Lifecycle
  tableaus (collapse/downed/medic/replace) restage on the balcony; the
  medic enters from the shaft doorway behind it.

### 2.2 Foundation band (y 111–127, reclaimed from the dead band)

- **Rev 5 simplification**: no buttresses, no undercroft, no bottom residue
  ledger. The pavement/pathway texture (duel_draw.c:521–550) is replaced by
  a **single stone course as the bottom border** — two rails at y ~112 and
  y ~116 with masonry joints between, seed-staggered per city. Rows below
  stay dark; the debug odometer keeps y 127 (debug builds only).
- **Residue display moves to where duels happen**: Track A's zone marks
  draw as compact scorch/rime/scar/rubble marks on the rooftop deck at
  their zone positions (doorsteps beside the crenellations, mid-zones along
  the deck), not in a bottom ledger. Repair aftermaths visibly fade them.

### 2.3 Protection contract update

The protection test's region list (`mechanics_test.c:2212`) changes from
{alert band, health band} to {crown instruments, foundation band, HP pips,
beam}. Golden frames re-baseline wholesale — geography touches nearly every
scenario, so Track G lands **first** among the drawing tracks and the
gallery is re-accepted once, not per-track.

---

## 3. Track A — battlefield residue (authoritative, session-scale)

**Status: complete** (landed 2026-07-17, on top of the v11 repack):
`sim_world_t.residue[4]` with `residue_step` pinned between `collision_step`
and `spell_step`; the full deposit table (impact/fizzle doorsteps,
ember×frost clash mids, singularity void scar, fire/repair aftermath hooks),
the ×5-prescaled 45 s decay clock, and all four transmutation rows with the
once-per-spell flag in `sp->resolved` bit 7. The encoder fills the v11 zones
from the world and `duel_snapshot_set_civic` now preserves the borrowed
zone-3 bits (no ordering rule). Deck marks render at mirrored battlefield
anchors (u 13/48/207/242 → x 23/26 left, 5/8 right), horizontally symmetric
per element — force mound, ember flame column, frost twin shards, and void
eating a literal hole in the deck; density scales with intensity. Two new
mechanics tests plus five golden scenes (355 total); the prose-KO guardrail
widened to 50–180 s (the feed reaction lands first KOs ~10-15 % earlier —
re-measured at the Track B/T HP retune).

### 3.1 Sim design

Four fixed zones on the u-axis (0–255): doorstep-L (u~8–48), mid-L
(u~48–128), mid-R (u~128–208), doorstep-R (u~208–248).

Per zone: `element` (2 b) + `intensity` (2 b, 0 = empty) on the wire;
`decay_ticks` (u8, ~45 s per intensity step) master-local. Whole array lives
in `sim_world_t` behind the authoritative gate.

Deposits (all capped at intensity 3):

- Impact/detonate at a doorstep: +element at that doorstep zone.
- Ember/frost symmetric detonation (`symmetric_area_pulse` callers,
  duel_incantation.c:828–833): +1 both mid zones.
- Fizzle at a downed wizard: +1 doorstep, that spell's element.
- Singularity collapse: void +2 at its mid zone.

Transmutation when an active spell's u enters a zone with intensity >= 2
(checked in `spell_step` before collision; one reaction per spell lifetime,
flagged in `sp->resolved` spare bits):

| Spell element × zone element | Reaction |
|---|---|
| ember × frost (or frost × ember) | Steam burst: zone cleared, 1-damage area pulse via the existing `resolve_payload` area path, FX_DETONATE |
| void × any | Absorb: zone intensity -1, spell magnitude +1 (cap 4) |
| force × force | Rubble scatter: zone -1, spell trajectory bumps one lane |
| same element | Feed: zone -1, spell magnitude +1 (cap 4) once |

Aftermath hooks: `AFTER_REPAIR` on a side also decays that side's doorstep
zone by 1 (visible repair); `AFTER_FIRE` deposits ember +1.

Rendering (rev 5): residue marks draw on the rooftop deck at their zone's
battlefield position — element-typed 2–3 px marks whose density scales
with intensity — so the duel's history sits directly under the spell
lanes.

### 3.2 Determinism and tests

New sub-step `residue_step` slots into the pinned tick order between
`collision_step` and `spell_step` (duel_incantation.c:1201–1203) — order is
re-pinned and the world-hash streams re-baselined once for the milestone.
Mechanics tests: deposit table, decay boundary, each transmutation row,
zone/lane interaction with wards unchanged (residue never touches
`ward_covers`).

---

## 4. Track B — living wizards

### 4.1 Temperament (per wizard, session-scale)

`temper` 0–7, starts 4 (even). Drift, applied at resolve time: damage taken
+1 (cap 7), own spell deflected/shattered -1 (floor 0), KO resets toward 4
by one step on replacement. Effects:

- Form weighting: `choose_form` (duel_incantation.c:81) gains a temper term —
  high temper doubles FIREBALL/CHAIN weights, low temper doubles
  CONJURE/SINGULARITY.
- Windup: high temper -2 ticks, low temper +2 (within existing clamps).
- Stance selection (§4.3). Temperament has no wire bits of its own: its
  visible expression rides the stance channel, windup length, and form
  weighting (the original 2-bit "temperament tier" cue was traded to the
  sky sub-phase in §6).

### 4.2 Doctrines (roster variants stop being cosmetic)

Variant 0–3 keeps its 2x form bias and gains an **element affinity**
(0: force, 1: ember, 2: frost, 3: void): ties in `dominant_row` break toward
the affinity, and STUDY (below) shifts the next cast's element toward it.
Replacement wizards therefore visibly change the duel's texture.

### 4.3 Non-casting stances (mechanical, deterministic)

New `stance` field, sub-state of `LIFE_ACTIVE`, chosen by rule when a wizard
has been INC_IDLE for >= 75 ticks (3 s):

| Stance | Entry rule | Mechanics | Exit |
|---|---|---|---|
| MEDITATE | hp <= half, temper low | regen at 2x (`SIM_REGEN_TICKS/2`), ward suppressed while held | any own keydown |
| STUDY | temper mid, hp fine | next compiled cast: +1 magnitude (cap 4) or element shift to affinity | on next commit |
| FORTIFY | temper high or opponent windup visible | ward_strength +1 (cap 4) once after 50 ticks | any own keydown |
| PACE/TAUNT | otherwise, alternating by tick parity | none (presentation) | any own keydown |

All entry rules read only authoritative state already in `sim_wizard_t`, so
the slave renders stance purely from the wire. Stances end instantly on
keydown — the typing path never waits on stance logic.

### 4.4 Pacing retune (with Track T)

`SIM_MAX_HP` 12 -> **8** and `SIM_REGEN_TICKS` 750 -> **500** (20 s), so KOs
and doctrine turnover actually occur during ordinary typing days. HP pip
geometry (duel_draw.c:1385) shrinks to 8 pips; the freed rows join the ward
column. Watch item for physical acceptance: KO cadence must not become
restless (backlog Q4).

---

## 5. Track T — tuning pass (ships first, no protocol change)

- Flatten `choose_form` eligibility (duel_incantation.c:86–88): open 4 forms
  by complexity 48, all 8 by 160 (today: 8 forms only above 224, which
  ordinary typing essentially never reaches).
- Raise base weights of SWARM/CHAIN/CONJURE from 2/2/1 so the exotic tail
  appears at realistic complexity.
- Land HP/regen retune here if Track B lags.

Pure constant changes inside the authoritative sim; world-hash streams
re-baseline, wire untouched. Goes to hardware immediately for ambient-variety
feedback while the rest of M15 is in flight.

---

## 6. Track P — split snapshot v11

**Status: complete** (landed 2026-07-17, after the Track G render audit):
version bumped to 11, ledger updated, decode rejects v10/corrupt frames.
The sky sub-phase is wired end-to-end — master derives the quarter of the
current phase, the slave renders it, and the celestial arc has its 16
steps (sun tucks behind the left tower at dawn, splits across the gap at
midday, sets behind the right tower at dusk; moon drifts the night sky).
Residue rides zones 0-1 in the freed seq byte, zone 2 in flags, zone 3
scattered across civic/flags/secondary behind `duel_snapshot_residue_*`
accessors; stances ride the view fx byte's high nibble (`fx_stance`).
Residue/stance ship zeroed for Tracks A/B. Mechanics tests cover the
round-trip, the zone-3 straddle boundary, fx-nibble wrap vs the flash
policy, sub-phase quarter boundaries, and the v10 version gate; goldens
re-baselined (350 scenes, +4 arc probes).

v10 has zero spare bytes (`docs/protocol-ledger.md`). Inventory that funds
v11 without growing past 32 bytes:

| Source | Freed |
|---|---|
| `seq` 2 B -> 1 B (wrapping 8-bit is ample for stale detection at snapshot cadence) | +8 b |
| `fx_seq` 8 b -> 4 b (wraps; consumers compare inequality only) | +4 b |
| `flags` bits 3–7 (reserved) | +5 b |
| `civic` bits 6–7, `secondary` bits 5–7 (reserved) | +5 b |
| **Total** | **22 b** |

Allocation:

| New field | Cost |
|---|---|
| Residue zones: 4 × (element 2 b + intensity 2 b) | 16 b |
| Stance: 2 × 2 b (stance id: none/meditate/study/fortify; pace/taunt derive locally from stance=none + idle, seed) | 4 b |
| Sky sub-phase: 2 b (celestial arc position within the phase; ×4 phases = 16 arc steps per cycle) | 2 b |
| **Total** | **22 b** |

Version bumps to 11; validator gains range checks for the new fields; a v10
half sees a version mismatch and takes the existing stale-link presentation
(README behavior, unchanged). Ledger updated in the same commit that changes
the packer.

Raw HID v2 is untouched — nothing in M15 needs new host semantics.

---

## 7. Track C (gated) — field objects, not a second in-flight slot

**Rejected:** a second full in-flight spell per side. Each wire spell costs
4 bytes (duel_view.h:11) — +8 bytes cannot fit v11, and the hash-pinned
collision precedence ladder (duel_incantation.c:846) would need a full
pairwise redesign. That is a milestone of its own, not a stretch goal.

**Plan of record instead:** *field objects*. Today a set trap or a growing
singularity **occupies its caster's only spell slot** for its whole
lifetime (`w->spell[side]`, duel_sim.h:306), which is exactly why spells
rarely coexist. M15 moves persistent entities into a 2-entry field array:

- `field[2]`: kind (3 b: none/trap/singularity/steam-cloud) + zone (2 b) +
  age bucket (2 b) + owner (1 b) = 1 byte each; master-local fine timers.
- When a CONJURE trap arms or an uncharged singularity finishes growing, it
  *transfers* from the spell slot to a free field entry (no free entry: it
  resolves immediately, today's behavior). The caster's slot frees, so their
  next cast flies **over/into their own standing trap** — routine
  coexistence.
- Collision ladder change is additive: in-flight spells check field entries
  (trap detonation via the existing `collide_trap` path, singularity capture
  via `collide_singularity`) before the existing spell-vs-spell ladder.
  Steam clouds (from residue transmutation) become visible lingering
  objects instead of instant pulses.

Wire cost: 2 bytes — which v11 as budgeted above does **not** have.
Funding options, in preference order: (1) compress the two spell descriptors
on the wire by dropping the `variance` bits (2 b × 2, presentation-only
jitter seed — slave can substitute the session seed) and `interaction`
(2 b × 2, recomputable on the slave from element+form for every compiled
descriptor); (2) hold C for v12. Decision at the **C gate**: after Tracks
T/G/A/B measure flash and the descriptor-compression spike proves the slave
reconstruction is exact on the full compile domain, C proceeds only if
projected total growth stays under 8,192 B minus a 1 KiB margin.

---

## 8. Budgets

Flash allocation against the 8,192 B ceiling (estimates, checkpointed after
each track lands; `make release-budget` per merge):

| Track | Estimate |
|---|---|
| T tuning | ~0 |
| G geography (crown/foundation/buttresses/alert-bell, wave retirement, §2.0 legibility pass on wizard/spells/ward/sky) | ~2.4 KiB |
| A residue (sim + foundation ledger drawing) | ~1.7 KiB |
| B living wizards (stances + doctrine + temperament + drawing) | ~2.4 KiB |
| P v11 pack/unpack/validate | ~0.4 KiB |
| C field objects (if gated in) | ~1.4 KiB |
| **Total** | **~8.2 KiB with C / ~6.8 KiB without** (C is admitted only if measured headroom allows; first overrun ejects C, second trims B's PACE/TAUNT art) |

Static RAM: residue array + stances + temperament + field entries < 40 B in
`sim_world_t` (×2 harness instances on desktop, ×1 in firmware) — well under
the 512 B ceiling. `duel_render_t` stays within its 40 B assert
(duel_draw.h:214).

## 9. Sequencing

1. **T** — constants only; flash to hardware for early feedback.
2. **G** — wave verification with Griffin in the previewer, then crown +
   foundation + alert-bell; golden gallery re-baselined once.
3. **P** — v11 skeleton (repack + validator + ledger) with fields zeroed.
4. **A** — residue sim + foundation ledger rendering (fills v11 fields).
5. **B** — temperament/doctrine/stances (fills remaining v11 fields).
6. **C gate** — measure, spike descriptor compression, go/no-go.
7. Acceptance: full desktop matrix (world-hash, gallery, no-alloc,
   ASan/UBSan, host pytest), release budget, then the two-half physical
   checklist per `docs/physical-checklist.md`, including desk-distance
   legibility of crown instruments and foundation marks.

## 10. Open questions (Griffin)

1. Wave identity: is the WONDER ripple the one? (Verified together in the
   previewer at the start of Track G; §1.1 lists two alternates.)
2. Crown forms: minaret + signal mast as proposed, or symmetric minarets?
3. Alert instrument: bell (astral) / beacon cage (mechanical) acceptable as
   the *only* passive alert surface, with scry keeping the detailed panel?
4. HP 8 vs 10 — how frequent should KOs feel? (Tunable after T on hardware.)
5. Stance set: MEDITATE/STUDY/FORTIFY mechanical + PACE/TAUNT cosmetic — cut
   or add any before art lands?
