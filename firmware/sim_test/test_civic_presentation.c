#include "test_harness.h"

static bool pixels_within(const duel_fb_t *fb, int y0, int y1) {
    for (int y = 0; y < DUEL_CANVAS_H; y++)
        for (int x = 0; x < DUEL_CANVAS_W; x++)
            if (duel_fb_get(fb, x, y) && (y < y0 || y > y1)) return false;
    return true;
}

static void test_civic_anchor_and_courier_matrix(void) {
    bool ok = true;
    for (uint8_t action = 0; action < DUEL_CIVIC_ACTION_COUNT; action++) {
        incantation_point_t fallback = incantation_occupation_anchor(INCANTATION_OCCUPATION_FLOORS, action);
        incantation_point_t commons = incantation_occupation_anchor(DUEL_CIVIC_FLOOR_COMMONS, action);
        EXPECT(fallback.x == commons.x && fallback.y == commons.y);
        for (uint8_t floor = 0; floor < INCANTATION_OCCUPATION_FLOORS; floor++) {
            incantation_point_t point = incantation_occupation_anchor(floor, action);
            EXPECT(point.x >= 0 && point.x < DUEL_CANVAS_W && point.y >= DUEL_FLOOR_BEAM_Y && point.y <= DUEL_FLOOR_Y1);
        }
    }

    sim_world_t world; sim_init(&world, SIMF_AUTHORITATIVE, 0);
    for (uint8_t floor = 0; floor < INCANTATION_OCCUPATION_FLOORS; floor++)
        for (uint8_t kind = DUEL_CIVIC_COURIER_MESSENGER;
             kind < DUEL_CIVIC_COURIER_COUNT; kind++)
            for (uint8_t life = DUEL_CIVIC_VISIT_ARRIVING;
                 life <= DUEL_CIVIC_VISIT_RESOLVING; life++)
                for (uint8_t density = DUEL_CIVIC_DENSITY_SINGLE;
                     density <= DUEL_CIVIC_DENSITY_MANY; density++)
                    for (uint8_t mode = DUEL_CIVIC_MODE_NORMAL;
                         mode <= DUEL_CIVIC_MODE_QUIET; mode++)
                        for (uint8_t city = 0; city < 2u; city++) {
                            duel_render_t r = {0}; duel_render_from_world(&r, &world);
                            r.civic = DUEL_CIVIC_PACK(floor, mode, 0);
                            r.shared_pres = (uint8_t)(DUEL_VISITOR_PACK(kind, city, life) |
                                DUEL_VISITOR_DENSITY_PACK(density));
                            duel_fb_t assigned, repeat, opposite;
                            duel_fb_clear(&assigned); duel_fb_clear(&repeat); duel_fb_clear(&opposite);
                            draw_courier(&assigned, &r, city == 0u);
                            draw_courier(&repeat, &r, city == 0u);
                            draw_courier(&opposite, &r, city != 0u);
                            EXPECT(memcmp(&assigned, &repeat, sizeof assigned) == 0 &&
                                  framebuffer_pixels(&opposite) == 0u &&
                                  pixels_within(&assigned, DUEL_FLOOR_BEAM_Y, DUEL_FLOOR_Y1));
                            if (floor == DUEL_CIVIC_FLOOR_SPECIAL)
                                EXPECT(framebuffer_pixels(&assigned) == 0u);
                            else
                                EXPECT(framebuffer_pixels(&assigned) >= 6u);
                        }

    /* City assignment is a pure mirror, and transition routing uses the visible
     * source room until the target reveal begins. */
    duel_render_t route = {0}; duel_render_from_world(&route, &world);
    route.civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_WORKSHOP, DUEL_CIVIC_MODE_NORMAL, 0);
    route.shared_pres = DUEL_VISITOR_PACK(DUEL_CIVIC_COURIER_PARCEL, 0,
                                           DUEL_CIVIC_VISIT_WAITING);
    route.floor_transition = INCANTATION_FLOOR_TRANSITION_PACK(DUEL_CIVIC_FLOOR_COMMONS, 1, true);
    duel_fb_t source, expected, mirror;
    duel_fb_clear(&source); duel_fb_clear(&expected); duel_fb_clear(&mirror);
    draw_courier(&source, &route, true);
    route.floor_transition = 0;
    route.civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_COMMONS, DUEL_CIVIC_MODE_NORMAL, 0);
    draw_courier(&expected, &route, true);
    route.shared_pres = DUEL_VISITOR_PACK(DUEL_CIVIC_COURIER_PARCEL, 1,
                                           DUEL_CIVIC_VISIT_WAITING);
    draw_courier(&mirror, &route, false);
    EXPECT(memcmp(&source, &expected, sizeof source) == 0 && exact_mirror(&expected, &mirror));
    for (uint8_t kind = DUEL_CIVIC_COURIER_MESSENGER;
         kind < DUEL_CIVIC_COURIER_COUNT; kind++) {
        duel_fb_t variant[INCANTATION_OCCUPATION_FLOORS];
        for (uint8_t floor = 0; floor < INCANTATION_OCCUPATION_FLOORS; floor++) {
            route.civic = DUEL_CIVIC_PACK(floor, DUEL_CIVIC_MODE_NORMAL, 0);
            route.shared_pres = DUEL_VISITOR_PACK(kind, 0, DUEL_CIVIC_VISIT_AGING);
            duel_fb_clear(&variant[floor]); draw_courier(&variant[floor], &route, true);
        }
        EXPECT(memcmp(&variant[0], &variant[1], sizeof variant[0]) != 0 &&
              memcmp(&variant[0], &variant[2], sizeof variant[0]) != 0 &&
              memcmp(&variant[1], &variant[2], sizeof variant[0]) != 0);
    }
    CHECK(ok, "incantation_canonical_anchors_and_courier_floor_lifecycle_density_mode_city_matrix");
}

