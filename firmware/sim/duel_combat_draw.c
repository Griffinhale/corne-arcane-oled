#include "duel_draw_internal.h"
#include "duel_host.h"

void duel_combat_draw_wizard(duel_fb_t *fb, bool casting, int facing, uint8_t variant, int xo, int yo) {
    const int cx = DUEL_CANVAS_W / 2 + xo;   // 16 + xo

    // Chunky hat: filled 3-row triangle over a wide brim, tip bent gapward.
    const int brim_y = 43 + yo;
    for (int y = brim_y - 3; y < brim_y; y++)
        duel_fb_hline(fb, cx - (y - brim_y + 4), cx + (y - brim_y + 4), y); // hw 1..3
    duel_fb_hline(fb, cx - 4, cx + 4, brim_y);
    duel_fb_px(fb, cx + facing, brim_y - 4, true);

    // Head under the brim (the M14 figure was hat-on-robe with no face).
    duel_fb_px(fb, cx - 1, 44 + yo, true); duel_fb_px(fb, cx, 44 + yo, true);
    duel_fb_px(fb, cx - 1, 45 + yo, true); duel_fb_px(fb, cx, 45 + yo, true);

    // Collar, then a solid robe: narrow shoulders flaring at the hem, with a
    // gap-side arm reaching for the staff. The hem's half-width 3 keeps the
    // figure clear of the tower shaft flare at x12 in every lifecycle offset.
    duel_fb_hline(fb, cx - 2, cx + 2, 46 + yo);
    const int robe_top = 47 + yo;
    const int robe_bot = 58 + yo;
    for (int y = robe_top; y <= robe_bot; y++) {
        int i = y - robe_top;
        int hw = i < 6 ? 1 : i < 10 ? 2 : 3;
        duel_fb_hline(fb, cx - hw, cx + hw, y);
    }
    duel_fb_px(fb, cx + facing * 3, 47 + yo, true);
    duel_fb_px(fb, cx + facing * 2, 48 + yo, true);

    // Roster variant masks (M5): pose-invariant hat/robe markings only, so a
    // replacement is recognisably a new combatant in every pose.
    switch (variant & 3) {
        case 1: // hat band: a cleared 1-px stripe across the widest hat row
            for (int x = cx - 2; x <= cx + 2; x++) duel_fb_px(fb, x, brim_y - 1, false);
            break;
        case 2: // robe hem fringe: 3 dots one row under the robe bottom
            duel_fb_px(fb, cx - 3, robe_bot + 1, true);
            duel_fb_px(fb, cx, robe_bot + 1, true);
            duel_fb_px(fb, cx + 3, robe_bot + 1, true);
            break;
        case 3: // pompom riding the bent tip
            duel_fb_px(fb, cx + facing, brim_y - 5, true);
            duel_fb_px(fb, cx + facing, brim_y - 6, true);
            break;
    }

    // Staff along the facing side, just outside the robe.
    if (!casting) {
        // Resting: a 2-px staff planted by the feet with a plus-shaped orb
        // finial (the launch column at battlefield u=0 leaves from its top).
        int sx = cx + facing * 5;
        for (int y = 44 + yo; y <= robe_bot; y++) {
            duel_fb_px(fb, sx, y, true);
            duel_fb_px(fb, sx + facing, y, true);
        }
        duel_fb_px(fb, sx + facing, 43 + yo, true);        // neck
        duel_fb_px(fb, sx + facing, 42 + yo, true);
        duel_fb_px(fb, sx, 41 + yo, true);                 // orb
        duel_fb_px(fb, sx + facing, 41 + yo, true);
        duel_fb_px(fb, sx + facing * 2, 41 + yo, true);
        duel_fb_px(fb, sx + facing, 40 + yo, true);
    } else {
        // Casting: 2-px staff raised toward the gap; tip focus keeps the M6
        // launch coordinates so departing carriers still leave from the orb.
        // The progressive scry.5 charge is drawn separately from
        // authoritative wind-up state in duel_scene_draw.
        duel_fb_line(fb, cx + facing * 2, 51 + yo, cx + facing * 5, 39 + yo);
        duel_fb_line(fb, cx + facing * 2, 52 + yo, cx + facing * 5, 40 + yo);
        duel_fb_px(fb, cx + facing * 5, 38 + yo, true); // staff-tip focus
        duel_fb_px(fb, cx + facing * 6, 38 + yo, true);
    }
}

