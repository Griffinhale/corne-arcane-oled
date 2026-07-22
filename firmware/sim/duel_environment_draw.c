#include "duel_draw_internal.h"
#include "duel_host.h"
#include "duel_resident.h"
#include "duel_runtime.h"

static void sky_sun(duel_fb_t *fb, int x, int y) {
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            duel_fb_px(fb, x + dx, y + dy, true);
    duel_fb_px(fb, x + 2, y, true);
    duel_fb_px(fb, x - 2, y, true);
    duel_fb_px(fb, x, y + 2, true);
    duel_fb_px(fb, x, y - 2, true);
    duel_fb_px(fb, x + 4, y, true);
    duel_fb_px(fb, x - 4, y, true);
    duel_fb_px(fb, x, y + 4, true);
    duel_fb_px(fb, x, y - 4, true);
}

static void sky_moon(duel_fb_t *fb, int x, int y) {
    duel_fb_px(fb, x, y - 3, true);
    duel_fb_px(fb, x + 1, y - 3, true);
    duel_fb_px(fb, x + 2, y - 2, true);
    duel_fb_px(fb, x + 3, y - 1, true);
    duel_fb_px(fb, x + 3, y, true);
    duel_fb_px(fb, x + 3, y + 1, true);
    duel_fb_px(fb, x + 2, y + 2, true);
    duel_fb_px(fb, x, y + 3, true);
    duel_fb_px(fb, x + 1, y + 3, true);
}

void duel_environment_draw_sky(duel_fb_t *fb, const duel_render_t *r, bool is_left) {
    uint8_t phase = DUEL_SECONDARY_SKY_PHASE(r->secondary);
    uint8_t sub = DUEL_SECONDARY_SKY_SUBPHASE(r->secondary);
    uint8_t floor = DUEL_CIVIC_FLOOR(r->civic);
    /* Arc positions in desk space (left canvas x0-31, right canvas x32-63).
     * Each half draws its clipped portion, so a body crossing the gap shows
     * a sliver on both panels and the pair reads as one panorama. Endpoints
     * tuck the disc behind a tower's lit edge column (x11 / desk 51) so it
     * rises and sets behind the architecture rather than popping. */
    static const int8_t sky_arc[16][2] = {
        {15, 18}, {16, 16}, {17, 15}, {19, 13}, /* dawn: clearing the left tower */
        {22, 10}, {26, 7},  {31, 5},  {36, 7},  /* day: apex over the gap */
        {41, 9},  {44, 12}, {46, 14}, {48, 16}, /* dusk: down to the right tower */
        {44, 8},  {41, 7},  {38, 7},  {35, 8},  /* night: moon drifts gapward */
    };
    const int8_t *at = sky_arc[((phase & 3u) << 2) | sub];
    int bx = is_left ? at[0] : at[0] - DUEL_CANVAS_W; /* duel_fb_px clips */
    if (phase != DUEL_SKY_NIGHT) {
        sky_sun(fb, bx, at[1]);
    } else {
        sky_moon(fb, bx, at[1]);
        uint8_t stars = floor == DUEL_CIVIC_FLOOR_SPECIAL ? 7u : 3u;
        for (uint8_t i = 0; i < stars; i++) {
            uint8_t h = (uint8_t)(r->seed * 29u + i * 47u + (is_left ? 11u : 83u));
            int x = (is_left ? 14 : 2) + (int)(h % 16u);
            int y = 3 + (int)((h >> 2) % 13u);
            duel_fb_px(fb, x, y, true);
            if (floor == DUEL_CIVIC_FLOOR_SPECIAL && i && (i % 3u) == 0u)
                duel_fb_line(fb, x - 3, y - 2, x, y);
        }
    }
}

/* Wizard tower: a half-width shaft on the outer side of each canvas
 * rising from the rooftop deck into a full architectural peak — astral
 * (left): taper, dome, and finial; mechanical (right): crenellated cap and
 * beacon mast. The gap-side balcony partway up is the future big-cast/
 * stance station; the peak is never occupied. The single shaft window reads
 * the sky phase (outlined by day, lit from dusk onward), and the HP windows
 * drawn later land inside the shaft as its lower tier of lights. */
