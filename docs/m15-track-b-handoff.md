# M15 handoff — Track B, Track T, and the road to the C gate

Written 2026-07-17 at commit `aa845a1`. Plan of record: `docs/m15-implementation-plan.md`
(§4 Track B, §5 Track T, §7/§9 the C gate). Wire authority:
`docs/protocol-ledger.md` (v11).

## 1. Where the milestone stands

Landed, in order: Track G geography (`fb91772`) + weight pass (`12113cc`) +
render-audit fixes (`96151e2`), Track P v11 repack (`3d91e46`), Track A
battlefield residue (`aa845a1`). All tracks carry a **Status: complete**
paragraph in the plan; read those before touching their surfaces.

Verification state at `aa845a1`:

- Root `make test` green: mechanics (incl. `residue_*` and the v11 repack
  tests), 355 visual goldens (pairwise-unique), noalloc-check, 75 host
  pytests. `make release-budget` PASS — but note it measures the **M14
  release artifacts**; no M15 firmware image has been compiled or flashed
  yet (M14 physical record still gates flashing, plan §0).
- `firmware/keymap.c` edits from Tracks P and A (sky sub-phase packing,
  residue tx-change masks, slave residue unpack) are **QMK-side and have
  never been compiled** — desktop rails don't build them. First QMK build
  of the milestone should happen before the C gate measurement, since the
  gate is a flash-size decision.

Remaining before the C gate, in the plan's §9 order: **Track T** (never
landed despite "ships first" — flashing was gated anyway, so it slipped),
**Track B**, then the gate itself (§5 below).

## 2. Surfaces already wired for Track B

The v11 stance channel is live end-to-end except for the two producer sites
and the renderer:

- **Wire/view**: `duel_view.h` — `fx_stance` byte, `VIEW_FX_PACK/SEQ/STANCE`
  macros, `DUEL_STANCE_NONE/MEDITATE/STUDY/FORTIFY` enum. The packer
  (`duel_view.c:59`) currently hardcodes `DUEL_STANCE_NONE, DUEL_STANCE_NONE`
  — that is the one line the sim's new stance fields replace. The decoder
  already surfaces `duel_view_wizard_t.stance` per side; all 2-bit values
  are legal so `duel_view_valid` needs no new check.
- **Flash policy**: `duel_flash_observe_view` masks the fx nibble, and the
  v11 mechanics test pins that a stance-only change does **not** re-arm a
  flash. Don't weaken that test; it is the reason stances could share the
  byte at all.
- **Renderer**: `wiz_draw_scene` has `wz->stance` available (currently
  unread). The balcony is built and waiting: slab desk x11–16 at y30–31,
  corbel x13/y32 + x12/y33 (`duel_draw.c:161-166`, comment marks it as the
  Track B restage point). MEDITATE/STUDY restage the wizard **on the
  balcony** (plan §2.1 rev 4); FORTIFY stays on deck with the ward;
  PACE/TAUNT derive locally from stance==NONE + idle + render seed and
  **never ride the wire** (`duel_view.h:33`).
- **Render budget**: `duel_render_t` is 38 of 40 asserted bytes
  (`duel_draw.h:230`). Track B needs no new render fields (stance rides the
  view), so the 2 spare bytes stay for C.
- **Residue interplay**: `sim_world_t.residue` is authoritative and already
  ticking; STUDY's "element shift toward affinity" changes which
  transmutation row a cast will hit — free gameplay depth, no extra code.

## 3. Track B implementation notes (plan §4)

### 3.1 Sim state and hashing rules

`sim_wizard_t` gains `temper` (0–7, start 4), `stance` (sub-state of
`LIFE_ACTIVE`), and a stance/idle tick counter. House rules that bite here:

- The struct is hashed world state: keep it free of implicit padding
  (all-u8 tail is fine; there's an existing spare `_pad` byte at
  `duel_sim.h` you can consume first) and update the size guard in
  `test_layout_and_protocol` if `sim_world_t` grows.
- Every field must be reset correctly on `wizard_ko`/`wizard_interrupt`/
  replacement: KO steps temper one toward 4 (§4.1), stance resets to NONE.
- World-hash determinism: any new randomness must come from `inc.hash` or
  the session seed, never wall clock.

### 3.2 Entry/exit (the §4.3 table, with anchors)

