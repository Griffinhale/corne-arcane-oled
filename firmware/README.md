# griffin_anim — Corne Arcane OLED (firmware-only)

Firmware-only OLED experiment keymap for the Corne v3 / RP2040 dual-OLED
"spell duel" system. Vial stays enabled here. `griffin` remains the stable
recovery baseline — do not experiment there.

The compiled four-layer default is captured from the `corne-arcane.vil` export
in the repository's parent directory and stored in `corne_arcane_layout.h`.
The live `griffin_anim` and Vial-free
`griffin_hostoled` keymaps both use this map. A newly built Vial firmware has
a fresh build ID, so its first boot resets dynamic-keymap EEPROM to this
compiled default. The host branch enables custom Raw HID and disables Vial/VIA.

## Status: M10 implemented; awaiting hardware verification (2026-07-13)

M0–M7 are flashed and confirmed on the physical keyboard: cross-screen bolts,
wards/health, the KO arc (collapse → downed → medic drag-off → replacement),
recipe-driven spell variety, VOID piercing, and the scry overlay all render
correctly with typing unaffected. M7.5's combat presentation, 10-tick wind-up,
recipe-scaled effects, upper-canvas composition, and captured default layout
are now accepted on both physical halves.

**M7.5 — Combat presentation and composition polish (hardware-verified).**
Mechanics remain unchanged except
for the explicitly requested fictional release delay: wind-up is now 10 ticks
(400 ms), the shortest candidate in the 10–14 tick range, while emitted typing
still takes the untouched QMK path immediately. Presentation changes are:

- Impact is local to the defender: directional contact burst, recoil/compression,
  bounded debris, restrained corner disturbance, and a marker at the lost pip.
- Deflect keeps a thick flaring ward dominant and throws broken carrier streaks
  back toward the gap; the wizard remains stable and no impact border is reused.
- Fizzle contracts from a sparse shell into a small core away from the body,
  with no flash or recoil.
- VOID visibly splits/punctures an active ward while the carrier continues
  through it; the later damaging impact retains the impact grammar.
- Recipe count maps to capped short/medium/long/saturated presentation tiers.
  Tier changes charge, carrier, trail, and outcome size only—never damage,
  flight speed, health, ward rules, or lifecycle.
- A growing rune and gathering motes occupy the upper canvas during wind-up;
  element-specific lanes and staff-tip battlefield mapping reduce overlap near
  the actor.

The authoritative additions are deliberately small: the former recipe-reserved
byte is now `cast_tier`, the two spare high bits of `spell.kind` carry the spawned
tier, and two packed charge bytes (wind-up + tier) make anticipation absolute on
the slave. The snapshot therefore advances from v4/27 bytes to v5/29 bytes,
still below QMK's 32-byte RPC limit. Recoil, particles, outcome latching, and
VOID ward deformation remain presentation-only render state.

**M6.5 — juice + first outcome-changing element (done, hardware-verified).**
Bench feedback on M6 was that spell variety read
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
streams CRC'd snapshots (`sim/duel_proto.{h,c}`, now 31 bytes at M10) to
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

