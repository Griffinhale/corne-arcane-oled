#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "duel_draw.h"
#include "duel_host.h"
#include "duel_proto.h"
#include "duel_resident.h"
#include "duel_sim.h"
#include "duel_view.h"

static int failures;
#define CHECK(condition, name) do { \
    if (condition) printf("PASS %s\n", name); \
    else { printf("FAIL %s (%s:%d)\n", name, __FILE__, __LINE__); failures++; } \
} while (0)

static void install_spell(sim_world_t *w, uint8_t side, uint32_t desc, uint8_t progress);

static uint32_t desc_set_magnitude_for_test(uint32_t desc, uint8_t magnitude) {
    return (desc & ~(3u << 10)) | ((uint32_t)(magnitude - 1u) << 10);
}

static void step(sim_world_t *w, uint32_t left, uint32_t right,
                 uint8_t llayer, uint8_t rlayer,
                 const sim_event_t *events, uint8_t n) {
    sim_inputs_t in = {0};
    in.held_pos[0] = left;
    in.held_pos[1] = right;
    in.layer[0] = llayer;
    in.layer[1] = rlayer;
    if (left) in.down_mask |= 1u;
    if (right) in.down_mask |= 2u;
    sim_tick(w, in, events, n, 0);
}

static void tap(sim_world_t *w, uint8_t side, uint8_t row, uint8_t col, uint8_t layer) {
    sim_event_t event = SIM_EV_PACK(SIM_EV_KEYDOWN, side, row, col);
    uint32_t held = 1u << (row * 6u + col);
    step(w, side ? 0 : held, side ? held : 0, side ? 0 : layer, side ? layer : 0, &event, 1);
    step(w, 0, 0, 0, 0, NULL, 0);
}

static void wait_ticks(sim_world_t *w, unsigned ticks) {
    while (ticks--) step(w, 0, 0, 0, 0, NULL, 0);
}

static void release_recipe(sim_world_t *w, uint8_t side, uint8_t row) {
    tap(w, side, row, 1, 0);
    wait_ticks(w, M13_IDLE_COMMIT_TICKS - 1u);
    for (unsigned guard = 0; guard < M13_WINDUP_MAX_TICKS + 2u && !w->spell[side].active; guard++)
        step(w, 0, 0, 0, 0, NULL, 0);
}

static void test_layout_and_protocol(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    duel_snapshot_t packet;
    duel_encode_external_alert_display(&w, 9, 0x1234, 0x55, 0x2a, 2, &packet);
    duel_snapshot_set_civic(&packet, 1, 2, 3, 4);
    bool ok = sizeof(duel_view_t) == 19 && sizeof(duel_snapshot_t) == 32 &&
              DUEL_VER == 10 && sizeof(sim_world_t) <= 56u + 1024u &&
              duel_decode_valid(&packet);
    duel_snapshot_t bad = packet;
    for (size_t i = 0; i < offsetof(duel_snapshot_t, crc); i++) {
        ((uint8_t *)&bad)[i] ^= 0x40u;
        ok &= !duel_decode_valid(&bad);
        bad = packet;
    }
    bad.ver = 9;
    bad.crc = duel_crc8(&bad, offsetof(duel_snapshot_t, crc));
    ok &= !duel_decode_valid(&bad);
    CHECK(ok, "m13_v10_exact_size_crc_and_version_rejection");
}

static void test_view_validation(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[0].ward_strength = 4;
    w.wiz[0].status = STATUS_FROZEN;
    w.wiz[0].status_intensity = 3;
    w.wiz[0].status_ticks = 125;
    w.spell[0].active = 1;
    w.spell[0].descriptor = SPELL_DESC_PACK(SPELL_BEAM, ELEM_FROST, PAY_HYBRID,
                                             TRAJ_AREA, 4, STATUS_FROZEN,
                                             INTERACT_SOLID, TEMPO_RAPID,
                                             TREND_ACCELERATING, 2);
    w.spell[0].progress = 91;
    duel_view_t view;
    duel_view_from_world(&w, &view);
    bool ok = duel_view_valid(&view);
    duel_view_spell_t spell = duel_view_spell(&view, 0);
    duel_view_wizard_t wizard = duel_view_wizard(&view, 0);
    ok &= spell.active && spell.descriptor == w.spell[0].descriptor && spell.progress == 91;
    ok &= wizard.hp == 12 && wizard.ward_strength == 4 && wizard.status == STATUS_FROZEN;
    duel_view_t bad = view;
    bad.wizard[0][0] = (uint8_t)((bad.wizard[0][0] & 0xf0u) | 13u);
    ok &= !duel_view_valid(&bad);
    bad = view; bad.wizard[0][0] = (uint8_t)((bad.wizard[0][0] & 0x8fu) | (5u << 4));
    ok &= !duel_view_valid(&bad);
    bad = view; bad.outcome_overlay |= 0x80u;
    ok &= !duel_view_valid(&bad);
    bad = view; memset(bad.spell[1], 0, 3); bad.spell[1][3] = 1;
    ok &= !duel_view_valid(&bad);
    bad = view; bad.spell[0][1] |= 0x70u; /* reserved status encoding */
    ok &= !duel_view_valid(&bad);
    CHECK(ok, "m13_view_roundtrip_and_reserved_validation");
}

static void test_complexity_formula(void) {
    sim_incantation_t inc = {0};
    inc.key_count = 70;
    inc.seen_pos = 0xffffu;
    inc.turns = 20;
    inc.layer_transitions = 9;
    inc.overlap_peak = 5;
    inc.rhythm_changes = 9;
    bool ok = m13_complexity(&inc) == 255;
    memset(&inc, 0, sizeof inc);
    inc.key_count = 3;             /* 6 */
    inc.seen_pos = 7;              /* 9 */
    inc.turns = 2;                 /* 4 */
    inc.layer_transitions = 1;     /* 4 */
    inc.overlap_peak = 2;          /* 8 */
    inc.rhythm_changes = 1;        /* 3 = 34 */
    ok &= m13_complexity(&inc) == 34;
    CHECK(ok, "m13_complexity_exact_and_clamped");
}

static sim_incantation_t incantation_at_complexity(uint8_t target) {
    sim_incantation_t inc;
    memset(&inc, 0, sizeof inc);
    inc.hash = 0x6d31332eu;
    inc.gap_min = 1u;
    for (uint8_t keys = 0; keys <= 64u; keys++) {
        for (uint8_t unique = 0; unique <= 16u; unique++) {
            for (uint8_t turns = 0; turns <= 16u; turns++) {
                inc.key_count = keys;
                inc.seen_pos = unique == 0u ? 0u : (1u << unique) - 1u;
                inc.turns = turns;
                if (m13_complexity(&inc) == target) {
                    inc.row_hist[1] = keys ? keys : 1u;
                    inc.row_recent[1] = 1u;
                    return inc;
                }
            }
        }
    }
    inc.key_count = 0xffu; /* impossible sentinel: the test will fail */
    return inc;
}

static void test_magnitude_thresholds(void) {
    static const uint8_t complexity[] = {47u, 48u, 111u, 112u, 191u, 192u};
    static const uint8_t magnitude[] = {1u, 2u, 2u, 3u, 3u, 4u};
    bool ok = true;
    for (size_t i = 0; i < sizeof complexity; i++) {
        sim_incantation_t inc = incantation_at_complexity(complexity[i]);
        ok &= inc.key_count != 0xffu && m13_complexity(&inc) == complexity[i] &&
              SPELL_DESC_MAGNITUDE(m13_compile(&inc, 0)) == magnitude[i];
    }
    sim_incantation_t saturated = {0};
    saturated.hash = 1u; saturated.key_count = 64u; saturated.seen_pos = 0xffffu;
    saturated.turns = 16u; saturated.layer_transitions = 8u;
    saturated.overlap_peak = 5u; saturated.rhythm_changes = 8u;
    saturated.row_hist[1] = 64u;
    ok &= SPELL_DESC_MAGNITUDE(m13_compile(&saturated, 0)) == 4u;
    CHECK(ok, "m13_magnitude_thresholds_48_112_192");
}

static void test_compiler_determinism_and_gates(void) {
    sim_incantation_t inc = {0};
    inc.hash = 0x12345678u;
    inc.key_count = 1;
    inc.seen_pos = 1;
    inc.row_hist[0] = 1;
    inc.row_recent[0] = 1;
    inc.gap_min = 1;
    uint32_t a = m13_compile(&inc, 0), b = m13_compile(&inc, 0);
    bool ok = a == b && SPELL_DESC_FORM(a) == SPELL_PROJECTILE &&
              SPELL_DESC_ELEMENT(a) == ELEM_FROST && (a & 0xff000000u) == 0 &&
              SPELL_DESC_VALID(a);

    /* Complexity 64 opens fireball/swarm but never beam/singularity. */
    memset(&inc, 0, sizeof inc);
    inc.key_count = 8;       /* 16 */
    inc.seen_pos = 0xffffu;  /* 48 => 64 */
    inc.row_hist[1] = 8; inc.row_recent[1] = 1; inc.gap_min = 0;
    bool saw_non_projectile = false;
    for (uint32_t h = 1; h < 500; h++) {
        inc.hash = h * 2654435761u;
        uint8_t form = SPELL_DESC_FORM(m13_compile(&inc, 1));
        ok &= form == SPELL_PROJECTILE || form == SPELL_FIREBALL || form == SPELL_SWARM;
        saw_non_projectile |= form != SPELL_PROJECTILE;
    }
    ok &= saw_non_projectile;
    CHECK(ok, "m13_compiler_determinism_privacy_and_complexity_gate");
}