void duel_environment_draw_tower(duel_fb_t *fb, const duel_render_t *r, bool is_left) {
#define TWR_X(x) (is_left ? (x) : (DUEL_CANVAS_W - 1 - (x)))
    uint8_t phase = DUEL_SECONDARY_SKY_PHASE(r->secondary);
    bool lit = phase == DUEL_SKY_DUSK || phase == DUEL_SKY_NIGHT;

    // Shaft edges from the peak base down onto the deck, with a base flare.
    for (int y = DUEL_TOWER_PEAK_Y; y <= DUEL_DECK_Y0; y++) {
        duel_fb_px(fb, TWR_X(1), y, true);
        duel_fb_px(fb, TWR_X(11), y, true);
    }
    duel_fb_px(fb, TWR_X(0), 58, true);
    duel_fb_px(fb, TWR_X(0), 59, true);
    duel_fb_px(fb, TWR_X(12), 58, true);
    duel_fb_px(fb, TWR_X(12), 59, true);

    if (is_left) {
        // Astral peak: shoulder, taper, dome, finial.
        duel_fb_hline(fb, 2, 10, 13);
        duel_fb_px(fb, 3, 12, true);
        duel_fb_px(fb, 9, 12, true);
        duel_fb_px(fb, 3, 11, true);
        duel_fb_px(fb, 9, 11, true);
        duel_fb_px(fb, 3, 10, true);
        duel_fb_px(fb, 9, 10, true);
        duel_fb_px(fb, 3, 9, true);
        duel_fb_px(fb, 9, 9, true);
        duel_fb_hline(fb, 4, 8, 8);
        duel_fb_px(fb, 4, 7, true);
        duel_fb_px(fb, 8, 7, true);
        duel_fb_px(fb, 5, 6, true);
        duel_fb_px(fb, 7, 6, true);
        duel_fb_px(fb, 6, 5, true);
        duel_fb_px(fb, 6, 4, true);
        duel_fb_px(fb, 6, 3, true);
        duel_fb_px(fb, 6, 2, true);
    } else {
        // Mechanical peak: crenellated cap with a beacon mast.
        duel_fb_desk_hline(fb, is_left, 0, 12, 13);
        duel_fb_desk_hline(fb, is_left, 1, 11, 12);
        for (int c = 1; c <= 9; c += 4) {
            duel_fb_px(fb, TWR_X(c), 10, true);
            duel_fb_px(fb, TWR_X(c), 11, true);
        }
        for (int y = 3; y <= 9; y++) {
            duel_fb_px(fb, TWR_X(5), y, true);
            duel_fb_px(fb, TWR_X(6), y, true);
        }
        duel_fb_desk_hline(fb, is_left, 4, 7, 2);
        duel_fb_px(fb, TWR_X(4), 1, true);
        duel_fb_px(fb, TWR_X(7), 1, true);
        if (lit) {
            duel_fb_px(fb, TWR_X(5), 0, true);
            duel_fb_px(fb, TWR_X(6), 0, true);
        }
    }

    // Upper shaft window: astral arches, mechanical squares its lintel.
    if (lit) {
        for (int y = 20; y <= 23; y++)
            duel_fb_desk_hline(fb, is_left, 4, 8, y);
        if (is_left)
            duel_fb_px(fb, 6, 19, true);
    } else {
        for (int y = 20; y <= 23; y++) {
            duel_fb_px(fb, TWR_X(4), y, true);
            duel_fb_px(fb, TWR_X(8), y, true);
        }
        if (is_left)
            duel_fb_px(fb, 6, 19, true);
        else
            duel_fb_desk_hline(fb, is_left, 4, 8, 19);
    }

    // Gap-side balcony: slab and corbel. Empty for now — the calm
    // stances and the big-cast ascent restage the wizard here.
    duel_fb_desk_hline(fb, is_left, 11, 16, 30);
    duel_fb_desk_hline(fb, is_left, 11, 16, 31);
    duel_fb_px(fb, TWR_X(13), 32, true);
    duel_fb_px(fb, TWR_X(12), 33, true);
#undef TWR_X
}

