/*
 * duel_sim.h — deterministic fixed-tick simulation core (M2).
 *
 * Hardware-agnostic: no QMK includes, no statics (all state lives in caller
 * structs so the host harness can run master+slave instances side by side),
 * integer math only, no allocation, no time reads. The QMK glue in keymap.c
 * owns the wall-clock -> tick conversion.
 *
 * Determinism contract: identical sim_init() plus an identical ordered stream
 * of (inputs, events) per tick produces a bit-identical sim_world_t. The tick
 * counter only advances inside sim_tick().
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define SIM_TICK_MS 40 /* 25 Hz */

#define SIM_SIDE_L 0
#define SIM_SIDE_R 1

// World flags. Combat outcomes (M4) only resolve when authoritative — set on
// the master half only, so the slave structurally cannot decide a duel.
#define SIMF_AUTHORITATIVE 0x01

/* ---- events: the detail channel ----------------------------------------
 * Row/col are captured from day one: M6 spell recipes group events by row
 * class and position, so the queue format never has to change. 4 bytes.
 * SIM_EV_OVERFLOW is injected by the queue at drain time; its `row` field
 * carries the number of dropped events. */
enum { SIM_EV_KEYDOWN = 1, SIM_EV_KEYUP = 2, SIM_EV_OVERFLOW = 3 };
typedef struct {
    uint8_t kind, side, row, col;
} sim_event_t;

/* ---- inputs: the level-sampled robustness channel ----------------------
 * Poses and cast edges key off sampled key levels, so a dropped event can
 * never wedge the state machine. bit0 = left any-key-down, bit1 = right.
 *
 * scry_mask (M7) is the same level-sampled idea for the layer-key chord: the
 * glue samples the two physical layer-key positions and whether any OTHER key
 * is held, so the chord machine can never be wedged by a dropped edge either.
 * SCRY_M_* below name the bits. */
#define SCRY_M_L     0x01 /* left layer key (physical position) held */
#define SCRY_M_R     0x02 /* right layer key held */
#define SCRY_M_OTHER 0x04 /* any non-layer-key key held, either half */
typedef struct {
    uint8_t down_mask;
    uint8_t scry_mask;
} sim_inputs_t;

/* ---- bounded event queue ------------------------------------------------
 * Producer and consumer both run in the firmware main loop (scan hooks and
 * housekeeping), so no atomics are needed. Overflow is explicit: pushes fail,
 * dropped events are counted, and drain injects one SIM_EV_OVERFLOW. */
#define SIM_EVQ_CAP 16
typedef struct {
    sim_event_t ev[SIM_EVQ_CAP];
    uint8_t     n;
    uint8_t     dropped; /* since last drain, saturating */
} sim_evq_t;

// Returns false (and counts the drop) when the queue is full.
bool sim_evq_push(sim_evq_t *q, sim_event_t e);
// Copies queued events to out (capacity >= SIM_EVQ_CAP + 1), appending one
// SIM_EV_OVERFLOW carrying the drop count if any push failed. Resets q.
uint8_t sim_evq_drain(sim_evq_t *q, sim_event_t *out);

/* ---- world state -------------------------------------------------------- */
enum { POSE_IDLE = 0, POSE_CAST = 1, POSE_RECOVER = 2 };

#define SIM_CAST_TICKS    9 /* ~360 ms at 25 Hz, re-armed while a key is held */
#define SIM_RECOVER_TICKS 3
#define SIM_MAX_HP        5

/* ---- combat (M4) ---------------------------------------------------------
 * The battlefield is one 8-bit axis: u = 0 at the left wizard, 255 at the
 * right. A cast winds up for SIM_CAST_WINDUP_TICKS after the rising edge,
 * then the spell flies SIM_SPELL_SPEED units per tick. Resolution is
 * rule-based at the defender's doorstep: shield up => deflect, else the
 * spell continues to the impact threshold. All spell spawn/motion/resolve
 * runs only when SIMF_AUTHORITATIVE is set (the master), so the slave
 * structurally cannot decide outcomes. */
