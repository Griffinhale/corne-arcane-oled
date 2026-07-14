/*
 * duel_event.c — M12 rare-event deck (Wave 7, presentation-only).
 *
 * A deterministic, safety-gated weighted deck over six families (four LOCAL to
 * one city, two SHARED across the desk gap / sky). The master advances the deck
 * one civic phase at a time and packs (id/phase/target) into the snapshot's
 * revision byte via DUEL_EVENT_PACK; both halves decode and render locally. The
 * engine keys strictly off (seed, civic phase) — never w.tick — and allocates
 * nothing, so mechanics and power discipline are untouched. Compiles out
 * entirely under !ARCANE_M12 (the header include keeps the TU non-empty).
 */
#include "duel_event.h"

#ifdef ARCANE_M12

#include "duel_host.h"

/* ------------------------------- deck -------------------------------------
 * One rare-event CYCLE spans M12_EVENT_PERIOD civic phases. The family is
 * chosen once per cycle (keyed by seed + cycle index) with a back-to-back
 * cooldown so no family repeats in adjacent cycles; the sub-phase within the
 * cycle drives the ARMED->ACTIVE->RESOLVING->COOLDOWN progression. Weighting is
 * ~75% local / 25% shared (24 : 8 of 32 total weight). */
#define M12_EVENT_PERIOD 8u

