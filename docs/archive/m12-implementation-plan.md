# M12 Twin Cities — end-to-end plan (INTEGRATED 12.0 + 12.1 + 12.2)

Grounded in the locked design spec and a fresh read of the codebase. Every file:line anchor verified.

> Historical implementation plan. M12 shipped and was incorporated into the
> physically accepted M13 baseline. Planning language and open questions below
> are retained for provenance; use `post-m13-backlog.md` for current future
> scope.

---

## IMPLEMENTATION STATUS (implemented; superseded by accepted M13)

All waves are implemented behind `ARCANE_M12`, converged, and desktop-verified on branch
`m12-twin-cities`. The release build stays byte-identical to M11.5.

| Wave | What landed | State |
|---|---|---|
| Phase 1 geography | Rooftop raised 17px; M9 archive underlay retired; scene/city-aware floor shell | ✅ desktop |
| Interface | Civic wire contract, enums, `m12_*_state_t`, `duel_render_t` civic fields | ✅ |
| Track R | Civic-driven floors (Commons/Research/Workshop), distinct astral/mechanical cities, resident engine (5 personalities, bounded civic tick) | ✅ desktop |
| Track P | 31-byte v8+civic snapshot (civic/secondary/shared_pres/revision), CRC/recovery, Raw HID payload[6/7] | ✅ desktop |
| Track H | Host floor/mode/secondary classification; civic byte packing; 59 pytest | ✅ |
| Phase 5 convergence | `keymap.c` relays civic; seed + bounded civic_phase clock | ✅ firmware-compiled |
| Wave 6 ecology | Courier forms (messenger/parcel/beacon/sentinel), category routing, count/age lifecycle | ✅ desktop |
| Wave 7 rare events | Deterministic weighted deck (6 families, 75/25, cooldowns, safety gates), local + shared rendering | ✅ desktop |
| Wave 6/7 convergence | `keymap.c` derives + relays visitor (`shared_pres`) and rare-event (`revision`) | ✅ firmware-compiled |

**Verification:** desktop matrix green — firmware sim (both `ARCANE_M12` on/off variants, world-hash
streams **identical** = mechanics untouched), 69 canonical M12 gallery scenarios + all structural
invariants, no-alloc gate, ASan/UBSan; host 59 pytest. Firmware compile-verified via a throwaway
keymap (host config, `ARCANE_M12`): links clean.

**Resource deltas (crkbd/rev1, rp2040, host branch):**
- Release flash: **49,944 bytes** — byte-identical to M11.5 baseline.
- Full M12 flash: **53,776 bytes** (+3,832 for the entire milestone). Ample headroom on RP2040.
- Split snapshot: 27 B release / **31 B** under M12 (1 reserve byte). Raw HID report unchanged at 32 B.

**Wire budget used:** civic (floor/mode/intensity), secondary (activity), shared_pres (visitor
kind/city/lifecycle + 2 reserved bits used for count density), revision (event id/phase/target).

**Physical disposition:** M12's current surfaces were exercised as part of the
accepted M13 two-half run, including actual-size floor legibility, stress,
power/reconnect behavior, and the preserved M12 rollback boot. This document is
now a historical implementation plan; `m13-acceptance.md` is authoritative.

---

**Scope decision (this session): implement M12.0, M12.1, and M12.2 together as one integrated M12.**
The spec staged them (§15.2); we are merging them. This is viable because the M12.0 data model was
designed with reserved seams (floor enum, civic bits, secondary codes, visitor kinds, event-id space) —
so 12.1/12.2 are mostly additional content, not new architecture. Two budgets tighten (wire + flash);
see §D5 and §10.

**Other confirmed decisions:**
- **Rooftop RELOCATION** — champion moves up; larger floor canvas (formal spec revision; §1, Phase 1a).
- Snapshot may reach **31 bytes** (keep 1 reserve) — but full scope pushes wire packing; see §D5.
- Presentation seed = the existing **1-byte** split session.
- Physical M11.5 items reportedly run — acceptance doc reconciled once measured values are supplied (§0).

### Which spec exclusions this lifts vs. keeps
Lifting the *staging* exclusions from §2.3: Workshop/Forge floor, Git/terminal visuals, transfer/system
activity, and the richer notification ecology are now **in scope**. Still firmly excluded (invariants,
unchanged): no new combat mechanics / floor bonuses; no permanent progression, save files, or long-lived
host DB; no streamed frames or host-supplied text/bitmaps; no unrestricted entity pool / pathfinding /
needs simulation. One-floor-at-a-time and the per-screen density ceiling (§8.1) still hold — a third floor
archetype enlarges the *library*, never the on-screen density.

