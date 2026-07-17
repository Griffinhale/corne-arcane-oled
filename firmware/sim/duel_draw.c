/*
 * duel_draw.c — wizard silhouette renderer on a plain framebuffer.
 *
 * Moved from the M1 wizard.h nearly verbatim; the only change is that every
 * primitive writes into a duel_fb_t instead of calling oled_write_pixel(),
 * so this file also compiles on the host for tests and the previewer.
 */
#include <string.h>

#include "duel_draw.h"
#include "duel_host.h"
#include "duel_resident.h"
#include "duel_courier.h"
#include "duel_event.h"
#include "duel_runtime.h"

static void wiz_line(duel_fb_t *fb, int x0, int y0, int x1, int y1);

void duel_render_from_world(duel_render_t *render, const sim_world_t *world) {
    duel_view_from_world(world, &render->view);
    render->shared_pres = incantation_aftermath_shared(world);
    render->revision = incantation_aftermath_revision(world);
}

static uint8_t render_host(const duel_render_t *render) {
    return DUEL_HOST_CONTEXT_ONLINE(render->external);
}

static uint8_t render_scene(const duel_render_t *render) {
    return DUEL_HOST_CONTEXT_SCENE(render->external);
}

static uint8_t render_notif(const duel_render_t *render) {
    return DUEL_HOST_CONTEXT_NOTIF(render->external);
}

/* Thirty-minute firmware sky. It is an underlay: every protected gameplay and
 * alert layer is painted later and therefore always wins. */
static void draw_sky(duel_fb_t *fb, const duel_render_t *r, bool is_left) {
    uint8_t phase = DUEL_SECONDARY_SKY_PHASE(r->secondary);
    uint8_t floor = DUEL_CIVIC_FLOOR(r->civic);
    if (phase == DUEL_SKY_DAWN) {
        for (int x = 3; x < 32; x += 7) duel_fb_px(fb, x, 57 - ((x + is_left) & 1), true);
    } else if (phase == DUEL_SKY_DAY) {
        int sun = is_left ? 5 : 26;
        duel_fb_px(fb, sun, 20, true);
        duel_fb_px(fb, sun - 2, 20, true); duel_fb_px(fb, sun + 2, 20, true);
        duel_fb_px(fb, sun, 18, true); duel_fb_px(fb, sun, 22, true);
        for (int x = 2; x < 32; x += 9) duel_fb_px(fb, x, 55, true);
    } else if (phase == DUEL_SKY_DUSK) {
        for (int x = 0; x < 32; x += 4)
            duel_fb_px(fb, x, 55 + ((x / 4) & 1), true);
        duel_fb_px(fb, is_left ? 27 : 4, 25, true);
    } else {
        uint8_t stars = floor == DUEL_CIVIC_FLOOR_SPECIAL ? 9u : 5u;
        for (uint8_t i = 0; i < stars; i++) {
            uint8_t h = (uint8_t)(r->seed * 29u + i * 47u + (is_left ? 11u : 83u));
            int x = 2 + h % 28u;
            int y = 17 + ((h >> 2) % 38u);
            duel_fb_px(fb, x, y, true);
            if (floor == DUEL_CIVIC_FLOOR_SPECIAL && i && (i % 3u) == 0u)
                wiz_line(fb, x - 3, y - 2, x, y);
        }
    }
}

static void draw_typing_ambience(duel_fb_t *fb, const duel_render_t *r,
                                 bool is_left, uint8_t floor, uint8_t mode) {
    if (!INCANTATION_AMBIENCE_ACTIVE(r->local_ambience) ||
        mode == DUEL_CIVIC_MODE_QUIET || floor == DUEL_CIVIC_FLOOR_SPECIAL)
        return;
    uint8_t tempo = INCANTATION_AMBIENCE_TEMPO(r->local_ambience);
    uint8_t trend = INCANTATION_AMBIENCE_TREND(r->local_ambience);
    incantation_point_t work = incantation_occupation_anchor(
        floor, DUEL_CIVIC_ACTION_WORK);
    for (uint8_t i = 0; i <= tempo; i++) {
        int drift = trend == TREND_ACCELERATING ? (int)i :
                    trend == TREND_DECELERATING ? -(int)i :
                    trend == TREND_IRREGULAR ? ((i & 1u) ? 2 : -2) : 0;
        int x = work.x - 4 + i * 3 + drift;
        int y = work.y - 7 - ((r->civic_phase + i + trend) & 3u);
        duel_fb_px(fb, incantation_desk_x(is_left, x), y, true);
        if (tempo >= TEMPO_RAPID)
            duel_fb_px(fb, incantation_desk_x(is_left, x + 1), y - 1, true);
    }
}

void duel_fb_clear(duel_fb_t *fb) {
    memset(fb->bits, 0, sizeof fb->bits);
}

void duel_fb_px(duel_fb_t *fb, int x, int y, bool on) {
    if (x < 0 || x >= DUEL_CANVAS_W || y < 0 || y >= DUEL_CANVAS_H) return;
    int     idx  = x + (y >> 3) * DUEL_CANVAS_W;
    uint8_t mask = (uint8_t)(1u << (y & 7));
    if (on) {
        fb->bits[idx] |= mask;
    } else {
        fb->bits[idx] &= (uint8_t)~mask;
    }
}

bool duel_fb_get(const duel_fb_t *fb, int x, int y) {
    if (x < 0 || x >= DUEL_CANVAS_W || y < 0 || y >= DUEL_CANVAS_H) return false;
    int idx = x + (y >> 3) * DUEL_CANVAS_W;
    return (fb->bits[idx] >> (y & 7)) & 1u;
}

void duel_fb_hline(duel_fb_t *fb, int x0, int x1, int y) {
    for (int x = x0; x <= x1; x++) duel_fb_px(fb, x, y, true);
}

static void wiz_line(duel_fb_t *fb, int x0, int y0, int x1, int y1) {
    int dx = x1 - x0, dy = y1 - y0;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    dx = dx < 0 ? -dx : dx;
    dy = dy < 0 ? -dy : dy;
    int err = dx - dy;
    for (;;) {
        duel_fb_px(fb, x0, y0, true);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
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
    duel_fb_px(fb, cx + r + 1, cy, true); duel_fb_px(fb, cx - r - 1, cy, true);
    duel_fb_px(fb, cx, cy + r + 1, true); duel_fb_px(fb, cx, cy - r - 1, true);
    duel_fb_px(fb, cx + r, cy + r, true); duel_fb_px(fb, cx - r, cy - r, true);
    duel_fb_px(fb, cx + r, cy - r, true); duel_fb_px(fb, cx - r, cy + r, true);
}

// A shallow integer-curvature dome peaking in the middle: the astral (left)
// city's signature motif — curved arches, domes, orbs.
static void floor_dome(duel_fb_t *fb, int lo, int hi, int top_y) {
    int cx = (lo + hi) / 2;
    int half = (hi - lo) / 2;
    if (half < 1) half = 1;
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
static void draw_floor_occupation(duel_fb_t *fb, uint8_t floor, bool is_left) {
#define OX(x) incantation_desk_x(is_left, x)
#define OSPAN(x0, x1, y) incantation_civic_hline(fb, is_left, (x0), (x1), (y))
#define ORECT(x0, y0, x1, y1) do { \
    int a_ = OX(x0), b_ = OX(x1); \
    archive_rect(fb, a_ < b_ ? a_ : b_, (y0), a_ < b_ ? b_ : a_, (y1)); \
} while (0)
    if (floor == DUEL_CIVIC_FLOOR_COMMONS) {
        /* Communal table / dispatch desk: the broadest horizontal mass. */
        OSPAN(3, 14, 96);
        OSPAN(3, 14, 97);
        for (int y = 98; y <= 105; y++) {
            duel_fb_px(fb, OX(5), y, true); duel_fb_px(fb, OX(13), y, true);
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
            for (int y = 91; y <= 95; y++) duel_fb_px(fb, OX(11), y, true);
            duel_fb_px(fb, OX(9), 94, true); duel_fb_px(fb, OX(13), 93, true);
        } else {
            /* Dispatch cubbies and clock. The shelf spans previously passed
             * unordered mirrored coordinates straight to duel_fb_hline, whose
             * loop draws nothing when x0 > x1 — so the right half never
             * actually showed its cubby shelves. */
            for (int y = 75; y <= 87; y += 6) OSPAN(25, 29, y);
            for (int x = 26; x <= 29; x += 3)
                for (int y = 70; y <= 90; y++) duel_fb_px(fb, OX(x), y, true);
            floor_gear(fb, OX(11), 90, 3);
            duel_fb_px(fb, OX(11), 87, true); duel_fb_px(fb, OX(13), 90, true);
            for (int x = 7; x <= 15; x += 4)
                for (int y = 99; y <= 103; y++) duel_fb_px(fb, OX(x), y, true);
        }
    } else if (floor == DUEL_CIVIC_FLOOR_RESEARCH) {
        /* Dominant telescope/analyzer, an unmistakable rising diagonal. */
        wiz_line(fb, OX(4), 98, OX(15), 76);
        wiz_line(fb, OX(5), 100, OX(16), 78);
        ORECT(11, 73, 17, 81);
        for (int y = 99; y <= 105; y++) duel_fb_px(fb, OX(7), y, true);
        wiz_line(fb, OX(7), 99, OX(3), 105);
        wiz_line(fb, OX(7), 99, OX(12), 105);
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
            duel_fb_px(fb, OX(25), 96, true); duel_fb_px(fb, OX(29), 98, true);
        } else {
            /* Probe, scope display, and specimen cylinder. */
            ORECT(2, 65, 12, 78);
            wiz_line(fb, OX(4), 75, OX(7), 69);
            wiz_line(fb, OX(7), 69, OX(10), 74);
            floor_gear(fb, OX(16), 84, 2);
            for (int y = 79; y <= 101; y++) {
                duel_fb_px(fb, OX(25), y, true); duel_fb_px(fb, OX(29), y, true);
            }
            OSPAN(25, 29, 99);
            duel_fb_px(fb, OX(27), 94, true); duel_fb_px(fb, OX(26), 96, true);
        }
    } else if (floor == DUEL_CIVIC_FLOOR_WORKSHOP) {
        /* Dominant forge: cauldron on astral, anvil/gear press on mechanical. */
        if (is_left) {
            floor_dome(fb, OX(3), OX(14), 90);
            OSPAN(4, 13, 96);
            for (int y = 97; y <= 103; y++) {
                duel_fb_px(fb, OX(5), y, true); duel_fb_px(fb, OX(13), y, true);
            }
            for (int x = 7; x <= 13; x += 2)
                duel_fb_px(fb, OX(x), 87 - ((x + 1) & 3), true);
        } else {
            OSPAN(3, 14, 94);
            OSPAN(6, 13, 95);
            wiz_line(fb, OX(9), 96, OX(7), 105);
            wiz_line(fb, OX(13), 96, OX(15), 105);
            floor_gear(fb, OX(11), 83, 4);
            for (int y = 68; y <= 79; y++) duel_fb_px(fb, OX(11), y, true);
            ORECT(7, 65, 15, 69); /* gear press crosshead */
        }
        /* Tool/reagent station and hoist/rack occupy the gap-side column. */
        ORECT(24, 78, 30, 104);
        for (int y = 84; y <= 100; y += 8)
            OSPAN(25, 29, y);
        if (is_left) {
            floor_dome(fb, OX(24), OX(30), 74);
            for (int x = 25; x <= 29; x += 2) {
                duel_fb_px(fb, OX(x), 81, true); duel_fb_px(fb, OX(x), 82, true);
            }
        } else {
            for (int y = 62; y <= 75; y++) duel_fb_px(fb, OX(27), y, true);
            ORECT(25, 74, 29, 79);
            duel_fb_px(fb, OX(27), 80, true); duel_fb_px(fb, OX(26), 81, true);
            floor_gear(fb, OX(27), 94, 2);
        }
    } else { /* Observatory: quiet instrument under a city-specific dome. */
        if (is_left) {
            floor_dome(fb, OX(2), OX(30), 66);
            floor_dome(fb, OX(5), OX(27), 70);
            wiz_line(fb, OX(7), 101, OX(18), 76);
            wiz_line(fb, OX(8), 102, OX(19), 77);
            ORECT(16, 73, 22, 80);
            duel_fb_px(fb, OX(9), 67, true);
            duel_fb_px(fb, OX(14), 72, true);
            duel_fb_px(fb, OX(24), 68, true);
        } else {
            ORECT(2, 65, 30, 69);
            for (int x = 4; x <= 28; x += 4) duel_fb_px(fb, OX(x), 67, true);
            floor_gear(fb, OX(16), 79, 7);
            wiz_line(fb, OX(7), 101, OX(16), 79);
            wiz_line(fb, OX(25), 101, OX(16), 79);
            for (int y = 87; y <= 104; y++) duel_fb_px(fb, OX(16), y, true);
        }
        ORECT(23, 83, 30, 104);
        wiz_line(fb, OX(24), 89, OX(29), 96);
        wiz_line(fb, OX(24), 96, OX(29), 89);
    }
#undef OSPAN
#undef ORECT
#undef OX
}