static void test_compiler_reachability(void) {
    uint32_t forms = 0, elements = 0, payloads = 0, trajectories = 0;
    uint32_t magnitudes = 0, statuses = 0, interactions = 0, tempos = 0, trends = 0;
    for (uint32_t i = 0; i < 8192u; i++) {
        for (uint8_t row = 0; row < 4; row++) {
            sim_incantation_t inc = {0};
            inc.hash = i * 2654435761u + row * 0x9e37u;
            inc.key_count = (uint8_t)(1u + i % 80u);
            uint8_t unique = (uint8_t)(1u + (i / 3u) % 24u);
            inc.seen_pos = unique == 24u ? 0x00ffffffu : ((1u << unique) - 1u);
            inc.turns = (uint8_t)((i / 5u) % 20u);
            inc.layer_transitions = (uint8_t)((i / 7u) % 10u);
            inc.overlap_peak = (uint8_t)(1u + (i / 11u) % 6u);
            inc.rhythm_changes = (uint8_t)((i / 13u) % 10u);
            inc.row_hist[row] = inc.key_count;
            inc.row_recent[row] = 4;
            inc.held_ticks = (i & 8u) ? (uint16_t)inc.key_count * 4u : 0u;
            inc.column_drift = (i & 16u) ? 6 : -2;
            uint8_t avg = (uint8_t)(1u + (i / 17u) % 6u);
            inc.gap_count = 3; inc.gap_sum = (uint16_t)avg * 3u;
            inc.gap_min = avg; inc.gap_max = (i & 32u) ? (uint8_t)(avg + 4u) : avg;
            inc.first_gap = (uint8_t)(avg + ((i >> 6) & 1u));
            inc.last_gap = (uint8_t)(avg + ((i >> 7) & 1u));
            uint32_t desc = m13_compile(&inc, (uint8_t)(i & 3u));
            forms |= 1u << SPELL_DESC_FORM(desc);
            elements |= 1u << SPELL_DESC_ELEMENT(desc);
            payloads |= 1u << SPELL_DESC_PAYLOAD(desc);
            trajectories |= 1u << SPELL_DESC_TRAJECTORY(desc);
            magnitudes |= 1u << (SPELL_DESC_MAGNITUDE(desc) - 1u);
            statuses |= 1u << SPELL_DESC_STATUS(desc);
            interactions |= 1u << SPELL_DESC_INTERACTION(desc);
            tempos |= 1u << SPELL_DESC_TEMPO(desc);
            trends |= 1u << SPELL_DESC_TREND(desc);
        }
    }
    bool ok = forms == 0xffu && elements == 0x0fu && payloads == 0x0fu &&
              trajectories == 0xffu && magnitudes == 0x0fu &&
              (statuses & 0x1fu) == 0x1fu && interactions == 0x0fu &&
              tempos == 0x0fu && trends == 0x0fu;
    CHECK(ok, "m13_initial_descriptor_attribute_reachability");
}

static void test_independent_accumulators_and_commit(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    sim_event_t events[2] = {
        SIM_EV_PACK(SIM_EV_KEYDOWN, 0, 0, 1),
        SIM_EV_PACK(SIM_EV_KEYDOWN, 1, 2, 4),
    };
    step(&w, 1u << 1, 1u << 16, 0, 2, events, 2);
    step(&w, 0, 0, 0, 0, NULL, 0);
    bool ok = w.wiz[0].inc.key_count == 1 && w.wiz[1].inc.key_count == 1 &&
              w.wiz[0].inc.seen_pos == (1u << 1) && w.wiz[1].inc.seen_pos == (1u << 16) &&
              w.wiz[0].inc.hash != w.wiz[1].inc.hash;
    wait_ticks(&w, M13_IDLE_COMMIT_TICKS - 1u);
    ok &= w.wiz[0].inc_state == INC_WINDUP && w.wiz[1].inc_state == INC_WINDUP;
    uint32_t left = w.wiz[0].pending_desc, right = w.wiz[1].pending_desc;
    ok &= SPELL_DESC_ELEMENT(left) == ELEM_FROST && SPELL_DESC_ELEMENT(right) == ELEM_EMBER;
    CHECK(ok, "m13_simultaneous_per_half_idle_commit");
}

static void test_forced_cap_and_rearm(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    sim_event_t event = SIM_EV_PACK(SIM_EV_KEYDOWN, 0, 1, 2);
    uint32_t held = 1u << 8;
    step(&w, held, 0, 0, 0, &event, 1);
    for (unsigned i = 1; i < M13_FORCE_COMMIT_TICKS; i++) step(&w, held, 0, 0, 0, NULL, 0);
    bool ok = w.wiz[0].rearm_lock && w.wiz[0].inc_state == INC_WINDUP;
    uint8_t count = w.wiz[0].inc.key_count;
    sim_event_t ignored = SIM_EV_PACK(SIM_EV_KEYDOWN, 0, 0, 0);
    step(&w, held | 1u, 0, 0, 0, &ignored, 1);
    ok &= w.wiz[0].inc.key_count == count;
    step(&w, 0, 0, 0, 0, NULL, 0);
    ok &= !w.wiz[0].rearm_lock;
    CHECK(ok, "m13_ten_second_force_commit_full_release_rearm");
}

static void test_release_and_prepared(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    release_recipe(&w, 0, 1);
    bool ok = w.spell[0].active && SPELL_DESC_VALID(w.spell[0].descriptor);
    tap(&w, 0, 2, 2, 0);
    wait_ticks(&w, M13_IDLE_COMMIT_TICKS - 1u);
    unsigned guard = 0;
    while (w.wiz[0].inc_state == INC_WINDUP && guard++ < 60) step(&w, 0, 0, 0, 0, NULL, 0);
    ok &= w.wiz[0].inc_state == INC_PREPARED && w.wiz[0].prepared && w.wiz[0].ward_strength;
    while (w.wiz[0].prepared && guard++ < 120) step(&w, 0, 0, 0, 0, NULL, 0);
    ok &= w.spell[0].active && !w.wiz[0].prepared;
    CHECK(ok, "m13_one_active_one_prepared_auto_release");
}

static void test_windup_ignored_input_and_interruption(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    tap(&w, 0, 1, 1, 0);
    wait_ticks(&w, M13_IDLE_COMMIT_TICKS - 1u);
    bool ok = w.wiz[0].inc_state == INC_WINDUP && w.wiz[0].ward_strength == 1;
    uint8_t count = w.wiz[0].inc.key_count;
    sim_event_t ignored = SIM_EV_PACK(SIM_EV_KEYDOWN, 0, 2, 5);
    step(&w, 1u << 17, 0, 3, 0, &ignored, 1);
    ok &= w.wiz[0].inc.key_count == count;
    step(&w, 0, 0, 0, 0, NULL, 0);

    uint32_t hostile = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_VOID, PAY_DAMAGE,
                                       TRAJ_LOW, 2, STATUS_NONE, INTERACT_PHASE,
                                       TEMPO_RAPID, TREND_STEADY, 0);
    install_spell(&w, 1, hostile, 239);
    step(&w, 0, 0, 0, 0, NULL, 0);
    ok &= w.wiz[0].inc_state == INC_IDLE && !w.wiz[0].pending_desc &&
          !w.wiz[0].prepared && !w.wiz[0].ward_strength &&
          !w.wiz[0].ward_capacity;
    CHECK(ok, "m13_windup_input_ignored_and_unblocked_contact_interrupts");
}

static void install_spell(sim_world_t *w, uint8_t side, uint32_t desc, uint8_t progress) {
    sim_spell_t *sp = &w->spell[side];
    memset(sp, 0, sizeof *sp);
    sp->active = 1;
    sp->descriptor = desc;
    sp->progress = progress;
    sp->dir = side ? -1 : 1;
}

static void test_ward_capacity_semantics(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    tap(&w, 0, 1, 1, 0);
    bool ok = w.wiz[0].inc_state == INC_COLLECTING &&
              w.wiz[0].ward_capacity == 1u && w.wiz[0].ward_strength == 1u;

    uint32_t chip = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE,
                                    TRAJ_MID, 1, STATUS_NONE, INTERACT_SOLID,
                                    TEMPO_FLOWING, TREND_STEADY, 0);
    install_spell(&w, 1, chip, 239);
    step(&w, 0, 0, 0, 0, NULL, 0);
    ok &= w.wiz[0].hp == SIM_MAX_HP && w.wiz[0].ward_capacity == 1u &&
          w.wiz[0].ward_strength == 0u && w.wiz[0].inc_state == INC_COLLECTING;

    bool stayed_spent_below_threshold = true;
    unsigned guard = 0;
    while (w.wiz[0].ward_capacity < 2u && guard++ < 64u) {
        uint8_t pos = (uint8_t)((guard * 7u) % 24u);
        tap(&w, 0, pos / 6u, pos % 6u, (uint8_t)(guard & 3u));
        if (w.wiz[0].ward_capacity == 1u) stayed_spent_below_threshold &=
            w.wiz[0].ward_strength == 0u;
    }
    ok &= stayed_spent_below_threshold && w.wiz[0].ward_capacity == 2u &&
          w.wiz[0].ward_strength == 1u; /* only the newly crossed tier returns */

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    tap(&w, 0, 1, 1, 0);
    uint32_t two = desc_set_magnitude_for_test(chip, 2u);
    install_spell(&w, 1, two, 239);
    step(&w, 0, 0, 0, 0, NULL, 0);
    ok &= w.wiz[0].hp == SIM_MAX_HP - 1u && !w.wiz[0].ward_strength &&
          !w.wiz[0].ward_capacity && w.wiz[0].inc_state == INC_IDLE;

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[0].ward_capacity = 2u; w.wiz[0].ward_strength = 2u;
    w.wiz[0].ward_focus = 2u;
    uint32_t high = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FROST, PAY_DAMAGE,
                                    TRAJ_HIGH, 1, STATUS_NONE, INTERACT_SOLID,
                                    TEMPO_FLOWING, TREND_STEADY, 0);
    install_spell(&w, 1, high, 239); step(&w, 0, 0, 0, 0, NULL, 0);
    ok &= w.wiz[0].hp == SIM_MAX_HP && w.wiz[0].ward_strength == 1u;

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[0].ward_capacity = 1u; w.wiz[0].ward_strength = 1u;
    w.wiz[0].ward_focus = 2u;
    install_spell(&w, 1, high, 239); step(&w, 0, 0, 0, 0, NULL, 0);
    ok &= w.wiz[0].hp == SIM_MAX_HP - 1u && !w.wiz[0].ward_capacity;

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    release_recipe(&w, 0, 1);
    ok &= w.spell[0].active && !w.wiz[0].ward_strength &&
          !w.wiz[0].ward_capacity;
    CHECK(ok, "m13_ward_capacity_spend_growth_leakage_coverage_and_launch_clear");
}

static void test_regeneration_boundary_and_hit_reset(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[1].hp = 10u;
    wait_ticks(&w, SIM_REGEN_TICKS - 1u);
    bool ok = w.wiz[1].hp == 10u && w.wiz[1].regen_ticks == 1u;
    wait_ticks(&w, 1u);
    ok &= w.wiz[1].hp == 11u && w.wiz[1].regen_ticks == SIM_REGEN_TICKS;

    wait_ticks(&w, SIM_REGEN_TICKS - 2u);
    uint32_t chip = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE,
                                    TRAJ_MID, 1, STATUS_NONE, INTERACT_PHASE,
                                    TEMPO_FLOWING, TREND_STEADY, 0);
    install_spell(&w, 0, chip, 239);
    step(&w, 0, 0, 0, 0, NULL, 0);
    ok &= w.wiz[1].hp == 10u && w.wiz[1].regen_ticks == SIM_REGEN_TICKS;
    wait_ticks(&w, SIM_REGEN_TICKS - 1u);
    ok &= w.wiz[1].hp == 10u;
    wait_ticks(&w, 1u);
    ok &= w.wiz[1].hp == 11u;
    CHECK(ok, "m13_regeneration_exact_30_seconds_and_damage_reset");
}

