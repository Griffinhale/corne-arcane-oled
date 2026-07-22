/*
 * duel_resident.c — resident engine (see duel_resident.h).
 *
 * Pure, allocation-free derivation and drawing of one city's resident.
 */
#include "duel_resident.h"
#include "duel_host.h"

// Small deterministic byte hash (FNV-1a flavoured). The only randomness source
// for the resident, keyed strictly by presentation inputs — never by w.tick.
static uint8_t resident_hash(uint8_t a, uint8_t b, uint8_t c) {
    uint32_t h = 2166136261u;
    h = (h ^ a) * 16777619u;
    h = (h ^ b) * 16777619u;
    h = (h ^ c) * 16777619u;
    h ^= h >> 13;
    h *= 16777619u;
    h ^= h >> 15;
    return (uint8_t)h;
}

uint8_t civic_resident_personality(uint8_t seed, bool is_left) {
    return (uint8_t)(resident_hash(seed, is_left ? 1u : 0u, 0xA5u) % DUEL_CIVIC_PERSONALITY_COUNT);
}

// Ambient action weights per personality, ordered by DUEL_CIVIC_ACTION_*:
// WORK, WALK, INSPECT, REST, WATCH_ROOF, HANDLE_DELIVERY, REACT. Event-driven
// actions (HANDLE_DELIVERY, REACT) keep a small ambient weight so the vocabulary
// is exercised; event-driven presentation can force them from couriers/combat.
static const uint8_t action_weights[DUEL_CIVIC_PERSONALITY_COUNT][DUEL_CIVIC_ACTION_COUNT] = {
    /* DILIGENT   */ {8, 2, 4, 1, 1, 2, 1},
    /* CURIOUS    */ {2, 5, 7, 1, 4, 2, 1},
    /* NERVOUS    */ {2, 4, 2, 1, 6, 2, 3},
    /* PROUD      */ {6, 2, 3, 1, 5, 1, 1},
    /* DISTRACTED */ {1, 5, 3, 6, 3, 1, 1},
};

static uint8_t pick_action(uint8_t seed, bool is_left, uint8_t personality, uint8_t slot) {
    const uint8_t *w = action_weights[personality];
    uint16_t total = 0;
    for (int i = 0; i < DUEL_CIVIC_ACTION_COUNT; i++)
        total = (uint16_t)(total + w[i]);
    uint8_t rnd =
        (uint8_t)(resident_hash(seed, (uint8_t)(is_left ? 0x11u : 0x22u), (uint8_t)(slot + 1u)) %
                  total);
    uint16_t acc = 0;
    for (int i = 0; i < DUEL_CIVIC_ACTION_COUNT; i++) {
        acc = (uint16_t)(acc + w[i]);
        if (rnd < acc)
            return (uint8_t)i;
    }
    return DUEL_CIVIC_ACTION_WORK;
}

// QUIET calms the resident: energetic actions collapse to stationary ones so
// motion visibly reduces without changing which floor is shown.
static uint8_t quiet_remap(uint8_t action) {
    switch (action) {
        case DUEL_CIVIC_ACTION_WALK:
            return DUEL_CIVIC_ACTION_REST;
        case DUEL_CIVIC_ACTION_REACT:
            return DUEL_CIVIC_ACTION_INSPECT;
        case DUEL_CIVIC_ACTION_WATCH_ROOF:
            return DUEL_CIVIC_ACTION_WORK;
        default:
            return action;
    }
}

civic_resident_t civic_resident_derive(uint8_t seed, bool is_left, uint8_t floor, uint8_t mode,
                                       uint8_t phase) {
    civic_resident_t res;
    res.personality = civic_resident_personality(seed, is_left);
    uint8_t slot = (uint8_t)(phase / DUEL_CIVIC_ACTION_SLOT);
    res.action = pick_action(seed, is_left, res.personality, slot);
    if (mode == DUEL_CIVIC_MODE_QUIET)
        res.action = quiet_remap(res.action);
    if (floor >= INCANTATION_OCCUPATION_FLOORS)
        floor = DUEL_CIVIC_FLOOR_COMMONS;
    res.station = INCANTATION_OCCUPATION_KEY(floor, res.action);
    res.progress = (uint8_t)(phase % DUEL_CIVIC_ACTION_SLOT);
    res.task = RESIDENT_NORMAL;
    return res;
}

