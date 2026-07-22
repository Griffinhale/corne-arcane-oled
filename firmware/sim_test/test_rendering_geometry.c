#include "test_harness.h"

static void test_floor_occupations_and_transitions(void) {
    duel_fb_t floor[2][DUEL_DISTRICT_COUNT];
    bool ok = true;
    for (uint8_t city = 0; city < 2u; city++)
        for (uint8_t occupation = 0; occupation < DUEL_DISTRICT_COUNT; occupation++)
            render_district_scene(occupation, city == 0u, 0u, 0u, &floor[city][occupation]);
    for (uint8_t city = 0; city < 2u; city++)
        for (uint8_t a = 0; a < DUEL_DISTRICT_COUNT; a++)
            for (uint8_t b = (uint8_t)(a + 1u); b < DUEL_DISTRICT_COUNT; b++) {
                unsigned diff =
                    band_difference(&floor[city][a], &floor[city][b], DUEL_FLOOR_Y0, DUEL_FLOOR_Y1);
                if (diff < 24u)
                    printf("DIAG floor city=%u pair=%u/%u diff=%u\n", city, a, b, diff);
                EXPECT(diff >= 24u);
            }

    for (uint8_t phase = 0; phase < 4u; phase++) {
        duel_fb_t transitioned;
        uint8_t byte = INCANTATION_FLOOR_TRANSITION_PACK(DUEL_DISTRICT_COMMONS, phase, true);
        render_district_scene(DUEL_DISTRICT_WORKSHOP, true, 0u, byte, &transitioned);
        const duel_fb_t *reference =
            phase < 2u ? &floor[0][DUEL_DISTRICT_COMMONS] : &floor[0][DUEL_DISTRICT_WORKSHOP];
        EXPECT(band_difference(&transitioned, reference, DUEL_FLOOR_Y0, DUEL_FLOOR_Y1) > 0u);
        /* Protection includes the beam row itself. */
        EXPECT(band_difference(&transitioned, reference, 0, DUEL_FLOOR_BEAM_Y) == 0u);
        EXPECT(band_difference(&transitioned, reference, DUEL_FLOOR_Y1 + 1, DUEL_CANVAS_H - 1) ==
               0u);
        EXPECT(INCANTATION_FLOOR_TRANSITION_SOURCE(byte) == DUEL_DISTRICT_COMMONS &&
               INCANTATION_FLOOR_TRANSITION_PHASE(byte) == phase &&
               INCANTATION_FLOOR_TRANSITION_ACTIVE(byte) && !(byte & 0xc0u));
    }
    CHECK(ok, "incantation_six_districts_two_city_voices_and_four_protected_transition_phases");
}

static void test_render_interaction_combine_solid_parity(void) {
    bool ok = true;
    static const uint8_t progresses[] = {60u, 200u};
    for (uint8_t elem = 0; elem < 4u; elem++)
        for (uint8_t form = 0; form < 8u; form++) {
            bool spell_drawn = false;
            for (size_t p = 0; p < sizeof progresses; p++)
                for (uint8_t caster = 0; caster < 2u; caster++) {
                    sim_world_t w;
                    sim_init(&w, SIMF_AUTHORITATIVE, 0);
                    install_spell(&w, caster,
                                  SPELL_DESC_PACK(form, elem, PAY_DAMAGE, TRAJ_MID, 2, STATUS_NONE,
                                                  INTERACT_COMBINE, TEMPO_RAPID, TREND_STEADY, 0),
                                  progresses[p]);
                    duel_render_t combine = {0};
                    duel_render_from_world(&combine, &w);
                    w.spell[caster].descriptor =
                        SPELL_DESC_PACK(form, elem, PAY_DAMAGE, TRAJ_MID, 2, STATUS_NONE,
                                        INTERACT_SOLID, TEMPO_RAPID, TREND_STEADY, 0);
                    duel_render_t solid = {0};
                    duel_render_from_world(&solid, &w);
                    w.spell[caster].active = 0;
                    duel_render_t none = {0};
                    duel_render_from_world(&none, &w);
                    for (uint8_t half = 0; half < 2u; half++) {
                        duel_fb_t fc, fs, fn;
                        incantation_render(&fc, &combine, half == 0u, false);
                        incantation_render(&fs, &solid, half == 0u, false);
                        incantation_render(&fn, &none, half == 0u, false);
                        EXPECT(memcmp(&fc, &fs, sizeof fc) == 0);
                        spell_drawn |= memcmp(&fc, &fn, sizeof fc) != 0;
                    }
                }
            /* Guard against a vacuous pass: every combo must actually put
             * carrier pixels on at least one canvas. */
            EXPECT(spell_drawn);
        }
    CHECK(ok, "incantation_render_combine_solid_parity_all_elements_forms");
}