Entry evaluated only when `life == LIFE_ACTIVE && inc_state == INC_IDLE`
held ≥ 75 ticks. Note `sim_wizard_t.recipe_idle` is recipe-scoped — add a
separate idle counter rather than overloading it. Exit **on any own
keydown**: clear stance in `inc_keydown` (and on the level-sampled rising
path in `sim_tick`), which keeps the typing path free of stance logic —
exit is a byte write, entry only ever runs on idle ticks.

Mechanics per stance:

- MEDITATE (hp ≤ half, temper low): regen at `SIM_REGEN_TICKS/2`, ward
  suppressed while held. Ward suppression should gate `ward_covers` input
  (strength presented as 0 on the wire) rather than destroy stored
  strength, so a keydown restores the ward instantly.
- STUDY (temper mid, hp fine): buff the **next** compiled cast (+1
  magnitude cap 4, or element shift to doctrine affinity). Cleanest hook:
  a pending flag consumed in `inc_commit` after `incantation_compile`.
- FORTIFY (temper high or opponent windup visible): `ward_strength +1`
  (cap 4) once after 50 held ticks. "Opponent windup visible" ==
  `w->wiz[side^1].inc_state == INC_WINDUP` — authoritative state, fine.
- PACE/TAUNT: no sim state at all — renderer-derived from NONE + idle.

The slave renders stance purely from the wire; keep every entry rule
reading only authoritative fields so the two halves can never disagree.

### 3.3 Temperament and doctrine

- Drift at resolve time in `resolve_payload` (damage taken +1) and the
  deflect/shatter paths (own spell stopped −1); clamp 0–7.
- `choose_form(complexity, variant, hash)` (`duel_incantation.c:120`) gains
  a temper parameter — high temper doubles FIREBALL/CHAIN weights, low
  doubles CONJURE/SINGULARITY. This changes the signature of
  `incantation_compile(inc, variant)` too; its callers are `inc_commit`
  plus ~8 sites in `mechanics_test.c` (grep `incantation_compile(`) that
  pass a bare variant — they'll need a temper argument (pass 4 for
  neutral to preserve those tests' intent).
- Windup: ±2 ticks by temper inside the existing
  `INCANTATION_WINDUP_MIN/MAX` clamps (`inc_commit`).
- Doctrine affinity (variant 0–3 → force/ember/frost/void): tie-break in
  `dominant_row` toward the affinity's row and STUDY's element shift.
  Note `row_element` order is {frost, force, ember, void} by row — the
  affinity→row mapping needs that table, not the enum order.

### 3.4 The §4.4 pacing retune (with Track T)

`SIM_MAX_HP` 12→8 and `SIM_REGEN_TICKS` 750→500. Ripples to catch:

- `hp_pip_xy` (`duel_draw.c:1580`): 12 pips → 8; freed rows join the ward
  column. The view's 4-bit hp field and `duel_view_valid`'s
  `hp <= SIM_MAX_HP` follow automatically.
- Golden scene `sky_commons_dawn_idle_12hp` — rename it (8hp) or it lies.
- The prose-KO guardrail (`test_prose_typing_ko_window`) is already
  widened to 50–180 s for residue; re-measure after the retune and reset
  honest bounds (plan Q4: KO cadence must not feel restless). The three
  DIAG lines print actuals on failure.
- Aftermath heal caps (`resolve_payload` PAY_HEAL) clamp to the new max —
  no code change, but heal-related test constants that assume 12 will drift.

## 4. Track T — still open (plan §5)

Pure constants inside the authoritative sim, wire untouched:

- `choose_form` eligibility (`duel_incantation.c:125-127`): still the old
  ladder (4 forms only above 96, all 8 above 224). Target: 4 forms by
  complexity 48, all 8 by 160.
- Base weights `{5,2,2,2,2,2,1,1}`: raise SWARM/CHAIN/CONJURE (currently
  2/2/1) so the exotic tail appears at prose complexity.
- HP/regen retune lands here if it precedes B (either track may carry it —
  just once).

T changes duel outcomes wholesale, so land it **before or with** B's
`choose_form` changes to avoid re-baselining twice. Since T and B both
touch `choose_form`, doing T's ladder + weights in the same commit series
as B's temper term is the pragmatic order now that hardware feedback is
gated anyway; keep the plan's separate-track bookkeeping by noting the
constants in the T section when they land.

## 5. The C gate (plan §7)

After T and B land and the suite is green:

1. **First QMK build of M15.** Compile both halves, run
   `make release-budget` against the *new* artifacts. Budget ledger so far
   predicts G≈2.4 + A≈1.7 + B≈2.4 + P≈0.4 ≈ 6.9 KiB of the 8,192 B
   ceiling — but that is estimate, not measurement, and nothing has been
   measured since M14.
2. **Descriptor-compression spike**: prove the slave can reconstruct
   `variance` (presentation jitter — substitute session seed) and
   `interaction` (recomputable from element+form) for **every** descriptor
   `incantation_compile` can emit — enumerate the compile domain on the
   host and diff. That frees 8 bits for C's 2 field-object bytes.
3. **Go/no-go**: C proceeds only if measured total growth stays under
   8,192 B minus a 1 KiB margin. First overrun ejects C; second trims B's
   PACE/TAUNT art (§8).

## 6. Integration/cleanup checklist (do before or at the gate)

- [ ] Track T constants (§4 above) — the only unlanded pre-B track.
- [ ] QMK compile of `keymap.c` (three uncompiled edit sites: master tx
      secondary pack ~line 411, tx change-detection masks ~line 420-445,
      slave residue unpack + civic masking ~line 565-575; plus the slave
      local-fallback secondary pack ~line 580).
- [ ] Physical checklist additions (`docs/physical-checklist.md`): residue
      mark legibility at desk distance (marks are 1–4 px), the 16-step
      celestial arc, alert banner, and the §2.0 weight-pass items —
      pending M14 record clearance.
- [ ] Golden discipline: goldens re-baseline **only** with a visual review.
      The session-scratchpad dumper tooling is not in the repo; rebuild
      recipe: compile a small C file against `firmware/sim/*.c` with
      `-I firmware/sim`, pose `duel_render_t`/scenarios, dump 67x128 PGMs
      (left canvas, 3-px gap, right canvas), sheet them with PIL. Consider
      committing a `tools/` version of this — it has now been rebuilt from
      scratch three sessions running.
- [ ] Open questions for Griffin that block art, not mechanics (plan §10):
      Q4 HP 8 vs 10 feel (after retune, on hardware), Q5 cut/keep
      PACE/TAUNT before their art lands. Q1–Q3 are settled by landed work.
- [ ] Optional §2.1 "open design item", explicitly not committed scope:
      HP as lit shaft windows (8 slots pairs naturally with the retune) and
      lifecycle tableaus restaging on the balcony. Decide with Griffin at
      the gate; both are pure presentation.

## 7. Landmines and conventions (learned the hard way)

- **`duel_snapshot_set_civic` preserves residue bits** (civic 6–7,
  secondary 7) since Track A; the Track P "set_residue after set_civic"
  ordering rule is retired. Anything comparing raw `flags`/`civic`/
  `secondary` bytes against freshly packed semantics must mask
  `DUEL_*_RESIDUE_BITS` (see `duel_master_tx` for the pattern).
- **`sp->resolved` is a bit field now**: bit 0 = payload landed
  (beam/chain), low bits also count swarm pulses, bit 7 = residue reaction
  spent. Never write `sp->resolved = 1` again.
- **fx nibble wraps at 16**; consumers compare equality only. Anything new
  reading `fx_stance` must go through `VIEW_FX_*` macros.
- **Desk-mirror contract**: scry-added pixels must mirror exactly
  (`incantation_diegetic_scry_*` test); asymmetric base cells (mechanical
  window lintel row y19, peak art) are why the scry summary lives in the
  gap-side strip. New instrument-layer art must clear its field first
  (banner/stale-glyph/scry-summary/status-glyph pattern).
- **Combat cluster is NOT mirrored** — wizard at cx=16 on both halves;
  desk-authored architecture (`TWR_X`/`FLR_X`/`SCRY_X`) IS mirrored.
  Stance art on the balcony is architecture-side: author in desk space.
- **Residue self-limits vs same-element bombardment** (emergent, accepted):
  a doorstep at intensity 2 feeds the next same-element spell (−1 zone)
  before its impact deposit (+1), so same-element pressure oscillates
  around 2 and only mixed-element play reaches 3.
- **Sequence acceptance** is `(int8_t)(seq - last) > 0` — wrapping byte;
  don't "fix" it to unsigned.
- Accepted visual quirks (do not "fix" in passing): detonate base lines
  merge with the deck; charge motes graze the balcony slab; bodies drag
  through the shaft doorway; big-cast halo dots enter the shaft; conjure
  orbit center mote sits on the shaft edge; wards/local fx transiently
  overlap residue marks (combat on top of its own history).