---

## 0. Gate status

Historical gate: M12 originally depended on M11.5 physical acceptance. M12 was
subsequently implemented and then incorporated into the physically accepted
M13 build. The current v10 snapshot is 32 bytes; the old v8 five-byte split
reserve described below no longer exists.

---

## 1. Revised screen geography (y=0 top-left) — ROOFTOP RAISED
Combat cluster translates **up ~20px** as one unit; floor inherits the freed space. **One floor shows
at a time** (Commons/Post, Archive/Research, OR Workshop/Forge) — geometry is identical for all three.

| Y | Region | Change |
|---|---|---|
| 0–15 | Alert/status | fixed — **hard ceiling on the lift** |
| ~16–30 | Sky / charge windup / skyline | ex-archive region repurposed |
| ~30–57 | Rooftop champion cluster | **MOVED UP ~20px from y54–77** |
| ~58–110 | **Active tower floor (~52px)** | NEW; holds whichever of the 3 archetypes is selected |
| 111–114 | Health | fixed |
| 115–127 | Foundation/trim | NEW sparse detail |

**Lift constraint:** charge windup (today `cy=39`, above the head) must stay ≥ y16 to clear the alert
region → ~20px lift is the recommended max. **Containment:** champion Y lives entirely in `duel_draw.c`;
the wire view / `sim_world_t` / protocol / host carry only *logical* state, so the relocation is
renderer-only — world goldens, protocol, host all untouched. Re-base combat Y constants via one
`DUEL_ROOF_BASE_Y`: `draw_charge` (`:354`), recovery sparks (`:614`), `wiz_body` (`:161`), `wiz_downed`
(`:223`), `medic_draw` (`:241`), `draw_ward` (`:394`), `SPELL_Y_BASE=63`/`spell_lane_y` (`:264`),
`spell_glyph` (`:273`). Battlefield→x (horizontal) unaffected.

---

## 2. Architectural decisions

**D0. Rooftop relocation is its own isolated, re-accepted commit** (Phase 1a) — no floor art in it.

**D1. Sync meaning, derive locally (§13.2).** Wire carries shared facts only: floor, mode, host-intensity,
secondary, notification summary, shared rare-event id+phase, visitor kind+city+lifecycle, revision, 1-byte
seed. Everything else (resident identity/action/station, city art, local typing level, reaction animation)
derives locally from `(seed, is_left, floor, personality, revision, action_idx, frame)`. No coordinates/
sprites cross the link.

**D2. Presentation-only.** No M12 state in `sim_world_t` (56B, hash-asserted). Lives in `duel_render_t`
(≤32B) + `oled_task_user` statics. View is read-only; never write back.

**D3. Floor animation vs skip-redraw/power.** Resident/floor advance on a bounded "civic tick" (~250–500ms)
stored as a coarse phase byte in `duel_render_t` (memcmp gate → redraws only on advance). Fine sub-motion
keys off `frame` only in the ACTIVE window. Idle settles; no strobing (§18.2, §19).

**D4. Wake chokepoint sacred.** No M12 path calls `duel_display_note_key()` / forces ACTIVE
(`keymap.c:86-91,124`) or alters the transmitted display phase (would wake the peer via
`duel_display_follow`, `keymap.c:583-590`).

**D5. Byte budget — FULL SCOPE (the tightening).**
Split: insert 4 bytes between `alert`(25) and `crc` → `sizeof 27→31`, RPC buffer 32 → 1 reserve byte.
Fields to pack across those 4 bytes at full scope:
```
floor[2] mode[2] host_intensity[2] secondary_activity[3]        = 9 bits
rare_event_id[4] rare_event_phase[2]                            = 6 bits   (deck now ~10-14 families)
visitor_kind[4] visitor_city[1] visitor_lifecycle[3]           = 8 bits   (many courier forms, 12.2)
revision[~4]                                                    = 4 bits
                                                        total  ≈ 27 bits  -> fits 4 bytes (32 bits)
```
So full 12.1+12.2 **still fits 4 bytes**, but needs dense bit-packing (fields cross byte boundaries;
`revision` shrinks to ~4 bits — or is dropped entirely, since the slave can detect shared-state change by
memcmp of the civic bytes as it already does for external/alert, freeing more room). **Levers if it gets
tight:** drop `revision`; or spend the 5th free byte (snapshot=32B, 0 reserve). **Recommend:** pack into
4 bytes, keep 1 reserve. CRC auto-extends (`offsetof(crc)`). Update `_Static_assert`→31 (`duel_proto.h:44`),
the 4 `duel_encode_*` (`duel_proto.c:18-47`), slave apply (`keymap.c:594`), TX compare (`keymap.c:417-421`).

