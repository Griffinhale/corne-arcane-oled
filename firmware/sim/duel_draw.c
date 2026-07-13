/*
 * duel_draw.c — wizard silhouette renderer on a plain framebuffer.
 *
 * Moved from the M1 wizard.h nearly verbatim; the only change is that every
 * primitive writes into a duel_fb_t instead of calling oled_write_pixel(),
 * so this file also compiles on the host for tests and the previewer.
 */
#include <string.h>

#include "duel_draw.h"

void duel_fb_clear(duel_fb_t *fb) {
    memset(fb->bits, 0, sizeof fb->bits);
}

void duel_fb_px(duel_fb_t *fb, int x, int y, bool on) {
    if (x < 0 || x >= DUEL_CANVAS_W || y < 0 || y >= DUEL_CANVAS_H) return;
    int idx = y * DUEL_CANVAS_W + x;
    if (on) {
        fb->bits[idx >> 3] |= (uint8_t)(1u << (idx & 7));
    } else {
        fb->bits[idx >> 3] &= (uint8_t)~(1u << (idx & 7));
    }
}

bool duel_fb_get(const duel_fb_t *fb, int x, int y) {
    if (x < 0 || x >= DUEL_CANVAS_W || y < 0 || y >= DUEL_CANVAS_H) return false;
    int idx = y * DUEL_CANVAS_W + x;
    return (fb->bits[idx >> 3] >> (idx & 7)) & 1u;
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

// A compact standing wizard (~1/3 of the original M1 figure, hardware
// feedback: full-size read as a blob at actual OLED scale). Centred at x=16
// with the staff hand at y~64 so bolts fly out at cast height. xo/yo shift
// the whole figure (duel_fb_px clips, so off-canvas offsets are free); the
// M5 lifecycle uses them to sink a collapsing wizard and walk in a fresh one.
static void wiz_body(duel_fb_t *fb, bool casting, int facing, uint8_t variant, int xo, int yo) {
    const int cx = DUEL_CANVAS_W / 2 + xo;   // 16 + xo

    // Pointed hat: filled triangle apex -> base (max half-width 3).
    const int hat_apex_y = 54 + yo;
    const int hat_base_y = 61 + yo;
    for (int y = hat_apex_y; y <= hat_base_y; y++) {
        int hw = (y - hat_apex_y) * 3 / (hat_base_y - hat_apex_y); // 0..3
        wiz_hspan(fb, cx - hw, cx + hw, y);
    }
    // Hat brim.
    wiz_hspan(fb, cx - 4, cx + 4, hat_base_y + 1);

    // Robe: trapezoid widening toward the base (half-width 2..4).
    const int robe_top = hat_base_y + 2;   // 63 + yo
    const int robe_bot = 75 + yo;
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
        for (int y = 64 + yo; y <= robe_bot; y++) duel_fb_px(fb, sx, y, true);
        duel_fb_px(fb, sx, 62 + yo, true);
        duel_fb_px(fb, sx - facing, 63 + yo, true);
    } else {
        // Casting: staff raised toward the hat, a bright charging starburst
        // above the apex so a wind-up reads clearly at a glance.
        wiz_line(fb, cx + facing * 2, 68 + yo, cx + facing * 5, 56 + yo);
        int bx = cx, by = hat_apex_y - 4;    // burst centre above the hat
        for (int dx = -1; dx <= 1; dx++)
            for (int dy = -1; dy <= 1; dy++) duel_fb_px(fb, bx + dx, by + dy, true); // solid orb
        duel_fb_px(fb, bx - 2, by, true); duel_fb_px(fb, bx + 2, by, true);          // radiating
        duel_fb_px(fb, bx, by - 2, true); duel_fb_px(fb, bx, by + 2, true);          // sparks
        // a bolt streaking off toward the gap
        duel_fb_px(fb, cx + facing * 3, by + 2, true);
        duel_fb_px(fb, cx + facing * 4, by + 2, true);
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
    wiz_hspan(fb, lo, hi, 72);
    wiz_hspan(fb, lo, hi, 73);
    // Fallen hat just past the head: 3-px base with a 1-px apex row on top.
    int hx = head - facing * 2;              // hat centre, one px clear of the head
    wiz_hspan(fb, hx < head ? hx - 1 : head + 1, hx < head ? head - 1 : hx + 1, 72);
    duel_fb_px(fb, hx, 71, true);
    if ((variant & 3) == 3) duel_fb_px(fb, hx, 70, true); // pompom stayed on
    // Dropped staff: flat on the ground, toward the gap.
    for (int i = 1; i <= 5; i++) duel_fb_px(fb, cx + facing * i, 75, true);
}

// M5 medic: a short hatless figure (8 px, clearly not a wizard) leaning into
// the drag — 2x2 head, torso kinked 1 px toward -facing, splayed legs.
static void medic_draw(duel_fb_t *fb, int x, int facing) {
    duel_fb_px(fb, x, 66, true); duel_fb_px(fb, x + 1, 66, true);
    duel_fb_px(fb, x, 67, true); duel_fb_px(fb, x + 1, 67, true);
    for (int y = 68; y <= 70; y++) duel_fb_px(fb, x, y, true);
    duel_fb_px(fb, x - facing, 71, true);
    duel_fb_px(fb, x - facing, 72, true);
    duel_fb_px(fb, x - facing - 1, 73, true);
    duel_fb_px(fb, x - facing + 1, 73, true);
}

bool duel_battlefield_to_x(uint8_t u, bool is_left, int *x) {
    if (is_left) {
        if (u > 95) return false;
        *x = 16 + u / 6; // wizard (16) -> gap edge (31)
        return true;
    }
    if (u < 160) return false;
    *x = 16 - (255 - u) / 6; // gap edge (1) -> wizard (16)
    return true;
}

#define SPELL_Y 64 // flight height ~= the resting staff orb

static void spell_glyph(duel_fb_t *fb, int x, int y, uint8_t kind, int dir) {
    int back = dir > 0 ? -1 : +1;

    // Bolder, silhouette-distinct element glyphs (readable at 32 px): FORCE is
    // a solid cannonball, FROST a spiky crystalline star, VOID a hollow ring,
    // EMBER a blazing head with a long comet tail.
    switch (DUEL_KIND_ELEMENT(kind)) {
        case ELEM_FORCE: // solid 3x3 core
            for (int dx = -1; dx <= 1; dx++)
                for (int dy = -1; dy <= 1; dy++) duel_fb_px(fb, x + dx, y + dy, true);
            break;
        case ELEM_FROST: // 8-point star reaching +-2 on the axes
            duel_fb_px(fb, x, y, true);
            duel_fb_px(fb, x - 2, y, true); duel_fb_px(fb, x + 2, y, true);
            duel_fb_px(fb, x, y - 2, true); duel_fb_px(fb, x, y + 2, true);
            duel_fb_px(fb, x - 1, y - 1, true); duel_fb_px(fb, x + 1, y - 1, true);
            duel_fb_px(fb, x - 1, y + 1, true); duel_fb_px(fb, x + 1, y + 1, true);
            break;
        case ELEM_VOID: // hollow ring, empty core
            duel_fb_px(fb, x - 1, y, true); duel_fb_px(fb, x + 1, y, true);
            duel_fb_px(fb, x, y - 1, true); duel_fb_px(fb, x, y + 1, true);
            duel_fb_px(fb, x - 1, y - 1, true); duel_fb_px(fb, x + 1, y - 1, true);
            duel_fb_px(fb, x - 1, y + 1, true); duel_fb_px(fb, x + 1, y + 1, true);
            break;
        case ELEM_EMBER: // blazing head + long comet tail behind
            duel_fb_px(fb, x, y, true);
            duel_fb_px(fb, x - 1, y, true); duel_fb_px(fb, x + 1, y, true);
            duel_fb_px(fb, x, y - 1, true); duel_fb_px(fb, x, y + 1, true);
            duel_fb_px(fb, x + 2 * back, y, true);
            duel_fb_px(fb, x + 3 * back, y, true);
            duel_fb_px(fb, x + 4 * back, y - 1, true);
            break;
    }

    switch (DUEL_KIND_MODIFIER(kind)) {
        case MOD_NONE:
            break;
        case MOD_SWIFT: // a long speed streak trailing behind
            duel_fb_px(fb, x + 2 * back, y, true);
            duel_fb_px(fb, x + 3 * back, y, true);
            duel_fb_px(fb, x + 4 * back, y, true);
            break;
        case MOD_HEAVY: // a heavy diagonal casing (clears FROST's axial spikes)
            duel_fb_px(fb, x - 2, y - 2, true); duel_fb_px(fb, x + 2, y - 2, true);
            duel_fb_px(fb, x - 2, y + 2, true); duel_fb_px(fb, x + 2, y + 2, true);
            break;
    }
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
        ov_rect(fb, lx, 15, lx + 3, 18, i == (r->overlay_layer & 3));
    }

    // Host link: solid bar when online, two disconnected boxes when offline
    // (the M8 heartbeat is not built yet, so the glue reports offline).
    if (r->overlay_host) {
        ov_rect(fb, 6, 23, 13, 27, true);
    } else {
        ov_rect(fb, 6, 23, 8, 27, false);
        ov_rect(fb, 11, 23, 13, 27, false);
    }
    // Notification count: up to four 2x2 dots on the right of the host row.
    int notif = r->overlay_notif > 4 ? 4 : r->overlay_notif;
    for (int i = 0; i < notif; i++) {
        int nx = 17 + i * 3;
        duel_fb_px(fb, nx, 24, true);     duel_fb_px(fb, nx + 1, 24, true);
        duel_fb_px(fb, nx, 25, true);     duel_fb_px(fb, nx + 1, 25, true);
    }

    // Scene selector: one marker per scene, the current one filled.
    for (int i = 0; i < SCRY_SCENES; i++) {
        int sx = 6 + i * 7;
        ov_rect(fb, sx, 35, sx + 3, 38, i == (r->w.scry.scene % SCRY_SCENES));
    }
}

