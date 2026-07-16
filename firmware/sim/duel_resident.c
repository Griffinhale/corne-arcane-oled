/*
 * duel_resident.c — M12 resident engine (see duel_resident.h).
 *
 * Pure, allocation-free derivation and drawing of one city's resident. Under
 * !ARCANE_M12 this is an empty translation unit (the typedef below keeps ISO C
 * happy) so the accepted M11.5 release stays byte-identical.
 */
typedef int duel_resident_translation_unit_not_empty;

#ifdef ARCANE_M12

#include "duel_resident.h"
#include "duel_host.h"

// Small deterministic byte hash (FNV-1a flavoured). The only randomness source
// for the resident, keyed strictly by presentation inputs — never by w.tick.
static uint8_t rez_hash(uint8_t a, uint8_t b, uint8_t c) {
    uint32_t h = 2166136261u;
    h = (h ^ a) * 16777619u;
    h = (h ^ b) * 16777619u;
    h = (h ^ c) * 16777619u;
    h ^= h >> 13;
    h *= 16777619u;
    h ^= h >> 15;
    return (uint8_t)h;
}

uint8_t m12_resident_personality(uint8_t seed, bool is_left) {
    return (uint8_t)(rez_hash(seed, is_left ? 1u : 0u, 0xA5u) % DUEL_M12_PERSONALITY_COUNT);
}

// Ambient action weights per personality, ordered by DUEL_M12_ACTION_*:
// WORK, WALK, INSPECT, REST, WATCH_ROOF, HANDLE_DELIVERY, REACT. Event-driven
// actions (HANDLE_DELIVERY, REACT) keep a small ambient weight so the vocabulary
// is exercised; Waves 4/6 later force them from real couriers/combat.
static const uint8_t action_weights[DUEL_M12_PERSONALITY_COUNT][DUEL_M12_ACTION_COUNT] = {
    /* DILIGENT   */ {8, 2, 4, 1, 1, 2, 1},
    /* CURIOUS    */ {2, 5, 7, 1, 4, 2, 1},
    /* NERVOUS    */ {2, 4, 2, 1, 6, 2, 3},
    /* PROUD      */ {6, 2, 3, 1, 5, 1, 1},
    /* DISTRACTED */ {1, 5, 3, 6, 3, 1, 1},
};

static uint8_t pick_action(uint8_t seed, bool is_left, uint8_t personality, uint8_t slot) {
    const uint8_t *w = action_weights[personality];
    uint16_t total = 0;
    for (int i = 0; i < DUEL_M12_ACTION_COUNT; i++) total = (uint16_t)(total + w[i]);
    uint8_t rnd = (uint8_t)(rez_hash(seed, (uint8_t)(is_left ? 0x11u : 0x22u), (uint8_t)(slot + 1u)) % total);
    uint16_t acc = 0;
    for (int i = 0; i < DUEL_M12_ACTION_COUNT; i++) {
        acc = (uint16_t)(acc + w[i]);
        if (rnd < acc) return (uint8_t)i;
    }
    return DUEL_M12_ACTION_WORK;
}

// QUIET calms the resident: energetic actions collapse to stationary ones so
// motion visibly reduces without changing which floor is shown.
static uint8_t quiet_remap(uint8_t action) {
    switch (action) {
        case DUEL_M12_ACTION_WALK:       return DUEL_M12_ACTION_REST;
        case DUEL_M12_ACTION_REACT:      return DUEL_M12_ACTION_INSPECT;
        case DUEL_M12_ACTION_WATCH_ROOF: return DUEL_M12_ACTION_WORK;
        default:                         return action;
    }
}