Raw HID: `payload[6]=civic`, `payload[7]=secondary`, len 6→8 (14 bytes free — trivial). `shared_pres`/
`revision`/visitor are master-computed presentation, not host bytes. Floor enum uses COMMONS/RESEARCH/
**WORKSHOP** (SPECIAL still reserved) — fits the 2-bit field. Secondary uses TRANSFER/SYSTEM (were
reserved) — fits 3 bits.

---

## 3. Data structures (presentation-only; §16.1)
```c
typedef struct { uint8_t identity_personality; uint8_t action_phase; uint8_t progress; } m12_resident_state_t; // 3B, 1/city local
typedef struct { uint8_t kind_phase; uint8_t progress_flags; } m12_prop_state_t;                                // 2B, 1/city local
typedef struct { uint8_t kind_target; uint8_t lifecycle_phase; uint8_t progress_flags; } m12_visitor_state_t;   // 3B, 1 global (12.2: many kinds)
typedef struct { uint8_t id_target; uint8_t phase; uint8_t progress; } m12_event_state_t;                       // 3B, 1 shared (expanded deck)
```
Entity budget unchanged (§8.2): 2 residents, 2 props, 1 global visitor, 1 shared event. 12.1/12.2 add
*kinds/motifs/deck entries*, not slots. `kind_target` / `id_target` fields are already byte-wide → room
for the expanded courier forms and event families.

---

## 4. Feature flag
`ARCANE_M12` (mirror `ARCANE_FIXED_SPLIT_CADENCE` in `firmware/rules.mk` + `host/firmware/rules.mk`),
guard all M12 code. M11.5 release stays bit-identical when off.

---

## 5. Phase-by-phase (each = a reviewable commit; record flash/.data/.bss/snapshot/timing every phase)

**0 — Baseline.** Tag M11.5; capture sizes/stack/timing/goldens/rollback. Gate: M11.5 checklist reconciled.

**1a — Rooftop relocation (isolated).** `DUEL_ROOF_BASE_Y`, lift ~20px, regenerate combat visual goldens.
Gate: world goldens unchanged; cluster legible at actual size; charge ≥y16. Physically re-accept before 1b.

**1b — Floor geometry.** `DUEL_FLOOR_Y0 ~58 / Y1 110`, foundation, `DUEL_LAYER_FLOOR`. `draw_floor_frame`
in `wiz_draw_scene` (~`duel_draw.c:665`): static room shell + left/right architecture via `wiz_hspan`/
`wiz_line`/`archive_rect`, mirrored by `is_left`. Gate: outside-band hashes unchanged; legible.

**2 — Floor definitions (THREE archetypes).** Immutable anchor motif tables for Commons/Post,
Archive/Research, **and Workshop/Forge** (12.1), both cities (left magical forge / right mechanical
assembly). City transitions (left veil / right gate skeleton, §6.2/§17.1). Floor selected by scene/civic
byte (COMMONS default, RESEARCH on browser, WORKSHOP on terminal/Git). Gate: all three floors × both
cities legible at size; transitions hash-stable.

**3 — Resident engine.** One resident/city, session personality (5 types), 7-action vocab, fixed stations
per floor (incl. Workshop stations), personality-weighted deterministic selector, cosmetic loyalty. Routine
SM on the civic tick (D3), local derivation (D1). Gate: no obvious loop; bounded; zero world-hash change;
resident distinguishable from anchors/couriers/medic.

**4 — Activity coupling.** 4 local typing levels per half (`keymap.c:113-137,370-380`); shared two-hand
pulses; combat-reaction grammar via the `fx_seq` hook (`keymap.c:688-702`), VOID-pierce via element
(`duel_draw.c:596`), reaction cooldown; sparse rooftop support (§10.2). Gate: zero mechanical change.

**5 — Host semantics (FULL 12.1).** Split civic/secondary bytes (D5); host payload[6]/[7], len 6→8.
Daemon: add `floor`+`mode` to `ApplicationProfile` (`profiles.py:11`); WORKSHOP from the existing Git/
repository adapter (`adapters.py:159` / `EventService.report_repository_state` `daemon.py:327`) and
terminal-completion events; secondary TRANSFER/SYSTEM from download/network activity; fold into
`SemanticResolver.update` (`semantic.py:10-58`) beside `scene`; mode NORMAL/QUIET from DND/Pomodoro. Privacy:
new bytes enum-only, boundary holds. Gate: privacy + reconnect/expiry pass; host-gone → rooftop complete.