static void test_rare_event_floor_phase_mode_target_matrix(void) {
    bool ok = true;
    for (uint8_t floor = 0; floor < INCANTATION_OCCUPATION_FLOORS; floor++)
        for (uint8_t id = DUEL_CIVIC_EVENT_RUNAWAY_SCROLL;
             id < DUEL_CIVIC_EVENT_COUNT; id++)
            for (uint8_t phase = DUEL_CIVIC_EVENT_PHASE_ARMED;
                 phase <= DUEL_CIVIC_EVENT_PHASE_COOLDOWN; phase++)
                for (uint8_t mode = DUEL_CIVIC_MODE_NORMAL;
                     mode <= DUEL_CIVIC_MODE_QUIET; mode++) {
                    bool shared = id >= DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER;
                    for (uint8_t target_case = 0; target_case < (shared ? 1u : 2u); target_case++) {
                        uint8_t target = shared ? DUEL_CIVIC_EVENT_TARGET_SHARED : target_case;
                        duel_render_t r = {0};
                        r.civic = DUEL_CIVIC_PACK(floor, mode, 0);
                        r.revision = DUEL_EVENT_PACK(id, phase, target);
                        duel_fb_t left, right, repeat;
                        duel_fb_clear(&left); duel_fb_clear(&right); duel_fb_clear(&repeat);
                        draw_rare_event(&left, &r, true);
                        draw_rare_event(&right, &r, false);
                        draw_rare_event(&repeat, &r, true);
                        EXPECT(memcmp(&left, &repeat, sizeof left) == 0);
                        if (floor == DUEL_CIVIC_FLOOR_SPECIAL) {
                            EXPECT(framebuffer_pixels(&left) == 0u &&
                                  framebuffer_pixels(&right) == 0u);
                        } else if (shared) {
                            EXPECT(framebuffer_pixels(&left) >= 4u && framebuffer_pixels(&right) >= 4u);
                        } else if (target == DUEL_CIVIC_EVENT_TARGET_LEFT) {
                            EXPECT(framebuffer_pixels(&left) >= 4u && framebuffer_pixels(&right) == 0u);
                        } else {
                            EXPECT(framebuffer_pixels(&right) >= 4u && framebuffer_pixels(&left) == 0u);
                        }
                        if (id == DUEL_CIVIC_EVENT_CIVIC_SKY)
                            EXPECT(pixels_within(&left, 16, 26) && pixels_within(&right, 16, 26));
                        else
                            EXPECT(pixels_within(&left, DUEL_FLOOR_BEAM_Y, DUEL_FLOOR_Y1) &&
                                  pixels_within(&right, DUEL_FLOOR_BEAM_Y, DUEL_FLOOR_Y1));
                    }
                }

    duel_render_t none = {0}; duel_fb_t empty;
    none.revision = DUEL_EVENT_PACK(DUEL_CIVIC_EVENT_NONE, 0, 0);
    duel_fb_clear(&empty); draw_rare_event(&empty, &none, true);
    EXPECT(framebuffer_pixels(&empty) == 0u);
    for (uint8_t id = DUEL_CIVIC_EVENT_RUNAWAY_SCROLL;
         id < DUEL_CIVIC_EVENT_COUNT; id++) {
        duel_fb_t variant[INCANTATION_OCCUPATION_FLOORS];
        for (uint8_t floor = 0; floor < INCANTATION_OCCUPATION_FLOORS; floor++) {
            none.civic = DUEL_CIVIC_PACK(floor, DUEL_CIVIC_MODE_NORMAL, 0);
            none.revision = DUEL_EVENT_PACK(id, DUEL_CIVIC_EVENT_PHASE_ACTIVE,
                id >= DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER ?
                    DUEL_CIVIC_EVENT_TARGET_SHARED : DUEL_CIVIC_EVENT_TARGET_LEFT);
            duel_fb_clear(&variant[floor]); draw_rare_event(&variant[floor], &none, true);
        }
        EXPECT(memcmp(&variant[0], &variant[1], sizeof variant[0]) != 0 &&
              memcmp(&variant[0], &variant[2], sizeof variant[0]) != 0 &&
              memcmp(&variant[1], &variant[2], sizeof variant[0]) != 0);
    }
    none.civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_WORKSHOP, DUEL_CIVIC_MODE_NORMAL, 0);
    none.revision = DUEL_EVENT_PACK(DUEL_CIVIC_EVENT_RUNAWAY_SCROLL,
                                    DUEL_CIVIC_EVENT_PHASE_ACTIVE,
                                    DUEL_CIVIC_EVENT_TARGET_LEFT);
    none.floor_transition = INCANTATION_FLOOR_TRANSITION_PACK(DUEL_CIVIC_FLOOR_COMMONS, 1, true);
    duel_fb_t transition, commons;
    duel_fb_clear(&transition); draw_rare_event(&transition, &none, true);
    none.civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_COMMONS, DUEL_CIVIC_MODE_NORMAL, 0);
    none.floor_transition = 0;
    duel_fb_clear(&commons); draw_rare_event(&commons, &none, true);
    EXPECT(memcmp(&transition, &commons, sizeof transition) == 0);
    CHECK(ok, "incantation_rare_event_floor_family_phase_mode_target_routing_and_safety_matrix");
}