/* Mirrors hp_window_xy: 2x2 lit windows, gapward column x7-8, outer x3-4,
 * rows bottom-up from y56. HP 8 uses four rows through y44; HP 10 adds the
 * supported fifth candidate row at y40. */
static bool health_pixel(bool is_left, int hp_index, int x, int y) {
    int canonical_x = (hp_index & 1) ? 3 : 7;
    int px = is_left ? canonical_x : DUEL_CANVAS_W - 2 - canonical_x;
    int py = 56 - (hp_index / 2) * 4;
    return (y == py || y == py + 1) && (x == px || x == px + 1);
}

static void test_health_grid_geometry_and_lifecycles(void) {
    bool ok = true;
    for (uint8_t side = 0; side < 2u; side++) {
        for (uint8_t hp = 0; hp <= SIM_MAX_HP; hp++) {
            sim_world_t w;
            sim_init(&w, SIMF_AUTHORITATIVE, 0);
            w.wiz[side].hp = hp;
            duel_render_t r = {0};
            duel_render_from_world(&r, &w);
            duel_fb_t fb;
            incantation_render(&fb, &r, side == 0u, false);
            unsigned lit = 0;
            for (int y = 40; y <= 57; y++) {
                for (int x = 3; x <= 8; x++) {
                    int sx = side == 0u ? x : DUEL_CANVAS_W - 1 - x;
                    bool expected = false;
                    for (int i = 0; i < hp; i++)
                        expected |= health_pixel(side == 0u, i, sx, y);
                    bool actual = duel_fb_get(&fb, sx, y);
                    if (actual != expected)
                        printf("DIAG health side=%u hp=%u x=%d y=%d actual=%u expected=%u\n", side,
                               hp, sx, y, actual, expected);
                    EXPECT(actual == expected);
                    lit += actual;
                }
            }
            EXPECT(lit == 4u * hp);
        }
    }

    /* The fixed grid remains unobscured for every wizard tableau. */
    static const uint8_t life[] = {LIFE_ACTIVE, LIFE_ACTIVE, LIFE_ACTIVE, LIFE_COLLAPSE,
                                   LIFE_DOWNED, LIFE_MEDIC,  LIFE_REPLACE};
    static const uint8_t pose[] = {POSE_IDLE, POSE_CAST, POSE_RECOVER, POSE_IDLE,
                                   POSE_IDLE, POSE_IDLE, POSE_IDLE};
    static const uint8_t ticks[] = {
        0, 0, 0, SIM_COLLAPSE_TICKS, SIM_DOWNED_TICKS, SIM_MEDIC_TICKS, SIM_REPLACE_TICKS};
    for (uint8_t side = 0; side < 2u; side++)
        for (size_t state = 0; state < sizeof life; state++) {
            sim_world_t w;
            sim_init(&w, SIMF_AUTHORITATIVE, 0);
            w.wiz[side].life = life[state];
            w.wiz[side].life_ticks = ticks[state];
            w.wiz[side].pose = pose[state];
            w.wiz[side].hp = 0;
            duel_render_t empty = {0};
            duel_render_from_world(&empty, &w);
            duel_fb_t zero;
            incantation_render(&zero, &empty, side == 0u, false);
            w.wiz[side].hp = SIM_MAX_HP;
            duel_render_t full = {0};
            duel_render_from_world(&full, &w);
            duel_fb_t grid;
            incantation_render(&grid, &full, side == 0u, false);
            for (int i = 0; i < SIM_MAX_HP; i++) {
                for (int x = 0; x < DUEL_CANVAS_W; x++)
                    for (int y = 40; y <= 57; y++)
                        if (health_pixel(side == 0u, i, x, y))
                            if (duel_fb_get(&zero, x, y) || !duel_fb_get(&grid, x, y)) {
                                printf("DIAG health-pose side=%u state=%zu x=%d y=%d zero=%u "
                                       "full=%u\n",
                                       side, state, x, y, duel_fb_get(&zero, x, y),
                                       duel_fb_get(&grid, x, y));
                                ok = false;
                            }
            }
        }
    CHECK(ok, "incantation_health_0_to_max_2x2_bottom_up_mirror_candidate_pose_clearance");
}