- `corne_arcane_layout.h` — shared compiled four-layer typing default captured
  from the Vial export. Layer/thumb positions are translated into
  `LAYOUT_split_3x6_3` physical order, including the reversed right matrix rows.
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
    M7.5 adds capped recipe presentation tiers and a 10-tick cast wind-up.
  - `duel_proto.{h,c}` — split snapshot wire format (v7, 31 bytes, CRC-8)
    and the slave-side sequence/session acceptance rules. The M7 `scry` byte
    carries overlay state; M7.5's two packed charge bytes carry absolute
    wind-up/tier state so both screens draw the same anticipation; M8's final
    context byte carries online/scene/notification state without entering the
    authoritative simulation.
  - `duel_host.{h,c}` — fixed 32-byte M8 Raw HID envelope, CRC validation,
    daemon session/sequence ordering, malformed/stale counters, heartbeat
    expiry, and compact disposable context. It has no clock or QMK dependency.
  - `duel_draw.{h,c}` — all drawing, onto a 512-byte 1bpp `duel_fb_t`
    (portrait 32x128). `wiz_draw` is the M1 silhouette; `wiz_draw_scene`
    renders a full half from a world snapshot (plus the `DUEL_DEBUG_HUD`
    overlay: bottom-row tick odometer sweeping once/second, top-corner
    dropped-event dots). M7.5 adds progressive upper-canvas charges, capped
    recipe-scaled carriers, distinct outcome grammars, and visible VOID ward
    puncture. `draw_overlay` remains the M7 scrying panel drawn on top of the
    running scene whenever `scry_is_open`.
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
  builds a terminal previewer for both canvases (`./preview --scenario impact`,
  `./preview --scenario void-pierce`, `./preview --scry 0`,
  `./preview traces/duel_ko.trace --play`). Regenerate goldens only via
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
cd ~/src/vial-qmk                         # qmk + arm-none-eabi-gcc are on PATH
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
free, ~1 s cooldown) winds up 10 ticks, then the bolt flies 4 units/tick —
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
the compiler reads the last four for element/modifier identity, while M7.5
uses the full capped count for presentation tier. The **element** is the dominant row class
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
`kind`, which now rides the snapshot. M6 advanced the wire format to **v3 at 26
bytes** (adds `spell_kind` per slot); M7 and M7.5 later advanced it to v5/29
bytes. Mixed versions degrade to the
stale-link glyph, so **flash both halves.** Absolute state means a dropped
packet costs only a frame of glyph — the next snapshot restores the exact
kind. Host-verified by the `t6_*` suite (`traces/duel_recipes.trace`;
`./preview --spell-kind frost/swift/medium` and friends render each glyph statically).

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

**M7 — Layer-key scrying overlay (done, hardware-verified).** The last
firmware-only milestone: a deliberate dense-information
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
link and notification slots (later populated by M8), plus the scene
selector. The chord is authoritative-only (only the master's merged matrix
holds both thumbs), so open+scene ride the snapshot — M7 advanced the wire to
**v4/27 bytes** (adds a `scry` byte), and M7.5 now uses v5/29 bytes. Mixed
versions degrade to the stale-link glyph, so **flash both halves.** The overlay
is pure presentation over the sim: the
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

## M7.5 flash and hardware verification checklist

The v5 snapshot/shared format changed. Flash **both** halves from the same
`griffin_anim` build; a mixed v4/v5 pair is not supported.

1. Confirm the recovery keymap `griffin` has not been edited. Disconnect USB,
   then disconnect TRRS only while the keyboard is unpowered.
2. Build the experimental keymap:
   `qmk compile -kb crkbd/rev1 -km griffin_anim -e CONVERT_TO=rp2040_ce`.
   Confirm the artifact is `crkbd_rev1_griffin_anim_rp2040_ce.uf2`.
3. With the halves separate, flash left with
   `qmk flash -kb crkbd/rev1 -km griffin_anim -e CONVERT_TO=rp2040_ce -bl uf2-split-left`.
4. Flash right from the same source/artifact with
   `qmk flash -kb crkbd/rev1 -km griffin_anim -e CONVERT_TO=rp2040_ce -bl uf2-split-right`.
5. Power down, reconnect TRRS, then attach USB to the left half only. Confirm
   neither OLED shows the stale-link glyph after the initial 0.5 s sync window.
6. Compare short (one or two ingredients) and long (five to eight ingredients)
   recipes: charge, carrier silhouette, trail, and outcome must be visibly
   larger/richer, while damage remains one pip in both cases.
7. Type ordinary prose continuously. The fictional release should anticipate
   for about 400 ms without any dropped, reordered, or delayed characters.
8. Trigger a normal impact, ward deflect, and downed-wizard fizzle. Impact must
   show contact/recoil/lost health; deflect must keep the wizard stable and
   redirect fragments from a flared ward; fizzle must remain small and harmless.
9. Cast VOID into an active ward. Observe a visible split/puncture, the carrier
   continuing through, and then the normal damaging impact grammar.
