#include "test_harness.h"

static void test_runtime_mailbox_policy(void) {
    duel_mailbox_t box = {0};
    uint8_t seen = 0, out[8] = {0};
    const uint8_t first[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    const uint8_t latest[8] = {8, 7, 6, 5, 4, 3, 2, 1};
    duel_mailbox_publish(&box, first, sizeof first);
    bool ok = true;
    EXPECT(duel_mailbox_consume(&box, &seen, out, sizeof out) &&
           memcmp(out, first, sizeof out) == 0 &&
           !duel_mailbox_consume(&box, &seen, out, sizeof out));
    box.version++; /* writer in progress: a partial/torn value is never read */
    box.data[0] = 0xffu;
    EXPECT(!duel_mailbox_consume(&box, &seen, out, sizeof out));
    box.version++;
    EXPECT(duel_mailbox_consume(&box, &seen, out, sizeof out) && out[0] == 0xffu);
    duel_mailbox_publish(&box, first, sizeof first);
    duel_mailbox_publish(&box, latest, sizeof latest);
    EXPECT(duel_mailbox_consume(&box, &seen, out, sizeof out) &&
           memcmp(out, latest, sizeof out) == 0);
    box.version = 254u;
    seen = 254u;
    duel_mailbox_publish(&box, first, sizeof first); /* version wraps to zero */
    EXPECT(box.version == 0u && duel_mailbox_consume(&box, &seen, out, sizeof out) && seen == 0u &&
           memcmp(out, first, sizeof out) == 0);
    CHECK(ok, "runtime_mailbox_stable_odd_torn_retry_latest_wins_and_wrap");
}

static void test_runtime_tx_policy(void) {
    duel_tx_policy_t tx = {0};
    bool ok = true;
    EXPECT(duel_tx_attempt(&tx, 1000u, false, false, true) && tx.sequence == 1u);
    duel_tx_commit(&tx, 1000u);
    EXPECT(!duel_tx_attempt(&tx, 1079u, false, false, true) && tx.sequence == 2u);
    EXPECT(duel_tx_attempt(&tx, 1080u, false, false, true));
    duel_tx_commit(&tx, 1080u);
    EXPECT(!duel_tx_attempt(&tx, 1329u, false, false, false));
    EXPECT(duel_tx_attempt(&tx, 1330u, false, false, false));
    duel_tx_commit(&tx, 1330u);
    EXPECT(!duel_tx_attempt(&tx, 1331u, true, false, false));
    EXPECT(!duel_tx_attempt(&tx, 1332u, false, true, false));
    EXPECT(duel_tx_attempt(&tx, 1410u, true, false, false));
    EXPECT(duel_tx_attempt(&tx, 1411u, false, true, false));
    tx.have_sent = true;
    tx.last_sent_ms = UINT32_MAX - 39u;
    EXPECT(!duel_tx_attempt(&tx, 39u, false, false, true));
    EXPECT(duel_tx_attempt(&tx, 40u, false, false, true));
    tx.sequence = UINT16_MAX;
    (void)duel_tx_attempt(&tx, 41u, true, false, false);
    EXPECT(tx.sequence == 0u);
    CHECK(ok, "runtime_tx_urgent_semantic_repair_boundaries_sequence_and_timer_wrap");
}

static void test_runtime_presentation_policy(void) {
    duel_floor_policy_t floor = {0};
    bool ok = true;
    EXPECT(duel_floor_note_target(&floor, DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_COMMONS, 0, 0), 100u,
                                  DUEL_DISPLAY_ACTIVE));
    EXPECT(duel_floor_note_target(&floor, DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_RESEARCH, 0, 0), 200u,
                                  DUEL_DISPLAY_ACTIVE));
    EXPECT(INCANTATION_FLOOR_TRANSITION_ACTIVE(duel_floor_presentation(&floor, 349u)) &&
           INCANTATION_FLOOR_TRANSITION_PHASE(duel_floor_presentation(&floor, 350u)) == 1u);
    EXPECT(duel_floor_note_target(&floor, DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_SPECIAL, 0, 0), 400u,
                                  DUEL_DISPLAY_ACTIVE) &&
           floor.source == DUEL_CIVIC_FLOOR_RESEARCH);
    EXPECT(!INCANTATION_FLOOR_TRANSITION_ACTIVE(duel_floor_presentation(&floor, 1000u)));
    EXPECT(duel_floor_note_target(&floor, DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_WORKSHOP, 0, 0), 1100u,
                                  DUEL_DISPLAY_SLEEP) &&
           !floor.active);

    duel_flash_policy_t flash = {0};
    EXPECT(duel_flash_note(&flash, 1u, FX_IMPACT_L, ELEM_EMBER, UINT32_MAX - 99u) &&
           duel_flash_remaining(&flash, 400u) == 2u && duel_flash_remaining(&flash, 500u) == 0u);
    EXPECT(duel_flash_note(&flash, 2u, FX_WARD_SHATTER_R, ELEM_FORCE, 1000u) &&
           duel_flash_remaining(&flash, 1399u) == 1u && duel_flash_remaining(&flash, 1400u) == 0u);

    uint32_t grace = 19u;
    EXPECT(duel_wake_grace_active(&grace, UINT32_MAX - 20u) && duel_wake_grace_active(&grace, 0u) &&
           !duel_display_should_follow(DUEL_DISPLAY_SLEEP, &grace, 0u) &&
           duel_display_should_follow(DUEL_DISPLAY_SLEEP, &grace, 20u) && grace == 0u);
    CHECK(ok, "runtime_floor_restart_sleep_snap_flash_deadlines_wake_grace_and_follow");
}