static void test_damage_heal_ward_and_status(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t damage = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE,
                                      TRAJ_MID, 4, STATUS_NONE, INTERACT_SOLID,
                                      TEMPO_FLOWING, TREND_STEADY, 0);
    w.wiz[1].ward_strength = 2; w.wiz[1].ward_focus = 2;
    install_spell(&w, 0, damage, 239);
    step(&w, 0, 0, 0, 0, NULL, 0);
    bool ok = w.wiz[1].hp == 10 && w.wiz[1].ward_strength == 0;

    uint32_t heal = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_HEAL,
                                    TRAJ_RETURNING, 4, STATUS_NONE, INTERACT_SOLID,
                                    TEMPO_FLOWING, TREND_STEADY, 0);
    w.wiz[0].hp = 5;
    install_spell(&w, 0, heal, 239);
    step(&w, 0, 0, 0, 0, NULL, 0);
    ok &= w.wiz[0].hp == 9;

    uint32_t burn = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_EMBER, PAY_STATUS,
                                    TRAJ_MID, 3, STATUS_BURNING, INTERACT_SOLID,
                                    TEMPO_RAPID, TREND_STEADY, 0);
    install_spell(&w, 0, burn, 239);
    step(&w, 0, 0, 0, 0, NULL, 0);
    uint8_t hp = w.wiz[1].hp;
    ok &= w.wiz[1].status == STATUS_BURNING && hp == 10;
    while (!w.wiz[1].status_burned) step(&w, 0, 0, 0, 0, NULL, 0);
    ok &= w.wiz[1].hp == (uint8_t)(hp - 1u) &&
          w.wiz[1].regen_ticks == SIM_REGEN_TICKS;
    CHECK(ok, "m13_damage_residual_heal_clamp_and_delayed_burn");
}

static void test_status_dominance_and_effects(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t frozen = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FROST, PAY_STATUS,
                                      TRAJ_LOW, 3, STATUS_FROZEN, INTERACT_PHASE,
                                      TEMPO_FLOWING, TREND_STEADY, 0);
    install_spell(&w, 0, frozen, 239);
    step(&w, 0, 0, 0, 0, NULL, 0);
    bool ok = w.wiz[1].status == STATUS_FROZEN && w.wiz[1].status_intensity == 3;
    uint8_t duration = w.wiz[1].status_ticks;
    uint32_t weak_burn = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_EMBER, PAY_STATUS,
                                         TRAJ_LOW, 1, STATUS_BURNING, INTERACT_PHASE,
                                         TEMPO_FLOWING, TREND_STEADY, 0);
    install_spell(&w, 0, weak_burn, 239);
    step(&w, 0, 0, 0, 0, NULL, 0);
    ok &= w.wiz[1].status == STATUS_FROZEN && w.wiz[1].status_intensity == 3 &&
          w.wiz[1].status_ticks < duration;

    sim_world_t normal, slowed;
    sim_init(&normal, SIMF_AUTHORITATIVE, 0); sim_init(&slowed, SIMF_AUTHORITATIVE, 0);
    uint32_t bolt = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE,
                                    TRAJ_MID, 1, STATUS_NONE, INTERACT_SOLID,
                                    TEMPO_RAPID, TREND_STEADY, 0);
    install_spell(&normal, 0, bolt, 0); install_spell(&slowed, 0, bolt, 0);
    slowed.wiz[0].status = STATUS_FROZEN; slowed.wiz[0].status_intensity = 2; slowed.wiz[0].status_ticks = 100;
    step(&normal, 0, 0, 0, 0, NULL, 0); step(&normal, 0, 0, 0, 0, NULL, 0);
    step(&slowed, 0, 0, 0, 0, NULL, 0); step(&slowed, 0, 0, 0, 0, NULL, 0);
    ok &= normal.spell[0].progress == 22 && slowed.spell[0].progress == 11;

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[0].inc_state = INC_PREPARED; w.wiz[0].prepared = 1;
    w.wiz[0].prepared_desc = desc_set_magnitude_for_test(bolt, 3);
    w.wiz[0].status = STATUS_DISRUPTED; w.wiz[0].status_intensity = 2; w.wiz[0].status_ticks = 100;
    step(&w, 0, 0, 0, 0, NULL, 0);
    ok &= w.spell[0].active && SPELL_DESC_MAGNITUDE(w.spell[0].descriptor) == 2 &&
          w.wiz[0].status == STATUS_NONE;

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[1].ward_strength = 4; w.wiz[1].ward_focus = 2;
    w.wiz[1].status = STATUS_MARKED; w.wiz[1].status_intensity = 2; w.wiz[1].status_ticks = 100;
    uint32_t area = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE,
                                    TRAJ_AREA, 2, STATUS_NONE, INTERACT_SOLID,
                                    TEMPO_RAPID, TREND_STEADY, 0);
    install_spell(&w, 0, area, 239); step(&w, 0, 0, 0, 0, NULL, 0);
    ok &= w.wiz[1].hp == 10 && w.wiz[1].ward_strength == 0;
    CHECK(ok, "m13_status_strength_frozen_disrupted_and_marked_effects");
}

static void test_form_lifecycles(void) {
    sim_world_t w;
    bool ok = true;
    uint32_t beam = SPELL_DESC_PACK(SPELL_BEAM, ELEM_FORCE, PAY_DAMAGE,
                                    TRAJ_MID, 2, STATUS_NONE, INTERACT_SOLID,
                                    TEMPO_RAPID, TREND_STEADY, 0);
    sim_init(&w, SIMF_AUTHORITATIVE, 0); install_spell(&w, 0, beam, 0);
    wait_ticks(&w, 5); uint8_t hp = w.wiz[1].hp;
    ok &= hp == 10 && w.spell[0].progress >= 64;
    wait_ticks(&w, 32);
    ok &= w.wiz[1].hp == hp && !w.spell[0].active;

    uint32_t singularity = SPELL_DESC_PACK(SPELL_SINGULARITY, ELEM_VOID, PAY_DAMAGE,
                                           TRAJ_AREA, 2, STATUS_NONE, INTERACT_ABSORB,
                                           TEMPO_DELIBERATE, TREND_STEADY, 0);
    sim_init(&w, SIMF_AUTHORITATIVE, 0); install_spell(&w, 0, singularity, 0);
    wait_ticks(&w, 28);
    ok &= !w.spell[0].active && w.wiz[1].hp == SIM_MAX_HP;

    uint32_t swarm = SPELL_DESC_PACK(SPELL_SWARM, ELEM_FORCE, PAY_DAMAGE,
                                     TRAJ_MID, 4, STATUS_NONE, INTERACT_SOLID,
                                     TEMPO_FRANTIC, TREND_STEADY, 0);
    sim_init(&w, SIMF_AUTHORITATIVE, 0); install_spell(&w, 0, swarm, 0); w.spell[0].aux = 6;
    wait_ticks(&w, 36);
    ok &= !w.spell[0].active && w.wiz[1].hp == 6;
    CHECK(ok, "m13_beam_once_singularity_empty_and_six_orb_lifecycles");
}

static void test_collision_precedence(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t phase = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_VOID, PAY_DAMAGE,
                                     TRAJ_MID, 2, STATUS_NONE, INTERACT_PHASE,
                                     TEMPO_RAPID, TREND_STEADY, 0);
    uint32_t solid = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE,
                                     TRAJ_MID, 2, STATUS_NONE, INTERACT_SOLID,
                                     TEMPO_RAPID, TREND_STEADY, 0);
    install_spell(&w, 0, phase, 120); install_spell(&w, 1, solid, 120);
    step(&w, 0, 0, 0, 0, NULL, 0);
    bool ok = w.spell[0].active && w.spell[1].active;

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t sing = SPELL_DESC_PACK(SPELL_SINGULARITY, ELEM_FORCE, PAY_DAMAGE,
                                    TRAJ_AREA, 2, STATUS_NONE, INTERACT_ABSORB,
                                    TEMPO_DELIBERATE, TREND_STEADY, 0);
    install_spell(&w, 0, sing, 48); w.spell[0].age = 10;
    install_spell(&w, 1, solid, 207);
    step(&w, 0, 0, 0, 0, NULL, 0);
    ok &= w.spell[0].active && w.spell[0].aux == 4 && !w.spell[1].active;

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t ember = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_EMBER, PAY_DAMAGE,
                                     TRAJ_MID, 2, STATUS_NONE, INTERACT_SOLID,
                                     TEMPO_RAPID, TREND_STEADY, 0);
    uint32_t frost = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FROST, PAY_DAMAGE,
                                     TRAJ_MID, 2, STATUS_NONE, INTERACT_SOLID,
                                     TEMPO_RAPID, TREND_STEADY, 0);
    install_spell(&w, 0, ember, 120); install_spell(&w, 1, frost, 120);
    step(&w, 0, 0, 0, 0, NULL, 0);
    ok &= !w.spell[0].active && !w.spell[1].active;
    CHECK(ok, "m13_collision_phase_singularity_and_ember_frost_precedence");
}

static uint32_t clash_desc(uint8_t element, uint8_t magnitude, uint8_t tempo,
                           uint8_t trend) {
    return SPELL_DESC_PACK(SPELL_PROJECTILE, element, PAY_DAMAGE, TRAJ_MID,
                           magnitude, STATUS_NONE, INTERACT_SOLID, tempo, trend, 0);
}

static void collide(sim_world_t *w, uint32_t left, uint32_t right) {
    install_spell(w, 0, left, 120u);
    install_spell(w, 1, right, 120u);
    step(w, 0, 0, 0, 0, NULL, 0);
}