static void test_local_layer_attunement(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint64_t before = incantation_bytes_hash(&w, sizeof w);
    duel_render_t base = {0};
    duel_render_from_world(&base, &w);
    base.civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_RESEARCH, DUEL_CIVIC_MODE_NORMAL, 0);
    base.seed = 7;
    base.civic_phase = 32;
    base.layer = DUEL_RENDER_LAYER_PACK(0, DUEL_RENDER_LOCAL_NONE);
    duel_fb_t bl, br, ll, lr, rl, rr;
    incantation_render(&bl, &base, true, false);
    incantation_render(&br, &base, false, false);

    duel_render_t local = base;
    local.layer = DUEL_RENDER_LAYER_PACK(1, DUEL_RENDER_LOCAL_LEFT);
    incantation_render(&ll, &local, true, false);
    incantation_render(&lr, &local, false, false);
    bool left_changes = memcmp(&bl, &ll, sizeof bl) != 0;
    bool left_spares_right = memcmp(&br, &lr, sizeof br) == 0;
    bool ok = true;
    EXPECT(left_changes && left_spares_right);
    local.layer = DUEL_RENDER_LAYER_PACK(2, DUEL_RENDER_LOCAL_RIGHT);
    incantation_render(&rl, &local, true, false);
    incantation_render(&rr, &local, false, false);
    bool right_spares_left = memcmp(&bl, &rl, sizeof bl) == 0;
    bool right_changes = memcmp(&br, &rr, sizeof br) != 0;
    EXPECT(right_spares_left && right_changes);

    /* Release and ordinary global-layer typing reproduce the exact baseline. */
    local.layer = DUEL_RENDER_LAYER_PACK(3, DUEL_RENDER_LOCAL_NONE);
    incantation_render(&rl, &local, true, false);
    incantation_render(&rr, &local, false, false);
    bool global_left_same = memcmp(&bl, &rl, sizeof bl) == 0;
    bool global_right_same = memcmp(&br, &rr, sizeof br) == 0;
    EXPECT(global_left_same && global_right_same);

    sim_world_t typed;
    sim_init(&typed, SIMF_AUTHORITATIVE, 0);
    for (int i = 0; i < SCRY_PENDING_TICKS * 3; i++)
        sim_tick(&typed,
                 (sim_inputs_t){
                     .scry_mask = SCRY_M_L | SCRY_M_OTHER, .layer = {1, 0}, .held_pos = {1u, 0}},
                 NULL, 0, 0);
    bool typing_closed = !scry_is_open(&typed) && typed.scry.state == SCRY_FIRST_HELD;
    bool world_same = incantation_bytes_hash(&w, sizeof w) == before;
    EXPECT(typing_closed && world_same);
    if (!ok)
        printf("DIAG local lc=%u lsr=%u rsl=%u rc=%u gl=%u gr=%u typing=%u world=%u state=%u\n",
               left_changes, left_spares_right, right_spares_left, right_changes, global_left_same,
               global_right_same, typing_closed, world_same, typed.scry.state);
    CHECK(ok,
          "incantation_local_attunement_physical_half_release_typing_pending_and_scry_suppression");
}

static bool framebuffer_region_equal(const duel_fb_t *a, const duel_fb_t *b, int x0, int x1, int y0,
                                     int y1) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (duel_fb_get(a, x, y) != duel_fb_get(b, x, y))
                return false;
    return true;
}

static unsigned framebuffer_cleared_pixels(const duel_fb_t *base, const duel_fb_t *open) {
    unsigned cleared = 0;
    for (int y = 0; y < DUEL_CANVAS_H; y++)
        for (int x = 0; x < DUEL_CANVAS_W; x++) {
            if (duel_fb_get(base, x, y) && !duel_fb_get(open, x, y))
                cleared++;
        }
    return cleared;
}

static unsigned framebuffer_backing_pixels(const duel_fb_t *base, const duel_fb_t *open) {
    unsigned added = 0;
    for (int y = 0; y < DUEL_CANVAS_H; y++)
        for (int x = 0; x < DUEL_CANVAS_W; x++)
            if (!duel_fb_get(base, x, y) && duel_fb_get(open, x, y))
                added++;
    return added;
}

static unsigned framebuffer_lit_pixels(const duel_fb_t *fb) {
    unsigned lit = 0u;
    for (int y = 0; y < DUEL_CANVAS_H; y++)
        for (int x = 0; x < DUEL_CANVAS_W; x++)
            lit += duel_fb_get(fb, x, y);
    return lit;
}

