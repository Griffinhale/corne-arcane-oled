/* Canonical transport/render projection of the authoritative duel world. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "duel_sim.h"


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

_Static_assert(sizeof(duel_view_t) == 19, "current canonical view must be exactly 19 bytes");

void duel_view_from_world(const sim_world_t *world, duel_view_t *view);
void duel_view_to_render_world(const duel_view_t *view, sim_world_t *world);
bool duel_view_valid(const duel_view_t *view);
duel_view_wizard_t duel_view_wizard(const duel_view_t *view, uint8_t side);
duel_view_spell_t duel_view_spell(const duel_view_t *view, uint8_t side);

static inline bool duel_view_scry_open(const duel_view_t *view) {
    return (view->outcome_overlay & 0x10u) != 0;
}
