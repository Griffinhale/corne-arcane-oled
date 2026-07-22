/* Hardware-independent timing, concurrency, and session presentation policy. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "duel_display.h"
#include "duel_host.h"
#include "duel_sim.h"
#include "duel_view.h"

/* ---- physical input sampling --------------------------------------------
 * crkbd geometry, mirrored by compile-time asserts in keymap.c. The glue
 * copies its matrix rows into a plain uint16_t[8] (left hand rows 0..3,
 * right hand rows 4..7) and everything below is a pure function of that. */
#define DUEL_INPUT_ROWS          8u
#define DUEL_INPUT_ROWS_PER_HAND 4u
#define DUEL_INPUT_COLS          6u

/* The two momentary layer thumbs (middle thumb of each hand) by physical
 * matrix position — immune to whatever Vial maps onto these keys. */
#define SCRY_KEY_L_ROW 3
#define SCRY_KEY_L_COL 4
#define SCRY_KEY_R_ROW 7
#define SCRY_KEY_R_COL 4

// Level-sampled scry chord mask (SCRY_M_*) from the current matrix rows.
uint8_t duel_scry_mask_from_rows(const uint16_t rows[DUEL_INPUT_ROWS]);
// Full per-tick input sample: down masks, scry mask, held positions, and the
// per-half physical spell layer.
sim_inputs_t duel_inputs_from_rows(const uint16_t rows[DUEL_INPUT_ROWS]);

/* ---- deterministic tick budget -------------------------------------------
 * How many sim ticks to run this pass. Catch-up replays missed ticks; a
 * stall of DUEL_TICK_CATCHUP_MAX or more (USB suspend) resyncs the deadline
 * instead of replaying unbounded history. Wrap-safe. */
#define DUEL_TICK_CATCHUP_MAX 5u
uint8_t duel_tick_budget(uint32_t *next_tick_ms, uint32_t now_ms, bool *resynced);

#define DUEL_MAILBOX_CAPACITY 32u

typedef struct {
    volatile uint8_t version;
    uint8_t data[DUEL_MAILBOX_CAPACITY];
} duel_mailbox_t;

void duel_mailbox_publish(duel_mailbox_t *mailbox, const void *source, size_t size);
bool duel_mailbox_consume(const duel_mailbox_t *mailbox, uint8_t *seen_version, void *destination,
                          size_t size);
bool duel_mailbox_read_latest(const duel_mailbox_t *mailbox, void *destination, size_t size);

#define DUEL_ACTIVE_TX_MS 80u
#ifdef ARCANE_FIXED_SPLIT_CADENCE
#define DUEL_REPAIR_TX_MS DUEL_ACTIVE_TX_MS
#else
#define DUEL_REPAIR_TX_MS 250u
#endif

typedef struct {
    uint32_t last_sent_ms;
    uint16_t sequence;
    bool have_sent;
} duel_tx_policy_t;

/* Each call is one attempted send and consumes a sequence number. False means
 * the caller must skip packet encoding, CRC, and transport entirely. */
bool duel_tx_attempt(duel_tx_policy_t *policy, uint32_t now_ms, bool urgent, bool fx_changed,
                     bool semantic_changed);
void duel_tx_commit(duel_tx_policy_t *policy, uint32_t started_ms);
bool duel_tx_repair_due(const duel_tx_policy_t *policy, uint32_t now_ms);

#define DUEL_FLOOR_TRANSITION_MS 600u
#define DUEL_FLOOR_PHASE_MS      150u

typedef struct {
    uint8_t target;
    uint8_t source;
    uint32_t started_ms;
    bool initialized;
    bool active;
} duel_floor_policy_t;

bool duel_floor_note_target(duel_floor_policy_t *policy, uint8_t civic, uint32_t now_ms,
                            duel_display_phase_t display_phase);
uint8_t duel_floor_presentation(duel_floor_policy_t *policy, uint32_t now_ms);

typedef struct {
    uint8_t seen_fx_seq;
    uint8_t kind;
    uint8_t spell_kind;
    uint32_t started_ms;
    uint16_t duration_ms;
} duel_flash_policy_t;