10. During short and long wind-ups, verify the growing rune/motes are legible in
    the upper canvas on both OLEDs—not clipped, inverted, or hidden by the actor.
11. Watch several elements, tiers, wards, impacts, and health states on both
    halves. There must be no OLED clipping or accidental wizard/health overlap.
12. Type rapidly on both halves while spells cross the gap. Confirm no split
    desynchronization, stale-link marker, backward jump, or mismatched tier.
13. Run a full five-hit KO: collapse, protected downed pose, medic drag-off, and
    replacement must remain clear; a cast at the downed wizard must still fizzle.
14. Open, change, and close the scry overlay on both screens. It must remain
    intact, synchronized, and return to the unchanged duel scene.

If typing, boot, or split behavior regresses, stop evaluation and flash the
known-good `griffin` recovery keymap on both halves.

## M8 — host heartbeat and semantic protocol (hardware-verified)

The Vial-free `griffin_hostoled` build now contains the entire accepted duel as
its offline fallback plus a bounded semantic Raw HID receiver. Reports are
fixed at 32 bytes with protocol version, type, daemon session ID, sequence,
absolute scene/notification summary, and CRC-8. A new daemon session is adopted
only through `HELLO` sequence zero; the previous session is remembered so
delayed packets cannot roll a restart backward. Only HELLO/HEARTBEAT refresh
liveness. After 1.5 seconds without one, firmware clears every external field
and continues the untouched duel.

The Linux daemon in `../host/` uses only Python's standard library. It discovers
QMK usage page `0xFF60` / usage `0x61`, sends paced absolute reports, and never
streams framebuffers. Host context is packed into one byte of split snapshot
v6/30 bytes, so the slave renders the same online bar, scene marker, and
notification count (visually capped to four dots). Combat state and world-hash
goldens are unchanged.

### M8 build and flash

Build is already verified; the artifact is
`~/src/vial-qmk/crkbd_rev1_griffin_hostoled_rp2040_ce.uf2`.

```bash
cd ~/src/vial-qmk
# Power down and disconnect TRRS; flash one half at a time.
qmk flash -kb crkbd/rev1 -km griffin_hostoled -e CONVERT_TO=rp2040_ce -bl uf2-split-left
qmk flash -kb crkbd/rev1 -km griffin_hostoled -e CONVERT_TO=rp2040_ce -bl uf2-split-right
```

Reassemble only while unpowered: TRRS connected, then USB into the left half.
Do not open Vial against this build.

### M8 hardware acceptance checklist

1. Before starting the daemon, type on both halves and run several full casts,
   wards, impacts, a KO/medic/replacement sequence, and the scry chord. The duel
   and captured layout must behave exactly like accepted M7.5; the scry host row
   must show the disconnected glyph.
2. In another terminal run `cd ~/dev/corne-arcane-oled/host && ./run_tests.sh`.
3. Start `python3 -m arcane_host.daemon --scene archive --notify 2 --verbose`.
   It should discover exactly one QMK Raw HID device.
4. Hold the scry chord. Both OLEDs must show the solid host bar, two notification
   dots, and archive scene marker. Release must return to the continuing duel.
5. Type rapidly on both halves for at least 30 seconds while heartbeats run.
   Confirm immediate output, normal spells, no stale split marker, and no OLED
   pause or desynchronization.
6. Stop the daemon with Ctrl-C while observing/reopening scry. Within 1.5 seconds
   both halves must return to the disconnected host glyph, zero dots, and local
   duel scene; combat must never reset or pause.
7. Restart with `python3 -m arcane_host.daemon --scene focus --notify 1 --verbose`.
   Both halves must adopt the new session, focus marker, and one dot without
   displaying delayed archive context.
8. Stop/restart it several times and suspend/wake the host once. Every absence
   must expire cleanly; every new session must converge on both screens.
9. Confirm Vial is not used with `griffin_hostoled`. Finish with another prose
   typing pass and full KO sequence.

If the host build regresses typing, split behavior, boot, or offline animation,
flash `griffin_anim` or the stable `griffin` keymap onto both halves. The exact
hardware-accepted M7.5 binary is preserved as
`~/src/vial-qmk/crkbd_rev1_griffin_anim_m75_verified_rp2040_ce.uf2`.

