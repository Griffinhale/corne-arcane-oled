# griffin_anim — Corne Arcane OLED (firmware-only)

Firmware-only OLED experiment keymap for the Corne v3 / RP2040 dual-OLED
"spell duel" system. Vial stays enabled here. `griffin` remains the stable
recovery baseline — do not experiment there.

## Status: M7 done (host-verified); hardware-verified through M6 (2026-07-13)

M0–M6 are all flashed and confirmed on the physical keyboard. The v3 flash is
a superset, so a single reflash of both halves cleared the M3/M4/M5/M6
checklists at once: cross-screen bolts, the KO arc (collapse → downed → medic
drag-off → replacement), and recipe-driven spell variety all render correctly
with typing unaffected. M6.5 and M7 are host-verified; a hardware eyeball of
both is pending the next reflash (wire is now v4 — flash both halves).

**M6.5 — juice + first outcome-changing element (done, host-verified;
hardware eyeball pending).** Bench feedback on M6 was that spell variety read
weak and hits/wind-ups were understated. Addressed:
- Bolder, silhouette-distinct element glyphs — FORCE a solid cannonball,
  FROST a spiky star, VOID a hollow ring, EMBER a comet with a long tail —
  with heftier SWIFT (speed streak) and HEAVY (diagonal casing) tells.
- Punchier impact: a thick double border that thins as it fades plus an
  expanding shock cross, and impacts now linger longer than deflects/fizzles.
- A brighter charging starburst on the cast wind-up so a cast reads at a glance.
- **VOID now pierces wards** — a VOID bolt cannot be deflected — so the
  element you cast changes the OUTCOME, not just the look. This is the one
  bounded mechanical change; deeper composition (more payloads/emitters/
  triggers) stays deferred per the design audit. All draw changes are pure
  presentation (world-hash goldens untouched); the pierce rule is
  authoritative and host-verified (`t6_void_pierces_ward`).

**M1 — Actor renderer + physical-side proof** (done, hardware-verified).
Each 128x32 OLED renders one wizard actor; each physical half owns its own
wizard, driven only by that half's local key activity, zero split traffic.

**M2 — Deterministic world loop** (done, hardware-verified). Fixed integer
25 Hz simulation tick, simulation state fully separated from presentation,
bounded event queue consumed outside the key path, render from a stable
snapshot. Determinism is machine-verified by the host test rig in
`sim_test/` (pulled forward from the roadmap's M11 so every later milestone
iterates without flashing).

**M3 — Split snapshot proof.** The master's world is authoritative and
streams CRC'd snapshots (`sim/duel_proto.{h,c}`, grown to 26 bytes by M6) to
the slave over a user split RPC every 2nd tick (12.5 Hz). Sequence + session
acceptance means
a stale or duplicated packet can never roll the slave's view backward; a
rebooted master is adopted immediately. If snapshots stop for 500 ms the
slave shows a broken-link glyph (top corner nearest the gap) and falls back
to its own local pose-only sim, so its wizard keeps reacting to typing. The
receive callback runs in the serial driver's HIGHPRIO thread and is a
seqlock-guarded memcpy and nothing more; housekeeping takes the consistent
copy. Loss/duplication/reordering/corruption behavior is host-verified
(`t3_*` tests).

## Layout