enum {
    INCANTATION_POSE_WORK = 0,
    INCANTATION_POSE_CARRY,
    INCANTATION_POSE_INSPECT,
    INCANTATION_POSE_SEATED,
    INCANTATION_POSE_WATCH,
    INCANTATION_POSE_EXCHANGE,
    INCANTATION_POSE_REACT,
};

enum {
    INCANTATION_MARK_NONE = 0,
    INCANTATION_MARK_DISPATCH,
    INCANTATION_MARK_NOTES,
    INCANTATION_MARK_SPECIMEN,
    INCANTATION_MARK_TOOL,
    INCANTATION_MARK_PARCEL,
    INCANTATION_MARK_LEDGER,
};

enum {
    INCANTATION_OBJECT_COMMONS_TABLE = 0,
    INCANTATION_OBJECT_COMMONS_BOARD,
    INCANTATION_OBJECT_COMMONS_CLOCK,
    INCANTATION_OBJECT_RESEARCH_SCOPE,
    INCANTATION_OBJECT_RESEARCH_CABINET,
    INCANTATION_OBJECT_RESEARCH_LOG,
    INCANTATION_OBJECT_WORKSHOP_FORGE,
    INCANTATION_OBJECT_WORKSHOP_RACK,
    INCANTATION_OBJECT_WORKSHOP_GAUGE,
    INCANTATION_OBJECT_OBSERVATORY_SCOPE,
    INCANTATION_OBJECT_OBSERVATORY_CHART,
    INCANTATION_OBJECT_OBSERVATORY_DOME,
    INCANTATION_OBJECT_SCRIPTORIUM_LECTERN,
    INCANTATION_OBJECT_SCRIPTORIUM_RACK,
    INCANTATION_OBJECT_SCRIPTORIUM_INDEX,
    INCANTATION_OBJECT_STUDIO_STAGE,
    INCANTATION_OBJECT_STUDIO_MIXER,
    INCANTATION_OBJECT_STUDIO_REEL,
};

/* One compact descriptor per (floor, action). `station` is the resident's
 * desk-space centre; the remaining bytes supply the floor-specific pose,
 * carried semantic, and the existing object that reacts over four subphases. */
typedef struct {
    uint8_t station;
    uint8_t pose;
    uint8_t carried;
    uint8_t reaction;
} incantation_occupation_desc_t;