m12_resident_t m12_resident_derive(uint8_t seed, bool is_left, uint8_t floor,
                                   uint8_t mode, uint8_t phase) {
    m12_resident_t res;
    res.personality = m12_resident_personality(seed, is_left);
    uint8_t slot = (uint8_t)(phase / DUEL_M12_ACTION_SLOT);
    res.action   = pick_action(seed, is_left, res.personality, slot);
    if (mode == DUEL_M12_MODE_QUIET) res.action = quiet_remap(res.action);
#ifdef ARCANE_M13
    if (floor >= M13_OCCUPATION_FLOORS) floor = DUEL_M12_FLOOR_COMMONS;
    res.station  = M13_OCCUPATION_KEY(floor, res.action);
#else
    (void)floor; // stations are action-relative; the archetype styles the room.
    res.station  = res.action; // one fixed station per action kind
#endif
    res.progress = (uint8_t)(phase % DUEL_M12_ACTION_SLOT);
#ifdef ARCANE_M13
    res.task = RESIDENT_NORMAL;
#endif
    return res;
}

#ifndef ARCANE_M13
// Fixed station anchors in desk space (x with the gap at 31; mirrored on the
// right OLED by the caller's FLR macro). feet_y is the ground the figure stands
// on; the ~8 px figure rises to feet_y-7, staying inside the floor band.
typedef struct { int8_t x; uint8_t feet_y; } rez_station_t;
// Feet stand on/near the ground line (y110); x spots keep clear of the tall
// gap-side cabinet (x23-28) so a working resident reads as standing AT the
// furniture, not boxed inside it.
static const rez_station_t stations[DUEL_M12_ACTION_COUNT] = {
    /* WORK            */ { 6, 105},   // at the outer counter
    /* WALK            */ {15, 107},   // crossing the open floor
    /* INSPECT         */ {21, 105},   // in front of the cabinet
    /* REST            */ { 4, 107},   // seated by the counter
    /* WATCH_ROOF      */ {13, 105},   // centre, gazing up
    /* HANDLE_DELIVERY */ {22, 105},   // gap-side, passing to the cabinet/lift
    /* REACT           */ {16, 105},   // centre
};