static void test_productive_clashes(void) {
    sim_world_t w;
    bool ok = true;

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    collide(&w, clash_desc(ELEM_FORCE, 3, TEMPO_FLOWING, TREND_STEADY),
            clash_desc(ELEM_FORCE, 2, TEMPO_FRANTIC, TREND_IRREGULAR));
    ok &= w.spell[0].active && !w.spell[1].active &&
          SPELL_DESC_MAGNITUDE(w.spell[0].descriptor) == 4u;

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    collide(&w, clash_desc(ELEM_FROST, 2, TEMPO_RAPID, TREND_STEADY),
            clash_desc(ELEM_FROST, 2, TEMPO_FLOWING, TREND_IRREGULAR));
    ok &= w.spell[0].active && !w.spell[1].active &&
          SPELL_DESC_MAGNITUDE(w.spell[0].descriptor) == 4u;

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    collide(&w, clash_desc(ELEM_FORCE, 2, TEMPO_RAPID, TREND_ACCELERATING),
            clash_desc(ELEM_FORCE, 2, TEMPO_RAPID, TREND_STEADY));
    ok &= w.spell[0].active && !w.spell[1].active;

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t exact = clash_desc(ELEM_FORCE, 2, TEMPO_FLOWING, TREND_STEADY);
    collide(&w, exact, exact);
    ok &= !w.spell[0].active && !w.spell[1].active &&
          w.wiz[0].hp == SIM_MAX_HP - 1u && w.wiz[1].hp == SIM_MAX_HP - 1u;

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[0].ward_capacity = 4u; w.wiz[0].ward_strength = 4u;
    w.wiz[1].ward_capacity = 4u; w.wiz[1].ward_strength = 4u;
    collide(&w, exact, exact);
    ok &= w.wiz[0].hp == SIM_MAX_HP && w.wiz[1].hp == SIM_MAX_HP &&
          w.wiz[0].ward_strength == 3u && w.wiz[1].ward_strength == 3u;

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[0].ward_capacity = 4u; w.wiz[0].ward_strength = 4u;
    collide(&w, clash_desc(ELEM_EMBER, 4, TEMPO_FRANTIC, TREND_IRREGULAR),
            clash_desc(ELEM_FROST, 4, TEMPO_FRANTIC, TREND_IRREGULAR));
    ok &= w.wiz[0].hp == SIM_MAX_HP && w.wiz[0].ward_strength == 3u &&
          w.wiz[1].hp == SIM_MAX_HP - 1u;

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    collide(&w, clash_desc(ELEM_FORCE, 2, TEMPO_FLOWING, TREND_STEADY),
            clash_desc(ELEM_VOID, 2, TEMPO_FLOWING, TREND_STEADY));
    ok &= !w.spell[0].active && !w.spell[1].active &&
          w.wiz[0].hp == SIM_MAX_HP && w.wiz[1].hp == SIM_MAX_HP;

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t maximum = clash_desc(ELEM_FORCE, 4, TEMPO_FLOWING, TREND_STEADY);
    install_spell(&w, 0, maximum, 239u);
    step(&w, 0, 0, 0, 0, NULL, 0);
    ok &= w.wiz[1].hp == SIM_MAX_HP - 4u;
    CHECK(ok, "m13_productive_clash_cap_tiebreak_pulses_wards_and_damage_cap");
}

static void test_m13_link_ordering(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    duel_snapshot_t a, b, old_session;
    duel_encode(&w, 7, 0xffffu, &a);
    duel_encode(&w, 7, 0u, &b);
    duel_encode(&w, 6, 100u, &old_session);
    duel_rx_state_t rx = {0};
    bool ok = duel_rx_accept(&rx, &a, false) && duel_rx_accept(&rx, &b, false) &&
              !duel_rx_accept(&rx, &a, false) && duel_rx_accept(&rx, &old_session, false);
    duel_snapshot_t corrupt = b; corrupt.view.phase[0] ^= 0x40u;
    ok &= !duel_decode_valid(&corrupt);
    CHECK(ok, "m13_sequence_wrap_session_restart_and_corruption");
}

static void test_render_purity(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    release_recipe(&w, 0, 0);
    sim_world_t before = w;
    duel_render_t render = {0};
    duel_render_from_world(&render, &w);
    duel_fb_t fb;
    duel_fb_clear(&fb);
    wiz_draw_scene(&fb, &render, true, 7, false);
    bool nonempty = false;
    for (size_t i = 0; i < sizeof fb.bits; i++) nonempty |= fb.bits[i] != 0;
    CHECK(nonempty && memcmp(&w, &before, sizeof w) == 0,
          "m13_render_nonempty_and_authoritative_pure");
}

static uint32_t compile_actual_pattern(uint32_t seed, uint8_t extra_gap) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    static const uint8_t pos[4] = {0, 7, 14, 21};
    for (uint8_t i = 0; i < 4u; i++) {
        tap(&w, 0, pos[i] / 6u, pos[i] % 6u, (uint8_t)((seed + i) & 3u));
        if (i != 3u) wait_ticks(&w, extra_gap);
    }
    wait_ticks(&w, M13_IDLE_COMMIT_TICKS + 1u);
    return w.wiz[0].pending_desc;
}

static void test_real_input_reachability_and_timing_buckets(void) {
    uint32_t bucket_a = compile_actual_pattern(3u, 1u);
    uint32_t bucket_b = compile_actual_pattern(3u, 2u);
    bool ok = bucket_a == bucket_b;
    uint32_t forms = 0;
    uint8_t max_complexity = 0;
    for (uint32_t seed = 0; seed < 2048u && forms != 0xffu; seed++) {
        sim_world_t w;
        sim_init(&w, SIMF_AUTHORITATIVE, 0);
        sim_event_t chord[5];
        uint32_t chord_mask = 0;
        for (uint8_t j = 0; j < 5u; j++) {
            uint8_t p = (uint8_t)((seed + j * 5u) % 24u);
            chord[j] = SIM_EV_PACK(SIM_EV_KEYDOWN, 0, p / 6u, p % 6u);
            chord_mask |= 1u << p;
        }
        step(&w, chord_mask, 0, (uint8_t)(seed & 3u), 0, chord, 5);
        step(&w, 0, 0, 0, 0, NULL, 0);
        for (uint8_t i = 0; i < 70u; i++) {
            uint8_t pos = (uint8_t)((seed * 7u + i * 5u + (uint16_t)i * i) % 24u);
            uint8_t layer = (uint8_t)((seed + i * 3u + (i >> 2)) & 3u);
            tap(&w, 0, pos / 6u, pos % 6u, layer);
        }
        uint8_t complexity = m13_complexity(&w.wiz[0].inc);
        if (complexity > max_complexity) max_complexity = complexity;
        wait_ticks(&w, M13_IDLE_COMMIT_TICKS + 1u);
        if (SPELL_DESC_VALID(w.wiz[0].pending_desc))
            forms |= 1u << SPELL_DESC_FORM(w.wiz[0].pending_desc);
    }
    if (!ok || forms != 0xffu)
        printf("DIAG actual forms=%02x complexity=%u bucket_a=%06x bucket_b=%06x\n",
               (unsigned)forms, max_complexity, (unsigned)bucket_a, (unsigned)bucket_b);
    ok &= forms == 0xffu;
    CHECK(ok, "m13_real_input_all_forms_and_bucket_repeatability");
}

static uint32_t prose_workload_first_ko(uint8_t profile) {
    static const uint16_t period[3][2] = {
        {165u, 183u}, /* steady phrases */
        {175u, 191u}, /* dense bursts with longer thought pauses */
        {180u, 200u}, /* mixed-layer editing prose */
    };
    static const uint8_t keys[3][2] = {{8u, 7u}, {14u, 12u}, {10u, 9u}};
    static const uint8_t offset[2] = {0u, 37u};
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    for (uint32_t tick = 0; tick < 4500u; tick++) {
        sim_inputs_t in = {0};
        sim_event_t event[2];
        uint8_t n = 0;
        for (uint8_t side = 0; side < 2u; side++) {
            uint16_t phase = (uint16_t)((tick + offset[side]) % period[profile][side]);
            if (phase < (uint16_t)keys[profile][side] * 2u && !(phase & 1u)) {
                uint8_t rank = (uint8_t)(phase / 2u);
                uint8_t pos = (uint8_t)((rank * (side ? 7u : 5u) +
                                         profile * 3u + side * 11u +
                                         tick / period[profile][side]) % 24u);
                uint8_t layer = profile == 2u ? (uint8_t)((rank + side) & 3u) :
                                profile == 1u && rank >= 8u ? (uint8_t)(1u + side) : 0u;
                in.held_pos[side] = 1u << pos;
                in.down_mask |= (uint8_t)(1u << side);
                in.layer[side] = layer;
                event[n++] = SIM_EV_PACK(SIM_EV_KEYDOWN, side, pos / 6u, pos % 6u);
            }
        }
        sim_tick(&w, in, event, n, 0);
        if (w.wiz[0].life != LIFE_ACTIVE || w.wiz[1].life != LIFE_ACTIVE)
            return tick + 1u;
    }
    return 0u;
}

static void test_prose_typing_ko_window(void) {
    bool ok = true;
    for (uint8_t profile = 0; profile < 3u; profile++) {
        uint32_t ko = prose_workload_first_ko(profile);
        if (ko < 1500u || ko > 4500u)
            printf("DIAG prose profile=%u first_ko_ticks=%lu\n", profile,
                   (unsigned long)ko);
        ok &= ko >= 1500u && ko <= 4500u;
    }
    CHECK(ok, "m13_steady_burst_mixed_prose_first_ko_60_to_180_seconds");
}

static void test_max_cast_aftermath_and_wire(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    sim_event_t event = SIM_EV_PACK(SIM_EV_KEYDOWN, 0, 1, 2);
    uint32_t held = 1u << 8;
    step(&w, held, 0, 0, 0, &event, 1);
    for (unsigned i = 1; i < M13_FORCE_COMMIT_TICKS; i++)
        step(&w, held, 0, 0, 0, NULL, 0);
    uint8_t shared = m13_aftermath_shared(&w);
    uint8_t revision = m13_aftermath_revision(&w);
    bool ok = w.aftermath[0].kind == AFTER_MAX_CAST &&
              w.aftermath[1].kind == AFTER_MAX_CAST &&
              w.aftermath[0].resident_state == RESIDENT_WATCH_CAST &&
              w.world_state == WORLD_WONDER &&
              M13_AFTER_KIND(shared, 0) == AFTER_MAX_CAST &&
              M13_AFTER_KIND(shared, 1) == AFTER_MAX_CAST &&
              (revision & M13_AFTERMATH_WIRE);
    duel_snapshot_t packet;
    duel_encode(&w, 4, 9, &packet);
    ok &= packet.shared_pres == shared && packet.revision == revision && duel_decode_valid(&packet);
    duel_render_t render = {0};
    duel_render_from_world(&render, &w);
    ok &= render.shared_pres == shared && render.revision == revision;
    step(&w, 0, 0, 0, 0, NULL, 0);
    wait_ticks(&w, 55);
    ok &= w.aftermath[0].kind == AFTER_MAX_CAST &&
          w.aftermath[0].resident_state == RESIDENT_WATCH_CAST;
    wait_ticks(&w, 45);
    ok &= w.aftermath[0].resident_state == RESIDENT_CHEER;
    CHECK(ok, "m13_max_cast_coordinated_authoritative_wire_aftermath");
}