#define SIM_SPELL_SPEED       4  /* full flight 8 -> 248 in 60 ticks (2.4 s) */
#define SIM_CAST_WINDUP_TICKS 6
#define SIM_CAST_COOLDOWN     25 /* ~1 s between casts per wizard */
#define SIM_SHIELD_TICKS      10 /* any keydown shields that side ~400 ms */
#define SIM_SPAWN_L           8
#define SIM_SPAWN_R           247
#define SIM_DOORSTEP_R        240 /* just in front of the right wizard */
#define SIM_IMPACT_R          248
#define SIM_DOORSTEP_L        15
#define SIM_IMPACT_L          7

// M6 recipe vocabulary. A cast compiles the recent keydown burst into a kind byte.
enum { ELEM_FORCE = 0, ELEM_EMBER = 1, ELEM_FROST = 2, ELEM_VOID = 3 };
enum { MOD_NONE = 0, MOD_SWIFT = 1, MOD_HEAVY = 2 };
enum { PAY_IMPACT = 0 };
// kind byte: bits0-1 element, bits2-3 modifier, bits4-5 payload, bits6-7 spare
#define DUEL_KIND_PACK(elem, mod, pay) ((uint8_t)(((elem)&3) | (((mod)&3)<<2) | (((pay)&3)<<4)))
#define DUEL_KIND_ELEMENT(k)  ((k) & 3)
#define DUEL_KIND_MODIFIER(k) (((k) >> 2) & 3)
#define DUEL_KIND_PAYLOAD(k)  (((k) >> 4) & 3)
#define RECIPE_EXPIRE_TICKS 25   /* ~1s of inactivity closes an open recipe */
#define RECIPE_N_MAX        15   /* recipe_n saturates here */

/* ---- lifecycle & roster (M5) ---------------------------------------------
 * When a wizard's hp reaches 0 it walks the COLLAPSE -> DOWNED -> MEDIC ->
 * REPLACE arc on fixed timers, then returns ACTIVE at full hp with the next
 * cosmetic roster variant. Only the authoritative sim advances the arc. */
enum { LIFE_ACTIVE = 0, LIFE_COLLAPSE = 1, LIFE_DOWNED = 2, LIFE_MEDIC = 3, LIFE_REPLACE = 4 };
#define SIM_COLLAPSE_TICKS 12  /* ~0.48 s keel-over */
#define SIM_DOWNED_TICKS   25  /* ~1 s protected on the ground */
#define SIM_MEDIC_TICKS    25  /* ~1 s medic drags the body off */
#define SIM_REPLACE_TICKS  20  /* ~0.8 s replacement walks in */
/* total downtime 82 ticks = 3.28 s at 25 Hz — no dead ends: no input needed to progress */
#define SIM_REGEN_TICKS 375    /* ~15 s per regained pip below max */
#define SIM_ROSTER_N    4      /* cosmetic roster variants cycled per replacement */

// fx kinds; the side names the DEFENDER (whose screen takes the hit/flash).
// FIZZLE = a spell dissipating at a downed wizard's doorstep.
enum { FX_NONE = 0, FX_IMPACT_L = 1, FX_IMPACT_R = 2, FX_DEFLECT_L = 3, FX_DEFLECT_R = 4, FX_FIZZLE_L = 5, FX_FIZZLE_R = 6 };

typedef struct {
    uint8_t pose;          /* POSE_* */
    uint8_t pose_ticks;    /* remaining in CAST/RECOVER */
    uint8_t hp;            /* 1..SIM_MAX_HP; reaching 0 starts the lifecycle arc */
    uint8_t shield_ticks;  /* decays per tick */
    uint8_t cast_cooldown; /* ticks until the next cast may start */
    uint8_t cast_windup;   /* rising edge -> spawn countdown */
    uint8_t  life;          /* LIFE_*; only the authoritative sim transitions it */
    uint8_t  life_ticks;    /* ticks remaining in the current non-ACTIVE phase */
    uint8_t  variant;       /* roster cosmetic 0..SIM_ROSTER_N-1; ++ on entering REPLACE */
    uint8_t  recipe_hist;   /* last-4 row classes, 2 bits each, newest in bits0-1; the
                               modifier reads its repetition/alternation pattern */
    uint8_t  recipe_n;      /* ingredients since recipe start, saturating at RECIPE_N_MAX */
    uint8_t  recipe_rsv;    /* reserved (0): headroom for a future cadence signal */
    uint8_t  recipe_idle;   /* ticks since last ingredient; RECIPE_EXPIRE_TICKS -> clear */
    uint8_t  _pad;          /* explicit padding: keeps world hashing deterministic */
    uint16_t regen_ticks;   /* countdown to next regen pip; local, never in snapshots */
} sim_wizard_t;

