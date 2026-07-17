/*
 * duel_event.c — rare-event deck (presentation-only).
 *
 * A deterministic, safety-gated weighted deck over six families (four LOCAL to
 * one city, two SHARED across the desk gap / sky). The master advances the deck
 * one civic phase at a time and packs (id/phase/target) into the snapshot's
 * revision byte via DUEL_EVENT_PACK; both halves decode and render locally. The
 * engine keys strictly off (seed, civic phase) — never w.tick — and allocates
 * nothing, so mechanics and power discipline are untouched.
 */
#include "duel_event.h"

#include "duel_host.h"
#include "duel_resident.h"

/* ------------------------------- deck -------------------------------------
 * One rare-event CYCLE spans CIVIC_EVENT_PERIOD civic phases. The family is
 * chosen once per cycle (keyed by seed + cycle index) with a back-to-back
 * cooldown so no family repeats in adjacent cycles; the sub-phase within the
 * cycle drives the ARMED->ACTIVE->RESOLVING->COOLDOWN progression. Weighting:
 * at balance zero the deck is ~75% local / 25% shared (24 : 8); session
 * imbalance raises only the diplomatic weight, by two per advantage point. */
#define CIVIC_EVENT_PERIOD 8u

// Base weights in DUEL_CIVIC_EVENT_* order (NONE first). Locals 1-4 sum to 24;
// shareds 5-6 sum to 8 before the diplomacy adjustment.
static const uint8_t ev_weights[DUEL_CIVIC_EVENT_COUNT] = {
    0, /* NONE                        */
    6, /* RUNAWAY_SCROLL   (local)    */
    6, /* JAMMED_GEAR      (local)    */
    6, /* WORK_BREAK       (local)    */
    6, /* DAMAGE_COMPLAINT (local)    */
    4, /* DIPLOMATIC_COURIER (shared) */
    4, /* CIVIC_SKY          (shared) */
};

// Small deterministic byte hash (FNV-1a flavoured), matching the resident
// engine's discipline: the ONLY randomness source, keyed by presentation state.
static uint8_t ev_hash(uint8_t a, uint8_t b, uint8_t c) {
    uint32_t h = 2166136261u;
    h = (h ^ a) * 16777619u;
    h = (h ^ b) * 16777619u;
    h = (h ^ c) * 16777619u;
    h ^= h >> 13;
    h *= 16777619u;
    h ^= h >> 15;
    return (uint8_t)h;
}

// Effective deck weight for one family after the diplomacy adjustment.
static uint8_t ev_weight(uint8_t family, int8_t balance) {
    uint8_t magnitude = (uint8_t)(balance < 0 ? -balance : balance);
    return family == DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER
               ? (uint8_t)(4u + 2u * magnitude) : ev_weights[family];
}

static uint8_t ev_pick_family(uint8_t rnd, int8_t balance) {
    uint16_t total = 0;
    for (int i = 1; i < DUEL_CIVIC_EVENT_COUNT; i++)
        total = (uint16_t)(total + ev_weight((uint8_t)i, balance));
    uint8_t r = (uint8_t)(rnd % total);
    uint16_t acc = 0;
    for (int i = 1; i < DUEL_CIVIC_EVENT_COUNT; i++) {
        acc = (uint16_t)(acc + ev_weight((uint8_t)i, balance));
        if (r < acc) return (uint8_t)i;
    }
    return DUEL_CIVIC_EVENT_RUNAWAY_SCROLL;
}

// The displayed family for one cycle, honouring the back-to-back cooldown. The
// cycle index is bounded (phase is a byte, PERIOD>=8 -> <=32 cycles), so the
// forward walk that resolves the "no repeat" rule is a small bounded loop with
// no recursion and no allocation.
static uint8_t ev_family_for_cycle(uint8_t seed, uint8_t cycle, int8_t balance) {
    uint8_t prev = DUEL_CIVIC_EVENT_NONE;
    uint8_t fam  = DUEL_CIVIC_EVENT_RUNAWAY_SCROLL;
    for (uint16_t c = 0; c <= cycle; c++) {
        fam = ev_pick_family(ev_hash(seed, (uint8_t)c, 0x3Cu), balance);
        if (fam == prev) {
            // Re-roll with a second salt, then linear-skip to guarantee the
            // family differs from the previous cycle (cooldown).
            fam = ev_pick_family(ev_hash(seed, (uint8_t)c, 0x7Eu), balance);
            uint8_t guard = 0;
            while (fam == prev && guard < DUEL_CIVIC_EVENT_COUNT) {
                fam = (uint8_t)(fam + 1u);
                if (fam >= DUEL_CIVIC_EVENT_COUNT) fam = DUEL_CIVIC_EVENT_RUNAWAY_SCROLL;
                guard++;
            }
        }
        prev = fam;
    }
    return fam;
}