// OLED power policy: the only sim module previously without a direct test.
// Timer arithmetic is wrap-safe unsigned age, so a wrap boundary case is
// included alongside the DIM/SLEEP thresholds, key wake, follow, and fade.
static void test_display_power_policy(void) {
    bool ok = true;
    duel_display_policy_t d;
    duel_display_init(&d, 1000u);
    EXPECT(d.phase == DUEL_DISPLAY_ACTIVE && d.initialized);

    /* Threshold ticks: one ms early stays, the boundary transitions. */
    EXPECT(duel_display_update(&d, 1000u + DUEL_DISPLAY_DIM_MS - 1u) == DUEL_DISPLAY_ACTIVE);
    EXPECT(duel_display_update(&d, 1000u + DUEL_DISPLAY_DIM_MS) == DUEL_DISPLAY_DIM);
    EXPECT(duel_display_update(&d, 1000u + DUEL_DISPLAY_SLEEP_MS - 1u) == DUEL_DISPLAY_DIM);
    EXPECT(duel_display_update(&d, 1000u + DUEL_DISPLAY_SLEEP_MS) == DUEL_DISPLAY_SLEEP);

    /* A physical key is the sole wake source. */
    duel_display_note_key(&d, 2000000u);
    EXPECT(d.phase == DUEL_DISPLAY_ACTIVE &&
           duel_display_update(&d, 2000001u) == DUEL_DISPLAY_ACTIVE);

    /* Brightness: full while active, ramp during the dim fade, floor after,
     * dark asleep. */
    EXPECT(duel_display_brightness(&d, 2000001u) == DUEL_DISPLAY_ACTIVE_BRIGHTNESS);
    uint32_t dim_at = 2000000u + DUEL_DISPLAY_DIM_MS;
    duel_display_update(&d, dim_at);
    EXPECT(d.phase == DUEL_DISPLAY_DIM &&
           duel_display_brightness(&d, dim_at) == DUEL_DISPLAY_ACTIVE_BRIGHTNESS);
    uint8_t mid_fade = duel_display_brightness(&d, dim_at + DUEL_DISPLAY_FADE_MS / 2u);
    EXPECT(mid_fade < DUEL_DISPLAY_ACTIVE_BRIGHTNESS && mid_fade > DUEL_DISPLAY_DIM_BRIGHTNESS);
    EXPECT(duel_display_brightness(&d, dim_at + DUEL_DISPLAY_FADE_MS) ==
           DUEL_DISPLAY_DIM_BRIGHTNESS);
    duel_display_update(&d, 2000000u + DUEL_DISPLAY_SLEEP_MS);
    EXPECT(duel_display_brightness(&d, 2000000u + DUEL_DISPLAY_SLEEP_MS + 5u) == 0u);

    /* Follow adopts the master's phase; ACTIVE also refreshes the local key
     * clock; out-of-range phases clamp to ACTIVE. */
    duel_display_follow(&d, DUEL_DISPLAY_ACTIVE, 3000000u);
    EXPECT(d.phase == DUEL_DISPLAY_ACTIVE && d.last_key_ms == 3000000u);
    duel_display_follow(&d, (duel_display_phase_t)3, 3000001u);
    EXPECT(d.phase == DUEL_DISPLAY_ACTIVE);
    duel_display_follow(&d, DUEL_DISPLAY_SLEEP, 3000002u);
    EXPECT(d.phase == DUEL_DISPLAY_SLEEP);

    /* ms-clock wrap: a key just before wrap keeps the panel awake across 0. */
    duel_display_note_key(&d, UINT32_MAX - 10u);
    EXPECT(duel_display_update(&d, 5u) == DUEL_DISPLAY_ACTIVE);
    EXPECT(duel_display_update(&d, DUEL_DISPLAY_DIM_MS - 11u) == DUEL_DISPLAY_DIM);

    /* Presentation deadlines share the same wrap-safe idiom. */
    EXPECT(duel_presentation_remaining(UINT32_MAX - 99u, DUEL_PRESENTATION_IMPACT_MS, 400u) == 2u);
    EXPECT(duel_presentation_remaining(0u, DUEL_PRESENTATION_OTHER_MS,
                                       DUEL_PRESENTATION_OTHER_MS) == 0u);
    EXPECT(duel_presentation_remaining(0u, DUEL_PRESENTATION_OTHER_MS, 1u) == 8u);
    CHECK(ok, "display_power_policy_thresholds_wake_follow_fade_and_wrap");
}