void m12_resident_draw(duel_fb_t *fb, const m12_resident_t *res, bool is_left,
                       uint8_t mode, uint32_t frame) {
    (void)frame; // resident advances on the civic phase only (plan §2 D3); the
                 // ACTIVE-window sub-motion hook is a Wave-4 activity coupling.
#define RZX(x) (is_left ? (x) : (DUEL_CANVAS_W - 1 - (x)))
    const rez_station_t st = stations[res->station];
    const int f  = is_left ? +1 : -1;         // toward the gap
    const int cx = RZX(st.x);
    const int fy = st.feet_y;
    const int gapward = f;                      // +x on left canvas points at gap

    // A standing person: round head, neck, a solid shoulder line, spine, and
    // split legs. Scaled up from the first pass (hardware feedback: read too
    // small) to ~10px, still clearly under the champion and unlike the
    // filled-rectangle furniture.
    int head_y = fy - 8;
    bool rest = res->action == DUEL_M12_ACTION_REST;
    if (rest) head_y = fy - 7; // seated/hunched: whole figure settles one row

    duel_fb_px(fb, cx, head_y - 1, true);            // crown
    for (int x = cx - 1; x <= cx + 1; x++) {         // head (3x2, rounder)
        duel_fb_px(fb, x, head_y, true);
        duel_fb_px(fb, x, head_y + 1, true);
    }
    duel_fb_px(fb, cx, head_y + 2, true);            // neck

    // Solid 3-wide shoulder line, then a spine down to the hips.
    int arm_y = head_y + 3;
    duel_fb_px(fb, cx - 1, arm_y, true);
    duel_fb_px(fb, cx,     arm_y, true);
    duel_fb_px(fb, cx + 1, arm_y, true);
    for (int y = arm_y + 1; y <= fy - 2; y++)        // spine/torso
        duel_fb_px(fb, cx, y, true);

    // Legs / feet.
    if (rest) {
        // Folded: knees out, feet tucked under.
        duel_fb_px(fb, cx - 1, fy - 1, true);
        duel_fb_px(fb, cx + 1, fy - 1, true);
        duel_fb_px(fb, cx,     fy, true);
    } else {
        duel_fb_px(fb, cx - 1, fy - 1, true);
        duel_fb_px(fb, cx + 1, fy - 1, true);
        duel_fb_px(fb, cx - 1, fy, true);
        duel_fb_px(fb, cx + 1, fy, true);
    }

    // Action-specific pose overlay. Motion between actions (station + pose)
    // is what makes the resident read as alive; it advances on the civic tick.
    switch (res->action) {
        case DUEL_M12_ACTION_WORK:
            // Both hands over a workbench toward the gap-side, with a task spark.
            duel_fb_px(fb, cx + gapward, arm_y + 1, true);
            duel_fb_px(fb, cx + 2 * gapward, arm_y + 1, true);
            if (res->progress & 1u) duel_fb_px(fb, cx + 2 * gapward, arm_y, true);
            break;
        case DUEL_M12_ACTION_WALK:
            // Wider gait plus a forward-swinging arm.
            duel_fb_px(fb, cx + 2 * gapward, fy, true);
            duel_fb_px(fb, cx - 2 * gapward, fy - 1, true);
            duel_fb_px(fb, cx + gapward, arm_y, true);
            break;
        case DUEL_M12_ACTION_INSPECT:
            // One hand raised holding something up to the light.
            duel_fb_px(fb, cx + gapward, head_y, true);
            duel_fb_px(fb, cx + gapward, head_y - 1, true);
            break;
        case DUEL_M12_ACTION_REST:
            // Calm: a slow breath dot above the head on odd phases.
            if (res->progress & 2u) duel_fb_px(fb, cx, head_y - 2, true);
            break;
        case DUEL_M12_ACTION_WATCH_ROOF: {
            // Gaze up toward the rooftop champion: a raised arm and a short
            // upward glance (three rising dots), not a full-height sight-line.
            duel_fb_px(fb, cx + gapward, head_y - 1, true);
            duel_fb_px(fb, cx + gapward, head_y - 3, true);
            duel_fb_px(fb, cx + gapward, head_y - 5, true);
            break;
        }
        case DUEL_M12_ACTION_HANDLE_DELIVERY:
            // Arms forward cradling a small parcel toward the gap-side lift.
            duel_fb_px(fb, cx + gapward, arm_y, true);
            duel_fb_px(fb, cx + 2 * gapward, arm_y, true);
            duel_fb_px(fb, cx + 2 * gapward, arm_y - 1, true);
            break;
        case DUEL_M12_ACTION_REACT:
            // Startle: both arms up and an exclamation mark above the head.
            duel_fb_px(fb, cx - 1, head_y, true);
            duel_fb_px(fb, cx + 1, head_y, true);
            duel_fb_px(fb, cx, head_y - 3, true);
            duel_fb_px(fb, cx, head_y - 4, true);
            break;
        default:
            break;
    }

    // Personality cosmetic marker so residents read as distinct individuals
    // even in the same action (and so each of the 5 types is legible).
    switch (res->personality) {
        case DUEL_M12_PERSONALITY_DILIGENT:
            break; // plain, no accessory
        case DUEL_M12_PERSONALITY_CURIOUS:
            duel_fb_px(fb, cx, head_y - 2, true); // inquisitive tuft
            break;
        case DUEL_M12_PERSONALITY_NERVOUS:
            duel_fb_px(fb, cx - gapward, head_y, true); // fidget to the side
            break;
        case DUEL_M12_PERSONALITY_PROUD:
            duel_fb_px(fb, cx - 1, head_y - 1, true);   // little crown
            duel_fb_px(fb, cx + 1, head_y - 1, true);
            break;
        case DUEL_M12_PERSONALITY_DISTRACTED:
            duel_fb_px(fb, cx + 2 * gapward, head_y - 2, true); // drifting thought
            break;
        default:
            break;
    }

#ifdef ARCANE_M13
    /* Aftermath task hats are deliberately bolder than personality marks: the
     * same resident visibly changes jobs rather than being replaced by a
     * generic reaction glyph. */
    switch (res->task) {
        case RESIDENT_CHEER:
            duel_fb_px(fb, cx - 1, head_y - 2, true);
            duel_fb_px(fb, cx, head_y - 4, true);
            duel_fb_px(fb, cx + 1, head_y - 3, true);
            break;
        case RESIDENT_COMPLAIN:
            duel_fb_px(fb, cx - 2, head_y - 2, true);
            duel_fb_px(fb, cx - 1, head_y - 2, true);
            duel_fb_px(fb, cx, head_y - 2, true);
            duel_fb_px(fb, cx + 3 * gapward, head_y - 3, true);
            break;
        case RESIDENT_PANIC:
            duel_fb_px(fb, cx, head_y - 3, true);
            duel_fb_px(fb, cx, head_y - 5, true);
            break;
        case RESIDENT_FIGHT_FIRE:
            for (int dx = -2; dx <= 2; dx++) duel_fb_px(fb, cx + dx, head_y - 2, true);
            duel_fb_px(fb, cx - 1, head_y - 3, true);
            duel_fb_px(fb, cx, head_y - 4, true);
            duel_fb_px(fb, cx + 1, head_y - 3, true);
            break;
        case RESIDENT_INSPECT:
            duel_fb_px(fb, cx - 1, head_y - 2, true);
            duel_fb_px(fb, cx, head_y - 3, true);
            duel_fb_px(fb, cx + 1, head_y - 2, true);
            duel_fb_px(fb, cx + 2 * gapward, head_y, true);
            break;
        case RESIDENT_REPAIR:
            for (int dx = -2; dx <= 2; dx++) duel_fb_px(fb, cx + dx, head_y - 2, true);
            duel_fb_px(fb, cx - 1, head_y - 3, true);
            duel_fb_px(fb, cx, head_y - 3, true);
            duel_fb_px(fb, cx + 1, head_y - 3, true);
            break;
        case RESIDENT_WATCH_CAST:
            duel_fb_px(fb, cx, head_y - 3, true);
            duel_fb_px(fb, cx - 2, head_y - 5, true);
            duel_fb_px(fb, cx + 2, head_y - 5, true);
            break;
        default:
            break;
    }
#endif

    (void)mode; // QUIET already collapsed the action set in m12_resident_derive.
#undef RZX
}