- `sim/` — **hardware-agnostic engine core**, compiled into both the firmware
  (via `SRC +=` in `rules.mk`) and the host test rig. No QMK includes, no
  statics, integer math only, no allocation, no time reads.
  - `duel_sim.{h,c}` — world state (`sim_world_t`), fixed tick (`sim_tick`),
    pose machine (IDLE → CAST → RECOVER; a held key sustains CAST), bounded
    event queue (`sim_evq_t`, drop-newest with explicit drop counting),
    combat resolution and the M5 lifecycle machine (ACTIVE → COLLAPSE →
    DOWNED → MEDIC → REPLACE on fixed timers, roster variants, slow regen).
    M7 adds the layer-key chord machine (`sim_scry_t`, `scry_step`; IDLE →
    FIRST_HELD → PENDING → ACTIVE → SELECT / CANCELLED), a pure level-logic
    state machine on the sampled `scry_mask`, authoritative-only like combat.
  - `duel_proto.{h,c}` — split snapshot wire format (v4, 27 bytes, CRC-8)
    and the slave-side sequence/session acceptance rules. The M7 `scry` byte
    (overlay open + scene) rides the wire so both screens show the overlay.
  - `duel_draw.{h,c}` — all drawing, onto a 512-byte 1bpp `duel_fb_t`
    (portrait 32x128). `wiz_draw` is the M1 silhouette; `wiz_draw_scene`
    renders a full half from a world snapshot (plus the `DUEL_DEBUG_HUD`
    overlay: bottom-row tick odometer sweeping once/second, top-corner
    dropped-event dots). `draw_overlay` is the M7 scrying panel (eye title,
    layer readout, host-link glyph, notification dots, scene selector), drawn
    on top of the running scene whenever `scry_is_open`.
- `keymap.c` — QMK glue only:
  - `oled_init_user()` — portrait rotation (`OLED_ROTATION_270` both halves).
  - `matrix_scan_user` / `matrix_slave_scan_user` — XOR matrix rows against
    the previous pass, push compact edge events (kind/side/row/col) to the
    queue. The master's matrix already contains the slave's rows (merged in
    `matrix_post_scan` before the hook fires), so the master captures both
    sides; the slave captures its own. **No render/alloc/split work in the
    key path.**
  - `duel_sample_scry()` — M7 physical-position sampling: reads the two layer
    thumbs by raw matrix position (MO(1) at (3,4), MO(2) at (7,4)) plus an
    "any other key held" bit, never emitted keycodes or the active layer, so
    the chord is immune to whatever Vial maps there. Only the master's merged
    matrix holds both, which is why the chord machine is authoritative-only.
  - `housekeeping_task_user()` — the 25 Hz tick scheduler (`timer_expired32`
    accumulator, wrap-safe, catch-up capped at 5 then resync for USB
    suspend). Drains the queue, samples key levels, runs `sim_tick`, then
    copies the world to the render snapshot.
  - `oled_task_user()` — draws ONLY from the snapshot into a `duel_fb_t`,
    blits to the OLED. Render cadence cannot reach the sim.
- `sim_test/` — host-only test rig (invisible to `qmk compile`):
  `./run_tests.sh` builds and runs everything (<1 s) — replay goldens,
  cadence-invariance, snapshot purity, queue overflow, uint32 tick-wrap, and
  a `nm`-based no-allocation gate, all under ASan/UBSan. `make preview`
  builds a terminal previewer for both canvases (`./preview --cast L`,
  `./preview --scry 0`, `./preview traces/cast_basic.trace --play`). Regenerate goldens only via
  `make golden` and review the diff.

## Design invariants

- Keyboard output never waits on display logic; key-path hooks only record
  compact events.
- One authoritative fixed-tick simulation; master will own shared state (M3).
  `SIMF_AUTHORITATIVE` is set on the master only, so the slave structurally
  cannot resolve combat (lands in M4).
- Deterministic: identical init + identical per-tick (inputs, events) streams
  produce bit-identical worlds. Cosmetic effects key off the render frame
  counter, never the sim tick.

## Build & flash (NixOS, user-scope, no sudo)

```bash
cd ~/src/vial-qmk && nix-shell            # qmk + python3 + arm-none-eabi-gcc
qmk compile -kb crkbd/rev1 -km griffin_anim -e CONVERT_TO=rp2040_ce
# Flash one half at a time (never hot-plug TRRS):
qmk flash -kb crkbd/rev1 -km griffin_anim -e CONVERT_TO=rp2040_ce -bl uf2-split-left
qmk flash -kb crkbd/rev1 -km griffin_anim -e CONVERT_TO=rp2040_ce -bl uf2-split-right
```

