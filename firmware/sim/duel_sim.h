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
 * One packed byte retains row/column/side and both edge directions. Overflow
 * remains queue metadata rather than consuming an event slot. */
enum { SIM_EV_KEYDOWN = 1, SIM_EV_KEYUP = 2 };
typedef uint8_t sim_event_t;
#define SIM_EV_PACK(kind, side, row, col) \
    ((sim_event_t)(((col) & 0x07u) | (((row) & 0x03u) << 3) | \
                   (((side) & 0x01u) << 5) | (((kind) == SIM_EV_KEYDOWN) ? 0x40u : 0u)))
#define SIM_EV_KIND(event) (((event) & 0x40u) ? SIM_EV_KEYDOWN : SIM_EV_KEYUP)
#define SIM_EV_SIDE(event) (((event) >> 5) & 0x01u)
#define SIM_EV_ROW(event)  (((event) >> 3) & 0x03u)
#define SIM_EV_COL(event)  ((event) & 0x07u)

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
#ifdef ARCANE_M13
    /* Physical, privacy-preserving projection. Bits are row * 6 + column;
       no keycodes or emitted characters enter the simulation. */
    uint32_t held_pos[2];
    uint8_t  layer[2];
#endif
} sim_inputs_t;

/* ---- bounded event queue ------------------------------------------------
 * Producer and consumer both run in the firmware main loop (scan hooks and
 * housekeeping), so no atomics are needed. Overflow is explicit: pushes fail,
 * dropped events are counted separately from ordinary event storage. */
#define SIM_EVQ_CAP 16
typedef struct {
    sim_event_t ev[SIM_EVQ_CAP];
    uint8_t     n;
    uint8_t     dropped; /* since last drain, saturating */
} sim_evq_t;

// Returns false (and counts the drop) when the queue is full.
bool sim_evq_push(sim_evq_t *q, sim_event_t e);
// Copies queued events to out (capacity >= SIM_EVQ_CAP), returns the saturating
// overflow count separately, and resets the queue.
uint8_t sim_evq_drain(sim_evq_t *q, sim_event_t *out, uint8_t *dropped);

/* ---- world state -------------------------------------------------------- */
enum { POSE_IDLE = 0, POSE_CAST = 1, POSE_RECOVER = 2 };

#define SIM_CAST_TICKS   12 /* tap pose stays raised through the 10-tick wind-up */
#define SIM_RECOVER_TICKS 3
#ifdef ARCANE_M13
#    define SIM_MAX_HP 12
#else
#    define SIM_MAX_HP 5
#endif

/* ---- combat (M4) ---------------------------------------------------------
 * The battlefield is one 8-bit axis: u = 0 at the left wizard, 255 at the
 * right. A cast winds up for SIM_CAST_WINDUP_TICKS after the rising edge,
 * then the spell flies SIM_SPELL_SPEED units per tick. Resolution is
 * rule-based at the defender's doorstep: shield up => deflect, else the
 * spell continues to the impact threshold. All spell spawn/motion/resolve
 * runs only when SIMF_AUTHORITATIVE is set (the master), so the slave
 * structurally cannot decide outcomes. */
#define SIM_SPELL_SPEED       4  /* full flight 8 -> 248 in 60 ticks (2.4 s) */
#define SIM_CAST_WINDUP_TICKS 10 /* 400 ms: shortest M7.5 hardware candidate */
#define SIM_CAST_COOLDOWN     25 /* ~1 s between casts per wizard */
#define SIM_SHIELD_TICKS      10 /* any keydown shields that side ~400 ms */
#define SIM_SPAWN_L           8
#define SIM_SPAWN_R           247
#define SIM_DOORSTEP_R        240 /* just in front of the right wizard */
#define SIM_IMPACT_R          248
#define SIM_DOORSTEP_L        15
#define SIM_IMPACT_L          7

