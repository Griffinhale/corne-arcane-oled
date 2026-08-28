/*
 * Autonomous city life: the spell system with nobody at the keys.
 *
 * The desktop has no input. Rather than leave the champions standing still,
 * this drives the firmware's own simulation with a caster that fabricates its
 * own key positions from a seeded generator. Every position here is invented;
 * none is sampled from a keyboard, a window, or a person. That is the whole
 * privacy story of this file, and it is structural: there is no input to read.
 *
 * The simulation is unmodified. Chains are collected, committed, wound up and
 * launched by the same incantation compiler the keyboard uses, so whatever the
 * city shows is a real state of the real world model.
 */
#include "duel_ambient.h"

#include <string.h>

#include "duel_incantation.h"
#include "duel_model.h"
#include "duel_runtime.h"
#include "duel_sim.h"
#include "duel_view.h"

/* A chain is a burst of taps close enough together to collect as one
 * incantation, then silence long enough to commit it. Both fenceposts are the
 * compiler's, not ours. */
#define AMBIENT_GAP_MIN    3u
#define AMBIENT_GAP_MAX    (INCANTATION_IDLE_COMMIT_TICKS - 3u)
#define AMBIENT_CHAIN_MAX  4u
#define AMBIENT_REST_MIN   30u  /* ~1.2 s */
#define AMBIENT_REST_MAX   190u /* ~7.6 s; long rests let a stance open */
#define AMBIENT_HOLD_TICKS 1u
/* One chain in eight ends on a held key long enough to force a commit, which
 * is the tower-lighting big cast. Rare on purpose: it owns the whole frame. */
#define AMBIENT_BIG_CAST_ODDS  8u
#define AMBIENT_BIG_HOLD_TICKS (INCANTATION_FORCE_COMMIT_TICKS + 6u)

enum { AMBIENT_REST = 0, AMBIENT_PRESS, AMBIENT_HOLD, AMBIENT_GAP };

typedef struct {
    uint16_t timer;
    uint8_t phase;
    uint8_t remaining;
    uint8_t position;
    uint8_t layer;
    bool big;
} ambient_caster_t;

typedef struct {
    sim_world_t world;
    duel_flash_policy_t flash;
    ambient_caster_t caster[2];
    duel_ambient_stats_t stats;
    uint32_t next_tick_ms;
    uint32_t prng;
    uint8_t seed;
    uint8_t last_spell_kind[2];
    uint8_t prior_hp[2];
    uint8_t prior_life[2];
    bool spell_active[2];
    bool started;
} ambient_state_t;

_Static_assert(sizeof(ambient_state_t) <= sizeof(duel_ambient_t),
               "the ambient world no longer fits its opaque handle");

/* xorshift32: deterministic, tiny, and never zero. The city must replay
 * identically from a seed or it cannot be reviewed. */
static uint32_t next_random(ambient_state_t *state) {
    uint32_t x = state->prng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state->prng = x;
    return x;
}

static uint32_t random_between(ambient_state_t *state, uint32_t low, uint32_t high) {
    return low + next_random(state) % (high - low + 1u);
}

static ambient_state_t *mutable_state(duel_ambient_t *ambient) {
    return (ambient_state_t *)(void *)ambient;
}

static const ambient_state_t *readable_state(const duel_ambient_t *ambient) {
    return (const ambient_state_t *)(const void *)ambient;
}

void duel_ambient_init(duel_ambient_t *ambient, uint8_t seed) {
    if (!ambient)
        return;
    ambient_state_t *state = mutable_state(ambient);
    memset(state, 0, sizeof *state);
    state->seed = seed;
    /* Any nonzero word; the seed must not be able to stall the generator. */
    state->prng = 0x9E3779B9u ^ ((uint32_t)seed * 0x01000193u);
    sim_init(&state->world, SIMF_AUTHORITATIVE, 0u);
    for (uint8_t side = 0; side < 2u; side++) {
        state->prior_hp[side] = state->world.wiz[side].hp;
        state->caster[side].timer = (uint16_t)random_between(state, 0u, AMBIENT_REST_MAX);
    }
}

/* Begin a chain: how many taps, where, on which layer, and whether it ends
 * held. Position and layer are invented; the compiler reads them exactly as it
 * reads a physical hand. */
static void begin_chain(ambient_state_t *state, ambient_caster_t *caster) {
    caster->remaining = (uint8_t)random_between(state, 1u, AMBIENT_CHAIN_MAX);
    caster->layer = (uint8_t)(next_random(state) % 4u == 0u ? random_between(state, 1u, 3u) : 0u);
    caster->big = next_random(state) % AMBIENT_BIG_CAST_ODDS == 0u;
    caster->phase = AMBIENT_PRESS;
    caster->timer = 0u;
}