#else /* ARCANE_M13 */

enum {
    M13_POSE_WORK = 0,
    M13_POSE_CARRY,
    M13_POSE_INSPECT,
    M13_POSE_SEATED,
    M13_POSE_WATCH,
    M13_POSE_EXCHANGE,
    M13_POSE_REACT,
};

enum {
    M13_MARK_NONE = 0,
    M13_MARK_DISPATCH,
    M13_MARK_NOTES,
    M13_MARK_SPECIMEN,
    M13_MARK_TOOL,
    M13_MARK_PARCEL,
    M13_MARK_LEDGER,
};

enum {
    M13_OBJECT_COMMONS_TABLE = 0,
    M13_OBJECT_COMMONS_BOARD,
    M13_OBJECT_COMMONS_CLOCK,
    M13_OBJECT_RESEARCH_SCOPE,
    M13_OBJECT_RESEARCH_CABINET,
    M13_OBJECT_RESEARCH_LOG,
    M13_OBJECT_WORKSHOP_FORGE,
    M13_OBJECT_WORKSHOP_RACK,
    M13_OBJECT_WORKSHOP_GAUGE,
};

/* One compact descriptor per (floor, action). `station` is the resident's
 * desk-space centre; the remaining bytes supply the floor-specific pose,
 * carried semantic, and the existing object that reacts over four subphases. */
typedef struct {
    uint8_t station;
    uint8_t pose;
    uint8_t carried;
    uint8_t reaction;
} m13_occupation_desc_t;