static void draw_typing_ambience(duel_fb_t *fb, const duel_render_t *r, bool is_left, uint8_t floor,
                                 uint8_t mode) {
    if (!INCANTATION_AMBIENCE_ACTIVE(r->local_ambience) || mode == DUEL_CIVIC_MODE_QUIET ||
        floor == DUEL_CIVIC_FLOOR_SPECIAL)
        return;
    uint8_t tempo = INCANTATION_AMBIENCE_TEMPO(r->local_ambience);
    uint8_t trend = INCANTATION_AMBIENCE_TREND(r->local_ambience);
    incantation_point_t work = incantation_occupation_anchor(floor, DUEL_CIVIC_ACTION_WORK);
    for (uint8_t i = 0; i <= tempo; i++) {
        int drift = trend == TREND_ACCELERATING   ? (int)i
                    : trend == TREND_DECELERATING ? -(int)i
                    : trend == TREND_IRREGULAR    ? ((i & 1u) ? 2 : -2)
                                                  : 0;
        int x = work.x - 4 + i * 3 + drift;
        int y = work.y - 7 - ((r->civic_phase + i + trend) & 3u);
        duel_fb_px(fb, duel_fb_desk_x(is_left, x), y, true);
        if (tempo >= TEMPO_RAPID)
            duel_fb_px(fb, duel_fb_desk_x(is_left, x + 1), y - 1, true);
    }
}

static void archive_rect(duel_fb_t *fb, int x0, int y0, int x1, int y1) {
    for (int x = x0; x <= x1; x++) {
        duel_fb_px(fb, x, y0, true);
        duel_fb_px(fb, x, y1, true);
    }
    for (int y = y0 + 1; y < y1; y++) {
        duel_fb_px(fb, x0, y, true);
        duel_fb_px(fb, x1, y, true);
    }
}

// A tiny 1bpp gear: hub, cross spokes, and eight teeth. The mechanical (right)
// city's signature motif — squared, radial, industrial.
static void floor_gear(duel_fb_t *fb, int cx, int cy, int r) {
    for (int d = -r; d <= r; d++) {
        duel_fb_px(fb, cx + d, cy, true);
        duel_fb_px(fb, cx, cy + d, true);
    }
    duel_fb_px(fb, cx + r + 1, cy, true);
    duel_fb_px(fb, cx - r - 1, cy, true);
    duel_fb_px(fb, cx, cy + r + 1, true);
    duel_fb_px(fb, cx, cy - r - 1, true);
    duel_fb_px(fb, cx + r, cy + r, true);
    duel_fb_px(fb, cx - r, cy - r, true);
    duel_fb_px(fb, cx + r, cy - r, true);
    duel_fb_px(fb, cx - r, cy + r, true);
}

// A shallow integer-curvature dome peaking in the middle: the astral (left)
// city's signature motif — curved arches, domes, orbs.
static void floor_dome(duel_fb_t *fb, int lo, int hi, int top_y) {
    int cx = (lo + hi) / 2;
    int half = (hi - lo) / 2;
    if (half < 1)
        half = 1;
    for (int dx = -half; dx <= half; dx++) {
        int y = top_y + (dx * dx) / half / 2;
        duel_fb_px(fb, cx + dx, y, true);
    }
}

// A large framed window set into the outer wall. This is the single biggest
// legibility win for the floor: one bold rectangle reads instantly as "a lived
// room" from across the desk and fills the otherwise-dead upper-outer void.
// Astral arches its crown and carries a lone centre mullion (curves); mechanical
// squares it with a full cross of mullions (four panes).

/* Occupation-first current furniture. Each floor owns two large silhouettes and
 * uses the same work/inspect/rest anchors as the resident engine: x~6 for the
 * dominant work object, x~21 for its supporting station, and x~4 for rest. */