// M5 fallen wizard: horizontal body with the head AWAY from the gap (the
// medic later drags it toward that edge), hat knocked off past the head,
// staff dropped on the ground toward the gap. -facing is the away direction
// on both halves (left: gap at x=31, facing +1; right: gap at x=0, facing -1).
void duel_combat_draw_downed(duel_fb_t *fb, int facing, uint8_t variant, int xo) {
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
void duel_combat_draw_medic(duel_fb_t *fb, int x, int facing) {
    duel_fb_px(fb, x, 66 + DUEL_ROOF_DY, true); duel_fb_px(fb, x + 1, 66 + DUEL_ROOF_DY, true);
    duel_fb_px(fb, x, 67 + DUEL_ROOF_DY, true); duel_fb_px(fb, x + 1, 67 + DUEL_ROOF_DY, true);
    for (int y = 68 + DUEL_ROOF_DY; y <= 70 + DUEL_ROOF_DY; y++) duel_fb_px(fb, x, y, true);
    duel_fb_px(fb, x - facing, 71 + DUEL_ROOF_DY, true);
    duel_fb_px(fb, x - facing, 72 + DUEL_ROOF_DY, true);
    duel_fb_px(fb, x - facing - 1, 73 + DUEL_ROOF_DY, true);
    duel_fb_px(fb, x - facing + 1, 73 + DUEL_ROOF_DY, true);
}

/* Balcony postures. MEDITATE/STUDY are the Track B calm stances; BIGCAST is
 * the forced-commit ascent (Change 3), authored from the STUDY standing body
 * with the staff thrust up toward the peak. */
/* Balcony restaging: the wizard climbs onto the tower balcony (slab desk
 * x11-16 at y30-31 — the restage point authored into duel_environment_draw_tower).
 * Balcony art is architecture-side, so it is authored in desk space and
 * mirrors with the tower; FORTIFY and PACE/TAUNT stay with the unmirrored
 * combat cluster on the deck and never come here. */
void duel_combat_draw_stance_balcony(duel_fb_t *fb, bool is_left, uint8_t posture,
                                uint32_t frame) {
#define BX(x) duel_fb_desk_x(is_left, x)
    if (posture == DUEL_BALCONY_BIGCAST) {
        // Forced-commit big cast: the STUDY standing body with both arms flung
        // overhead and the staff thrust up the shaft toward the finial.
        duel_fb_px(fb, BX(13), 19, true);                    // hat crown
        duel_fb_desk_hline(fb, is_left, 12, 15, 20);    // hat brim
        duel_fb_px(fb, BX(13), 21, true); duel_fb_px(fb, BX(14), 21, true); // head
        duel_fb_px(fb, BX(13), 22, true); duel_fb_px(fb, BX(14), 22, true);
        for (int y = 23; y <= 28; y++) {                     // robe
            duel_fb_px(fb, BX(13), y, true);
            duel_fb_px(fb, BX(14), y, true);
        }
        duel_fb_desk_hline(fb, is_left, 12, 15, 29);    // hem
        // Both arms raised toward the peak.
        duel_fb_px(fb, BX(12), 21, true); duel_fb_px(fb, BX(15), 21, true);
        duel_fb_px(fb, BX(12), 20, true); duel_fb_px(fb, BX(15), 20, true);
        // Staff thrust up the gap-side of the shaft toward the finial, tip
        // sparking (render-frame cosmetic).
        for (int y = 14; y <= 20; y++) duel_fb_px(fb, BX(12), y, true);
        duel_fb_px(fb, BX(11), 15, true); duel_fb_px(fb, BX(13), 15, true); // orb head
        duel_fb_px(fb, BX(12), 12 - (int)((frame >> 3) & 1u), true);        // spark
    } else if (posture == DUEL_BALCONY_MEDITATE) {
        // Seated figure folded onto the slab: hat, head, robe, crossed lap.
        duel_fb_px(fb, BX(13), 21, true);
        duel_fb_desk_hline(fb, is_left, 12, 15, 22);
        duel_fb_px(fb, BX(13), 23, true); duel_fb_px(fb, BX(14), 23, true);
        duel_fb_desk_hline(fb, is_left, 13, 14, 24);
        for (int y = 25; y <= 27; y++)
            duel_fb_desk_hline(fb, is_left, 12, 15, y);
        duel_fb_desk_hline(fb, is_left, 11, 16, 28);
        duel_fb_desk_hline(fb, is_left, 11, 16, 29);
        // Slow motes circling the crown (render-frame cosmetic).
        uint8_t beat = (uint8_t)((frame >> 3) & 3u);
        duel_fb_px(fb, BX(11), 19 - (beat & 1u), true);
        duel_fb_px(fb, BX(16), 18 + (beat >> 1), true);
    } else {
        // Standing at the gap-side rail, nose in an open tome.
        duel_fb_px(fb, BX(12), 19, true);
        duel_fb_desk_hline(fb, is_left, 11, 14, 20);
        duel_fb_px(fb, BX(12), 21, true); duel_fb_px(fb, BX(13), 21, true);
        duel_fb_px(fb, BX(12), 22, true); duel_fb_px(fb, BX(13), 22, true);
        for (int y = 23; y <= 28; y++) {
            duel_fb_px(fb, BX(12), y, true);
            duel_fb_px(fb, BX(13), y, true);
        }
        duel_fb_desk_hline(fb, is_left, 11, 14, 29);
        // The tome, pages peaked at the spine, held over the rail.
        duel_fb_desk_hline(fb, is_left, 14, 16, 26);
        duel_fb_desk_hline(fb, is_left, 14, 16, 27);
        duel_fb_px(fb, BX(15), 25, true);
        // A study rune drifts up off the page (render-frame cosmetic).
        duel_fb_px(fb, BX(15 + (int)((frame >> 4) & 1u)),
                   23 - (int)((frame >> 3) & 1u), true);
    }
#undef BX
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

bool duel_combat_battlefield_to_x(uint8_t u, bool is_left, int *x) {
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

int duel_combat_spell_lane_y(uint8_t kind) {
    switch (DUEL_KIND_ELEMENT(kind)) {
        case ELEM_FROST: return SPELL_Y_BASE - 5;
        case ELEM_VOID:  return SPELL_Y_BASE - 2;
        case ELEM_EMBER: return SPELL_Y_BASE + 3;
        default:         return SPELL_Y_BASE;
    }
}

static void spell_glyph(duel_fb_t *fb, int x, int y, uint8_t kind, int dir, bool lift) {
    int back    = dir > 0 ? -1 : +1;
    int tier    = DUEL_KIND_TIER(kind);

    // Low/ground-lane carriers ride through the HP-window band. Lift the
    // compact SHORT-tier glyphs one presentation tier (the same idiom
    // duel_overlay_draw_local_fx uses) so the silhouette carries enough mass to read over
    // the windows redrawing beneath it.
    if (lift && tier < SPELL_TIER_SATURATED) tier++;

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
            // M15 weight pass: a solid 3x3 core so the flake registers at desk
            // distance; cross arms and diagonal spikes keep the star identity.
            if (tier == SPELL_TIER_SHORT) {
                for (int d = -1; d <= 1; d++) {
                    duel_fb_px(fb, x + d, y, true);
                    duel_fb_px(fb, x, y + d, true);
                }
                duel_fb_px(fb, x - 1, y - 1, true); duel_fb_px(fb, x + 1, y - 1, true);
                duel_fb_px(fb, x - 1, y + 1, true); duel_fb_px(fb, x + 1, y + 1, true);
                break;
            }
            for (int dx = -1; dx <= 1; dx++)
                for (int dy = -1; dy <= 1; dy++) duel_fb_px(fb, x + dx, y + dy, true);
            int arm = tier >= SPELL_TIER_LONG ? 3 : 2;
            for (int d = 2; d <= arm; d++) {
                duel_fb_px(fb, x - d, y, true); duel_fb_px(fb, x + d, y, true);
                duel_fb_px(fb, x, y - d, true); duel_fb_px(fb, x, y + d, true);
            }
            duel_fb_px(fb, x - 2, y - 2, true); duel_fb_px(fb, x + 2, y - 2, true);
            duel_fb_px(fb, x - 2, y + 2, true); duel_fb_px(fb, x + 2, y + 2, true);
            if (tier == SPELL_TIER_SATURATED) {
                duel_fb_px(fb, x - 3, y - 3, true); duel_fb_px(fb, x + 3, y - 3, true);
                duel_fb_px(fb, x - 3, y + 3, true); duel_fb_px(fb, x + 3, y + 3, true);
            }
            break;
        }
        case ELEM_VOID: {
            // M15 weight pass: a solid ring (donut) instead of a 1-px outline;
            // the dark centre stays the void signature at every tier.
            if (tier == SPELL_TIER_SHORT) {
                for (int dx = -1; dx <= 1; dx++) {
                    duel_fb_px(fb, x + dx, y - 1, true);
                    duel_fb_px(fb, x + dx, y + 1, true);
                }
                duel_fb_px(fb, x - 1, y, true); duel_fb_px(fb, x + 1, y, true);
                duel_fb_px(fb, x, y - 1, false); // diamond-like, hollow core
                break;
            }
            for (int dx = -1; dx <= 1; dx++)
                for (int dy = -1; dy <= 1; dy++)
                    duel_fb_px(fb, x + dx, y + dy, dx || dy);
            if (tier == SPELL_TIER_MEDIUM) {
                duel_fb_px(fb, x - 2, y, true); duel_fb_px(fb, x + 2, y, true);
                duel_fb_px(fb, x, y - 2, true); duel_fb_px(fb, x, y + 2, true);
            } else {
                int rx = 2 + (tier == SPELL_TIER_SATURATED);
                for (int dx = -rx; dx <= rx; dx++) {
                    duel_fb_px(fb, x + dx, y - 2, true);
                    duel_fb_px(fb, x + dx, y + 2, true);
                }
                duel_fb_px(fb, x - rx, y - 1, true); duel_fb_px(fb, x + rx, y - 1, true);
                duel_fb_px(fb, x - rx, y, true);     duel_fb_px(fb, x + rx, y, true);
                duel_fb_px(fb, x - rx, y + 1, true); duel_fb_px(fb, x + rx, y + 1, true);
            }
            break;
        }
        case ELEM_EMBER: {
            // M15 weight pass: a solid teardrop head (back corners clipped so
            // the mass points forward) with a 2-row flame tail near the head.
            if (tier == SPELL_TIER_SHORT) {
                for (int d = -1; d <= 1; d++) {
                    duel_fb_px(fb, x + d, y, true);
                    duel_fb_px(fb, x, y + d, true);
                }
                duel_fb_px(fb, x + 2 * back, y - 1, true);
                break;
            }
            for (int dx = -1; dx <= 1; dx++)
                for (int dy = -1; dy <= 1; dy++)
                    if (!(dx == back && dy)) duel_fb_px(fb, x + dx, y + dy, true);
            duel_fb_px(fb, x - 2 * back, y, true); // nose
            int tail = 2 + tier * 2;
            for (int d = 2; d <= tail; d++) duel_fb_px(fb, x + d * back, y - (d & 1), true);
            duel_fb_px(fb, x + 2 * back, y, true);
            duel_fb_px(fb, x + 3 * back, y, true);
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

void duel_combat_draw_spell(duel_fb_t *fb, const duel_view_spell_t *spell,
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
    // Carriers in the low/ground lanes get a one-tier glyph lift (see
    // spell_glyph) so they clear the HP windows they overfly.
    bool low_lane = SPELL_DESC_TRAJECTORY(spell->descriptor) == TRAJ_LOW ||
                    SPELL_DESC_TRAJECTORY(spell->descriptor) == TRAJ_GROUND;
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
            duel_fb_line(fb, x0, yb, x1, yb);
        }
        if (full) duel_fb_line(fb, x0, yb + 1, x1, yb + 1);
        if (full && SPELL_DESC_MAGNITUDE(spell->descriptor) >= 3u)
            duel_fb_line(fb, x0, yb - 1, x1, yb - 1);
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
            duel_fb_line(fb, px, py, nx, ny); px = nx; py = ny;
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
        if (!duel_combat_battlefield_to_x(u, is_left, &x)) return;
        spell_glyph(fb, x, y, spell->kind, travel_dir, low_lane);
        return;
    }

    if (form == SPELL_CONJURE) {
        bool trap = SPELL_DESC_TRAJECTORY(spell->descriptor) == TRAJ_GROUND ||
                    SPELL_DESC_TRAJECTORY(spell->descriptor) == TRAJ_AREA;
        uint8_t charges = progress >> 5;
        if (trap) {
            if (!duel_combat_battlefield_to_x(u, is_left, &x)) return;
            y = SPELL_Y_BASE + 11;
            duel_fb_line(fb, x - 3, y, x + 3, y);
            duel_fb_px(fb, x - 2, y - 1, true); duel_fb_px(fb, x + 2, y - 1, true);
            if ((frame & 3u) == 0u) duel_fb_px(fb, x, y - 3, true);
        } else {
            if (caster_local)
                draw_orbiting_motes(fb, 1u + (charges > 2u), local_cx - facing * 5,
                                    SPELL_Y_BASE - 8, frame, 2u, facing);
            if (phase < 10u || !charges) return;
            if (!duel_combat_battlefield_to_x(u, is_left, &x)) return;
            duel_fb_px(fb, x, y, true); duel_fb_px(fb, x - travel_dir, y - 1, true);
        }
        return;
    }
    if (!duel_combat_battlefield_to_x(u, is_left, &x)) return;

    if (form == SPELL_FIREBALL) {
        spell_glyph(fb, x, y, spell->kind, travel_dir, low_lane);
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
        spell_glyph(fb, x, y, spell->kind, travel_dir, low_lane);
    }

    // Fading trail (M15 weight pass): a solid 2-row stub hugs the head, then
    // tempo-counted dots thin out behind it — the length grammar is unchanged.
    uint8_t trail = SPELL_DESC_TEMPO(spell->descriptor);
    int back = caster_side == SIM_SIDE_L ? -1 : 1;
    if (trail) {
        duel_fb_px(fb, x + back * 2, y, true);
        duel_fb_px(fb, x + back * 3, y, true);
        duel_fb_px(fb, x + back * 3, y - 1, true);
    }
    for (uint8_t i = 1; i < trail; i++)
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

void duel_combat_draw_status(duel_fb_t *fb, const duel_view_wizard_t *wz,
                            int facing, uint32_t frame) {
    if (!wz->status || !wz->status_intensity) return;
    int cx = 16 - facing * 5;
    int cy = 55 + DUEL_ROOF_DY;
    int phase = (int)(frame & 3u);
    // The glyph anchor sits on the tower shaft's inner edge column (x11 on
    // the left half, one px off it on the right), which would swallow most
    // of its pixels asymmetrically. Clear a small backdrop first so the
    // status reads identically over the wall on both halves.
    for (int y = cy - 6; y <= cy + 2; y++)
        for (int x = cx - 3; x <= cx + 3; x++)
            duel_fb_px(fb, x, y, false);
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

void duel_combat_draw_reaction(duel_fb_t *fb, uint8_t outcome, bool is_left,
                              uint8_t frames) {
    if (!frames || outcome < FX_HEAL_L || outcome > FX_COLLAPSE) return;
    int x = is_left ? 5 : DUEL_CANVAS_W - 1 - 5;
    int y = 101;
    if (outcome == FX_HEAL_L || outcome == FX_HEAL_R) { /* civic cheer/confetti */
        duel_fb_px(fb, x - 2, y - 2, true); duel_fb_px(fb, x + 2, y - 2, true);
        duel_fb_line(fb, x - 1, y, x + 1, y);
    } else if (outcome == FX_COMPLAINT) {
        duel_fb_line(fb, x - 2, y, x + 1, y);
        duel_fb_px(fb, x + 2, y - 1, true);
    } else if (outcome == FX_DETONATE) { /* roof explosion */
        // M15 weight pass: one tier bigger — longer rays, a doubled base
        // line, and a smoke puff above the burst crown.
        x = is_left ? 27 : 4; y = SPELL_Y_BASE + 14;
        for (int d = 1; d <= 6; d++) {
            duel_fb_px(fb, x - d, y - d, true); duel_fb_px(fb, x + d, y - d, true);
            duel_fb_px(fb, x - d, y + (d & 1), true); duel_fb_px(fb, x + d, y + (d & 1), true);
        }
        duel_fb_line(fb, x - 7, y, x + 7, y);
        duel_fb_line(fb, x - 4, y + 1, x + 4, y + 1);
        duel_fb_px(fb, x - 1, y - 7, true); duel_fb_px(fb, x + 1, y - 7, true);
        duel_fb_px(fb, x, y - 8, true);
    } else if (outcome == FX_RESIDUE) {
        duel_fb_px(fb, x - 2, y, true); duel_fb_px(fb, x + 2, y, true);
        duel_fb_px(fb, x, y - 2, true); duel_fb_px(fb, x, y + 2, true);
    } else if (outcome == FX_COMBINE) {
        duel_fb_line(fb, x - 2, y - 2, x + 2, y - 2);
        duel_fb_px(fb, x - 2, y - 1, true); duel_fb_px(fb, x + 2, y - 1, true);
        duel_fb_px(fb, x, y, true);
    } else { /* singularity collapse */
        duel_fb_px(fb, x, y, true);
        duel_fb_px(fb, x - 2, y - 2, true); duel_fb_px(fb, x + 2, y + 2, true);
    }
}

// Progressive upper-canvas anticipation. Growth comes from authoritative
// wind-up/tier state; only the tiny orbiting accents key off the render frame.
void duel_combat_draw_charge(duel_fb_t *fb, const duel_view_wizard_t *wz, int facing, uint32_t frame) {
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
        duel_fb_line(fb, cx - radius - 1, cy + radius + 2, cx + radius + 1, cy + radius + 2);
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

void duel_combat_draw_ward(duel_fb_t *fb, int facing, int strength, int focus,
                      bool punctured, int puncture_y) {
    int ax = 16 + facing * 9;
    int focus_y = focus == 0 ? SPELL_Y_BASE + 10 : focus == 1 ? SPELL_Y_BASE + 5 :
                  focus == 3 ? SPELL_Y_BASE - 7 : SPELL_Y_BASE;
    int reach = 3 + strength * 3;
    int y0 = focus_y - reach, y1 = focus_y + reach;
    int cy = focus_y;
    if (strength >= 4) {
        y0 = SPELL_Y_BASE - 18; y1 = SPELL_Y_BASE + 12;
        cy = (y0 + y1) / 2; reach = (y1 - y0) / 2;
    }
    // M15 weight pass: a continuous parabolic arc bulging toward the gap,
    // 2-3 px thick by strength, replaces the M6 straight dotted bars. The
    // strength/focus/puncture grammar is unchanged. Low-focus arcs used to
    // overhang into the room below the beam; now that the deck is explicit
    // architecture, the ward terminates on it instead of piercing it.
    int bulge = 2 + (strength >= 2) + (strength >= 4); // 2..4
    int thick = strength >= 3 ? 3 : 2;
    for (int y = y0; y <= y1 && y < DUEL_DECK_Y0; y++) {
        int dy = y - cy;
        int off = bulge * (reach * reach - dy * dy) / (reach * reach);
        int d = y - puncture_y;
        if (d < 0) d = -d;
        if (punctured && d <= 2) continue;
        for (int t = 0; t < thick; t++)
            duel_fb_px(fb, ax + facing * (off - t), y, true);
    }
    // Anchor flares at both ends and a focus notch at the apex row, marking
    // the lane the ward is concentrated on. A deck-clipped arc plants its
    // lower end in the deck and needs no flare there.
    duel_fb_px(fb, ax - facing, y0 - 1, true);
    duel_fb_px(fb, ax - facing * 2, y0 - 2, true);
    if (y1 + 1 < DUEL_DECK_Y0) duel_fb_px(fb, ax - facing, y1 + 1, true);
    if (y1 + 2 < DUEL_DECK_Y0) duel_fb_px(fb, ax - facing * 2, y1 + 2, true);
    int notch_d = focus_y - puncture_y;
    if (notch_d < 0) notch_d = -notch_d;
    if (!punctured || notch_d > 2) {
        duel_fb_px(fb, ax + facing * (bulge + 1), focus_y - 1, true);
        duel_fb_px(fb, ax + facing * (bulge + 1), focus_y, true);
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

bool duel_combat_incoming_void_at_ward(const duel_view_t *view, int defender,
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