static void test_diegetic_scry_instruments(void) {
    bool ok = true;
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[0].ward_strength = 2u;
    w.wiz[1].status = STATUS_FROZEN;
    w.wiz[1].status_intensity = 2u;
    w.wiz[1].status_ticks = 80u;
    w.residue[SIM_RESIDUE_MID_L] = (sim_residue_t){ELEM_EMBER, 2u, SIM_RESIDUE_DECAY_UNITS};
    w.field[0] = (sim_field_t){
        .descriptor =
            SPELL_DESC_PACK(SPELL_CONJURE, ELEM_FORCE, PAY_STATUS, TRAJ_GROUND, 2u, STATUS_MARKED,
                            INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, 0u),
        .timer = SIM_FIELD_RUNE_TICKS,
        .kind = FIELD_RUNE,
        .zone = SIM_RESIDUE_MID_L,
        .owner = SIM_SIDE_L,
    };
    duel_fb_t page_frame[2][SCRY_SCENES];
    duel_fb_t page_header[2][SCRY_SCENES];
    bool header_recorded[2][SCRY_SCENES] = {{false}};
    for (uint8_t page = 0; page < SCRY_SCENES; page++)
        for (uint8_t host_scene = 0; host_scene < DUEL_HOST_SCENE_COUNT; host_scene++)
            for (uint8_t online = 0; online < 2u; online++)
                for (uint8_t side = 0; side < 2u; side++) {
                    duel_render_t r = {0};
                    duel_render_from_world(&r, &w);
                    r.civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_RESEARCH, DUEL_CIVIC_MODE_NORMAL,
                                              DUEL_CIVIC_INTENSITY_BUSY);
                    r.seed = 9;
                    r.civic_phase = 48;
                    r.external = DUEL_HOST_CONTEXT_PACK(online, host_scene, 4u, false);
                    r.alert = DUEL_HOST_ALERT_PACK(DUEL_HOST_CATEGORY_SECURITY,
                                                   DUEL_HOST_PRIORITY_CRITICAL, 3u);
                    /* Deliberately unrelated to both host scene and page:
                     * emitted layer never selects almanac content. */
                    r.layer = DUEL_RENDER_LAYER_PACK((page + 1u) & 3u, DUEL_RENDER_LOCAL_NONE);
                    r.view.outcome_overlay =
                        (uint8_t)((r.view.outcome_overlay & 0x1fu) | (page << 5));
                    duel_fb_t base, open;
                    incantation_render(&base, &r, side == 0u, false);
                    r.view.outcome_overlay |= 0x10u;
                    incantation_render(&open, &r, side == 0u, false);
                    bool changed = memcmp(&base, &open, sizeof base) != 0;
                    unsigned cleared = framebuffer_cleared_pixels(&base, &open);
                    unsigned backing = framebuffer_backing_pixels(&base, &open);
                    unsigned lit = framebuffer_lit_pixels(&open);
                    bool outer_margin_same = framebuffer_region_equal(&base, &open, 0, 0, 0, 127) &&
                                             framebuffer_region_equal(&base, &open, 31, 31, 0, 127);
                    bool rollers = duel_fb_get(&open, 4, 5) && duel_fb_get(&open, 27, 5) &&
                                   duel_fb_get(&open, 1, 6) && duel_fb_get(&open, 30, 6) &&
                                   duel_fb_get(&open, 4, 123) && duel_fb_get(&open, 27, 123) &&
                                   duel_fb_get(&open, 1, 122) && duel_fb_get(&open, 30, 122) &&
                                   duel_fb_get(&open, 3, 64) && duel_fb_get(&open, 28, 64);
                    EXPECT(changed && cleared >= 300u && backing >= 300u && backing <= 900u &&
                           lit < 1400u && outer_margin_same && rollers);
                    /* The page heading/fraction is authoritative and does not
                     * change when host scene supplies different content. */
                    if (!header_recorded[side][page]) {
                        page_header[side][page] = open;
                        header_recorded[side][page] = true;
                    } else {
                        EXPECT(framebuffer_region_equal(&page_header[side][page], &open, 2, 29, 5,
                                                        21));
                    }
                    if (host_scene == DUEL_HOST_SCENE_ARCHIVE && online)
                        page_frame[side][page] = open;
                }
    for (uint8_t side = 0; side < 2u; side++)
        for (uint8_t a = 0; a < SCRY_SCENES; a++)
            for (uint8_t b = (uint8_t)(a + 1u); b < SCRY_SCENES; b++)
                EXPECT(memcmp(&page_frame[side][a], &page_frame[side][b],
                              sizeof page_frame[side][a]) != 0);
    for (uint8_t a = 0; a < SCRY_SCENES; a++) {
        EXPECT(framebuffer_region_equal(&page_header[SIM_SIDE_L][a], &page_header[SIM_SIDE_R][a], 2,
                                        29, 5, 21));
        for (uint8_t b = (uint8_t)(a + 1u); b < SCRY_SCENES; b++)
            EXPECT(!framebuffer_region_equal(&page_header[SIM_SIDE_L][a],
                                             &page_header[SIM_SIDE_L][b], 2, 29, 5, 21));
    }

    /* Moving the content stream never moves the parchment itself. */
    duel_render_t moving = {0};
    duel_render_from_world(&moving, &w);
    moving.view.outcome_overlay = VIEW_OVERLAY_PACK(0u, true, 1u);
    moving.scry_motion = DUEL_SCRY_MOTION_PACK(DUEL_SCRY_EXTENT_FULL, false);
    duel_fb_t scroll_zero, scroll_one;
    incantation_render(&scroll_zero, &moving, true, false);
    moving.scry_scroll = 1u;
    incantation_render(&scroll_one, &moving, true, false);
    EXPECT(memcmp(&scroll_zero, &scroll_one, sizeof scroll_zero) != 0 &&
           framebuffer_region_equal(&scroll_zero, &scroll_one, 1, 30, 5, 7) &&
           framebuffer_region_equal(&scroll_zero, &scroll_one, 1, 30, 121, 123));

    /* Every normalized alert remains legible on the Host scroll; stale-link
     * and diagnostics retain their later-layer priority. */
    duel_render_t r = {0};
    duel_render_from_world(&r, &w);
    r.civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_COMMONS, DUEL_CIVIC_MODE_NORMAL, 0);
    r.external = DUEL_HOST_CONTEXT_PACK(true, DUEL_HOST_SCENE_FOCUS, 3, true);
    r.alert = DUEL_HOST_ALERT_PACK(DUEL_HOST_CATEGORY_SECURITY, DUEL_HOST_PRIORITY_CRITICAL, 7);
    r.layer = DUEL_RENDER_LAYER_PACK(3, DUEL_RENDER_LOCAL_NONE);
    r.view.outcome_overlay = (uint8_t)((r.view.outcome_overlay & 0x0fu) | 0x10u | (2u << 5));
    duel_render_t empty_alert = r;
    empty_alert.alert = 0;
    empty_alert.external = DUEL_HOST_CONTEXT_PACK(true, DUEL_HOST_SCENE_FOCUS, 4, false);
    duel_fb_t empty_alert_fb;
    incantation_render(&empty_alert_fb, &empty_alert, true, false);
    for (uint8_t category = 1; category < DUEL_HOST_CATEGORY_COUNT; category++)
        for (uint8_t priority_level = DUEL_HOST_PRIORITY_LOW;
             priority_level <= DUEL_HOST_PRIORITY_CRITICAL; priority_level++)
            for (uint8_t persistent = 0; persistent < 2u; persistent++) {
                duel_render_t alert = r;
                alert.external = DUEL_HOST_CONTEXT_PACK(true, DUEL_HOST_SCENE_FOCUS, 4, persistent);
                alert.alert = DUEL_HOST_ALERT_PACK(category, priority_level, 3);
                duel_fb_t category_fb;
                incantation_render(&category_fb, &alert, true, false);
                EXPECT(memcmp(&empty_alert_fb, &category_fb, sizeof category_fb) != 0);
            }
    r.flags |= DUEL_RENDER_STALE;
    r.diag_tick = 7;
    duel_fb_t priority;
    incantation_render(&priority, &r, true, true);
    /* Stale-link box in the corner, and the diagnostics build's 1 Hz sync
     * heartbeat on the left tower-top tip (diag_tick 7 < 13 -> lit at x6 y0),
     * drawn last so it keeps its later-layer priority over the scene. */
    EXPECT(duel_fb_get(&priority, 23, 2) && duel_fb_get(&priority, 6, 0));
    CHECK(ok, "incantation_two_labelled_scrolls_authoritative_pages_motion_alert_and_priority");
}