static void test_fireball_room_resident_object_arc(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t fireball = SPELL_DESC_PACK(SPELL_FIREBALL, ELEM_EMBER, PAY_DAMAGE,
                                        TRAJ_ROOF, 3, STATUS_NONE, INTERACT_SOLID,
                                        TEMPO_FLOWING, TREND_STEADY, 0);
    install_spell(&w, 0, fireball, 239);
    step(&w, 0, 0, 0, 0, NULL, 0);
    bool ok = !w.spell[0].active && w.wiz[1].hp == 9 &&
              w.fx_kind == FX_DETONATE && w.aftermath[1].kind == AFTER_FIRE &&
              w.aftermath[1].resident_state == RESIDENT_PANIC &&
              w.aftermath[1].room_state == ROOM_DISRUPTED &&
              w.aftermath[1].object_state == OBJECT_FIRE &&
              w.world_state == WORLD_CRISIS;
    wait_ticks(&w, 50);
    ok &= w.aftermath[1].resident_state == RESIDENT_FIGHT_FIRE &&
          w.aftermath[1].object_state == OBJECT_FIRE;
    wait_ticks(&w, 85);
    ok &= w.aftermath[1].resident_state == RESIDENT_REPAIR &&
          w.aftermath[1].room_state == ROOM_RECOVERY &&
          w.aftermath[1].object_state == OBJECT_DAMAGED;
    wait_ticks(&w, 40);
    ok &= w.aftermath[1].kind == AFTER_NONE && w.world_state == WORLD_CALM;
    CHECK(ok, "m13_fireball_roof_resident_room_object_recovery_arc");
}

static void test_reachable_complaint_and_ward_shatter(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t chip = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE,
                                    TRAJ_MID, 1, STATUS_NONE, INTERACT_SOLID,
                                    TEMPO_FLOWING, TREND_STEADY, 0);
    w.wiz[1].ward_strength = 1; w.wiz[1].ward_focus = 2;
    install_spell(&w, 0, chip, 239); step(&w, 0, 0, 0, 0, NULL, 0);
    bool ok = w.fx_kind == FX_WARD_SHATTER_R && w.wiz[1].hp == SIM_MAX_HP &&
              w.wiz[1].ward_strength == 0;
    install_spell(&w, 0, chip, 239); step(&w, 0, 0, 0, 0, NULL, 0);
    ok &= w.fx_kind == FX_COMPLAINT && w.wiz[1].hp == SIM_MAX_HP - 1u &&
          w.aftermath[1].kind == AFTER_COMPLAINT &&
          w.aftermath[1].resident_state == RESIDENT_COMPLAIN;
    CHECK(ok, "m13_ward_shatter_and_complaint_reachable");
}

static void test_ground_chain_summon_and_trap(void) {
    sim_world_t w;
    uint32_t ground = SPELL_DESC_PACK(SPELL_GROUND_WAVE, ELEM_FORCE, PAY_DAMAGE,
                                      TRAJ_GROUND, 2, STATUS_NONE, INTERACT_SOLID,
                                      TEMPO_FLOWING, TREND_STEADY, 0);
    uint32_t high = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FROST, PAY_DAMAGE,
                                    TRAJ_HIGH, 2, STATUS_NONE, INTERACT_SOLID,
                                    TEMPO_FLOWING, TREND_STEADY, 0);
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    install_spell(&w, 0, ground, 120); install_spell(&w, 1, high, 120);
    step(&w, 0, 0, 0, 0, NULL, 0);
    bool ok = w.spell[0].active && w.spell[1].active;

    uint32_t chain = SPELL_DESC_PACK(SPELL_CHAIN, ELEM_FORCE, PAY_DAMAGE,
                                     TRAJ_HOMING, 2, STATUS_NONE, INTERACT_SOLID,
                                     TEMPO_RAPID, TREND_ACCELERATING, 0);
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    install_spell(&w, 0, chain, 120); install_spell(&w, 1, high, 120);
    step(&w, 0, 0, 0, 0, NULL, 0);
    ok &= w.spell[0].active && !w.spell[1].active && w.fx_kind == FX_RESIDUE;

    uint32_t trap = SPELL_DESC_PACK(SPELL_CONJURE, ELEM_EMBER, PAY_DAMAGE,
                                    TRAJ_GROUND, 2, STATUS_NONE, INTERACT_SOLID,
                                    TEMPO_DELIBERATE, TREND_STEADY, 0);
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    install_spell(&w, 0, trap, 16); w.spell[0].aux = 3;
    install_spell(&w, 1, high, 175);
    step(&w, 0, 0, 0, 0, NULL, 0);
    ok &= !w.spell[0].active && !w.spell[1].active &&
          w.wiz[1].hp == 10 && w.fx_kind == FX_DETONATE;

    uint32_t summon = SPELL_DESC_PACK(SPELL_CONJURE, ELEM_FORCE, PAY_DAMAGE,
                                      TRAJ_RETURNING, 2, STATUS_NONE, INTERACT_SOLID,
                                      TEMPO_FRANTIC, TREND_STEADY, 0);
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    install_spell(&w, 0, summon, 0); w.spell[0].aux = 2;
    wait_ticks(&w, 22);
    ok &= !w.spell[0].active && w.wiz[1].hp == 10;
    CHECK(ok, "m13_ground_chain_summon_and_trap_lifecycles");
}

static void test_swarm_gather_launch_and_tempo_motion(void) {
    sim_world_t w;
    uint32_t swarm = SPELL_DESC_PACK(SPELL_SWARM, ELEM_FORCE, PAY_DAMAGE,
                                     TRAJ_MID, 4, STATUS_NONE, INTERACT_SOLID,
                                     TEMPO_FRANTIC, TREND_ACCELERATING, 0);
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    install_spell(&w, 0, swarm, 0); w.spell[0].aux = 6;
    wait_ticks(&w, 11);
    bool ok = w.wiz[1].hp == SIM_MAX_HP && (w.spell[0].progress >> 5) == 6u &&
              (w.spell[0].progress & 31u) < 12u;
    wait_ticks(&w, 4);
    ok &= w.wiz[1].hp == SIM_MAX_HP && (w.spell[0].progress & 31u) >= 12u;
    wait_ticks(&w, 1);
    ok &= w.wiz[1].hp == SIM_MAX_HP - 1u && (w.spell[0].progress >> 5) == 5u;
    wait_ticks(&w, 20);
    ok &= !w.spell[0].active && w.wiz[1].hp == 6;

    uint32_t slow = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE,
                                    TRAJ_LOW, 1, STATUS_NONE, INTERACT_SOLID,
                                    TEMPO_DELIBERATE, TREND_DECELERATING, 0);
    uint32_t fast = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE,
                                    TRAJ_LOW, 1, STATUS_NONE, INTERACT_SOLID,
                                    TEMPO_FRANTIC, TREND_ACCELERATING, 0);
    sim_world_t a, b;
    sim_init(&a, SIMF_AUTHORITATIVE, 0); sim_init(&b, SIMF_AUTHORITATIVE, 0);
    install_spell(&a, 0, slow, 0); install_spell(&b, 0, fast, 0);
    wait_ticks(&a, 8); wait_ticks(&b, 8);
    ok &= b.spell[0].progress > a.spell[0].progress;
    CHECK(ok, "m13_swarm_gather_serial_launch_and_tempo_trend_motion");
}

static void test_bilateral_beam_and_aftermath_split_render(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t beam = SPELL_DESC_PACK(SPELL_BEAM, ELEM_FORCE, PAY_DAMAGE,
                                    TRAJ_MID, 3, STATUS_NONE, INTERACT_SOLID,
                                    TEMPO_FLOWING, TREND_STEADY, 0);
    install_spell(&w, 1, beam, 128);
    w.aftermath[0] = (sim_aftermath_t){AFTER_INSPECT, 80, 2, RESIDENT_INSPECT,
                                       ROOM_ALERT, OBJECT_RESIDUE};
    w.world_state = WORLD_RECOVERY;
    duel_render_t master = {0};
    duel_render_from_world(&master, &w); master.seed = 9; master.civic_phase = 12;
    duel_fb_t ml, mr;
    duel_fb_clear(&ml); duel_fb_clear(&mr);
    wiz_draw_scene(&ml, &master, true, 0, false);
    wiz_draw_scene(&mr, &master, false, 0, false);
    int beam_y = 63 + DUEL_ROOF_DY;
    bool ok = duel_fb_get(&ml, 21, beam_y) && duel_fb_get(&ml, 31, beam_y) &&
              duel_fb_get(&mr, 0, beam_y) && duel_fb_get(&mr, 10, beam_y) &&
              !duel_fb_get(&ml, 0, beam_y) && !duel_fb_get(&mr, 31, beam_y);

    duel_snapshot_t packet;
    duel_encode(&w, 8, 20, &packet);
    duel_rx_state_t rx = {0};
    ok &= duel_rx_accept(&rx, &packet, false) && duel_decode_valid(&rx.last);
    duel_render_t slave = master;
    slave.view = rx.last.view; slave.shared_pres = rx.last.shared_pres;
    slave.revision = rx.last.revision;
    duel_fb_t sl, sr;
    duel_fb_clear(&sl); duel_fb_clear(&sr);
    wiz_draw_scene(&sl, &slave, true, 0, false);
    wiz_draw_scene(&sr, &slave, false, 0, false);
    ok &= memcmp(&ml, &sl, sizeof ml) == 0 && memcmp(&mr, &sr, sizeof mr) == 0;
    CHECK(ok, "m13_bilateral_beam_and_aftermath_split_render_convergence");
}

static bool exact_mirror(const duel_fb_t *a, const duel_fb_t *b) {
    for (int y = 0; y < DUEL_CANVAS_H; y++)
        for (int x = 0; x < DUEL_CANVAS_W; x++) {
            if (duel_fb_get(a, x, y) !=
                duel_fb_get(b, DUEL_CANVAS_W - 1 - x, y)) return false;
        }
    return true;
}

static unsigned band_difference(const duel_fb_t *a, const duel_fb_t *b,
                                int y0, int y1) {
    unsigned n = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = 0; x < DUEL_CANVAS_W; x++)
            n += duel_fb_get(a, x, y) != duel_fb_get(b, x, y);
    return n;
}

static void render_floor_scene(uint8_t floor, bool is_left, uint8_t transition,
                               duel_fb_t *fb) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    duel_render_t r = {0};
    duel_render_from_world(&r, &w);
    r.civic = DUEL_CIVIC_PACK(floor, DUEL_M12_MODE_NORMAL, 0);
    r.seed = 0x42u; r.civic_phase = 19u; r.floor_transition = transition;
    duel_fb_clear(fb);
    wiz_draw_scene(fb, &r, is_left, 7u, false);
}