// Matrix -> sim_inputs_t sampling (moved out of keymap.c): the scry chord,
// per-side held positions, and the physical spell-layer policy.
static void test_runtime_input_sampling(void) {
    bool ok = true;
    uint16_t rows[DUEL_INPUT_ROWS] = {0};
    sim_inputs_t in = duel_inputs_from_rows(rows);
    EXPECT(in.down_mask == 0 && in.scry_mask == 0 && in.layer[0] == 0 && in.layer[1] == 0 &&
           !in.held_pos[0] && !in.held_pos[1]);

    /* An ordinary key: left down mask, position bit, OTHER (kills the chord). */
    rows[1] = 1u << 2;
    in = duel_inputs_from_rows(rows);
    EXPECT(in.down_mask == 1u && in.held_pos[0] == (1u << (1u * 6u + 2u)) && in.held_pos[1] == 0 &&
           in.scry_mask == SCRY_M_OTHER && in.layer[0] == 0);

    /* Lone left layer thumb: spell layer 1 on the left wizard only. */
    memset(rows, 0, sizeof rows);
    rows[SCRY_KEY_L_ROW] = 1u << SCRY_KEY_L_COL;
    in = duel_inputs_from_rows(rows);
    EXPECT(in.scry_mask == SCRY_M_L && in.layer[0] == 1u && in.layer[1] == 0u &&
           in.down_mask == 1u);

    /* Lone right layer thumb: spell layer 2 on the right wizard only. */
    memset(rows, 0, sizeof rows);
    rows[SCRY_KEY_R_ROW] = 1u << SCRY_KEY_R_COL;
    in = duel_inputs_from_rows(rows);
    EXPECT(in.scry_mask == SCRY_M_R && in.layer[0] == 0u && in.layer[1] == 2u &&
           in.down_mask == 2u && in.held_pos[1] == (1u << (3u * 6u + 4u)));

    /* The deliberate two-thumb chord: layer 3 for both, no OTHER. */
    rows[SCRY_KEY_L_ROW] = 1u << SCRY_KEY_L_COL;
    in = duel_inputs_from_rows(rows);
    EXPECT(in.scry_mask == (SCRY_M_L | SCRY_M_R) && in.layer[0] == 3u && in.layer[1] == 3u &&
           in.down_mask == 3u);

    /* Chord plus any other key = ordinary layer-3 use: OTHER set. */
    rows[0] = 1u;
    in = duel_inputs_from_rows(rows);
    EXPECT(in.scry_mask == (SCRY_M_L | SCRY_M_R | SCRY_M_OTHER));
    CHECK(ok, "runtime_input_sampling_scry_chord_positions_and_spell_layers");
}