static const incantation_occupation_desc_t incantation_occupations[INCANTATION_OCCUPATION_FLOORS *
                                                                   DUEL_CIVIC_ACTION_COUNT] = {
    /* Commons: sort, carry dispatch, board, tea/table, clock, file, urgent. */
    {18, INCANTATION_POSE_WORK, INCANTATION_MARK_DISPATCH, INCANTATION_OBJECT_COMMONS_TABLE},
    {16, INCANTATION_POSE_CARRY, INCANTATION_MARK_DISPATCH, INCANTATION_OBJECT_COMMONS_TABLE},
    {21, INCANTATION_POSE_INSPECT, INCANTATION_MARK_DISPATCH, INCANTATION_OBJECT_COMMONS_BOARD},
    {16, INCANTATION_POSE_SEATED, INCANTATION_MARK_NONE, INCANTATION_OBJECT_COMMONS_TABLE},
    {18, INCANTATION_POSE_WATCH, INCANTATION_MARK_NONE, INCANTATION_OBJECT_COMMONS_CLOCK},
    {21, INCANTATION_POSE_EXCHANGE, INCANTATION_MARK_PARCEL, INCANTATION_OBJECT_COMMONS_BOARD},
    {18, INCANTATION_POSE_REACT, INCANTATION_MARK_DISPATCH, INCANTATION_OBJECT_COMMONS_BOARD},
    /* Research: scope, notes/specimen, cabinet, log, reading, transfer, anomaly. */
    {18, INCANTATION_POSE_WORK, INCANTATION_MARK_NOTES, INCANTATION_OBJECT_RESEARCH_SCOPE},
    {16, INCANTATION_POSE_CARRY, INCANTATION_MARK_SPECIMEN, INCANTATION_OBJECT_RESEARCH_LOG},
    {21, INCANTATION_POSE_INSPECT, INCANTATION_MARK_SPECIMEN, INCANTATION_OBJECT_RESEARCH_CABINET},
    {17, INCANTATION_POSE_SEATED, INCANTATION_MARK_LEDGER, INCANTATION_OBJECT_RESEARCH_LOG},
    {18, INCANTATION_POSE_WATCH, INCANTATION_MARK_NONE, INCANTATION_OBJECT_RESEARCH_SCOPE},
    {21, INCANTATION_POSE_EXCHANGE, INCANTATION_MARK_SPECIMEN, INCANTATION_OBJECT_RESEARCH_CABINET},
    {18, INCANTATION_POSE_REACT, INCANTATION_MARK_SPECIMEN, INCANTATION_OBJECT_RESEARCH_SCOPE},
    /* Workshop: forge/press, parts, rack, bench, gauge, hoist, jam/spark. */
    {18, INCANTATION_POSE_WORK, INCANTATION_MARK_TOOL, INCANTATION_OBJECT_WORKSHOP_FORGE},
    {16, INCANTATION_POSE_CARRY, INCANTATION_MARK_TOOL, INCANTATION_OBJECT_WORKSHOP_GAUGE},
    {21, INCANTATION_POSE_INSPECT, INCANTATION_MARK_TOOL, INCANTATION_OBJECT_WORKSHOP_RACK},
    {17, INCANTATION_POSE_SEATED, INCANTATION_MARK_NONE, INCANTATION_OBJECT_WORKSHOP_FORGE},
    {18, INCANTATION_POSE_WATCH, INCANTATION_MARK_NONE, INCANTATION_OBJECT_WORKSHOP_GAUGE},
    {21, INCANTATION_POSE_EXCHANGE, INCANTATION_MARK_PARCEL, INCANTATION_OBJECT_WORKSHOP_RACK},
    {18, INCANTATION_POSE_REACT, INCANTATION_MARK_TOOL, INCANTATION_OBJECT_WORKSHOP_FORGE},
    /* Observatory: stargaze, carry chart, inspect scope, log, watch dome. */
    {18, INCANTATION_POSE_WORK, INCANTATION_MARK_NOTES, INCANTATION_OBJECT_OBSERVATORY_SCOPE},
    {16, INCANTATION_POSE_CARRY, INCANTATION_MARK_LEDGER, INCANTATION_OBJECT_OBSERVATORY_CHART},
    {21, INCANTATION_POSE_INSPECT, INCANTATION_MARK_SPECIMEN, INCANTATION_OBJECT_OBSERVATORY_CHART},
    {17, INCANTATION_POSE_SEATED, INCANTATION_MARK_LEDGER, INCANTATION_OBJECT_OBSERVATORY_SCOPE},
    {18, INCANTATION_POSE_WATCH, INCANTATION_MARK_NONE, INCANTATION_OBJECT_OBSERVATORY_DOME},
    {21, INCANTATION_POSE_EXCHANGE, INCANTATION_MARK_NOTES, INCANTATION_OBJECT_OBSERVATORY_CHART},
    {18, INCANTATION_POSE_REACT, INCANTATION_MARK_SPECIMEN, INCANTATION_OBJECT_OBSERVATORY_DOME},
    /* Scriptorium: lectern/quill, scroll rack, index and copy-desk work. */
    {15, INCANTATION_POSE_WORK, INCANTATION_MARK_NOTES, INCANTATION_OBJECT_SCRIPTORIUM_LECTERN},
    {17, INCANTATION_POSE_CARRY, INCANTATION_MARK_LEDGER, INCANTATION_OBJECT_SCRIPTORIUM_RACK},
    {23, INCANTATION_POSE_INSPECT, INCANTATION_MARK_NOTES, INCANTATION_OBJECT_SCRIPTORIUM_INDEX},
    {16, INCANTATION_POSE_SEATED, INCANTATION_MARK_LEDGER, INCANTATION_OBJECT_SCRIPTORIUM_LECTERN},
    {18, INCANTATION_POSE_WATCH, INCANTATION_MARK_NONE, INCANTATION_OBJECT_SCRIPTORIUM_RACK},
    {22, INCANTATION_POSE_EXCHANGE, INCANTATION_MARK_PARCEL, INCANTATION_OBJECT_SCRIPTORIUM_INDEX},
    {18, INCANTATION_POSE_REACT, INCANTATION_MARK_NOTES, INCANTATION_OBJECT_SCRIPTORIUM_LECTERN},
    /* Studio: resonance stage, mixer/projector, and reel handling. */
    {15, INCANTATION_POSE_WORK, INCANTATION_MARK_TOOL, INCANTATION_OBJECT_STUDIO_STAGE},
    {17, INCANTATION_POSE_CARRY, INCANTATION_MARK_PARCEL, INCANTATION_OBJECT_STUDIO_REEL},
    {23, INCANTATION_POSE_INSPECT, INCANTATION_MARK_TOOL, INCANTATION_OBJECT_STUDIO_MIXER},
    {16, INCANTATION_POSE_SEATED, INCANTATION_MARK_NONE, INCANTATION_OBJECT_STUDIO_MIXER},
    {18, INCANTATION_POSE_WATCH, INCANTATION_MARK_NONE, INCANTATION_OBJECT_STUDIO_STAGE},
    {22, INCANTATION_POSE_EXCHANGE, INCANTATION_MARK_PARCEL, INCANTATION_OBJECT_STUDIO_REEL},
    {18, INCANTATION_POSE_REACT, INCANTATION_MARK_TOOL, INCANTATION_OBJECT_STUDIO_STAGE},
};

