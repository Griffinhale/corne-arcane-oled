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

void duel_render_from_world(duel_render_t *render, const sim_world_t *world) {
    duel_view_from_world(world, &render->view);
#ifdef ARCANE_M13
    render->shared_pres = m13_aftermath_shared(world);
    render->revision = m13_aftermath_revision(world);
#endif
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
#define wiz_hspan duel_fb_hline

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

#ifndef ARCANE_M12
// M9 hybrid Archive underlay. The x coordinates below are authored in desk
// space with x=31 at the centre gap, then mirrored on the right OLED. All
// marks stay in y=3..44, clear of actors, combat carriers, and health.
// Retired under M12 (the raised rooftop owns this band; archival life moves to
// the tower floor), so its definition compiles only in the M11.5 release path.
static void draw_archive(duel_fb_t *fb, const duel_view_wizard_t *wz, bool is_left, uint32_t frame) {
#define ARCH_X(x) (is_left ? (x) : (DUEL_CANVAS_W - 1 - (x)))
    // A single arch spans the physical gap: apex at each inner edge, falling
    // toward the outer edges. Integer curvature keeps it deterministic.
    for (int x = 0; x < DUEL_CANVAS_W; x++) {
        int d = DUEL_CANVAS_W - 1 - x;
        int y = 3 + d * d / 24; // 3..43
        duel_fb_px(fb, ARCH_X(x), y, true);
        if ((d % 7) == 0 && y < 44) duel_fb_px(fb, ARCH_X(x), y + 1, true);
    }

    // Sparse shelves and small books/rune tablets. Gaps prevent the upper
    // canvas from becoming a solid 1bpp texture on the real OLED.
    for (int x = 2; x <= 12; x++) duel_fb_px(fb, ARCH_X(x), 16, true);
    for (int x = 4; x <= 17; x++) duel_fb_px(fb, ARCH_X(x), 30, true);
    for (int x = 1; x <= 14; x++) duel_fb_px(fb, ARCH_X(x), 44, true);
    archive_rect(fb, ARCH_X(3) < ARCH_X(6) ? ARCH_X(3) : ARCH_X(6), 11,
                     ARCH_X(3) < ARCH_X(6) ? ARCH_X(6) : ARCH_X(3), 15);
    archive_rect(fb, ARCH_X(8) < ARCH_X(10) ? ARCH_X(8) : ARCH_X(10), 12,
                     ARCH_X(8) < ARCH_X(10) ? ARCH_X(10) : ARCH_X(8), 15);
    archive_rect(fb, ARCH_X(5) < ARCH_X(8) ? ARCH_X(5) : ARCH_X(8), 24,
                     ARCH_X(5) < ARCH_X(8) ? ARCH_X(8) : ARCH_X(5), 29);
    wiz_line(fb, ARCH_X(11), 25, ARCH_X(14), 29);

    // A single slow page/rune variation keeps the Archive alive without
    // turning the shelves into visual noise. It is render-frame-only and is
    // mirrored in desk space so the physical gap composition stays coherent.
    int slow = (int)((frame >> 4) & 3u);
    duel_fb_px(fb, ARCH_X(15 + slow), 38, true);
    duel_fb_px(fb, ARCH_X(17 - slow), 40, true);
    if (slow & 1) duel_fb_px(fb, ARCH_X(16), 39, true);

    // Shield state is raised by every keydown and lasts ten 40 ms ticks. It
    // drives a bounded expanding activity rune without adding state. During a
    // cast the synchronized recipe tier adds arms/rings, but never mechanics.
    int active = wz->shield_ticks || wz->cast_windup;
    int radius = 1;
    if (active) {
        int elapsed = wz->shield_ticks ? SIM_SHIELD_TICKS - wz->shield_ticks
                                       : SIM_CAST_WINDUP_TICKS - wz->cast_windup;
        if (elapsed < 0) elapsed = 0;
        radius = 2 + elapsed / 3; // immediate light, expands to a bounded 5 px
        if (radius > 5) radius = 5;
    }
    int tier = wz->cast_windup ? (wz->cast_tier & 3) : 0;
    int cx = ARCH_X(21), cy = 24;
    duel_fb_px(fb, cx, cy, true);
    duel_fb_px(fb, cx - radius, cy, true); duel_fb_px(fb, cx + radius, cy, true);
    duel_fb_px(fb, cx, cy - radius, true); duel_fb_px(fb, cx, cy + radius, true);
    if (active) {
        duel_fb_px(fb, cx - radius + 1, cy - radius + 1, true);
        duel_fb_px(fb, cx + radius - 1, cy - radius + 1, true);
        duel_fb_px(fb, cx - radius + 1, cy + radius - 1, true);
        duel_fb_px(fb, cx + radius - 1, cy + radius - 1, true);
    }
    if (tier >= SPELL_TIER_MEDIUM) {
        wiz_line(fb, cx - radius, cy - radius, cx + radius, cy + radius);
    }
    if (tier >= SPELL_TIER_LONG) {
        wiz_line(fb, cx - radius, cy + radius, cx + radius, cy - radius);
    }
    if (tier == SPELL_TIER_SATURATED) {
        archive_rect(fb, cx - radius - 1, cy - radius - 1,
                     cx + radius + 1, cy + radius + 1);
    }
#undef ARCH_X
}

static void clear_archive_panel(duel_fb_t *fb) {
    for (int y = 2; y <= 42; y++)
        for (int x = 2; x <= 29; x++) duel_fb_px(fb, x, y, false);
}
#endif // !ARCANE_M12

#ifdef ARCANE_M12
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
#ifndef ARCANE_M13
static void floor_window(duel_fb_t *fb, int x0, int x1, int y0, int y1, bool is_left) {
    int cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    archive_rect(fb, x0, y0, x1, y1);
    for (int y = y0 + 1; y < y1; y++) duel_fb_px(fb, cx, y, true);   // centre mullion
    if (is_left) {
        floor_dome(fb, x0, x1, y0 - 3);                             // astral arched crown
    } else {
        wiz_hspan(fb, x0 + 1, x1 - 1, cy);                          // mechanical transom -> 4 panes
    }
}

// Something hanging from the ceiling beam into the centre void: fills the dead
// air between the beam and the ground-level furniture and gives the room a
// vertical anchor. Astral hangs a glowing lantern; mechanical hangs a hoist
// block on a chain. cx is authored in desk space by the caller.
static void floor_hanging(duel_fb_t *fb, int cx, bool is_left) {
    for (int y = 62; y <= 69; y++) duel_fb_px(fb, cx, y, true);      // short chain from the beam
    if (is_left) {
        floor_dome(fb, cx - 2, cx + 2, 70);                         // lantern cap
        archive_rect(fb, cx - 2, 72, cx + 2, 79);                   // lantern body
        duel_fb_px(fb, cx, 75, true); duel_fb_px(fb, cx, 76, true); // flame/core
    } else {
        archive_rect(fb, cx - 2, 71, cx + 2, 78);                   // hoist block
        wiz_hspan(fb, cx - 1, cx + 1, 74);                          // block strap
        duel_fb_px(fb, cx, 79, true);                               // hook shank
        duel_fb_px(fb, cx - 1, 80, true);                           // hook curl
    }
}

// Occupation furniture for the floor: two bold ground-level masses that match
// the window's weight and frame the resident working among them — a tall gap-side
// cabinet (balances the outer-wall window) and an open counter beneath the
// window. The floor archetype only *accents* these; the brick-wipe transition
// (not the furniture) is what sells a floor change, so all three archetypes
// share this silhouette rather than each cramming a distinct, tiny one. Tops sit
// at waist height with open fronts so a resident at a station reads as standing
// AT the piece, not boxed inside it. Authored in desk space (gap at x=31) and
// mirrored on the right.
static void draw_floor_anchors(duel_fb_t *fb, uint8_t floor, bool is_left) {
#define FLR_X(x) (is_left ? (x) : (DUEL_CANVAS_W - 1 - (x)))
    int ga = FLR_X(23), gb = FLR_X(28); int glo = ga < gb ? ga : gb, ghi = ga < gb ? gb : ga;
    int oa = FLR_X(2),  ob = FLR_X(9);  int olo = oa < ob ? oa : ob, ohi = oa < ob ? ob : oa;
    int gmid = (glo + ghi) / 2, omid = (olo + ohi) / 2;

    // Gap-side cabinet: a tall, solid-outlined mass with an interior shelf and a
    // city-styled crown.
    archive_rect(fb, glo, 88, ghi, 104);
    wiz_hspan(fb, glo, ghi, 96);                                    // interior shelf
    if (is_left) {
        floor_dome(fb, glo, ghi, 86);                              // astral arched crown
        duel_fb_px(fb, gmid, 84, true);                            // finial
    } else {
        wiz_hspan(fb, glo, ghi, 86);                               // mechanical lintel
        floor_gear(fb, gmid, 84, 1);                               // drive gear atop
    }

    // Outer counter beneath the window: an open-front bench (solid top + legs).
    wiz_hspan(fb, olo, ohi, 99);                                   // counter top
    wiz_hspan(fb, olo, ohi, 100);
    for (int y = 101; y <= 104; y++) { duel_fb_px(fb, olo, y, true); duel_fb_px(fb, ohi, y, true); }

    // Archetype accent — small; not required to read as a distinct floor.
    switch (floor) {
    case DUEL_M12_FLOOR_RESEARCH:
        wiz_hspan(fb, glo, ghi, 92);                               // extra catalog shelf
        duel_fb_px(fb, omid, 97, true); duel_fb_px(fb, omid, 98, true); // upright on the counter
        break;
    case DUEL_M12_FLOOR_WORKSHOP:
        if (is_left) { duel_fb_px(fb, gmid, 82, true); duel_fb_px(fb, gmid - 1, 80, true); } // forge flame
        else floor_gear(fb, gmid, 92, 2);                         // drive gear in the cabinet
        duel_fb_px(fb, omid, 97, true); duel_fb_px(fb, omid + 1, 97, true);  // tool on the bench
        break;
    default: break; // Commons: plain
    }
#undef FLR_X
}
#endif

#ifdef ARCANE_M13
/* Occupation-first M13 furniture. Each floor owns two large silhouettes and
 * uses the same work/inspect/rest anchors as the resident engine: x~6 for the
 * dominant work object, x~21 for its supporting station, and x~4 for rest. */
static void draw_floor_occupation(duel_fb_t *fb, uint8_t floor, bool is_left) {
#define OX(x) (is_left ? (x) : (DUEL_CANVAS_W - 1 - (x)))
#define ORECT(x0, y0, x1, y1) do { \
    int a_ = OX(x0), b_ = OX(x1); \
    archive_rect(fb, a_ < b_ ? a_ : b_, (y0), a_ < b_ ? b_ : a_, (y1)); \
} while (0)
    if (floor == DUEL_M12_FLOOR_COMMONS) {
        /* Communal table / dispatch desk: the broadest horizontal mass. */
        wiz_hspan(fb, OX(3) < OX(14) ? OX(3) : OX(14),
                  OX(3) < OX(14) ? OX(14) : OX(3), 96);
        wiz_hspan(fb, OX(3) < OX(14) ? OX(3) : OX(14),
                  OX(3) < OX(14) ? OX(14) : OX(3), 97);
        for (int y = 98; y <= 105; y++) {
            duel_fb_px(fb, OX(5), y, true); duel_fb_px(fb, OX(13), y, true);
        }
        /* Notice/mail board, deliberately tall and gap-side. */
        ORECT(24, 69, 30, 91);
        if (is_left) {
            floor_dome(fb, OX(24), OX(30), 66); /* arched notice board */
            for (int y = 75; y <= 87; y += 6) {
                wiz_hspan(fb, OX(25), OX(29), y);
                duel_fb_px(fb, OX(24), y - 2, true);
            }
            floor_dome(fb, OX(8), OX(14), 88); /* tea-orb stand */
            for (int y = 91; y <= 95; y++) duel_fb_px(fb, OX(11), y, true);
            duel_fb_px(fb, OX(9), 94, true); duel_fb_px(fb, OX(13), 93, true);
        } else {
            /* Dispatch cubbies and clock. */
            for (int y = 75; y <= 87; y += 6) wiz_hspan(fb, OX(25), OX(29), y);
            for (int x = 26; x <= 29; x += 3)
                for (int y = 70; y <= 90; y++) duel_fb_px(fb, OX(x), y, true);
            floor_gear(fb, OX(11), 90, 3);
            duel_fb_px(fb, OX(11), 87, true); duel_fb_px(fb, OX(13), 90, true);
            for (int x = 7; x <= 15; x += 4)
                for (int y = 99; y <= 103; y++) duel_fb_px(fb, OX(x), y, true);
        }
    } else if (floor == DUEL_M12_FLOOR_RESEARCH) {
        /* Dominant telescope/analyzer, an unmistakable rising diagonal. */
        wiz_line(fb, OX(4), 98, OX(15), 76);
        wiz_line(fb, OX(5), 100, OX(16), 78);
        ORECT(11, 73, 17, 81);
        for (int y = 99; y <= 105; y++) duel_fb_px(fb, OX(7), y, true);
        wiz_line(fb, OX(7), 99, OX(3), 105);
        wiz_line(fb, OX(7), 99, OX(12), 105);
        /* Specimen cabinet/cylinder supporting the instrument. */
        ORECT(24, 76, 30, 104);
        wiz_hspan(fb, OX(24) < OX(30) ? OX(24) : OX(30),
                  OX(24) < OX(30) ? OX(30) : OX(24), 90);
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
            wiz_hspan(fb, OX(25) < OX(29) ? OX(25) : OX(29),
                      OX(25) < OX(29) ? OX(29) : OX(25), 99);
            duel_fb_px(fb, OX(27), 94, true); duel_fb_px(fb, OX(26), 96, true);
        }
    } else if (floor == DUEL_M12_FLOOR_WORKSHOP) {
        /* SPECIAL remains reserved and intentionally has no occupation art. */
        /* Dominant forge: cauldron on astral, anvil/gear press on mechanical. */
        if (is_left) {
            floor_dome(fb, OX(3), OX(14), 90);
            wiz_hspan(fb, OX(4), OX(13), 96);
            for (int y = 97; y <= 103; y++) {
                duel_fb_px(fb, OX(5), y, true); duel_fb_px(fb, OX(13), y, true);
            }
            for (int x = 7; x <= 13; x += 2)
                duel_fb_px(fb, OX(x), 87 - ((x + 1) & 3), true);
        } else {
            wiz_hspan(fb, OX(3) < OX(14) ? OX(3) : OX(14),
                      OX(3) < OX(14) ? OX(14) : OX(3), 94);
            wiz_hspan(fb, OX(6) < OX(13) ? OX(6) : OX(13),
                      OX(6) < OX(13) ? OX(13) : OX(6), 95);
            wiz_line(fb, OX(9), 96, OX(7), 105);
            wiz_line(fb, OX(13), 96, OX(15), 105);
            floor_gear(fb, OX(11), 83, 4);
            for (int y = 68; y <= 79; y++) duel_fb_px(fb, OX(11), y, true);
            ORECT(7, 65, 15, 69); /* gear press crosshead */
        }
        /* Tool/reagent station and hoist/rack occupy the gap-side column. */
        ORECT(24, 78, 30, 104);
        for (int y = 84; y <= 100; y += 8)
            wiz_hspan(fb, OX(25) < OX(29) ? OX(25) : OX(29),
                      OX(25) < OX(29) ? OX(29) : OX(25), y);
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
    }
