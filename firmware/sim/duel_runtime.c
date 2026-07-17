#include <string.h>

#include "duel_runtime.h"
#include "duel_courier.h"
#include "duel_draw.h"
#include "duel_event.h"

uint8_t duel_scry_mask_from_rows(const uint16_t rows[DUEL_INPUT_ROWS]) {
    uint8_t mask = 0;
    if (rows[SCRY_KEY_L_ROW] & (1u << SCRY_KEY_L_COL)) mask |= SCRY_M_L;
    if (rows[SCRY_KEY_R_ROW] & (1u << SCRY_KEY_R_COL)) mask |= SCRY_M_R;
    // Any key held that is NOT one of the two layer keys disqualifies a chord
    // (this is ordinary layer-3 use) and, once the overlay is up, drives scene
    // selection — so mask it in level-sampled form like the rest of the inputs.
    for (uint8_t r = 0; r < DUEL_INPUT_ROWS; r++) {
        uint16_t row = rows[r];
        if (r == SCRY_KEY_L_ROW) row &= (uint16_t)~(1u << SCRY_KEY_L_COL);
        if (r == SCRY_KEY_R_ROW) row &= (uint16_t)~(1u << SCRY_KEY_R_COL);
        if (row) { mask |= SCRY_M_OTHER; break; }
    }
    return mask;
}

sim_inputs_t duel_inputs_from_rows(const uint16_t rows[DUEL_INPUT_ROWS]) {
    sim_inputs_t in = {0};
    for (uint8_t r = 0; r < DUEL_INPUT_ROWS_PER_HAND; r++) {
        if (rows[r]) { in.down_mask |= 1 << SIM_SIDE_L; break; }
    }
    for (uint8_t r = DUEL_INPUT_ROWS_PER_HAND; r < DUEL_INPUT_ROWS; r++) {
        if (rows[r]) { in.down_mask |= 1 << SIM_SIDE_R; break; }
    }
    in.scry_mask = duel_scry_mask_from_rows(rows);
    bool layer_l = (rows[SCRY_KEY_L_ROW] & (1u << SCRY_KEY_L_COL)) != 0;
    bool layer_r = (rows[SCRY_KEY_R_ROW] & (1u << SCRY_KEY_R_COL)) != 0;
    for (uint8_t side = 0; side < 2; side++) {
        uint8_t row0 = side == SIM_SIDE_L ? 0u : DUEL_INPUT_ROWS_PER_HAND;
        for (uint8_t row = 0; row < DUEL_INPUT_ROWS_PER_HAND; row++) {
            uint16_t held = rows[row0 + row];
            for (uint8_t col = 0; col < DUEL_INPUT_COLS; col++)
                if (held & (1u << col))
                    in.held_pos[side] |= (uint32_t)1u << (row * DUEL_INPUT_COLS + col);
        }
        /* Spell layers are physical per-half ingredients, not the global QMK
         * layer selected for host output. A lone thumb therefore influences
         * only its wizard; the deliberate two-thumb chord is layer 3 for both. */
        in.layer[side] = layer_l && layer_r ? 3u :
                         side == SIM_SIDE_L && layer_l ? 1u :
                         side == SIM_SIDE_R && layer_r ? 2u : 0u;
    }
    return in;
}

uint8_t duel_tick_budget(uint32_t *next_tick_ms, uint32_t now_ms, bool *resynced) {
    uint8_t ticks = 0;
    *resynced = false;
    while ((int32_t)(now_ms - *next_tick_ms) >= 0) { /* wrap-safe "expired" */
        ticks++;
        *next_tick_ms += SIM_TICK_MS;
        if (ticks >= DUEL_TICK_CATCHUP_MAX) {
            // Long stall (USB suspend): resync instead of replaying history.
            *next_tick_ms = now_ms + SIM_TICK_MS;
            *resynced = true;
            break;
        }
    }
    return ticks;
}

#define DUEL_COMPILER_BARRIER() __asm__ volatile("" ::: "memory")

void duel_mailbox_publish(duel_mailbox_t *mailbox, const void *source, size_t size) {
    if (size > DUEL_MAILBOX_CAPACITY) return;
    mailbox->version++;
    DUEL_COMPILER_BARRIER();
    memcpy(mailbox->data, source, size);
    DUEL_COMPILER_BARRIER();
    mailbox->version++;
}

bool duel_mailbox_consume(const duel_mailbox_t *mailbox, uint8_t *seen_version,
                          void *destination, size_t size) {
    if (size > DUEL_MAILBOX_CAPACITY) return false;
    uint8_t first = mailbox->version;
    if (first == *seen_version || (first & 1u)) return false;
    DUEL_COMPILER_BARRIER();
    memcpy(destination, mailbox->data, size);
    DUEL_COMPILER_BARRIER();
    uint8_t second = mailbox->version;
    if (first != second) return false;
    *seen_version = first;
    return true;
}