**6 — Notification ecology (FULL 12.2).** Beyond one generic courier: category-specific courier forms
(messenger bird, parcel/cart, beacon/conduit, sentinel), category routing to the right city (§11.3),
lifecycle NONE→ARRIVING→WAITING→AGING→RESOLVING (§17.3) with the **full aging grammar** (new age → courier
arriving; waiting → pacing/filing; old → seated/annoyed / dust / entrenched alarm; clear → filed/dismissed/
departs), **persistent sentinels** for critical-persistent, and count buckets 1/2-4/5+ as density (no extra
actors). Gate: real Freedesktop replace/close drives the full arrival→aging→resolution physically; slot
conflict downgrades to glyph; **never wakes a sleeping panel** (D4).

**7 — Rare events (EXPANDED deck).** 12.0 deck (4 local + 2 shared) **plus** 12.1-flavored families
(workshop mishap: jammed gear/escaped tool; transfer/parcel event) — ~10-14 families total, still 1 shared
slot. Eligibility gates (§14.1), family cooldowns, 75/25 local/shared, frequency targets (§14.3). Shared
events use `shared_pres` for both-half agreement. Confirm the 4-bit event-id space (D5) covers the deck.
Gate: long-session arbitration + repetition review; safety-gate suppression provable.

**8 — Release acceptance (full).** Gallery covering all three floors, all courier forms + full aging
grammar, expanded deck, all combat reactions, power states (§18.1); stress, power, suspend, rollback,
resource report. Gate: §19 gates met with the revised flash target (§10). Sign-off + release hashes.

---

## 6. Test / gallery (every phase)
Insertion point `firmware/sim_test/scenarios.c` (`scenarios[]` `:10-48` + `duel_scenario_build()` `:82-188`)
→ auto-flows into visual goldens, invariants, PBM/HTML gallery. New groups: `cities, floors (×3),
transitions, residents, couriers (×forms), rare_events (×families)`. Update `test_catalog` required-names +
threshold (`visual_test.c:73-83`). New invariants: cluster in raised band; floor within y58–110; foundation
y115–127; protected regions unchanged. World goldens untouched every phase; no-alloc + ASan/UBSan green.
Note: the full-scope gallery is materially larger — budget review time for the actual-size pass.

---

## 7. Reserved for the future (post-M12)
With 12.1/12.2 folded in, the only reserved seams M12 must **still** leave open are the `SPECIAL` floor-enum
slot, the leftover civic/secondary reserved bits, the free host payload bytes [8..19], and the 1 reserve
snapshot byte — kept for a later SPECIAL/M13 world so this milestone needs no architectural reset.

---

## 8. Risk register (top items)
1. **Wire packing at full scope (D5)** — 4 bytes fit only with dense bit-packing; keep `revision` minimal or
   drop it. Highest-attention data decision.
2. **Rooftop relocation re-acceptance (1a)** — reopens combat visuals; isolate + physically re-accept alone.
3. **Flash growth (§10)** — 3 floors × 2 cities of motif tables + courier forms + bigger deck is the main
   size driver; measure per phase, keep a reserve.
4. **Gallery/acceptance size** — full-scope canonical coverage is large; the actual-size physical pass is
   the real completeness gate (§18.2), not logical hashes alone.
5. **Power discipline under more actors (D4)** — more couriers/events must still never wake panels.

---

## 9. Open items
1. M11.5 evidence values for the acceptance-doc reconciliation (§0).
2. Exact rooftop lift offset (~20px; finalized in 1a at actual size).
3. `revision` byte: keep (~4 bits) or drop for wire headroom (D5)?
4. Revised flash reserve target now that 12.1/12.2 are in-scope (was ≥12KiB "for future"; pick the new
   floor — I suggest keeping ≥8KiB for SPECIAL/M13).

## 10. Resource note
The §19 rationale of "≥12KiB flash reserve for future Workshop/notification" is partly *spent* implementing
them now. Timing/RAM/protocol gates are content-insensitive and unchanged (housekeeping <2ms, comp+OLED
<5ms, ≤+1KiB static RAM, ≤31B snapshot, no Raw HID size change, keys-only wake). **Flash is the scaling
risk** and its target should be re-derived after Phase 2/6 land the bulk of the content; recommend holding
a smaller but real reserve (≥8KiB) for SPECIAL/M13.

## 11. Suggested first commit
**Phase 1a only**, behind `ARCANE_M12`: `DUEL_ROOF_BASE_Y`, lift the cluster ~20px, regenerate combat
goldens, add the raised-cluster invariant. No floor, protocol, or residents. Report changes, resource
deltas, tests, rollback. Stop for physical re-acceptance of the raised rooftop before Phase 1b.

