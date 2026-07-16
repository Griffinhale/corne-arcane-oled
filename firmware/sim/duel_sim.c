#include <string.h>

#include "duel_sim.h"

bool sim_evq_push(sim_evq_t *q, sim_event_t e) {
    if (q->n >= SIM_EVQ_CAP) {
        if (q->dropped < 0xFF) q->dropped++;
        return false;
    }
    q->ev[q->n++] = e;
    return true;
}

uint8_t sim_evq_drain(sim_evq_t *q, sim_event_t *out, uint8_t *dropped) {
    uint8_t n = q->n;
    memcpy(out, q->ev, (size_t)n * sizeof *out);
    *dropped = q->dropped;
    q->n       = 0;
    q->dropped = 0;
    return n;
}

void sim_init(sim_world_t *w, uint8_t flags, uint32_t start_tick) {
    memset(w, 0, sizeof *w);
    w->tick      = start_tick;
    w->flags     = flags;
    w->wiz[0].hp = SIM_MAX_HP;
    w->wiz[1].hp = SIM_MAX_HP;
    w->wiz[0].regen_ticks = SIM_REGEN_TICKS;
    w->wiz[1].regen_ticks = SIM_REGEN_TICKS;
}
