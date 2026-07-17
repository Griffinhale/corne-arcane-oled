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

/* Packed byte layout, stated once and consumed by the packer
 * (duel_view_from_world), unpacker (duel_view_wizard), and validator
 * (duel_view_valid) alike so the three can never drift apart.
 *   wizard[0]: hp[0:3] ward_strength[4:6] rearm_lock[7]
 *   wizard[1]: life[0:2] variant[3:4] status[5:7]
 *   wizard[2]: pose[0:1] inc_state[2:4] ward_focus[5:6] prepared[7]
 *   outcome_overlay: fx_kind[0:3] scry_open[4] scry_scene[5:6] reserved[7]
 *   phase (during WINDUP/PREPARED): form[0:2] element[3:4] progress[5:7] */
#define VIEW_W0_PACK(hp, ward, rearm) \
    ((uint8_t)(((hp) & 0x0fu) | (((ward) & 7u) << 4) | ((rearm) ? 0x80u : 0u)))
#define VIEW_W0_HP(b)    ((uint8_t)((b) & 0x0fu))
#define VIEW_W0_WARD(b)  ((uint8_t)(((b) >> 4) & 7u))
#define VIEW_W0_REARM(b) ((uint8_t)(((b) >> 7) & 1u))

#define VIEW_W1_PACK(life, variant, status) \
    ((uint8_t)(((life) & 7u) | (((variant) & 3u) << 3) | (((status) & 7u) << 5)))
#define VIEW_W1_LIFE(b)    ((uint8_t)((b) & 7u))
#define VIEW_W1_VARIANT(b) ((uint8_t)(((b) >> 3) & 3u))
#define VIEW_W1_STATUS(b)  ((uint8_t)(((b) >> 5) & 7u))

#define VIEW_W2_PACK(pose, inc_state, focus, prepared) \
    ((uint8_t)(((pose) & 3u) | (((inc_state) & 7u) << 2) | \
               (((focus) & 3u) << 5) | ((prepared) ? 0x80u : 0u)))
#define VIEW_W2_POSE(b)     ((uint8_t)((b) & 3u))
#define VIEW_W2_INC(b)      ((uint8_t)(((b) >> 2) & 7u))
#define VIEW_W2_FOCUS(b)    ((uint8_t)(((b) >> 5) & 3u))
#define VIEW_W2_PREPARED(b) ((uint8_t)(((b) >> 7) & 1u))

#define VIEW_OVERLAY_PACK(fx, open, scene) \
    ((uint8_t)(((fx) & 0x0fu) | ((open) ? 0x10u : 0u) | (((scene) & 3u) << 5)))
#define VIEW_OVERLAY_FX(b)    ((uint8_t)((b) & 0x0fu))
#define VIEW_OVERLAY_OPEN(b)  (((b) & 0x10u) != 0)
#define VIEW_OVERLAY_SCENE(b) ((uint8_t)(((b) >> 5) & 3u))

#define VIEW_PHASE_PACK(form, elem, progress) \
    ((uint8_t)(((form) & 7u) | (((elem) & 3u) << 3) | (((progress) & 7u) << 5)))
#define VIEW_PHASE_FORM(b)     ((uint8_t)((b) & 7u))
#define VIEW_PHASE_ELEMENT(b)  ((uint8_t)(((b) >> 3) & 3u))
#define VIEW_PHASE_PROGRESS(b) ((uint8_t)(((b) >> 5) & 7u))

typedef struct {
    uint8_t pose;
    uint8_t hp;
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
    return VIEW_OVERLAY_OPEN(view->outcome_overlay);
}