bool duel_flash_note(duel_flash_policy_t *policy, uint8_t fx_seq, uint8_t kind, uint8_t spell_kind,
                     uint32_t now_ms);
uint8_t duel_flash_remaining(const duel_flash_policy_t *policy, uint32_t now_ms);

/* One render-side observation pass: caches the last visible style per spell
 * slot, then arms a flash deadline for a new one-shot outcome, scaling it
 * from the DEFENDER's cached spell style. Returns true when a new flash was
 * armed. */
bool duel_flash_observe_view(duel_flash_policy_t *policy, uint8_t last_spell_kind[2],
                             const duel_view_t *view, uint32_t now_ms);

/* Wake grace: a local keypress holds the panel awake (and vetoes following
 * the master into DIM/SLEEP) for this long. */
#define DUEL_WAKE_GRACE_MS 120u
bool duel_wake_grace_active(uint32_t *wake_until_ms, uint32_t now_ms);
bool duel_display_should_follow(uint8_t remote_phase, uint32_t *wake_until_ms, uint32_t now_ms);

enum {
    DUEL_SKY_DAWN = 0,
    DUEL_SKY_DAY,
    DUEL_SKY_DUSK,
    DUEL_SKY_NIGHT,
};

#define DUEL_SKY_CYCLE_MS 1800000u
uint8_t duel_sky_phase(uint32_t session_elapsed_ms);
// v11 celestial arc position within the current phase (0..3): 4 phases x 4
// sub-phases = 16 arc steps per cycle, carried in secondary bits 5-6.
uint8_t duel_sky_subphase(uint32_t session_elapsed_ms);

enum {
    DUEL_DIPLOMACY_RIGHT_ADVANTAGE = 0,
    DUEL_DIPLOMACY_BALANCED = 1,
    DUEL_DIPLOMACY_LEFT_ADVANTAGE = 2,
};

typedef struct {
    int8_t balance; /* -3 right advantage, +3 left advantage */
    uint8_t prior_life[2];
    bool initialized;
} duel_diplomacy_t;

void duel_diplomacy_init(duel_diplomacy_t *state);
bool duel_diplomacy_update(duel_diplomacy_t *state, uint8_t left_life, uint8_t right_life);
uint8_t duel_diplomacy_target(const duel_diplomacy_t *state);

/* ---- master-derived shared civic presentation ----------------------------
 * The visitor is a pure function of the notification summary; the rare-event
 * deck is deterministic from the session seed + civic phase and safety-gated
 * (spec §14.1): suppressed while a critical (sentinel) visitor is stationed
 * or a champion is not standing. A live aftermath temporarily owns both
 * bytes (bit7 of revision discriminates). */

// Wall-clock period of one civic tick: the bounded cadence at which the
// resident and floor advance. ~300 ms keeps each 16-tick action (~4.8 s)
// inside the spec's 3-10 s window while staying far below combat cadence.
#define DUEL_CIVIC_TICK_MS 300u

typedef struct {
    uint8_t shared_pres;
    uint8_t revision;
} duel_civic_shared_t;

duel_civic_shared_t duel_civic_shared_derive(uint8_t session, uint32_t now_ms,
                                             const duel_host_state_t *host,
                                             const sim_world_t *world, int8_t diplomacy_balance);

/* ---- slave presenter ------------------------------------------------------
 * The remote-vs-local-fallback decision for the slave half: render the last
 * accepted snapshot while the link is live, drop to the local
 * non-authoritative sim when it goes stale, and re-acquire cleanly. The
 * stale edge is latched against the flag the render currently carries so a
 * flip is presented exactly once. */
typedef struct {
    bool using_remote;
} duel_slave_presenter_t;

typedef struct {
    bool use_remote;      /* render source this pass */
    bool consider_follow; /* remote path: adopt the master's display phase */
    bool base_refresh;    /* redraw needed before any display-phase change is
                             folded in (caller ORs its own display_changed) */
    bool set_stale;       /* local path: mark the render stale */
} duel_slave_decision_t;

duel_slave_decision_t duel_slave_present(duel_slave_presenter_t *presenter, bool accepted,
                                         bool have_any, bool stale, bool ticked,
                                         bool render_invalid, bool render_stale_flag);