static unsigned crowd_head_additions(const duel_fb_t *base, const duel_fb_t *moment, bool is_left,
                                     uint8_t seed, uint8_t district) {
    unsigned added = 0u;
    for (uint8_t i = 0; i < DUEL_CROWD_BYSTANDERS; i++) {
        int desk_x = 6 + i * 20 + ((seed + district + i) & 1u);
        int x = is_left ? desk_x : DUEL_CANVAS_W - 1 - desk_x;
        int feet = 108 - (int)((seed + i + district) & 1u);
        for (int dx = -1; dx <= 1; dx++)
            added += !duel_fb_get(base, x + dx, feet - 5) && duel_fb_get(moment, x + dx, feet - 5);
    }
    return added;
}

static void test_observatory_quarters_and_bounded_crowds(void) {
    bool ok = true;
    sim_world_t world;
    sim_init(&world, SIMF_AUTHORITATIVE, 0u);
    duel_fb_t ritual[2][4];
    for (uint8_t side = 0; side < 2u; side++)
        for (uint8_t stage = 0; stage < 4u; stage++) {
            duel_render_t render = {0};
            duel_render_from_world(&render, &world);
            render.seed = 0x35u;
            render.civic_phase = 7u;
            render.external = DUEL_HOST_CONTEXT_PACK(true, DUEL_HOST_SCENE_FOCUS, 0u, false);
            render.civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_SPECIAL, DUEL_CIVIC_MODE_QUIET, stage);
            incantation_render(&ritual[side][stage], &render, side == SIM_SIDE_L, false);
            duel_fb_t later;
            render.civic_phase = 203u; /* no within-quarter occupation motion */
            incantation_render(&later, &render, side == SIM_SIDE_L, false);
            EXPECT(band_difference(&ritual[side][stage], &later, DUEL_FLOOR_Y0, DUEL_FLOOR_Y1) ==
                   0u);
        }
    for (uint8_t side = 0; side < 2u; side++)
        for (uint8_t a = 0; a < 4u; a++)
            for (uint8_t b = (uint8_t)(a + 1u); b < 4u; b++)
                EXPECT(band_difference(&ritual[side][a], &ritual[side][b], DUEL_FLOOR_Y0,
                                       DUEL_FLOOR_Y1) >= 8u);

    /* An arrival crowd is exactly two derived bystanders plus the ordinary
     * resident. Its session-phased window is strictly shorter than the
     * default aftermath, and QUIET/Observatory suppress it. */
    EXPECT(DUEL_CROWD_MAX_VISIBLE == 3u && DUEL_CROWD_BYSTANDERS == 2u &&
           DUEL_CROWD_ARRIVAL_PHASES * DUEL_CIVIC_TICK_MS <=
               SIM_AFTER_DEFAULT_TICKS * SIM_TICK_MS &&
           (DUEL_CROWD_ARRIVAL_PHASES + 1u) * DUEL_CIVIC_TICK_MS >
               SIM_AFTER_DEFAULT_TICKS * SIM_TICK_MS);
    for (uint8_t side = 0; side < 2u; side++) {
        duel_render_t base = {0};
        duel_render_from_world(&base, &world);
        base.seed = 9u;
        base.civic_phase = (uint8_t)(64u - base.seed);
        base.external = DUEL_HOST_CONTEXT_PACK(true, DUEL_HOST_SCENE_DUEL, 1u, false);
        base.civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_COMMONS, DUEL_CIVIC_MODE_NORMAL, 0u);
        duel_render_t arrival = base;
        arrival.shared_pres =
            DUEL_VISITOR_PACK(DUEL_CIVIC_COURIER_MESSENGER, side, DUEL_CIVIC_VISIT_ARRIVING);
        duel_fb_t base_frame, arrival_frame;
        incantation_render(&base_frame, &base, side == SIM_SIDE_L, false);
        incantation_render(&arrival_frame, &arrival, side == SIM_SIDE_L, false);
        EXPECT(crowd_head_additions(&base_frame, &arrival_frame, side == SIM_SIDE_L, base.seed,
                                    DUEL_DISTRICT_COMMONS) >= 2u);

        base.civic_phase = (uint8_t)(base.civic_phase + DUEL_CROWD_ARRIVAL_PHASES);
        arrival.civic_phase = base.civic_phase;
        incantation_render(&base_frame, &base, side == SIM_SIDE_L, false);
        incantation_render(&arrival_frame, &arrival, side == SIM_SIDE_L, false);
        EXPECT(crowd_head_additions(&base_frame, &arrival_frame, side == SIM_SIDE_L, base.seed,
                                    DUEL_DISTRICT_COMMONS) == 0u);

        base.civic_phase = (uint8_t)(64u - base.seed);
        arrival.civic_phase = base.civic_phase;
        base.civic = arrival.civic =
            DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_COMMONS, DUEL_CIVIC_MODE_QUIET, 0u);
        incantation_render(&base_frame, &base, side == SIM_SIDE_L, false);
        incantation_render(&arrival_frame, &arrival, side == SIM_SIDE_L, false);
        EXPECT(crowd_head_additions(&base_frame, &arrival_frame, side == SIM_SIDE_L, base.seed,
                                    DUEL_DISTRICT_COMMONS) == 0u);

        base.external = arrival.external =
            DUEL_HOST_CONTEXT_PACK(true, DUEL_HOST_SCENE_FOCUS, 1u, false);
        base.civic = arrival.civic =
            DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_SPECIAL, DUEL_CIVIC_MODE_NORMAL, 0u);
        incantation_render(&base_frame, &base, side == SIM_SIDE_L, false);
        incantation_render(&arrival_frame, &arrival, side == SIM_SIDE_L, false);
        EXPECT(crowd_head_additions(&base_frame, &arrival_frame, side == SIM_SIDE_L, base.seed,
                                    DUEL_DISTRICT_OBSERVATORY) == 0u);
    }
    CHECK(ok, "observatory_four_quarter_boundaries_and_crowds_max_three_brief_quiet_suppressed");
}

