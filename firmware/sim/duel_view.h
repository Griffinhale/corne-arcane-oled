/* Canonical transport/render projection of the authoritative duel world. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "duel_sim.h"

#ifdef ARCANE_M13

/* Exactly 19 bytes: 2x3 wizard, 2x4 active spell, outcome sequence,
   outcome/overlay, 2 phase bytes, and one status-visual byte. */
typedef struct __attribute__((packed)) {
    uint8_t wizard[2][3];
    uint8_t spell[2][4];
    uint8_t fx_seq;
    uint8_t outcome_overlay;
    uint8_t phase[2];
    uint8_t status_visual;
} duel_view_t;

typedef struct {
    uint8_t pose;
    uint8_t pose_ticks;
    uint8_t hp;
    uint8_t shield_ticks;
    uint8_t life;
    uint8_t life_ticks;
    uint8_t variant;
    uint8_t cast_windup;
    uint8_t cast_tier;
    uint8_t inc_state;
    uint8_t ward_strength;
    uint8_t ward_focus;
    uint8_t prepared;
    uint8_t rearm_lock;
    uint8_t status;
    uint8_t status_intensity;
    uint8_t status_duration;
} duel_view_wizard_t;

typedef struct {
    uint8_t active;
    uint8_t pos;
    int8_t  dir;
    uint8_t kind;
    uint32_t descriptor;
    uint8_t progress;
} duel_view_spell_t;

_Static_assert(sizeof(duel_view_t) == 19, "M13 canonical view must be exactly 19 bytes");

void duel_view_from_world(const sim_world_t *world, duel_view_t *view);
void duel_view_to_render_world(const duel_view_t *view, sim_world_t *world);
bool duel_view_valid(const duel_view_t *view);
duel_view_wizard_t duel_view_wizard(const duel_view_t *view, uint8_t side);
duel_view_spell_t duel_view_spell(const duel_view_t *view, uint8_t side);

static inline bool duel_view_scry_open(const duel_view_t *view) {
    return (view->outcome_overlay & 0x10u) != 0;
}

#else /* ARCANE_M13 */

#define DUEL_POSE_PACK(pose, ticks) ((uint8_t)(((pose) & 0x03u) | (((ticks) & 0x3Fu) << 2)))
#define DUEL_POSE_STATE(value)      ((uint8_t)((value) & 0x03u))
#define DUEL_POSE_TICKS(value)      ((uint8_t)((value) >> 2))

#define DUEL_HP_PAIR(left, right) ((uint8_t)(((left) & 0x07u) | (((right) & 0x07u) << 3)))
#define DUEL_HP(value, side)      ((uint8_t)(((value) >> (3u * (side))) & 0x07u))

#define DUEL_SHIELD_PAIR(left, right) ((uint8_t)(((left) & 0x0Fu) | (((right) & 0x0Fu) << 4)))
#define DUEL_SHIELD(value, side)      ((uint8_t)(((value) >> (4u * (side))) & 0x0Fu))

#define DUEL_SPELLSTATE_ACTIVE(slot) (1u << (slot))
#define DUEL_SPELLSTATE_NEG(slot)    (1u << (2u + (slot)))

#define DUEL_LIFE_PACK(life, variant) ((uint8_t)(((life) & 0x07u) | (((variant) & 0x07u) << 3)))
#define DUEL_LIFE_STATE(value)        ((uint8_t)((value) & 0x07u))
#define DUEL_LIFE_VARIANT(value)      ((uint8_t)(((value) >> 3) & 0x07u))

#define DUEL_CHARGE_PACK(windup, tier) ((uint8_t)(((windup) & 0x0Fu) | (((tier) & 0x03u) << 4)))
#define DUEL_CHARGE_WINDUP(value)      ((uint8_t)((value) & 0x0Fu))
#define DUEL_CHARGE_TIER(value)        ((uint8_t)(((value) >> 4) & 0x03u))

#define DUEL_SCRY_PACK(open, scene) ((uint8_t)(((open) ? 1u : 0u) | (((scene) & 0x03u) << 1)))
#define DUEL_SCRY_OPEN(value)       ((uint8_t)((value) & 0x01u))
#define DUEL_SCRY_SCENE(value)      ((uint8_t)(((value) >> 1) & 0x03u))

typedef struct {
    uint8_t pose[2];
    uint8_t hp_pair;
    uint8_t shield_pair;
    uint8_t spell_pos[2];
    uint8_t spell_state;
    uint8_t fx_seq;
    uint8_t fx_kind;
    uint8_t life[2];
    uint8_t life_ticks[2];
    uint8_t spell_kind[2];
    uint8_t charge[2];
    uint8_t scry;
} duel_view_t;

typedef struct {
    uint8_t pose;
    uint8_t pose_ticks;
    uint8_t hp;
    uint8_t shield_ticks;
    uint8_t life;
    uint8_t life_ticks;
    uint8_t variant;
    uint8_t cast_windup;
    uint8_t cast_tier;
} duel_view_wizard_t;

typedef struct {
    uint8_t active;
    uint8_t pos;
    int8_t  dir;
    uint8_t kind;
} duel_view_spell_t;

_Static_assert(sizeof(duel_view_t) == 18, "duel_view_t must remain the canonical 18-byte view");
_Static_assert(SIM_CAST_WINDUP_TICKS <= 15, "wind-up must fit the 4-bit view field");
_Static_assert(SIM_ROSTER_N <= 8, "roster variant must fit the 3-bit view field");
_Static_assert(SCRY_SCENES <= 4, "scene selector must fit the 2-bit view field");

void duel_view_from_world(const sim_world_t *world, duel_view_t *view);
void duel_view_to_render_world(const duel_view_t *view, sim_world_t *world);
bool duel_view_valid(const duel_view_t *view);

static inline duel_view_wizard_t duel_view_wizard(const duel_view_t *view, uint8_t side) {
    duel_view_wizard_t wizard = {
        .pose = DUEL_POSE_STATE(view->pose[side]),
        .pose_ticks = DUEL_POSE_TICKS(view->pose[side]),
        .hp = DUEL_HP(view->hp_pair, side),
        .shield_ticks = DUEL_SHIELD(view->shield_pair, side),
        .life = DUEL_LIFE_STATE(view->life[side]),
        .life_ticks = view->life_ticks[side],
        .variant = DUEL_LIFE_VARIANT(view->life[side]),
        .cast_windup = DUEL_CHARGE_WINDUP(view->charge[side]),
        .cast_tier = DUEL_CHARGE_TIER(view->charge[side]),
    };
    return wizard;
}

static inline duel_view_spell_t duel_view_spell(const duel_view_t *view, uint8_t side) {
    duel_view_spell_t spell = {
        .active = (view->spell_state & DUEL_SPELLSTATE_ACTIVE(side)) != 0,
        .pos = view->spell_pos[side],
        .dir = (view->spell_state & DUEL_SPELLSTATE_NEG(side)) ? -4 : 4,
        .kind = view->spell_kind[side],
    };
    return spell;
}

static inline bool duel_view_scry_open(const duel_view_t *view) {
    return DUEL_SCRY_OPEN(view->scry) != 0;
}

#endif /* ARCANE_M13 */
