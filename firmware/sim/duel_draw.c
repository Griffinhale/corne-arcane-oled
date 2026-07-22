/* Full-scene composition order; subsystem drawing lives in sibling modules. */
#include "duel_draw_internal.h"
#include "duel_courier.h"
#include "duel_event.h"
#include "duel_host.h"
#include "duel_resident.h"

void duel_scene_draw(duel_fb_t *fb, const duel_render_t *r, bool is_left, uint32_t frame, bool debug_hud) {
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
    bool have_piercer = duel_combat_incoming_void_at_ward(&r->view, side, &piercer);
    bool ward_punctured = have_piercer ||
                          (local_impact && DUEL_KIND_ELEMENT(r->flash_spell_kind) == ELEM_VOID);
    int ward_lane = have_piercer ? duel_combat_spell_lane_y(piercer.kind) : duel_combat_spell_lane_y(r->flash_spell_kind);
    // The raised rooftop owns the upper band (the old archive underlay is
    // retired); the archival occupation lives in the tower floor below, where
    // the courier (Wave 6) and rare event (Wave 7) layer in as well.
    duel_environment_draw_sky(fb, r, is_left);
    duel_environment_draw_tower(fb, r, is_left);
    duel_environment_draw_floor(fb, r, is_left);
    duel_overlay_draw_residue(fb, r, is_left);
    if (!(r->revision & INCANTATION_AFTERMATH_WIRE) &&
        DUEL_CIVIC_FLOOR(r->civic) != DUEL_CIVIC_FLOOR_SPECIAL) {
        draw_courier(fb, r, is_left);
        draw_rare_event(fb, r, is_left);
    }

    // Lifecycle (M5): each phase has its own tableau, derived purely from
    // (life, life_ticks, variant) so master and slave render identically.
    // Sparks and the shield arc only apply to a standing, active wizard.
    switch (wz->life) {
        case LIFE_ACTIVE: {
            // Track B calm stances: MEDITATE/STUDY restage the wizard on the
            // balcony and leave the deck empty. MEDITATE's ward is already
            // presented as 0 by the packer; STUDY's stored ward keeps
            // guarding the vacated deck below.
            if (wz->stance == DUEL_STANCE_MEDITATE || wz->stance == DUEL_STANCE_STUDY) {
                duel_combat_draw_stance_balcony(fb, is_left,
                    wz->stance == DUEL_STANCE_MEDITATE ? DUEL_BALCONY_MEDITATE
                                                      : DUEL_BALCONY_STUDY,
                    frame);
                if (wz->ward_strength)
                    duel_combat_draw_ward(fb, facing, wz->ward_strength, wz->ward_focus,
                              ward_punctured, ward_lane);
                break;
            }
            // Forced-commit "civic-scale" big cast: rearm_lock spans the windup
            // and the prepared hold (set on forced commit, cleared only on key
            // release), so the wizard ascends to the balcony and the whole tower
            // lights for the duration. rearm_lock flips false at launch (state ->
            // INC_REARM), dropping the wizard back onto an empty deck exactly on
            // cast. A normal prepared cast keeps rearm_lock == 0 and never climbs.
            if (wz->rearm_lock &&
                (wz->inc_state == INC_WINDUP || wz->inc_state == INC_PREPARED)) {
                duel_combat_draw_stance_balcony(fb, is_left, DUEL_BALCONY_BIGCAST, frame);
                // Blinking halo re-centred on the balcony figure, motes rising
                // past the shaft, and a peak flare — the civic-scale lighting
                // that replaced the retired WORLD_WONDER ripple, now following
                // the figure up the tower.
                int hcx = duel_fb_desk_x(is_left, 13);
                static const int8_t halo[5][2] = {
                    {-4, -6}, {4, -8}, {5, 2}, {-5, 4}, {4, 7}};
                for (int i = 0; i < 5; i++)
                    if ((((frame >> 1) + (uint32_t)i) & 1u) == 0u)
                        duel_fb_px(fb, hcx + halo[i][0], 24 + halo[i][1], true);
                int shaft_lip = is_left ? 13 : 18;
                for (int i = 0; i < 3; i++) {
                    int my = 56 - (int)((frame * 2u + (uint32_t)i * 12u) % 36u);
                    duel_fb_px(fb, shaft_lip, my, true);
                }
                int peak_x = is_left ? 6 : 25;
                duel_fb_px(fb, peak_x - 2, 2, true);
                duel_fb_px(fb, peak_x + 2, 2, true);
                duel_fb_px(fb, peak_x, 0, true);
                if (wz->ward_strength)
                    duel_combat_draw_ward(fb, facing, wz->ward_strength, wz->ward_focus,
                              ward_punctured, ward_lane);
                break;
            }
            // PACE/TAUNT never ride the wire: a calm idle wizard (stance
            // NONE, nothing brewing, no own carrier in flight) alternates
            // between a slow deck shuffle and a defiant staff flourish,
            // phased by the session seed.
            bool calm = wz->stance == DUEL_STANCE_NONE && wz->inc_state == INC_IDLE &&
                        wz->pose == POSE_IDLE && !local_fx &&
                        !duel_view_spell(&r->view, (uint8_t)side).active;
            int idle_xo = 0;
            bool taunt = false;
            if (calm) {
                if ((((r->seed >> 2) ^ (frame >> 7)) & 1u) != 0u) {
                    taunt = ((frame >> 4) & 3u) == 0u;
                } else {
                    static const int8_t shuffle[8] = { 0, 1, 1, 0, 0, -1, -1, 0 };
                    idle_xo = shuffle[(frame >> 3) & 7u];
                }
            }
            // A damaging hit pushes the defender away from the gap and briefly
            // compresses the silhouette. Deflect/fizzle leave it rock steady.
            duel_combat_draw_wizard(fb, wz->pose == POSE_CAST || taunt, facing, wz->variant,
                     local_impact ? -facing * (r->flash_frames >= 8 ? 2 : 1) : idle_xo,
                     local_impact && r->flash_frames >= 8 ? 1 : 0);
            if (taunt) {
                // Defiant sparks flicking off the raised orb.
                duel_fb_px(fb, 16 + facing * 7, 36, true);
                duel_fb_px(fb, 16 + facing * 6, 34, true);
            }
            if (wz->stance == DUEL_STANCE_FORTIFY) {
                // Braced on deck: heels dug in beside the hem, and the staff
                // orb pulsing while the ward charge builds.
                duel_fb_px(fb, 16 - facing * 4, 59, true);
                duel_fb_px(fb, 16 + facing * 4, 59, true);
                if (((frame >> 2) & 1u) == 0u) {
                    int sx = 16 + facing * 5;
                    duel_fb_px(fb, sx, 39, true);
                    duel_fb_px(fb, sx + facing, 39, true);
                }
            }

            if (wz->pose == POSE_RECOVER && !local_impact) {
                // Fading sparks above the hat make RECOVER observable on hardware.
                duel_fb_px(fb, 14, 50 + DUEL_ROOF_DY, true);
                duel_fb_px(fb, 18, 51 + DUEL_ROOF_DY, true);
            }

            // Shield: a vertical ward arc on the gap side of this half's wizard.
            if (wz->ward_strength) {
                duel_combat_draw_ward(fb, facing,
                          wz->ward_strength,
                          wz->ward_focus,
                          ward_punctured, ward_lane);
            }
            duel_combat_draw_charge(fb, wz, facing, frame);
            if (wz->inc_state == INC_COLLECTING) {
                // Collection runes ride between the balcony and the hat —
                // their old rows (y30-31) are the balcony slab now.
                int runes = 1 + r->view.phase[side] / 64;
                for (int i = 0; i < runes; i++) {
                    int rx = 12 + i * 3;
                    duel_fb_px(fb, rx, 35, true);
                    duel_fb_px(fb, rx + 1, 34, true);
                }
            } else if (wz->prepared) {
                int px = 16 + facing * 2, py = 43 + DUEL_ROOF_DY;
                duel_fb_px(fb, px, py - 2, true); duel_fb_px(fb, px, py + 2, true);
                duel_fb_px(fb, px - 2, py, true); duel_fb_px(fb, px + 2, py, true);
            }
            duel_combat_draw_status(fb, wz, facing, frame);
            break;
        }

        case LIFE_COLLAPSE: {
            // Sink the standing figure for the first two thirds, then flat.
            int elapsed = SIM_COLLAPSE_TICKS - wz->life_ticks;
            if (wz->life_ticks > 4) {
                duel_combat_draw_wizard(fb, false, facing, wz->variant, 0, elapsed / 3); // yo 0..2
            } else {
                duel_combat_draw_downed(fb, facing, wz->variant, 0);
            }
            break;
        }

        case LIFE_DOWNED:
            duel_combat_draw_downed(fb, facing, wz->variant, 0);
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
            duel_combat_draw_downed(fb, facing, wz->variant, -facing * dx);
            duel_combat_draw_medic(fb, 16 - facing * (12 + dx), facing); // ~7 px past the head
            break;
        }

        case LIFE_REPLACE: {
            // The next roster variant (sim already bumped wz->variant) walks
            // in from the away-from-gap edge with a 1-px render-frame bob.
            int elapsed = SIM_REPLACE_TICKS - wz->life_ticks;
            int xo      = 16 - elapsed * 16 / SIM_REPLACE_TICKS; // 16 -> 0
            duel_combat_draw_wizard(fb, false, facing, wz->variant, -facing * xo, (int)((frame >> 1) & 1));
            break;
        }
    }

    duel_combat_draw_reaction(fb, r->flash_kind, is_left, r->flash_frames);

    // HP shaft windows for THIS half's wizard.
    duel_overlay_draw_health(fb, wz, is_left);

    // Spells in flight, wherever the battlefield axis lands on this canvas.
    // Drawn AFTER the HP windows so low/ground-lane carriers (which cross the
    // window band) win the contested pixels instead of being cleared-then-
    // overwritten by the 2x2 cells; the trade-off — a low spell over its own
    // HP columns briefly overpaints them — matches duel_overlay_draw_local_fx, also post-HP.
    for (int s = 0; s < 2; s++) {
        duel_view_spell_t spell = duel_view_spell(&r->view, (uint8_t)s);
        if (!spell.active) continue;
        duel_view_wizard_t caster = duel_view_wizard(&r->view, (uint8_t)s);
        duel_combat_draw_spell(fb, &spell, (uint8_t)s, caster.variant, is_left, frame);
    }

    // One-shot outcomes use three deliberately different grammars.
    if (local_fx) duel_overlay_draw_local_fx(fb, r, wz, facing, is_left);

    // normalized alert sigil sits above all scene/combat artwork, but an open scry
    // replaces it with the normalized in-panel summary.
    if (!duel_view_scry_open(&r->view)) duel_overlay_draw_alert(fb, r, is_left);

    if (!duel_view_scry_open(&r->view)) duel_overlay_draw_attunement(fb, r, wz, is_left);

    // scry scrying overlay, drawn above the world when the layer-key chord is
    // held. The stale-link and debug glyphs draw AFTER, so a broken link is
    // still legible in its corner even with the panel up.
    if (duel_view_scry_open(&r->view)) {
        duel_overlay_draw_scry(fb, r, is_left);
    }

    if (r->flags & DUEL_RENDER_STALE) {
        // Two separated chain links in the top corner nearest the gap. The
        // celestial arc (and, under scry, the relocated alert summary) can
        // occupy these cells, so the link-loss indicator clears its field
        // first — it must stay legible over everything.
        int bx = is_left ? 23 : 2;
        for (int y = 1; y <= 9; y++)
            for (int x = bx - 1; x <= bx + 7; x++)
                duel_fb_px(fb, x, y, false);
        duel_overlay_draw_box3(fb, bx, 2);
        duel_overlay_draw_box3(fb, bx + 4, 6);
    }

    if (debug_hud) {
        // Diagnostics-only sync heartbeat: a 1 Hz pulse on this half's tower-top
        // tip (astral finial / mechanical beacon), replacing the old bottom-row
        // edge sweep. diag_tick runs 0..24 each second, so lighting its first
        // half reads as one blink per second on the spire. Release builds leave
        // debug_hud false, so this is absent from the shipped image.
        if (r->diag_tick < 13) {
            int peak_x = is_left ? 6 : 25;
            duel_fb_px(fb, peak_x, 0, true);
            duel_fb_px(fb, peak_x, 1, true);
        }
        int dots = r->diag_overflow > 4 ? 4 : r->diag_overflow;
        for (int i = 0; i < dots; i++) duel_fb_px(fb, 1 + 2 * i, 0, true);
    }
}