static const incantation_occupation_desc_t *incantation_occupation(uint8_t key) {
    if (key >= sizeof incantation_occupations / sizeof incantation_occupations[0])
        key = INCANTATION_OCCUPATION_KEY(DUEL_CIVIC_FLOOR_COMMONS, DUEL_CIVIC_ACTION_WORK);
    return &incantation_occupations[key];
}

uint8_t incantation_effective_district(const duel_render_t *r) {
    uint8_t district = duel_render_district(r);
    if (INCANTATION_FLOOR_TRANSITION_ACTIVE(r->floor_transition) &&
        INCANTATION_FLOOR_TRANSITION_PHASE(r->floor_transition) < 2u)
        district = INCANTATION_FLOOR_TRANSITION_SOURCE(r->floor_transition);
    return district;
}

uint8_t incantation_effective_floor(const duel_render_t *r) {
    return duel_district_floor(incantation_effective_district(r));
}

/* Desk-space anchor of each floor object (INCANTATION_OBJECT_*, 3 per floor).
 * The occupation anchor and the object-reaction sparkle both index this table,
 * keyed by the occupation's reaction object. */
static const int8_t incantation_object_anchors[18][2] = {
    {14, 95}, {24, 82}, {11, 88}, {14, 79}, {24, 88}, {13, 94}, {14, 91}, {24, 90}, {11, 82},
    {13, 88}, {24, 84}, {16, 82}, {13, 91}, {25, 87}, {22, 76}, {14, 88}, {24, 82}, {27, 96},
};

incantation_point_t incantation_occupation_anchor(uint8_t floor, uint8_t action) {
    if (floor >= INCANTATION_OCCUPATION_FLOORS)
        floor = DUEL_CIVIC_FLOOR_COMMONS;
    if (action >= DUEL_CIVIC_ACTION_COUNT)
        action = DUEL_CIVIC_ACTION_WORK;
    const incantation_occupation_desc_t *desc =
        incantation_occupation(INCANTATION_OCCUPATION_KEY(floor, action));
    return (incantation_point_t){incantation_object_anchors[desc->reaction][0],
                                 incantation_object_anchors[desc->reaction][1]};
}

static void incantation_draw_object_reaction(duel_fb_t *fb, uint8_t reaction, bool is_left,
                                             uint8_t progress) {
    static const int8_t phase_pixels[4][3][2] = {
        {{0, 0}, {0, 0}, {0, 0}},
        {{0, 0}, {1, -1}, {1, -1}},
        {{0, -1}, {0, 1}, {0, 1}},
        {{-1, 0}, {1, 0}, {0, -1}},
    };
    /* The reaction IS the object index: anchor it directly (the old
     * reaction -> action -> occupation -> reaction round-trip was identity). */
    int x = duel_fb_desk_x(is_left, incantation_object_anchors[reaction % 18u][0]);
    int y = incantation_object_anchors[reaction % 18u][1];
    int toward_gap = is_left ? 1 : -1;
    const int8_t(*pixels)[2] = phase_pixels[(progress >> 2) & 3u];
    for (int i = 0; i < 3; i++)
        duel_fb_px(fb, x + pixels[i][0] * toward_gap, y + pixels[i][1], true);
}