Artifact: `crkbd_rev1_griffin_anim_rp2040_ce.uf2` (118 K). Reassemble
USB-left + TRRS. Host tests: `sim_test/run_tests.sh`.

## M2 hardware checklist

1. Wizards look as in M1; bottom-row odometer pixel sweeps once per second.
2. Tap → CAST ~0.36 s → 3-tick RECOVER (sparks above the hat) → idle; a held
   key sustains CAST; halves stay independent.
3. Cadence: temporarily `#define OLED_UPDATE_INTERVAL 150` in `config.h` —
   odometer still sweeps at 1/s (visibly stuttery), cast duration unchanged.
   Remove after.
4. Roll fingers across 10+ keys fast: no lockup, every character reaches the
   editor. (Overflow dots should NOT appear at `SIM_EVQ_CAP` 16 unless forced
   by temporarily lowering the cap.)
5. Typing latency feels unchanged.

## Rollback

```bash
# Reflash the stable keymap:
qmk flash -kb crkbd/rev1 -km griffin -e CONVERT_TO=rp2040_ce -bl uf2-split-left   # and -right
# Or restore this dir from a backup tarball:
tar -xzf ~/corne-griffin_anim-<stamp>.tar.gz -C ~/src/vial-qmk
```

## M3 hardware checklist

1. Type on the slave (right) half — both odometers/tick displays track each
   other within ~80 ms; the master screen's wizard state matches the slave's.
2. Pull the TRRS mid-animation (power down first per the usual discipline if
   preferred; the protocol tolerates a live pull) — the slave shows the
   broken-link glyph within 0.5 s and keeps animating from local typing; the
   master keeps typing normally. Replug — the glyph clears within 0.5 s and
   both screens re-converge with no backward jump.
3. Reset only the master — the slave adopts the new session within 0.5 s.

**M4 — First cross-screen spell.** One spell slot per wizard on the 0..255
battlefield axis (0 = left wizard, 255 = right). A rising key edge (slot
free, ~1 s cooldown) winds up 6 ticks, then the bolt flies 4 units/tick —
2.4 s per crossing, with u 96..159 deliberately invisible in the physical
desk gap. Any keydown raises that side's ward for 10 ticks (rendered as an
arc in front of the wizard): a spell reaching the defender's doorstep
(u 240 / 15) with the ward up deflects (arc flare); otherwise it impacts
(u 248 / 7) — border strobe on BOTH screens, one hp pip lost. A felled
wizard now walks the M5 lifecycle arc — see M5. All spawn/motion/resolution is
gated on `SIMF_AUTHORITATIVE` (master only); the slave shows outcomes purely
from snapshots, and a lost impact packet costs only the flash — the next
snapshot carries absolute state. Flight, screen ownership, the exact 11-tick
shield window, and missed-impact recovery are host-verified by
`t4_flight_golden`, `t4_screen_ownership`, `t4_shield_window`, and
`t4_missed_impact_recovery` (`./preview traces/cast_impact.trace --play`
shows the duel).

## M4 hardware checklist

1. Right half idle, tap a left key: left wizard casts, the bolt leaves the
   staff, exits the left screen's gap edge, ~0.6 s of empty desk, enters the
   right screen's gap edge, impact: border strobe on both screens, right hp
   pips 3 -> 2.
2. Repeat while typing on the right: the bolt deflects at the ward arc
   (arc flare, no border strobe), hp unchanged.
3. Type only on the SLAVE half: the bolt spawns there and flies toward the
   master's screen (proves master reads the synced slave rows end to end).
4. Pull TRRS just before an impact, replug: no flash on the slave, but hp /
   empty slot are correct within 0.5 s.
5. Type prose in an editor during a duel: zero dropped or delayed keys.