static void test_runtime_tick_budget(void) {
    bool ok = true;
    uint32_t next = 1000u;
    bool resynced = true;
    EXPECT(duel_tick_budget(&next, 999u, &resynced) == 0u && !resynced && next == 1000u);
    EXPECT(duel_tick_budget(&next, 1000u, &resynced) == 1u && !resynced &&
           next == 1000u + SIM_TICK_MS);
    /* Missed two deadlines: catch up by replaying, deadline stays on grid. */
    EXPECT(duel_tick_budget(&next, 1000u + 3u * SIM_TICK_MS, &resynced) == 3u && !resynced &&
           next == 1000u + 4u * SIM_TICK_MS);
    /* Long stall (USB suspend): capped at DUEL_TICK_CATCHUP_MAX and resynced
     * to now + one tick instead of replaying history. */
    next = 2000u;
    EXPECT(duel_tick_budget(&next, 2000u + 10u * SIM_TICK_MS, &resynced) == DUEL_TICK_CATCHUP_MAX &&
           resynced && next == 2000u + 11u * SIM_TICK_MS);
    /* 49.7-day wrap boundary: a deadline just before wrap still fires. */
    next = UINT32_MAX - 10u;
    EXPECT(duel_tick_budget(&next, 5u, &resynced) == 1u && !resynced &&
           next == (uint32_t)(UINT32_MAX - 10u + SIM_TICK_MS));
    CHECK(ok, "runtime_tick_budget_catchup_stall_resync_and_wrap");
}

static void test_runtime_slave_presenter(void) {
    bool ok = true;
    duel_slave_presenter_t pres = {0};
    /* Nothing ever received: local fallback, quiet while idle. */
    duel_slave_decision_t d = duel_slave_present(&pres, false, false, false, false, false, false);
    EXPECT(!d.use_remote && !d.base_refresh && !d.set_stale);
    /* Local ticks re-render the fallback. */
    d = duel_slave_present(&pres, false, false, false, true, false, false);
    EXPECT(!d.use_remote && d.base_refresh && !d.set_stale);
    /* First accepted snapshot: adopt remote, follow the master's phase. */
    d = duel_slave_present(&pres, true, true, false, false, false, false);
    EXPECT(d.use_remote && d.consider_follow && d.base_refresh);
    /* Steady remote with no new packet: nothing to redo. */
    d = duel_slave_present(&pres, false, true, false, false, false, false);
    EXPECT(d.use_remote && !d.consider_follow && !d.base_refresh);
    /* Link goes stale: fall back once, marking the render stale. */
    d = duel_slave_present(&pres, false, true, true, false, false, false);
    EXPECT(!d.use_remote && d.base_refresh && d.set_stale);
    /* Still stale and already presented as such: quiet until a tick. */
    d = duel_slave_present(&pres, false, true, true, false, false, true);
    EXPECT(!d.use_remote && !d.base_refresh);
    d = duel_slave_present(&pres, false, true, true, true, false, true);
    EXPECT(!d.use_remote && d.base_refresh && d.set_stale);
    /* Re-acquire: fresh acceptance clears stale, re-follows, re-presents. */
    d = duel_slave_present(&pres, true, true, false, false, false, true);
    EXPECT(d.use_remote && d.consider_follow && d.base_refresh);
    /* A render invalidation alone (local keypress) also re-presents remote. */
    d = duel_slave_present(&pres, false, true, false, false, true, false);
    EXPECT(d.use_remote && !d.consider_follow && d.base_refresh);
    CHECK(ok, "runtime_slave_presenter_fallback_stale_edge_and_reacquire");
}