static const m13_occupation_desc_t m13_occupations[M13_OCCUPATION_FLOORS *
                                                    DUEL_M12_ACTION_COUNT] = {
    /* Commons: sort, carry dispatch, board, tea/table, clock, file, urgent. */
    {18, M13_POSE_WORK,     M13_MARK_DISPATCH, M13_OBJECT_COMMONS_TABLE},
    {16, M13_POSE_CARRY,    M13_MARK_DISPATCH, M13_OBJECT_COMMONS_TABLE},
    {21, M13_POSE_INSPECT,  M13_MARK_DISPATCH, M13_OBJECT_COMMONS_BOARD},
    {16, M13_POSE_SEATED,   M13_MARK_NONE,     M13_OBJECT_COMMONS_TABLE},
    {18, M13_POSE_WATCH,    M13_MARK_NONE,     M13_OBJECT_COMMONS_CLOCK},
    {21, M13_POSE_EXCHANGE, M13_MARK_PARCEL,   M13_OBJECT_COMMONS_BOARD},
    {18, M13_POSE_REACT,    M13_MARK_DISPATCH, M13_OBJECT_COMMONS_BOARD},
    /* Research: scope, notes/specimen, cabinet, log, reading, transfer, anomaly. */
    {18, M13_POSE_WORK,     M13_MARK_NOTES,    M13_OBJECT_RESEARCH_SCOPE},
    {16, M13_POSE_CARRY,    M13_MARK_SPECIMEN, M13_OBJECT_RESEARCH_LOG},
    {21, M13_POSE_INSPECT,  M13_MARK_SPECIMEN, M13_OBJECT_RESEARCH_CABINET},
    {17, M13_POSE_SEATED,   M13_MARK_LEDGER,   M13_OBJECT_RESEARCH_LOG},
    {18, M13_POSE_WATCH,    M13_MARK_NONE,     M13_OBJECT_RESEARCH_SCOPE},
    {21, M13_POSE_EXCHANGE, M13_MARK_SPECIMEN, M13_OBJECT_RESEARCH_CABINET},
    {18, M13_POSE_REACT,    M13_MARK_SPECIMEN, M13_OBJECT_RESEARCH_SCOPE},
    /* Workshop: forge/press, parts, rack, bench, gauge, hoist, jam/spark. */
    {18, M13_POSE_WORK,     M13_MARK_TOOL,      M13_OBJECT_WORKSHOP_FORGE},
    {16, M13_POSE_CARRY,    M13_MARK_TOOL,      M13_OBJECT_WORKSHOP_GAUGE},
    {21, M13_POSE_INSPECT,  M13_MARK_TOOL,      M13_OBJECT_WORKSHOP_RACK},
    {17, M13_POSE_SEATED,   M13_MARK_NONE,      M13_OBJECT_WORKSHOP_FORGE},
    {18, M13_POSE_WATCH,    M13_MARK_NONE,      M13_OBJECT_WORKSHOP_GAUGE},
    {21, M13_POSE_EXCHANGE, M13_MARK_PARCEL,    M13_OBJECT_WORKSHOP_RACK},
    {18, M13_POSE_REACT,    M13_MARK_TOOL,      M13_OBJECT_WORKSHOP_FORGE},
};

static const m13_occupation_desc_t *m13_occupation(uint8_t key) {
    if (key >= sizeof m13_occupations / sizeof m13_occupations[0])
        key = M13_OCCUPATION_KEY(DUEL_M12_FLOOR_COMMONS, DUEL_M12_ACTION_WORK);
    return &m13_occupations[key];
}

static void m13_object_anchor(uint8_t reaction, int *x, int *y) {
    static const uint8_t anchors[][2] = {
        {14, 95}, {24, 82}, {11, 88},
        {14, 79}, {24, 88}, {13, 94},
        {14, 91}, {24, 90}, {11, 82},
    };
    if (reaction >= sizeof anchors / sizeof anchors[0]) reaction = 0;
    *x = anchors[reaction][0];
    *y = anchors[reaction][1];
}

static void m13_draw_object_reaction(duel_fb_t *fb, uint8_t reaction,
                                     bool is_left, uint8_t progress) {
    static const int8_t phase_pixels[4][3][2] = {
        {{ 0,  0}, { 0,  0}, { 0,  0}},
        {{ 0,  0}, { 1, -1}, { 1, -1}},
        {{ 0, -1}, { 0,  1}, { 0,  1}},
        {{-1,  0}, { 1,  0}, { 0, -1}},
    };
    int x, y;
    m13_object_anchor(reaction, &x, &y);
    if (!is_left) x = DUEL_CANVAS_W - 1 - x;
    int toward_gap = is_left ? 1 : -1;
    const int8_t (*pixels)[2] = phase_pixels[(progress >> 2) & 3u];
    for (int i = 0; i < 3; i++)
        duel_fb_px(fb, x + pixels[i][0] * toward_gap, y + pixels[i][1], true);
}