**M5 — Lifecycle and roster.** Health is now five pips (`SIM_MAX_HP` 5),
and a felled wizard no longer resets the duel (that M4 placeholder is gone):
the fifth impact starts a fixed-timer KO arc — collapse (12 ticks) → downed
under a blinking protected halo (25) → a medic drags the body off (25) → the
replacement walks in (20) — 82 ticks ≈ 3.3 s total, no input needed at any
point, so there are no dead ends. Each replacement wears the next of
`SIM_ROSTER_N` (4) cosmetic masks (base look / hat band / hem fringe /
pompom), cycling per walk-in, so the new combatant is visibly new in every
pose. A bolt reaching a downed wizard's doorstep fizzles harmlessly (small
puff, no pip loss, the arc is untouched); the opponent stays fully active
throughout — casting, warding, typing. A lost pip regenerates after ~15 s
(`SIM_REGEN_TICKS` 375; any hit resets the clock). All lifecycle and regen
transitions are gated on `SIMF_AUTHORITATIVE`, so the slave renders purely
from snapshots and structurally cannot advance the arc. M5 advanced the wire
format to v2, 24 bytes (life state, phase ticks and roster variant per
wizard); M6 later extended it to v3 — as always,
**flash both halves: mixed versions degrade to the stale-link glyph.** Note
the stale-link fallback (local pose-only sim) shows an upright wizard even
mid-downtime; relinking restores the true tableau instantly, since snapshots
carry absolute state. Host-verified by the `t5_*` suite
(`traces/duel_ko.trace`; `./preview traces/duel_ko.trace --play` shows the
full KO arc).

## M5 hardware checklist

1. KO the right wizard (5 impacts, watch its pips 5 → 0): collapse → halo
   over the body → medic drag → a different-looking wizard walks in, ≈3.3 s
   total, while the left wizard keeps casting normally.
2. Cast at the downed wizard: the bolt fizzles at the doorstep (small puff,
   no border strobe, no pip change).
3. After the replacement arrives, the new wizard (different hat/robe detail)
   casts fine.
4. Wait ~15 s after a single hit: the pip regenerates on its own.
5. Pull the TRRS across an entire replacement, replug: the slave lands
   directly on the new variant at 5 pips within 0.5 s.
6. Type prose during a KO arc: zero dropped or delayed keys.

**M6 — Noita-inspired spell builder.** A cast is no longer a fixed bolt: the
recent burst of physical keydowns is compiled into the spell's `kind`
(element + modifier), which changes its glyph and flight speed. Each keydown
contributes its **row class** (top / home / bottom / thumb) as an ingredient;
the compiler reads the last four. The **element** is the dominant row class
(top → FROST, home → FORCE, bottom → EMBER, thumb → VOID; ties break to the
most recent), drawing four distinct bolt glyphs (plus / diagonal cross /
hollow ring / comet tail). The **modifier** is the row-class *pattern* — all
identical → HEAVY (a slow, fat dir-3 bolt), strictly alternating → SWIFT (a
fast dir-6 bolt with a motion streak), anything else or a single key → NONE
(the baseline dir-4 plus). Because the modifier is a pattern, not a timing
measure, it is deterministic regardless of tick cadence. Faster spells are
harder to ward: SWIFT reaches the doorstep in a single tick (a ~10-tick ward
window) versus HEAVY's two (~11 ticks). A recipe accumulates on the
authoritative side only, is consumed and cleared at spawn, and is discarded
after `RECIPE_EXPIRE_TICKS` (25) of inactivity, so a burst after a pause
starts clean; the accumulator is bounded (`RECIPE_N_MAX` 15) and never
allocates. All recipe feed/expiry/compile is gated on `SIMF_AUTHORITATIVE`,
so the slave structurally never compiles a spell — it renders the master's
`kind`, which now rides the snapshot. The wire format is therefore **v3 at 26
bytes** (adds `spell_kind` per slot); mixed versions degrade to the
stale-link glyph, so **flash both halves.** Absolute state means a dropped
packet costs only a frame of glyph — the next snapshot restores the exact
kind. Host-verified by the `t6_*` suite (`traces/duel_recipes.trace`;
`./preview --spell-kind frost/swift` and friends render each glyph statically).

## M6 hardware checklist