static void test_runtime_civic_shared_derive(void) {
    bool ok = true;
    sim_world_t world;
    sim_init(&world, SIMF_AUTHORITATIVE, 0);
    duel_host_state_t host = {0};
    /* Offline host: no visitor is derived; the rare-event deck still runs
     * (both champions standing => eligible). */
    duel_civic_shared_t calm = duel_civic_shared_derive(0x5au, 1900u, &host, &world, 0);
    EXPECT(DUEL_VISITOR_KIND(calm.shared_pres) == DUEL_CIVIC_COURIER_NONE &&
           !(calm.revision & INCANTATION_AFTERMATH_WIRE));
    /* Safety gate (spec §14.1): a downed champion empties the event slot at
     * every civic phase. */
    world.wiz[SIM_SIDE_L].life = LIFE_DOWNED;
    for (uint32_t phase = 0; phase < 256u; phase++) {
        duel_civic_shared_t gated =
            duel_civic_shared_derive(0x5au, phase * DUEL_CIVIC_TICK_MS, &host, &world, 0);
        EXPECT(DUEL_EVENT_ID(gated.revision) == DUEL_CIVIC_EVENT_NONE);
    }
    world.wiz[SIM_SIDE_L].life = LIFE_ACTIVE;
    /* A live aftermath owns both coordination bytes (bit7 discriminator). */
    world.aftermath[0].kind = AFTER_CHEER;
    world.aftermath[0].ticks = 50u;
    world.aftermath[0].intensity = 2u;
    duel_civic_shared_t owned = duel_civic_shared_derive(0x5au, 1900u, &host, &world, 0);
    EXPECT((owned.revision & INCANTATION_AFTERMATH_WIRE) &&
           owned.shared_pres == incantation_aftermath_shared(&world) &&
           owned.revision == incantation_aftermath_revision(&world));
    CHECK(ok, "runtime_civic_shared_offline_safety_gate_and_aftermath_override");
}

static void test_runtime_flash_observe(void) {
    bool ok = true;
    sim_world_t world;
    sim_init(&world, SIMF_AUTHORITATIVE, 0);
    /* Left slot carries a live spell; the world then reports a right-side
     * impact: the flash must scale from the LEFT slot's cached style. */
    world.spell[SIM_SIDE_L].active = 1;
    world.spell[SIM_SIDE_L].descriptor =
        SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_EMBER, PAY_DAMAGE, TRAJ_MID, 3, STATUS_NONE,
                        INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, 2);
    world.spell[SIM_SIDE_L].progress = 100u;
    duel_view_t view;
    duel_view_from_world(&world, &view);
    duel_flash_policy_t flash = {0};
    uint8_t last_kind[2] = {0, 0};
    EXPECT(!duel_flash_observe_view(&flash, last_kind, &view, 100u) &&
           DUEL_KIND_ELEMENT(last_kind[SIM_SIDE_L]) == ELEM_EMBER);
    world.spell[SIM_SIDE_L].active = 0;
    world.spell[SIM_SIDE_L].descriptor = 0;
    world.fx_seq = 1u;
    world.fx_kind = FX_IMPACT_R;
    duel_view_from_world(&world, &view);
    EXPECT(duel_flash_observe_view(&flash, last_kind, &view, 200u) && flash.kind == FX_IMPACT_R &&
           flash.spell_kind == last_kind[SIM_SIDE_L] &&
           flash.duration_ms == DUEL_PRESENTATION_IMPACT_MS);
    /* The same fx_seq never re-arms. */
    EXPECT(!duel_flash_observe_view(&flash, last_kind, &view, 300u));
    CHECK(ok, "runtime_flash_observe_caches_style_and_arms_defender_flash");
}