**Hardware result (2026-07-13): accepted.** The physical-keyboard run and photo
review confirm the ordinary duel fallback and synchronized scry/host
presentation render cleanly on the real OLEDs without visible clipping or
corruption. Daemon-driven context works across both halves.

## M9 — Application-aware Arcane Archive (hardware-verified)

The packaged Plasma 6 host service receives only KWin `resourceClass` and
`desktopFileName` over a private session D-Bus method. Browser aliases settle
to Archive after 200 ms; all empty, unknown, desktop, and non-browser focus
settles to Duel. No title, URL, tab, document, or page data is read, sent, or
retained. Automatic arbitration is the daemon default; explicit `--scene` is a
diagnostic override. HID absence/reconnect and KWin restart are recovered
without exiting, with a new daemon session and `HELLO` for every keyboard
reconnect.

When the synchronized external context is online/Archive, the renderer adds a
mirrored sparse shelf/book/rune arch at `y=3..44` beneath the accepted duel.
Existing shield state supplies an immediate, bounded ~400 ms typing pulse;
existing wind-up/tier state strengthens the cast rune. Wizards, wards, spells,
outcomes, health, KO/medic flow, scry, stale link, and debug HUD retain their
precedence. Duel and Focus take the byte-identical pre-M9 render path.

Raw HID remains v1/32 bytes, split snapshot remains v6/30 bytes, and
`sim_world_t`, combat rules, and simulation goldens are unchanged. Terminal
preview scenarios are `archive-idle`, `archive-pulse`, `archive-cast`,
`archive-impact`, `archive-ko`, and `archive-scry`; `--host-scene` accepts
`duel`, `archive`, or `focus`.

### M9 hardware acceptance checklist

1. Import `corne.nix` directly from this checkout, rebuild NixOS, and confirm
   `systemctl --user status corne-arcane-host` plus the KWin bridge log.
2. Build `griffin_hostoled` and flash both halves individually from the same
   source so both receive the M9 renderer.
3. Confirm Firefox and Chrome enter Archive after about 200 ms; terminal,
   editor, desktop, empty, and unknown focus return to Duel.
4. Rapidly Alt-Tab across browser/non-browser windows and confirm no flicker.
5. Type ordinary prose on both halves: characters must remain immediate while
   each upper canvas produces a visible bounded pulse.
6. Compare short and long recipe charges in Archive; the latter is richer but
   still changes no damage or combat timing.
7. Exercise impact, deflect, fizzle, VOID penetration, KO, medic, and
   replacement sequences; all accepted duel grammars must remain clear.
8. Open and close scry in both scenes. Its panel must stay clear and restore
   the continuing Archive or Duel beneath it.
9. Check clipping, health readability, split synchronization, suspend/wake,
   HID unplug/replug, KWin restart, and the 1.5-second daemon expiry fallback.
10. Finish with fast typing on both halves and confirm no dropped, reordered,
    or delayed output.

Rollback is protocol-compatible: stop/disable the user service for complete
Duel fallback within 1.5 seconds, roll back the NixOS generation, revert only
the M9 commit(s), or reflash both halves with the M8 build. The preserved M7.5
rollback UF2 remains
`~/src/vial-qmk/crkbd_rev1_griffin_anim_m75_verified_rp2040_ce.uf2`.

**Hardware result (2026-07-13): accepted.** On Debian 13 Plasma/Wayland, real
application-focus changes reach the daemon and switch both physical OLEDs
between synchronized Archive and Duel correctly. This proves the complete KWin
→ session D-Bus → daemon → Raw HID → split-render path. The Archive artwork can
be refined further in M11 polish without reopening the M9 mechanism or changing
its interfaces.

## M10 — Notification policy and adapters (awaiting hardware verification)