bool duel_mailbox_read_latest(const duel_mailbox_t *mailbox, void *destination,
                              size_t size) {
    if (size > DUEL_MAILBOX_CAPACITY) return false;
    uint8_t first = mailbox->version;
    if (first & 1u) return false;
    DUEL_COMPILER_BARRIER();
    memcpy(destination, mailbox->data, size);
    DUEL_COMPILER_BARRIER();
    return first == mailbox->version;
}

bool duel_tx_attempt(duel_tx_policy_t *policy, uint32_t now_ms, bool urgent,
                     bool fx_changed, bool semantic_changed) {
    policy->sequence++;
    if (!policy->have_sent) return true;
    uint32_t elapsed = now_ms - policy->last_sent_ms;
    /* Urgency selects semantic cadence; it never violates the hard 80 ms
     * transport floor. This false path is what lets the caller skip encoding
     * and CRC entirely while still consuming the attempted sequence. */
    if (elapsed < DUEL_ACTIVE_TX_MS) return false;
    if (urgent || fx_changed || semantic_changed) return true;
    return elapsed >= DUEL_REPAIR_TX_MS;
}

void duel_tx_commit(duel_tx_policy_t *policy, uint32_t started_ms) {
    policy->last_sent_ms = started_ms;
    policy->have_sent = true;
}

bool duel_tx_repair_due(const duel_tx_policy_t *policy, uint32_t now_ms) {
    return policy->have_sent && now_ms - policy->last_sent_ms >= DUEL_REPAIR_TX_MS;
}

bool duel_floor_note_target(duel_floor_policy_t *policy, uint8_t civic,
                            uint32_t now_ms, duel_display_phase_t display_phase) {
    uint8_t target = DUEL_CIVIC_FLOOR(civic);
    if (!policy->initialized) {
        policy->target = policy->source = target;
        policy->initialized = true;
        return true;
    }
    if (target == policy->target) return false;
    policy->source = policy->target;
    policy->target = target;
    if (display_phase == DUEL_DISPLAY_SLEEP) {
        policy->active = false;
    } else {
        policy->started_ms = now_ms;
        policy->active = true;
    }
    return true;
}

uint8_t duel_floor_presentation(duel_floor_policy_t *policy, uint32_t now_ms) {
    if (policy->active && now_ms - policy->started_ms >= DUEL_FLOOR_TRANSITION_MS)
        policy->active = false;
    uint8_t phase = policy->active
                        ? (uint8_t)((now_ms - policy->started_ms) / DUEL_FLOOR_PHASE_MS)
                        : 0u;
    return INCANTATION_FLOOR_TRANSITION_PACK(policy->source, phase, policy->active);
}

bool duel_flash_note(duel_flash_policy_t *policy, uint8_t fx_seq, uint8_t kind,
                     uint8_t spell_kind, uint32_t now_ms) {
    if (fx_seq == policy->seen_fx_seq) return false;
    policy->seen_fx_seq = fx_seq;
    policy->kind = kind;
    policy->spell_kind = spell_kind;
    policy->started_ms = now_ms;
    policy->duration_ms = (kind == FX_IMPACT_L || kind == FX_IMPACT_R)
                              ? DUEL_PRESENTATION_IMPACT_MS
                              : DUEL_PRESENTATION_OTHER_MS;
    return true;
}

uint8_t duel_flash_remaining(const duel_flash_policy_t *policy, uint32_t now_ms) {
    return duel_presentation_remaining(policy->started_ms, policy->duration_ms, now_ms);
}

bool duel_flash_observe_view(duel_flash_policy_t *policy,
                             uint8_t last_spell_kind[2],
                             const duel_view_t *view, uint32_t now_ms) {
    // Remember the last visible style in each spell slot. Resolution clears
    // the authoritative slot, but its outcome can still scale from this local
    // presentation cache without growing combat state or the wire.
    for (uint8_t s = 0; s < 2; s++) {
        duel_view_spell_t spell = duel_view_spell(view, s);
        if (spell.active) last_spell_kind[s] = spell.kind;
    }
    if (view->fx_seq == policy->seen_fx_seq) return false;
    uint8_t flash_kind = VIEW_OVERLAY_FX(view->outcome_overlay);
    bool defender_left = flash_kind == FX_IMPACT_L || flash_kind == FX_DEFLECT_L ||
                         flash_kind == FX_FIZZLE_L || flash_kind == FX_HEAL_L ||
                         flash_kind == FX_WARD_SHATTER_L;
    // The defender flashes with the style of the spell that reached it — the
    // one cast from the OPPOSITE side's slot.
    return duel_flash_note(policy, view->fx_seq, flash_kind,
                           last_spell_kind[defender_left ? SIM_SIDE_R : SIM_SIDE_L],
                           now_ms);
}

bool duel_wake_grace_active(uint32_t *wake_until_ms, uint32_t now_ms) {
    bool active = *wake_until_ms && (int32_t)(now_ms - *wake_until_ms) < 0;
    if (!active) *wake_until_ms = 0;
    return active;
}

bool duel_display_should_follow(uint8_t remote_phase, uint32_t *wake_until_ms,
                                uint32_t now_ms) {
    return remote_phase <= DUEL_DISPLAY_SLEEP &&
           !duel_wake_grace_active(wake_until_ms, now_ms);
}