static void test_runtime_sky_and_diplomacy(void) {
    bool ok = true;
    EXPECT(duel_sky_phase(0u) == DUEL_SKY_DAWN && duel_sky_phase(149999u) == DUEL_SKY_DAWN &&
           duel_sky_phase(150000u) == DUEL_SKY_DAY && duel_sky_phase(1349999u) == DUEL_SKY_DAY &&
           duel_sky_phase(1350000u) == DUEL_SKY_DUSK && duel_sky_phase(1499999u) == DUEL_SKY_DUSK &&
           duel_sky_phase(1500000u) == DUEL_SKY_NIGHT &&
           duel_sky_phase(1799999u) == DUEL_SKY_NIGHT && duel_sky_phase(1800000u) == DUEL_SKY_DAWN);
    sim_world_t world;
    sim_init(&world, SIMF_AUTHORITATIVE, 0);
    duel_snapshot_t master;
    test_encode_snapshot(&world, 7u, 500u, &master);
    duel_snapshot_set_civic(&master, 0u, DUEL_SECONDARY_SKY_PACK(0u, DUEL_SKY_NIGHT), 0u, 0u);
    duel_rx_state_t rx = {0};
    EXPECT(duel_decode_valid(&master) && duel_rx_accept(&rx, &master, false) &&
           DUEL_SECONDARY_SKY_PHASE(rx.last.secondary) == DUEL_SKY_NIGHT);
    /* A stale half may be in its own dawn; stale adoption takes the live
     * master's current phase even when a collided session has a lower seq. */
    duel_snapshot_set_civic(&master, 0u, DUEL_SECONDARY_SKY_PACK(0u, DUEL_SKY_DAY), 0u, 0u);
    master.seq = 1u;
    master.crc = duel_crc8(&master, offsetof(duel_snapshot_t, crc));
    EXPECT(duel_sky_phase(0u) == DUEL_SKY_DAWN && duel_rx_accept(&rx, &master, true) &&
           DUEL_SECONDARY_SKY_PHASE(rx.last.secondary) == DUEL_SKY_DAY);
    duel_diplomacy_t dip;
    duel_diplomacy_init(&dip);
    EXPECT(!duel_diplomacy_update(&dip, LIFE_ACTIVE, LIFE_ACTIVE));
    EXPECT(duel_diplomacy_update(&dip, LIFE_ACTIVE, LIFE_COLLAPSE) && dip.balance == 1);
    EXPECT(!duel_diplomacy_update(&dip, LIFE_ACTIVE, LIFE_DOWNED) && dip.balance == 1);
    duel_diplomacy_update(&dip, LIFE_ACTIVE, LIFE_ACTIVE);
    for (int i = 0; i < 5; i++) {
        duel_diplomacy_update(&dip, LIFE_ACTIVE, LIFE_COLLAPSE);
        duel_diplomacy_update(&dip, LIFE_ACTIVE, LIFE_ACTIVE);
    }
    EXPECT(dip.balance == 3 && duel_diplomacy_target(&dip) == DUEL_DIPLOMACY_LEFT_ADVANTAGE);
    duel_diplomacy_init(&dip);
    EXPECT(dip.balance == 0 && duel_diplomacy_target(&dip) == DUEL_DIPLOMACY_BALANCED);
    CHECK(ok, "runtime_sky_boundaries_wrap_and_diplomacy_ko_edge_saturation_reset");
}

static void test_observatory_sky_and_suppression(void) {
    sim_world_t world;
    sim_init(&world, SIMF_AUTHORITATIVE, 0);
    duel_render_t base = {0};
    duel_render_from_world(&base, &world);
    base.seed = 0x5au;
    base.civic_phase = 19u;
    base.civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_SPECIAL, DUEL_CIVIC_MODE_QUIET, 0);
    base.alert =
        DUEL_HOST_ALERT_PACK(DUEL_HOST_CATEGORY_COMMUNICATION, DUEL_HOST_PRIORITY_CRITICAL, 2);

    duel_fb_t clean, disposable;
    duel_fb_clear(&clean);
    duel_scene_draw(&clean, &base, true, 7u, false);
    duel_render_t noisy = base;
    noisy.shared_pres =
        (uint8_t)(DUEL_VISITOR_PACK(DUEL_CIVIC_COURIER_BEACON, 0, DUEL_CIVIC_VISIT_WAITING) |
                  DUEL_VISITOR_DENSITY_PACK(DUEL_CIVIC_DENSITY_MANY));
    noisy.revision = DUEL_EVENT_PACK(DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER,
                                     DUEL_CIVIC_EVENT_PHASE_ACTIVE, DUEL_CIVIC_EVENT_TARGET_LEFT);
    noisy.local_ambience = INCANTATION_AMBIENCE_PACK(true, TEMPO_FRANTIC, TREND_IRREGULAR);
    duel_fb_clear(&disposable);
    duel_scene_draw(&disposable, &noisy, true, 7u, false);
    bool ok = true;
    EXPECT(memcmp(clean.bits, disposable.bits, sizeof clean.bits) == 0);

    duel_fb_t phase_fb[4];
    for (uint8_t phase = DUEL_SKY_DAWN; phase <= DUEL_SKY_NIGHT; phase++) {
        duel_render_t sky = base;
        sky.secondary = DUEL_SECONDARY_SKY_PACK(0, phase);
        duel_fb_clear(&phase_fb[phase]);
        duel_scene_draw(&phase_fb[phase], &sky, true, 7u, false);
        if (phase)
            EXPECT(memcmp(phase_fb[phase - 1].bits, phase_fb[phase].bits,
                          sizeof phase_fb[phase].bits) != 0);
    }
    /* M15 contract: the sky may repaint the upper band (celestial arc, tower
     * window lighting), but the deck, the room interior, and everything
     * below — through the stone course to the canvas bottom — must be
     * bit-identical across phases. */
    for (int y = DUEL_DECK_Y0; y < DUEL_CANVAS_H; y++) {
        for (int x = 0; x < DUEL_CANVAS_W; x++)
            EXPECT(duel_fb_get(&phase_fb[DUEL_SKY_DAWN], x, y) ==
                   duel_fb_get(&phase_fb[DUEL_SKY_NIGHT], x, y));
    }
    CHECK(ok, "observatory_distinct_sky_protected_regions_and_disposable_ambience_suppression");
}