// M6 recipe vocabulary. A cast compiles the recent keydown burst into a kind
// byte. M7.5 uses the two previously spare high bits for a capped presentation
// tier; it never changes damage or any other combat rule.
enum { ELEM_FORCE = 0, ELEM_EMBER = 1, ELEM_FROST = 2, ELEM_VOID = 3 };
enum { MOD_NONE = 0, MOD_SWIFT = 1, MOD_HEAVY = 2 };
enum { PAY_IMPACT = 0 };
enum { SPELL_TIER_SHORT = 0, SPELL_TIER_MEDIUM = 1, SPELL_TIER_LONG = 2, SPELL_TIER_SATURATED = 3 };
// kind byte: bits0-1 element, bits2-3 modifier, bits4-5 payload, bits6-7 presentation tier
#define DUEL_KIND_PACK(elem, mod, pay) ((uint8_t)(((elem)&3) | (((mod)&3)<<2) | (((pay)&3)<<4)))
#define DUEL_KIND_ELEMENT(k)  ((k) & 3)
#define DUEL_KIND_MODIFIER(k) (((k) >> 2) & 3)
#define DUEL_KIND_PAYLOAD(k)  (((k) >> 4) & 3)
#define DUEL_KIND_TIER(k)     (((k) >> 6) & 3)
#define DUEL_KIND_WITH_TIER(k, tier) ((uint8_t)(((k) & 0x3F) | (((tier) & 3) << 6)))
#define RECIPE_EXPIRE_TICKS 25   /* ~1s of inactivity closes an open recipe */
#define RECIPE_N_MAX        15   /* recipe_n saturates here */

static inline uint8_t duel_recipe_tier(uint8_t ingredient_count) {
    if (ingredient_count <= 2) return SPELL_TIER_SHORT;
    if (ingredient_count <= 4) return SPELL_TIER_MEDIUM;
    if (ingredient_count <= 8) return SPELL_TIER_LONG;
    return SPELL_TIER_SATURATED;
}

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
#ifdef ARCANE_M13
#    define SIM_REGEN_TICKS 750 /* exactly 30 s per regained pip below max */
#else
#    define SIM_REGEN_TICKS 375 /* ~15 s per regained pip below max */
#endif
#define SIM_ROSTER_N    4      /* cosmetic roster variants cycled per replacement */

// fx kinds; the side names the DEFENDER (whose screen takes the hit/flash).
// FIZZLE = a spell dissipating at a downed wizard's doorstep.
enum { FX_NONE = 0, FX_IMPACT_L = 1, FX_IMPACT_R = 2,
       FX_DEFLECT_L = 3, FX_DEFLECT_R = 4, FX_FIZZLE_L = 5,
       FX_FIZZLE_R = 6 };

#ifdef ARCANE_M13
/* M13 24-bit compiled descriptor. The high byte must always remain zero. */
enum { SPELL_PROJECTILE = 0, SPELL_SINGULARITY = 1, SPELL_FIREBALL = 2,
       SPELL_BEAM = 3, SPELL_SWARM = 4, SPELL_GROUND_WAVE = 5,
       SPELL_CHAIN = 6, SPELL_CONJURE = 7 };
enum { PAY_DAMAGE = 0, PAY_HEAL = 1, PAY_STATUS = 2, PAY_HYBRID = 3 };
enum { TRAJ_GROUND = 0, TRAJ_LOW = 1, TRAJ_MID = 2, TRAJ_HIGH = 3,
       TRAJ_ROOF = 4, TRAJ_RETURNING = 5, TRAJ_AREA = 6, TRAJ_HOMING = 7 };
enum { STATUS_NONE = 0, STATUS_BURNING = 1, STATUS_FROZEN = 2,
       STATUS_DISRUPTED = 3, STATUS_MARKED = 4 };
enum { INTERACT_SOLID = 0, INTERACT_PHASE = 1, INTERACT_ABSORB = 2,
       INTERACT_COMBINE = 3 };
enum { TEMPO_DELIBERATE = 0, TEMPO_FLOWING = 1, TEMPO_RAPID = 2,
       TEMPO_FRANTIC = 3 };