// Local families land in one city (seed/cycle-chosen); shared families straddle.
static uint8_t ev_target_for(uint8_t family, uint8_t seed, uint8_t cycle,
                             int8_t balance) {
    if (family == DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER)
        return balance > 0 ? DUEL_CIVIC_EVENT_TARGET_LEFT :
               balance < 0 ? DUEL_CIVIC_EVENT_TARGET_RIGHT :
                             DUEL_CIVIC_EVENT_TARGET_SHARED;
    if (family > DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER)
        return DUEL_CIVIC_EVENT_TARGET_SHARED;
    return (ev_hash(seed, cycle, 0x5Au) & 1u) ? DUEL_CIVIC_EVENT_TARGET_RIGHT
                                              : DUEL_CIVIC_EVENT_TARGET_LEFT;
}

// Sub-phase (0..PERIOD-1) -> lifecycle phase (ARMED..COOLDOWN), split evenly.
static uint8_t ev_phase_for_sub(uint8_t sub) {
    return (uint8_t)((sub * 4u) / CIVIC_EVENT_PERIOD);
}

static uint8_t ev_pack_idtarget(uint8_t id, uint8_t target) {
    return (uint8_t)((id & 7u) | ((target & 3u) << 5));
}

civic_event_state_t civic_event_derive(uint8_t seed, uint8_t phase, bool eligible,
                                       int8_t session_balance) {
    if (session_balance < -3) session_balance = -3;
    if (session_balance > 3) session_balance = 3;
    uint8_t cycle = (uint8_t)(phase / CIVIC_EVENT_PERIOD);
    uint8_t sub   = (uint8_t)(phase % CIVIC_EVENT_PERIOD);
    civic_event_state_t st;
    st.phase    = ev_phase_for_sub(sub);
    st.progress = sub;
    if (!eligible) {
        // Safety-gated: critical visitor, transition, KO/replacement, or family
        // cooldown all fold into `eligible`. The slot stays empty (NONE).
        st.id_target = ev_pack_idtarget(DUEL_CIVIC_EVENT_NONE, DUEL_CIVIC_EVENT_TARGET_LEFT);
        return st;
    }
    uint8_t family = ev_family_for_cycle(seed, cycle, session_balance);
    uint8_t target = ev_target_for(family, seed, cycle, session_balance);
    st.id_target = ev_pack_idtarget(family, target);
    return st;
}

/* ----------------------------- rendering ----------------------------------
 * All local families draw entirely inside the floor band (y61-110). Shared
 * families straddle the desk gap (courier, floor band near the gap edge) or the
 * sky band (civic sky, y18-24, above the champion and below the alert region).
 * draw_rare_event runs first in wiz_draw_scene, so the combat / health / alert
 * layers paint over it and can never be occluded. QUIET mode drops the motion
 * accents, calming the event without removing its identity. */


static void event_px(duel_fb_t *fb, bool is_left, int x, int y) {
    duel_fb_px(fb, incantation_desk_x(is_left, x), y, true);
}

static uint8_t event_action(uint8_t floor, uint8_t id) {
    switch (id) {
        case DUEL_CIVIC_EVENT_RUNAWAY_SCROLL:
        case DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER:
            return DUEL_CIVIC_ACTION_HANDLE_DELIVERY;
        case DUEL_CIVIC_EVENT_JAMMED_GEAR:
            return floor == DUEL_CIVIC_FLOOR_COMMONS ? DUEL_CIVIC_ACTION_WATCH_ROOF
                                                   : DUEL_CIVIC_ACTION_REACT;
        case DUEL_CIVIC_EVENT_WORK_BREAK:
            return DUEL_CIVIC_ACTION_REST;
        case DUEL_CIVIC_EVENT_DAMAGE_COMPLAINT:
            return DUEL_CIVIC_ACTION_INSPECT;
        default:
            return DUEL_CIVIC_ACTION_WATCH_ROOF;
    }
}