static void draw_floor_transition(duel_fb_t *fb, const duel_render_t *r,
                                  bool is_left) {
    if (!INCANTATION_FLOOR_TRANSITION_ACTIVE(r->floor_transition)) return;
    uint8_t phase = INCANTATION_FLOOR_TRANSITION_PHASE(r->floor_transition);
    int inner = is_left ? 31 : 0;
    if (phase == 0u) { /* source-room shutter */
        for (int x = 1; x < 32; x += 4) {
            int height = 12 + ((x + r->civic_phase) & 7);
            for (int y = 62; y < 62 + height; y++) duel_fb_px(fb, x, y, true);
            duel_fb_px(fb, x + 1, 62 + height, true);
        }
    } else if (phase == 1u) { /* full brick/elevator wipe */
        for (int y = 62; y <= 110; y++)
            for (int x = 0; x < 32; x++) duel_fb_px(fb, x, y, false);
        for (int y = 62; y <= 110; y += 5) {
            duel_fb_hline(fb, 0, 31, y);
            int offset = ((y / 5) & 1) ? 3 : 0;
            for (int x = offset; x < 32; x += 7)
                for (int dy = 1; dy < 5 && y + dy <= 110; dy++)
                    duel_fb_px(fb, x, y + dy, true);
        }
        for (int y = 62; y <= 110; y++) duel_fb_px(fb, inner, y, true);
    } else if (phase == 2u) { /* target-room reveal */
        for (int y = 62; y <= 82; y++)
            for (int x = 0; x < 32; x++) duel_fb_px(fb, x, y, false);
        for (int y = 62; y <= 82; y += 5) duel_fb_hline(fb, 0, 31, y);
        for (int x = 2; x < 32; x += 6) duel_fb_px(fb, x, 84, true);
    } else { /* settling dust / sparks */
        for (int i = 0; i < 8; i++) {
            int x = (i * 7 + r->seed) & 31;
            int y = 68 + ((i * 11 + r->civic_phase) % 38);
            duel_fb_px(fb, x, y, true);
            if ((i & 2) == 0) duel_fb_px(fb, x + (is_left ? -1 : 1), y + 1, true);
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
// x=31) and mirrored on the right OLED like the retired draw_archive.
static void draw_floor(duel_fb_t *fb, const duel_render_t *r, bool is_left) {
#define FLR_X(x) (is_left ? (x) : (DUEL_CANVAS_W - 1 - (x)))
    // The civic byte is authoritative for the occupation; during the first two
    // transition phases the outgoing (source) floor is still the one shown.
    uint8_t floor = incantation_effective_floor(r);
    uint8_t mode  = DUEL_CIVIC_MODE(r->civic);

    // Solid ceiling beam splitting the rooftop from the floor (both cities).
    // City character lives in the details below the beam, not the beam itself.
    for (int x = 0; x < DUEL_CANVAS_W; x++)
        duel_fb_px(fb, x, DUEL_FLOOR_BEAM_Y, true);
    if (r->revision & INCANTATION_AFTERMATH_WIRE) {
        uint8_t world = INCANTATION_AFTER_WORLD(r->shared_pres);
        if (world == WORLD_WONDER) {
            for (int x = 2; x < DUEL_CANVAS_W; x += 5)
                duel_fb_px(fb, x, 64 + (int)((x + r->civic_phase) & 3u), true);
        } else if (world == WORLD_CRISIS) {
            wiz_line(fb, FLR_X(12), 61, FLR_X(15), 66);
            wiz_line(fb, FLR_X(15), 66, FLR_X(17), 63);
        } else if (world == WORLD_RECOVERY) {
            for (int x = 4; x < DUEL_CANVAS_W; x += 8) duel_fb_px(fb, x, 63, true);
        }
    }
    if (is_left) {
        // A hanging astral arc under the beam near the gap.
        duel_fb_px(fb, FLR_X(28), 62, true);
        duel_fb_px(fb, FLR_X(29), 63, true);
        duel_fb_px(fb, FLR_X(30), 64, true);
    } else {
        for (int x = 1; x < DUEL_CANVAS_W; x += 6) duel_fb_px(fb, x, 62, true);
    }

    // Outer wall (edge away from the gap) with city texture, plus a short
    // lift-shaft stub by the gap.
    for (int y = 63; y <= 109; y++) duel_fb_px(fb, FLR_X(0), y, true);
    if (is_left) {
        // Astral: curved buttress studs bowing inward, with two wall orbs.
        for (int y = 66; y <= 106; y += 8) duel_fb_px(fb, FLR_X(1), y, true);
        duel_fb_px(fb, FLR_X(2), 74, true);
        duel_fb_px(fb, FLR_X(2), 96, true);
    } else {
        // Mechanical: riveted plating and straight tie-bars.
        for (int y = 65; y <= 107; y += 6) { duel_fb_px(fb, FLR_X(1), y, true); duel_fb_px(fb, FLR_X(2), y, true); }
    }
    for (int y = 63; y <= 70; y++) duel_fb_px(fb, FLR_X(30), y, true);
    if (is_left) {
        duel_fb_px(fb, FLR_X(29), 66, true);                              // counterweight orb
    } else {
        duel_fb_px(fb, FLR_X(29), 64, true); duel_fb_px(fb, FLR_X(28), 64, true); // gear teeth
    }

    // Big fixtures that own the empty upper two-thirds of the room: a framed
    // window set into the outer wall and a fixture hanging in the centre void.
    // Both are large, bold shapes chosen to read at desk distance (hardware
    // feedback: the old furniture was too thin/low to register). Authored in
    // desk space and mirrored per canvas.

    // Ground line of the room.
    duel_fb_hline(fb, 0, DUEL_CANVAS_W - 1, DUEL_FLOOR_Y1);

    // Occupation furniture, then the session-seeded resident living among it.
    draw_floor_occupation(fb, floor, is_left);
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
                res.task = RESIDENT_CHEER; res.action = DUEL_CIVIC_ACTION_REACT; break;
            case AFTER_COMPLAINT:
                res.task = RESIDENT_COMPLAIN; res.action = DUEL_CIVIC_ACTION_REACT; break;
            case AFTER_PANIC:
                res.task = RESIDENT_PANIC; res.action =
                    (after_phase & 1u) ? DUEL_CIVIC_ACTION_WALK : DUEL_CIVIC_ACTION_REACT;
                break;
            case AFTER_FIRE:
                if (after_phase == 0u) {
                    res.task = RESIDENT_PANIC; res.action = DUEL_CIVIC_ACTION_REACT;
                } else if (after_phase < 3u) {
                    res.task = RESIDENT_FIGHT_FIRE; res.action = DUEL_CIVIC_ACTION_WORK;
                } else {
                    res.task = RESIDENT_REPAIR; res.action = DUEL_CIVIC_ACTION_WORK;
                }
                break;
            case AFTER_INSPECT:
                res.task = RESIDENT_INSPECT; res.action = DUEL_CIVIC_ACTION_INSPECT; break;
            case AFTER_REPAIR:
                res.task = RESIDENT_REPAIR; res.action = DUEL_CIVIC_ACTION_WORK; break;
            case AFTER_MAX_CAST:
                res.task = after_phase < 2u ? RESIDENT_WATCH_CAST : RESIDENT_CHEER;
                res.action = after_phase < 2u ? DUEL_CIVIC_ACTION_WATCH_ROOF : DUEL_CIVIC_ACTION_REACT;
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
        res.task = target == DUEL_CIVIC_EVENT_TARGET_SHARED ? RESIDENT_DIPLO_NEUTRAL :
                   ((target == DUEL_CIVIC_EVENT_TARGET_LEFT) == is_left
                        ? RESIDENT_DIPLO_PROUD : RESIDENT_DIPLO_RECEIVING);
    }
    civic_resident_draw(fb, &res, is_left, mode, 0);

    /* Lasting room/object consequences. They share the authoritative aftermath
     * phase with the resident task, so reconnecting halves resume mid-arc. All
     * marks use the same floor/action descriptor as the assigned civic task. */
    uint8_t mark_action = after_kind == AFTER_INSPECT ? DUEL_CIVIC_ACTION_INSPECT :
                          after_kind == AFTER_COMPLAINT ? DUEL_CIVIC_ACTION_INSPECT :
                          after_kind == AFTER_PANIC ? DUEL_CIVIC_ACTION_REACT :
                          after_kind == AFTER_MAX_CAST ? DUEL_CIVIC_ACTION_WATCH_ROOF :
                          after_kind == AFTER_CHEER ? DUEL_CIVIC_ACTION_REACT :
                          DUEL_CIVIC_ACTION_WORK;
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
            wiz_line(fb, FLR_X(mark.x - 6), my + 5, mx, my - 1); /* hose / spell stream */
        if (after_phase == 3u) {
            wiz_line(fb, FLR_X(mark.x - 3), my - 3, FLR_X(mark.x + 3), my + 3);
            wiz_line(fb, FLR_X(mark.x - 3), my + 3, FLR_X(mark.x + 3), my - 3);
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
            wiz_line(fb, FLR_X(mark.x - 3), my - 4, FLR_X(mark.x + 3), my + 4);
            wiz_line(fb, FLR_X(mark.x + 3), my - 4, FLR_X(mark.x - 2), my + 3);
        } else {
            incantation_civic_hline(fb, is_left, mark.x - 3, mark.x + 3, my + 3);
            duel_fb_px(fb, mx, my, true);
        }
    } else if (after_kind == AFTER_MAX_CAST) {
        int motes = 6 - after_phase;
        for (int i = 0; i < motes; i++)
            duel_fb_px(fb, FLR_X(mark.x - 10 + i * 4), my - 15 +
                       (int)((r->civic_phase + i * 3u) % 12u), true);
        if (after_phase == 2u)
            incantation_civic_hline(fb, is_left, mark.x - 8, mark.x + 8, my + 8);
    } else if (after_kind == AFTER_COMPLAINT) {
        duel_fb_px(fb, mx, my, true);
        incantation_civic_hline(fb, is_left, mark.x - 4, mark.x, my - 1);
    } else if (after_kind == AFTER_CHEER) {
        duel_fb_px(fb, FLR_X(mark.x - 2), my - 2 - after_phase, true);
        duel_fb_px(fb, mx, my - 4 + after_phase, true);
    }

    // Paving course in the band freed by relocating HP up to the rooftop
    // (y112-115). A 1-byte session seed staggers the pattern so each boot lays a
    // slightly different pavement without streaming anything. Astral sets rounded
    // cobbles; mechanical lays rectangular flagstones with joints.
    uint8_t g = r->seed;
    if (is_left) {
        for (int x = (g & 3); x < DUEL_CANVAS_W; x += 4) {
            duel_fb_px(fb, x + 1, 113, true);                      // cobble crown
            duel_fb_px(fb, x, 114, true);
            duel_fb_px(fb, x + 1, 114, true);
            duel_fb_px(fb, x + 2, 114, true);
        }
    } else {
        for (int x = 0; x < DUEL_CANVAS_W; x++)
            if (((x + (g & 1)) & 3) != 3) duel_fb_px(fb, x, 114, true); // flagstone tops
        for (int x = 1 + (g & 3); x < DUEL_CANVAS_W; x += 4)
            duel_fb_px(fb, x, 112, true);                          // vertical joints
    }

    // Foundation coursing, clear of the y127 odometer. The capstone line reads as
    // masonry base; regular joints below give texture without noise. Left is a
    // sparser astral course; right a denser mechanical one.
    duel_fb_hline(fb, 0, DUEL_CANVAS_W - 1, 117);                       // capstone (both cities)
    if (is_left) {
        for (int x = 2; x < DUEL_CANVAS_W; x += 8)                 // sparse ashlar joints
            for (int y = 119; y <= 122; y++) duel_fb_px(fb, x, y, true);
    } else {
        for (int x = 0; x < DUEL_CANVAS_W; x += 4)                 // dense brick joints, offset course
            for (int y = 119; y <= 122; y++) duel_fb_px(fb, x + ((y >> 1) & 1) * 2, y, true);
    }
    draw_floor_transition(fb, r, is_left);
#undef FLR_X
}

// A compact standing wizard (~1/3 of the original M1 figure, hardware
// feedback: full-size read as a blob at actual OLED scale). Centred at x=16
// with the staff hand at y~64 so bolts fly out at cast height. xo/yo shift
// the whole figure (duel_fb_px clips, so off-canvas offsets are free); the
// M5 lifecycle uses them to sink a collapsing wizard and walk in a fresh one.
static void wiz_body(duel_fb_t *fb, bool casting, int facing, uint8_t variant, int xo, int yo) {
    const int cx = DUEL_CANVAS_W / 2 + xo;   // 16 + xo

    // Pointed hat: filled triangle apex -> base (max half-width 3).
    const int hat_apex_y = 54 + yo + DUEL_ROOF_DY;
    const int hat_base_y = 61 + yo + DUEL_ROOF_DY;
    for (int y = hat_apex_y; y <= hat_base_y; y++) {
        int hw = (y - hat_apex_y) * 3 / (hat_base_y - hat_apex_y); // 0..3
        duel_fb_hline(fb, cx - hw, cx + hw, y);
    }
    // Hat brim.
    duel_fb_hline(fb, cx - 4, cx + 4, hat_base_y + 1);

    // Robe: trapezoid widening toward the base (half-width 2..4).
    const int robe_top = hat_base_y + 2;   // 63 + yo
    const int robe_bot = 75 + yo + DUEL_ROOF_DY;
    for (int y = robe_top; y <= robe_bot; y++) {
        int hw = 2 + (y - robe_top) * 2 / (robe_bot - robe_top); // 2..4
        duel_fb_hline(fb, cx - hw, cx + hw, y);
    }

    // Roster variant masks (M5): pose-invariant hat/robe markings only, so a
    // replacement is recognisably a new combatant in every pose. Variant 0 is
    // the untouched base look — its bytes must match pre-M5 output exactly.
    switch (variant & 3) {
        case 1: // hat band: a cleared 1-px stripe across the hat fill (hw=2 row)
            for (int x = cx - 2; x <= cx + 2; x++) duel_fb_px(fb, x, hat_apex_y + 5, false);
            break;
        case 2: // robe hem fringe: 3 dots one row under the robe bottom
            duel_fb_px(fb, cx - 3, robe_bot + 1, true);
            duel_fb_px(fb, cx, robe_bot + 1, true);
            duel_fb_px(fb, cx + 3, robe_bot + 1, true);
            break;
        case 3: // hat pompom: 2 px above the apex (clear of the cast burst at y~50)
            duel_fb_px(fb, cx, hat_apex_y - 2, true);
            duel_fb_px(fb, cx, hat_apex_y - 1, true);
            break;
    }

    // Staff along the facing side, just outside the robe.
    int sx = cx + facing * 6;
    if (!casting) {
        // Resting: vertical staff with an orb finial near the top.
        for (int y = 64 + yo + DUEL_ROOF_DY; y <= robe_bot; y++) duel_fb_px(fb, sx, y, true);
        duel_fb_px(fb, sx, 62 + yo + DUEL_ROOF_DY, true);
        duel_fb_px(fb, sx - facing, 63 + yo + DUEL_ROOF_DY, true);
    } else {
        // Casting: staff raised toward the hat. The progressive scry.5 charge is
        // drawn separately from authoritative wind-up state in wiz_draw_scene.
        wiz_line(fb, cx + facing * 2, 68 + yo + DUEL_ROOF_DY, cx + facing * 5, 56 + yo + DUEL_ROOF_DY);
        duel_fb_px(fb, cx + facing * 5, 55 + yo + DUEL_ROOF_DY, true); // staff-tip focus
    }
}

void wiz_draw(duel_fb_t *fb, bool casting, int facing, uint8_t variant) {
    wiz_body(fb, casting, facing, variant, 0, 0);
}

// M5 fallen wizard: horizontal body with the head AWAY from the gap (the
// medic later drags it toward that edge), hat knocked off past the head,
// staff dropped on the ground toward the gap. -facing is the away direction
// on both halves (left: gap at x=31, facing +1; right: gap at x=0, facing -1).
static void wiz_downed(duel_fb_t *fb, int facing, uint8_t variant, int xo) {
    const int cx = DUEL_CANVAS_W / 2 + xo;   // 16 + xo
    int head = cx - facing * 5;              // head end, away from the gap
    int feet = cx + facing * 3;
    int lo = head < feet ? head : feet, hi = head < feet ? feet : head;
    duel_fb_hline(fb, lo, hi, 72 + DUEL_ROOF_DY);
    duel_fb_hline(fb, lo, hi, 73 + DUEL_ROOF_DY);
    // Fallen hat just past the head: 3-px base with a 1-px apex row on top.
    int hx = head - facing * 2;              // hat centre, one px clear of the head
    duel_fb_hline(fb, hx < head ? hx - 1 : head + 1, hx < head ? head - 1 : hx + 1, 72 + DUEL_ROOF_DY);
    duel_fb_px(fb, hx, 71 + DUEL_ROOF_DY, true);
    if ((variant & 3) == 3) duel_fb_px(fb, hx, 70 + DUEL_ROOF_DY, true); // pompom stayed on
    // Dropped staff: flat on the ground, toward the gap.
    for (int i = 1; i <= 5; i++) duel_fb_px(fb, cx + facing * i, 75 + DUEL_ROOF_DY, true);
}

// M5 medic: a short hatless figure (8 px, clearly not a wizard) leaning into
// the drag — 2x2 head, torso kinked 1 px toward -facing, splayed legs.
static void medic_draw(duel_fb_t *fb, int x, int facing) {
    duel_fb_px(fb, x, 66 + DUEL_ROOF_DY, true); duel_fb_px(fb, x + 1, 66 + DUEL_ROOF_DY, true);
    duel_fb_px(fb, x, 67 + DUEL_ROOF_DY, true); duel_fb_px(fb, x + 1, 67 + DUEL_ROOF_DY, true);
    for (int y = 68 + DUEL_ROOF_DY; y <= 70 + DUEL_ROOF_DY; y++) duel_fb_px(fb, x, y, true);
    duel_fb_px(fb, x - facing, 71 + DUEL_ROOF_DY, true);
    duel_fb_px(fb, x - facing, 72 + DUEL_ROOF_DY, true);
    duel_fb_px(fb, x - facing - 1, 73 + DUEL_ROOF_DY, true);
    duel_fb_px(fb, x - facing + 1, 73 + DUEL_ROOF_DY, true);
}

/* Battlefield gap band: u in [DUEL_U_GAP_LO, DUEL_U_GAP_HI] is between the two
 * canvases (visible on neither half). The flare windows and per-half mapping
 * below all share these fenceposts. */
#define DUEL_U_GAP_LO 96u
#define DUEL_U_GAP_HI 159u

// x of the centre-gap edge on this half, and the horizontal direction that
// moves further inward (toward the wizard, away from the gap).
static inline int gap_edge_x(bool is_left) { return is_left ? 31 : 0; }
static inline int inward_dir(bool is_left) { return is_left ? -1 : 1; }

bool duel_battlefield_to_x(uint8_t u, bool is_left, int *x) {
    if (is_left) {
        if (u >= DUEL_U_GAP_LO) return false;
        *x = 22 + u / 10; // staff tip (22) -> gap edge (31)
        return true;
    }
    if (u <= DUEL_U_GAP_HI) return false;
    *x = 9 - (255 - u) / 10; // gap edge (0) -> staff tip (9)
    return true;
}

#define SPELL_Y_BASE (63 + DUEL_ROOF_DY)

static int spell_lane_y(uint8_t kind) {
    switch (DUEL_KIND_ELEMENT(kind)) {
        case ELEM_FROST: return SPELL_Y_BASE - 5;
        case ELEM_VOID:  return SPELL_Y_BASE - 2;
        case ELEM_EMBER: return SPELL_Y_BASE + 3;
        default:         return SPELL_Y_BASE;
    }
}

static void spell_glyph(duel_fb_t *fb, int x, int y, uint8_t kind, int dir) {
    int back    = dir > 0 ? -1 : +1;
    int tier    = DUEL_KIND_TIER(kind);

    // Element identity stays primary while the capped recipe tier controls the
    // carrier's footprint. Short is deliberately compact; medium matches M6's
    // normal scale; long/saturated add bounded mass and trail complexity.
    switch (DUEL_KIND_ELEMENT(kind)) {
        case ELEM_FORCE: {
            int rx = tier == SPELL_TIER_SHORT ? 0 : (tier >= SPELL_TIER_LONG ? 2 : 1);
            int ry = tier == SPELL_TIER_SHORT ? 0 : (tier == SPELL_TIER_SATURATED ? 2 : 1);
            for (int dx = -rx; dx <= rx; dx++)
                for (int dy = -ry; dy <= ry; dy++) duel_fb_px(fb, x + dx, y + dy, true);
            if (tier == SPELL_TIER_LONG) {
                duel_fb_px(fb, x, y - 2, true);
                duel_fb_px(fb, x, y + 2, true);
            }
            break;
        }
        case ELEM_FROST: {
            int radius = tier == SPELL_TIER_SHORT ? 1 : (tier >= SPELL_TIER_LONG ? 3 : 2);
            for (int d = -radius; d <= radius; d++) {
                duel_fb_px(fb, x + d, y, true);
                duel_fb_px(fb, x, y + d, true);
            }
            int diag = tier >= SPELL_TIER_LONG ? 2 : 1;
            duel_fb_px(fb, x - diag, y - diag, true); duel_fb_px(fb, x + diag, y - diag, true);
            duel_fb_px(fb, x - diag, y + diag, true); duel_fb_px(fb, x + diag, y + diag, true);
            if (tier == SPELL_TIER_SATURATED) {
                duel_fb_px(fb, x - 3, y - 3, true); duel_fb_px(fb, x + 3, y - 3, true);
                duel_fb_px(fb, x - 3, y + 3, true); duel_fb_px(fb, x + 3, y + 3, true);
            }
            break;
        }
        case ELEM_VOID: {
            int rx = tier == SPELL_TIER_SHORT ? 1 : (tier >= SPELL_TIER_LONG ? 2 + (tier == SPELL_TIER_SATURATED) : 1);
            int ry = tier >= SPELL_TIER_LONG ? 2 : 1;
            for (int dx = -rx; dx <= rx; dx++) {
                duel_fb_px(fb, x + dx, y - ry, true);
                duel_fb_px(fb, x + dx, y + ry, true);
            }
            for (int dy = -ry + 1; dy < ry; dy++) {
                duel_fb_px(fb, x - rx, y + dy, true);
                duel_fb_px(fb, x + rx, y + dy, true);
            }
            if (tier == SPELL_TIER_SHORT) duel_fb_px(fb, x, y - 1, false); // diamond-like, hollow core
            break;
        }
        case ELEM_EMBER: {
            int core = tier >= SPELL_TIER_LONG ? 2 : 1;
            for (int d = -core; d <= core; d++) {
                duel_fb_px(fb, x + d, y, true);
                duel_fb_px(fb, x, y + d, true);
            }
            int tail = 2 + tier * 2;
            for (int d = 2; d <= tail; d++) duel_fb_px(fb, x + d * back, y - (d & 1), true);
            if (tier >= SPELL_TIER_LONG) {
                duel_fb_px(fb, x + 2 * back, y + 2, true);
                duel_fb_px(fb, x + 4 * back, y + 1, true);
            }
            break;
        }
    }

    switch (DUEL_KIND_MODIFIER(kind)) {
        case MOD_NONE:
            break;
        case MOD_SWIFT: // speed streak grows modestly with presentation tier
            for (int d = 2; d <= 4 + tier; d++) duel_fb_px(fb, x + d * back, y, true);
            break;
        case MOD_HEAVY: { // a heavy diagonal casing outside the element core
            int shell = tier >= SPELL_TIER_LONG ? 3 : 2;
            duel_fb_px(fb, x - shell, y - shell, true); duel_fb_px(fb, x + shell, y - shell, true);
            duel_fb_px(fb, x - shell, y + shell, true); duel_fb_px(fb, x + shell, y + shell, true);
            break;
        }
    }
}

static int incantation_trajectory_y(uint32_t desc, uint8_t flight) {
    switch (SPELL_DESC_TRAJECTORY(desc)) {
        case TRAJ_GROUND: return SPELL_Y_BASE + 11;
        case TRAJ_LOW: return SPELL_Y_BASE + 5;
        case TRAJ_HIGH: return SPELL_Y_BASE - 7;
        case TRAJ_ROOF: {
            uint8_t half = flight < 128u ? flight : (uint8_t)(255u - flight);
            return SPELL_Y_BASE - 8 - half / 12;
        }
        case TRAJ_RETURNING: return SPELL_Y_BASE - 2 - flight / 24;
        case TRAJ_HOMING: return SPELL_Y_BASE - 7 + flight / 24;
        case TRAJ_AREA: return SPELL_Y_BASE;
        default: return SPELL_Y_BASE;
    }
}

static void draw_orbiting_motes(duel_fb_t *fb, uint8_t count, int cx, int cy,
                                uint32_t frame, uint8_t spread, int facing) {
    for (uint8_t i = 0; i < count; i++) {
        int sx = (int)(i % 3u) - 1;
        int sy = (int)(i / 3u) * 2 - 1;
        int wobble = (int)((frame + i) & 1u);
        duel_fb_px(fb, cx + sx * spread * facing, cy + sy * 2 + wobble, true);
    }
}

static uint8_t draw_tempo_interval(uint32_t desc, uint8_t deliberate,
                                   uint8_t flowing, uint8_t rapid, uint8_t frantic) {
    switch (SPELL_DESC_TEMPO(desc)) {
        case TEMPO_DELIBERATE: return deliberate;
        case TEMPO_RAPID: return rapid;
        case TEMPO_FRANTIC: return frantic;
        default: return flowing;
    }
}

static uint8_t draw_trend_flight(uint32_t desc, uint8_t linear) {
    uint16_t v = linear;
    if (SPELL_DESC_TREND(desc) == TREND_ACCELERATING)
        v = (uint16_t)linear * linear / 240u;
    else if (SPELL_DESC_TREND(desc) == TREND_DECELERATING) {
        uint16_t remain = (uint16_t)(240u - linear);
        v = 240u - remain * remain / 240u;
    } else if (SPELL_DESC_TREND(desc) == TREND_IRREGULAR && linear > 8u)
        v = (uint16_t)(linear + ((linear / 16u) & 1u ? 7u : 0u));
    return v > 240u ? 240u : (uint8_t)v;
}

static bool incantation_in_gap(uint8_t u) {
    return u >= DUEL_U_GAP_LO && u <= DUEL_U_GAP_HI;
}

static void incantation_draw_inner_flare(duel_fb_t *fb, uint32_t desc, bool is_left,
                                 int y, uint8_t phase, uint8_t reach) {
    int edge = gap_edge_x(is_left);
    int inward = inward_dir(is_left);
    for (uint8_t i = 0; i < reach; i++)
        duel_fb_px(fb, edge + inward * i, y + ((i + phase) & 1u), true);
    if (SPELL_DESC_ELEMENT(desc) == ELEM_FROST) {
        duel_fb_px(fb, edge + inward, y - 2, true);
        duel_fb_px(fb, edge + inward, y + 2, true);
    } else if (SPELL_DESC_ELEMENT(desc) == ELEM_EMBER) {
        duel_fb_px(fb, edge + inward * (reach + 1u), y - 1 - (phase & 1u), true);
    } else if (SPELL_DESC_ELEMENT(desc) == ELEM_VOID) {
        duel_fb_px(fb, edge + inward * 2, y, false);
    }
}

/* The physical battlefield remains blank for u=96..159. These edge-local
 * handoffs communicate deterministic travel without changing carrier state,
 * collision time, or the desk-gap duration. */
static bool incantation_draw_gap_cue(duel_fb_t *fb, uint32_t desc, uint8_t form,
                             uint8_t progress, uint8_t flight, uint8_t u,
                             uint8_t caster_side, bool is_left, int y) {
    bool reflected_singularity = form == SPELL_SINGULARITY &&
                                 progress >= 160u && progress <= 207u;
    if (!incantation_in_gap(u) && !reflected_singularity) return false;
    uint8_t phase = (uint8_t)((progress + SPELL_DESC_VARIANCE(desc)) & 3u);
    bool caster_local = is_left == (caster_side == SIM_SIDE_L);
    bool portal = SPELL_DESC_INTERACTION(desc) == INTERACT_PHASE ||
                  SPELL_DESC_ELEMENT(desc) == ELEM_VOID ||
                  SPELL_DESC_TRAJECTORY(desc) == TRAJ_RETURNING ||
                  form == SPELL_CONJURE || reflected_singularity;
    bool trail = form == SPELL_SWARM ||
                 SPELL_DESC_TRAJECTORY(desc) == TRAJ_HOMING ||
                 SPELL_DESC_TRAJECTORY(desc) == TRAJ_AREA;
    int edge = gap_edge_x(is_left);
    int inward = inward_dir(is_left);
    if (portal) {
        /* Paired rune mouths persist at both inner edges. */
        for (int d = -3; d <= 3; d++) {
            duel_fb_px(fb, edge + inward * (1 + (d == 0)), y + d, true);
            if ((d + phase) % 3 == 0)
                duel_fb_px(fb, edge + inward * 3, y + d, true);
        }
        duel_fb_px(fb, edge, y - 2 + (phase & 1u), true);
        duel_fb_px(fb, edge, y + 2 - (phase & 1u), true);
        duel_fb_px(fb, edge + inward * 4, y + (caster_local ? -2 : 2), true);
    } else if (trail) {
        /* Broad/homing carriers leave synchronized traces on both edges. */
        for (int i = 0; i < 4; i++)
            duel_fb_px(fb, edge + inward * i, y + ((i + phase) % 3) - 1, true);
        duel_fb_px(fb, edge + inward * 2, y - 3, true);
        duel_fb_px(fb, edge + inward * 2, y + 3, true);
        duel_fb_px(fb, edge + inward * 4, y + (caster_local ? -2 : 2), true);
    } else {
        /* Ordinary departure shrinks over the first half; destination motes
         * grow over the second. Only the relevant city edge participates. */
        bool departure = flight < 128u;
        if ((departure && !caster_local) || (!departure && caster_local)) return true;
        uint8_t local = departure ? (uint8_t)(flight - DUEL_U_GAP_LO) :
                                    (uint8_t)(flight - 128u);
        uint8_t reach = departure ? (uint8_t)(3u - local / 11u) :
                                    (uint8_t)(1u + local / 11u);
        if (reach < 1u) reach = 1u;
        if (reach > 3u) reach = 3u;
        incantation_draw_inner_flare(fb, desc, is_left, y, phase, reach);
        duel_fb_px(fb, edge + inward * 4, y + (departure ? -2 : 2), true);
    }
    return true;
}

void incantation_draw_spell(duel_fb_t *fb, const duel_view_spell_t *spell,
                    uint8_t caster_side, uint8_t variant, bool is_left,
                    uint32_t frame) {
    uint8_t form = SPELL_DESC_FORM(spell->descriptor);
    uint8_t progress = spell->progress;
    uint8_t flight = progress;
    uint8_t phase = progress & 31u;
    bool caster_local = is_left == (caster_side == SIM_SIDE_L);
    int facing = is_left ? 1 : -1;
    int local_cx = is_left ? 16 : 15;
    int travel_dir = caster_side == SIM_SIDE_L ? 1 : -1;
    if (form == SPELL_SWARM) {
        uint8_t interval = draw_tempo_interval(spell->descriptor, 10u, 8u, 6u, 4u);
        flight = phase < 12u ? 8u : draw_trend_flight(spell->descriptor,
                 (uint8_t)((uint16_t)(phase - 12u) * 240u / (interval - 1u)));
    }
    if (form == SPELL_CONJURE) {
        bool trap = SPELL_DESC_TRAJECTORY(spell->descriptor) == TRAJ_GROUND ||
                    SPELL_DESC_TRAJECTORY(spell->descriptor) == TRAJ_AREA;
        uint8_t interval = draw_tempo_interval(spell->descriptor, 15u, 12u, 9u, 6u);
        flight = trap ? (uint8_t)(phase < 16u ? phase * 5u : 80u) :
                 phase < 10u ? 8u : draw_trend_flight(spell->descriptor,
                 (uint8_t)((uint16_t)(phase - 10u) * 240u / (interval - 1u)));
    }
    if (SPELL_DESC_TRAJECTORY(spell->descriptor) == TRAJ_RETURNING &&
        form != SPELL_CONJURE) {
        flight = flight < 128u ? flight : (uint8_t)(255u - flight);
        if (progress >= 128u) travel_dir = -travel_dir;
    }
    if (form == SPELL_SINGULARITY && progress < 192u) flight = 48u;
    uint8_t u = caster_side == SIM_SIDE_L ? flight : (uint8_t)(255u - flight);
    int x = 0;
    int bob = SPELL_DESC_TREND(spell->descriptor) == TREND_IRREGULAR ?
              (int)((frame + SPELL_DESC_VARIANCE(spell->descriptor)) & 1u) : 0;
    int y = incantation_trajectory_y(spell->descriptor, flight) + bob;

    if (form == SPELL_BEAM) {
        int yb = incantation_trajectory_y(spell->descriptor, 192u);
        bool full = progress >= 64u && progress < 224u;
        bool fizzle = progress >= 224u;
        int x0 = is_left ? 21 : 0;
        int x1 = is_left ? 31 : 10;
        if (fizzle) {
            for (int px = x0; px <= x1; px += 2) duel_fb_px(fb, px, yb + (px & 1), true);
        } else {
            wiz_line(fb, x0, yb, x1, yb);
        }
        if (full) wiz_line(fb, x0, yb + 1, x1, yb + 1);
        if (full && SPELL_DESC_MAGNITUDE(spell->descriptor) >= 3u)
            wiz_line(fb, x0, yb - 1, x1, yb - 1);
        if (variant == 2u) {
            for (int px = 3; px < DUEL_CANVAS_W; px += 6) duel_fb_px(fb, px, yb - 2, true);
        }
        if (caster_local) {
            int origin = is_left ? 21 : 10;
            duel_fb_px(fb, origin, yb - 2, true);
            duel_fb_px(fb, origin - facing, yb + 2, true);
        }
        if (incantation_in_gap(progress))
            incantation_draw_inner_flare(fb, spell->descriptor, is_left, yb,
                                 (uint8_t)(progress & 3u),
                                 (uint8_t)(2u + (progress - DUEL_U_GAP_LO) / 21u));
        return;
    }

    if (form == SPELL_CHAIN) {
        int x0 = is_left ? (caster_local ? 21 : 31) : (caster_local ? 10 : 0);
        int x1 = is_left ? (caster_local ? 31 : 21) : (caster_local ? 0 : 10);
        int dir = x1 > x0 ? 1 : -1;
        int reach = 3 + (int)(progress / 32u);
        if (reach > 10) reach = 10;
        int px = x0, py = y;
        for (int i = 1; i <= reach; i++) {
            int nx = x0 + dir * i;
            int ny = y + ((i + SPELL_DESC_VARIANCE(spell->descriptor)) & 1 ? -2 : 2);
            wiz_line(fb, px, py, nx, ny); px = nx; py = ny;
        }
        duel_fb_px(fb, x1, y, true);
        if (incantation_in_gap(progress))
            incantation_draw_inner_flare(fb, spell->descriptor, is_left, y,
                                 (uint8_t)(progress & 3u),
                                 (uint8_t)(2u + (progress - DUEL_U_GAP_LO) / 21u));
        return;
    }

    if (incantation_draw_gap_cue(fb, spell->descriptor, form, progress, flight, u,
                         caster_side, is_left, y))
        return;

    if (form == SPELL_SWARM) {
        uint8_t count = progress >> 5;
        if (caster_local) {
            uint8_t orbit_count = phase < 12u ? count : count ? (uint8_t)(count - 1u) : 0u;
            draw_orbiting_motes(fb, orbit_count, local_cx + facing * 2,
                                SPELL_Y_BASE - 6, frame, 3u, facing);
        }
        if (phase < 12u || !count) return;
        if (!duel_battlefield_to_x(u, is_left, &x)) return;
        spell_glyph(fb, x, y, spell->kind, travel_dir);
        return;
    }

    if (form == SPELL_CONJURE) {
        bool trap = SPELL_DESC_TRAJECTORY(spell->descriptor) == TRAJ_GROUND ||
                    SPELL_DESC_TRAJECTORY(spell->descriptor) == TRAJ_AREA;
        uint8_t charges = progress >> 5;
        if (trap) {
            if (!duel_battlefield_to_x(u, is_left, &x)) return;
            y = SPELL_Y_BASE + 11;
            wiz_line(fb, x - 3, y, x + 3, y);
            duel_fb_px(fb, x - 2, y - 1, true); duel_fb_px(fb, x + 2, y - 1, true);
            if ((frame & 3u) == 0u) duel_fb_px(fb, x, y - 3, true);
        } else {
            if (caster_local)
                draw_orbiting_motes(fb, 1u + (charges > 2u), local_cx - facing * 5,
                                    SPELL_Y_BASE - 8, frame, 2u, facing);
            if (phase < 10u || !charges) return;
            if (!duel_battlefield_to_x(u, is_left, &x)) return;
            duel_fb_px(fb, x, y, true); duel_fb_px(fb, x - travel_dir, y - 1, true);
        }
        return;
    }
    if (!duel_battlefield_to_x(u, is_left, &x)) return;

    if (form == SPELL_FIREBALL) {
        spell_glyph(fb, x, y, spell->kind, travel_dir);
        duel_fb_px(fb, x - travel_dir, y + 2, true);
        duel_fb_px(fb, x - 2 * travel_dir, y + 3, true);
    } else if (form == SPELL_SINGULARITY) {
        int radius = progress < 128u ? 2 : progress < 192u ? 3 : 2;
        for (int d = -radius; d <= radius; d++) {
            duel_fb_px(fb, x + d, y - radius, true);
            duel_fb_px(fb, x + d, y + radius, true);
            duel_fb_px(fb, x - radius, y + d, true);
            duel_fb_px(fb, x + radius, y + d, true);
        }
        duel_fb_px(fb, x, y, false);
        if (progress >= 128u && progress < 192u) {
            duel_fb_px(fb, x - 1, y, true); duel_fb_px(fb, x + 1, y, true);
        }
    } else if (form == SPELL_GROUND_WAVE) {
        int dir = caster_side == SIM_SIDE_L ? 1 : -1;
        for (int i = 0; i < 7; i++)
            duel_fb_px(fb, x - dir * i, y - (i & 1), true);
    } else {
        spell_glyph(fb, x, y, spell->kind, travel_dir);
    }

    uint8_t trail = SPELL_DESC_TEMPO(spell->descriptor);
    int back = caster_side == SIM_SIDE_L ? -1 : 1;
    for (uint8_t i = 0; i < trail; i++)
        duel_fb_px(fb, x + back * (3 + i * 2), y + (i & 1u), true);
    if (SPELL_DESC_PAYLOAD(spell->descriptor) == PAY_HEAL) {
        duel_fb_px(fb, x - 2, y - 2, true); duel_fb_px(fb, x + 2, y - 2, true);
        duel_fb_px(fb, x, y + 2, true);
        if (SPELL_DESC_TRAJECTORY(spell->descriptor) == TRAJ_RETURNING)
            duel_fb_px(fb, x - travel_dir * 4, y + 1, true);
    } else if (SPELL_DESC_TRAJECTORY(spell->descriptor) == TRAJ_AREA) {
        duel_fb_px(fb, x - 3, y, true); duel_fb_px(fb, x + 3, y, true);
        duel_fb_px(fb, x, y - 3, true); duel_fb_px(fb, x, y + 3, true);
    }

    /* Roster voice accents are recipe-cosmetic only. */
    if (variant == 1u) duel_fb_px(fb, x - 3 * travel_dir, y + 1, true);
    else if (variant == 2u) { duel_fb_px(fb, x - 2, y - 3, true); duel_fb_px(fb, x + 2, y - 3, true); }
    else if (variant == 3u) duel_fb_px(fb, x, y, false);
}

static void draw_incantation_status(duel_fb_t *fb, const duel_view_wizard_t *wz,
                            int facing, uint32_t frame) {
    if (!wz->status || !wz->status_intensity) return;
    int cx = 16 - facing * 5;
    int cy = 55 + DUEL_ROOF_DY;
    int phase = (int)(frame & 3u);
    if (wz->status == STATUS_BURNING) {
        for (int i = 0; i < wz->status_intensity; i++) duel_fb_px(fb, cx + i - 1, cy - phase - i, true);
    } else if (wz->status == STATUS_FROZEN) {
        duel_fb_px(fb, cx - 2, cy, true); duel_fb_px(fb, cx + 2, cy, true);
        duel_fb_px(fb, cx, cy - 2, true); duel_fb_px(fb, cx, cy + 2, true);
    } else if (wz->status == STATUS_DISRUPTED) {
        duel_fb_px(fb, cx - 2, cy - 1, true); duel_fb_px(fb, cx, cy, true);
        duel_fb_px(fb, cx + 2, cy + 1, true);
    } else {
        duel_fb_px(fb, cx, cy, true);
        duel_fb_px(fb, cx - 1, cy - 1, true); duel_fb_px(fb, cx + 1, cy - 1, true);
    }
}

static void draw_incantation_reaction(duel_fb_t *fb, uint8_t outcome, bool is_left,
                              uint8_t frames) {
    if (!frames || outcome < FX_HEAL_L || outcome > FX_COLLAPSE) return;
    int x = is_left ? 5 : DUEL_CANVAS_W - 1 - 5;
    int y = 101;
    if (outcome == FX_HEAL_L || outcome == FX_HEAL_R) { /* civic cheer/confetti */
        duel_fb_px(fb, x - 2, y - 2, true); duel_fb_px(fb, x + 2, y - 2, true);
        wiz_line(fb, x - 1, y, x + 1, y);
    } else if (outcome == FX_COMPLAINT) {
        wiz_line(fb, x - 2, y, x + 1, y);
        duel_fb_px(fb, x + 2, y - 1, true);
    } else if (outcome == FX_DETONATE) { /* roof explosion */
        x = is_left ? 27 : 4; y = SPELL_Y_BASE + 14;
        for (int d = 1; d <= 5; d++) {
            duel_fb_px(fb, x - d, y - d, true); duel_fb_px(fb, x + d, y - d, true);
            duel_fb_px(fb, x - d, y + (d & 1), true); duel_fb_px(fb, x + d, y + (d & 1), true);
        }
        wiz_line(fb, x - 6, y, x + 6, y);
    } else if (outcome == FX_RESIDUE) {
        duel_fb_px(fb, x - 2, y, true); duel_fb_px(fb, x + 2, y, true);
        duel_fb_px(fb, x, y - 2, true); duel_fb_px(fb, x, y + 2, true);
    } else if (outcome == FX_COMBINE) {
        wiz_line(fb, x - 2, y - 2, x + 2, y - 2);
        duel_fb_px(fb, x - 2, y - 1, true); duel_fb_px(fb, x + 2, y - 1, true);
        duel_fb_px(fb, x, y, true);
    } else { /* singularity collapse */
        duel_fb_px(fb, x, y, true);
        duel_fb_px(fb, x - 2, y - 2, true); duel_fb_px(fb, x + 2, y + 2, true);
    }
}

// Progressive upper-canvas anticipation. Growth comes from authoritative
// wind-up/tier state; only the tiny orbiting accents key off the render frame.
static void draw_charge(duel_fb_t *fb, const duel_view_wizard_t *wz, int facing, uint32_t frame) {
    if (!wz->cast_windup) return;
    int elapsed = SIM_CAST_WINDUP_TICKS - wz->cast_windup;
    int stage   = elapsed * 4 / (SIM_CAST_WINDUP_TICKS - 1); // 0..4
    int tier    = wz->cast_tier & 3;
    int max_r   = 1 + tier;
    int radius  = 1 + stage * (max_r - 1) / 4;
    int cx      = 16 + facing * 2;
    int cy      = 39 + DUEL_ROOF_DY;

    duel_fb_px(fb, cx, cy, true);
    if (stage >= 1) {
        duel_fb_px(fb, cx - radius, cy, true); duel_fb_px(fb, cx + radius, cy, true);
        duel_fb_px(fb, cx, cy - radius, true); duel_fb_px(fb, cx, cy + radius, true);
    }
    if (stage >= 2) {
        int d = radius > 1 ? radius - 1 : 1;
        duel_fb_px(fb, cx - d, cy - d, true); duel_fb_px(fb, cx + d, cy - d, true);
        duel_fb_px(fb, cx - d, cy + d, true); duel_fb_px(fb, cx + d, cy + d, true);
    }
    if (stage >= 3) {
        for (int d = -radius; d <= radius; d++) {
            if ((d & 1) == 0) duel_fb_px(fb, cx + d, cy - radius - 2, true);
        }
    }
    if (stage >= 4) {
        wiz_line(fb, cx - radius - 1, cy + radius + 2, cx + radius + 1, cy + radius + 2);
    }

    // Gathering motes move inward as the release approaches. Their phase is
    // cosmetic, but count and maximum spread are capped by the recipe tier.
    int motes = 2 + tier;
    for (int i = 0; i < motes; i++) {
        int side = ((int)(frame + (uint32_t)i) & 1) ? 1 : -1;
        int dx   = side * (7 - stage - (i & 1));
        int dy   = 10 - stage * 2 + i * 2;
        duel_fb_px(fb, cx + dx, cy + dy, true);
    }
}

static void draw_ward(duel_fb_t *fb, int facing, int strength, int focus,
                      bool punctured, int puncture_y) {
    int ax = 16 + facing * 9;
    int focus_y = focus == 0 ? SPELL_Y_BASE + 10 : focus == 1 ? SPELL_Y_BASE + 5 :
                  focus == 3 ? SPELL_Y_BASE - 7 : SPELL_Y_BASE;
    int reach = 3 + strength * 3;
    int y0 = focus_y - reach, y1 = focus_y + reach;
    if (strength >= 4) { y0 = SPELL_Y_BASE - 18; y1 = SPELL_Y_BASE + 12; }
    for (int t = 0; t < strength; t++) {
        int x = ax + facing * t;
        for (int y = y0 + t; y <= y1 - t; y++) {
            int d = y - puncture_y;
            if (d < 0) d = -d;
            if (punctured && d <= 2) continue;
            duel_fb_px(fb, x, y, true);
        }
        duel_fb_px(fb, x - facing, y0 - 2 + t, true);
        duel_fb_px(fb, x - facing, y0 - 1 + t, true);
        duel_fb_px(fb, x - facing, y1 + 1 - t, true);
        duel_fb_px(fb, x - facing, y1 + 2 - t, true);
    }
    if (punctured) {
        // Split lips and inward cracks make the VOID interaction read as an
        // actual breach rather than a projectile merely overpainting the arc.
        duel_fb_px(fb, ax - facing, puncture_y - 3, true);
        duel_fb_px(fb, ax - 2 * facing, puncture_y - 4, true);
        duel_fb_px(fb, ax - facing, puncture_y + 3, true);
        duel_fb_px(fb, ax - 2 * facing, puncture_y + 4, true);
    }
}

static bool incoming_void_at_ward(const duel_view_t *view, int defender,
                                  duel_view_spell_t *incoming) {
    for (int s = 0; s < 2; s++) {
        duel_view_spell_t spell = duel_view_spell(view, (uint8_t)s);
        if (!spell.active || DUEL_KIND_ELEMENT(spell.kind) != ELEM_VOID) continue;
        if ((defender == SIM_SIDE_R && spell.dir > 0 && spell.pos >= 228) ||
            (defender == SIM_SIDE_L && spell.dir < 0 && spell.pos <= 27)) {
            *incoming = spell;
            return true;
        }
    }
    return false;
}

// A 3x3 open square, one half of the broken-link stale glyph.
static void draw_box3(duel_fb_t *fb, int x, int y) {
    for (int i = 0; i < 3; i++) {
        duel_fb_px(fb, x + i, y, true);
        duel_fb_px(fb, x + i, y + 2, true);
    }
    duel_fb_px(fb, x, y + 1, true);
    duel_fb_px(fb, x + 2, y + 1, true);
}

// Normalized alert glyphs. Each row is five bits wide; category identity
// is deterministic and independent of application/source text.
static const uint8_t alert_glyphs[DUEL_HOST_CATEGORY_COUNT][7] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // none
    {0x1F, 0x10, 0x10, 0x16, 0x10, 0x10, 0x1F}, // terminal prompt
    {0x0E, 0x11, 0x11, 0x15, 0x0E, 0x04, 0x08}, // communication bubble
    {0x04, 0x0E, 0x15, 0x04, 0x15, 0x0E, 0x04}, // transfer arrows
    {0x04, 0x15, 0x0E, 0x1B, 0x0E, 0x15, 0x04}, // system cog
    {0x0A, 0x1F, 0x11, 0x17, 0x15, 0x11, 0x1F}, // calendar
    {0x0E, 0x11, 0x11, 0x1F, 0x0E, 0x04, 0x04}, // security shield
    {0x04, 0x0E, 0x1F, 0x1B, 0x1F, 0x0E, 0x04}, // other diamond
};

