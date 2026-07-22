/* World-to-render projection and presentation-only frame state. */
#pragma once

#include <stdint.h>

#include "duel_civic.h"
#include "duel_view.h"

#define DUEL_DECK_Y0      60
#define DUEL_FLOOR_BEAM_Y 61
#define DUEL_FLOOR_Y0     62
#define DUEL_FLOOR_Y1     110
#define DUEL_STONE_Y0     112
#define DUEL_STONE_Y1     116
#define DUEL_TOWER_W      13
#define DUEL_TOWER_PEAK_Y 14
#define DUEL_ROOF_DY      (-17)

typedef struct {
    duel_view_t view;
    uint8_t external;
    uint8_t alert;
    uint8_t layer;
    uint8_t flags;
    uint8_t flash_frames;
    uint8_t flash_kind;
    uint8_t flash_spell_kind;
    uint16_t diag_overflow;
    uint8_t diag_tick;
    uint8_t civic;
    uint8_t secondary;
    uint8_t shared_pres;
    uint8_t revision;
    uint8_t seed;
    uint8_t civic_phase;
    uint8_t floor_transition;
    uint8_t local_ambience;
    uint8_t residue[2];
} duel_render_t;

#define DUEL_RENDER_RESIDUE_ELEMENT(r, zone) \
    ((uint8_t)(((r)->residue[(zone) >> 1] >> (((zone) & 1u) * 4u)) & 3u))
#define DUEL_RENDER_RESIDUE_INTENSITY(r, zone) \
    ((uint8_t)(((r)->residue[(zone) >> 1] >> (((zone) & 1u) * 4u + 2u)) & 3u))
#define DUEL_RENDER_STALE 0x01u
#define DUEL_RENDER_GLOBAL_LAYER(v) ((uint8_t)((v) & 0x03u))
#define DUEL_RENDER_LOCAL_SHIFT 4u
#define DUEL_RENDER_LOCAL_NONE  0u
#define DUEL_RENDER_LOCAL_LEFT  1u
#define DUEL_RENDER_LOCAL_RIGHT 2u
#define DUEL_RENDER_LOCAL_LAYER(v) \
    ((uint8_t)(((v) >> DUEL_RENDER_LOCAL_SHIFT) & 0x03u))
#define DUEL_RENDER_LAYER_PACK(global, local) \
    ((uint8_t)(((global) & 0x03u) | (((local) & 0x03u) << DUEL_RENDER_LOCAL_SHIFT)))

_Static_assert(sizeof(duel_render_t) == 40, "render state layout changed");

void duel_render_from_world(duel_render_t *render, const sim_world_t *world);
uint8_t duel_render_host_online(const duel_render_t *render);
uint8_t duel_render_scene(const duel_render_t *render);
uint8_t duel_render_notification_count(const duel_render_t *render);