static void draw_event_floor_mark(duel_fb_t *fb, bool is_left, uint8_t id,
                                  uint8_t floor, int x, int y) {
    /* Compact civic glyph table: dispatch/chart/blueprint; clock/analyzer/press;
     * tea/log/tool; board/cabinet/rack; seal/specimen/toothed banner. */
    static const int8_t mark[5][3][3][2] = {
        {{{-2,-1}, {-6,-1}, {-10,-1}}, {{-2,-1}, {-5,-2}, {-8,-1}}, {{-2,-2}, {-6,-2}, {-10,-2}}},
        {{{ 1,-1}, { 0,-2}, { -1, 1}}, {{ 4,-4}, { 4,-1}, { 4, 2}}, {{-3, 4}, { 0, 4}, { 3, 4}}},
        {{{-2,-2}, {-1,-2}, {  0,-1}}, {{ 0,-2}, { 2,-2}, { 0,-3}}, {{ 0,-3}, { 2,-3}, { 4,-3}}},
        {{{-3,-9}, { 0,-9}, {  3,-9}}, {{-3,-8}, {-3,-4}, {-3, 0}}, {{-3, 1}, { 0, 1}, { 3, 1}}},
        {{{ 1, 1}, { 2, 1}, {  3, 1}}, {{ 1, 1}, { 2, 2}, { 3, 1}}, {{ 1, 4}, { 3, 4}, { 5, 4}}},
    };
    const int8_t (*pixels)[2] = mark[id - 1u][floor];
    for (int i = 0; i < 3; i++)
        event_px(fb, is_left, x + pixels[i][0], y + pixels[i][1]);
}

static void draw_floor_event(duel_fb_t *fb, bool is_left, uint8_t floor,
                             uint8_t id, uint8_t phase, bool quiet) {
    incantation_point_t at = incantation_occupation_anchor(floor, event_action(floor, id));
    int x = at.x, y = at.y;
    switch (id) {
        case DUEL_CIVIC_EVENT_RUNAWAY_SCROLL: {
            static const uint8_t length[4] = {4, 13, 8, 2};
            int end = x - length[phase & 3u];
            if (end < 3) end = 3;
            incantation_civic_hline(fb, is_left, end, x, y);
            incantation_civic_vline(fb, is_left, x, y - 3, y + 1);
            draw_event_floor_mark(fb, is_left, id, floor, x, y);
            if (!quiet && phase == DUEL_CIVIC_EVENT_PHASE_ACTIVE)
                event_px(fb, is_left, end - 1, y - 3);
            break;
        }
        case DUEL_CIVIC_EVENT_JAMMED_GEAR:
            /* Queue clock, seized analyzer, or workshop press. */
            event_px(fb, is_left, x, y);
            event_px(fb, is_left, x - 3, y); event_px(fb, is_left, x + 3, y);
            event_px(fb, is_left, x, y - 3); event_px(fb, is_left, x, y + 3);
            event_px(fb, is_left, x - 2, y - 2); event_px(fb, is_left, x + 2, y - 2);
            event_px(fb, is_left, x - 2, y + 2); event_px(fb, is_left, x + 2, y + 2);
            draw_event_floor_mark(fb, is_left, id, floor, x, y);
            if (!quiet && phase == DUEL_CIVIC_EVENT_PHASE_ACTIVE) {
                event_px(fb, is_left, x + 4, y - 4); event_px(fb, is_left, x + 5, y - 5);
            }
            break;
        case DUEL_CIVIC_EVENT_WORK_BREAK:
            /* Tea wait, observation log, or a tool laid across the bench. */
            incantation_civic_hline(fb, is_left, x - 4, x + 4, y);
            event_px(fb, is_left, x - 2, y - 1); event_px(fb, is_left, x - 1, y - 1);
            draw_event_floor_mark(fb, is_left, id, floor, x, y);
            if (!quiet && phase < DUEL_CIVIC_EVENT_PHASE_RESOLVING)
                event_px(fb, is_left, x - 2 + (phase & 1u), y - 5);
            break;
        default: /* DAMAGE_COMPLAINT: damaged board, cabinet, or rack. */
            for (int i = 0; i < 5 + (phase == DUEL_CIVIC_EVENT_PHASE_ACTIVE ? 4 : 0); i++)
                event_px(fb, is_left, x + ((i >> 1) & 1), y - 8 + i);
            draw_event_floor_mark(fb, is_left, id, floor, x, y);
            if (phase == DUEL_CIVIC_EVENT_PHASE_RESOLVING) {
                event_px(fb, is_left, x - 1, y - 3); event_px(fb, is_left, x + 2, y - 3);
            }
            break;
    }
}