void wiz_draw_scene(duel_fb_t *fb, const duel_render_t *r, bool is_left, uint32_t frame, bool debug_hud) {
    const sim_world_t  *w      = &r->w;
    int                 side   = is_left ? SIM_SIDE_L : SIM_SIDE_R;
    const sim_wizard_t *wz     = &w->wiz[side];
    int                 facing = is_left ? +1 : -1; // toward the gap (see header)

    // Lifecycle (M5): each phase has its own tableau, derived purely from
    // (life, life_ticks, variant) so master and slave render identically.
    // Sparks and the shield arc only apply to a standing, active wizard.
    switch (wz->life) {
        case LIFE_ACTIVE:
            wiz_draw(fb, wz->pose == POSE_CAST, facing, wz->variant);

            if (wz->pose == POSE_RECOVER) {
                // Fading sparks above the hat make RECOVER observable on hardware.
                duel_fb_px(fb, 14, 50, true);
                duel_fb_px(fb, 18, 51, true);
                if (wz->pose_ticks >= 2) duel_fb_px(fb, 16, 49, true);
            }

            // Shield: a vertical ward arc on the gap side of this half's wizard.
            if (wz->shield_ticks) {
                int ax = 16 + facing * 9;
                for (int y = 59; y <= 71; y++) duel_fb_px(fb, ax, y, true);
                duel_fb_px(fb, ax - facing, 57, true);
                duel_fb_px(fb, ax - facing, 58, true);
                duel_fb_px(fb, ax - facing, 72, true);
                duel_fb_px(fb, ax - facing, 73, true);
            }
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
                duel_fb_px(fb, 15, 69, true);
                duel_fb_px(fb, 16, 69, true);
                duel_fb_px(fb, 17, 69, true);
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
        if (!w->spell[s].active) continue;
        int x;
        if (!duel_battlefield_to_x(w->spell[s].pos, is_left, &x)) continue;
        int y = SPELL_Y + ((frame >> 1) & 1); // ±1 px bob, render-frame only
        spell_glyph(fb, x, y, w->spell[s].kind, w->spell[s].dir);
    }

    // HP pips for THIS half's wizard, bottom corner away from the gap.
    for (int i = 0; i < wz->hp && i < SIM_MAX_HP; i++) {
        int px = is_left ? 5 + 4 * i : 24 - 4 * i;
        duel_fb_px(fb, px, 112, true);
        duel_fb_px(fb, px + 1, 112, true);
        duel_fb_px(fb, px, 113, true);
        duel_fb_px(fb, px + 1, 113, true);
    }

    // One-shot outcome flashes (armed by the glue when fx_seq changes).
    if (r->flash_frames) {
        bool defender_left = r->flash_kind == FX_IMPACT_L || r->flash_kind == FX_DEFLECT_L || r->flash_kind == FX_FIZZLE_L;
        bool is_impact     = r->flash_kind == FX_IMPACT_L || r->flash_kind == FX_IMPACT_R;
        bool is_fizzle     = r->flash_kind == FX_FIZZLE_L || r->flash_kind == FX_FIZZLE_R;
        if (is_impact) {
            // Impact: a bold border on BOTH screens — thick and solid for the
            // first frames, thinning as it fades — plus a shock cross that
            // bursts out from the hit height. Punchier than a 1-px strobe.
            int thick = r->flash_frames >= 8 ? 2 : 1;
            for (int t = 0; t < thick; t++) {
                for (int x = 0; x < DUEL_CANVAS_W; x++) {
                    duel_fb_px(fb, x, t, true);
                    duel_fb_px(fb, x, DUEL_CANVAS_H - 1 - t, true);
                }
                for (int y = 0; y < DUEL_CANVAS_H; y++) {
                    duel_fb_px(fb, t, y, true);
                    duel_fb_px(fb, DUEL_CANVAS_W - 1 - t, y, true);
                }
            }
            if (r->flash_frames >= 5) { // expanding shock cross at the gap edge
                int reach = 13 - r->flash_frames; // grows as the flash decays
                int hx    = is_left ? DUEL_CANVAS_W - 2 : 1;
                for (int d = -reach; d <= reach; d++) {
                    duel_fb_px(fb, hx, SPELL_Y + d, true);
                    duel_fb_px(fb, hx - facing * (d < 0 ? -d : d), SPELL_Y, true);
                }
            }
        } else if (defender_left == is_left) {
            if (is_fizzle) {
                // Fizzle: a small puff dissipating at the downed wizard's
                // doorstep — plus shape first, then an expanding diagonal ring.
                int fx0 = is_left ? 18 : 14;
                if (r->flash_frames >= 5) {
                    duel_fb_px(fb, fx0, SPELL_Y, true);
                    duel_fb_px(fb, fx0 - 1, SPELL_Y, true);
                    duel_fb_px(fb, fx0 + 1, SPELL_Y, true);
                    duel_fb_px(fb, fx0, SPELL_Y - 1, true);
                    duel_fb_px(fb, fx0, SPELL_Y + 1, true);
                } else {
                    duel_fb_px(fb, fx0 - 2, SPELL_Y - 2, true);
                    duel_fb_px(fb, fx0 + 2, SPELL_Y - 2, true);
                    duel_fb_px(fb, fx0 - 2, SPELL_Y + 2, true);
                    duel_fb_px(fb, fx0 + 2, SPELL_Y + 2, true);
                }
            } else {
                // Deflect flares a double ward arc on the defender's screen only.
                int ax = 16 + facing * 9;
                for (int y = 55; y <= 75; y++) {
                    duel_fb_px(fb, ax, y, true);
                    if (r->flash_frames & 1) duel_fb_px(fb, ax + facing, y, true);
                }
            }
        }
    }

    // M7 scrying overlay, drawn above the world when the layer-key chord is
    // held. The stale-link and debug glyphs draw AFTER, so a broken link is
    // still legible in its corner even with the panel up.
    if (scry_is_open(&r->w)) draw_overlay(fb, r);

    if (r->stale_link) {
        // Two separated chain links in the top corner nearest the gap.
        int bx = is_left ? 23 : 2;
        draw_box3(fb, bx, 2);
        draw_box3(fb, bx + 4, 6);
    }

    if (debug_hud) {
        duel_fb_px(fb, (int)(w->tick % 25), DUEL_CANVAS_H - 1, true);
        int dots = w->overflow_count > 4 ? 4 : w->overflow_count;
        for (int i = 0; i < dots; i++) duel_fb_px(fb, 1 + 2 * i, 0, true);
    }
}