uint8_t duel_sky_phase(uint32_t session_elapsed_ms) {
    uint32_t within = session_elapsed_ms % DUEL_SKY_CYCLE_MS;
    if (within < 150000u) return DUEL_SKY_DAWN;
    if (within < 1350000u) return DUEL_SKY_DAY;
    if (within < 1500000u) return DUEL_SKY_DUSK;
    return DUEL_SKY_NIGHT;
}

void duel_diplomacy_init(duel_diplomacy_t *state) {
    memset(state, 0, sizeof *state);
}

bool duel_diplomacy_update(duel_diplomacy_t *state, uint8_t left_life,
                           uint8_t right_life) {
    if (!state->initialized) {
        state->prior_life[SIM_SIDE_L] = left_life;
        state->prior_life[SIM_SIDE_R] = right_life;
        state->initialized = true;
        return false;
    }
    int8_t before = state->balance;
    if (state->prior_life[SIM_SIDE_L] == LIFE_ACTIVE && left_life != LIFE_ACTIVE &&
        state->balance > -3)
        state->balance--;
    if (state->prior_life[SIM_SIDE_R] == LIFE_ACTIVE && right_life != LIFE_ACTIVE &&
        state->balance < 3)
        state->balance++;
    state->prior_life[SIM_SIDE_L] = left_life;
    state->prior_life[SIM_SIDE_R] = right_life;
    return before != state->balance;
}

uint8_t duel_diplomacy_target(const duel_diplomacy_t *state) {
    return state->balance > 0 ? DUEL_DIPLOMACY_LEFT_ADVANTAGE
         : state->balance < 0 ? DUEL_DIPLOMACY_RIGHT_ADVANTAGE
                              : DUEL_DIPLOMACY_BALANCED;
}

duel_civic_shared_t duel_civic_shared_derive(uint8_t session, uint32_t now_ms,
                                             const duel_host_state_t *host,
                                             const sim_world_t *world,
                                             int8_t diplomacy_balance) {
    duel_civic_shared_t out;
    uint8_t ext        = duel_host_context(host);
    uint8_t alr        = duel_host_alert(host);
    uint8_t category   = DUEL_HOST_ALERT_CATEGORY(alr);
    uint8_t count      = DUEL_HOST_CONTEXT_NOTIF(ext);
    uint8_t age        = DUEL_HOST_ALERT_AGE(alr);
    bool    persistent = DUEL_HOST_CONTEXT_PERSISTENT(ext);
    uint8_t phase = (uint8_t)(now_ms / DUEL_CIVIC_TICK_MS);
    bool observatory = DUEL_CIVIC_FLOOR(duel_host_civic(host)) ==
                       DUEL_CIVIC_FLOOR_SPECIAL;
    civic_visitor_state_t vis = civic_visitor_derive(session, phase, category,
                                                     count, age, persistent);
    out.shared_pres = observatory ? 0u : civic_visitor_shared_pres(vis);
    // Rare events are safety-gated (spec §14.1): suppressed while a critical
    // (sentinel) visitor is stationed or a champion is not standing.
    bool eligible = DUEL_VISITOR_KIND(out.shared_pres) != DUEL_CIVIC_COURIER_SENTINEL &&
                    world->wiz[SIM_SIDE_L].life == LIFE_ACTIVE &&
                    world->wiz[SIM_SIDE_R].life == LIFE_ACTIVE;
    out.revision = civic_event_revision(civic_event_derive(
        session, phase, eligible && !observatory, diplomacy_balance));
    /* Lasting spell aftermath temporarily takes precedence over disposable
     * courier/rare-event coordination. The marker bit lets the renderer select
     * the current interpretation; ordinary presentation resumes at expiry. */
    uint8_t aftermath_revision = incantation_aftermath_revision(world);
    if (aftermath_revision & INCANTATION_AFTERMATH_WIRE) {
        out.shared_pres = incantation_aftermath_shared(world);
        out.revision = aftermath_revision;
    }
    return out;
}

duel_slave_decision_t duel_slave_present(duel_slave_presenter_t *presenter,
                                         bool accepted, bool have_any,
                                         bool stale, bool ticked,
                                         bool render_invalid,
                                         bool render_stale_flag) {
    duel_slave_decision_t out = {0};
    bool stale_edge = stale != render_stale_flag;
    if (!stale && have_any) {
        // A fresh acceptance always re-presents; so does re-acquiring the
        // link after a local-fallback stretch (using_remote was false).
        out.use_remote = true;
        out.consider_follow = accepted || !presenter->using_remote;
        out.base_refresh = accepted || !presenter->using_remote || stale_edge ||
                           render_invalid;
        presenter->using_remote = true;
    } else {
        // Local pose-only fallback: never authoritative, never combat.
        out.base_refresh = ticked || presenter->using_remote || stale_edge ||
                           render_invalid;
        out.set_stale = stale;
        presenter->using_remote = false;
    }
    return out;
}