static void duel_environment_draw_floor_occupation(duel_fb_t *fb, uint8_t floor, bool is_left) {
#define OX(x)            duel_fb_desk_x(is_left, x)
#define OSPAN(x0, x1, y) duel_fb_desk_hline(fb, is_left, (x0), (x1), (y))
#define ORECT(x0, y0, x1, y1)                                                                      \
    do {                                                                                           \
        int a_ = OX(x0), b_ = OX(x1);                                                              \
        archive_rect(fb, a_ < b_ ? a_ : b_, (y0), a_ < b_ ? b_ : a_, (y1));                        \
    } while (0)
    if (floor == DUEL_CIVIC_FLOOR_COMMONS) {
        /* Communal table / dispatch desk: the broadest horizontal mass. */
        OSPAN(3, 14, 96);
        OSPAN(3, 14, 97);
        for (int y = 98; y <= 105; y++) {
            duel_fb_px(fb, OX(5), y, true);
            duel_fb_px(fb, OX(13), y, true);
        }
        /* Notice/mail board, deliberately tall and gap-side. */
        ORECT(24, 69, 30, 91);
        if (is_left) {
            floor_dome(fb, OX(24), OX(30), 66); /* arched notice board */
            for (int y = 75; y <= 87; y += 6) {
                OSPAN(25, 29, y);
                duel_fb_px(fb, OX(24), y - 2, true);
            }
            floor_dome(fb, OX(8), OX(14), 88); /* tea-orb stand */
            for (int y = 91; y <= 95; y++)
                duel_fb_px(fb, OX(11), y, true);
            duel_fb_px(fb, OX(9), 94, true);
            duel_fb_px(fb, OX(13), 93, true);
        } else {
            /* Dispatch cubbies and clock. OSPAN orders mirrored endpoints
             * before drawing so both halves retain their cubby shelves. */
            for (int y = 75; y <= 87; y += 6)
                OSPAN(25, 29, y);
            for (int x = 26; x <= 29; x += 3)
                for (int y = 70; y <= 90; y++)
                    duel_fb_px(fb, OX(x), y, true);
            floor_gear(fb, OX(11), 90, 3);
            duel_fb_px(fb, OX(11), 87, true);
            duel_fb_px(fb, OX(13), 90, true);
            for (int x = 7; x <= 15; x += 4)
                for (int y = 99; y <= 103; y++)
                    duel_fb_px(fb, OX(x), y, true);
        }
    } else if (floor == DUEL_CIVIC_FLOOR_RESEARCH) {
        /* Dominant telescope/analyzer, an unmistakable rising diagonal. */
        duel_fb_line(fb, OX(4), 98, OX(15), 76);
        duel_fb_line(fb, OX(5), 100, OX(16), 78);
        ORECT(11, 73, 17, 81);
        for (int y = 99; y <= 105; y++)
            duel_fb_px(fb, OX(7), y, true);
        duel_fb_line(fb, OX(7), 99, OX(3), 105);
        duel_fb_line(fb, OX(7), 99, OX(12), 105);
        /* Specimen cabinet/cylinder supporting the instrument. */
        ORECT(24, 76, 30, 104);
        OSPAN(24, 30, 90);
        if (is_left) {
            /* Orrery and star chart. */
            floor_dome(fb, OX(2), OX(12), 65);
            duel_fb_px(fb, OX(7), 68, true);
            for (int x = 3; x <= 11; x += 2)
                duel_fb_px(fb, OX(x), 70 + ((x * 3) & 7), true);
            floor_dome(fb, OX(24), OX(30), 72);
            duel_fb_px(fb, OX(27), 83, true);
            duel_fb_px(fb, OX(25), 96, true);
            duel_fb_px(fb, OX(29), 98, true);
        } else {
            /* Probe, scope display, and specimen cylinder. */
            ORECT(2, 65, 12, 78);
            duel_fb_line(fb, OX(4), 75, OX(7), 69);
            duel_fb_line(fb, OX(7), 69, OX(10), 74);
            floor_gear(fb, OX(16), 84, 2);
            for (int y = 79; y <= 101; y++) {
                duel_fb_px(fb, OX(25), y, true);
                duel_fb_px(fb, OX(29), y, true);
            }
            OSPAN(25, 29, 99);
            duel_fb_px(fb, OX(27), 94, true);
            duel_fb_px(fb, OX(26), 96, true);
        }
    } else if (floor == DUEL_CIVIC_FLOOR_WORKSHOP) {
        /* Dominant forge: cauldron on astral, anvil/gear press on mechanical. */
        if (is_left) {
            floor_dome(fb, OX(3), OX(14), 90);
            OSPAN(4, 13, 96);
            for (int y = 97; y <= 103; y++) {
                duel_fb_px(fb, OX(5), y, true);
                duel_fb_px(fb, OX(13), y, true);
            }
            for (int x = 7; x <= 13; x += 2)
                duel_fb_px(fb, OX(x), 87 - ((x + 1) & 3), true);
        } else {
            OSPAN(3, 14, 94);
            OSPAN(6, 13, 95);
            duel_fb_line(fb, OX(9), 96, OX(7), 105);
            duel_fb_line(fb, OX(13), 96, OX(15), 105);
            floor_gear(fb, OX(11), 83, 4);
            for (int y = 68; y <= 79; y++)
                duel_fb_px(fb, OX(11), y, true);
            ORECT(7, 65, 15, 69); /* gear press crosshead */
        }
        /* Tool/reagent station and hoist/rack occupy the gap-side column. */
        ORECT(24, 78, 30, 104);
        for (int y = 84; y <= 100; y += 8)
            OSPAN(25, 29, y);
        if (is_left) {
            floor_dome(fb, OX(24), OX(30), 74);
            for (int x = 25; x <= 29; x += 2) {
                duel_fb_px(fb, OX(x), 81, true);
                duel_fb_px(fb, OX(x), 82, true);
            }
        } else {
            for (int y = 62; y <= 75; y++)
                duel_fb_px(fb, OX(27), y, true);
            ORECT(25, 74, 29, 79);
            duel_fb_px(fb, OX(27), 80, true);
            duel_fb_px(fb, OX(26), 81, true);
            floor_gear(fb, OX(27), 94, 2);
        }
    } else { /* Observatory: quiet instrument under a city-specific dome. */
        if (is_left) {
            floor_dome(fb, OX(2), OX(30), 66);
            floor_dome(fb, OX(5), OX(27), 70);
            duel_fb_line(fb, OX(7), 101, OX(18), 76);
            duel_fb_line(fb, OX(8), 102, OX(19), 77);
            ORECT(16, 73, 22, 80);
            duel_fb_px(fb, OX(9), 67, true);
            duel_fb_px(fb, OX(14), 72, true);
            duel_fb_px(fb, OX(24), 68, true);
        } else {
            ORECT(2, 65, 30, 69);
            for (int x = 4; x <= 28; x += 4)
                duel_fb_px(fb, OX(x), 67, true);
            floor_gear(fb, OX(16), 79, 7);
            duel_fb_line(fb, OX(7), 101, OX(16), 79);
            duel_fb_line(fb, OX(25), 101, OX(16), 79);
            for (int y = 87; y <= 104; y++)
                duel_fb_px(fb, OX(16), y, true);
        }
        ORECT(23, 83, 30, 104);
        duel_fb_line(fb, OX(24), 89, OX(29), 96);
        duel_fb_line(fb, OX(24), 96, OX(29), 89);
    }
#undef OSPAN
#undef ORECT
#undef OX
}

