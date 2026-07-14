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

static void wiz_hspan(duel_fb_t *fb, int x0, int x1, int y) {
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
    // The civic byte is authoritative for the occupation. Until the glue layer
    // (keymap) translates scene->civic in a later wave, bridge the legacy
    // scene channel: an online Archive scene with a default (Commons) civic byte
    // shows the Research floor, honouring the scene-driven world-test contract.
    if (floor == DUEL_M12_FLOOR_COMMONS && render_host(r) &&
        render_scene(r) == DUEL_HOST_SCENE_ARCHIVE)
        floor = DUEL_M12_FLOOR_RESEARCH;

    // Solid ceiling beam splitting the rooftop from the floor (both cities).
    // City character lives in the details below the beam, not the beam itself.
    for (int x = 0; x < DUEL_CANVAS_W; x++)
        duel_fb_px(fb, x, 61, true);
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
    int wa = FLR_X(2), wb = FLR_X(10);
    int wlo = wa < wb ? wa : wb, whi = wa < wb ? wb : wa;
    floor_window(fb, wlo, whi, 66, 84, is_left);
    // Off the courier lifecycle columns (duel_courier life_ax = {24,17,11,26})
    // so a hanging fixture never swallows a courier's density tell, and clear of
    // the gap-side cabinet so the two don't stack up.
    floor_hanging(fb, FLR_X(15), is_left);

    // Ground line of the room.
    wiz_hspan(fb, 0, DUEL_CANVAS_W - 1, 110);

    // Occupation furniture, then the session-seeded resident living among it.
    draw_floor_anchors(fb, floor, is_left);
    m12_resident_t res = m12_resident_derive(r->seed, is_left, floor, mode, r->civic_phase);
    m12_resident_draw(fb, &res, is_left, mode, 0);

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

static void draw_ward(duel_fb_t *fb, int facing, int thickness, bool punctured, int puncture_y) {
    int ax = 16 + facing * 9;
    for (int t = 0; t < thickness; t++) {
        int x = ax + facing * t;
        for (int y = 58 + DUEL_ROOF_DY; y <= 72 + DUEL_ROOF_DY; y++) {
            int d = y - puncture_y;
            if (d < 0) d = -d;
            if (punctured && d <= 2) continue;
            duel_fb_px(fb, x, y, true);
        }
        duel_fb_px(fb, x - facing, 56 + DUEL_ROOF_DY, true);
        duel_fb_px(fb, x - facing, 57 + DUEL_ROOF_DY, true);
        duel_fb_px(fb, x - facing, 73 + DUEL_ROOF_DY, true);
        duel_fb_px(fb, x - facing, 74 + DUEL_ROOF_DY, true);
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

// Outlined (or filled) rectangle helper for the M7 overlay panel.
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
static void draw_overlay(duel_fb_t *fb, const duel_render_t *r) {
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
    uint8_t scene = render_host(r) ? render_scene(r) : DUEL_SCRY_SCENE(r->view.scry);
    for (int i = 0; i < SCRY_SCENES; i++) {
        int sx = 6 + i * 7;
        ov_rect(fb, sx, 35, sx + 3, 38, i == (scene % SCRY_SCENES));
    }
}

void wiz_draw_scene(duel_fb_t *fb, const duel_render_t *r, bool is_left, uint32_t frame, bool debug_hud) {
    int side = is_left ? SIM_SIDE_L : SIM_SIDE_R;
    duel_view_wizard_t wizard = duel_view_wizard(&r->view, (uint8_t)side);
    const duel_view_wizard_t *wz = &wizard;
    int                 facing = is_left ? +1 : -1; // toward the gap (see header)
    bool defender_left = r->flash_kind == FX_IMPACT_L || r->flash_kind == FX_DEFLECT_L || r->flash_kind == FX_FIZZLE_L;
    bool local_fx       = r->flash_frames && defender_left == is_left;
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
    draw_courier(fb, r, is_left);
    draw_rare_event(fb, r, is_left);
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
                draw_ward(fb, facing, 1, ward_punctured, ward_lane);
            }
            draw_charge(fb, wz, facing, frame);
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
        int x;
        if (!duel_battlefield_to_x(spell.pos, is_left, &x)) continue;
        int y = spell_lane_y(spell.kind) + ((frame >> 1) & 1); // 1 px cosmetic bob
        spell_glyph(fb, x, y, spell.kind, spell.dir);
    }

    // HP pips for THIS half's wizard.
#ifdef ARCANE_M12
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
#ifdef ARCANE_M12
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
        } else {
            // Redirection: the ward is the dominant thick shape while the
            // carrier breaks into two streaks thrown back toward the gap.
            int ax   = 16 + facing * 9;
            int dist = 2 + (8 - r->flash_frames) / 2;
            draw_ward(fb, facing, 2, false, fy);
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

    // M7 scrying overlay, drawn above the world when the layer-key chord is
    // held. The stale-link and debug glyphs draw AFTER, so a broken link is
    // still legible in its corner even with the panel up.
    if (duel_view_scry_open(&r->view)) {
#ifndef ARCANE_M12
        if (archive) clear_archive_panel(fb);
#endif
        draw_overlay(fb, r);
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