static void test_aftermath_floor_kind_phase_half_matrix(void) {
    bool ok = true;
    sim_world_t world; sim_init(&world, SIMF_AUTHORITATIVE, 0);
    for (uint8_t floor = 0; floor < INCANTATION_OCCUPATION_FLOORS; floor++)
        for (uint8_t kind = AFTER_CHEER; kind <= AFTER_MAX_CAST; kind++)
            for (uint8_t phase = 0; phase < 4u; phase++)
                for (uint8_t side = 0; side < 2u; side++) {
                    duel_render_t base = {0}; duel_render_from_world(&base, &world);
                    base.seed = 0x51u; base.civic_phase = 23u;
                    base.civic = DUEL_CIVIC_PACK(floor, DUEL_CIVIC_MODE_NORMAL, 0);
                    duel_render_t after = base;
                    after.shared_pres = (uint8_t)((kind << (side * 3u)) |
                                                   (WORLD_RECOVERY << 6));
                    after.revision = (uint8_t)(INCANTATION_AFTERMATH_WIRE |
                                               (phase << (side * 2u)));
                    duel_fb_t before, first, second;
                    incantation_render(&before, &base, side == 0u, false);
                    incantation_render(&first, &after, side == 0u, false);
                    incantation_render(&second, &after, side == 0u, false);
                    EXPECT(memcmp(&first, &second, sizeof first) == 0 &&
                          memcmp(&before, &first, sizeof first) != 0 &&
                          band_difference(&before, &first, 0, DUEL_FLOOR_BEAM_Y) == 0u &&
                          band_difference(&before, &first, DUEL_FLOOR_Y1 + 1, DUEL_CANVAS_H - 1) == 0u);
                }
    CHECK(ok, "incantation_aftermath_floor_kind_phase_half_anchor_priority_and_protected_regions");
}