enum { TREND_DECELERATING = 0, TREND_STEADY = 1, TREND_ACCELERATING = 2,
       TREND_IRREGULAR = 3 };

/* M13 one-shot outcomes retain the legacy 0..6 values above. Values 7..15
 * are deliberately side-neutral aftermaths except for the two ward-shatter
 * outcomes, whose side must be explicit for the local fracture animation. */
enum { FX_HEAL_L = 7, FX_HEAL_R = 8, FX_COMPLAINT = 9,
       FX_DETONATE = 10, FX_RESIDUE = 11, FX_COMBINE = 12,
       FX_COLLAPSE = 13, FX_WARD_SHATTER_L = 14, FX_WARD_SHATTER_R = 15 };

/* Bounded authoritative civic aftermath. The renderer derives movement,
 * task hats, room dressing, and phase from these fields; no actor list or
 * allocation is required. */
enum { AFTER_NONE = 0, AFTER_CHEER = 1, AFTER_COMPLAINT = 2,
       AFTER_PANIC = 3, AFTER_FIRE = 4, AFTER_INSPECT = 5,
       AFTER_REPAIR = 6, AFTER_MAX_CAST = 7 };
enum { RESIDENT_NORMAL = 0, RESIDENT_CHEER = 1, RESIDENT_COMPLAIN = 2,
       RESIDENT_PANIC = 3, RESIDENT_FIGHT_FIRE = 4,
       RESIDENT_INSPECT = 5, RESIDENT_REPAIR = 6,
       RESIDENT_WATCH_CAST = 7 };
enum { ROOM_CALM = 0, ROOM_ALERT = 1, ROOM_DISRUPTED = 2,
       ROOM_RECOVERY = 3 };
enum { OBJECT_NONE = 0, OBJECT_FIRE = 1, OBJECT_RESIDUE = 2,
       OBJECT_DAMAGED = 4 };
enum { WORLD_CALM = 0, WORLD_WONDER = 1, WORLD_CRISIS = 2,
       WORLD_RECOVERY = 3 };

typedef struct {
    uint8_t kind;
    uint8_t ticks;
    uint8_t intensity;
    uint8_t resident_state;
    uint8_t room_state;
    uint8_t object_state;
} sim_aftermath_t;

#define SPELL_DESC_FORM(d)        ((uint8_t)((d) & 7u))
#define SPELL_DESC_ELEMENT(d)     ((uint8_t)(((d) >> 3) & 3u))
#define SPELL_DESC_PAYLOAD(d)     ((uint8_t)(((d) >> 5) & 3u))
#define SPELL_DESC_TRAJECTORY(d)  ((uint8_t)(((d) >> 7) & 7u))
#define SPELL_DESC_MAGNITUDE(d)   ((uint8_t)(1u + (((d) >> 10) & 3u)))
#define SPELL_DESC_STATUS(d)      ((uint8_t)(((d) >> 12) & 7u))
#define SPELL_DESC_INTERACTION(d) ((uint8_t)(((d) >> 15) & 3u))
#define SPELL_DESC_TEMPO(d)       ((uint8_t)(((d) >> 17) & 3u))
#define SPELL_DESC_TREND(d)       ((uint8_t)(((d) >> 19) & 3u))
#define SPELL_DESC_VARIANCE(d)    ((uint8_t)(((d) >> 21) & 3u))
#define SPELL_DESC_VALID(d)       (((d) & 0x00800000u) != 0)
#define SPELL_DESC_PACK(form, elem, payload, traj, mag, status, interaction, tempo, trend, variance) \
    ((uint32_t)(((form) & 7u) | (((elem) & 3u) << 3) | (((payload) & 3u) << 5) | \
                (((traj) & 7u) << 7) | ((((mag) - 1u) & 3u) << 10) | \
                (((status) & 7u) << 12) | (((interaction) & 3u) << 15) | \
                (((tempo) & 3u) << 17) | (((trend) & 3u) << 19) | \
                (((variance) & 3u) << 21) | 0x00800000u))