static void test_gap_cue_families_temporal_mirrors(void) {
    static const uint32_t desc[] = {
        SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE, TRAJ_MID, 2, STATUS_NONE,
                        INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, 1),
        SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FROST, PAY_DAMAGE, TRAJ_HOMING, 2, STATUS_NONE,
                        INTERACT_SOLID, TEMPO_RAPID, TREND_ACCELERATING, 2),
        SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_VOID, PAY_DAMAGE, TRAJ_MID, 2, STATUS_NONE,
                        INTERACT_PHASE, TEMPO_FLOWING, TREND_STEADY, 3),
        SPELL_DESC_PACK(SPELL_BEAM, ELEM_EMBER, PAY_DAMAGE, TRAJ_MID, 3, STATUS_NONE,
                        INTERACT_SOLID, TEMPO_RAPID, TREND_STEADY, 0),
        SPELL_DESC_PACK(SPELL_CHAIN, ELEM_FORCE, PAY_DAMAGE, TRAJ_HOMING, 3, STATUS_NONE,
                        INTERACT_SOLID, TEMPO_RAPID, TREND_IRREGULAR, 1),
    };
    static const uint8_t progress[] = {105u, 130u, 155u};
    bool ok = true;
    for (size_t family = 0; family < sizeof desc / sizeof desc[0]; family++) {
        for (size_t stage = 0; stage < sizeof progress; stage++) {
            duel_view_spell_t left = {
                .active = 1u,
                .descriptor = desc[family],
                .progress = progress[stage],
                .kind = DUEL_KIND_WITH_TIER(
                    DUEL_KIND_PACK(SPELL_DESC_ELEMENT(desc[family]), MOD_NONE, PAY_IMPACT), 1u)};
            duel_view_spell_t right = left;
            duel_fb_t ll, lr, rl, rr;
            duel_fb_clear(&ll);
            duel_fb_clear(&lr);
            duel_fb_clear(&rl);
            duel_fb_clear(&rr);
            duel_combat_draw_spell(&ll, &left, 0, 0, true, 9u);
            duel_combat_draw_spell(&lr, &left, 0, 0, false, 9u);
            duel_combat_draw_spell(&rl, &right, 1, 0, true, 9u);
            duel_combat_draw_spell(&rr, &right, 1, 0, false, 9u);
            EXPECT(exact_mirror(&ll, &rr) && exact_mirror(&lr, &rl) &&
                   framebuffer_pixels(&ll) + framebuffer_pixels(&lr) > 0u);
        }
    }

    /* Non-continuous families have no edge handoff before departure or after
     * arrival. Beam/chain remain bilateral by design and are tested above. */
    for (size_t family = 0; family < 3u; family++) {
        for (uint8_t p = 32u; p <= 224u; p = (uint8_t)(p + 192u)) {
            duel_view_spell_t sp = {
                .active = 1u,
                .descriptor = desc[family],
                .progress = p,
                .kind = DUEL_KIND_WITH_TIER(
                    DUEL_KIND_PACK(SPELL_DESC_ELEMENT(desc[family]), MOD_NONE, PAY_IMPACT), 1u)};
            duel_fb_t left, right;
            duel_fb_clear(&left);
            duel_fb_clear(&right);
            duel_combat_draw_spell(&left, &sp, 0, 0, true, 9u);
            duel_combat_draw_spell(&right, &sp, 0, 0, false, 9u);
            for (int y = 0; y < DUEL_CANVAS_H; y++)
                EXPECT(!duel_fb_get(&left, 31, y) && !duel_fb_get(&right, 0, y));
            if (p == 224u)
                break;
        }
    }
    CHECK(ok, "incantation_gap_cue_departure_midpoint_arrival_mirrors_and_bounds");
}

