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

uint8_t sim_evq_drain(sim_evq_t *q, sim_event_t *out) {
    uint8_t n = q->n;
    memcpy(out, q->ev, (size_t)n * sizeof *out);
    if (q->dropped) {
        out[n++] = (sim_event_t){SIM_EV_OVERFLOW, 0, q->dropped, 0};
    }
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

static void wiz_step(sim_wizard_t *wz, bool down, bool was_down) {
    bool rising = down && !was_down;
    switch (wz->pose) {
        case POSE_IDLE:
            if (rising) {
                wz->pose       = POSE_CAST;
                wz->pose_ticks = SIM_CAST_TICKS;
            }
            break;
        case POSE_CAST:
            if (down) {
                wz->pose_ticks = SIM_CAST_TICKS; // held key sustains the pose
            } else if (--wz->pose_ticks == 0) {
                wz->pose       = POSE_RECOVER;
                wz->pose_ticks = SIM_RECOVER_TICKS;
            }
            break;
        case POSE_RECOVER:
            if (rising) {
                wz->pose       = POSE_CAST;
                wz->pose_ticks = SIM_CAST_TICKS;
            } else if (--wz->pose_ticks == 0) {
                wz->pose = POSE_IDLE;
            }
            break;
    }
}

static uint8_t element_of_row(uint8_t row_class) {
    switch (row_class) {
        case 0: return ELEM_FROST;
        case 1: return ELEM_FORCE;
        case 2: return ELEM_EMBER;
        default: return ELEM_VOID;
    }
}

static int8_t speed_of(uint8_t modifier) {
    switch (modifier) {
        case MOD_SWIFT: return 6;
        case MOD_HEAVY: return 3;
        default: return SIM_SPELL_SPEED;
    }
}

static uint8_t recipe_compile(const sim_wizard_t *wz) {
    uint8_t n = wz->recipe_n < 4 ? wz->recipe_n : 4;
    if (n == 0) return DUEL_KIND_PACK(ELEM_FORCE, MOD_NONE, PAY_IMPACT);

    uint8_t counts[4] = {0};
    for (uint8_t i = 0; i < n; i++) {
        uint8_t rc = (uint8_t)((wz->recipe_hist >> (2 * i)) & 3);
        counts[rc]++;
    }

    uint8_t winning_rc = 0;
    uint8_t max_count  = 0;
    for (uint8_t i = 0; i < n; i++) {
        uint8_t rc = (uint8_t)((wz->recipe_hist >> (2 * i)) & 3);
        if (counts[rc] > max_count) {
            winning_rc = rc;
            max_count  = counts[rc];
        }
    }

    // Modifier from the pattern of the last min(n,4) row classes (newest in
    // bits0-1): all identical -> HEAVY (hammering one row); strictly
    // alternating, no two adjacent equal -> SWIFT (rolling across rows); any
    // other mix (or a single ingredient) -> NONE. Timing-independent, so it is
    // deterministic regardless of tick cadence.
    uint8_t modifier = MOD_NONE;
    if (wz->recipe_n >= 2) {
        bool all_same = true, alternating = true;
        for (uint8_t i = 1; i < n; i++) {
            uint8_t cur  = (uint8_t)((wz->recipe_hist >> (2 * i)) & 3);
            uint8_t prev = (uint8_t)((wz->recipe_hist >> (2 * (i - 1))) & 3);
            if (cur != prev) all_same = false;
            else alternating = false;
        }
        if (all_same) modifier = MOD_HEAVY;
        else if (alternating) modifier = MOD_SWIFT;
    }
    return DUEL_KIND_PACK(element_of_row(winning_rc), modifier, PAY_IMPACT);
}

static void spell_outcome(sim_world_t *w, sim_spell_t *sp, int defender, bool deflected) {
    sp->active = 0;
    if (deflected) {
        w->fx_kind = defender == SIM_SIDE_L ? FX_DEFLECT_L : FX_DEFLECT_R;
    } else {
        w->fx_kind = defender == SIM_SIDE_L ? FX_IMPACT_L : FX_IMPACT_R;
        if (w->wiz[defender].hp) w->wiz[defender].hp--;
        w->wiz[defender].regen_ticks = SIM_REGEN_TICKS; // a hit resets the regen clock
        if (w->wiz[defender].hp == 0) {
            /* Felled: begin the lifecycle arc. No dead ends — every phase has a
               positive timer and the chain always returns to ACTIVE at full hp. */
            sim_wizard_t *d = &w->wiz[defender];
            d->life       = LIFE_COLLAPSE;
            d->life_ticks = SIM_COLLAPSE_TICKS;
            d->pose       = POSE_IDLE;
            d->pose_ticks = 0;
            d->shield_ticks  = 0;
            d->cast_windup   = 0; // cancel a pending cast: nothing spawns from a corpse
            d->cast_cooldown = 0; // the replacement may cast immediately
        }
    }
    w->fx_seq++;
}

// A spell reaching a non-ACTIVE defender's doorstep dissipates harmlessly:
// no hp change, no lifecycle perturbation, just the one-shot fx.
static void spell_fizzle(sim_world_t *w, sim_spell_t *sp, int defender) {
    sp->active = 0;
    w->fx_kind = defender == SIM_SIDE_L ? FX_FIZZLE_L : FX_FIZZLE_R;
    w->fx_seq++;
}

// M7 layer-key chord machine. Pure level logic on scry_mask (see duel_sim.h),
// so a dropped key edge can never wedge it. Authoritative-only: the caller
// runs this on the master, whose merged matrix owns both layer-key positions.
static void scry_step(sim_scry_t *sc, uint8_t mask) {
    bool l     = mask & SCRY_M_L;
    bool r     = mask & SCRY_M_R;
    bool both  = l && r;
    bool any   = l || r;
    bool other = mask & SCRY_M_OTHER;

    switch (sc->state) {
        case SCRY_IDLE:
            if (both && !other) { sc->state = SCRY_PENDING; sc->timer = SCRY_PENDING_TICKS; }
            else if (any)       { sc->state = SCRY_FIRST_HELD; }
            break;
        case SCRY_FIRST_HELD:
            if (!any)                { sc->state = SCRY_IDLE; }
            else if (both && !other) { sc->state = SCRY_PENDING; sc->timer = SCRY_PENDING_TICKS; }
            // one key + other keys = an ordinary layer roll: stays here, never opens.
            break;
        case SCRY_PENDING:
            if (other)                 sc->state = SCRY_CANCELLED;           // real layer-3 use
            else if (!both)            sc->state = any ? SCRY_FIRST_HELD : SCRY_IDLE; // let go early
            else if (--sc->timer == 0) sc->state = SCRY_ACTIVE;             // held still -> open
            break;
        case SCRY_ACTIVE:
            if (!both)      sc->state = any ? SCRY_FIRST_HELD : SCRY_IDLE;   // release closes it
            else if (other) { sc->state = SCRY_SELECT; sc->scene = (uint8_t)((sc->scene + 1) % SCRY_SCENES); }
            break;
        case SCRY_SELECT:
            if (!both)       sc->state = any ? SCRY_FIRST_HELD : SCRY_IDLE;
            else if (!other) sc->state = SCRY_ACTIVE;                        // selector released
            break;
        default: // SCRY_CANCELLED: latched until a full release, so no flicker
            if (!any) sc->state = SCRY_IDLE;
            break;
    }
}

void sim_tick(sim_world_t *w, sim_inputs_t in, const sim_event_t *ev, uint8_t n) {
    for (uint8_t i = 0; i < n; i++) {
        if (ev[i].kind == SIM_EV_OVERFLOW) {
            uint32_t sum = (uint32_t)w->overflow_count + ev[i].row;
            w->overflow_count = sum > 0xFFFF ? 0xFFFF : (uint16_t)sum;
        } else if (ev[i].kind == SIM_EV_KEYDOWN && ev[i].side < 2) {
            if (w->flags & SIMF_AUTHORITATIVE) {
                sim_wizard_t *wz = &w->wiz[ev[i].side];
                uint8_t       rc = ev[i].row & 3;
                wz->recipe_hist = (uint8_t)((wz->recipe_hist << 2) | rc);
                if (wz->recipe_n < RECIPE_N_MAX) wz->recipe_n++;
                wz->recipe_idle = 0;
            }
            if (w->wiz[ev[i].side].life == LIFE_ACTIVE) {
                w->wiz[ev[i].side].shield_ticks = SIM_SHIELD_TICKS; // a downed wizard cannot ward
            }
        }
        // KEYUP does not feed recipes.
    }

    // Lifecycle arc: fixed timers only, so no input is ever needed to get a
    // wizard back on its feet. Slave worlds never transition (render-only).
    if (w->flags & SIMF_AUTHORITATIVE) {
        for (int s = 0; s < 2; s++) {
            sim_wizard_t *wz = &w->wiz[s];
            if (wz->life == LIFE_ACTIVE) continue;
            if (--wz->life_ticks != 0) continue;
            switch (wz->life) {
                case LIFE_COLLAPSE:
                    wz->life       = LIFE_DOWNED;
                    wz->life_ticks = SIM_DOWNED_TICKS;
                    break;
                case LIFE_DOWNED:
                    wz->life       = LIFE_MEDIC;
                    wz->life_ticks = SIM_MEDIC_TICKS;
                    break;
                case LIFE_MEDIC:
                    // Variant bumps on ENTERING replace so the walk-in shows the new look.
                    wz->life       = LIFE_REPLACE;
                    wz->life_ticks = SIM_REPLACE_TICKS;
                    wz->variant    = (uint8_t)((wz->variant + 1) % SIM_ROSTER_N);
                    break;
                default: // REPLACE done: back at full strength
                    wz->life        = LIFE_ACTIVE;
                    wz->hp          = SIM_MAX_HP;
                    wz->regen_ticks = SIM_REGEN_TICKS;
                    break;
            }
        }
    }

    // Spells move BEFORE casts spawn, so a fresh spell is visible at its
    // spawn position for one tick instead of jumping a step immediately.
    if (w->flags & SIMF_AUTHORITATIVE) {
        for (int s = 0; s < 2; s++) {
            sim_spell_t *sp = &w->spell[s];
            if (!sp->active) continue;
            sp->pos = (uint8_t)(sp->pos + sp->dir);
            if (sp->dir > 0) { // flying right, defender is the right wizard
                if (w->wiz[SIM_SIDE_R].life != LIFE_ACTIVE) {
                    if (sp->pos >= SIM_DOORSTEP_R) spell_fizzle(w, sp, SIM_SIDE_R);
                } else if (sp->pos >= SIM_IMPACT_R) {
                    spell_outcome(w, sp, SIM_SIDE_R, false);
                } else if (sp->pos >= SIM_DOORSTEP_R && w->wiz[SIM_SIDE_R].shield_ticks &&
                           DUEL_KIND_ELEMENT(sp->kind) != ELEM_VOID) {
                    spell_outcome(w, sp, SIM_SIDE_R, true); // VOID pierces the ward
                }
            } else {
                if (w->wiz[SIM_SIDE_L].life != LIFE_ACTIVE) {
                    if (sp->pos <= SIM_DOORSTEP_L) spell_fizzle(w, sp, SIM_SIDE_L);
                } else if (sp->pos <= SIM_IMPACT_L) {
                    spell_outcome(w, sp, SIM_SIDE_L, false);
                } else if (sp->pos <= SIM_DOORSTEP_L && w->wiz[SIM_SIDE_L].shield_ticks &&
                           DUEL_KIND_ELEMENT(sp->kind) != ELEM_VOID) {
                    spell_outcome(w, sp, SIM_SIDE_L, true); // VOID pierces the ward
                }
            }
        }
    }

    for (int s = 0; s < 2; s++) {
        sim_wizard_t *wz = &w->wiz[s];
        // No poses, casts or windup while down; cooldown was zeroed at
        // collapse, so skipping its decrement here is harmless. A key held
        // across the whole downtime still can't auto-cast at respawn:
        // prev_down_mask keeps the bit set, so there is no rising edge.
        if (wz->life != LIFE_ACTIVE) continue;
        bool          down   = (in.down_mask >> s) & 1;
        bool          was    = (w->prev_down_mask >> s) & 1;
        bool          rising = down && !was;

        wiz_step(wz, down, was);

        if (rising && !w->spell[s].active && wz->cast_cooldown == 0 && wz->cast_windup == 0) {
            wz->cast_windup = SIM_CAST_WINDUP_TICKS;
        } else if (wz->cast_windup && --wz->cast_windup == 0) {
            if (w->flags & SIMF_AUTHORITATIVE) {
                uint8_t kind = recipe_compile(wz);
                int8_t  spd  = speed_of(DUEL_KIND_MODIFIER(kind));
                w->spell[s] = (sim_spell_t){
                    .active = 1,
                    .pos    = s == SIM_SIDE_L ? SIM_SPAWN_L : SIM_SPAWN_R,
                    .dir    = s == SIM_SIDE_L ? spd : (int8_t)-spd,
                    .kind   = kind,
                };
                wz->recipe_hist = 0;
                wz->recipe_n    = 0;
                wz->recipe_idle = 0;
            }
            wz->cast_cooldown = SIM_CAST_COOLDOWN;
        }
        if (wz->cast_cooldown) wz->cast_cooldown--;
    }

    // Regen: a pip returns exactly SIM_REGEN_TICKS after the last reset
    // (impact / previous regen / respawn), so the countdown never underflows.
    // Authoritative only — the slave's world must be structurally unable to
    // change hp.
    if (w->flags & SIMF_AUTHORITATIVE) {
        for (int s = 0; s < 2; s++) {
            sim_wizard_t *wz = &w->wiz[s];
            if (wz->life != LIFE_ACTIVE || wz->hp >= SIM_MAX_HP) continue;
            if (--wz->regen_ticks == 0) {
                wz->hp++;
                wz->regen_ticks = SIM_REGEN_TICKS;
            }
        }
    }

    // Recipe clocks are authoritative world state. Inactivity discards an
    // unconsumed burst so a later cast starts from a clean accumulator.
    if (w->flags & SIMF_AUTHORITATIVE) {
        for (int s = 0; s < 2; s++) {
            sim_wizard_t *wz = &w->wiz[s];
            if (wz->recipe_n == 0) continue;
            if (wz->recipe_idle < 0xFF) wz->recipe_idle++;
            if (wz->recipe_idle >= RECIPE_EXPIRE_TICKS) {
                wz->recipe_hist = 0;
                wz->recipe_n    = 0;
                wz->recipe_idle = 0;
            }
        }
    }

    // Shields decay AFTER resolution so a same-tick keydown still deflects.
    for (int s = 0; s < 2; s++) {
        if (w->wiz[s].shield_ticks) w->wiz[s].shield_ticks--;
    }

    // Layer-key overlay chord (M7). Authoritative-only, so the slave's world
    // can never open the overlay on its own — it renders scry state from the
    // snapshot exactly like every other outcome.
    if (w->flags & SIMF_AUTHORITATIVE) {
        scry_step(&w->scry, in.scry_mask);
    }

    w->prev_down_mask = in.down_mask;
    w->tick++;
}