static void test_floor_occupations_and_transitions(void) {
    duel_fb_t floor[2][3];
    bool ok = true;
    for (uint8_t city = 0; city < 2u; city++)
        for (uint8_t occupation = 0; occupation < 3u; occupation++)
            render_floor_scene(occupation, city == 0u, 0u,
                               &floor[city][occupation]);
    for (uint8_t city = 0; city < 2u; city++)
        for (uint8_t a = 0; a < 3u; a++)
            for (uint8_t b = (uint8_t)(a + 1u); b < 3u; b++) {
                unsigned diff = band_difference(&floor[city][a], &floor[city][b], 62, 110);
                if (diff < 40u) printf("DIAG floor city=%u pair=%u/%u diff=%u\n",
                                       city, a, b, diff);
                ok &= diff >= 40u;
            }

    for (uint8_t phase = 0; phase < 4u; phase++) {
        duel_fb_t transitioned;
        uint8_t byte = M13_FLOOR_TRANSITION_PACK(DUEL_M12_FLOOR_COMMONS,
                                                  phase, true);
        render_floor_scene(DUEL_M12_FLOOR_WORKSHOP, true, byte, &transitioned);
        const duel_fb_t *reference = phase < 2u ?
            &floor[0][DUEL_M12_FLOOR_COMMONS] : &floor[0][DUEL_M12_FLOOR_WORKSHOP];
        ok &= band_difference(&transitioned, reference, 62, 110) > 0u;
        ok &= band_difference(&transitioned, reference, 0, 60) == 0u;
        ok &= band_difference(&transitioned, reference, 111, 127) == 0u;
        ok &= M13_FLOOR_TRANSITION_SOURCE(byte) == DUEL_M12_FLOOR_COMMONS &&
              M13_FLOOR_TRANSITION_PHASE(byte) == phase &&
              M13_FLOOR_TRANSITION_ACTIVE(byte) && !(byte & 0xe0u);
    }
    CHECK(ok, "m13_six_occupation_scenes_40px_and_four_protected_transition_phases");
}

static unsigned framebuffer_pixels(const duel_fb_t *fb) {
    unsigned n = 0;
    for (int y = 0; y < DUEL_CANVAS_H; y++)
        for (int x = 0; x < DUEL_CANVAS_W; x++) n += duel_fb_get(fb, x, y);
    return n;
}

static void m13_render(duel_fb_t *fb, const duel_render_t *r, bool is_left,
                       bool diagnostics) {
    duel_fb_clear(fb);
    wiz_draw_scene(fb, r, is_left, 7u, diagnostics);
}

static uint64_t m13_bytes_hash(const void *data, size_t size) {
    const uint8_t *p = data;
    uint64_t h = UINT64_C(1469598103934665603);
    while (size--) { h ^= *p++; h *= UINT64_C(1099511628211); }
    return h;
}

static uint8_t quiet_action(uint8_t action) {
    if (action == DUEL_M12_ACTION_WALK) return DUEL_M12_ACTION_REST;
    if (action == DUEL_M12_ACTION_REACT) return DUEL_M12_ACTION_INSPECT;
    if (action == DUEL_M12_ACTION_WATCH_ROOF) return DUEL_M12_ACTION_WORK;
    return action;
}

static void test_resident_occupation_derivation(void) {
    bool seen[2][M13_OCCUPATION_FLOORS][DUEL_M12_ACTION_COUNT] = {{{false}}};
    bool personalities[2][DUEL_M12_PERSONALITY_COUNT] = {{false}};
    bool ok = true;
    for (uint16_t seed = 0; seed < 256u; seed++) {
        for (uint8_t side = 0; side < 2u; side++) {
            personalities[side][m12_resident_personality((uint8_t)seed, side == 0u)] = true;
            for (uint8_t slot = 0; slot < 16u; slot++) {
                uint8_t phase = (uint8_t)(slot * DUEL_M12_ACTION_SLOT);
                m12_resident_t common = m12_resident_derive((uint8_t)seed, side == 0u,
                    DUEL_M12_FLOOR_COMMONS, DUEL_M12_MODE_NORMAL, phase);
                for (uint8_t floor = 0; floor < M13_OCCUPATION_FLOORS; floor++) {
                    m12_resident_t a = m12_resident_derive((uint8_t)seed, side == 0u,
                        floor, DUEL_M12_MODE_NORMAL, phase);
                    m12_resident_t b = m12_resident_derive((uint8_t)seed, side == 0u,
                        floor, DUEL_M12_MODE_NORMAL, phase);
                    ok &= memcmp(&a, &b, sizeof a) == 0 && a.action == common.action;
                    ok &= a.station == M13_OCCUPATION_KEY(floor, a.action);
                    seen[side][floor][a.action] = true;

                    m12_resident_t quiet = m12_resident_derive((uint8_t)seed, side == 0u,
                        floor, DUEL_M12_MODE_QUIET, phase);
                    ok &= quiet.action == quiet_action(common.action) &&
                          quiet.station == M13_OCCUPATION_KEY(floor, quiet.action);
                }
                m12_resident_t invalid = m12_resident_derive((uint8_t)seed, side == 0u,
                    DUEL_M12_FLOOR_SPECIAL, DUEL_M12_MODE_NORMAL, phase);
                ok &= invalid.action == common.action &&
                      invalid.station == M13_OCCUPATION_KEY(DUEL_M12_FLOOR_COMMONS,
                                                            invalid.action);
            }
        }
    }
    for (uint8_t side = 0; side < 2u; side++) {
        for (uint8_t p = 0; p < DUEL_M12_PERSONALITY_COUNT; p++) ok &= personalities[side][p];
        for (uint8_t floor = 0; floor < M13_OCCUPATION_FLOORS; floor++)
            for (uint8_t action = 0; action < DUEL_M12_ACTION_COUNT; action++)
                ok &= seen[side][floor][action];
    }

    /* Authoritative aftermath suppresses personality, progress, carry, and
     * object-reaction modifiers while retaining the same resident body/task. */
    m12_resident_t after = {DUEL_M12_PERSONALITY_DILIGENT, DUEL_M12_ACTION_REACT,
        M13_OCCUPATION_KEY(DUEL_M12_FLOOR_RESEARCH, DUEL_M12_ACTION_REACT), 0,
        RESIDENT_CHEER};
    duel_fb_t first, second;
    duel_fb_clear(&first); duel_fb_clear(&second);
    m12_resident_draw(&first, &after, true, DUEL_M12_MODE_NORMAL, 0);
    after.personality = DUEL_M12_PERSONALITY_DISTRACTED; after.progress = 15;
    m12_resident_draw(&second, &after, true, DUEL_M12_MODE_NORMAL, 99);
    ok &= memcmp(&first, &second, sizeof first) == 0;
    CHECK(ok, "m13_resident_42_keys_personalities_quiet_fallback_deterministic_aftermath");
}

static void test_resident_geometry_and_object_separation(void) {
    bool ok = true;
    for (uint8_t side = 0; side < 2u; side++) {
        m12_resident_t core = {DUEL_M12_PERSONALITY_DILIGENT, DUEL_M12_ACTION_WORK,
            M13_OCCUPATION_KEY(DUEL_M12_FLOOR_COMMONS, DUEL_M12_ACTION_WORK), 0, 0xffu};
        duel_fb_t body;
        duel_fb_clear(&body);
        m12_resident_draw(&body, &core, side == 0u, DUEL_M12_MODE_NORMAL, 0);
        int x0 = 32, x1 = -1, y0 = 128, y1 = -1;
        for (int y = 0; y < DUEL_CANVAS_H; y++)
            for (int x = 0; x < DUEL_CANVAS_W; x++)
                if (duel_fb_get(&body, x, y)) {
                    if (x < x0) x0 = x;
                    if (x > x1) x1 = x;
                    if (y < y0) y0 = y;
                    if (y > y1) y1 = y;
                }
        ok &= x1 - x0 + 1 == 5 && y1 - y0 + 1 == 14 && y0 >= 61 && y1 <= 110;

        for (uint8_t floor = 0; floor < M13_OCCUPATION_FLOORS; floor++) {
            core.station = M13_OCCUPATION_KEY(floor, DUEL_M12_ACTION_WORK);
            duel_fb_clear(&body);
            m12_resident_draw(&body, &core, side == 0u, DUEL_M12_MODE_NORMAL, 0);
            duel_fb_t object;
            duel_fb_clear(&object);
            m13_resident_draw_attunement(&object, side == 0u, floor);
            for (int y = 0; y < DUEL_CANVAS_H; y++)
                for (int x = 0; x < DUEL_CANVAS_W; x++)
                    ok &= !(duel_fb_get(&body, x, y) && duel_fb_get(&object, x, y));

            duel_fb_t room;
            render_floor_scene(floor, side == 0u, 0u, &room);
            ok &= framebuffer_pixels(&room) > framebuffer_pixels(&body) * 2u;
        }
    }
    CHECK(ok, "m13_resident_5x14_core_bounds_negative_space_and_object_mass");
}

static bool health_pixel(bool is_left, int hp_index, int x, int y) {
    int canonical_x = (hp_index & 1) ? 4 : 7;
    int px = is_left ? canonical_x : DUEL_CANVAS_W - 2 - canonical_x;
    int py = 57 - (hp_index / 2) * 2;
    return y == py && (x == px || x == px + 1);
}