static void incantation_draw_carried(duel_fb_t *fb, uint8_t mark, int x, int y, int gapward) {
    static const int8_t mark_pixels[6][4][2] = {
        {{0, 0}, {0, 1}, {1, 0}, {0, 0}},   /* dispatch */
        {{0, 0}, {0, 1}, {1, 1}, {0, 0}},   /* notes */
        {{0, -1}, {-1, 0}, {1, 0}, {0, 1}}, /* specimen */
        {{-1, 1}, {0, 0}, {1, -1}, {0, 0}}, /* tool */
        {{0, 0}, {1, 0}, {0, 1}, {1, 1}},   /* parcel */
        {{0, 0}, {1, 0}, {0, 2}, {1, 2}},   /* ledger */
    };
    if (mark == INCANTATION_MARK_NONE)
        return;
    x += 3 * gapward;
    const int8_t(*pixels)[2] = mark_pixels[mark - 1u];
    for (int i = 0; i < 4; i++)
        duel_fb_px(fb, x + pixels[i][0] * gapward, y + pixels[i][1], true);
}

static void incantation_draw_core(duel_fb_t *fb, int cx, int fy, bool seated) {
    int top = fy - (seated ? 11 : 13);
    duel_fb_hline(fb, cx - 1, cx + 1, top);
    duel_fb_hline(fb, cx - 2, cx + 2, top + 1);
    duel_fb_hline(fb, cx - 2, cx + 2, top + 2);
    duel_fb_hline(fb, cx - 1, cx + 1, top + 3);
    duel_fb_hline(fb, cx - 2, cx + 2, top + 4); /* shoulders */
    int torso_end = top + (seated ? 8 : 9);
    for (int y = top + 5; y <= torso_end; y++)
        duel_fb_hline(fb, cx - 1, cx + 1, y);
    int hips = top + (seated ? 9 : 10);
    duel_fb_hline(fb, cx - 2, cx + 2, hips);
    if (seated) {
        duel_fb_px(fb, cx - 2, top + 10, true);
        duel_fb_px(fb, cx + 2, top + 10, true);
        duel_fb_px(fb, cx - 2, fy, true);
        duel_fb_px(fb, cx - 1, fy, true);
        duel_fb_px(fb, cx + 1, fy, true);
        duel_fb_px(fb, cx + 2, fy, true);
    } else {
        for (int y = top + 11; y <= top + 12; y++) {
            duel_fb_px(fb, cx - 1, y, true);
            duel_fb_px(fb, cx + 1, y, true);
        }
        duel_fb_hline(fb, cx - 2, cx + 2, fy);
    }
}

