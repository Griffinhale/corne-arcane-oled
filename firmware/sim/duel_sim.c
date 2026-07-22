#include <string.h>

#include "duel_sim_internal.h"

bool sim_evq_push(sim_evq_t *q, sim_event_t e) {
    if (q->n >= SIM_EVQ_CAP) {
        if (q->dropped < 0xFF) q->dropped++;
        return false;
    }
    q->ev[q->n++] = e;
    return true;
}

void sim_evq_reset(sim_evq_t *q) {
    q->n       = 0;
    q->dropped = 0;
}

void sim_init(sim_world_t *w, uint8_t flags, uint32_t start_tick) {
    memset(w, 0, sizeof *w);
    w->tick      = start_tick;
    w->flags     = flags;
    w->wiz[0].hp = SIM_MAX_HP;
    w->wiz[1].hp = SIM_MAX_HP;
    w->wiz[0].regen_ticks = SIM_REGEN_TICKS;
    w->wiz[1].regen_ticks = SIM_REGEN_TICKS;
    w->wiz[0].temper = SIM_TEMPER_NEUTRAL;
    w->wiz[1].temper = SIM_TEMPER_NEUTRAL;
}

void sim_tick(sim_world_t *w, sim_inputs_t in, const sim_event_t *ev, uint8_t n,
              uint8_t dropped) {
    if (dropped) {
        uint32_t sum = (uint32_t)w->overflow_count + dropped;
        w->overflow_count = sum > 0xffffu ? 0xffffu : (uint16_t)sum;
    }

    uint32_t event_down[2] = {0, 0};
    if (w->flags & SIMF_AUTHORITATIVE)
        duel_combat_ingest_events(w, in, ev, n, event_down);

    for (uint8_t side = 0; side < 2; side++) {
        sim_wizard_t *wizard = &w->wiz[side];
        uint32_t held = in.held_pos[side] & 0x00ffffffu;
        if (w->flags & SIMF_AUTHORITATIVE)
            duel_combat_collect_side(w, in, side, event_down[side]);
        duel_combat_pose_step(wizard, (in.down_mask & (1u << side)) != 0,
                              (w->prev_down_mask & (1u << side)) != 0);
        wizard->prev_held = held;
    }

    if (w->flags & SIMF_AUTHORITATIVE) {
        for (uint8_t side = 0; side < 2; side++)
            duel_combat_lifecycle_step(&w->wiz[side]);
        for (uint8_t side = 0; side < 2; side++) duel_combat_stance_step(w, side);
        duel_combat_regeneration_step(w);
        duel_combat_collision_step(w);
        duel_combat_residue_step(w);
        duel_combat_spell_step(w, SIM_SIDE_L);
        duel_combat_spell_step(w, SIM_SIDE_R);
        duel_combat_status_release_step(w);
        duel_combat_aftermath_step(w);
        duel_combat_scry_step(&w->scry, in.scry_mask);
    }

    w->prev_down_mask = in.down_mask;
    w->tick++;
}