enum { INC_IDLE = 0, INC_COLLECTING = 1, INC_WINDUP = 2,
       INC_PREPARED = 3, INC_REARM = 4 };
#define M13_IDLE_COMMIT_TICKS 13
#define M13_FORCE_COMMIT_TICKS 250
#define M13_WINDUP_MIN_TICKS 8
#define M13_WINDUP_MAX_TICKS 50

typedef struct {
    uint32_t hash;
    uint32_t seen_pos;
    uint16_t gap_sum;
    uint16_t held_ticks;
    uint8_t row_hist[4];
    uint8_t row_recent[4];
    uint8_t key_count;
    uint8_t last_pos;
    uint8_t last_layer;
    uint8_t last_gap;
    uint8_t first_gap;
    uint8_t gap_min;
    uint8_t gap_max;
    uint8_t elapsed;
    uint8_t idle;
    uint8_t quiet;
    uint8_t transitions;
    uint8_t turns;
    uint8_t repetitions;
    uint8_t layer_transitions;
    uint8_t overlap_peak;
    uint8_t rhythm_changes;
    int8_t  column_drift;
    int8_t  last_direction;
    uint8_t gap_count;
    uint8_t newest_rank;
    uint8_t last_gap_bucket;
} sim_incantation_t;

uint8_t m13_complexity(const sim_incantation_t *inc);
uint32_t m13_compile(const sim_incantation_t *inc, uint8_t variant);
#endif

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
    uint8_t  cast_tier;     /* M7.5 capped presentation tier while charging */
    uint8_t  recipe_idle;   /* ticks since last ingredient; RECIPE_EXPIRE_TICKS -> clear */
    uint8_t  _pad;          /* explicit padding: keeps world hashing deterministic */
    uint16_t regen_ticks;   /* countdown to next regen pip; local, never in snapshots */
#ifdef ARCANE_M13
    sim_incantation_t inc;
    uint32_t pending_desc;
    uint32_t prepared_desc;
    uint32_t prev_held;
    uint8_t  inc_state;
    uint8_t  ward_strength;
    uint8_t  ward_capacity;
    uint8_t  ward_focus;
    uint8_t  windup_total;
    uint8_t  prepared;
    uint8_t  rearm_lock;
    uint8_t  status;
    uint8_t  status_intensity;
    uint8_t  status_ticks;
    uint8_t  status_burned;
#endif
} sim_wizard_t;

typedef struct {
    uint8_t active; /* 0/1 — one slot per wizard */
    uint8_t pos;    /* battlefield u: 0 = left wizard, 255 = right wizard */
    int8_t  dir;    /* units per tick, + toward the right */
    uint8_t kind;   /* DUEL_KIND_PACK element/modifier/payload */
#ifdef ARCANE_M13
    uint32_t descriptor;
    uint8_t progress;
    uint8_t age;
    uint8_t aux;
    uint8_t resolved;
#endif
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
#ifdef ARCANE_M13
    sim_aftermath_t aftermath[2];
    uint8_t         world_state;
    uint8_t         _m13_pad;
#endif
} sim_world_t;

#ifdef ARCANE_M13
uint8_t m13_aftermath_shared(const sim_world_t *world);
uint8_t m13_aftermath_revision(const sim_world_t *world);
#endif

// True while the scrying overlay should be drawn (ACTIVE or SELECT). Slave
// worlds decoded from the wire land in exactly these states, so both halves
// agree from the same predicate.
static inline bool scry_is_open(const sim_world_t *w) {
    return w->scry.state == SCRY_ACTIVE || w->scry.state == SCRY_SELECT;
}

// start_tick is normally 0; wrap tests initialise near UINT32_MAX.
void sim_init(sim_world_t *w, uint8_t flags, uint32_t start_tick);
// Advances exactly one tick. `ev` is the batch drained for this tick.
void sim_tick(sim_world_t *w, sim_inputs_t in, const sim_event_t *ev, uint8_t n,
              uint8_t dropped);