static void test_health_grid_geometry_and_lifecycles(void) {
    bool ok = true;
    for (uint8_t side = 0; side < 2u; side++) {
        for (uint8_t hp = 0; hp <= SIM_MAX_HP; hp++) {
            sim_world_t w;
            sim_init(&w, SIMF_AUTHORITATIVE, 0);
            w.wiz[side].hp = hp;
            duel_render_t r = {0}; duel_render_from_world(&r, &w);
            duel_fb_t fb; m13_render(&fb, &r, side == 0u, false);
            unsigned lit = 0;
            for (int y = 47; y <= 57; y++) {
                for (int x = 4; x <= 8; x++) {
                    int sx = side == 0u ? x : DUEL_CANVAS_W - 1 - x;
                    bool expected = false;
                    for (int i = 0; i < hp; i++) expected |= health_pixel(side == 0u, i, sx, y);
                    bool actual = duel_fb_get(&fb, sx, y);
                    if (actual != expected)
                        printf("DIAG health side=%u hp=%u x=%d y=%d actual=%u expected=%u\n",
                               side, hp, sx, y, actual, expected);
                    ok &= actual == expected;
                    lit += actual;
                }
            }
            ok &= lit == 2u * hp;
        }
    }

    /* The fixed grid remains unobscured for every wizard tableau. */
    static const uint8_t life[] = {LIFE_ACTIVE, LIFE_ACTIVE, LIFE_ACTIVE,
        LIFE_COLLAPSE, LIFE_DOWNED, LIFE_MEDIC, LIFE_REPLACE};
    static const uint8_t pose[] = {POSE_IDLE, POSE_CAST, POSE_RECOVER,
        POSE_IDLE, POSE_IDLE, POSE_IDLE, POSE_IDLE};
    static const uint8_t ticks[] = {0, 0, 0, SIM_COLLAPSE_TICKS,
        SIM_DOWNED_TICKS, SIM_MEDIC_TICKS, SIM_REPLACE_TICKS};
    for (uint8_t side = 0; side < 2u; side++)
        for (size_t state = 0; state < sizeof life; state++) {
            sim_world_t w;
            sim_init(&w, SIMF_AUTHORITATIVE, 0);
            w.wiz[side].life = life[state]; w.wiz[side].life_ticks = ticks[state];
            w.wiz[side].pose = pose[state]; w.wiz[side].hp = 0;
            duel_render_t empty = {0}; duel_render_from_world(&empty, &w);
            duel_fb_t zero; m13_render(&zero, &empty, side == 0u, false);
            w.wiz[side].hp = SIM_MAX_HP;
            duel_render_t full = {0}; duel_render_from_world(&full, &w);
            duel_fb_t grid; m13_render(&grid, &full, side == 0u, false);
            for (int i = 0; i < SIM_MAX_HP; i++) {
                for (int x = 0; x < DUEL_CANVAS_W; x++)
                    for (int y = 47; y <= 57; y++)
                        if (health_pixel(side == 0u, i, x, y))
                            if (duel_fb_get(&zero, x, y) || !duel_fb_get(&grid, x, y)) {
                                printf("DIAG health-pose side=%u state=%zu x=%d y=%d zero=%u full=%u\n",
                                       side, state, x, y, duel_fb_get(&zero, x, y),
                                       duel_fb_get(&grid, x, y));
                                ok = false;
                            }
            }
        }
    CHECK(ok, "m13_health_0_12_two_pixels_bottom_up_mirror_5x11_pose_clearance");
}

static void test_local_layer_attunement(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint64_t before = m13_bytes_hash(&w, sizeof w);
    duel_render_t base = {0}; duel_render_from_world(&base, &w);
    base.civic = DUEL_CIVIC_PACK(DUEL_M12_FLOOR_RESEARCH, DUEL_M12_MODE_NORMAL, 0);
    base.seed = 7; base.civic_phase = 32;
    base.layer = DUEL_RENDER_LAYER_PACK(0, DUEL_RENDER_LOCAL_NONE);
    duel_fb_t bl, br, ll, lr, rl, rr;
    m13_render(&bl, &base, true, false); m13_render(&br, &base, false, false);

    duel_render_t local = base;
    local.layer = DUEL_RENDER_LAYER_PACK(1, DUEL_RENDER_LOCAL_LEFT);
    m13_render(&ll, &local, true, false); m13_render(&lr, &local, false, false);
    bool left_changes = memcmp(&bl, &ll, sizeof bl) != 0;
    bool left_spares_right = memcmp(&br, &lr, sizeof br) == 0;
    bool ok = left_changes && left_spares_right;
    local.layer = DUEL_RENDER_LAYER_PACK(2, DUEL_RENDER_LOCAL_RIGHT);
    m13_render(&rl, &local, true, false); m13_render(&rr, &local, false, false);
    bool right_spares_left = memcmp(&bl, &rl, sizeof bl) == 0;
    bool right_changes = memcmp(&br, &rr, sizeof br) != 0;
    ok &= right_spares_left && right_changes;

    /* Release and ordinary global-layer typing reproduce the exact baseline. */
    local.layer = DUEL_RENDER_LAYER_PACK(3, DUEL_RENDER_LOCAL_NONE);
    m13_render(&rl, &local, true, false); m13_render(&rr, &local, false, false);
    bool global_left_same = memcmp(&bl, &rl, sizeof bl) == 0;
    bool global_right_same = memcmp(&br, &rr, sizeof br) == 0;
    ok &= global_left_same && global_right_same;

    /* During the bilateral dwell each OLED can show its own mark. Once scry is
     * authoritative, the instruments replace both without retaining either. */
    duel_render_t open_none = base;
    open_none.view.outcome_overlay |= 0x10u;
    open_none.layer = DUEL_RENDER_LAYER_PACK(3, DUEL_RENDER_LOCAL_NONE);
    duel_fb_t ol0, or0, ol1, or1;
    m13_render(&ol0, &open_none, true, false); m13_render(&or0, &open_none, false, false);
    duel_render_t open_local = open_none;
    open_local.layer = DUEL_RENDER_LAYER_PACK(3, DUEL_RENDER_LOCAL_LEFT);
    m13_render(&ol1, &open_local, true, false);
    open_local.layer = DUEL_RENDER_LAYER_PACK(3, DUEL_RENDER_LOCAL_RIGHT);
    m13_render(&or1, &open_local, false, false);
    bool open_left_same = memcmp(&ol0, &ol1, sizeof ol0) == 0;
    bool open_right_same = memcmp(&or0, &or1, sizeof or0) == 0;
    ok &= open_left_same && open_right_same;

    sim_world_t typed;
    sim_init(&typed, SIMF_AUTHORITATIVE, 0);
    for (int i = 0; i < SCRY_PENDING_TICKS * 3; i++)
        sim_tick(&typed, (sim_inputs_t){.scry_mask = SCRY_M_L | SCRY_M_OTHER,
                 .layer = {1, 0}, .held_pos = {1u, 0}}, NULL, 0, 0);
    bool typing_closed = !scry_is_open(&typed) && typed.scry.state == SCRY_FIRST_HELD;
    bool world_same = m13_bytes_hash(&w, sizeof w) == before;
    ok &= typing_closed && world_same;
    if (!ok) printf("DIAG local lc=%u lsr=%u rsl=%u rc=%u gl=%u gr=%u ol=%u or=%u typing=%u world=%u state=%u\n",
                    left_changes, left_spares_right, right_spares_left, right_changes,
                    global_left_same, global_right_same, open_left_same, open_right_same,
                    typing_closed, world_same, typed.scry.state);
    CHECK(ok, "m13_local_attunement_physical_half_release_typing_pending_and_scry_suppression");
}

static bool framebuffer_subset(const duel_fb_t *small, const duel_fb_t *large) {
    for (int y = 0; y < DUEL_CANVAS_H; y++)
        for (int x = 0; x < DUEL_CANVAS_W; x++)
            if (duel_fb_get(small, x, y) && !duel_fb_get(large, x, y)) return false;
    return true;
}

static void test_diegetic_scry_instruments(void) {
    bool ok = true;
    for (uint8_t floor = 0; floor < M13_OCCUPATION_FLOORS; floor++)
        for (uint8_t scene = 0; scene < SCRY_SCENES; scene++)
            for (uint8_t online = 0; online < 2u; online++)
                for (uint8_t notif_case = 0; notif_case < 2u; notif_case++)
                    for (uint8_t side = 0; side < 2u; side++) {
                    sim_world_t w;
                    sim_init(&w, SIMF_AUTHORITATIVE, 0);
                    duel_render_t r = {0}; duel_render_from_world(&r, &w);
                    r.civic = DUEL_CIVIC_PACK(floor, DUEL_M12_MODE_NORMAL, 0);
                    r.seed = 9; r.civic_phase = 48;
                    uint8_t notif = notif_case ? 4u : 0u;
                    r.external = DUEL_HOST_CONTEXT_PACK(online, scene, notif, false);
                    r.layer = DUEL_RENDER_LAYER_PACK(scene, DUEL_RENDER_LOCAL_NONE);
                    r.view.outcome_overlay = (uint8_t)((r.view.outcome_overlay & 0x1fu) |
                                                       (scene << 5));
                    duel_fb_t base, open;
                    m13_render(&base, &r, side == 0u, false);
                    r.view.outcome_overlay |= 0x10u;
                    m13_render(&open, &r, side == 0u, false);
                    bool subset = framebuffer_subset(&base, &open);
                    bool changed = memcmp(&base, &open, sizeof base) != 0;
                    if (!subset || !changed)
                        printf("DIAG scry case floor=%u scene=%u online=%u notif=%u side=%u subset=%u changed=%u\n",
                               floor, scene, online, notif, side, subset, changed);
                    ok &= subset && changed;
                    for (int y = 0; y < DUEL_CANVAS_H; y++)
                        for (int x = 0; x < DUEL_CANVAS_W; x++)
                            if (duel_fb_get(&base, x, y) != duel_fb_get(&open, x, y))
                                if (!(y <= 35 || (y >= 59 && y <= 60))) {
                                    printf("DIAG scry exclusion floor=%u scene=%u online=%u notif=%u side=%u x=%d y=%d\n",
                                           floor, scene, online, notif, side, x, y);
                                    ok = false;
                                }
                }

    /* Compare only added instruments: all architectural positions mirror while
     * selector indices retain their semantic order. */
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    duel_render_t r = {0}; duel_render_from_world(&r, &w);
    r.civic = DUEL_CIVIC_PACK(DUEL_M12_FLOOR_COMMONS, DUEL_M12_MODE_NORMAL, 0);
    r.external = DUEL_HOST_CONTEXT_PACK(true, DUEL_HOST_SCENE_FOCUS, 3, true);
    r.alert = DUEL_HOST_ALERT_PACK(DUEL_HOST_CATEGORY_SECURITY,
                                   DUEL_HOST_PRIORITY_CRITICAL, 7);
    r.layer = DUEL_RENDER_LAYER_PACK(3, DUEL_RENDER_LOCAL_NONE);
    duel_fb_t lb, rb, lo, ro;
    m13_render(&lb, &r, true, false); m13_render(&rb, &r, false, false);
    r.view.outcome_overlay = (uint8_t)((r.view.outcome_overlay & 0x0fu) | 0x10u |
                                       (DUEL_HOST_SCENE_FOCUS << 5));
    m13_render(&lo, &r, true, false); m13_render(&ro, &r, false, false);
    for (int y = 0; y < DUEL_CANVAS_H; y++)
        for (int x = 0; x < DUEL_CANVAS_W; x++) {
            bool ld = duel_fb_get(&lo, x, y) != duel_fb_get(&lb, x, y);
            bool rd = duel_fb_get(&ro, 31 - x, y) != duel_fb_get(&rb, 31 - x, y);
            if (ld != rd) {
                printf("DIAG scry mirror x=%d y=%d ld=%u rd=%u\n", x, y, ld, rd);
                ok = false;
            }
        }

    /* Every normalized alert remains visible in the outer corner; stale-link
     * and diagnostics retain their later-layer priority. */
    duel_render_t empty_alert = r;
    empty_alert.alert = 0;
    empty_alert.external = DUEL_HOST_CONTEXT_PACK(true, DUEL_HOST_SCENE_FOCUS, 4, false);
    duel_fb_t empty_alert_fb;
    m13_render(&empty_alert_fb, &empty_alert, true, false);
    for (uint8_t category = 1; category < DUEL_HOST_CATEGORY_COUNT; category++)
        for (uint8_t priority_level = DUEL_HOST_PRIORITY_LOW;
             priority_level <= DUEL_HOST_PRIORITY_CRITICAL; priority_level++)
            for (uint8_t persistent = 0; persistent < 2u; persistent++) {
                duel_render_t alert = r;
                alert.external = DUEL_HOST_CONTEXT_PACK(true, DUEL_HOST_SCENE_FOCUS,
                                                         4, persistent);
                alert.alert = DUEL_HOST_ALERT_PACK(category, priority_level, 3);
                duel_fb_t category_fb;
                m13_render(&category_fb, &alert, true, false);
                ok &= memcmp(&empty_alert_fb, &category_fb, sizeof category_fb) != 0;
            }
    r.flags |= DUEL_RENDER_STALE;
    r.diag_tick = 7;
    duel_fb_t priority;
    m13_render(&priority, &r, true, true);
    ok &= duel_fb_get(&priority, 23, 2) && duel_fb_get(&priority, 7, 127);
    CHECK(ok, "m13_diegetic_scry_all_scenes_floors_host_alert_subset_mirror_exclusions_priority");
}

