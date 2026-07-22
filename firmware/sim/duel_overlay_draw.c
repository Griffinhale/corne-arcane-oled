#include "duel_draw_internal.h"
#include "duel_host.h"
#include "duel_resident.h"
#include "duel_runtime.h"

void duel_overlay_draw_box3(duel_fb_t *fb, int x, int y) {
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

static void draw_alert_bitmap(duel_fb_t *fb, uint8_t category, int ox, int oy, bool mirror) {
    if (category >= DUEL_HOST_CATEGORY_COUNT)
        return;
    for (int y = 0; y < 7; y++) {
        for (int x = 0; x < 5; x++) {
            if (alert_glyphs[category][y] & (1u << (4 - x))) {
                duel_fb_px(fb, mirror ? ox - x : ox + x, oy + y, true);
            }
        }
    }
}

/* M15: the alert hangs as a banner on the wizard tower's shaft (between the
 * upper window and the balcony) instead of owning a reserved top corner.
 * Canonical coordinates describe the left shaft; the right half is its exact
 * desk mirror, so the pair still reads as one desk-space instrument. */
void duel_overlay_draw_alert(duel_fb_t *fb, const duel_render_t *r, bool is_left) {
    uint8_t category = DUEL_HOST_ALERT_CATEGORY(r->alert);
    uint8_t priority = DUEL_HOST_ALERT_PRIORITY(r->alert);
    uint8_t age = DUEL_HOST_ALERT_AGE(r->alert);
    if (!duel_render_host_online(r) || !duel_render_notification_count(r) ||
        category == DUEL_HOST_CATEGORY_NONE || priority == DUEL_HOST_PRIORITY_NONE)
        return;
    // Clear the banner field on the shaft face so the glyph stays legible
    // over a lit window edge or a dragged body crossing the doorway.
    for (int y = 24; y <= 35; y++)
        for (int x = 2; x <= 10; x++)
            duel_fb_px(fb, is_left ? x : 31 - x, y, false);
    draw_alert_bitmap(fb, category, is_left ? 3 : 28, 26, !is_left);
    if (priority >= DUEL_HOST_PRIORITY_NORMAL) {
        for (int x = 2; x <= 8; x++) {
            duel_fb_px(fb, is_left ? x : 31 - x, 25, true);
            duel_fb_px(fb, is_left ? x : 31 - x, 33, true);
        }
    }
    if (priority == DUEL_HOST_PRIORITY_CRITICAL) {
        for (int y = 26; y <= 32; y++) {
            duel_fb_px(fb, is_left ? 2 : 29, y, true);
            duel_fb_px(fb, is_left ? 8 : 23, y, true);
        }
        duel_fb_px(fb, is_left ? 10 : 21, 25, true);
        duel_fb_px(fb, is_left ? 10 : 21, 33, true);
    }
    int accents = 3 - (age > 5 ? 3 : age / 2);
    for (int i = 0; i < accents; i++)
        duel_fb_px(fb, is_left ? 2 + i * 3 : 29 - i * 3, 24, true);
    int pips = duel_render_notification_count(r) > 4 ? 4 : duel_render_notification_count(r);
    for (int i = 0; i < pips; i++)
        duel_fb_px(fb, is_left ? 2 + i * 2 : 29 - i * 2, 35, true);
    if (DUEL_HOST_CONTEXT_PERSISTENT(r->external)) {
        int ax = is_left ? 6 : 25;
        duel_fb_px(fb, ax, 36, true);
        duel_fb_px(fb, ax, 37, true);
        duel_fb_px(fb, ax + (is_left ? -1 : 1), 38, true);
        duel_fb_px(fb, ax + (is_left ? 1 : -1), 38, true);
    }
}

/* current scry is additive architecture rather than a panel: lens and motes in the
 * sky, layer runes on the away tower wall, link runes at the gap, the alert
 * summary in the gap-side top strip (the former outer corner belongs to the
 * tower peak now), and scene sigils embedded in the ceiling beam. */
void duel_overlay_draw_scry(duel_fb_t *fb, const duel_render_t *r, bool is_left) {
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
    int notif = duel_render_notification_count(r) > 4 ? 4 : duel_render_notification_count(r);
    for (int i = 0; i < notif; i++)
        duel_fb_px(fb, ex + mote_xy[i][0] * facing, ey + mote_xy[i][1], true);

    /* Four global layer runes climb the away-side tower wall. The inactive
     * dash sits at x2-3: the shaft's always-lit edge column (x1) would
     * swallow an x1 dot on both halves, and x4 lands on the shaft window's
     * asymmetric lintel row (mirror-test contract). */
    uint8_t active = DUEL_RENDER_GLOBAL_LAYER(r->layer);
    for (int i = 0; i < 4; i++) {
        int y0 = 18 + i * 5;
        if (i == active) {
            for (int y = y0; y < y0 + 3; y++)
                for (int x = 1; x <= 3; x++)
                    duel_fb_px(fb, SCRY_X(x), y, true);
        } else {
            duel_fb_px(fb, SCRY_X(2), y0 + 1, true);
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
    if (duel_render_host_online(r)) {
        for (int x = 26; x <= 28; x++)
            duel_fb_px(fb, SCRY_X(x), hy + 1, true);
    } else {
        duel_fb_px(fb, SCRY_X(27), hy, true);
        duel_fb_px(fb, SCRY_X(27), hy + 2, true);
    }

    /* The scry alert summary sits in the gap-side top strip (the former
     * outer corner now belongs to the tower peak, whose art is deliberately
     * asymmetric — added scry instruments must land on mirror-symmetric
     * base cells). Scry suppresses the ordinary shaft banner, then adds only
     * these category/priority instruments. */
    uint8_t category = DUEL_HOST_ALERT_CATEGORY(r->alert);
    uint8_t priority = DUEL_HOST_ALERT_PRIORITY(r->alert);
    if (category && priority) {
        /* Backdrop first: the celestial arc crosses this strip by day, and
         * an instrument summary must stay legible over it (same discipline
         * as the shaft banner's cleared field). */
        for (int y = 0; y <= 14; y++)
            for (int x = 22; x <= 30; x++)
                duel_fb_px(fb, SCRY_X(x), y, false);
        draw_alert_bitmap(fb, category, is_left ? 24 : 7, 3, !is_left);
        for (int i = 0; i < priority; i++)
            duel_fb_px(fb, SCRY_X(23 + i * 3), 1, true);
        if (DUEL_HOST_CONTEXT_PERSISTENT(r->external)) {
            duel_fb_px(fb, SCRY_X(26), 11, true);
            duel_fb_px(fb, SCRY_X(26), 12, true);
            duel_fb_px(fb, SCRY_X(25), 13, true);
            duel_fb_px(fb, SCRY_X(27), 13, true);
        }
    }

    /* Selector meaning is stable while every architectural position mirrors. */
    uint8_t scene = duel_render_host_online(r) ? duel_render_scene(r)
                                               : (uint8_t)((r->view.outcome_overlay >> 5) & 3u);
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

void duel_overlay_draw_attunement(duel_fb_t *fb, const duel_render_t *r,
                                  const duel_view_wizard_t *wz, bool is_left) {
    uint8_t local = DUEL_RENDER_LOCAL_LAYER(r->layer);
    if ((is_left && local != DUEL_RENDER_LOCAL_LEFT) ||
        (!is_left && local != DUEL_RENDER_LOCAL_RIGHT))
        return;
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

/* Diegetic HP: eight 2x2 lit windows stacked as the shaft's lower tier, two
 * columns (gapward x7-8, outer x3-4) by four rows between the banner field
 * and the base flare. Each row fills gapward then outward, bottom-up, so
 * damage darkens the shaft from the top. Single source for the clear, fill,
 * and lost-window flash sites. */
static void hp_window_xy(int i, bool is_left, int *px, int *py) {
    int canonical_x = (i & 1) ? 3 : 7;
    *px = is_left ? canonical_x : DUEL_CANVAS_W - 2 - canonical_x;
    *py = 56 - (i / 2) * 4;
}

void duel_overlay_draw_health(duel_fb_t *fb, const duel_view_wizard_t *wz, bool is_left) {
    /* The health instrument owns its exact cells even while medics and
     * replacement silhouettes cross the away-side rooftop entrance. */
    int px, py;
    for (int i = 0; i < SIM_MAX_HP; i++) {
        hp_window_xy(i, is_left, &px, &py);
        for (int dy = 0; dy < 2; dy++) {
            duel_fb_px(fb, px, py + dy, false);
            duel_fb_px(fb, px + 1, py + dy, false);
        }
    }
    int hp = wz->hp > SIM_MAX_HP ? SIM_MAX_HP : wz->hp;
    for (int i = 0; i < hp; i++) {
        hp_window_xy(i, is_left, &px, &py);
        for (int dy = 0; dy < 2; dy++) {
            duel_fb_px(fb, px, py + dy, true);
            duel_fb_px(fb, px + 1, py + dy, true);
        }
    }
}

// One-shot local outcome flourishes (impact/fizzle/heal/shatter/deflect).
// All render-frame state: losing it costs only the flourish, never health or
// split convergence.
void duel_overlay_draw_local_fx(duel_fb_t *fb, const duel_render_t *r, const duel_view_wizard_t *wz,
                                int facing, bool is_left) {
    bool is_impact = r->flash_kind == FX_IMPACT_L || r->flash_kind == FX_IMPACT_R;
    bool is_fizzle = r->flash_kind == FX_FIZZLE_L || r->flash_kind == FX_FIZZLE_R;
    int tier = DUEL_KIND_TIER(r->flash_spell_kind);
    int fy = duel_combat_spell_lane_y(r->flash_spell_kind);

    if (is_impact) {
        // Force enters from the gap: contact burst, inward shock line,
        // local debris, recoil above, and a flashing frame at the shaft
        // window that just went dark. Only the defender's border corners twitch.
        // M15 weight pass: the flourish scales one presentation tier up.
        if (tier < SPELL_TIER_SATURATED)
            tier++;
        int hx = 16 + facing * 5;
        int reach = 2 + tier + (r->flash_frames >= 8);
        for (int d = 0; d <= reach; d++)
            duel_fb_px(fb, hx + facing * d, fy, true);
        for (int d = 1; d <= reach; d++) {
            duel_fb_px(fb, hx, fy - d, true);
            duel_fb_px(fb, hx, fy + d, true);
        }
        duel_fb_line(fb, hx, fy, hx - facing * (3 + tier), fy - 3 - tier);
        duel_fb_line(fb, hx, fy, hx - facing * (2 + tier), fy + 4 + tier);
        duel_fb_px(fb, hx - facing * 6, fy - 8 - tier, true);
        duel_fb_px(fb, hx - facing * 4, fy + 9 + tier, true);
        if (tier >= SPELL_TIER_LONG) {
            duel_fb_px(fb, hx + facing * 2, fy - 7, true);
            duel_fb_px(fb, hx + facing * 3, fy + 7, true);
        }
        if (r->flash_frames >= 7) {
            for (int d = 0; d < 4; d++) {
                duel_fb_px(fb, d, 0, true);
                duel_fb_px(fb, DUEL_CANVAS_W - 1 - d, 0, true);
                duel_fb_px(fb, d, DUEL_CANVAS_H - 1, true);
                duel_fb_px(fb, DUEL_CANVAS_W - 1 - d, DUEL_CANVAS_H - 1, true);
            }
        }
        if (wz->hp < SIM_MAX_HP) {
            int px, py;
            hp_window_xy(wz->hp, is_left, &px, &py);
            duel_fb_px(fb, px - 1, py - 1, true);
            duel_fb_px(fb, px + 2, py - 1, true);
            duel_fb_px(fb, px - 1, py + 2, true);
            duel_fb_px(fb, px + 2, py + 2, true);
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
        duel_fb_px(fb, hx - radius, fy, true);
        duel_fb_px(fb, hx + radius, fy, true);
        duel_fb_px(fb, hx, fy - radius, true);
        duel_fb_px(fb, hx, fy + radius, true);
        duel_fb_line(fb, hx - 1, fy, hx + 1, fy);
        duel_fb_line(fb, hx, fy - 1, hx, fy + 1);
    } else if (r->flash_kind == FX_WARD_SHATTER_L || r->flash_kind == FX_WARD_SHATTER_R) {
        int ax = 16 + facing * 9;
        for (int i = 0; i < 4; i++) {
            int scatter = 2 + i * 2 + (8 - r->flash_frames) / 2;
            duel_fb_px(fb, ax + facing * scatter, fy - 6 + i * 4, true);
            duel_fb_px(fb, ax - facing * (scatter / 2), fy - 4 + i * 3, true);
        }
        duel_fb_line(fb, ax, fy - 7, ax - facing * 2, fy - 2);
        duel_fb_line(fb, ax - facing * 2, fy - 2, ax + facing, fy + 6);
    } else {
        // Redirection: the ward is the dominant thick shape while the
        // carrier breaks into two streaks thrown back toward the gap.
        int ax = 16 + facing * 9;
        int dist = 2 + (8 - r->flash_frames) / 2;
        duel_combat_draw_ward(fb, facing, 2, 2, false, fy);
        duel_fb_line(fb, ax + facing, fy, ax + facing * (dist + 2), fy - dist - tier);
        duel_fb_line(fb, ax + facing, fy, ax + facing * (dist + 1), fy + dist + tier);
        duel_fb_px(fb, ax - facing, fy - 5, true);
        duel_fb_px(fb, ax - facing, fy + 5, true);
        if (tier >= SPELL_TIER_LONG) {
            duel_fb_px(fb, ax + facing * (dist + 3), fy - 2, true);
            duel_fb_px(fb, ax + facing * (dist + 2), fy + 3, true);
        }
    }
}

/* Battlefield residue marks (M15 Track A): the duel's session-scale history
 * sits on the rooftop deck directly under the spell lanes, at each zone's
 * battlefield position. Each canvas shows its own two zones (the other two
 * live across the gap). Element picks the mark's shape, intensity its
 * density; every pattern is horizontally symmetric, so the desk-mirror
 * contract holds without per-side flips. Void residue is the exception that
 * proves the deck is real: it eats a hole in the deck rows instead of adding
 * pixels. Anchor u values keep the marks clear of the crenellation teeth
 * (x28-29 / x2-3) and mirror exactly (13<->242, 48<->207). Wards and local
 * fx draw later and may transiently overlap — combat happens on top of its
 * own history. */
void duel_overlay_draw_residue(duel_fb_t *fb, const duel_render_t *r, bool is_left) {
    static const uint8_t zone_anchor_u[SIM_RESIDUE_ZONES] = {13u, 48u, 207u, 242u};
    for (uint8_t zone = 0; zone < SIM_RESIDUE_ZONES; zone++) {
        uint8_t intensity = DUEL_RENDER_RESIDUE_INTENSITY(r, zone);
        if (!intensity)
            continue;
        int x;
        if (!duel_combat_battlefield_to_x(zone_anchor_u[zone], is_left, &x))
            continue;
        int base = DUEL_DECK_Y0 - 1;
        switch (DUEL_RENDER_RESIDUE_ELEMENT(r, zone)) {
            case ELEM_FORCE: /* rubble mound spreading, then heaping */
                duel_fb_px(fb, x, base, true);
                if (intensity >= 2) {
                    duel_fb_px(fb, x - 1, base, true);
                    duel_fb_px(fb, x + 1, base, true);
                }
                if (intensity >= 3)
                    duel_fb_px(fb, x, base - 1, true);
                break;
            case ELEM_EMBER: /* flame column rising, then a glowing bed */
                duel_fb_px(fb, x, base, true);
                duel_fb_px(fb, x, base - 1, true);
                if (intensity >= 2)
                    duel_fb_px(fb, x, base - 2, true);
                if (intensity >= 3) {
                    duel_fb_px(fb, x - 1, base, true);
                    duel_fb_px(fb, x + 1, base, true);
                }
                break;
            case ELEM_FROST: /* twin shards growing, then a centre spire */
                duel_fb_px(fb, x - 1, base, true);
                duel_fb_px(fb, x + 1, base, true);
                if (intensity >= 2) {
                    duel_fb_px(fb, x - 1, base - 1, true);
                    duel_fb_px(fb, x + 1, base - 1, true);
                }
                if (intensity >= 3)
                    duel_fb_px(fb, x, base - 2, true);
                break;
            default: /* ELEM_VOID: a pit widening, then biting the beam */
                duel_fb_px(fb, x, DUEL_DECK_Y0, false);
                if (intensity >= 2) {
                    duel_fb_px(fb, x - 1, DUEL_DECK_Y0, false);
                    duel_fb_px(fb, x + 1, DUEL_DECK_Y0, false);
                }
                if (intensity >= 3)
                    duel_fb_px(fb, x, DUEL_FLOOR_BEAM_Y, false);
                break;
        }
    }
}
