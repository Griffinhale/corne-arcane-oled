/*
 * runner.h — drives a sim_world_t from a trace exactly the way the firmware
 * glue does: per tick, deliver due edge events through a sim_evq_t and sample
 * key levels (down_mask) from the tracked key states.
 */
#pragma once

#include <string.h>

#include "duel_sim.h"
#include "trace.h"

typedef struct {
    sim_world_t    w;
    bool           down[2][4][6];
    const trace_t *t;
    int            ev_idx;
    uint32_t       ticks_run;
} runner_t;

static inline void runner_init(runner_t *r, const trace_t *t, uint8_t flags) {
    memset(r, 0, sizeof *r);
    r->t = t;
    sim_init(&r->w, flags, t->start_tick);
}

static inline bool runner_done(const runner_t *r) {
    // unsigned diff is wrap-safe (end_tick = start_tick + relative length)
    return r->ticks_run >= (uint32_t)(r->t->end_tick - r->t->start_tick);
}

static inline void runner_step(runner_t *r) {
    sim_evq_t   q = {0};
    sim_event_t evs[SIM_EVQ_CAP + 1];

    while (r->ev_idx < r->t->n_ev && r->t->ev[r->ev_idx].tick == r->w.tick) {
        const trace_ev_t *te = &r->t->ev[r->ev_idx++];
        sim_evq_push(&q, (sim_event_t){te->kind, te->side, te->row, te->col});
        r->down[te->side][te->row][te->col] = (te->kind == TRACE_EV_PRESS);
    }
    uint8_t n = sim_evq_drain(&q, evs);

    sim_inputs_t in = {0};
    for (int s = 0; s < 2; s++) {
        for (int row = 0; row < 4 && !((in.down_mask >> s) & 1); row++) {
            for (int col = 0; col < 6; col++) {
                if (r->down[s][row][col]) {
                    in.down_mask |= (uint8_t)(1 << s);
                    break;
                }
            }
        }
    }
    // scry_mask mirrors keymap.c's physical-position sampling: the two layer
    // thumbs live at per-hand row 3, col 4 (matrix rows 3 and 7); anything else
    // held is the "other" bit.
    if (r->down[SIM_SIDE_L][3][4]) in.scry_mask |= SCRY_M_L;
    if (r->down[SIM_SIDE_R][3][4]) in.scry_mask |= SCRY_M_R;
    for (int s = 0; s < 2 && !(in.scry_mask & SCRY_M_OTHER); s++) {
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 6; col++) {
                if (r->down[s][row][col] && !(row == 3 && col == 4)) {
                    in.scry_mask |= SCRY_M_OTHER;
                    break;
                }
            }
        }
    }
    sim_tick(&r->w, in, evs, n);
    r->ticks_run++;
}