static void draw_alert_bitmap(duel_fb_t *fb, uint8_t category,
                              int ox, int oy, bool mirror) {
    if (category >= DUEL_HOST_CATEGORY_COUNT) return;
    for (int y = 0; y < 7; y++) {
        for (int x = 0; x < 5; x++) {
            if (alert_glyphs[category][y] & (1u << (4 - x))) {
                duel_fb_px(fb, mirror ? ox - x : ox + x, oy + y, true);
            }
        }
    }
}

static void clear_alert_corner(duel_fb_t *fb, bool is_left) {
    int x0 = is_left ? 0 : 22;
    int x1 = is_left ? 9 : 31;
    for (int y = 1; y <= 15; y++)
        for (int x = x0; x <= x1; x++) duel_fb_px(fb, x, y, false);
}

static void draw_alert_sigil(duel_fb_t *fb, const duel_render_t *r, bool is_left) {
    uint8_t category = DUEL_HOST_ALERT_CATEGORY(r->alert);
    uint8_t priority = DUEL_HOST_ALERT_PRIORITY(r->alert);
    uint8_t age = DUEL_HOST_ALERT_AGE(r->alert);
    if (!render_host(r) || !render_notif(r) || category == DUEL_HOST_CATEGORY_NONE ||
        priority == DUEL_HOST_PRIORITY_NONE) return;
    clear_alert_corner(fb, is_left);
    // Canonical coordinates describe the left outer corner. The right half is
    // its exact x mirror, producing a single paired desk-space sigil.
    draw_alert_bitmap(fb, category, is_left ? 2 : 29, 4, !is_left);
    if (priority >= DUEL_HOST_PRIORITY_NORMAL) {
        for (int x = 1; x <= 7; x++) {
            duel_fb_px(fb, is_left ? x : 31 - x, 3, true);
            duel_fb_px(fb, is_left ? x : 31 - x, 11, true);
        }
        duel_fb_px(fb, is_left ? 1 : 30, 4, true);
        duel_fb_px(fb, is_left ? 7 : 24, 4, true);
    }
    if (priority == DUEL_HOST_PRIORITY_CRITICAL) {
        for (int y = 3; y <= 11; y++) {
            duel_fb_px(fb, is_left ? 1 : 30, y, true);
            duel_fb_px(fb, is_left ? 7 : 24, y, true);
        }
        duel_fb_px(fb, is_left ? 9 : 22, 2, true);
        duel_fb_px(fb, is_left ? 9 : 22, 12, true);
    }
    int accents = 3 - (age > 5 ? 3 : age / 2);
    for (int i = 0; i < accents; i++)
        duel_fb_px(fb, is_left ? 1 + i * 3 : 30 - i * 3, 1, true);
    int pips = render_notif(r) > 4 ? 4 : render_notif(r);
    for (int i = 0; i < pips; i++)
        duel_fb_px(fb, is_left ? 1 + i * 2 : 30 - i * 2, 13, true);
    if (DUEL_HOST_CONTEXT_PERSISTENT(r->external)) {
        int ax = is_left ? 4 : 27;
        duel_fb_px(fb, ax, 13, true); duel_fb_px(fb, ax, 14, true);
        duel_fb_px(fb, ax, 15, true);
        duel_fb_px(fb, ax + (is_left ? -1 : 1), 15, true);
        duel_fb_px(fb, ax + (is_left ? 1 : -1), 15, true);
    }
}