static void draw_shared_event(duel_fb_t *fb, bool is_left, uint8_t floor,
                              uint8_t id, uint8_t phase, bool quiet) {
    incantation_point_t at = incantation_occupation_anchor(floor, event_action(floor, id));
    if (id == DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER) {
        int top = at.y - 12;
        static const uint8_t reach[4] = {27, 31, 29, 26};
        incantation_civic_vline(fb, is_left, at.x, top, at.y);
        incantation_civic_hline(fb, is_left, at.x, reach[phase & 3u], top);
        incantation_civic_hline(fb, is_left, at.x, reach[phase & 3u], top + 3);
        /* Dispatch seal, specimen pennant, or toothed workshop banner. */
        draw_event_floor_mark(fb, is_left, id, floor, at.x, top);
        event_px(fb, is_left, at.x - 1, at.y - 2);
        incantation_civic_vline(fb, is_left, at.x - 1, at.y - 1, at.y);
    } else {
        /* Civic sky keeps its shared horizon while its ribbon adopts the active
         * room's dispatch, chart, or blueprint cadence. */
        int base = 22;
        for (int x = 0; x < DUEL_CANVAS_W; x++) {
            int dy = floor == DUEL_CIVIC_FLOOR_COMMONS ? ((x / 4) & 1) :
                     floor == DUEL_CIVIC_FLOOR_RESEARCH ? ((x / 3) & 1) : ((x / 2) & 1);
            duel_fb_px(fb, x, base - dy, true);
        }
        int streamers = phase == DUEL_CIVIC_EVENT_PHASE_ACTIVE ? 3 :
                        phase == DUEL_CIVIC_EVENT_PHASE_COOLDOWN ? 0 : 1;
        for (int i = 0; i < streamers; i++)
            incantation_civic_vline(fb, is_left, (at.x + i * 7) & 31, base - 4, base - 1);
        if (!quiet && phase == DUEL_CIVIC_EVENT_PHASE_ACTIVE)
            for (int x = floor; x < DUEL_CANVAS_W; x += 3) duel_fb_px(fb, x, base + 2, true);
    }
}

void draw_rare_event(duel_fb_t *fb, const duel_render_t *r, bool is_left) {
    uint8_t id = DUEL_EVENT_ID(r->revision);
    if (id == DUEL_CIVIC_EVENT_NONE || id >= DUEL_CIVIC_EVENT_COUNT) return;
    uint8_t target = DUEL_EVENT_TARGET(r->revision);
    if (id != DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER &&
        ((target == DUEL_CIVIC_EVENT_TARGET_LEFT && !is_left) ||
         (target == DUEL_CIVIC_EVENT_TARGET_RIGHT && is_left))) return;
    uint8_t floor = DUEL_CIVIC_FLOOR(r->civic);
    if (INCANTATION_FLOOR_TRANSITION_ACTIVE(r->floor_transition) &&
        INCANTATION_FLOOR_TRANSITION_PHASE(r->floor_transition) < 2u)
        floor = INCANTATION_FLOOR_TRANSITION_SOURCE(r->floor_transition);
    if (floor == DUEL_CIVIC_FLOOR_SPECIAL) return;
    if (floor >= INCANTATION_OCCUPATION_FLOORS) floor = DUEL_CIVIC_FLOOR_COMMONS;
    uint8_t phase = DUEL_EVENT_PHASE(r->revision);
    bool quiet = DUEL_CIVIC_MODE(r->civic) == DUEL_CIVIC_MODE_QUIET;
    if (id >= DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER)
        draw_shared_event(fb, is_left, floor, id, phase, quiet);
    else
        draw_floor_event(fb, is_left, floor, id, phase, quiet);
}