static void m13_draw_carried(duel_fb_t *fb, uint8_t mark, int x, int y,
                             int gapward) {
    static const int8_t mark_pixels[6][4][2] = {
        {{ 0,  0}, { 0, 1}, { 1,  0}, { 0, 0}}, /* dispatch */
        {{ 0,  0}, { 0, 1}, { 1,  1}, { 0, 0}}, /* notes */
        {{ 0, -1}, {-1, 0}, { 1,  0}, { 0, 1}}, /* specimen */
        {{-1,  1}, { 0, 0}, { 1, -1}, { 0, 0}}, /* tool */
        {{ 0,  0}, { 1, 0}, { 0,  1}, { 1, 1}}, /* parcel */
        {{ 0,  0}, { 1, 0}, { 0,  2}, { 1, 2}}, /* ledger */
    };
    if (mark == M13_MARK_NONE) return;
    x += 3 * gapward;
    const int8_t (*pixels)[2] = mark_pixels[mark - 1u];
    for (int i = 0; i < 4; i++)
        duel_fb_px(fb, x + pixels[i][0] * gapward, y + pixels[i][1], true);
}

static void m13_draw_core(duel_fb_t *fb, int cx, int fy, bool seated) {
    int top = fy - (seated ? 11 : 13);
    for (int x = cx - 1; x <= cx + 1; x++) duel_fb_px(fb, x, top, true);
    for (int x = cx - 2; x <= cx + 2; x++) {
        duel_fb_px(fb, x, top + 1, true);
        duel_fb_px(fb, x, top + 2, true);
        duel_fb_px(fb, x, top + 4, true); /* shoulders */
    }
    for (int x = cx - 1; x <= cx + 1; x++) duel_fb_px(fb, x, top + 3, true);
    int torso_end = top + (seated ? 8 : 9);
    for (int y = top + 5; y <= torso_end; y++)
        for (int x = cx - 1; x <= cx + 1; x++) duel_fb_px(fb, x, y, true);
    int hips = top + (seated ? 9 : 10);
    for (int x = cx - 2; x <= cx + 2; x++) duel_fb_px(fb, x, hips, true);
    if (seated) {
        duel_fb_px(fb, cx - 2, top + 10, true); duel_fb_px(fb, cx + 2, top + 10, true);
        duel_fb_px(fb, cx - 2, fy, true); duel_fb_px(fb, cx - 1, fy, true);
        duel_fb_px(fb, cx + 1, fy, true); duel_fb_px(fb, cx + 2, fy, true);
    } else {
        for (int y = top + 11; y <= top + 12; y++) {
            duel_fb_px(fb, cx - 1, y, true); duel_fb_px(fb, cx + 1, y, true);
        }
        for (int x = cx - 2; x <= cx + 2; x++) duel_fb_px(fb, x, fy, true);
    }
}