static void duel_environment_draw_floor_transition(duel_fb_t *fb, const duel_render_t *r,
                                                   bool is_left) {
    if (!INCANTATION_FLOOR_TRANSITION_ACTIVE(r->floor_transition))
        return;
    uint8_t phase = INCANTATION_FLOOR_TRANSITION_PHASE(r->floor_transition);
    int inner = is_left ? 31 : 0;
    if (phase == 0u) { /* source-room shutter */
        for (int x = 1; x < 32; x += 4) {
            int height = 12 + ((x + r->civic_phase) & 7);
            for (int y = 62; y < 62 + height; y++)
                duel_fb_px(fb, x, y, true);
            duel_fb_px(fb, x + 1, 62 + height, true);
        }
    } else if (phase == 1u) { /* full brick/elevator wipe */
        for (int y = 62; y <= 110; y++)
            for (int x = 0; x < 32; x++)
                duel_fb_px(fb, x, y, false);
        for (int y = 62; y <= 110; y += 5) {
            duel_fb_hline(fb, 0, 31, y);
            int offset = ((y / 5) & 1) ? 3 : 0;
            for (int x = offset; x < 32; x += 7)
                for (int dy = 1; dy < 5 && y + dy <= 110; dy++)
                    duel_fb_px(fb, x, y + dy, true);
        }
        for (int y = 62; y <= 110; y++)
            duel_fb_px(fb, inner, y, true);
    } else if (phase == 2u) { /* target-room reveal */
        for (int y = 62; y <= 82; y++)
            for (int x = 0; x < 32; x++)
                duel_fb_px(fb, x, y, false);
        for (int y = 62; y <= 82; y += 5)
            duel_fb_hline(fb, 0, 31, y);
        for (int x = 2; x < 32; x += 6)
            duel_fb_px(fb, x, 84, true);
    } else { /* settling dust / sparks */
        for (int i = 0; i < 8; i++) {
            int x = (i * 7 + r->seed) & 31;
            int y = 68 + ((i * 11 + r->civic_phase) % 38);
            duel_fb_px(fb, x, y, true);
            if ((i & 2) == 0)
                duel_fb_px(fb, x + (is_left ? -1 : 1), y + 1, true);
        }
    }
}