static void test_all_forms_bilateral_mirror(void) {
    bool ok = true;
    for (uint8_t form = SPELL_PROJECTILE; form <= SPELL_CONJURE; form++) {
        uint8_t trajectory = form == SPELL_FIREBALL      ? TRAJ_ROOF
                             : form == SPELL_GROUND_WAVE ? TRAJ_GROUND
                             : form == SPELL_CHAIN       ? TRAJ_HOMING
                             : form == SPELL_CONJURE     ? TRAJ_RETURNING
                                                         : TRAJ_MID;
        uint32_t desc =
            SPELL_DESC_PACK(form, ELEM_FORCE, PAY_DAMAGE, trajectory, 3, STATUS_NONE,
                            form == SPELL_SINGULARITY ? INTERACT_ABSORB : INTERACT_SOLID,
                            TEMPO_RAPID, TREND_ACCELERATING, 2);
        uint8_t progress = form == SPELL_BEAM          ? 128u
                           : form == SPELL_SINGULARITY ? 144u
                           : form == SPELL_SWARM       ? (uint8_t)((5u << 5) | 14u)
                           : form == SPELL_CHAIN       ? 176u
                           : form == SPELL_CONJURE     ? (uint8_t)((3u << 5) | 14u)
                                                       : 72u;
        sim_world_t left_world, right_world;
        sim_init(&left_world, SIMF_AUTHORITATIVE, 0);
        sim_init(&right_world, SIMF_AUTHORITATIVE, 0);
        install_spell(&left_world, 0, desc, progress);
        install_spell(&right_world, 1, desc, progress);
        duel_view_t lv, rv;
        duel_view_from_world(&left_world, &lv);
        duel_view_from_world(&right_world, &rv);
        duel_view_spell_t ls = duel_view_spell(&lv, 0, 0), rs = duel_view_spell(&rv, 1, 0);
        duel_fb_t ll, lr, rl, rr;
        duel_fb_clear(&ll);
        duel_fb_clear(&lr);
        duel_fb_clear(&rl);
        duel_fb_clear(&rr);
        duel_combat_draw_spell(&ll, &ls, 0, 0, true, 5);
        duel_combat_draw_spell(&lr, &ls, 0, 0, false, 5);
        duel_combat_draw_spell(&rl, &rs, 1, 0, true, 5);
        duel_combat_draw_spell(&rr, &rs, 1, 0, false, 5);
        bool mirrored = exact_mirror(&ll, &rr) && exact_mirror(&lr, &rl);
        if (!mirrored)
            printf("DIAG bilateral form=%u\n", form);
        EXPECT(mirrored);
    }
    CHECK(ok, "incantation_all_forms_bilateral_pixel_mirror");
}