static bool rgb_is(duel_rgb_t color, uint8_t r, uint8_t g, uint8_t b) {
    return color.r == r && color.g == g && color.b == b;
}

static void test_rgb_world_surface_policy(void) {
    duel_rgb_world_t world = {.display_phase = DUEL_DISPLAY_ACTIVE};
    bool ok = true;
    EXPECT(rgb_is(duel_rgb_policy(&world, DUEL_RGB_LED_UNDERGLOW, true), 6, 0, 18) &&
           rgb_is(duel_rgb_policy(&world, DUEL_RGB_LED_UNDERGLOW, false), 18, 6, 0) &&
           rgb_is(duel_rgb_policy(&world, DUEL_RGB_LED_KEYLIGHT, true), 0, 0, 0));
    world.observatory = true;
    EXPECT(rgb_is(duel_rgb_policy(&world, DUEL_RGB_LED_UNDERGLOW, true), 4, 0, 20));
    world.prepared[SIM_SIDE_L] = true;
    for (uint8_t element = ELEM_FORCE; element <= ELEM_VOID; element++) {
        static const duel_rgb_t expected[4] = {
            {12, 12, 16},
            {24, 3, 0},
            {0, 10, 24},
            {8, 0, 20},
        };
        world.prepared_element[SIM_SIDE_L] = element;
        duel_rgb_t color = duel_rgb_policy(&world, DUEL_RGB_LED_KEYLIGHT, true);
        EXPECT(rgb_is(color, expected[element].r, expected[element].g, expected[element].b));
        EXPECT(rgb_is(duel_rgb_policy(&world, DUEL_RGB_LED_KEYLIGHT, false), 0, 0, 0));
    }
    world.flash_active = true;
    world.flash_kind = FX_WARD_SHATTER_L;
    EXPECT(rgb_is(duel_rgb_policy(&world, DUEL_RGB_LED_KEYLIGHT, true), 20, 20, 32) &&
           !rgb_is(duel_rgb_policy(&world, DUEL_RGB_LED_KEYLIGHT, false), 20, 20, 32));
    world.flash_kind = FX_IMPACT_R;
    EXPECT(rgb_is(duel_rgb_policy(&world, DUEL_RGB_LED_UNDERGLOW, false), 32, 0, 0));
    world.stale = true;
    EXPECT(rgb_is(duel_rgb_policy(&world, DUEL_RGB_LED_UNDERGLOW, true), 0, 4, 10) &&
           rgb_is(duel_rgb_policy(&world, DUEL_RGB_LED_KEYLIGHT, true), 0, 0, 0));
    world.display_phase = DUEL_DISPLAY_DIM;
    EXPECT(rgb_is(duel_rgb_policy(&world, DUEL_RGB_LED_UNDERGLOW, true), 0, 1, 2));
    world.display_phase = DUEL_DISPLAY_SLEEP;
    EXPECT(rgb_is(duel_rgb_policy(&world, DUEL_RGB_LED_UNDERGLOW, true), 0, 0, 0));
    CHECK(ok, "rgb_world_surface_all_priorities_sides_elements_dim_sleep_and_stale");
}