/* current scry is additive architecture rather than a panel: lens and motes in the
 * sky, layer runes on the away tower edge, link runes at the gap, alert in its
 * established corner, and scene sigils embedded in the ceiling beam. */
static void draw_overlay(duel_fb_t *fb, const duel_render_t *r, bool is_left) {
#define SCRY_X(x) (is_left ? (x) : (DUEL_CANVAS_W - 1 - (x)))
    int facing = is_left ? 1 : -1;

    /* Compact focus lens, mirrored about the physical desk centre. */
    int ex = SCRY_X(15), ey = 8;
    duel_fb_px(fb, ex - 3 * facing, ey, true);
    duel_fb_px(fb, ex + 3 * facing, ey, true);
    duel_fb_px(fb, ex - 2 * facing, ey - 1, true);
    duel_fb_px(fb, ex + 2 * facing, ey - 1, true);
    duel_fb_px(fb, ex - 2 * facing, ey + 1, true);
    duel_fb_px(fb, ex + 2 * facing, ey + 1, true);
    for (int dx = -1; dx <= 1; dx++) {
        duel_fb_px(fb, ex + dx * facing, ey - 2, true);
        duel_fb_px(fb, ex + dx * facing, ey + 2, true);
    }
    duel_fb_px(fb, ex, ey, true);

    /* Notification motes orbit fixed sockets; count never changes geometry. */
    static const int8_t mote_xy[4][2] = {{-5, -3}, {5, -3}, {-5, 3}, {5, 3}};
    int notif = render_notif(r) > 4 ? 4 : render_notif(r);
    for (int i = 0; i < notif; i++)
        duel_fb_px(fb, ex + mote_xy[i][0] * facing, ey + mote_xy[i][1], true);

    /* Four global layer runes climb the away-side wall. */
    uint8_t active = DUEL_RENDER_GLOBAL_LAYER(r->layer);
    for (int i = 0; i < 4; i++) {
        int y0 = 18 + i * 5;
        if (i == active) {
            for (int y = y0; y < y0 + 3; y++)
                for (int x = 1; x <= 3; x++) duel_fb_px(fb, SCRY_X(x), y, true);
        } else {
            duel_fb_px(fb, SCRY_X(1), y0 + 1, true);
            duel_fb_px(fb, SCRY_X(3), y0 + 1, true);
        }
    }

    /* Paired host runes sit on the gap-facing upper edge. */
    int hy = 18;
    for (int y = 0; y < 3; y++) {
        duel_fb_px(fb, SCRY_X(24), hy + y, true);
        duel_fb_px(fb, SCRY_X(26), hy + y, true);
        duel_fb_px(fb, SCRY_X(28), hy + y, true);
        duel_fb_px(fb, SCRY_X(30), hy + y, true);
    }
    if (render_host(r)) {
        for (int x = 26; x <= 28; x++) duel_fb_px(fb, SCRY_X(x), hy + 1, true);
    } else {
        duel_fb_px(fb, SCRY_X(27), hy, true);
        duel_fb_px(fb, SCRY_X(27), hy + 2, true);
    }

    /* The normalized alert keeps its established outer corner. Scry suppresses
     * the ordinary sigil, then adds only these category/priority instruments. */
    uint8_t category = DUEL_HOST_ALERT_CATEGORY(r->alert);
    uint8_t priority = DUEL_HOST_ALERT_PRIORITY(r->alert);
    if (category && priority) {
        draw_alert_bitmap(fb, category, is_left ? 2 : 29, 4, !is_left);
        for (int i = 0; i < priority; i++)
            duel_fb_px(fb, SCRY_X(1 + i * 3), 2, true);
        if (DUEL_HOST_CONTEXT_PERSISTENT(r->external)) {
            duel_fb_px(fb, SCRY_X(4), 13, true);
            duel_fb_px(fb, SCRY_X(4), 14, true);
            duel_fb_px(fb, SCRY_X(3), 15, true);
            duel_fb_px(fb, SCRY_X(5), 15, true);
        }
    }

    /* Selector meaning is stable while every architectural position mirrors. */
    uint8_t scene = render_host(r) ? render_scene(r) :
        (uint8_t)((r->view.outcome_overlay >> 5) & 3u);
    static const uint8_t selector_x[SCRY_SCENES] = {7, 14, 21};
    for (int i = 0; i < SCRY_SCENES; i++) {
        int x0 = selector_x[i];
        if (i == scene % SCRY_SCENES) {
            for (int x = 0; x < 3; x++) {
                duel_fb_px(fb, SCRY_X(x0 + x), 59, true);
                duel_fb_px(fb, SCRY_X(x0 + x), 60, true);
            }
        } else {
            duel_fb_px(fb, SCRY_X(x0), 60, true);
            duel_fb_px(fb, SCRY_X(x0 + 2), 60, true);
        }
    }
#undef SCRY_X
}