void m12_resident_draw(duel_fb_t *fb, const m12_resident_t *res, bool is_left,
                       uint8_t mode, uint32_t frame) {
    (void)mode; (void)frame;
    const m13_occupation_desc_t *desc = m13_occupation(res->station);
    int gapward = is_left ? 1 : -1;
    int cx = is_left ? desc->station : DUEL_CANVAS_W - 1 - desc->station;
    int fy = desc->pose == M13_POSE_CARRY ? 107 :
             desc->pose == M13_POSE_SEATED ? 107 : 105;
    bool ordinary = res->task == RESIDENT_NORMAL;
    bool seated = ordinary && desc->pose == M13_POSE_SEATED;
    int top = fy - (seated ? 11 : 13);
    int shoulders = top + 4;

    m13_draw_core(fb, cx, fy, seated);

    if (ordinary) {
        switch (desc->pose) {
            case M13_POSE_WORK:
                duel_fb_px(fb, cx + 2 * gapward, shoulders + 2, true);
                duel_fb_px(fb, cx + 3 * gapward, shoulders + 2, true);
                break;
            case M13_POSE_CARRY:
                duel_fb_px(fb, cx + 2 * gapward, shoulders + 2, true);
                duel_fb_px(fb, cx + 2 * gapward, shoulders + 3, true);
                break;
            case M13_POSE_INSPECT:
                duel_fb_px(fb, cx + 2 * gapward, top + 2, true);
                duel_fb_px(fb, cx + 3 * gapward, top + 1, true);
                break;
            case M13_POSE_SEATED:
                duel_fb_px(fb, cx + 2 * gapward, shoulders + 2, true);
                break;
            case M13_POSE_WATCH:
                duel_fb_px(fb, cx + 2 * gapward, top + 1, true);
                duel_fb_px(fb, cx + 3 * gapward, top - 1, true);
                duel_fb_px(fb, cx + 3 * gapward, top - 3, true);
                break;
            case M13_POSE_EXCHANGE:
                duel_fb_px(fb, cx + 2 * gapward, shoulders + 1, true);
                duel_fb_px(fb, cx + 3 * gapward, shoulders + 1, true);
                break;
            case M13_POSE_REACT:
                duel_fb_px(fb, cx - 3, top + 2, true);
                duel_fb_px(fb, cx + 3, top + 2, true);
                duel_fb_px(fb, cx, top - 2, true);
                break;
        }
        m13_draw_carried(fb, desc->carried, cx, shoulders + 2, gapward);
        m13_draw_object_reaction(fb, desc->reaction, is_left, res->progress);

        /* Personality marks are re-anchored to the enlarged crown and yield
         * completely to authoritative aftermath hats. */
        switch (res->personality) {
            case DUEL_M12_PERSONALITY_CURIOUS:
                duel_fb_px(fb, cx, top - 1, true);
                break;
            case DUEL_M12_PERSONALITY_NERVOUS:
                duel_fb_px(fb, cx - gapward * 3, top + 2, true);
                break;
            case DUEL_M12_PERSONALITY_PROUD:
                duel_fb_px(fb, cx - 1, top - 1, true);
                duel_fb_px(fb, cx + 1, top - 1, true);
                break;
            case DUEL_M12_PERSONALITY_DISTRACTED:
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
            for (int x = cx - 2; x <= cx + 2; x++) duel_fb_px(fb, x, top - 1, true);
            duel_fb_px(fb, cx + 3 * gapward, top - 2, true);
            break;
        case RESIDENT_PANIC:
            duel_fb_px(fb, cx - 3, top + 2, true); duel_fb_px(fb, cx + 3, top + 2, true);
            duel_fb_px(fb, cx, top - 3, true);
            break;
        case RESIDENT_FIGHT_FIRE:
            for (int x = cx - 2; x <= cx + 2; x++) duel_fb_px(fb, x, top - 1, true);
            duel_fb_px(fb, cx, top - 2, true);
            break;
        case RESIDENT_INSPECT:
            duel_fb_px(fb, cx - 1, top - 1, true); duel_fb_px(fb, cx, top - 2, true);
            duel_fb_px(fb, cx + 1, top - 1, true); duel_fb_px(fb, cx + 3 * gapward, top + 2, true);
            break;
        case RESIDENT_REPAIR:
            for (int x = cx - 2; x <= cx + 2; x++) duel_fb_px(fb, x, top - 1, true);
            duel_fb_px(fb, cx - 1, top - 2, true); duel_fb_px(fb, cx + 1, top - 2, true);
            break;
        case RESIDENT_WATCH_CAST:
            duel_fb_px(fb, cx, top - 2, true);
            duel_fb_px(fb, cx - 2, top - 3, true); duel_fb_px(fb, cx + 2, top - 3, true);
            break;
        default:
            break;
    }
}

void m13_resident_draw_attunement(duel_fb_t *fb, bool is_left, uint8_t floor) {
    if (floor >= M13_OCCUPATION_FLOORS) floor = DUEL_M12_FLOOR_COMMONS;
    const m13_occupation_desc_t *work = m13_occupation(
        M13_OCCUPATION_KEY(floor, DUEL_M12_ACTION_WORK));
    int x, y;
    m13_object_anchor(work->reaction, &x, &y);
    if (!is_left) x = DUEL_CANVAS_W - 1 - x;
    duel_fb_px(fb, x, y, true);
}

#endif /* ARCANE_M13 */

#endif // ARCANE_M12