/* One side's contribution to this tick's inputs. Returns the event count. */
static uint8_t step_caster(ambient_state_t *state, uint8_t side, sim_inputs_t *inputs,
                           sim_event_t *events) {
    ambient_caster_t *caster = &state->caster[side];
    const sim_wizard_t *wizard = &state->world.wiz[side];

    /* A champion who is not standing casts nothing; the lifecycle carries the
     * scene until a replacement walks in. */
    if (wizard->life != LIFE_ACTIVE) {
        caster->phase = AMBIENT_REST;
        caster->timer = (uint16_t)random_between(state, AMBIENT_REST_MIN, AMBIENT_REST_MAX);
        return 0u;
    }

    if (caster->timer > 0u) {
        caster->timer--;
        if (caster->phase == AMBIENT_HOLD) {
            inputs->held_pos[side] = 1u << caster->position;
            inputs->down_mask |= (uint8_t)(1u << side);
            inputs->layer[side] = caster->layer;
        }
        return 0u;
    }

    switch (caster->phase) {
        case AMBIENT_REST:
            begin_chain(state, caster);
            /* fall through: the first tap lands on this tick */
            __attribute__((fallthrough));
        case AMBIENT_PRESS: {
            caster->position = (uint8_t)(next_random(state) % 24u);
            inputs->held_pos[side] = 1u << caster->position;
            inputs->down_mask |= (uint8_t)(1u << side);
            inputs->layer[side] = caster->layer;
            events[0] =
                SIM_EV_PACK(SIM_EV_KEYDOWN, side, caster->position / 6u, caster->position % 6u);
            caster->remaining--;
            bool last = caster->remaining == 0u;
            caster->phase = AMBIENT_HOLD;
            caster->timer =
                (uint16_t)(last && caster->big ? AMBIENT_BIG_HOLD_TICKS : AMBIENT_HOLD_TICKS);
            return 1u;
        }
        case AMBIENT_HOLD:
            /* Released. Either another tap follows inside the collection
             * window, or the silence commits what was collected. */
            if (caster->remaining > 0u) {
                caster->phase = AMBIENT_PRESS;
                caster->timer = (uint16_t)random_between(state, AMBIENT_GAP_MIN, AMBIENT_GAP_MAX);
            } else {
                caster->phase = AMBIENT_REST;
                caster->timer = (uint16_t)random_between(state, AMBIENT_REST_MIN, AMBIENT_REST_MAX);
            }
            return 0u;
        default:
            caster->phase = AMBIENT_REST;
            caster->timer = AMBIENT_REST_MIN;
            return 0u;
    }
}

static void note_outcomes(ambient_state_t *state) {
    for (uint8_t side = 0; side < 2u; side++) {
        const sim_wizard_t *wizard = &state->world.wiz[side];
        bool active = state->world.spell[side].active != 0u;
        if (active && !state->spell_active[side])
            state->stats.casts++;
        state->spell_active[side] = active;
        if (wizard->hp < state->prior_hp[side])
            state->stats.impacts += (uint32_t)(state->prior_hp[side] - wizard->hp);
        state->prior_hp[side] = wizard->hp;
        if (wizard->life != state->prior_life[side] && wizard->life == LIFE_COLLAPSE)
            state->stats.knockdowns++;
        state->prior_life[side] = wizard->life;
    }
}

static void run_tick(ambient_state_t *state) {
    sim_inputs_t inputs;
    sim_event_t events[2];
    uint8_t count = 0;
    memset(&inputs, 0, sizeof inputs);
    for (uint8_t side = 0; side < 2u; side++)
        count += step_caster(state, side, &inputs, events + count);
    sim_tick(&state->world, inputs, events, count, 0u);
    state->stats.ticks++;
    note_outcomes(state);
}

uint8_t duel_ambient_advance(duel_ambient_t *ambient, uint32_t now_ms) {
    if (!ambient)
        return 0u;
    ambient_state_t *state = mutable_state(ambient);
    if (!state->started) {
        state->started = true;
        state->next_tick_ms = now_ms;
    }
    bool resynced = false;
    uint8_t budget = duel_tick_budget(&state->next_tick_ms, now_ms, &resynced);
    for (uint8_t i = 0; i < budget; i++)
        run_tick(state);
    return budget;
}

duel_ambient_stats_t duel_ambient_stats(const duel_ambient_t *ambient) {
    duel_ambient_stats_t empty = {0, 0, 0, 0};
    return ambient ? readable_state(ambient)->stats : empty;
}

const sim_world_t *duel_ambient_world(const duel_ambient_t *ambient) {
    return &readable_state(ambient)->world;
}

void duel_ambient_project(duel_ambient_t *ambient, duel_render_t *render, uint32_t now_ms) {
    ambient_state_t *state = mutable_state(ambient);
    duel_render_from_world(render, &state->world);
    /* One-shot outcomes are a presentation deadline, not world state: the same
     * observation pass the master runs before it composes a frame. */
    duel_flash_observe_view(&state->flash, state->last_spell_kind, &render->view, state->seed,
                            now_ms);
    render->flash_frames = duel_flash_remaining(&state->flash, now_ms);
    render->flash_kind = state->flash.kind;
    render->flash_spell_kind = state->flash.spell_kind;
    render->local_ambience = incantation_local_ambience(&state->world.wiz[SIM_SIDE_L]);
}