static void draw_local_attunement(duel_fb_t *fb, const duel_render_t *r,
                                  const duel_view_wizard_t *wz, bool is_left) {
    uint8_t local = DUEL_RENDER_LOCAL_LAYER(r->layer);
    if ((is_left && local != DUEL_RENDER_LOCAL_LEFT) ||
        (!is_left && local != DUEL_RENDER_LOCAL_RIGHT)) return;
    int facing = is_left ? 1 : -1;
    bool casting = wz->life == LIFE_ACTIVE && wz->pose == POSE_CAST;
    int tip_x = 16 + facing * (casting ? 5 : 6);
    int tip_y = (casting ? 55 : 62) + DUEL_ROOF_DY;
    /* A stable three-pixel arc outside the staff/casting hand. */
    duel_fb_px(fb, tip_x + facing * 2, tip_y - 1, true);
    duel_fb_px(fb, tip_x + facing * 3, tip_y, true);
    duel_fb_px(fb, tip_x + facing * 2, tip_y + 1, true);
    int notches = local == DUEL_RENDER_LOCAL_RIGHT ? 2 : 1;
    for (int i = 0; i < notches; i++)
        duel_fb_px(fb, tip_x + facing * (1 + i), tip_y - 3, true);
    incantation_resident_draw_attunement(fb, is_left, DUEL_CIVIC_FLOOR(r->civic));
}