static uint8_t quiet_action(uint8_t action) {
    if (action == DUEL_CIVIC_ACTION_WALK) return DUEL_CIVIC_ACTION_REST;
    if (action == DUEL_CIVIC_ACTION_REACT) return DUEL_CIVIC_ACTION_INSPECT;
    if (action == DUEL_CIVIC_ACTION_WATCH_ROOF) return DUEL_CIVIC_ACTION_WORK;
    return action;
}

static void test_resident_occupation_derivation(void) {
    bool seen[2][INCANTATION_OCCUPATION_FLOORS][DUEL_CIVIC_ACTION_COUNT] = {{{false}}};
    bool personalities[2][DUEL_CIVIC_PERSONALITY_COUNT] = {{false}};
    bool ok = true;
    for (uint16_t seed = 0; seed < 256u; seed++) {
        for (uint8_t side = 0; side < 2u; side++) {
            personalities[side][civic_resident_personality((uint8_t)seed, side == 0u)] = true;
            for (uint8_t slot = 0; slot < 16u; slot++) {
                uint8_t phase = (uint8_t)(slot * DUEL_CIVIC_ACTION_SLOT);
                civic_resident_t common = civic_resident_derive((uint8_t)seed, side == 0u,
                    DUEL_CIVIC_FLOOR_COMMONS, DUEL_CIVIC_MODE_NORMAL, phase);
                for (uint8_t floor = 0; floor < INCANTATION_OCCUPATION_FLOORS; floor++) {
                    civic_resident_t a = civic_resident_derive((uint8_t)seed, side == 0u,
                        floor, DUEL_CIVIC_MODE_NORMAL, phase);
                    civic_resident_t b = civic_resident_derive((uint8_t)seed, side == 0u,
                        floor, DUEL_CIVIC_MODE_NORMAL, phase);
                    EXPECT(memcmp(&a, &b, sizeof a) == 0 && a.action == common.action);
                    EXPECT(a.station == INCANTATION_OCCUPATION_KEY(floor, a.action));
                    seen[side][floor][a.action] = true;

                    civic_resident_t quiet = civic_resident_derive((uint8_t)seed, side == 0u,
                        floor, DUEL_CIVIC_MODE_QUIET, phase);
                    EXPECT(quiet.action == quiet_action(common.action) &&
                          quiet.station == INCANTATION_OCCUPATION_KEY(floor, quiet.action));
                }
                civic_resident_t special = civic_resident_derive((uint8_t)seed, side == 0u,
                    DUEL_CIVIC_FLOOR_SPECIAL, DUEL_CIVIC_MODE_NORMAL, phase);
                EXPECT(special.action == common.action &&
                      special.station == INCANTATION_OCCUPATION_KEY(DUEL_CIVIC_FLOOR_SPECIAL,
                                                            special.action));
            }
        }
    }
    for (uint8_t side = 0; side < 2u; side++) {
        for (uint8_t p = 0; p < DUEL_CIVIC_PERSONALITY_COUNT; p++) EXPECT(personalities[side][p]);
        for (uint8_t floor = 0; floor < INCANTATION_OCCUPATION_FLOORS; floor++)
            for (uint8_t action = 0; action < DUEL_CIVIC_ACTION_COUNT; action++)
                EXPECT(seen[side][floor][action]);
    }

    /* Authoritative aftermath suppresses personality, progress, carry, and
     * object-reaction modifiers while retaining the same resident body/task. */
    civic_resident_t after = {DUEL_CIVIC_PERSONALITY_DILIGENT, DUEL_CIVIC_ACTION_REACT,
        INCANTATION_OCCUPATION_KEY(DUEL_CIVIC_FLOOR_RESEARCH, DUEL_CIVIC_ACTION_REACT), 0,
        RESIDENT_CHEER};
    duel_fb_t first, second;
    duel_fb_clear(&first); duel_fb_clear(&second);
    civic_resident_draw(&first, &after, true, DUEL_CIVIC_MODE_NORMAL, 0);
    after.personality = DUEL_CIVIC_PERSONALITY_DISTRACTED; after.progress = 15;
    civic_resident_draw(&second, &after, true, DUEL_CIVIC_MODE_NORMAL, 99);
    EXPECT(memcmp(&first, &second, sizeof first) == 0);
    CHECK(ok, "incantation_resident_42_keys_personalities_quiet_fallback_deterministic_aftermath");
}