#undef ORECT
#undef OX
}

static void draw_floor_transition(duel_fb_t *fb, const duel_render_t *r,
                                  bool is_left) {
    if (!M13_FLOOR_TRANSITION_ACTIVE(r->floor_transition)) return;
    uint8_t phase = M13_FLOOR_TRANSITION_PHASE(r->floor_transition);
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
            wiz_hspan(fb, 0, 31, y);
            int offset = ((y / 5) & 1) ? 3 : 0;
            for (int x = offset; x < 32; x += 7)
                for (int dy = 1; dy < 5 && y + dy <= 110; dy++)
                    duel_fb_px(fb, x, y + dy, true);
        }
        for (int y = 62; y <= 110; y++) duel_fb_px(fb, inner, y, true);
    } else if (phase == 2u) { /* target-room reveal */
        for (int y = 62; y <= 82; y++)
            for (int x = 0; x < 32; x++) duel_fb_px(fb, x, y, false);
        for (int y = 62; y <= 82; y += 5) wiz_hspan(fb, 0, 31, y);
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
#endif

// M12 tower floor beneath the raised rooftop. A schematic cutaway room whose
// OCCUPATION is chosen by the civic byte (DUEL_CIVIC_FLOOR): Commons/post,
// Archive/Research, or Workshop/Forge. The two cities render the same room in
// clearly different architectural languages — left is astral/curved (dashed
// beams, domes, orbs, buttresses); right is mechanical/squared (solid beams,
// rivets, gears, tie-bars). One session-seeded resident lives in the floor,
// derived and drawn locally (duel_resident.c). Authored in desk space (gap at
// x=31) and mirrored on the right OLED like the retired draw_archive.
static void draw_floor(duel_fb_t *fb, const duel_render_t *r, bool is_left) {
#define FLR_X(x) (is_left ? (x) : (DUEL_CANVAS_W - 1 - (x)))
    uint8_t floor = DUEL_CIVIC_FLOOR(r->civic);
    uint8_t mode  = DUEL_CIVIC_MODE(r->civic);
#ifdef ARCANE_M13
    if (M13_FLOOR_TRANSITION_ACTIVE(r->floor_transition) &&
        M13_FLOOR_TRANSITION_PHASE(r->floor_transition) < 2u)
        floor = M13_FLOOR_TRANSITION_SOURCE(r->floor_transition);
#endif
    // The civic byte is authoritative for the occupation. Until the glue layer
    // (keymap) translates scene->civic in a later wave, bridge the legacy
    // scene channel: an online Archive scene with a default (Commons) civic byte
    // shows the Research floor, honouring the scene-driven world-test contract.
#ifndef ARCANE_M13
    if (floor == DUEL_M12_FLOOR_COMMONS && render_host(r) &&
        render_scene(r) == DUEL_HOST_SCENE_ARCHIVE)
        floor = DUEL_M12_FLOOR_RESEARCH;
#endif

    // Solid ceiling beam splitting the rooftop from the floor (both cities).
    // City character lives in the details below the beam, not the beam itself.
    for (int x = 0; x < DUEL_CANVAS_W; x++)
        duel_fb_px(fb, x, 61, true);
#ifdef ARCANE_M13
    if (r->revision & M13_AFTERMATH_WIRE) {
        uint8_t world = M13_AFTER_WORLD(r->shared_pres);
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
#endif
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
#ifndef ARCANE_M13
    int wa = FLR_X(2), wb = FLR_X(10);
    int wlo = wa < wb ? wa : wb, whi = wa < wb ? wb : wa;
    floor_window(fb, wlo, whi, 66, 84, is_left);
    // Off the courier lifecycle columns (duel_courier life_ax = {24,17,11,26})
    // so a hanging fixture never swallows a courier's density tell, and clear of
    // the gap-side cabinet so the two don't stack up.
    floor_hanging(fb, FLR_X(15), is_left);
#endif

    // Ground line of the room.
    wiz_hspan(fb, 0, DUEL_CANVAS_W - 1, 110);

    // Occupation furniture, then the session-seeded resident living among it.
#ifdef ARCANE_M13
    draw_floor_occupation(fb, floor, is_left);
#else
    draw_floor_anchors(fb, floor, is_left);
#endif
    m12_resident_t res = m12_resident_derive(r->seed, is_left, floor, mode, r->civic_phase);
#ifdef ARCANE_M13
    uint8_t after_kind = AFTER_NONE, after_phase = 0;
    if (r->revision & M13_AFTERMATH_WIRE) {
        uint8_t side = is_left ? SIM_SIDE_L : SIM_SIDE_R;
        after_kind = M13_AFTER_KIND(r->shared_pres, side);
        after_phase = M13_AFTER_PHASE(r->revision, side);
        res.progress = (uint8_t)(after_phase * 4u + (r->civic_phase & 3u));
        switch (after_kind) {
            case AFTER_CHEER:
                res.task = RESIDENT_CHEER; res.action = DUEL_M12_ACTION_REACT;
                res.station = M13_OCCUPATION_KEY(floor, res.action); break;
            case AFTER_COMPLAINT:
                res.task = RESIDENT_COMPLAIN; res.action = DUEL_M12_ACTION_REACT;
                res.station = M13_OCCUPATION_KEY(floor, res.action); break;
            case AFTER_PANIC:
                res.task = RESIDENT_PANIC; res.action =
                    (after_phase & 1u) ? DUEL_M12_ACTION_WALK : DUEL_M12_ACTION_REACT;
                res.station = M13_OCCUPATION_KEY(floor, res.action); break;
            case AFTER_FIRE:
                if (after_phase == 0u) {
                    res.task = RESIDENT_PANIC; res.action = DUEL_M12_ACTION_REACT;
                } else if (after_phase < 3u) {
                    res.task = RESIDENT_FIGHT_FIRE; res.action = DUEL_M12_ACTION_WORK;
                } else {
                    res.task = RESIDENT_REPAIR; res.action = DUEL_M12_ACTION_WORK;
                }
                res.station = M13_OCCUPATION_KEY(floor, res.action);
                break;
            case AFTER_INSPECT:
                res.task = RESIDENT_INSPECT; res.action = DUEL_M12_ACTION_INSPECT;
                res.station = M13_OCCUPATION_KEY(floor, res.action); break;
            case AFTER_REPAIR:
                res.task = RESIDENT_REPAIR; res.action = DUEL_M12_ACTION_WORK;
                res.station = M13_OCCUPATION_KEY(floor, res.action); break;
            case AFTER_MAX_CAST:
                res.task = after_phase < 2u ? RESIDENT_WATCH_CAST : RESIDENT_CHEER;
                res.action = after_phase < 2u ? DUEL_M12_ACTION_WATCH_ROOF : DUEL_M12_ACTION_REACT;
                res.station = M13_OCCUPATION_KEY(floor, res.action);
                break;
            default:
                break;
        }
    }
#endif
    m12_resident_draw(fb, &res, is_left, mode, 0);

#ifdef ARCANE_M13
    /* Lasting room/object consequences. They share the authoritative aftermath
     * phase with the resident task, so reconnecting halves resume mid-arc. All
     * marks use the same floor/action descriptor as the assigned civic task. */
    uint8_t mark_action = after_kind == AFTER_INSPECT ? DUEL_M12_ACTION_INSPECT :
                          after_kind == AFTER_COMPLAINT ? DUEL_M12_ACTION_INSPECT :
                          after_kind == AFTER_PANIC ? DUEL_M12_ACTION_REACT :
                          after_kind == AFTER_MAX_CAST ? DUEL_M12_ACTION_WATCH_ROOF :
                          after_kind == AFTER_CHEER ? DUEL_M12_ACTION_REACT :
                          DUEL_M12_ACTION_WORK;
    m13_point_t mark = m13_occupation_anchor(floor, mark_action);
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
            m13_civic_hline(fb, is_left, mark.x - 3, mark.x + 3, my + 3);
            duel_fb_px(fb, mx, my, true);
        }
    } else if (after_kind == AFTER_MAX_CAST) {
        int motes = 6 - after_phase;
        for (int i = 0; i < motes; i++)
            duel_fb_px(fb, FLR_X(mark.x - 10 + i * 4), my - 15 +
                       (int)((r->civic_phase + i * 3u) % 12u), true);
        if (after_phase == 2u)
            m13_civic_hline(fb, is_left, mark.x - 8, mark.x + 8, my + 8);
    } else if (after_kind == AFTER_COMPLAINT) {
        duel_fb_px(fb, mx, my, true);
        m13_civic_hline(fb, is_left, mark.x - 4, mark.x, my - 1);
    } else if (after_kind == AFTER_CHEER) {
        duel_fb_px(fb, FLR_X(mark.x - 2), my - 2 - after_phase, true);
        duel_fb_px(fb, mx, my - 4 + after_phase, true);
    }
#endif

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
    wiz_hspan(fb, 0, DUEL_CANVAS_W - 1, 117);                       // capstone (both cities)
    if (is_left) {
        for (int x = 2; x < DUEL_CANVAS_W; x += 8)                 // sparse ashlar joints
            for (int y = 119; y <= 122; y++) duel_fb_px(fb, x, y, true);
    } else {
        for (int x = 0; x < DUEL_CANVAS_W; x += 4)                 // dense brick joints, offset course
            for (int y = 119; y <= 122; y++) duel_fb_px(fb, x + ((y >> 1) & 1) * 2, y, true);
    }
#ifdef ARCANE_M13
    draw_floor_transition(fb, r, is_left);
#endif
#undef FLR_X
}
#endif

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
        wiz_hspan(fb, cx - hw, cx + hw, y);
    }
    // Hat brim.
    wiz_hspan(fb, cx - 4, cx + 4, hat_base_y + 1);

    // Robe: trapezoid widening toward the base (half-width 2..4).
    const int robe_top = hat_base_y + 2;   // 63 + yo
    const int robe_bot = 75 + yo + DUEL_ROOF_DY;
    for (int y = robe_top; y <= robe_bot; y++) {
        int hw = 2 + (y - robe_top) * 2 / (robe_bot - robe_top); // 2..4
        wiz_hspan(fb, cx - hw, cx + hw, y);
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
        // Casting: staff raised toward the hat. The progressive M7.5 charge is
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
    wiz_hspan(fb, lo, hi, 72 + DUEL_ROOF_DY);
    wiz_hspan(fb, lo, hi, 73 + DUEL_ROOF_DY);
    // Fallen hat just past the head: 3-px base with a 1-px apex row on top.
    int hx = head - facing * 2;              // hat centre, one px clear of the head
    wiz_hspan(fb, hx < head ? hx - 1 : head + 1, hx < head ? head - 1 : hx + 1, 72 + DUEL_ROOF_DY);
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

bool duel_battlefield_to_x(uint8_t u, bool is_left, int *x) {
    if (is_left) {
        if (u > 95) return false;
        *x = 22 + u / 10; // staff tip (22) -> gap edge (31)
        return true;
    }
    if (u < 160) return false;
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

#ifdef ARCANE_M13
static int m13_trajectory_y(uint32_t desc, uint8_t flight) {
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

static bool m13_in_gap(uint8_t u) { return u >= 96u && u <= 159u; }

static void m13_draw_inner_flare(duel_fb_t *fb, uint32_t desc, bool is_left,
                                 int y, uint8_t phase, uint8_t reach) {
    int edge = is_left ? 31 : 0;
    int inward = is_left ? -1 : 1;
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
static bool m13_draw_gap_cue(duel_fb_t *fb, uint32_t desc, uint8_t form,
                             uint8_t progress, uint8_t flight, uint8_t u,
                             uint8_t caster_side, bool is_left, int y) {
    bool reflected_singularity = form == SPELL_SINGULARITY &&
                                 progress >= 160u && progress <= 207u;
    if (!m13_in_gap(u) && !reflected_singularity) return false;
    uint8_t phase = (uint8_t)((progress + SPELL_DESC_VARIANCE(desc)) & 3u);
    bool caster_local = is_left == (caster_side == SIM_SIDE_L);
    bool portal = SPELL_DESC_INTERACTION(desc) == INTERACT_PHASE ||
                  SPELL_DESC_ELEMENT(desc) == ELEM_VOID ||
                  SPELL_DESC_TRAJECTORY(desc) == TRAJ_RETURNING ||
                  form == SPELL_CONJURE || reflected_singularity;
    bool trail = form == SPELL_SWARM ||
                 SPELL_DESC_TRAJECTORY(desc) == TRAJ_HOMING ||
                 SPELL_DESC_TRAJECTORY(desc) == TRAJ_AREA;
    int edge = is_left ? 31 : 0;
    int inward = is_left ? -1 : 1;
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
        uint8_t local = departure ? (uint8_t)(flight - 96u) :
                                    (uint8_t)(flight - 128u);
        uint8_t reach = departure ? (uint8_t)(3u - local / 11u) :
                                    (uint8_t)(1u + local / 11u);
        if (reach < 1u) reach = 1u;
        if (reach > 3u) reach = 3u;
        m13_draw_inner_flare(fb, desc, is_left, y, phase, reach);
        duel_fb_px(fb, edge + inward * 4, y + (departure ? -2 : 2), true);
    }
    return true;
}

void m13_draw_spell(duel_fb_t *fb, const duel_view_spell_t *spell,
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
    int y = m13_trajectory_y(spell->descriptor, flight) + bob;

    if (form == SPELL_BEAM) {
        int yb = m13_trajectory_y(spell->descriptor, 192u);
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
        if (progress >= 96u && progress <= 159u)
            m13_draw_inner_flare(fb, spell->descriptor, is_left, yb,
                                 (uint8_t)(progress & 3u),
                                 (uint8_t)(2u + (progress - 96u) / 21u));
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
        if (progress >= 96u && progress <= 159u)
            m13_draw_inner_flare(fb, spell->descriptor, is_left, y,
                                 (uint8_t)(progress & 3u),
                                 (uint8_t)(2u + (progress - 96u) / 21u));
        return;
    }

    if (m13_draw_gap_cue(fb, spell->descriptor, form, progress, flight, u,
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

static void draw_m13_status(duel_fb_t *fb, const duel_view_wizard_t *wz,
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

static void draw_m13_reaction(duel_fb_t *fb, uint8_t outcome, bool is_left,
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
#endif

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
#ifdef ARCANE_M13
    int focus_y = focus == 0 ? SPELL_Y_BASE + 10 : focus == 1 ? SPELL_Y_BASE + 5 :
                  focus == 3 ? SPELL_Y_BASE - 7 : SPELL_Y_BASE;
    int reach = 3 + strength * 3;
    int y0 = focus_y - reach, y1 = focus_y + reach;
    if (strength >= 4) { y0 = SPELL_Y_BASE - 18; y1 = SPELL_Y_BASE + 12; }
#else
    (void)focus;
    int y0 = 58 + DUEL_ROOF_DY, y1 = 72 + DUEL_ROOF_DY;
#endif
    for (int t = 0; t < strength; t++) {
        int x = ax + facing * t;
#ifdef ARCANE_M13
        for (int y = y0 + t; y <= y1 - t; y++) {
#else
        for (int y = y0; y <= y1; y++) {
#endif
            int d = y - puncture_y;
            if (d < 0) d = -d;
            if (punctured && d <= 2) continue;
            duel_fb_px(fb, x, y, true);
        }
#ifdef ARCANE_M13
        duel_fb_px(fb, x - facing, y0 - 2 + t, true);
        duel_fb_px(fb, x - facing, y0 - 1 + t, true);
        duel_fb_px(fb, x - facing, y1 + 1 - t, true);
        duel_fb_px(fb, x - facing, y1 + 2 - t, true);
#else
        duel_fb_px(fb, x - facing, y0 - 2, true);
        duel_fb_px(fb, x - facing, y0 - 1, true);
        duel_fb_px(fb, x - facing, y1 + 1, true);
        duel_fb_px(fb, x - facing, y1 + 2, true);
#endif
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

// M10 normalized alert glyphs. Each row is five bits wide; category identity
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

#ifndef ARCANE_M13
// Outlined (or filled) rectangle helper for the frozen M7 overlay panel.
static void ov_rect(duel_fb_t *fb, int x0, int y0, int x1, int y1, bool fill) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (fill || x == x0 || x == x1 || y == y0 || y == y1) duel_fb_px(fb, x, y, true);
}

// M7 scrying overlay: a concise bordered panel drawn ON TOP of the still-
// running duel while the layer-key chord is held. Content is presentation-only
// (layer / host / notification / scene selector); whether it draws is decided
// by the world (scry_is_open), so master and slave show it in lockstep. The
// same compact panel is drawn on both 32-px canvases for at-a-glance reading.
static void draw_overlay(duel_fb_t *fb, const duel_render_t *r, bool is_left) {
    (void)is_left;
    ov_rect(fb, 3, 3, 28, 41, false); // panel border

    // Title: a scrying "eye" — lens outline with a pupil.
    const int ex = 16, ey = 8;
    duel_fb_px(fb, ex - 3, ey, true);     duel_fb_px(fb, ex + 3, ey, true);
    duel_fb_px(fb, ex - 2, ey - 1, true); duel_fb_px(fb, ex + 2, ey - 1, true);
    duel_fb_px(fb, ex - 2, ey + 1, true); duel_fb_px(fb, ex + 2, ey + 1, true);
    for (int dx = -1; dx <= 1; dx++) { duel_fb_px(fb, ex + dx, ey - 2, true); duel_fb_px(fb, ex + dx, ey + 2, true); }
    duel_fb_px(fb, ex, ey, true);        // pupil
    for (int x = 5; x <= 26; x++) duel_fb_px(fb, x, 12, true); // separator

    // Layer readout: four slots, the active layer filled solid, the rest hollow.
    for (int i = 0; i < 4; i++) {
        int lx = 6 + i * 5;
        ov_rect(fb, lx, 15, lx + 3, 18, i == (r->layer & 3));
    }

    // Host link: solid bar when online, two disconnected boxes when offline.
    if (render_host(r)) {
        ov_rect(fb, 6, 23, 13, 27, true);
    } else {
        ov_rect(fb, 6, 23, 8, 27, false);
        ov_rect(fb, 11, 23, 13, 27, false);
    }
    // Notification count: up to four 2x2 dots on the right of the host row.
    int notif = render_notif(r) > 4 ? 4 : render_notif(r);
    for (int i = 0; i < notif; i++) {
        int nx = 17 + i * 3;
        duel_fb_px(fb, nx, 24, true);     duel_fb_px(fb, nx + 1, 24, true);
        duel_fb_px(fb, nx, 25, true);     duel_fb_px(fb, nx + 1, 25, true);
    }

    // V2 normalized summary: category glyph plus priority marks and critical
    // persistence anchor. V1/count-only traffic deliberately leaves this area
    // empty while retaining the legacy count pips above.
    uint8_t category = DUEL_HOST_ALERT_CATEGORY(r->alert);
    uint8_t priority = DUEL_HOST_ALERT_PRIORITY(r->alert);
    if (category && priority) {
        draw_alert_bitmap(fb, category, 18, 27, false);
        for (int i = 0; i < priority; i++)
            duel_fb_px(fb, 24 + i, 28, true);
        if (DUEL_HOST_CONTEXT_PERSISTENT(r->external)) {
            duel_fb_px(fb, 26, 30, true); duel_fb_px(fb, 26, 31, true);
            duel_fb_px(fb, 25, 31, true); duel_fb_px(fb, 27, 31, true);
        }
    }

    // Scene selector: host context owns the readout while online; heartbeat
    // expiry immediately returns it to the firmware-local scry selection.
#ifdef ARCANE_M13
    uint8_t scene = render_host(r) ? render_scene(r) : (uint8_t)((r->view.outcome_overlay >> 5) & 3u);
#else
    uint8_t scene = render_host(r) ? render_scene(r) : DUEL_SCRY_SCENE(r->view.scry);
#endif
    for (int i = 0; i < SCRY_SCENES; i++) {
        int sx = 6 + i * 7;
        ov_rect(fb, sx, 35, sx + 3, 38, i == (scene % SCRY_SCENES));
    }
}
#else
/* M13 scry is additive architecture rather than a panel: lens and motes in the
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
#endif

#ifdef ARCANE_M13
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
    m13_resident_draw_attunement(fb, is_left, DUEL_CIVIC_FLOOR(r->civic));
}
#endif

void wiz_draw_scene(duel_fb_t *fb, const duel_render_t *r, bool is_left, uint32_t frame, bool debug_hud) {
    int side = is_left ? SIM_SIDE_L : SIM_SIDE_R;
    duel_view_wizard_t wizard = duel_view_wizard(&r->view, (uint8_t)side);
    const duel_view_wizard_t *wz = &wizard;
    int                 facing = is_left ? +1 : -1; // toward the gap (see header)
#ifdef ARCANE_M13
    bool defender_left = r->flash_kind == FX_IMPACT_L || r->flash_kind == FX_DEFLECT_L ||
                         r->flash_kind == FX_FIZZLE_L || r->flash_kind == FX_HEAL_L ||
                         r->flash_kind == FX_WARD_SHATTER_L;
    bool side_outcome = r->flash_kind <= FX_FIZZLE_R ||
                        r->flash_kind == FX_HEAL_L || r->flash_kind == FX_HEAL_R ||
                        r->flash_kind == FX_WARD_SHATTER_L ||
                        r->flash_kind == FX_WARD_SHATTER_R;
    bool local_fx = r->flash_frames && side_outcome && defender_left == is_left;
#else
    bool defender_left = r->flash_kind == FX_IMPACT_L || r->flash_kind == FX_DEFLECT_L || r->flash_kind == FX_FIZZLE_L;
    bool local_fx = r->flash_frames && defender_left == is_left;
#endif
    bool local_impact   = local_fx && (r->flash_kind == FX_IMPACT_L || r->flash_kind == FX_IMPACT_R);
    duel_view_spell_t piercer;
    bool have_piercer = incoming_void_at_ward(&r->view, side, &piercer);
    bool ward_punctured = have_piercer ||
                          (local_impact && DUEL_KIND_ELEMENT(r->flash_spell_kind) == ELEM_VOID);
    int ward_lane = have_piercer ? spell_lane_y(piercer.kind) : spell_lane_y(r->flash_spell_kind);
    bool archive = render_host(r) && render_scene(r) == DUEL_HOST_SCENE_ARCHIVE;

#ifdef ARCANE_M12
    // M12 retires the M9 upper archive underlay: the raised rooftop now owns that
    // band, and the archival occupation moves into the tower floor below. The
    // courier (Wave 6) and rare event (Wave 7) layer into the same floor.
    (void)archive;
    draw_floor(fb, r, is_left);
#ifdef ARCANE_M13
    if (!(r->revision & M13_AFTERMATH_WIRE)) {
        draw_courier(fb, r, is_left);
        draw_rare_event(fb, r, is_left);
    }
#else
    draw_courier(fb, r, is_left);
    draw_rare_event(fb, r, is_left);
#endif
#else
    if (archive) draw_archive(fb, wz, is_left, frame);
#endif

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
                if (wz->pose_ticks >= 2) duel_fb_px(fb, 16, 49 + DUEL_ROOF_DY, true);
            }

            // Shield: a vertical ward arc on the gap side of this half's wizard.
            if (wz->shield_ticks) {
                draw_ward(fb, facing,
#ifdef ARCANE_M13
                          wz->ward_strength,
                          wz->ward_focus,
#else
                          1,
                          2,
#endif
                          ward_punctured, ward_lane);
            }
            draw_charge(fb, wz, facing, frame);
#ifdef ARCANE_M13
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
            draw_m13_status(fb, wz, facing, frame);
#endif
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
#ifdef ARCANE_M13
        duel_view_wizard_t caster = duel_view_wizard(&r->view, (uint8_t)s);
        m13_draw_spell(fb, &spell, (uint8_t)s, caster.variant, is_left, frame);
#else
        int x;
        if (!duel_battlefield_to_x(spell.pos, is_left, &x)) continue;
        int y = spell_lane_y(spell.kind) + ((frame >> 1) & 1); // 1 px cosmetic bob
        spell_glyph(fb, x, y, spell.kind, spell.dir);
#endif
    }

#ifdef ARCANE_M13
    draw_m13_reaction(fb, r->flash_kind, is_left, r->flash_frames);
#endif

    // HP pips for THIS half's wizard.
#ifdef ARCANE_M13
    /* M13: twelve 2x1 pips in a 5x11 grid, attached low beside the wizard.
     * Each row fills gapward then outward; damage therefore clears from top. */
    {
        /* The health instrument owns its exact cells even while medics and
         * replacement silhouettes cross the away-side rooftop entrance. */
        for (int i = 0; i < SIM_MAX_HP; i++) {
            int canonical_x = (i & 1) ? 4 : 7;
            int px = is_left ? canonical_x : DUEL_CANVAS_W - 2 - canonical_x;
            int py = 57 - (i / 2) * 2;
            duel_fb_px(fb, px, py, false);
            duel_fb_px(fb, px + 1, py, false);
        }
        int hp = wz->hp > SIM_MAX_HP ? SIM_MAX_HP : wz->hp;
        for (int i = 0; i < hp; i++) {
            int row = i / 2;
            int canonical_x = (i & 1) ? 4 : 7;
            int px = is_left ? canonical_x : DUEL_CANVAS_W - 2 - canonical_x;
            int py = 57 - row * 2;
            duel_fb_px(fb, px, py, true);
            duel_fb_px(fb, px + 1, py, true);
        }
    }
#elif defined(ARCANE_M12)
    // M12: a vertical pip column standing on the rooftop just outside the robe
    // on the away-from-gap side, so health reads as the champion's own and the
    // whole bottom band (y111+) frees up for ground texture. Depletes top-down.
    {
        const int robe_bot = 75 + DUEL_ROOF_DY;      // matches wiz_body
        const int hx = is_left ? 8 : DUEL_CANVAS_W - 1 - 9; // 8 / 22, just outside the robe
        for (int i = 0; i < wz->hp && i < SIM_MAX_HP; i++) {
            int py = robe_bot - 1 - 3 * i;
            duel_fb_px(fb, hx,     py,     true);
            duel_fb_px(fb, hx + 1, py,     true);
            duel_fb_px(fb, hx,     py + 1, true);
            duel_fb_px(fb, hx + 1, py + 1, true);
        }
    }
#else
    // Release (frozen M11.5 layout): pips in the bottom corner away from the gap.
    for (int i = 0; i < wz->hp && i < SIM_MAX_HP; i++) {
        int px = is_left ? 5 + 4 * i : 24 - 4 * i;
        duel_fb_px(fb, px, 112, true);
        duel_fb_px(fb, px + 1, 112, true);
        duel_fb_px(fb, px, 113, true);
        duel_fb_px(fb, px + 1, 113, true);
    }
#endif

    // One-shot outcomes use three deliberately different grammars. All of this
    // is render-frame state: losing it costs only the flourish, never health or
    // split convergence.
    if (local_fx) {
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
                int lost = wz->hp;
#ifdef ARCANE_M13
                int row = lost / 2;
                int canonical_x = (lost & 1) ? 4 : 7;
                int px = is_left ? canonical_x : DUEL_CANVAS_W - 2 - canonical_x;
                int py = 57 - row * 2;
                duel_fb_px(fb, px - 1, py - 1, true);
                duel_fb_px(fb, px + 2, py - 1, true);
#elif defined(ARCANE_M12)
                // Twin the corner-flash to the pip that just vanished from the
                // rooftop HP column (the first empty slot above the survivors).
                int hx = is_left ? 8 : DUEL_CANVAS_W - 1 - 9;
                int py = (75 + DUEL_ROOF_DY) - 1 - 3 * lost;
                duel_fb_px(fb, hx - 1, py, true);     duel_fb_px(fb, hx + 2, py, true);
                duel_fb_px(fb, hx - 1, py + 1, true); duel_fb_px(fb, hx + 2, py + 1, true);
#else
                int px   = is_left ? 5 + 4 * lost : 24 - 4 * lost;
                duel_fb_px(fb, px - 1, 111, true); duel_fb_px(fb, px + 2, 111, true);
                duel_fb_px(fb, px - 1, 114, true); duel_fb_px(fb, px + 2, 114, true);
#endif
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
#ifdef ARCANE_M13
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
#endif
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

    // M10 alert sigil sits above all scene/combat artwork, but an open scry
    // replaces it with the normalized in-panel summary.
    if (!duel_view_scry_open(&r->view)) draw_alert_sigil(fb, r, is_left);

#ifdef ARCANE_M13
    if (!duel_view_scry_open(&r->view)) draw_local_attunement(fb, r, wz, is_left);
#endif

    // M7 scrying overlay, drawn above the world when the layer-key chord is
    // held. The stale-link and debug glyphs draw AFTER, so a broken link is
    // still legible in its corner even with the panel up.
    if (duel_view_scry_open(&r->view)) {
#ifndef ARCANE_M12
        if (archive) clear_archive_panel(fb);
#endif
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