/* HP pip geometry: twelve 2x1 pips in two columns attached low beside the
 * wizard. Each row fills gapward then outward, so damage clears from the top.
 * Single source for the clear, fill, and lost-pip flash sites. */
static void hp_pip_xy(int i, bool is_left, int *px, int *py) {
    int canonical_x = (i & 1) ? 4 : 7;
    *px = is_left ? canonical_x : DUEL_CANVAS_W - 2 - canonical_x;
    *py = 57 - (i / 2) * 2;
}

static void draw_hp_pips(duel_fb_t *fb, const duel_view_wizard_t *wz, bool is_left) {
    /* The health instrument owns its exact cells even while medics and
     * replacement silhouettes cross the away-side rooftop entrance. */
    int px, py;
    for (int i = 0; i < SIM_MAX_HP; i++) {
        hp_pip_xy(i, is_left, &px, &py);
        duel_fb_px(fb, px, py, false);
        duel_fb_px(fb, px + 1, py, false);
    }
    int hp = wz->hp > SIM_MAX_HP ? SIM_MAX_HP : wz->hp;
    for (int i = 0; i < hp; i++) {
        hp_pip_xy(i, is_left, &px, &py);
        duel_fb_px(fb, px, py, true);
        duel_fb_px(fb, px + 1, py, true);
    }
}

