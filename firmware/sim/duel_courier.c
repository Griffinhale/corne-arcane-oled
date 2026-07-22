/*
 * duel_courier.c — Twin Cities notification ecology (Wave 6).
 *
 * Presentation-only. The master derives one global visitor from the normalized
 * notification summary; both halves render it locally from the packed shared_pres
 * byte. Nothing here touches combat, host state, or notification policy, and no
 * field keys off w.tick (determinism gate, plan §2 D3).
 */
#include "duel_courier.h"


#include "duel_host.h"
#include "duel_resident.h"

// --- Wave 6 notification-ecology routing (spec §11.3) ------------------------
// Category -> courier kind and city, before the persistent / security override.
// City 0 = left (astral) city, 1 = right (mechanical) city. communication and
// calendar land left; transfer and system (and terminal/build) land right.
static uint8_t category_kind(uint8_t category) {
    switch (category) {
    case DUEL_HOST_CATEGORY_COMMUNICATION: return DUEL_CIVIC_COURIER_MESSENGER;
    case DUEL_HOST_CATEGORY_CALENDAR:      return DUEL_CIVIC_COURIER_MESSENGER;
    case DUEL_HOST_CATEGORY_TRANSFER:      return DUEL_CIVIC_COURIER_PARCEL;
    case DUEL_HOST_CATEGORY_SYSTEM:        return DUEL_CIVIC_COURIER_BEACON;
    case DUEL_HOST_CATEGORY_TERMINAL:      return DUEL_CIVIC_COURIER_BEACON;
    case DUEL_HOST_CATEGORY_SECURITY:      return DUEL_CIVIC_COURIER_SENTINEL;
    case DUEL_HOST_CATEGORY_OTHER:         return DUEL_CIVIC_COURIER_MESSENGER;
    default:                               return DUEL_CIVIC_COURIER_NONE;
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
    if (age == 0u)      return DUEL_CIVIC_VISIT_ARRIVING; // new
    if (age <= 2u)      return DUEL_CIVIC_VISIT_WAITING;  // pending
    if (age <= 6u)      return DUEL_CIVIC_VISIT_AGING;    // old
    return DUEL_CIVIC_VISIT_RESOLVING;                    // aged to dismissal (7)
}

static uint8_t count_density(uint8_t count) {
    if (count >= 5u) return DUEL_CIVIC_DENSITY_MANY;
    if (count >= 2u) return DUEL_CIVIC_DENSITY_FEW;
    return DUEL_CIVIC_DENSITY_SINGLE;
}

civic_visitor_state_t civic_visitor_derive(uint8_t seed, uint8_t phase, uint8_t category,
                                       uint8_t count, uint8_t age, bool persistent) {
    // Routing is a pure function of the notification summary; seed/phase are
    // reserved for later sub-motion and deliberately do NOT flip the visitor's
    // kind/city/lifecycle (that would strobe the form every civic tick).
    (void)seed; (void)phase;

    civic_visitor_state_t st = {0};

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
        kind = DUEL_CIVIC_COURIER_SENTINEL;

    st.kind_target     = (uint8_t)((kind & 7u) | ((city & 1u) << 3));
    st.lifecycle_phase = age_lifecycle(age);
    st.progress_flags  = (uint8_t)(count_density(count) | (persistent ? 0x04u : 0u));
    return st;
}

uint8_t civic_visitor_shared_pres(civic_visitor_state_t state) {
    return (uint8_t)(DUEL_VISITOR_PACK(DUEL_VISITOR_STATE_KIND(state),
                                       DUEL_VISITOR_STATE_CITY(state),
                                       state.lifecycle_phase) |
                     DUEL_VISITOR_DENSITY_PACK(DUEL_VISITOR_STATE_DENSITY(state)));
}

static void draw_courier_floor_mark(duel_fb_t *fb, bool is_left, uint8_t kind,
                                    uint8_t floor, int x, int y) {
    /* Three pixels reinterpret each stable courier core as, respectively:
     * dispatch/note/punch-card; files/canister/crate; bell/probe/stack lamp;
     * security header/anomaly seal/lockout arm. */
    static const int8_t mark[4][3][3][2] = {
        {{{-1, 2}, { 0, 2}, { 1, 2}},
         {{-1, 2}, { 0, 3}, { 1, 2}},
         {{-1, 2}, { 1, 2}, { 0, 1}}},
        {{{ 0,-4}, { 0,-3}, { 0,-2}},
         {{-1,-5}, { 0,-5}, { 1,-5}},
         {{-2, 0}, { 2,-4}, { 0,-2}}},
        {{{-2,-6}, {-1,-7}, { 1,-7}},
         {{-1,-8}, { 1,-8}, { 2,-6}},
         {{-1,-7}, { 1,-7}, {-1,-4}}},
        {{{-2,-7}, {-1,-7}, { 0,-7}},
         {{-1,-7}, { 1,-7}, {-1,-5}},
         {{-4,-5}, {-3,-5}, {-2,-5}}},
    };
    const int8_t (*pixels)[2] = mark[kind - 1u][floor];
    for (int i = 0; i < 3; i++)
        duel_fb_px(fb, duel_fb_desk_x(is_left, x + pixels[i][0]), y + pixels[i][1], true);
}

void draw_courier(duel_fb_t *fb, const duel_render_t *r, bool is_left) {
    const uint8_t sp   = r->shared_pres;
    const uint8_t kind = DUEL_VISITOR_KIND(sp);
    if (kind == DUEL_CIVIC_COURIER_NONE || kind >= DUEL_CIVIC_COURIER_COUNT) return;

    // The visitor is assigned to exactly one city; the other half draws nothing.
    const uint8_t city = DUEL_VISITOR_CITY(sp); // 0 left, 1 right
    const bool assigned = is_left ? (city == 0u) : (city == 1u);
    if (!assigned) return;

    const uint8_t life    = DUEL_VISITOR_LIFECYCLE(sp);
    const uint8_t density = DUEL_VISITOR_DENSITY(sp);
    const bool    quiet   = DUEL_CIVIC_MODE(r->civic) == DUEL_CIVIC_MODE_QUIET;

    uint8_t floor = incantation_effective_floor(r);
    if (floor == DUEL_CIVIC_FLOOR_SPECIAL) return;
    if (floor >= INCANTATION_OCCUPATION_FLOORS) floor = DUEL_CIVIC_FLOOR_COMMONS;
    static const uint8_t destination_action[] = {
        DUEL_CIVIC_ACTION_WORK, DUEL_CIVIC_ACTION_HANDLE_DELIVERY,
        DUEL_CIVIC_ACTION_HANDLE_DELIVERY, DUEL_CIVIC_ACTION_WATCH_ROOF,
        DUEL_CIVIC_ACTION_INSPECT,
    };
    incantation_point_t destination = incantation_occupation_anchor(floor, destination_action[kind]);
    int x = destination.x, y = destination.y;
    if (kind != DUEL_CIVIC_COURIER_SENTINEL) {
        /* The established gap lift remains the entrance. Waiting is two thirds
         * through the route, aging reaches the occupation object, and resolving
         * returns to the lift. The entrance point must match the lift stub the
         * floor scenery draws by the gap. */
#define INCANTATION_LIFT_X 27
#define INCANTATION_LIFT_Y 72
        static const uint8_t route_step[4] = {0, 2, 3, 0};
        int step = route_step[life & 3u];
        x = INCANTATION_LIFT_X + (destination.x - INCANTATION_LIFT_X) * step / 3;
        y = INCANTATION_LIFT_Y + (destination.y - INCANTATION_LIFT_Y) * step / 3;
    }
    int cx = duel_fb_desk_x(is_left, x), g = is_left ? 1 : -1;

    switch (kind) {
        case DUEL_CIVIC_COURIER_MESSENGER:
            /* Shared carrier core: a winged dispatch packet. The floor mark is
             * a filing tail, specimen loop, or punched gear card. */
            duel_fb_px(fb, cx, y, true);
            duel_fb_px(fb, cx - 1, y - 1, true); duel_fb_px(fb, cx + 1, y - 1, true);
            duel_fb_px(fb, cx - 2, y, true); duel_fb_px(fb, cx + 2, y, true);
            duel_fb_px(fb, cx + 2 * g, y + 1, true);
            break;
        case DUEL_CIVIC_COURIER_PARCEL:
            /* Filing cart, canister trolley, or braced parts crate. */
            duel_fb_desk_hline(fb, is_left, x - 2, x + 2, y);
            duel_fb_desk_hline(fb, is_left, x - 2, x + 2, y - 4);
            duel_fb_desk_vline(fb, is_left, x - 2, y - 4, y);
            duel_fb_desk_vline(fb, is_left, x + 2, y - 4, y);
            duel_fb_px(fb, cx - g, y + 1, true); duel_fb_px(fb, cx + g, y + 1, true);
            break;
        case DUEL_CIVIC_COURIER_BEACON:
            duel_fb_desk_vline(fb, is_left, x, y - 7, y + 2);
            duel_fb_desk_hline(fb, is_left, x - 2, x + 2, y + 2);
            break;
        default: /* Sentinel: security post / anomaly seal / lockout barrier. */
            duel_fb_desk_vline(fb, is_left, x, y - 8, y + 1);
            duel_fb_desk_hline(fb, is_left, x - 2, x + 2, y + 1);
            draw_courier_floor_mark(fb, is_left, kind, floor, x, y);
            if (life == DUEL_CIVIC_VISIT_AGING) {
                duel_fb_px(fb, cx - 3, y + 1, true); duel_fb_px(fb, cx + 3, y + 1, true);
            }
            return;
    }

    draw_courier_floor_mark(fb, is_left, kind, floor, x, y);

    if (density >= DUEL_CIVIC_DENSITY_FEW) {
        duel_fb_px(fb, cx - 3, y, true); duel_fb_px(fb, cx + 3, y, true);
    }
    if (density >= DUEL_CIVIC_DENSITY_MANY) {
        duel_fb_px(fb, cx - 2, y + 4, true); duel_fb_px(fb, cx + 2, y + 4, true);
    }
    if (life == DUEL_CIVIC_VISIT_ARRIVING && !quiet) {
        duel_fb_px(fb, cx - 2 * g, y + 2, true); duel_fb_px(fb, cx - 4 * g, y + 3, true);
    } else if (life == DUEL_CIVIC_VISIT_WAITING) {
        duel_fb_px(fb, cx, y + 3, true);
    } else if (life == DUEL_CIVIC_VISIT_AGING) {
        duel_fb_px(fb, cx - 1, y + 3, true); duel_fb_px(fb, cx + 1, y + 4, true);
    } else if (life == DUEL_CIVIC_VISIT_RESOLVING && !quiet) {
        duel_fb_px(fb, cx + 3 * g, y, true); duel_fb_px(fb, cx + 4 * g, y - 1, true);
    }
}