1. Type a rolling burst (alternating rows, e.g. `q a q`) then cast: a SWIFT
   bolt with a trailing streak crosses noticeably faster than a plain tap-cast.
2. Hammer one row (e.g. `f f f`) then cast: a HEAVY bolt crawls across.
3. Cast with keys from different rows and watch the glyph change shape
   (plus / X / ring / comet) run to run with the dominant row.
4. Pause ~1 s between typing and casting: the spell reverts to a plain bolt
   (the stale recipe was discarded).
5. Confirm the slave shows the **same** element glyph the master casts, and
   that a TRRS pull mid-flight recovers the correct glyph on replug.
6. Type fast prose while spells fly: zero dropped or delayed keys.

**M7 — Layer-key scrying overlay (done, host-verified; hardware eyeball
pending).** The last firmware-only milestone: a deliberate dense-information
gesture that opens a temporary overlay above the still-running duel. An
explicit chord state machine — IDLE → FIRST_HELD → PENDING → ACTIVE →
SELECT / CANCELLED (`sim_scry_t`, `scry_step`) — is driven purely by a
level-sampled `scry_mask`, so a dropped key edge can never wedge it, exactly
like the shield and pose machines. The two momentary layer thumbs are read by
**physical matrix position** (MO(1) at (3,4), MO(2) at (7,4)), never by
emitted keycode or active layer, so the gesture is independent of the Vial
map. Co-holding both thumbs is precisely QMK layer 3, so the machine tells a
deliberate scry apart from ordinary layer-3 use structurally: both thumbs held
*still* for a ~0.4 s dwell (`SCRY_PENDING_TICKS` 10) opens the overlay, but
**any other key touched during the co-hold latches CANCELLED until a full
release** (that is layer-3 use), and a one-key layer roll never leaves
FIRST_HELD. Releasing closes the overlay and leaves the underlying scene
untouched. While ACTIVE, tapping a selector key cycles the scene
(`SCRY_SCENES` 3). The panel shows concise state only — layer readout, host
link (offline until M8), notification count (0 until M8), and the scene
selector. The chord is authoritative-only (only the master's merged matrix
holds both thumbs), so open+scene ride the snapshot — **wire is now v4, 27
bytes** (adds a `scry` byte); mixed versions degrade to the stale-link glyph,
so **flash both halves.** The overlay is pure presentation over the sim: the
duel keeps casting/warding/dying underneath, and `scry_mask` feeds only the
chord machine (host-verified: a chord-stepped world hashes identically to a
quiet one once scry state is masked). Host-verified by the `t7_*` suite
(`./preview --scry 0|1|2` renders each scene statically).

## M7 hardware checklist

1. Normal layer rolls never open the overlay: hold the left layer thumb and
   type numbers/nav (layer 1), hold the right layer thumb and type symbols
   (layer 2) — nothing pops.
2. Deliberate gesture: hold BOTH middle thumbs still (~0.4 s) — the scrying
   panel fades up over the duel (which keeps running beneath it). Release —
   the panel vanishes and the scene is exactly as it was.
3. Layer-3 use does NOT trip it: hold both thumbs and press an RGB/boot key
   (layer 3) — no overlay; and it stays suppressed until you fully release,
   so mashing layer 3 never flickers the panel.
4. While the panel is up, tap any other key to cycle the scene selector
   (three markers, the current one filled); release the selector to keep the
   panel open on the new scene.
5. The overlay shows on BOTH screens identically (it rides the snapshot); a
   TRRS pull while it is open drops it to the stale-link glyph, replug
   restores it.
6. Type prose with the panel closed and while flicking it open/shut: zero
   dropped or delayed keys.

## Next: M8 — host heartbeat and semantic protocol

The first host-branch milestone (`griffin_hostoled`, VIA/Vial disabled after
a static layout capture): versioned Raw HID messages with sequence + daemon
session ID, a heartbeat, scene class, and one synthetic notification — which
finally fill in the overlay's host-link and notification stubs. Daemon
absence/crash must never break the firmware scene.