---

## 12. Parallelization strategy

Goal: cover as much ground concurrently as possible without big-bang integration risk. The approach is
"serialize the few genuinely-coupled steps, engineer around the rest, then fan out into isolated tracks."

### 12.1 The three hard serialization constraints
1. **Phase 1a must land + be re-accepted alone.** It rewrites the combat Y-constants every rendering phase
   builds on and regenerates the *combat* visual goldens; concurrent pixel work would collide on both.
2. **`duel_draw.c` is a monolith.** `wiz_draw_scene` (`duel_draw.c:585-786`) is where floors/residents/
   couriers all want to add draw calls — concurrent edits conflict.
3. **`golden/visual.hashes` is one shared file.** Two pixel-affecting phases both regenerate it → merge
   conflict and no way to attribute which change moved which hash.

Everything else is only *logically* coupled through the civic-byte wire contract, which is an interface,
not a file. Proto/host firmware (`duel_proto.c`, `duel_host.c`) and the Python daemon (`host/arcane_host/`)
touch entirely different files from the renderer.

### 12.2 The two enabling moves (do these before fan-out)
- **A. Interface-first commit** (small, serial, immediately after 1a): land `ARCANE_M12`; the civic/
  secondary/shared-presentation wire byte layout + pack/unpack macros (D5); the floor/mode/personality/
  action/courier/event **enums**; the `m12_*_state_t` structs; and empty stub `draw_floor`/`draw_resident`/
  `draw_courier` hooks wired into `wiz_draw_scene` **once**. After this, `wiz_draw_scene` needs no further
  structural edits — tracks fill stubs.
- **B. New translation units, not a fatter monolith.** Author M12 rendering in `duel_floor.c`,
  `duel_resident.c`, `duel_courier.c`, `duel_civic.c` — each owns its draw helper. Different files → no edit
  contention; keeps the per-file no-alloc symbol gate (`Makefile:49-55`) clean.
- **Golden namespacing:** each track owns its own scenario group **and** its own golden file
  (`golden/visual_floors.hashes`, `..._residents.hashes`, `..._couriers.hashes`) so regen never collides.

### 12.3 Wave / track structure
```
Wave 0 (serial):   Phase 0 baseline ─► Phase 1a rooftop relocation (re-accept alone)
Wave 1 (serial):   Interface commit (flag + wire contract + enums + structs + stub hooks)   ← the barrier
        ┌───────────────────┬────────────────────────────┬───────────────────────────┐
Wave 2  │ TRACK R (render)   │ TRACK P (proto/host fw)     │ TRACK H (Python daemon)    │
(parallel) 1b floor geometry │ D5 wire bytes: duel_proto   │ floor/mode on Application  │
        │ 2 floor defs ×3     │  + duel_host payload[6/7]   │  Profile; Git→WORKSHOP;    │
        │ 3 resident engine   │  own files, own tests       │  transfer/system secondary │
        │ (duel_floor.c,      │                             │  own tree, own pytest      │
        │  duel_resident.c)   │                             │                            │
        └───────────────────┴────────────────────────────┴───────────────────────────┘
Wave 3 (serial):   4 activity coupling ─► 5 host-semantics integration (contract converges here)
Wave 4 (parallel): 6 notification ecology (needs R+P+H) ║ 7 rare-event deck
Wave 5 (serial):   8 arbitration (§13.1) + full acceptance gallery + resource report
```
Within Track R, 1b→2→3 serialize on `duel_floor.c`/goldens, but **Phase 2's three floor archetypes are
mutually independent** — Commons/Research/Workshop motif tables can be authored concurrently into three
functions and fast-merged.

### 12.4 Strictly serial (don't fight these)
1a relocation · the interface commit · Phase 5 wire integration (the end-to-end contract agreement point) ·
Phase 8 (whole-system arbitration + actual-size acceptance).

### 12.5 Agent/worktree execution model
Tracks R/P/H run as concurrent sub-agents in **isolated git worktrees** off the post-interface commit, each
owning disjoint files + golden files. Merge order: P and H (no pixel goldens) merge first and cheaply; R
merges via its namespaced golden files. Rebase each track on the interface commit only — never on each other.

### 12.6 The safety net that makes this safe
**World goldens stay byte-identical through every M12 commit** (mechanical isolation, §18.3). That single
tripwire fails `test_main.c` in isolation the instant any track perturbs mechanics — before merge. With
per-track visual-golden namespacing and the per-file no-alloc gate, every track is independently verifiable,
turning parallel merge into a low-risk operation rather than a big-bang integration.
