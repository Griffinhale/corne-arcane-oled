/*
 * duel_courier.c — M12 notification ecology (Wave 6).
 *
 * Presentation-only. The master derives one global visitor from the normalized
 * notification summary; both halves render it locally from the packed shared_pres
 * byte. Nothing here touches combat, host state, or notification policy, and no
 * field keys off w.tick (determinism gate, plan §2 D3).
 */
#include "duel_courier.h"

#ifdef ARCANE_M12

#include "duel_host.h"

// --- Wave 6 notification-ecology routing (spec §11.3) ------------------------
// Category -> courier kind and city, before the persistent / security override.
// City 0 = left (astral) city, 1 = right (mechanical) city. communication and
// calendar land left; transfer and system (and terminal/build) land right.
static uint8_t category_kind(uint8_t category) {
    switch (category) {
    case DUEL_HOST_CATEGORY_COMMUNICATION: return DUEL_M12_COURIER_MESSENGER;
    case DUEL_HOST_CATEGORY_CALENDAR:      return DUEL_M12_COURIER_MESSENGER;
    case DUEL_HOST_CATEGORY_TRANSFER:      return DUEL_M12_COURIER_PARCEL;
    case DUEL_HOST_CATEGORY_SYSTEM:        return DUEL_M12_COURIER_BEACON;
    case DUEL_HOST_CATEGORY_TERMINAL:      return DUEL_M12_COURIER_BEACON;
    case DUEL_HOST_CATEGORY_SECURITY:      return DUEL_M12_COURIER_SENTINEL;
    case DUEL_HOST_CATEGORY_OTHER:         return DUEL_M12_COURIER_MESSENGER;
    default:                               return DUEL_M12_COURIER_NONE;
    }
}

static uint8_t category_city(uint8_t category) {
    switch (category) {
    case DUEL_HOST_CATEGORY_TRANSFER:
    case DUEL_HOST_CATEGORY_SYSTEM:
    case DUEL_HOST_CATEGORY_TERMINAL:
        return 1u; // right / mechanical city
    default:
        return 0u; // left / astral city (communication, calendar, security, other)
    }
}

static uint8_t age_lifecycle(uint8_t age) {
    if (age == 0u)      return DUEL_M12_VISIT_ARRIVING; // new
    if (age <= 2u)      return DUEL_M12_VISIT_WAITING;  // pending
    if (age <= 6u)      return DUEL_M12_VISIT_AGING;    // old
    return DUEL_M12_VISIT_RESOLVING;                    // aged to dismissal (7)
}

static uint8_t count_density(uint8_t count) {
    if (count >= 5u) return DUEL_M12_DENSITY_MANY;
    if (count >= 2u) return DUEL_M12_DENSITY_FEW;
    return DUEL_M12_DENSITY_SINGLE;
}

m12_visitor_state_t m12_visitor_derive(uint8_t seed, uint8_t phase, uint8_t category,
                                       uint8_t count, uint8_t age, bool persistent) {
    // Routing is a pure function of the notification summary; seed/phase are
    // reserved for later sub-motion and deliberately do NOT flip the visitor's
    // kind/city/lifecycle (that would strobe the form every civic tick).
    (void)seed; (void)phase;

    m12_visitor_state_t st = {0};

    // none / low: no notification, no visitor.
    if (category == DUEL_HOST_CATEGORY_NONE || category >= DUEL_HOST_CATEGORY_COUNT ||
        count == 0u) {
        return st; // kind_target == COURIER_NONE
    }

    uint8_t kind = category_kind(category);
    uint8_t city = category_city(category);

    // Security-critical or any persistent notification stations a sentinel; the
    // city stays the category's city so a persistent transfer entrenches on the
    // right, a persistent message on the left.
    if (persistent || category == DUEL_HOST_CATEGORY_SECURITY)
        kind = DUEL_M12_COURIER_SENTINEL;

    st.kind_target     = (uint8_t)((kind & 7u) | ((city & 1u) << 3));
    st.lifecycle_phase = age_lifecycle(age);
    st.progress_flags  = (uint8_t)(count_density(count) | (persistent ? 0x04u : 0u));
    return st;
}