void civic_resident_draw(duel_fb_t *fb, const civic_resident_t *res, bool is_left, uint8_t mode,
                         uint32_t frame) {
    (void)mode;
    (void)frame;
    const incantation_occupation_desc_t *desc = incantation_occupation(res->station);
    int gapward = is_left ? 1 : -1;
    int cx = duel_fb_desk_x(is_left, desc->station);
    int fy = desc->pose == INCANTATION_POSE_CARRY    ? 107
             : desc->pose == INCANTATION_POSE_SEATED ? 107
                                                     : 105;
    bool ordinary = res->task == RESIDENT_NORMAL;
    bool seated = ordinary && desc->pose == INCANTATION_POSE_SEATED;
    int top = fy - (seated ? 11 : 13);
    int shoulders = top + 4;

    incantation_draw_core(fb, cx, fy, seated);

    if (ordinary) {
        switch (desc->pose) {
            case INCANTATION_POSE_WORK:
                duel_fb_px(fb, cx + 2 * gapward, shoulders + 2, true);
                duel_fb_px(fb, cx + 3 * gapward, shoulders + 2, true);
                break;
            case INCANTATION_POSE_CARRY:
                duel_fb_px(fb, cx + 2 * gapward, shoulders + 2, true);
                duel_fb_px(fb, cx + 2 * gapward, shoulders + 3, true);
                break;
            case INCANTATION_POSE_INSPECT:
                duel_fb_px(fb, cx + 2 * gapward, top + 2, true);
                duel_fb_px(fb, cx + 3 * gapward, top + 1, true);
                break;
            case INCANTATION_POSE_SEATED:
                duel_fb_px(fb, cx + 2 * gapward, shoulders + 2, true);
                break;
            case INCANTATION_POSE_WATCH:
                duel_fb_px(fb, cx + 2 * gapward, top + 1, true);
                duel_fb_px(fb, cx + 3 * gapward, top - 1, true);
                duel_fb_px(fb, cx + 3 * gapward, top - 3, true);
                break;
            case INCANTATION_POSE_EXCHANGE:
                duel_fb_px(fb, cx + 2 * gapward, shoulders + 1, true);
                duel_fb_px(fb, cx + 3 * gapward, shoulders + 1, true);
                break;
            case INCANTATION_POSE_REACT:
                duel_fb_px(fb, cx - 3, top + 2, true);
                duel_fb_px(fb, cx + 3, top + 2, true);
                duel_fb_px(fb, cx, top - 2, true);
                break;
        }
        incantation_draw_carried(fb, desc->carried, cx, shoulders + 2, gapward);
        incantation_draw_object_reaction(fb, desc->reaction, is_left, res->progress);

        /* Personality marks are re-anchored to the enlarged crown and yield
         * completely to authoritative aftermath hats. */
        switch (res->personality) {
            case DUEL_CIVIC_PERSONALITY_CURIOUS:
                duel_fb_px(fb, cx, top - 1, true);
                break;
            case DUEL_CIVIC_PERSONALITY_NERVOUS:
                duel_fb_px(fb, cx - gapward * 3, top + 2, true);
                break;
            case DUEL_CIVIC_PERSONALITY_PROUD:
                duel_fb_px(fb, cx - 1, top - 1, true);
                duel_fb_px(fb, cx + 1, top - 1, true);
                break;
            case DUEL_CIVIC_PERSONALITY_DISTRACTED:
                duel_fb_px(fb, cx + gapward * 3, top - 2, true);
                break;
            default:
                break;
        }
        return;
    }

    /* Aftermath task hats/gestures have priority and may rise three pixels
     * above the crown, but never revive an ordinary occupation reaction. */
    switch (res->task) {
        case RESIDENT_CHEER:
            duel_fb_px(fb, cx - 2, top - 1, true);
            duel_fb_px(fb, cx, top - 3, true);
            duel_fb_px(fb, cx + 2, top - 1, true);
            break;
        case RESIDENT_COMPLAIN:
            duel_fb_hline(fb, cx - 2, cx + 2, top - 1);
            duel_fb_px(fb, cx + 3 * gapward, top - 2, true);
            break;
        case RESIDENT_PANIC:
            duel_fb_px(fb, cx - 3, top + 2, true);
            duel_fb_px(fb, cx + 3, top + 2, true);
            duel_fb_px(fb, cx, top - 3, true);
            break;
        case RESIDENT_FIGHT_FIRE:
            duel_fb_hline(fb, cx - 2, cx + 2, top - 1);
            duel_fb_px(fb, cx, top - 2, true);
            break;
        case RESIDENT_INSPECT:
            duel_fb_px(fb, cx - 1, top - 1, true);
            duel_fb_px(fb, cx, top - 2, true);
            duel_fb_px(fb, cx + 1, top - 1, true);
            duel_fb_px(fb, cx + 3 * gapward, top + 2, true);
            break;
        case RESIDENT_REPAIR:
            duel_fb_hline(fb, cx - 2, cx + 2, top - 1);
            duel_fb_px(fb, cx - 1, top - 2, true);
            duel_fb_px(fb, cx + 1, top - 2, true);
            break;
        case RESIDENT_WATCH_CAST:
            duel_fb_px(fb, cx, top - 2, true);
            duel_fb_px(fb, cx - 2, top - 3, true);
            duel_fb_px(fb, cx + 2, top - 3, true);
            break;
        case RESIDENT_DIPLO_PROUD:
            duel_fb_px(fb, cx - 2, top - 1, true);
            duel_fb_px(fb, cx, top - 3, true);
            duel_fb_px(fb, cx + 2, top - 1, true);
            duel_fb_px(fb, cx - gapward * 3, shoulders + 2, true);
            break;
        case RESIDENT_DIPLO_RECEIVING:
            duel_fb_px(fb, cx + 2 * gapward, shoulders + 1, true);
            duel_fb_px(fb, cx + 3 * gapward, shoulders + 1, true);
            duel_fb_hline(fb, cx - 1, cx + 1, top - 1);
            break;
        case RESIDENT_DIPLO_NEUTRAL:
            duel_fb_px(fb, cx - 3, shoulders + 2, true);
            duel_fb_px(fb, cx + 3, shoulders + 2, true);
            break;
        default:
            break;
    }
}

void incantation_resident_draw_attunement(duel_fb_t *fb, bool is_left, uint8_t floor) {
    incantation_point_t anchor = incantation_occupation_anchor(floor, DUEL_CIVIC_ACTION_WORK);
    duel_fb_px(fb, duel_fb_desk_x(is_left, anchor.x), anchor.y, true);
}