// One-shot local outcome flourishes (impact/fizzle/heal/shatter/deflect).
// All render-frame state: losing it costs only the flourish, never health or
// split convergence.
static void draw_local_fx(duel_fb_t *fb, const duel_render_t *r,
                          const duel_view_wizard_t *wz, int facing, bool is_left) {
    bool is_impact = r->flash_kind == FX_IMPACT_L || r->flash_kind == FX_IMPACT_R;
    bool is_fizzle = r->flash_kind == FX_FIZZLE_L || r->flash_kind == FX_FIZZLE_R;
    int tier       = DUEL_KIND_TIER(r->flash_spell_kind);
    int fy         = spell_lane_y(r->flash_spell_kind);

    if (is_impact) {
        // Force enters from the gap: contact burst, inward shock line,
        // local debris, recoil above, and a flashing marker at the pip that
        // just disappeared. Only the defender's border corners twitch.
        int hx    = 16 + facing * 5;
        int reach = 2 + tier + (r->flash_frames >= 8);
        for (int d = 0; d <= reach; d++) duel_fb_px(fb, hx + facing * d, fy, true);
        for (int d = 1; d <= reach; d++) {
            duel_fb_px(fb, hx, fy - d, true);
            duel_fb_px(fb, hx, fy + d, true);
        }
        wiz_line(fb, hx, fy, hx - facing * (3 + tier), fy - 3 - tier);
        wiz_line(fb, hx, fy, hx - facing * (2 + tier), fy + 4 + tier);
        duel_fb_px(fb, hx - facing * 6, fy - 8 - tier, true);
        duel_fb_px(fb, hx - facing * 4, fy + 9 + tier, true);
        if (tier >= SPELL_TIER_LONG) {
            duel_fb_px(fb, hx + facing * 2, fy - 7, true);
            duel_fb_px(fb, hx + facing * 3, fy + 7, true);
        }
        if (r->flash_frames >= 7) {
            for (int d = 0; d < 4; d++) {
                duel_fb_px(fb, d, 0, true); duel_fb_px(fb, DUEL_CANVAS_W - 1 - d, 0, true);
                duel_fb_px(fb, d, DUEL_CANVAS_H - 1, true);
                duel_fb_px(fb, DUEL_CANVAS_W - 1 - d, DUEL_CANVAS_H - 1, true);
            }
        }
        if (wz->hp < SIM_MAX_HP) {
            int px, py;
            hp_pip_xy(wz->hp, is_left, &px, &py);
            duel_fb_px(fb, px - 1, py - 1, true);
            duel_fb_px(fb, px + 2, py - 1, true);
        }
    } else if (is_fizzle) {
        // Harmless dissipation stays away from the body and contracts from
        // a sparse outer shell into a tiny core. No border and no recoil.
        int fx = 16 + facing * 8;
        if (r->flash_frames >= 5) {
            int radius = 2 + (tier >= SPELL_TIER_LONG);
            duel_fb_px(fb, fx - radius, fy - radius, true);
            duel_fb_px(fb, fx + radius, fy - radius, true);
            duel_fb_px(fb, fx - radius, fy + radius, true);
            duel_fb_px(fb, fx + radius, fy + radius, true);
            duel_fb_px(fb, fx + facing * (radius + 1), fy, true);
        } else {
            duel_fb_px(fb, fx, fy, true);
            if (r->flash_frames >= 3) {
                duel_fb_px(fb, fx - 1, fy, true);
                duel_fb_px(fb, fx + 1, fy, true);
            }
        }
    } else if (r->flash_kind == FX_HEAL_L || r->flash_kind == FX_HEAL_R) {
        int hx = 16 - facing * 5;
        int radius = 2 + (r->flash_frames > 4u);
        duel_fb_px(fb, hx - radius, fy, true); duel_fb_px(fb, hx + radius, fy, true);
        duel_fb_px(fb, hx, fy - radius, true); duel_fb_px(fb, hx, fy + radius, true);
        wiz_line(fb, hx - 1, fy, hx + 1, fy);
        wiz_line(fb, hx, fy - 1, hx, fy + 1);
    } else if (r->flash_kind == FX_WARD_SHATTER_L ||
               r->flash_kind == FX_WARD_SHATTER_R) {
        int ax = 16 + facing * 9;
        for (int i = 0; i < 4; i++) {
            int scatter = 2 + i * 2 + (8 - r->flash_frames) / 2;
            duel_fb_px(fb, ax + facing * scatter, fy - 6 + i * 4, true);
            duel_fb_px(fb, ax - facing * (scatter / 2), fy - 4 + i * 3, true);
        }
        wiz_line(fb, ax, fy - 7, ax - facing * 2, fy - 2);
        wiz_line(fb, ax - facing * 2, fy - 2, ax + facing, fy + 6);
    } else {
        // Redirection: the ward is the dominant thick shape while the
        // carrier breaks into two streaks thrown back toward the gap.
        int ax   = 16 + facing * 9;
        int dist = 2 + (8 - r->flash_frames) / 2;
        draw_ward(fb, facing, 2, 2, false, fy);
        wiz_line(fb, ax + facing, fy, ax + facing * (dist + 2), fy - dist - tier);
        wiz_line(fb, ax + facing, fy, ax + facing * (dist + 1), fy + dist + tier);
        duel_fb_px(fb, ax - facing, fy - 5, true);
        duel_fb_px(fb, ax - facing, fy + 5, true);
        if (tier >= SPELL_TIER_LONG) {
            duel_fb_px(fb, ax + facing * (dist + 3), fy - 2, true);
            duel_fb_px(fb, ax + facing * (dist + 2), fy + 3, true);
        }
    }
}