uint8_t m12_visitor_shared_pres(m12_visitor_state_t state) {
    return (uint8_t)(DUEL_VISITOR_PACK(DUEL_VISITOR_STATE_KIND(state),
                                       DUEL_VISITOR_STATE_CITY(state),
                                       state.lifecycle_phase) |
                     DUEL_VISITOR_DENSITY_PACK(DUEL_VISITOR_STATE_DENSITY(state)));
}

void draw_courier(duel_fb_t *fb, const duel_render_t *r, bool is_left) {
    const uint8_t sp   = r->shared_pres;
    const uint8_t kind = DUEL_VISITOR_KIND(sp);
    if (kind == DUEL_M12_COURIER_NONE || kind >= DUEL_M12_COURIER_COUNT) return;

    // The visitor is assigned to exactly one city; the other half draws nothing.
    const uint8_t city = DUEL_VISITOR_CITY(sp); // 0 left, 1 right
    const bool assigned = is_left ? (city == 0u) : (city == 1u);
    if (!assigned) return;

    const uint8_t life    = DUEL_VISITOR_LIFECYCLE(sp);
    const uint8_t density = DUEL_VISITOR_DENSITY(sp);
    const bool    quiet   = DUEL_CIVIC_MODE(r->civic) == DUEL_M12_MODE_QUIET;

#define CX(x) (is_left ? (x) : (DUEL_CANVAS_W - 1 - (x)))
    const int g = is_left ? +1 : -1; // toward the centre gap

    // Lifecycle station in desk space: ARRIVING enters by the gap-side lift, then
    // WAITING/AGING drift inward and out, RESOLVING returns to the gap to depart.
    static const uint8_t life_ax[4] = { 24u, 17u, 11u, 26u };
    int ax     = life_ax[life & 3u];
    int settle = (life == DUEL_M12_VISIT_AGING) ? 1 : 0; // old visitors settle
    int cx = 0, cy = 0;

    switch (kind) {
    case DUEL_M12_COURIER_MESSENGER: {
        // Communication / calendar bird flitting high in the floor band.
        cx = CX(ax); cy = 68 + settle;
        duel_fb_px(fb, cx - 2, cy, true);           // gull wings meeting at a body
        duel_fb_px(fb, cx - 1, cy - 1, true);
        duel_fb_px(fb, cx,     cy, true);
        duel_fb_px(fb, cx + 1, cy - 1, true);
        duel_fb_px(fb, cx + 2, cy, true);
        duel_fb_px(fb, cx + g,     cy + 1, true);   // head + beak toward the gap
        duel_fb_px(fb, cx + 2 * g, cy + 1, true);
        if (density >= DUEL_M12_DENSITY_FEW) {      // broader wingspan
            duel_fb_px(fb, cx - 3, cy + 1, true);
            duel_fb_px(fb, cx + 3, cy + 1, true);
        }
        if (density >= DUEL_M12_DENSITY_MANY) {     // fuller plumage
            duel_fb_px(fb, cx - 1, cy + 1, true);
            duel_fb_px(fb, cx + 1, cy + 1, true);
            duel_fb_px(fb, cx,     cy - 2, true);
        }
        break;
    }
    case DUEL_M12_COURIER_PARCEL: {
        // Transfer / download hand-cart resting on the room floor.
        cx = CX(ax); cy = 88 + settle;
        for (int yy = cy - 2; yy <= cy; yy++)       // parcel box
            for (int xx = cx - 1; xx <= cx + 1; xx++) duel_fb_px(fb, xx, yy, true);
        duel_fb_px(fb, cx - 1, cy + 1, true);       // wheels
        duel_fb_px(fb, cx + 1, cy + 1, true);
        duel_fb_px(fb, cx + 2 * g, cy - 1, true);   // pull handle toward the gap
        duel_fb_px(fb, cx + 2 * g, cy - 2, true);
        if (density >= DUEL_M12_DENSITY_FEW)        // a second stacked parcel
            for (int xx = cx - 1; xx <= cx; xx++) {
                duel_fb_px(fb, xx, cy - 4, true); duel_fb_px(fb, xx, cy - 3, true);
            }
        if (density >= DUEL_M12_DENSITY_MANY)       // a third, toppling
            for (int xx = cx; xx <= cx + 1; xx++) {
                duel_fb_px(fb, xx, cy - 6, true); duel_fb_px(fb, xx, cy - 5, true);
            }
        break;
    }
    case DUEL_M12_COURIER_BEACON: {
        // System / network signal conduit: a mast with a pulsing beacon head.
        cx = CX(ax); cy = 76 + settle;
        int top = cy - 6, bot = cy + 8;
        for (int yy = top; yy <= bot; yy++) duel_fb_px(fb, cx, yy, true); // mast
        duel_fb_px(fb, cx - 1, top, true);          // beacon head flare
        duel_fb_px(fb, cx + 1, top, true);
        duel_fb_px(fb, cx,     top - 1, true);
        duel_fb_px(fb, cx - 1, bot, true);          // base bracket
        duel_fb_px(fb, cx + 1, bot, true);
        if (density >= DUEL_M12_DENSITY_FEW) {      // side conduit taps
            duel_fb_px(fb, cx + g, cy - 2, true);
            duel_fb_px(fb, cx + g, cy + 2, true);
        }
        if (density >= DUEL_M12_DENSITY_MANY) {     // radiating emitter rays
            duel_fb_px(fb, cx - 2, top - 1, true);
            duel_fb_px(fb, cx + 2, top - 1, true);
            duel_fb_px(fb, cx - g, cy, true);
        }
        break;
    }
    case DUEL_M12_COURIER_SENTINEL:
    default: {
        // Security-critical / persistent stationed alarm: a fixed entrenched post
        // with a warning lamp. It never drifts with the lifecycle station.
        cx = CX(19); cy = 90;
        for (int yy = cy - 8; yy <= cy; yy++) duel_fb_px(fb, cx, yy, true); // staff
        duel_fb_px(fb, cx,     cy - 9, true);       // alarm lamp
        duel_fb_px(fb, cx - 1, cy - 8, true);
        duel_fb_px(fb, cx + 1, cy - 8, true);
        duel_fb_px(fb, cx - 1, cy, true);           // entrenched foot
        duel_fb_px(fb, cx + 1, cy, true);
        duel_fb_px(fb, cx - 2, cy, true);
        duel_fb_px(fb, cx + 2, cy, true);
        duel_fb_px(fb, cx + 2 * g, cy - 6, true);   // warning bar toward the gap
        duel_fb_px(fb, cx + 2 * g, cy - 4, true);
        duel_fb_px(fb, cx + 2 * g, cy - 2, true);
        if (life == DUEL_M12_VISIT_AGING) {         // entrenched: dust banked up
            duel_fb_px(fb, cx - 3, cy, true);
            duel_fb_px(fb, cx + 3, cy, true);
        }
        return; // stationed: no drifting lifecycle badge
    }
    }

    // Lifecycle badge for the mobile couriers (the station already differs per
    // phase; the badge names it). QUIET calms motion: the arrival trail and the
    // departure spark are suppressed so the visitor reads as settled.
    switch (life) {
    case DUEL_M12_VISIT_ARRIVING:
        if (!quiet) {                               // motion trail, away from gap
            duel_fb_px(fb, cx - 2 * g, cy - 1, true);
            duel_fb_px(fb, cx - 3 * g, cy, true);
        }
        break;
    case DUEL_M12_VISIT_WAITING:
        duel_fb_px(fb, cx, cy + 3, true);           // a patient standing mark
        break;
    case DUEL_M12_VISIT_AGING:
        duel_fb_px(fb, cx - 1, cy + 3, true);       // gathered dust
        duel_fb_px(fb, cx + 1, cy + 4, true);
        break;
    case DUEL_M12_VISIT_RESOLVING:
        if (!quiet) {                               // departing spark toward gap
            duel_fb_px(fb, cx + 3 * g, cy, true);
            duel_fb_px(fb, cx + 4 * g, cy - 1, true);
        }
        break;
    default: break;
    }
#undef CX
}

#endif // ARCANE_M12