static void test_resident_geometry_and_object_separation(void) {
    bool ok = true;
    for (uint8_t side = 0; side < 2u; side++) {
        civic_resident_t core = {DUEL_CIVIC_PERSONALITY_DILIGENT, DUEL_CIVIC_ACTION_WORK,
            INCANTATION_OCCUPATION_KEY(DUEL_CIVIC_FLOOR_COMMONS, DUEL_CIVIC_ACTION_WORK), 0, 0xffu};
        duel_fb_t body;
        duel_fb_clear(&body);
        civic_resident_draw(&body, &core, side == 0u, DUEL_CIVIC_MODE_NORMAL, 0);
        int x0 = 32, x1 = -1, y0 = 128, y1 = -1;
        for (int y = 0; y < DUEL_CANVAS_H; y++)
            for (int x = 0; x < DUEL_CANVAS_W; x++)
                if (duel_fb_get(&body, x, y)) {
                    if (x < x0) x0 = x;
                    if (x > x1) x1 = x;
                    if (y < y0) y0 = y;
                    if (y > y1) y1 = y;
                }
        EXPECT(x1 - x0 + 1 == 5 && y1 - y0 + 1 == 14 && y0 >= 61 && y1 <= 110);

        for (uint8_t floor = 0; floor < INCANTATION_OCCUPATION_FLOORS; floor++) {
            core.station = INCANTATION_OCCUPATION_KEY(floor, DUEL_CIVIC_ACTION_WORK);
            duel_fb_clear(&body);
            civic_resident_draw(&body, &core, side == 0u, DUEL_CIVIC_MODE_NORMAL, 0);
            duel_fb_t object;
            duel_fb_clear(&object);
            incantation_resident_draw_attunement(&object, side == 0u, floor);
            for (int y = 0; y < DUEL_CANVAS_H; y++)
                for (int x = 0; x < DUEL_CANVAS_W; x++)
                    EXPECT(!(duel_fb_get(&body, x, y) && duel_fb_get(&object, x, y)));

            duel_fb_t room;
            render_floor_scene(floor, side == 0u, 0u, &room);
            EXPECT(framebuffer_pixels(&room) > framebuffer_pixels(&body) * 2u);
        }
    }
    CHECK(ok, "incantation_resident_5x14_core_bounds_negative_space_and_object_mass");
}

/* v12 wire-compression canary: the planned descriptor repack drops the
 * interaction bits by substituting SOLID for COMBINE on the slave, which is
 * sound only while no renderer path draws COMBINE differently (the sole
 * non-authoritative interaction read is the INTERACT_PHASE portal). This
 * pins that spike result: it fails the moment anyone adds a COMBINE visual. */

void run_civic_presentation_tests(void) {
    test_resident_occupation_derivation();
    test_resident_geometry_and_object_separation();
    test_civic_anchor_and_courier_matrix();
    test_rare_event_floor_phase_mode_target_matrix();
    test_aftermath_floor_kind_phase_half_matrix();
}