typedef struct {
    uint8_t active; /* 0/1 — one slot per wizard */
    uint8_t pos;    /* battlefield u: 0 = left wizard, 255 = right wizard */
    int8_t  dir;    /* units per tick, + toward the right */
    uint8_t kind;   /* DUEL_KIND_PACK element/modifier/payload */
} sim_spell_t;

/* ---- layer-key scrying overlay (M7) --------------------------------------
 * An explicit chord state machine, driven purely by the level-sampled
 * scry_mask, opens a temporary in-world overlay above the still-running duel.
 * It is authoritative-only (the master owns both layer-key positions via its
 * merged matrix), so open/scene ride the snapshot and the slave shows the
 * overlay purely from the wire.
 *
 * The two layer keys are the middle thumbs; co-holding them is exactly QMK
 * layer 3, so a deliberate scry must be told apart from ordinary layer-3 use.
 * The machine does so structurally:
 *   IDLE       neither layer key held (or a lone key -> FIRST_HELD)
 *   FIRST_HELD one layer key held — where every normal layer roll lives, so a
 *              roll (one thumb + other keys) never escalates
 *   PENDING    both layer keys held and NOTHING else; a dwell counts down
 *   ACTIVE     dwell elapsed: overlay shown
 *   SELECT     a selector key tapped while ACTIVE cycles the scene
 *   CANCELLED  any other key touched during PENDING (i.e. real layer-3 use);
 *              latched until a full release so it cannot flicker open
 * A release from ACTIVE/SELECT simply closes the overlay; the underlying
 * scene is never disturbed. */
enum { SCRY_IDLE = 0, SCRY_FIRST_HELD = 1, SCRY_PENDING = 2,
       SCRY_ACTIVE = 3, SCRY_SELECT = 4, SCRY_CANCELLED = 5 };
#define SCRY_PENDING_TICKS 10 /* ~0.4 s deliberate still-hold before the overlay opens */
#define SCRY_SCENES        3  /* concise panels the selector cycles */

typedef struct {
    uint8_t state; /* SCRY_* */
    uint8_t timer; /* PENDING dwell countdown */
    uint8_t scene; /* 0..SCRY_SCENES-1, cycled by the selector */
    uint8_t _pad;  /* explicit padding: keeps world hashing deterministic */
} sim_scry_t;

typedef struct {
    uint32_t     tick;
    uint8_t      flags;          /* SIMF_* */
    uint8_t      prev_down_mask; /* for rising-edge cast detection */
    sim_wizard_t wiz[2];
    sim_spell_t  spell[2];
    uint8_t      fx_seq;         /* increments once per one-shot outcome (M4) */
    uint8_t      fx_kind;        /* FX_*: none/impact/deflect/fizzle, L/R per defender */
    uint16_t     overflow_count; /* lifetime dropped events, saturating */
    uint16_t     _pad;           /* explicit padding: keeps world hashing deterministic */
    sim_scry_t   scry;           /* M7 layer-key overlay chord machine (authoritative-only) */
} sim_world_t;

// True while the scrying overlay should be drawn (ACTIVE or SELECT). Slave
// worlds decoded from the wire land in exactly these states, so both halves
// agree from the same predicate.
static inline bool scry_is_open(const sim_world_t *w) {
    return w->scry.state == SCRY_ACTIVE || w->scry.state == SCRY_SELECT;
}

// start_tick is normally 0; wrap tests initialise near UINT32_MAX.
void sim_init(sim_world_t *w, uint8_t flags, uint32_t start_tick);
// Advances exactly one tick. `ev` is the batch drained for this tick.
void sim_tick(sim_world_t *w, sim_inputs_t in, const sim_event_t *ev, uint8_t n);