// Twin Cities tower floor beneath the raised rooftop. A schematic cutaway room whose
// OCCUPATION is chosen by the civic byte (DUEL_CIVIC_FLOOR): Commons/post,
// Archive/Research, or Workshop/Forge. The two cities render the same room in
// clearly different architectural languages — left is astral/curved (dashed
// beams, domes, orbs, buttresses); right is mechanical/squared (solid beams,
// rivets, gears, tie-bars). One session-seeded resident lives in the floor,
// derived and drawn locally (duel_resident.c). Authored in desk space (gap at
// x=31) and mirrored on the right OLED.
void duel_environment_draw_floor(duel_fb_t *fb, const duel_render_t *r, bool is_left) {
#define FLR_X(x) (is_left ? (x) : (DUEL_CANVAS_W - 1 - (x)))
    // The civic byte is authoritative for the occupation; during the first two
    // transition phases the outgoing (source) floor is still the one shown.
    uint8_t floor = incantation_effective_floor(r);
    uint8_t mode = DUEL_CIVIC_MODE(r->civic);

    // Rooftop deck: the ceiling beam thickened one row upward (both cities),
    // with crenellation teeth at the gap corner; the wizard tower's base
    // flare crenellates the other end. City character lives in the details
    // below the beam, not the beam itself.
    for (int x = 0; x < DUEL_CANVAS_W; x++) {
        duel_fb_px(fb, x, DUEL_DECK_Y0, true);
        duel_fb_px(fb, x, DUEL_FLOOR_BEAM_Y, true);
    }
    int tooth = is_left ? 28 : 2;
    duel_fb_px(fb, tooth, 58, true);
    duel_fb_px(fb, tooth + 1, 58, true);
    duel_fb_px(fb, tooth, 59, true);
    duel_fb_px(fb, tooth + 1, 59, true);
    if (r->revision & INCANTATION_AFTERMATH_WIRE) {
        uint8_t world = INCANTATION_AFTER_WORLD(r->shared_pres);
        /* WORLD_WONDER reads from the big-cast tower glow and the residents
         * watching the roof. */
        if (world == WORLD_CRISIS) {
            duel_fb_line(fb, FLR_X(12), 61, FLR_X(15), 66);
            duel_fb_line(fb, FLR_X(15), 66, FLR_X(17), 63);
        } else if (world == WORLD_RECOVERY) {
            for (int x = 4; x < DUEL_CANVAS_W; x += 8)
                duel_fb_px(fb, x, 63, true);
        }
    }
    if (is_left) {
        // A hanging astral arc under the beam near the gap.
        duel_fb_px(fb, FLR_X(28), 62, true);
        duel_fb_px(fb, FLR_X(29), 63, true);
        duel_fb_px(fb, FLR_X(30), 64, true);
    } else {
        for (int x = 1; x < DUEL_CANVAS_W; x += 6)
            duel_fb_px(fb, x, 62, true);
    }

    // Outer wall (edge away from the gap) with city texture, plus a short
    // lift-shaft stub by the gap.
    for (int y = 63; y <= 109; y++)
        duel_fb_px(fb, FLR_X(0), y, true);
    if (is_left) {
        // Astral: curved buttress studs bowing inward, with two wall orbs.
        for (int y = 66; y <= 106; y += 8)
            duel_fb_px(fb, FLR_X(1), y, true);
        duel_fb_px(fb, FLR_X(2), 74, true);
        duel_fb_px(fb, FLR_X(2), 96, true);
    } else {
        // Mechanical: riveted plating and straight tie-bars.
        for (int y = 65; y <= 107; y += 6) {
            duel_fb_px(fb, FLR_X(1), y, true);
            duel_fb_px(fb, FLR_X(2), y, true);
        }
    }
    for (int y = 63; y <= 70; y++)
        duel_fb_px(fb, FLR_X(30), y, true);
    if (is_left) {
        duel_fb_px(fb, FLR_X(29), 66, true); // counterweight orb
    } else {
        duel_fb_px(fb, FLR_X(29), 64, true);
        duel_fb_px(fb, FLR_X(28), 64, true); // gear teeth
    }

    // Big fixtures that own the empty upper two-thirds of the room: a framed
    // window set into the outer wall and a fixture hanging in the centre void.
    // Both are large, bold shapes chosen to read at desk distance. They are
    // authored in desk space and mirrored per canvas.

    // Ground line of the room.
    duel_fb_hline(fb, 0, DUEL_CANVAS_W - 1, DUEL_FLOOR_Y1);

    // Occupation furniture, then the session-seeded resident living among it.
    duel_environment_draw_floor_occupation(fb, floor, is_left);
    draw_typing_ambience(fb, r, is_left, floor, mode);
    civic_resident_t res = civic_resident_derive(r->seed, is_left, floor, mode, r->civic_phase);
    if (floor == DUEL_CIVIC_FLOOR_SPECIAL) {
        res.action = DUEL_CIVIC_ACTION_WATCH_ROOF;
        res.station = INCANTATION_OCCUPATION_KEY(floor, res.action);
    }
    uint8_t after_kind = AFTER_NONE, after_phase = 0;
    if (r->revision & INCANTATION_AFTERMATH_WIRE) {
        uint8_t side = is_left ? SIM_SIDE_L : SIM_SIDE_R;
        after_kind = INCANTATION_AFTER_KIND(r->shared_pres, side);
        after_phase = INCANTATION_AFTER_PHASE(r->revision, side);
        res.progress = (uint8_t)(after_phase * 4u + (r->civic_phase & 3u));
        switch (after_kind) {
            case AFTER_CHEER:
                res.task = RESIDENT_CHEER;
                res.action = DUEL_CIVIC_ACTION_REACT;
                break;
            case AFTER_COMPLAINT:
                res.task = RESIDENT_COMPLAIN;
                res.action = DUEL_CIVIC_ACTION_REACT;
                break;
            case AFTER_PANIC:
                res.task = RESIDENT_PANIC;
                res.action = (after_phase & 1u) ? DUEL_CIVIC_ACTION_WALK : DUEL_CIVIC_ACTION_REACT;
                break;
            case AFTER_FIRE:
                if (after_phase == 0u) {
                    res.task = RESIDENT_PANIC;
                    res.action = DUEL_CIVIC_ACTION_REACT;
                } else if (after_phase < 3u) {
                    res.task = RESIDENT_FIGHT_FIRE;
                    res.action = DUEL_CIVIC_ACTION_WORK;
                } else {
                    res.task = RESIDENT_REPAIR;
                    res.action = DUEL_CIVIC_ACTION_WORK;
                }
                break;
            case AFTER_INSPECT:
                res.task = RESIDENT_INSPECT;
                res.action = DUEL_CIVIC_ACTION_INSPECT;
                break;
            case AFTER_REPAIR:
                res.task = RESIDENT_REPAIR;
                res.action = DUEL_CIVIC_ACTION_WORK;
                break;
            case AFTER_MAX_CAST:
                res.task = after_phase < 2u ? RESIDENT_WATCH_CAST : RESIDENT_CHEER;
                res.action =
                    after_phase < 2u ? DUEL_CIVIC_ACTION_WATCH_ROOF : DUEL_CIVIC_ACTION_REACT;
                break;
            default:
                break;
        }
        if (after_kind != AFTER_NONE)
            res.station = INCANTATION_OCCUPATION_KEY(floor, res.action);
    } else if (floor != DUEL_CIVIC_FLOOR_SPECIAL &&
               DUEL_EVENT_ID(r->revision) == DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER) {
        uint8_t target = DUEL_EVENT_TARGET(r->revision);
        res.action = DUEL_CIVIC_ACTION_HANDLE_DELIVERY;
        res.station = INCANTATION_OCCUPATION_KEY(floor, res.action);
        res.task =
            target == DUEL_CIVIC_EVENT_TARGET_SHARED
                ? RESIDENT_DIPLO_NEUTRAL
                : ((target == DUEL_CIVIC_EVENT_TARGET_LEFT) == is_left ? RESIDENT_DIPLO_PROUD
                                                                       : RESIDENT_DIPLO_RECEIVING);
    }
    civic_resident_draw(fb, &res, is_left, mode, 0);

    /* Lasting room/object consequences. They share the authoritative aftermath
     * phase with the resident task, so reconnecting halves resume mid-arc. All
     * marks use the same floor/action descriptor as the assigned civic task. */
    uint8_t mark_action = after_kind == AFTER_INSPECT     ? DUEL_CIVIC_ACTION_INSPECT
                          : after_kind == AFTER_COMPLAINT ? DUEL_CIVIC_ACTION_INSPECT
                          : after_kind == AFTER_PANIC     ? DUEL_CIVIC_ACTION_REACT
                          : after_kind == AFTER_MAX_CAST  ? DUEL_CIVIC_ACTION_WATCH_ROOF
                          : after_kind == AFTER_CHEER     ? DUEL_CIVIC_ACTION_REACT
                                                          : DUEL_CIVIC_ACTION_WORK;
    incantation_point_t mark = incantation_occupation_anchor(floor, mark_action);
    int mx = FLR_X(mark.x), my = mark.y;
    if (after_kind == AFTER_FIRE) {
        if (after_phase < 3u) {
            for (int i = 0; i < 4 - after_phase; i++) {
                int dx = (i - 1) * (is_left ? 1 : -1);
                duel_fb_px(fb, mx + dx, my - 1 - (int)((r->civic_phase + i) & 3u), true);
                duel_fb_px(fb, mx + dx, my + 1, true);
            }
        }
        if (after_phase >= 1u && after_phase < 3u)
            duel_fb_line(fb, FLR_X(mark.x - 6), my + 5, mx, my - 1); /* hose / spell stream */
        if (after_phase == 3u) {
            duel_fb_line(fb, FLR_X(mark.x - 3), my - 3, FLR_X(mark.x + 3), my + 3);
            duel_fb_line(fb, FLR_X(mark.x - 3), my + 3, FLR_X(mark.x + 3), my - 3);
        }
    } else if (after_kind == AFTER_INSPECT) {
        for (int i = 0; i < 5 - after_phase; i++)
            duel_fb_px(fb, mx + (is_left ? 1 : -1) * (i & 1 ? i : -i), my + 4 - i * 2, true);
        if (after_phase >= 2u) {
            duel_fb_px(fb, FLR_X(mark.x - 2), my + 2, true);
            duel_fb_px(fb, FLR_X(mark.x - 1), my + 1, true);
        }
    } else if (after_kind == AFTER_REPAIR || after_kind == AFTER_PANIC) {
        if (after_phase < 3u) {
            duel_fb_line(fb, FLR_X(mark.x - 3), my - 4, FLR_X(mark.x + 3), my + 4);
            duel_fb_line(fb, FLR_X(mark.x + 3), my - 4, FLR_X(mark.x - 2), my + 3);
        } else {
            duel_fb_desk_hline(fb, is_left, mark.x - 3, mark.x + 3, my + 3);
            duel_fb_px(fb, mx, my, true);
        }
    } else if (after_kind == AFTER_MAX_CAST) {
        int motes = 6 - after_phase;
        for (int i = 0; i < motes; i++)
            duel_fb_px(fb, FLR_X(mark.x - 10 + i * 4),
                       my - 15 + (int)((r->civic_phase + i * 3u) % 12u), true);
        if (after_phase == 2u)
            duel_fb_desk_hline(fb, is_left, mark.x - 8, mark.x + 8, my + 8);
    } else if (after_kind == AFTER_COMPLAINT) {
        duel_fb_px(fb, mx, my, true);
        duel_fb_desk_hline(fb, is_left, mark.x - 4, mark.x, my - 1);
    } else if (after_kind == AFTER_CHEER) {
        duel_fb_px(fb, FLR_X(mark.x - 2), my - 2 - after_phase, true);
        duel_fb_px(fb, mx, my - 4 + after_phase, true);
    }

    // Stone course: a single masonry border under the room floor; rows below
    // stay dark for the debug odometer. The 1-byte session seed staggers the joints,
    // and the two cities offset differently so the border never looks
    // stamped from one mold.
    uint8_t g = r->seed;
    duel_fb_hline(fb, 0, DUEL_CANVAS_W - 1, DUEL_STONE_Y0);
    duel_fb_hline(fb, 0, DUEL_CANVAS_W - 1, DUEL_STONE_Y1);
    for (int x = (int)(g & 3u) + (is_left ? 2 : 4); x < DUEL_CANVAS_W; x += 6)
        for (int y = DUEL_STONE_Y0 + 1; y < DUEL_STONE_Y1; y++)
            duel_fb_px(fb, x, y, true);
    duel_environment_draw_floor_transition(fb, r, is_left);
#undef FLR_X
}

// A compact standing wizard sized for legibility at actual OLED scale. Centred at x=16
// with the staff hand at y~64 so bolts fly out at cast height. xo/yo shift
// the whole figure (duel_fb_px clips, so off-canvas offsets are free); the
// Lifecycle uses them to sink a collapsing wizard and walk in a fresh one.