The host now owns a bounded monotonic-time policy. Low/normal/transient-critical
events aggregate in a fixed six-second batch; repeats never extend it. New
batches have a ten-second start-to-start budget, and cooldown-only arrivals are
suppressed and counted rather than replayed. Non-transient critical alerts
bypass that budget and persist until their desktop notification closes, the
private clear method is called, or daemon context expires. Counts saturate at
15. Category ties select the newest distinct event at the highest active
priority, while age remains anchored to its first occurrence.

Raw HID advances to v2 without changing the 32-byte report. Every message is an
absolute six-field summary: scene, count, category, priority, age, and
persistence. NOTIFY makes changes prompt but never refreshes liveness;
500 ms heartbeats take scheduling priority. Firmware also accepts the old
v1/two-byte summaries, which retain count-only scry behavior and do not draw a
corner glyph. Split sync advances to v7/31 bytes: the old external byte uses its
top bit for persistence and a new packed byte carries category/priority/age;
CRC covers both.

Each half draws a deterministic mirrored 5x7 category sigil in its outer upper
corner. Priority adds framing/sparks, count adds up to four pips, age removes
accents, and persistence adds an anchor. It draws above Duel/Archive/combat and
below scry, stale-link, and debug overlays. Opening scry replaces the outer
sigil with the normalized panel summary. Count-zero Duel, Focus, and Archive
frames remain byte-identical; `sim_world_t`, mechanics, combat, and all replay
goldens are unchanged. Preview scenarios are `terminal-completion`,
`aggregated-normal`, `persistent-critical`, `aged-alert`, and
`alert-under-scry`.

The existing private D-Bus object now also exposes synthetic injection, clear,
and redacted terminal completion. The packaged `corne-arcane-event` client and
Zsh hook report only monotonic duration plus exit status for commands lasting
at least ten seconds, asynchronously and only when a recognized terminal is
unfocused. Freedesktop monitoring uses a separate monitor connection. Summary
and body are discarded immediately after a per-session salted digest; actions,
icons, URLs, images, and plaintext are never logged, persisted, or transmitted.
Monitor denial disables only that adapter.

### M10 hardware acceptance sequence

1. Refresh and build `griffin_hostoled`, then power down, disconnect TRRS, and
   flash **both halves** from the same source. Split v7 is incompatible with v6:

   ```bash
   cd ~/dev/corne-arcane-oled
   ./host/install_firmware.sh
   cd ~/src/vial-qmk
   qmk compile -kb crkbd/rev1 -km griffin_hostoled -e CONVERT_TO=rp2040_ce
   qmk flash -kb crkbd/rev1 -km griffin_hostoled -e CONVERT_TO=rp2040_ce -bl uf2-split-left
   qmk flash -kb crkbd/rev1 -km griffin_hostoled -e CONVERT_TO=rp2040_ce -bl uf2-split-right
   ```
2. Inject terminal/normal, repeated and distinct normal, aged, and
   security/critical/persistent events with `corne-arcane-event`. Confirm fixed
   aggregation expiry, cooldown suppression, mirrored glyph grammar, scry
   replacement, persistence, and explicit clear.
3. Source the packaged Zsh hook. Run a 10+ second command, leave Konsole before
   completion, and confirm a terminal alert. Repeat while Konsole remains
   focused and confirm suppression; verify success is low and failure normal.
4. Send low, normal, transient-critical, and persistent-critical desktop
   notifications, including replacement and dismissal. Confirm focused-source
   suppression, critical pass-through, replacement without count growth, and
   persistent removal on close.
5. Flood repeated notifications while typing rapidly on both halves. Confirm
   key output, 500 ms heartbeat, split convergence, and scene changes remain
   immediate; run combat, KO/medic, Archive pulse, scry, stale marker, and debug
   checks for visibility.
6. Stop/restart the daemon, unplug/replug HID, suspend/wake, and test a denied
   desktop monitor. Offline Duel must return within 1.5 seconds and every live
   reconnect must converge through a fresh HELLO.

Until all six steps pass, M10 remains implemented/awaiting hardware
verification. Full rollback is accepted commit `26c49a2` with its M9 daemon and
v6 firmware pair, or the preserved M7.5 recovery UF2. Do not run the M10 daemon
against M9 firmware; stop the service or install the M9 package first.