static void test_aftermath_split_loss_and_reconnect(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.aftermath[0] =
        (sim_aftermath_t){AFTER_FIRE, 175, 4, RESIDENT_PANIC, ROOM_DISRUPTED, OBJECT_FIRE};
    w.world_state = WORLD_CRISIS;
    duel_snapshot_t first, later, corrupt, restarted;
    test_encode_snapshot(&w, 3, 10, &first);
    duel_rx_state_t rx = {0};
    bool ok = true;
    EXPECT(duel_decode_valid(&first) && duel_rx_accept(&rx, &first, false));
    uint8_t old_revision = rx.last.revision;
    wait_ticks(&w, 50);
    test_encode_snapshot(&w, 3, 11, &later);
    EXPECT(later.revision != old_revision && rx.last.revision == old_revision); /* dropped update */
    corrupt = later;
    corrupt.shared_pres ^= 0x04u;
    EXPECT(!duel_decode_valid(&corrupt) && rx.last.revision == old_revision);
    EXPECT(duel_rx_accept(&rx, &later, false) && rx.last.revision == later.revision);
    wait_ticks(&w, 50);
    test_encode_snapshot(&w, 4, 1, &restarted);
    EXPECT(duel_rx_accept(&rx, &restarted, true) && rx.last.session == 4u &&
           rx.last.revision == restarted.revision &&
           INCANTATION_AFTER_KIND(rx.last.shared_pres, 0) == AFTER_FIRE);
    CHECK(ok, "incantation_aftermath_split_loss_corruption_and_reconnect");
}

void run_rendering_geometry_tests(void) {
    test_render_interaction_combine_solid_parity();
    test_health_grid_geometry_and_lifecycles();
    test_local_layer_attunement();
    test_diegetic_scry_instruments();
    test_observatory_quarters_and_bounded_crowds();
    test_floor_occupations_and_transitions();
    test_gap_cue_families_temporal_mirrors();
    test_all_forms_bilateral_mirror();
    test_aftermath_split_loss_and_reconnect();
}