void wiz_draw_scene(duel_fb_t *fb, const duel_render_t *r, bool is_left, uint32_t frame, bool debug_hud) {
    int side = is_left ? SIM_SIDE_L : SIM_SIDE_R;
    duel_view_wizard_t wizard = duel_view_wizard(&r->view, (uint8_t)side);
    const duel_view_wizard_t *wz = &wizard;
    int                 facing = is_left ? +1 : -1; // toward the gap (see header)
    bool defender_left = r->flash_kind == FX_IMPACT_L || r->flash_kind == FX_DEFLECT_L ||
                         r->flash_kind == FX_FIZZLE_L || r->flash_kind == FX_HEAL_L ||
                         r->flash_kind == FX_WARD_SHATTER_L;
    bool side_outcome = r->flash_kind <= FX_FIZZLE_R ||
                        r->flash_kind == FX_HEAL_L || r->flash_kind == FX_HEAL_R ||
                        r->flash_kind == FX_WARD_SHATTER_L ||
                        r->flash_kind == FX_WARD_SHATTER_R;
    bool local_fx = r->flash_frames && side_outcome && defender_left == is_left;
    bool local_impact   = local_fx && (r->flash_kind == FX_IMPACT_L || r->flash_kind == FX_IMPACT_R);
    duel_view_spell_t piercer;
    bool have_piercer = incoming_void_at_ward(&r->view, side, &piercer);
    bool ward_punctured = have_piercer ||
                          (local_impact && DUEL_KIND_ELEMENT(r->flash_spell_kind) == ELEM_VOID);
    int ward_lane = have_piercer ? spell_lane_y(piercer.kind) : spell_lane_y(r->flash_spell_kind);
    // The raised rooftop owns the upper band (the old archive underlay is
    // retired); the archival occupation lives in the tower floor below, where
    // the courier (Wave 6) and rare event (Wave 7) layer in as well.
    draw_sky(fb, r, is_left);
    draw_floor(fb, r, is_left);
    if (!(r->revision & INCANTATION_AFTERMATH_WIRE) &&
        DUEL_CIVIC_FLOOR(r->civic) != DUEL_CIVIC_FLOOR_SPECIAL) {
        draw_courier(fb, r, is_left);
        draw_rare_event(fb, r, is_left);
    }

    // Lifecycle (M5): each phase has its own tableau, derived purely from
    // (life, life_ticks, variant) so master and slave render identically.
    // Sparks and the shield arc only apply to a standing, active wizard.
    switch (wz->life) {
        case LIFE_ACTIVE:
            // A damaging hit pushes the defender away from the gap and briefly
            // compresses the silhouette. Deflect/fizzle leave it rock steady.
            wiz_body(fb, wz->pose == POSE_CAST, facing, wz->variant,
                     local_impact ? -facing * (r->flash_frames >= 8 ? 2 : 1) : 0,
                     local_impact && r->flash_frames >= 8 ? 1 : 0);

            if (wz->pose == POSE_RECOVER && !local_impact) {
                // Fading sparks above the hat make RECOVER observable on hardware.
                duel_fb_px(fb, 14, 50 + DUEL_ROOF_DY, true);
                duel_fb_px(fb, 18, 51 + DUEL_ROOF_DY, true);
            }

            // Shield: a vertical ward arc on the gap side of this half's wizard.
            if (wz->ward_strength) {
                draw_ward(fb, facing,
                          wz->ward_strength,
                          wz->ward_focus,
                          ward_punctured, ward_lane);
            }
            draw_charge(fb, wz, facing, frame);
            if (wz->inc_state == INC_COLLECTING) {
                int runes = 1 + r->view.phase[side] / 64;
                for (int i = 0; i < runes; i++) {
                    int rx = 12 + i * 3;
                    duel_fb_px(fb, rx, 48 + DUEL_ROOF_DY, true);
                    duel_fb_px(fb, rx + 1, 47 + DUEL_ROOF_DY, true);
                }
            } else if (wz->prepared) {
                int px = 16 + facing * 2, py = 43 + DUEL_ROOF_DY;
                duel_fb_px(fb, px, py - 2, true); duel_fb_px(fb, px, py + 2, true);
                duel_fb_px(fb, px - 2, py, true); duel_fb_px(fb, px + 2, py, true);
            }
            draw_incantation_status(fb, wz, facing, frame);
            break;

        case LIFE_COLLAPSE: {
            // Sink the standing figure for the first two thirds, then flat.
            int elapsed = SIM_COLLAPSE_TICKS - wz->life_ticks;
            if (wz->life_ticks > 4) {
                wiz_body(fb, false, facing, wz->variant, 0, elapsed / 3); // yo 0..2
            } else {
                wiz_downed(fb, facing, wz->variant, 0);
            }
            break;
        }

        case LIFE_DOWNED:
            wiz_downed(fb, facing, wz->variant, 0);
            // "Protected" halo over the body, blinking (render-frame cosmetic).
            if (!((frame >> 2) & 1)) {
                duel_fb_px(fb, 15, 69 + DUEL_ROOF_DY, true);
                duel_fb_px(fb, 16, 69 + DUEL_ROOF_DY, true);
                duel_fb_px(fb, 17, 69 + DUEL_ROOF_DY, true);
            }
            break;

        case LIFE_MEDIC: {
            // Medic drags the body toward the away-from-gap edge (-facing).
            int elapsed = SIM_MEDIC_TICKS - wz->life_ticks;
            int dx      = elapsed * 16 / SIM_MEDIC_TICKS; // 0..16
            wiz_downed(fb, facing, wz->variant, -facing * dx);
            medic_draw(fb, 16 - facing * (12 + dx), facing); // ~7 px past the head
            break;
        }

        case LIFE_REPLACE: {
            // The next roster variant (sim already bumped wz->variant) walks
            // in from the away-from-gap edge with a 1-px render-frame bob.
            int elapsed = SIM_REPLACE_TICKS - wz->life_ticks;
            int xo      = 16 - elapsed * 16 / SIM_REPLACE_TICKS; // 16 -> 0
            wiz_body(fb, false, facing, wz->variant, -facing * xo, (int)((frame >> 1) & 1));
            break;
        }
    }

    // Spells in flight, wherever the battlefield axis lands on this canvas.
    for (int s = 0; s < 2; s++) {
        duel_view_spell_t spell = duel_view_spell(&r->view, (uint8_t)s);
        if (!spell.active) continue;
        duel_view_wizard_t caster = duel_view_wizard(&r->view, (uint8_t)s);
        incantation_draw_spell(fb, &spell, (uint8_t)s, caster.variant, is_left, frame);
    }

    draw_incantation_reaction(fb, r->flash_kind, is_left, r->flash_frames);

    // HP pips for THIS half's wizard.
    draw_hp_pips(fb, wz, is_left);

    // One-shot outcomes use three deliberately different grammars.
    if (local_fx) draw_local_fx(fb, r, wz, facing, is_left);

    // normalized alert sigil sits above all scene/combat artwork, but an open scry
    // replaces it with the normalized in-panel summary.
    if (!duel_view_scry_open(&r->view)) draw_alert_sigil(fb, r, is_left);

    if (!duel_view_scry_open(&r->view)) draw_local_attunement(fb, r, wz, is_left);

    // scry scrying overlay, drawn above the world when the layer-key chord is
    // held. The stale-link and debug glyphs draw AFTER, so a broken link is
    // still legible in its corner even with the panel up.
    if (duel_view_scry_open(&r->view)) {
        draw_overlay(fb, r, is_left);
    }

    if (r->flags & DUEL_RENDER_STALE) {
        // Two separated chain links in the top corner nearest the gap.
        int bx = is_left ? 23 : 2;
        draw_box3(fb, bx, 2);
        draw_box3(fb, bx + 4, 6);
    }

    if (debug_hud) {
        duel_fb_px(fb, r->diag_tick, DUEL_CANVAS_H - 1, true);
        int dots = r->diag_overflow > 4 ? 4 : r->diag_overflow;
        for (int i = 0; i < dots; i++) duel_fb_px(fb, 1 + 2 * i, 0, true);
    }
}