// Deck weights in DUEL_M12_EVENT_* order (NONE first). Locals 1-4 sum to 24
// (75%); shareds 5-6 sum to 8 (25%); grand total 32.
static const uint8_t ev_weights[DUEL_M12_EVENT_COUNT] = {
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

// Weighted family selection from a random byte. Never returns NONE.
static uint8_t ev_pick_family(uint8_t rnd) {
    uint16_t total = 0;
    for (int i = 1; i < DUEL_M12_EVENT_COUNT; i++) total = (uint16_t)(total + ev_weights[i]);
    uint8_t r = (uint8_t)(rnd % total);
    uint16_t acc = 0;
    for (int i = 1; i < DUEL_M12_EVENT_COUNT; i++) {
        acc = (uint16_t)(acc + ev_weights[i]);
        if (r < acc) return (uint8_t)i;
    }
    return DUEL_M12_EVENT_RUNAWAY_SCROLL;
}

// The displayed family for one cycle, honouring the back-to-back cooldown. The
// cycle index is bounded (phase is a byte, PERIOD>=8 -> <=32 cycles), so the
// forward walk that resolves the "no repeat" rule is a small bounded loop with
// no recursion and no allocation.
static uint8_t ev_family_for_cycle(uint8_t seed, uint8_t cycle) {
    uint8_t prev = DUEL_M12_EVENT_NONE;
    uint8_t fam  = DUEL_M12_EVENT_RUNAWAY_SCROLL;
    for (uint16_t c = 0; c <= cycle; c++) {
        fam = ev_pick_family(ev_hash(seed, (uint8_t)c, 0x3Cu));
        if (fam == prev) {
            // Re-roll with a second salt, then linear-skip to guarantee the
            // family differs from the previous cycle (cooldown).
            fam = ev_pick_family(ev_hash(seed, (uint8_t)c, 0x7Eu));
            uint8_t guard = 0;
            while (fam == prev && guard < DUEL_M12_EVENT_COUNT) {
                fam = (uint8_t)(fam + 1u);
                if (fam >= DUEL_M12_EVENT_COUNT) fam = DUEL_M12_EVENT_RUNAWAY_SCROLL;
                guard++;
            }
        }
        prev = fam;
    }
    return fam;
}

// Local families land in one city (seed/cycle-chosen); shared families straddle.
static uint8_t ev_target_for(uint8_t family, uint8_t seed, uint8_t cycle) {
    if (family >= DUEL_M12_EVENT_DIPLOMATIC_COURIER) return DUEL_M12_EVENT_TARGET_SHARED;
    return (ev_hash(seed, cycle, 0x5Au) & 1u) ? DUEL_M12_EVENT_TARGET_RIGHT
                                              : DUEL_M12_EVENT_TARGET_LEFT;
}

// Sub-phase (0..PERIOD-1) -> lifecycle phase (ARMED..COOLDOWN), split evenly.
static uint8_t ev_phase_for_sub(uint8_t sub) {
    return (uint8_t)((sub * 4u) / M12_EVENT_PERIOD);
}

static uint8_t ev_pack_idtarget(uint8_t id, uint8_t target) {
    return (uint8_t)((id & 7u) | ((target & 3u) << 5));
}

m12_event_state_t m12_event_derive(uint8_t seed, uint8_t phase, bool eligible) {
    uint8_t cycle = (uint8_t)(phase / M12_EVENT_PERIOD);
    uint8_t sub   = (uint8_t)(phase % M12_EVENT_PERIOD);
    m12_event_state_t st;
    st.phase    = ev_phase_for_sub(sub);
    st.progress = sub;
    if (!eligible) {
        // Safety-gated: critical visitor, transition, KO/replacement, or family
        // cooldown all fold into `eligible`. The slot stays empty (NONE).
        st.id_target = ev_pack_idtarget(DUEL_M12_EVENT_NONE, DUEL_M12_EVENT_TARGET_LEFT);
        return st;
    }
    uint8_t family = ev_family_for_cycle(seed, cycle);
    uint8_t target = ev_target_for(family, seed, cycle);
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

static int evx(bool is_left, int x) { return is_left ? x : (DUEL_CANVAS_W - 1 - x); }

static void ev_hspan(duel_fb_t *fb, bool is_left, int x0, int x1, int y) {
    int a = evx(is_left, x0), b = evx(is_left, x1);
    if (a > b) { int t = a; a = b; b = t; }
    for (int x = a; x <= b; x++) duel_fb_px(fb, x, y, true);
}

static void ev_vline(duel_fb_t *fb, bool is_left, int x, int y0, int y1) {
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for (int y = y0; y <= y1; y++) duel_fb_px(fb, evx(is_left, x), y, true);
}

static void ev_px(duel_fb_t *fb, bool is_left, int x, int y) {
    duel_fb_px(fb, evx(is_left, x), y, true);
}

// RUNAWAY_SCROLL (local, magical): a scroll unrolls across the floor shelf and
// re-rolls as it resolves. Progress reads through the trailing sheet length.
static void ev_draw_scroll(duel_fb_t *fb, bool is_left, uint8_t phase, bool quiet) {
    const int y = 88;
    ev_vline(fb, is_left, 22, y - 3, y + 1);   // rolled end
    ev_vline(fb, is_left, 23, y - 3, y + 1);
    int len;
    switch (phase) {
        case DUEL_M12_EVENT_PHASE_ARMED:     len = 4;  break;
        case DUEL_M12_EVENT_PHASE_ACTIVE:    len = 13; break;
        case DUEL_M12_EVENT_PHASE_RESOLVING: len = 8;  break;
        default:                             len = 2;  break; // COOLDOWN
    }
    int x_end = 21 - len + 1;
    if (x_end < 6) x_end = 6;
    ev_hspan(fb, is_left, x_end, 21, y);
    if (phase == DUEL_M12_EVENT_PHASE_ACTIVE || phase == DUEL_M12_EVENT_PHASE_RESOLVING)
        for (int x = x_end; x <= 21; x += 3) ev_px(fb, is_left, x, y - 1); // writing lines
    if (!quiet && phase == DUEL_M12_EVENT_PHASE_ACTIVE) {
        ev_px(fb, is_left, x_end - 1, y - 2);   // fluttering leading edge
        ev_px(fb, is_left, x_end, y - 3);
    }
    if (phase == DUEL_M12_EVENT_PHASE_COOLDOWN) {
        ev_px(fb, is_left, 18, y + 2);          // dropped wax specks
        ev_px(fb, is_left, 15, y + 2);
    }
}

// JAMMED_GEAR (local, mechanical): a stuck gear with a jam wedge, sparks while
// ACTIVE, and a dropped bolt once it settles.
static void ev_draw_gear(duel_fb_t *fb, bool is_left, uint8_t phase, bool quiet) {
    const int cx = 15, cy = 80;
    for (int dy = -4; dy <= 4; dy++)
        for (int dx = -4; dx <= 4; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 >= 9 && d2 <= 16) ev_px(fb, is_left, cx + dx, cy + dy); // ring
        }
    ev_px(fb, is_left, cx, cy);                 // hub
    ev_px(fb, is_left, cx, cy - 5); ev_px(fb, is_left, cx, cy + 5); // teeth
    ev_px(fb, is_left, cx - 5, cy); ev_px(fb, is_left, cx + 5, cy);
    // Jam wedge biting into the NE rim; length grows through the active phase.
    int wedge = (phase == DUEL_M12_EVENT_PHASE_ARMED) ? 2 : 4;
    for (int i = 0; i < wedge; i++) ev_px(fb, is_left, cx + 5 - i, cy - 5 + i);
    if (!quiet && phase == DUEL_M12_EVENT_PHASE_ACTIVE) {
        ev_px(fb, is_left, cx + 6, cy - 6);     // grinding sparks
        ev_px(fb, is_left, cx + 7, cy - 6);
        ev_px(fb, is_left, cx + 6, cy - 7);
    }
    if (phase == DUEL_M12_EVENT_PHASE_COOLDOWN) {
        ev_px(fb, is_left, cx, cy + 7);         // dropped bolt
        ev_px(fb, is_left, cx + 1, cy + 7);
    }
}

// WORK_BREAK (local, social): the resident pauses — a small table, a steaming
// mug, then the mug set aside. Steam is the motion accent QUIET removes.
static void ev_draw_break(duel_fb_t *fb, bool is_left, uint8_t phase, bool quiet) {
    const int ty = 92;
    ev_hspan(fb, is_left, 12, 20, ty);          // tabletop
    ev_vline(fb, is_left, 12, ty, ty + 4);      // legs
    ev_vline(fb, is_left, 20, ty, ty + 4);
    ev_px(fb, is_left, 14, ty - 1); ev_px(fb, is_left, 15, ty - 1); // mug
    ev_px(fb, is_left, 14, ty - 2); ev_px(fb, is_left, 15, ty - 2);
    ev_px(fb, is_left, 16, ty - 1);             // handle
    if (!quiet && (phase == DUEL_M12_EVENT_PHASE_ARMED || phase == DUEL_M12_EVENT_PHASE_ACTIVE)) {
        int h = (phase == DUEL_M12_EVENT_PHASE_ACTIVE) ? 5 : 3;
        for (int i = 1; i <= h; i++) ev_px(fb, is_left, 14 + (i & 1), ty - 2 - i); // rising steam
    }
    if (phase == DUEL_M12_EVENT_PHASE_RESOLVING) ev_px(fb, is_left, 15, ty - 4); // last wisp
    if (phase == DUEL_M12_EVENT_PHASE_COOLDOWN)  ev_px(fb, is_left, 18, ty - 1); // set aside
}

// DAMAGE_COMPLAINT (local): a jagged crack up a wall panel with a caution mark;
// resolves to a patched seam.
static void ev_draw_complaint(duel_fb_t *fb, bool is_left, uint8_t phase, bool quiet) {
    int bot;
    switch (phase) {
        case DUEL_M12_EVENT_PHASE_ARMED:     bot = 78; break;
        case DUEL_M12_EVENT_PHASE_ACTIVE:    bot = 88; break;
        case DUEL_M12_EVENT_PHASE_RESOLVING: bot = 84; break;
        default:                             bot = 76; break; // COOLDOWN
    }
    for (int y = 72; y <= bot; y++) ev_px(fb, is_left, 16 + ((y >> 1) & 1), y); // zigzag crack
    if (phase != DUEL_M12_EVENT_PHASE_COOLDOWN) {
        ev_vline(fb, is_left, 22, 70, 75);      // caution bar
        ev_px(fb, is_left, 22, 77);             // caution dot
    }
    if (!quiet && phase == DUEL_M12_EVENT_PHASE_ACTIVE) {
        ev_px(fb, is_left, 13, 74);             // shake ticks
        ev_px(fb, is_left, 19, 80);
    }
    if (phase == DUEL_M12_EVENT_PHASE_RESOLVING) {
        ev_px(fb, is_left, 15, 82); ev_px(fb, is_left, 17, 82); // patch staples
    }
}

// DIPLOMATIC_COURIER (shared): a banner on a pole and a small courier at the
// desk-gap edge. Both halves draw it in desk space so the banner meets across
// the physical gap. The banner reach grows as it unfurls.
static void ev_draw_courier(duel_fb_t *fb, bool is_left, uint8_t phase, bool quiet) {
    const int px = 25, top = 68;
    ev_vline(fb, is_left, px, top, 82);         // pole
    int reach;
    switch (phase) {
        case DUEL_M12_EVENT_PHASE_ARMED:     reach = 27; break;
        case DUEL_M12_EVENT_PHASE_ACTIVE:    reach = 31; break;
        case DUEL_M12_EVENT_PHASE_RESOLVING: reach = 29; break;
        default:                             reach = 26; break; // COOLDOWN
    }
    ev_hspan(fb, is_left, px, reach, top);      // banner top
    ev_hspan(fb, is_left, px, reach, top + 3);  // banner bottom
    if (phase == DUEL_M12_EVENT_PHASE_ACTIVE || phase == DUEL_M12_EVENT_PHASE_RESOLVING)
        ev_px(fb, is_left, reach, top + 1);     // pennant tip at the gap
    // Courier figure at the base of the pole.
    ev_px(fb, is_left, 24, 80);                 // head
    ev_vline(fb, is_left, 24, 81, 82);          // body
    ev_px(fb, is_left, 23, 82); ev_px(fb, is_left, 25, 82); // legs
    if (!quiet && phase == DUEL_M12_EVENT_PHASE_ACTIVE) ev_px(fb, is_left, 30, top - 1); // sparkle
    if (phase == DUEL_M12_EVENT_PHASE_COOLDOWN)         ev_px(fb, is_left, 28, 82);      // dropped seal
}

// CIVIC_SKY (shared, sky band): a low aurora ribbon with streamers, drawn on
// both halves so it reads continuous across the gap. Kept in y18-24, clear of
// the alert region (<=15) and above the raised champion (>=37 idle).
static void ev_draw_sky(duel_fb_t *fb, bool is_left, uint8_t phase, bool quiet) {
    const int baseY = 22;
    for (int x = 0; x < DUEL_CANVAS_W; x++) {
        int wob = ((x / 3) & 1) ? 1 : 0;
        duel_fb_px(fb, x, baseY - wob, true);   // ribbon (canvas-absolute; symmetric across gap)
    }
    int streamers;
    switch (phase) {
        case DUEL_M12_EVENT_PHASE_ARMED:     streamers = 1; break;
        case DUEL_M12_EVENT_PHASE_ACTIVE:    streamers = 3; break;
        case DUEL_M12_EVENT_PHASE_RESOLVING: streamers = 2; break;
        default:                             streamers = 0; break; // COOLDOWN
    }
    for (int s = 0; s < streamers; s++) ev_vline(fb, is_left, 6 + s * 9, baseY - 4, baseY - 1);
    if (!quiet && (phase == DUEL_M12_EVENT_PHASE_ACTIVE || phase == DUEL_M12_EVENT_PHASE_RESOLVING))
        for (int x = 0; x < DUEL_CANVAS_W; x += 2) duel_fb_px(fb, x, baseY + 2, true); // shimmer row
}

void draw_rare_event(duel_fb_t *fb, const duel_render_t *r, bool is_left) {
    uint8_t id = DUEL_EVENT_ID(r->revision);
    if (id == DUEL_M12_EVENT_NONE) return;              // empty / safety-gated slot
    uint8_t phase  = DUEL_EVENT_PHASE(r->revision);
    uint8_t target = DUEL_EVENT_TARGET(r->revision);
    bool quiet = DUEL_CIVIC_MODE(r->civic) == DUEL_M12_MODE_QUIET;

    // Local families live in one city: the other half draws nothing.
    if (target == DUEL_M12_EVENT_TARGET_LEFT  && !is_left) return;
    if (target == DUEL_M12_EVENT_TARGET_RIGHT &&  is_left) return;

    switch (id) {
        case DUEL_M12_EVENT_RUNAWAY_SCROLL:     ev_draw_scroll(fb, is_left, phase, quiet);    break;
        case DUEL_M12_EVENT_JAMMED_GEAR:        ev_draw_gear(fb, is_left, phase, quiet);      break;
        case DUEL_M12_EVENT_WORK_BREAK:         ev_draw_break(fb, is_left, phase, quiet);     break;
        case DUEL_M12_EVENT_DAMAGE_COMPLAINT:   ev_draw_complaint(fb, is_left, phase, quiet); break;
        case DUEL_M12_EVENT_DIPLOMATIC_COURIER: ev_draw_courier(fb, is_left, phase, quiet);   break;
        case DUEL_M12_EVENT_CIVIC_SKY:          ev_draw_sky(fb, is_left, phase, quiet);       break;
        default: break;
    }
}

#endif // ARCANE_M12