static void test_gap_cue_families_temporal_mirrors(void) {
    static const uint32_t desc[] = {
        SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE, TRAJ_MID, 2,
                        STATUS_NONE, INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, 1),
        SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FROST, PAY_DAMAGE, TRAJ_HOMING, 2,
                        STATUS_NONE, INTERACT_SOLID, TEMPO_RAPID, TREND_ACCELERATING, 2),
        SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_VOID, PAY_DAMAGE, TRAJ_MID, 2,
                        STATUS_NONE, INTERACT_PHASE, TEMPO_FLOWING, TREND_STEADY, 3),
        SPELL_DESC_PACK(SPELL_BEAM, ELEM_EMBER, PAY_DAMAGE, TRAJ_MID, 3,
                        STATUS_NONE, INTERACT_SOLID, TEMPO_RAPID, TREND_STEADY, 0),
        SPELL_DESC_PACK(SPELL_CHAIN, ELEM_FORCE, PAY_DAMAGE, TRAJ_HOMING, 3,
                        STATUS_NONE, INTERACT_SOLID, TEMPO_RAPID, TREND_IRREGULAR, 1),
    };
    static const uint8_t progress[] = {105u, 130u, 155u};
    bool ok = true;
    for (size_t family = 0; family < sizeof desc / sizeof desc[0]; family++) {
        for (size_t stage = 0; stage < sizeof progress; stage++) {
            duel_view_spell_t left = {.active = 1u, .descriptor = desc[family],
                .progress = progress[stage],
                .kind = DUEL_KIND_WITH_TIER(DUEL_KIND_PACK(SPELL_DESC_ELEMENT(desc[family]),
                                                           MOD_NONE, PAY_IMPACT), 1u)};
            duel_view_spell_t right = left;
            duel_fb_t ll, lr, rl, rr;
            duel_fb_clear(&ll); duel_fb_clear(&lr); duel_fb_clear(&rl); duel_fb_clear(&rr);
            m13_draw_spell(&ll, &left, 0, 0, true, 9u);
            m13_draw_spell(&lr, &left, 0, 0, false, 9u);
            m13_draw_spell(&rl, &right, 1, 0, true, 9u);
            m13_draw_spell(&rr, &right, 1, 0, false, 9u);
            ok &= exact_mirror(&ll, &rr) && exact_mirror(&lr, &rl) &&
                  framebuffer_pixels(&ll) + framebuffer_pixels(&lr) > 0u;
        }
    }

    /* Non-continuous families have no edge handoff before departure or after
     * arrival. Beam/chain remain bilateral by design and are tested above. */
    for (size_t family = 0; family < 3u; family++) {
        for (uint8_t p = 32u; p <= 224u; p = (uint8_t)(p + 192u)) {
            duel_view_spell_t sp = {.active = 1u, .descriptor = desc[family],
                .progress = p,
                .kind = DUEL_KIND_WITH_TIER(DUEL_KIND_PACK(SPELL_DESC_ELEMENT(desc[family]),
                                                           MOD_NONE, PAY_IMPACT), 1u)};
            duel_fb_t left, right;
            duel_fb_clear(&left); duel_fb_clear(&right);
            m13_draw_spell(&left, &sp, 0, 0, true, 9u);
            m13_draw_spell(&right, &sp, 0, 0, false, 9u);
            for (int y = 0; y < DUEL_CANVAS_H; y++)
                ok &= !duel_fb_get(&left, 31, y) && !duel_fb_get(&right, 0, y);
            if (p == 224u) break;
        }
    }
    CHECK(ok, "m13_gap_cue_departure_midpoint_arrival_mirrors_and_bounds");
}

static void test_all_forms_bilateral_mirror(void) {
    bool ok = true;
    for (uint8_t form = SPELL_PROJECTILE; form <= SPELL_CONJURE; form++) {
        uint8_t trajectory = form == SPELL_FIREBALL ? TRAJ_ROOF :
                             form == SPELL_GROUND_WAVE ? TRAJ_GROUND :
                             form == SPELL_CHAIN ? TRAJ_HOMING :
                             form == SPELL_CONJURE ? TRAJ_RETURNING : TRAJ_MID;
        uint32_t desc = SPELL_DESC_PACK(form, ELEM_FORCE, PAY_DAMAGE, trajectory,
                                        3, STATUS_NONE,
                                        form == SPELL_SINGULARITY ? INTERACT_ABSORB : INTERACT_SOLID,
                                        TEMPO_RAPID, TREND_ACCELERATING, 2);
        uint8_t progress = form == SPELL_BEAM ? 128u :
                           form == SPELL_SINGULARITY ? 144u :
                           form == SPELL_SWARM ? (uint8_t)((5u << 5) | 14u) :
                           form == SPELL_CHAIN ? 176u :
                           form == SPELL_CONJURE ? (uint8_t)((3u << 5) | 14u) : 72u;
        sim_world_t left_world, right_world;
        sim_init(&left_world, SIMF_AUTHORITATIVE, 0);
        sim_init(&right_world, SIMF_AUTHORITATIVE, 0);
        install_spell(&left_world, 0, desc, progress);
        install_spell(&right_world, 1, desc, progress);
        duel_view_t lv, rv;
        duel_view_from_world(&left_world, &lv); duel_view_from_world(&right_world, &rv);
        duel_view_spell_t ls = duel_view_spell(&lv, 0), rs = duel_view_spell(&rv, 1);
        duel_fb_t ll, lr, rl, rr;
        duel_fb_clear(&ll); duel_fb_clear(&lr); duel_fb_clear(&rl); duel_fb_clear(&rr);
        m13_draw_spell(&ll, &ls, 0, 0, true, 5);
        m13_draw_spell(&lr, &ls, 0, 0, false, 5);
        m13_draw_spell(&rl, &rs, 1, 0, true, 5);
        m13_draw_spell(&rr, &rs, 1, 0, false, 5);
        bool mirrored = exact_mirror(&ll, &rr) && exact_mirror(&lr, &rl);
        if (!mirrored) printf("DIAG bilateral form=%u\n", form);
        ok &= mirrored;
    }
    CHECK(ok, "m13_all_forms_bilateral_pixel_mirror");
}

static void test_aftermath_split_loss_and_reconnect(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.aftermath[0] = (sim_aftermath_t){AFTER_FIRE, 175, 4, RESIDENT_PANIC,
                                       ROOM_DISRUPTED, OBJECT_FIRE};
    w.world_state = WORLD_CRISIS;
    duel_snapshot_t first, later, corrupt, restarted;
    duel_encode(&w, 3, 10, &first);
    duel_rx_state_t rx = {0};
    bool ok = duel_decode_valid(&first) && duel_rx_accept(&rx, &first, false);
    uint8_t old_revision = rx.last.revision;
    wait_ticks(&w, 50);
    duel_encode(&w, 3, 11, &later);
    ok &= later.revision != old_revision && rx.last.revision == old_revision; /* dropped update */
    corrupt = later; corrupt.shared_pres ^= 0x04u;
    ok &= !duel_decode_valid(&corrupt) && rx.last.revision == old_revision;
    ok &= duel_rx_accept(&rx, &later, false) && rx.last.revision == later.revision;
    wait_ticks(&w, 50);
    duel_encode(&w, 4, 1, &restarted);
    ok &= duel_rx_accept(&rx, &restarted, true) &&
          rx.last.session == 4u && rx.last.revision == restarted.revision &&
          M13_AFTER_KIND(rx.last.shared_pres, 0) == AFTER_FIRE;
    CHECK(ok, "m13_aftermath_split_loss_corruption_and_reconnect");
}

int main(void) {
    test_layout_and_protocol();
    test_view_validation();
    test_complexity_formula();
    test_magnitude_thresholds();
    test_compiler_determinism_and_gates();
    test_compiler_reachability();
    test_independent_accumulators_and_commit();
    test_forced_cap_and_rearm();
    test_release_and_prepared();
    test_windup_ignored_input_and_interruption();
    test_ward_capacity_semantics();
    test_regeneration_boundary_and_hit_reset();
    test_damage_heal_ward_and_status();
    test_status_dominance_and_effects();
    test_form_lifecycles();
    test_collision_precedence();
    test_productive_clashes();
    test_m13_link_ordering();
    test_render_purity();
    test_real_input_reachability_and_timing_buckets();
    test_prose_typing_ko_window();
    test_max_cast_aftermath_and_wire();
    test_fireball_room_resident_object_arc();
    test_reachable_complaint_and_ward_shatter();
    test_ground_chain_summon_and_trap();
    test_swarm_gather_launch_and_tempo_motion();
    test_bilateral_beam_and_aftermath_split_render();
    test_resident_occupation_derivation();
    test_resident_geometry_and_object_separation();
    test_health_grid_geometry_and_lifecycles();
    test_local_layer_attunement();
    test_diegetic_scry_instruments();
    test_floor_occupations_and_transitions();
    test_gap_cue_families_temporal_mirrors();
    test_all_forms_bilateral_mirror();
    test_aftermath_split_loss_and_reconnect();
    if (failures) { printf("%d M13 test(s) failed\n", failures); return 1; }
    printf("all M13 tests passed\n");
    return 0;
}