static void test_live_ambience_classifier(void) {
    bool ok = true;
    static const struct {
        uint16_t sum;
        uint8_t count, first, last, min, max;
    } cases[] = {
        {12, 2, 6, 6, 6, 6}, {8, 2, 5, 3, 3, 5}, {4, 2, 1, 3, 1, 3},
        {2, 2, 1, 1, 1, 1},  {8, 3, 2, 2, 1, 6},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        sim_incantation_t inc = {0};
        inc.key_count = 3;
        inc.row_hist[1] = 3;
        inc.hash = (uint32_t)(0x1234u + i);
        inc.gap_sum = cases[i].sum;
        inc.gap_count = cases[i].count;
        inc.first_gap = cases[i].first;
        inc.last_gap = cases[i].last;
        inc.gap_min = cases[i].min;
        inc.gap_max = cases[i].max;
        uint8_t live = incantation_tempo_trend(&inc);
        uint32_t desc = incantation_compile(&inc, 0, SIM_TEMPER_NEUTRAL);
        EXPECT(INCANTATION_AMBIENCE_TEMPO(live) == SPELL_DESC_TEMPO(desc) &&
               INCANTATION_AMBIENCE_TREND(live) == SPELL_DESC_TREND(desc));
        sim_wizard_t wizard = {.inc = inc, .inc_state = INC_COLLECTING};
        EXPECT(incantation_local_ambience(&wizard) == live);
        wizard.inc_state = INC_WINDUP;
        wizard.pending_desc = desc;
        EXPECT(INCANTATION_AMBIENCE_TEMPO(incantation_local_ambience(&wizard)) ==
               SPELL_DESC_TEMPO(desc));
        wizard.inc_state = INC_IDLE;
        EXPECT(incantation_local_ambience(&wizard) == 0u);
    }
    sim_wizard_t halves[2] = {{0}};
    halves[0].inc_state = halves[1].inc_state = INC_COLLECTING;
    halves[0].inc.gap_count = halves[1].inc.gap_count = 2;
    halves[0].inc.gap_sum = 2;
    halves[1].inc.gap_sum = 12;
    EXPECT(INCANTATION_AMBIENCE_TEMPO(incantation_local_ambience(&halves[0])) == TEMPO_FRANTIC &&
           INCANTATION_AMBIENCE_TEMPO(incantation_local_ambience(&halves[1])) == TEMPO_DELIBERATE);
    CHECK(ok, "ambience_live_compiler_equivalence_launch_calm_and_per_half_independence");
}

static void test_diplomatic_weight_target_and_combat_independence(void) {
    unsigned balanced = 0, imbalanced = 0;
    bool ok = true, saw_left = false, saw_right = false, saw_balanced = false;
    sim_world_t world;
    sim_init(&world, SIMF_AUTHORITATIVE, 0);
    uint64_t before = incantation_bytes_hash(&world, sizeof world);
    for (unsigned seed = 0; seed < 256u; seed++) {
        for (uint8_t cycle = 0; cycle < 16u; cycle++) {
            uint8_t phase = (uint8_t)(cycle * 8u + 2u);
            civic_event_state_t zero = civic_event_derive((uint8_t)seed, phase, true, 0);
            civic_event_state_t left = civic_event_derive((uint8_t)seed, phase, true, 3);
            civic_event_state_t right = civic_event_derive((uint8_t)seed, phase, true, -3);
            balanced += (zero.id_target & 7u) == DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER;
            imbalanced += (left.id_target & 7u) == DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER;
            if ((zero.id_target & 7u) == DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER)
                saw_balanced |= ((zero.id_target >> 5) & 3u) == DUEL_CIVIC_EVENT_TARGET_SHARED;
            if ((left.id_target & 7u) == DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER)
                saw_left |= ((left.id_target >> 5) & 3u) == DUEL_CIVIC_EVENT_TARGET_LEFT;
            if ((right.id_target & 7u) == DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER)
                saw_right |= ((right.id_target >> 5) & 3u) == DUEL_CIVIC_EVENT_TARGET_RIGHT;
        }
    }
    EXPECT(imbalanced > balanced && saw_left && saw_right && saw_balanced &&
           incantation_bytes_hash(&world, sizeof world) == before);
    CHECK(ok, "diplomacy_weight_4_plus_balance_targeting_and_zero_combat_influence");
}

void run_runtime_display_rgb_tests(void) {
    test_runtime_mailbox_policy();
    test_runtime_tx_policy();
    test_display_power_policy();
    test_runtime_input_sampling();
    test_runtime_tick_budget();
    test_runtime_slave_presenter();
    test_runtime_civic_shared_derive();
    test_runtime_flash_observe();
    test_runtime_presentation_policy();
    test_runtime_sky_and_diplomacy();
    test_observatory_sky_and_suppression();
    test_rgb_world_surface_policy();
    test_live_ambience_classifier();
    test_diplomatic_weight_target_and_combat_independence();
}
