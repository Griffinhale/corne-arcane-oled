#include "test_harness.h"

static void test_independent_accumulators_and_commit(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    sim_event_t events[2] = {
        SIM_EV_PACK(SIM_EV_KEYDOWN, 0, 0, 1),
        SIM_EV_PACK(SIM_EV_KEYDOWN, 1, 2, 4),
    };
    step(&w, 1u << 1, 1u << 16, 0, 2, events, 2);
    step(&w, 0, 0, 0, 0, NULL, 0);
    bool ok = true;
    EXPECT(w.wiz[0].inc.key_count == 1 && w.wiz[1].inc.key_count == 1 &&
           w.wiz[0].inc.seen_pos == (1u << 1) && w.wiz[1].inc.seen_pos == (1u << 16) &&
           w.wiz[0].inc.hash != w.wiz[1].inc.hash);
    wait_ticks(&w, INCANTATION_IDLE_COMMIT_TICKS - 1u);
    EXPECT(w.wiz[0].inc_state == INC_WINDUP && w.wiz[1].inc_state == INC_WINDUP);
    uint32_t left = w.wiz[0].pending_desc, right = w.wiz[1].pending_desc;
    EXPECT(SPELL_DESC_ELEMENT(left) == ELEM_FROST && SPELL_DESC_ELEMENT(right) == ELEM_EMBER);
    CHECK(ok, "incantation_simultaneous_per_half_idle_commit");
}

static void test_forced_cap_and_rearm(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    sim_event_t event = SIM_EV_PACK(SIM_EV_KEYDOWN, 0, 1, 2);
    uint32_t held = 1u << 8;
    step(&w, held, 0, 0, 0, &event, 1);
    for (unsigned i = 1; i < INCANTATION_FORCE_COMMIT_TICKS; i++)
        step(&w, held, 0, 0, 0, NULL, 0);
    bool ok = true;
    EXPECT(w.wiz[0].rearm_lock && w.wiz[0].inc_state == INC_WINDUP);
    uint8_t count = w.wiz[0].inc.key_count;
    sim_event_t ignored = SIM_EV_PACK(SIM_EV_KEYDOWN, 0, 0, 0);
    step(&w, held | 1u, 0, 0, 0, &ignored, 1);
    EXPECT(w.wiz[0].inc.key_count == count);
    step(&w, 0, 0, 0, 0, NULL, 0);
    EXPECT(!w.wiz[0].rearm_lock);
    CHECK(ok, "incantation_ten_second_force_commit_full_release_rearm");
}

static void test_release_and_prepared(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    release_recipe(&w, 0, 1);
    bool ok = true;
    EXPECT(w.spell[0].active && SPELL_DESC_VALID(w.spell[0].descriptor));
    tap(&w, 0, 2, 2, 0);
    wait_ticks(&w, INCANTATION_IDLE_COMMIT_TICKS - 1u);
    unsigned guard = 0;
    while (w.wiz[0].inc_state == INC_WINDUP && guard++ < 60)
        step(&w, 0, 0, 0, 0, NULL, 0);
    EXPECT(w.wiz[0].inc_state == INC_PREPARED && w.wiz[0].prepared && w.wiz[0].ward_strength);
    while (w.wiz[0].prepared && guard++ < 120)
        step(&w, 0, 0, 0, 0, NULL, 0);
    EXPECT(w.spell[0].active && !w.wiz[0].prepared);
    CHECK(ok, "incantation_one_active_one_prepared_auto_release");
}

static void test_windup_ignored_input_and_interruption(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    tap(&w, 0, 1, 1, 0);
    wait_ticks(&w, INCANTATION_IDLE_COMMIT_TICKS - 1u);
    bool ok = true;
    EXPECT(w.wiz[0].inc_state == INC_WINDUP && w.wiz[0].ward_strength == 1);
    uint8_t count = w.wiz[0].inc.key_count;
    sim_event_t ignored = SIM_EV_PACK(SIM_EV_KEYDOWN, 0, 2, 5);
    step(&w, 1u << 17, 0, 3, 0, &ignored, 1);
    EXPECT(w.wiz[0].inc.key_count == count);
    step(&w, 0, 0, 0, 0, NULL, 0);

    uint32_t hostile = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_VOID, PAY_DAMAGE, TRAJ_LOW, 2,
                                       STATUS_NONE, INTERACT_PHASE, TEMPO_RAPID, TREND_STEADY, 0);
    land_spell(&w, 1, hostile);
    EXPECT(w.wiz[0].inc_state == INC_IDLE && !w.wiz[0].pending_desc && !w.wiz[0].prepared &&
           !w.wiz[0].ward_strength && !w.wiz[0].ward_capacity);
    CHECK(ok, "incantation_windup_input_ignored_and_unblocked_contact_interrupts");
}

static void test_ward_capacity_semantics(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    tap(&w, 0, 1, 1, 0);
    bool ok = true;
    EXPECT(w.wiz[0].inc_state == INC_COLLECTING && w.wiz[0].ward_capacity == 1u &&
           w.wiz[0].ward_strength == 1u);

    uint32_t chip = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE, TRAJ_MID, 1,
                                    STATUS_NONE, INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, 0);
    land_spell(&w, 1, chip);
    EXPECT(w.wiz[0].hp == SIM_MAX_HP && w.wiz[0].ward_capacity == 1u &&
           w.wiz[0].ward_strength == 0u && w.wiz[0].inc_state == INC_COLLECTING);

    bool stayed_spent_below_threshold = true;
    unsigned guard = 0;
    while (w.wiz[0].ward_capacity < 2u && guard++ < 64u) {
        uint8_t pos = (uint8_t)((guard * 7u) % 24u);
        tap(&w, 0, pos / 6u, pos % 6u, (uint8_t)(guard & 3u));
        if (w.wiz[0].ward_capacity == 1u)
            stayed_spent_below_threshold &= w.wiz[0].ward_strength == 0u;
    }
    EXPECT(stayed_spent_below_threshold && w.wiz[0].ward_capacity == 2u &&
           w.wiz[0].ward_strength == 1u); /* only the newly crossed tier returns */

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    tap(&w, 0, 1, 1, 0);
    uint32_t two = desc_set_magnitude_for_test(chip, 2u);
    land_spell(&w, 1, two);
    EXPECT(w.wiz[0].hp == SIM_MAX_HP - 1u && !w.wiz[0].ward_strength && !w.wiz[0].ward_capacity &&
           w.wiz[0].inc_state == INC_IDLE);

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[0].ward_capacity = 2u;
    w.wiz[0].ward_strength = 2u;
    w.wiz[0].ward_focus = 2u;
    uint32_t high = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FROST, PAY_DAMAGE, TRAJ_HIGH, 1,
                                    STATUS_NONE, INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, 0);
    land_spell(&w, 1, high);
    EXPECT(w.wiz[0].hp == SIM_MAX_HP && w.wiz[0].ward_strength == 1u);

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[0].ward_capacity = 1u;
    w.wiz[0].ward_strength = 1u;
    w.wiz[0].ward_focus = 2u;
    land_spell(&w, 1, high);
    EXPECT(w.wiz[0].hp == SIM_MAX_HP - 1u && !w.wiz[0].ward_capacity);

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    release_recipe(&w, 0, 1);
    EXPECT(w.spell[0].active && !w.wiz[0].ward_strength && !w.wiz[0].ward_capacity);
    CHECK(ok, "incantation_ward_capacity_spend_growth_leakage_coverage_and_launch_clear");
}

static void test_regeneration_boundary_and_hit_reset(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[1].hp = SIM_MAX_HP - 2u;
    wait_ticks(&w, SIM_REGEN_TICKS - 1u);
    bool ok = true;
    EXPECT(w.wiz[1].hp == SIM_MAX_HP - 2u && w.wiz[1].regen_ticks == 1u);
    wait_ticks(&w, 1u);
    EXPECT(w.wiz[1].hp == SIM_MAX_HP - 1u && w.wiz[1].regen_ticks == SIM_REGEN_TICKS);

    wait_ticks(&w, SIM_REGEN_TICKS - 2u);
    uint32_t chip = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE, TRAJ_MID, 1,
                                    STATUS_NONE, INTERACT_PHASE, TEMPO_FLOWING, TREND_STEADY, 0);
    land_spell(&w, 0, chip);
    EXPECT(w.wiz[1].hp == SIM_MAX_HP - 2u && w.wiz[1].regen_ticks == SIM_REGEN_TICKS);
    wait_ticks(&w, SIM_REGEN_TICKS - 1u);
    EXPECT(w.wiz[1].hp == SIM_MAX_HP - 2u);
    wait_ticks(&w, 1u);
    EXPECT(w.wiz[1].hp == SIM_MAX_HP - 1u);
    CHECK(ok, "incantation_regeneration_exact_20_seconds_and_damage_reset");
}

/* M15 Track B: stance entry rules, exact timing, the STUDY buff's two arms,
 * MEDITATE's regen/ward gates, FORTIFY's held grant and windup trigger, and
 * the stance wire path through the view's fx_stance nibble. */
static void test_stance_entry_mechanics_and_exit(void) {
    sim_world_t w;
    duel_view_t v;
    bool ok = true;

    /* STUDY: unhurt + neutral temper. Entry lands exactly at
     * SIM_STANCE_ENTRY_TICKS of INC_IDLE and rides the wire. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    wait_ticks(&w, SIM_STANCE_ENTRY_TICKS - 1u);
    EXPECT(w.wiz[0].stance == DUEL_STANCE_NONE && w.wiz[1].stance == DUEL_STANCE_NONE);
    wait_ticks(&w, 1u);
    EXPECT(w.wiz[0].stance == DUEL_STANCE_STUDY && w.wiz[0].studied == 1u &&
           w.wiz[1].stance == DUEL_STANCE_STUDY);
    duel_view_from_world(&w, &v);
    EXPECT(VIEW_FX_STANCE(v.fx_stance, SIM_SIDE_L) == DUEL_STANCE_STUDY &&
           duel_view_wizard(&v, SIM_SIDE_R).stance == DUEL_STANCE_STUDY && duel_view_valid(&v));

    /* Any own keydown exits instantly; the pending buff survives into the
     * commit and shifts a frost recipe to the variant-0 force affinity. */
    release_recipe(&w, 0, 0);
    EXPECT(w.wiz[0].stance == DUEL_STANCE_NONE && w.spell[0].active &&
           SPELL_DESC_ELEMENT(w.spell[0].descriptor) == ELEM_FORCE && w.wiz[0].studied == 0u);

    /* Already-aligned STUDY deepens instead: a force recipe gains +1
     * magnitude over the single-key baseline of 1. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    wait_ticks(&w, SIM_STANCE_ENTRY_TICKS);
    EXPECT(w.wiz[1].stance == DUEL_STANCE_STUDY);
    release_recipe(&w, 1, 1);
    EXPECT(w.spell[1].active && SPELL_DESC_ELEMENT(w.spell[1].descriptor) == ELEM_FORCE &&
           SPELL_DESC_MAGNITUDE(w.spell[1].descriptor) == 2u);

    /* MEDITATE: hurt + cool. Regen burns double while held; the ward is
     * suppressed on the wire but the stored strength survives. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[0].hp = 3u;
    w.wiz[0].temper = 1u;
    w.wiz[0].ward_strength = 2u;
    w.wiz[0].ward_capacity = 2u;
    w.wiz[0].ward_focus = 2u;
    wait_ticks(&w, SIM_STANCE_ENTRY_TICKS);
    EXPECT(w.wiz[0].stance == DUEL_STANCE_MEDITATE);
    uint16_t regen = w.wiz[0].regen_ticks;
    wait_ticks(&w, 10u);
    EXPECT(w.wiz[0].regen_ticks == (uint16_t)(regen - 20u));
    duel_view_from_world(&w, &v);
    EXPECT(duel_view_wizard(&v, SIM_SIDE_L).ward_strength == 0u && w.wiz[0].ward_strength == 2u &&
           duel_view_valid(&v));
    /* A keydown restores the presented ward instantly. */
    tap(&w, 0, 1, 2, 0);
    duel_view_from_world(&w, &v);
    EXPECT(w.wiz[0].stance == DUEL_STANCE_NONE &&
           duel_view_wizard(&v, SIM_SIDE_L).ward_strength == 2u);

    /* While meditating, ward_covers is gated: a coverable chip punches
     * through, and the interruption ends the stance. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[0].hp = 3u;
    w.wiz[0].temper = 1u;
    w.wiz[0].ward_strength = 2u;
    w.wiz[0].ward_capacity = 2u;
    w.wiz[0].ward_focus = 2u;
    wait_ticks(&w, SIM_STANCE_ENTRY_TICKS);
    uint32_t chip = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE, TRAJ_MID, 1,
                                    STATUS_NONE, INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, 0);
    land_spell(&w, 1, chip);
    EXPECT(w.wiz[0].hp == 2u && w.wiz[0].stance == DUEL_STANCE_NONE && w.wiz[0].temper == 2u);

    /* FORTIFY by hot temper: one ward pip exactly at the 50-tick hold. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[0].temper = 7u;
    wait_ticks(&w, SIM_STANCE_ENTRY_TICKS);
    EXPECT(w.wiz[0].stance == DUEL_STANCE_FORTIFY && w.wiz[0].ward_strength == 0u);
    wait_ticks(&w, SIM_STANCE_FORTIFY_HOLD_TICKS - 1u);
    EXPECT(w.wiz[0].ward_strength == 0u);
    wait_ticks(&w, 1u);
    EXPECT(w.wiz[0].ward_strength == 1u);
    wait_ticks(&w, 100u);
    EXPECT(w.wiz[0].ward_strength == 1u); /* granted exactly once */

    /* FORTIFY by visible opponent windup: a hurt neutral wizard paces until
     * the other side starts winding up. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[0].hp = 4u;
    wait_ticks(&w, SIM_STANCE_ENTRY_TICKS + 10u);
    EXPECT(w.wiz[0].stance == DUEL_STANCE_NONE);
    tap(&w, 1, 1, 1, 0);
    wait_ticks(&w, INCANTATION_IDLE_COMMIT_TICKS);
    EXPECT(w.wiz[1].inc_state == INC_WINDUP && w.wiz[0].stance == DUEL_STANCE_FORTIFY);
    CHECK(ok, "incantation_stance_entry_rules_buffs_gates_and_wire_nibble");
}

/* M15 Track B: temperament drift at resolve time, its windup and KO
 * consequences, all clamped and deterministic. */
static void test_temper_drift_windup_and_ko_step(void) {
    sim_world_t w;
    bool ok = true;
    uint32_t chip = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE, TRAJ_MID, 1,
                                    STATUS_NONE, INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, 0);

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    land_spell(&w, 0, chip);
    EXPECT(w.wiz[1].temper == 5u && w.wiz[0].temper == SIM_TEMPER_NEUTRAL);
    w.wiz[1].ward_strength = 4u;
    w.wiz[1].ward_focus = 2u;
    land_spell(&w, 0, chip);
    EXPECT(w.wiz[0].temper == 3u && w.wiz[1].temper == 5u); /* full stop cools */

    /* Windup: hot -2 / cool +2 around the neutral value, same recipe. */
    uint8_t wind[3];
    static const uint8_t tempers[3] = {SIM_TEMPER_NEUTRAL, 7u, 1u};
    for (uint8_t i = 0; i < 3u; i++) {
        sim_init(&w, SIMF_AUTHORITATIVE, 0);
        w.wiz[0].temper = tempers[i];
        tap(&w, 0, 0, 1, 0);
        tap(&w, 0, 1, 3, 0);
        tap(&w, 0, 2, 2, 0);
        wait_ticks(&w, INCANTATION_IDLE_COMMIT_TICKS - 1u);
        EXPECT(w.wiz[0].inc_state == INC_WINDUP);
        wind[i] = w.wiz[0].windup_total;
    }
    EXPECT(wind[0] > INCANTATION_WINDUP_MIN_TICKS + 2u && wind[1] == (uint8_t)(wind[0] - 2u) &&
           wind[2] == (uint8_t)(wind[0] + 2u));

    /* KO steps temper one back toward neutral (after the final hit's +1). */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[1].temper = 7u;
    w.wiz[1].hp = 1u;
    land_spell(&w, 0, chip);
    EXPECT(w.wiz[1].life == LIFE_COLLAPSE && w.wiz[1].temper == 6u);
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[1].temper = 0u;
    w.wiz[1].hp = 1u;
    land_spell(&w, 0, chip);
    EXPECT(w.wiz[1].life == LIFE_COLLAPSE && w.wiz[1].temper == 2u);
    CHECK(ok, "incantation_temper_drift_windup_shift_and_ko_recentering");
}

static void test_damage_heal_ward_and_status(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t damage = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE, TRAJ_MID, 4,
                                      STATUS_NONE, INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, 0);
    w.wiz[1].ward_strength = 2;
    w.wiz[1].ward_focus = 2;
    land_spell(&w, 0, damage);
    bool ok = true;
    EXPECT(w.wiz[1].hp == SIM_MAX_HP - 2u && w.wiz[1].ward_strength == 0);

    uint32_t heal = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_HEAL, TRAJ_RETURNING, 4,
                                    STATUS_NONE, INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, 0);
    w.wiz[0].hp = 5;
    land_spell(&w, 0, heal);
    EXPECT(w.wiz[0].hp == SIM_MAX_HP); /* 5 + 4 clamps at the retuned max */

    uint32_t burn = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_EMBER, PAY_STATUS, TRAJ_MID, 3,
                                    STATUS_BURNING, INTERACT_SOLID, TEMPO_RAPID, TREND_STEADY, 0);
    land_spell(&w, 0, burn);
    uint8_t hp = w.wiz[1].hp;
    EXPECT(w.wiz[1].status == STATUS_BURNING && hp == SIM_MAX_HP - 2u);
    while (!w.wiz[1].status_burned)
        step(&w, 0, 0, 0, 0, NULL, 0);
    EXPECT(w.wiz[1].hp == (uint8_t)(hp - 1u) && w.wiz[1].regen_ticks == SIM_REGEN_TICKS);
    CHECK(ok, "incantation_damage_residual_heal_clamp_and_delayed_burn");
}

static void test_status_dominance_and_effects(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t frozen =
        SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FROST, PAY_STATUS, TRAJ_LOW, 3, STATUS_FROZEN,
                        INTERACT_PHASE, TEMPO_FLOWING, TREND_STEADY, 0);
    land_spell(&w, 0, frozen);
    bool ok = true;
    EXPECT(w.wiz[1].status == STATUS_FROZEN && w.wiz[1].status_intensity == 3);
    uint8_t duration = w.wiz[1].status_ticks;
    uint32_t weak_burn =
        SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_EMBER, PAY_STATUS, TRAJ_LOW, 1, STATUS_BURNING,
                        INTERACT_PHASE, TEMPO_FLOWING, TREND_STEADY, 0);
    land_spell(&w, 0, weak_burn);
    EXPECT(w.wiz[1].status == STATUS_FROZEN && w.wiz[1].status_intensity == 3 &&
           w.wiz[1].status_ticks < duration);

    sim_world_t normal, slowed;
    sim_init(&normal, SIMF_AUTHORITATIVE, 0);
    sim_init(&slowed, SIMF_AUTHORITATIVE, 0);
    uint32_t bolt = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE, TRAJ_MID, 1,
                                    STATUS_NONE, INTERACT_SOLID, TEMPO_RAPID, TREND_STEADY, 0);
    install_spell(&normal, 0, bolt, 0);
    install_spell(&slowed, 0, bolt, 0);
    slowed.wiz[0].status = STATUS_FROZEN;
    slowed.wiz[0].status_intensity = 2;
    slowed.wiz[0].status_ticks = 100;
    step(&normal, 0, 0, 0, 0, NULL, 0);
    step(&normal, 0, 0, 0, 0, NULL, 0);
    step(&slowed, 0, 0, 0, 0, NULL, 0);
    step(&slowed, 0, 0, 0, 0, NULL, 0);
    EXPECT(normal.spell[0].progress == 22 && slowed.spell[0].progress == 11);

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[0].inc_state = INC_PREPARED;
    w.wiz[0].prepared = 1;
    w.wiz[0].prepared_desc = desc_set_magnitude_for_test(bolt, 3);
    w.wiz[0].status = STATUS_DISRUPTED;
    w.wiz[0].status_intensity = 2;
    w.wiz[0].status_ticks = 100;
    step(&w, 0, 0, 0, 0, NULL, 0);
    EXPECT(w.spell[0].active && SPELL_DESC_MAGNITUDE(w.spell[0].descriptor) == 2 &&
           w.wiz[0].status == STATUS_NONE);

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[1].ward_strength = 4;
    w.wiz[1].ward_focus = 2;
    w.wiz[1].status = STATUS_MARKED;
    w.wiz[1].status_intensity = 2;
    w.wiz[1].status_ticks = 100;
    uint32_t area = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE, TRAJ_AREA, 2,
                                    STATUS_NONE, INTERACT_SOLID, TEMPO_RAPID, TREND_STEADY, 0);
    land_spell(&w, 0, area);
    EXPECT(w.wiz[1].hp == SIM_MAX_HP - 2u && w.wiz[1].ward_strength == 0);
    CHECK(ok, "incantation_status_strength_frozen_disrupted_and_marked_effects");
}

static void test_form_lifecycles(void) {
    sim_world_t w;
    bool ok = true;
    uint32_t beam = SPELL_DESC_PACK(SPELL_BEAM, ELEM_FORCE, PAY_DAMAGE, TRAJ_MID, 2, STATUS_NONE,
                                    INTERACT_SOLID, TEMPO_RAPID, TREND_STEADY, 0);
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    install_spell(&w, 0, beam, 0);
    wait_ticks(&w, 5);
    uint8_t hp = w.wiz[1].hp;
    EXPECT(hp == SIM_MAX_HP - 2u && w.spell[0].progress >= 64);
    wait_ticks(&w, 32);
    EXPECT(w.wiz[1].hp == hp && !w.spell[0].active);

    uint32_t singularity =
        SPELL_DESC_PACK(SPELL_SINGULARITY, ELEM_VOID, PAY_DAMAGE, TRAJ_AREA, 2, STATUS_NONE,
                        INTERACT_ABSORB, TEMPO_DELIBERATE, TREND_STEADY, 0);
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    install_spell(&w, 0, singularity, 0);
    wait_ticks(&w, 28);
    EXPECT(!w.spell[0].active && w.wiz[1].hp == SIM_MAX_HP);

    uint32_t swarm = SPELL_DESC_PACK(SPELL_SWARM, ELEM_FORCE, PAY_DAMAGE, TRAJ_MID, 4, STATUS_NONE,
                                     INTERACT_SOLID, TEMPO_FRANTIC, TREND_STEADY, 0);
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    install_spell(&w, 0, swarm, 0);
    w.spell[0].aux = 6;
    wait_ticks(&w, 36);
    EXPECT(!w.spell[0].active && w.wiz[1].hp == SIM_MAX_HP - 6u);
    CHECK(ok, "incantation_beam_once_singularity_empty_and_six_orb_lifecycles");
}

static void test_collision_precedence(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t phase = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_VOID, PAY_DAMAGE, TRAJ_MID, 2,
                                     STATUS_NONE, INTERACT_PHASE, TEMPO_RAPID, TREND_STEADY, 0);
    uint32_t solid = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE, TRAJ_MID, 2,
                                     STATUS_NONE, INTERACT_SOLID, TEMPO_RAPID, TREND_STEADY, 0);
    install_spell(&w, 0, phase, 120);
    install_spell(&w, 1, solid, 120);
    step(&w, 0, 0, 0, 0, NULL, 0);
    bool ok = true;
    EXPECT(w.spell[0].active && w.spell[1].active);

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t sing =
        SPELL_DESC_PACK(SPELL_SINGULARITY, ELEM_FORCE, PAY_DAMAGE, TRAJ_AREA, 2, STATUS_NONE,
                        INTERACT_ABSORB, TEMPO_DELIBERATE, TREND_STEADY, 0);
    install_spell(&w, 0, sing, 48);
    w.spell[0].age = 10;
    install_spell(&w, 1, solid, 207);
    step(&w, 0, 0, 0, 0, NULL, 0);
    EXPECT(w.spell[0].active && w.spell[0].aux == 4 && !w.spell[1].active);

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t ember = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_EMBER, PAY_DAMAGE, TRAJ_MID, 2,
                                     STATUS_NONE, INTERACT_SOLID, TEMPO_RAPID, TREND_STEADY, 0);
    uint32_t frost = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FROST, PAY_DAMAGE, TRAJ_MID, 2,
                                     STATUS_NONE, INTERACT_SOLID, TEMPO_RAPID, TREND_STEADY, 0);
    install_spell(&w, 0, ember, 120);
    install_spell(&w, 1, frost, 120);
    step(&w, 0, 0, 0, 0, NULL, 0);
    EXPECT(!w.spell[0].active && !w.spell[1].active);
    CHECK(ok, "incantation_collision_phase_singularity_and_ember_frost_precedence");
}

static uint32_t clash_desc(uint8_t element, uint8_t magnitude, uint8_t tempo, uint8_t trend) {
    return SPELL_DESC_PACK(SPELL_PROJECTILE, element, PAY_DAMAGE, TRAJ_MID, magnitude, STATUS_NONE,
                           INTERACT_SOLID, tempo, trend, 0);
}

static uint32_t form_desc(uint8_t form, uint8_t trajectory, uint8_t magnitude, uint8_t tempo,
                          uint8_t trend) {
    return SPELL_DESC_PACK(form, ELEM_FORCE, PAY_DAMAGE, trajectory, magnitude, STATUS_NONE,
                           INTERACT_SOLID, tempo, trend, 0);
}

// Mirror-form duels resolve symmetrically (magnitude, then the tempo/trend
// tiebreak, dead tie annihilating both) instead of silently favouring the
// left slot.
static void test_mirror_form_collisions(void) {
    sim_world_t w;
    bool ok = true;

    /* Beams: dead tie burns both out. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t beam = form_desc(SPELL_BEAM, TRAJ_MID, 2, TEMPO_FLOWING, TREND_STEADY);
    install_spell(&w, 0, beam, 120);
    install_spell(&w, 1, beam, 120);
    idle_step(&w);
    EXPECT(!w.spell[0].active && !w.spell[1].active);

    /* Beams: the better-paced RIGHT beam survives (the old code would have
     * kept the left one regardless). */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    install_spell(&w, 0, beam, 120);
    install_spell(&w, 1, form_desc(SPELL_BEAM, TRAJ_MID, 2, TEMPO_FRANTIC, TREND_STEADY), 120);
    idle_step(&w);
    EXPECT(!w.spell[0].active && w.spell[1].active);

    /* Chains: the stronger side survives, and equal-magnitude survivors pay
     * the ordinary one-step chain toll; a dead tie consumes both. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t chain3 = form_desc(SPELL_CHAIN, TRAJ_HOMING, 3, TEMPO_RAPID, TREND_STEADY);
    install_spell(&w, 0, form_desc(SPELL_CHAIN, TRAJ_HOMING, 2, TEMPO_RAPID, TREND_STEADY), 120);
    install_spell(&w, 1, chain3, 120);
    idle_step(&w);
    EXPECT(!w.spell[0].active && w.spell[1].active &&
           SPELL_DESC_MAGNITUDE(w.spell[1].descriptor) == 3u && w.fx_kind == FX_RESIDUE);
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    install_spell(&w, 0, chain3, 120);
    install_spell(&w, 1, chain3, 120);
    idle_step(&w);
    EXPECT(!w.spell[0].active && !w.spell[1].active && w.fx_kind == FX_RESIDUE);
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    install_spell(&w, 0, chain3, 120);
    install_spell(&w, 1, form_desc(SPELL_CHAIN, TRAJ_HOMING, 3, TEMPO_FRANTIC, TREND_STEADY), 120);
    idle_step(&w);
    EXPECT(!w.spell[0].active && w.spell[1].active &&
           SPELL_DESC_MAGNITUDE(w.spell[1].descriptor) == 2u);

    /* Swarms trade one mote each per contact tick (the old code bled only
     * the left swarm). progress 49 puts both swarms mid-gap in contact. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t swarm = form_desc(SPELL_SWARM, TRAJ_MID, 2, TEMPO_DELIBERATE, TREND_STEADY);
    install_spell(&w, 0, swarm, 49);
    w.spell[0].aux = 2;
    install_spell(&w, 1, swarm, 49);
    w.spell[1].aux = 1;
    idle_step(&w);
    EXPECT(w.spell[0].active && w.spell[0].aux == 1u && !w.spell[1].active);
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    install_spell(&w, 0, swarm, 49);
    w.spell[0].aux = 1;
    install_spell(&w, 1, swarm, 49);
    w.spell[1].aux = 1;
    idle_step(&w);
    EXPECT(!w.spell[0].active && !w.spell[1].active);
    CHECK(ok, "incantation_mirror_beam_chain_swarm_symmetric_resolution");
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
    EXPECT(w.spell[0].active && !w.spell[1].active &&
           SPELL_DESC_MAGNITUDE(w.spell[0].descriptor) == 4u);

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    collide(&w, clash_desc(ELEM_FROST, 2, TEMPO_RAPID, TREND_STEADY),
            clash_desc(ELEM_FROST, 2, TEMPO_FLOWING, TREND_IRREGULAR));
    EXPECT(w.spell[0].active && !w.spell[1].active &&
           SPELL_DESC_MAGNITUDE(w.spell[0].descriptor) == 4u);

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    collide(&w, clash_desc(ELEM_FORCE, 2, TEMPO_RAPID, TREND_ACCELERATING),
            clash_desc(ELEM_FORCE, 2, TEMPO_RAPID, TREND_STEADY));
    EXPECT(w.spell[0].active && !w.spell[1].active);

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t exact = clash_desc(ELEM_FORCE, 2, TEMPO_FLOWING, TREND_STEADY);
    collide(&w, exact, exact);
    EXPECT(!w.spell[0].active && !w.spell[1].active && w.wiz[0].hp == SIM_MAX_HP - 1u &&
           w.wiz[1].hp == SIM_MAX_HP - 1u);

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[0].ward_capacity = 4u;
    w.wiz[0].ward_strength = 4u;
    w.wiz[1].ward_capacity = 4u;
    w.wiz[1].ward_strength = 4u;
    collide(&w, exact, exact);
    EXPECT(w.wiz[0].hp == SIM_MAX_HP && w.wiz[1].hp == SIM_MAX_HP && w.wiz[0].ward_strength == 3u &&
           w.wiz[1].ward_strength == 3u);

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[0].ward_capacity = 4u;
    w.wiz[0].ward_strength = 4u;
    collide(&w, clash_desc(ELEM_EMBER, 4, TEMPO_FRANTIC, TREND_IRREGULAR),
            clash_desc(ELEM_FROST, 4, TEMPO_FRANTIC, TREND_IRREGULAR));
    EXPECT(w.wiz[0].hp == SIM_MAX_HP && w.wiz[0].ward_strength == 3u &&
           w.wiz[1].hp == SIM_MAX_HP - 1u);

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    collide(&w, clash_desc(ELEM_FORCE, 2, TEMPO_FLOWING, TREND_STEADY),
            clash_desc(ELEM_VOID, 2, TEMPO_FLOWING, TREND_STEADY));
    EXPECT(!w.spell[0].active && !w.spell[1].active && w.wiz[0].hp == SIM_MAX_HP &&
           w.wiz[1].hp == SIM_MAX_HP);

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t maximum = clash_desc(ELEM_FORCE, 4, TEMPO_FLOWING, TREND_STEADY);
    land_spell(&w, 0, maximum);
    EXPECT(w.wiz[1].hp == SIM_MAX_HP - 4u);
    CHECK(ok, "incantation_productive_clash_cap_tiebreak_pulses_wards_and_damage_cap");
}

static void test_incantation_link_ordering(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    duel_snapshot_t a, b, old_session;
    test_encode_snapshot(&w, 7, 0xffffu, &a);
    test_encode_snapshot(&w, 7, 0u, &b);
    test_encode_snapshot(&w, 6, 100u, &old_session);
    duel_rx_state_t rx = {0};
    bool ok = true;
    EXPECT(duel_rx_accept(&rx, &a, false) && duel_rx_accept(&rx, &b, false) &&
           !duel_rx_accept(&rx, &a, false) && duel_rx_accept(&rx, &old_session, false));
    duel_snapshot_t corrupt = b;
    corrupt.view.phase[0] ^= 0x40u;
    EXPECT(!duel_decode_valid(&corrupt));
    CHECK(ok, "incantation_sequence_wrap_session_restart_and_corruption");
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
    duel_scene_draw(&fb, &render, true, 7, false);
    bool nonempty = false;
    for (size_t i = 0; i < sizeof fb.bits; i++)
        nonempty |= fb.bits[i] != 0;
    CHECK(nonempty && memcmp(&w, &before, sizeof w) == 0,
          "incantation_render_nonempty_and_authoritative_pure");
}

static uint32_t compile_actual_pattern(uint32_t seed, uint8_t extra_gap) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    static const uint8_t pos[4] = {0, 7, 14, 21};
    for (uint8_t i = 0; i < 4u; i++) {
        tap(&w, 0, pos[i] / 6u, pos[i] % 6u, (uint8_t)((seed + i) & 3u));
        if (i != 3u)
            wait_ticks(&w, extra_gap);
    }
    wait_ticks(&w, INCANTATION_IDLE_COMMIT_TICKS + 1u);
    return w.wiz[0].pending_desc;
}

static void test_real_input_reachability_and_timing_buckets(void) {
    uint32_t bucket_a = compile_actual_pattern(3u, 1u);
    uint32_t bucket_b = compile_actual_pattern(3u, 2u);
    bool ok = true;
    EXPECT(bucket_a == bucket_b);
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
        uint8_t complexity = incantation_complexity(&w.wiz[0].inc);
        if (complexity > max_complexity)
            max_complexity = complexity;
        wait_ticks(&w, INCANTATION_IDLE_COMMIT_TICKS + 1u);
        if (SPELL_DESC_VALID(w.wiz[0].pending_desc))
            forms |= 1u << SPELL_DESC_FORM(w.wiz[0].pending_desc);
    }
    if (!ok || forms != 0xffu)
        printf("DIAG actual forms=%02x complexity=%u bucket_a=%06x bucket_b=%06x\n",
               (unsigned)forms, max_complexity, (unsigned)bucket_a, (unsigned)bucket_b);
    EXPECT(forms == 0xffu);
    CHECK(ok, "incantation_real_input_all_forms_and_bucket_repeatability");
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
                uint8_t pos = (uint8_t)((rank * (side ? 7u : 5u) + profile * 3u + side * 11u +
                                         tick / period[profile][side]) %
                                        24u);
                uint8_t layer = profile == 2u                 ? (uint8_t)((rank + side) & 3u)
                                : profile == 1u && rank >= 8u ? (uint8_t)(1u + side)
                                                              : 0u;
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
    /* Re-measured after Track T (HP 12->8, regen 20 s) AND Track B: first
     * KOs land at 398/1367/1044 ticks (~16/55/42 s). Profiles 1-2 sit at
     * pre-B pacing (FORTIFY wards absorb what STUDY adds), but profile 0's
     * steady phrases open with a STUDY-buffed magnitude-3 swarm — five
     * 1-hp pulses — whose per-pulse temper drift then doubles the fireball
     * weight: a deliberate escalation spiral, measured here so a future
     * change that tightens it further trips the bound. Whether ~16 s to
     * first blood feels restless on the desk is backlog Q4 (hardware). */
    bool ok = true;
    for (uint8_t profile = 0; profile < 3u; profile++) {
        uint32_t ko = prose_workload_first_ko(profile);
        if (ko < 350u || ko > 3750u)
            printf("DIAG prose profile=%u first_ko_ticks=%lu\n", profile, (unsigned long)ko);
        EXPECT(ko >= 350u && ko <= 3750u);
    }
    CHECK(ok, "incantation_steady_burst_mixed_prose_first_ko_14_to_150_seconds");
}

static void test_max_cast_aftermath_and_wire(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    sim_event_t event = SIM_EV_PACK(SIM_EV_KEYDOWN, 0, 1, 2);
    uint32_t held = 1u << 8;
    step(&w, held, 0, 0, 0, &event, 1);
    for (unsigned i = 1; i < INCANTATION_FORCE_COMMIT_TICKS; i++)
        step(&w, held, 0, 0, 0, NULL, 0);
    uint8_t shared = incantation_aftermath_shared(&w);
    uint8_t revision = incantation_aftermath_revision(&w);
    bool ok = true;
    EXPECT(w.aftermath[0].kind == AFTER_MAX_CAST && w.aftermath[1].kind == AFTER_MAX_CAST &&
           w.aftermath[0].resident_state == RESIDENT_WATCH_CAST && w.world_state == WORLD_WONDER &&
           INCANTATION_AFTER_KIND(shared, 0) == AFTER_MAX_CAST &&
           INCANTATION_AFTER_KIND(shared, 1) == AFTER_MAX_CAST &&
           (revision & INCANTATION_AFTERMATH_WIRE));
    duel_snapshot_t packet;
    test_encode_snapshot(&w, 4, 9, &packet);
    EXPECT(packet.shared_pres == shared && packet.revision == revision &&
           duel_decode_valid(&packet));
    duel_render_t render = {0};
    duel_render_from_world(&render, &w);
    EXPECT(render.shared_pres == shared && render.revision == revision);
    step(&w, 0, 0, 0, 0, NULL, 0);
    /* Wait to one tick shy of the halfway boundary (phase 2 = cheer), then
     * cross it. The arc started during the casts above, so derive the elapsed
     * ticks from the countdown rather than assuming a fresh start. */
    unsigned elapsed = SIM_AFTER_MAX_CAST_TICKS - w.aftermath[0].ticks;
    wait_ticks(&w, SIM_AFTER_MAX_CAST_TICKS / 2u - 1u - elapsed);
    EXPECT(w.aftermath[0].kind == AFTER_MAX_CAST &&
           w.aftermath[0].resident_state == RESIDENT_WATCH_CAST);
    wait_ticks(&w, 2u);
    EXPECT(w.aftermath[0].resident_state == RESIDENT_CHEER);
    CHECK(ok, "incantation_max_cast_coordinated_authoritative_wire_aftermath");
}

static void test_fireball_room_resident_object_arc(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t fireball =
        SPELL_DESC_PACK(SPELL_FIREBALL, ELEM_EMBER, PAY_DAMAGE, TRAJ_ROOF, 3, STATUS_NONE,
                        INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, 0);
    land_spell(&w, 0, fireball);
    bool ok = true;
    EXPECT(!w.spell[0].active && w.wiz[1].hp == SIM_MAX_HP - 3u && w.fx_kind == FX_DETONATE &&
           w.aftermath[1].kind == AFTER_FIRE && w.aftermath[1].resident_state == RESIDENT_PANIC &&
           w.aftermath[1].room_state == ROOM_DISRUPTED &&
           w.aftermath[1].object_state == OBJECT_FIRE && w.world_state == WORLD_CRISIS);
    /* Quarter-phase boundaries of the fire arc: response, recovery, expiry. */
    wait_ticks(&w, SIM_AFTER_FIRE_TICKS / 4u + 1u);
    EXPECT(w.aftermath[1].resident_state == RESIDENT_FIGHT_FIRE &&
           w.aftermath[1].object_state == OBJECT_FIRE);
    wait_ticks(&w, SIM_AFTER_FIRE_TICKS / 2u + 1u);
    EXPECT(w.aftermath[1].resident_state == RESIDENT_REPAIR &&
           w.aftermath[1].room_state == ROOM_RECOVERY &&
           w.aftermath[1].object_state == OBJECT_DAMAGED);
    wait_ticks(&w, SIM_AFTER_FIRE_TICKS / 4u + 2u);
    EXPECT(w.aftermath[1].kind == AFTER_NONE && w.world_state == WORLD_CALM);
    CHECK(ok, "incantation_fireball_roof_resident_room_object_recovery_arc");
}

static void test_reachable_complaint_and_ward_shatter(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t chip = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE, TRAJ_MID, 1,
                                    STATUS_NONE, INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, 0);
    w.wiz[1].ward_strength = 1;
    w.wiz[1].ward_focus = 2;
    land_spell(&w, 0, chip);
    bool ok = true;
    EXPECT(w.fx_kind == FX_WARD_SHATTER_R && w.wiz[1].hp == SIM_MAX_HP &&
           w.wiz[1].ward_strength == 0);
    land_spell(&w, 0, chip);
    EXPECT(w.fx_kind == FX_COMPLAINT && w.wiz[1].hp == SIM_MAX_HP - 1u &&
           w.aftermath[1].kind == AFTER_COMPLAINT &&
           w.aftermath[1].resident_state == RESIDENT_COMPLAIN);
    CHECK(ok, "incantation_ward_shatter_and_complaint_reachable");
}

static void test_ground_chain_summon_and_trap(void) {
    sim_world_t w;
    uint32_t ground = SPELL_DESC_PACK(SPELL_GROUND_WAVE, ELEM_FORCE, PAY_DAMAGE, TRAJ_GROUND, 2,
                                      STATUS_NONE, INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, 0);
    uint32_t high = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FROST, PAY_DAMAGE, TRAJ_HIGH, 2,
                                    STATUS_NONE, INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, 0);
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    install_spell(&w, 0, ground, 120);
    install_spell(&w, 1, high, 120);
    step(&w, 0, 0, 0, 0, NULL, 0);
    bool ok = true;
    EXPECT(w.spell[0].active && w.spell[1].active);

    uint32_t chain =
        SPELL_DESC_PACK(SPELL_CHAIN, ELEM_FORCE, PAY_DAMAGE, TRAJ_HOMING, 2, STATUS_NONE,
                        INTERACT_SOLID, TEMPO_RAPID, TREND_ACCELERATING, 0);
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    install_spell(&w, 0, chain, 120);
    install_spell(&w, 1, high, 120);
    step(&w, 0, 0, 0, 0, NULL, 0);
    EXPECT(w.spell[0].active && !w.spell[1].active && w.fx_kind == FX_RESIDUE);

    uint32_t trap = SPELL_DESC_PACK(SPELL_CONJURE, ELEM_EMBER, PAY_DAMAGE, TRAJ_GROUND, 2,
                                    STATUS_NONE, INTERACT_SOLID, TEMPO_DELIBERATE, TREND_STEADY, 0);
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    install_spell(&w, 0, trap, 16);
    w.spell[0].aux = 3;
    install_spell(&w, 1, high, 175);
    step(&w, 0, 0, 0, 0, NULL, 0);
    EXPECT(!w.spell[0].active && !w.spell[1].active && w.wiz[1].hp == SIM_MAX_HP - 2u &&
           w.fx_kind == FX_DETONATE);

    uint32_t summon = SPELL_DESC_PACK(SPELL_CONJURE, ELEM_FORCE, PAY_DAMAGE, TRAJ_RETURNING, 2,
                                      STATUS_NONE, INTERACT_SOLID, TEMPO_FRANTIC, TREND_STEADY, 0);
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    install_spell(&w, 0, summon, 0);
    w.spell[0].aux = 2;
    wait_ticks(&w, 22);
    EXPECT(!w.spell[0].active && w.wiz[1].hp == SIM_MAX_HP - 2u);
    CHECK(ok, "incantation_ground_chain_summon_and_trap_lifecycles");
}

static void test_swarm_gather_launch_and_tempo_motion(void) {
    sim_world_t w;
    uint32_t swarm = SPELL_DESC_PACK(SPELL_SWARM, ELEM_FORCE, PAY_DAMAGE, TRAJ_MID, 4, STATUS_NONE,
                                     INTERACT_SOLID, TEMPO_FRANTIC, TREND_ACCELERATING, 0);
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    install_spell(&w, 0, swarm, 0);
    w.spell[0].aux = 6;
    wait_ticks(&w, 11);
    bool ok = true;
    EXPECT(w.wiz[1].hp == SIM_MAX_HP && (w.spell[0].progress >> 5) == 6u &&
           (w.spell[0].progress & 31u) < 12u);
    wait_ticks(&w, 4);
    EXPECT(w.wiz[1].hp == SIM_MAX_HP && (w.spell[0].progress & 31u) >= 12u);
    wait_ticks(&w, 1);
    EXPECT(w.wiz[1].hp == SIM_MAX_HP - 1u && (w.spell[0].progress >> 5) == 5u);
    wait_ticks(&w, 20);
    EXPECT(!w.spell[0].active && w.wiz[1].hp == SIM_MAX_HP - 6u);

    uint32_t slow =
        SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE, TRAJ_LOW, 1, STATUS_NONE,
                        INTERACT_SOLID, TEMPO_DELIBERATE, TREND_DECELERATING, 0);
    uint32_t fast =
        SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE, TRAJ_LOW, 1, STATUS_NONE,
                        INTERACT_SOLID, TEMPO_FRANTIC, TREND_ACCELERATING, 0);
    sim_world_t a, b;
    sim_init(&a, SIMF_AUTHORITATIVE, 0);
    sim_init(&b, SIMF_AUTHORITATIVE, 0);
    install_spell(&a, 0, slow, 0);
    install_spell(&b, 0, fast, 0);
    wait_ticks(&a, 8);
    wait_ticks(&b, 8);
    EXPECT(b.spell[0].progress > a.spell[0].progress);
    CHECK(ok, "incantation_swarm_gather_serial_launch_and_tempo_trend_motion");
}

static void test_bilateral_beam_and_aftermath_split_render(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t beam = SPELL_DESC_PACK(SPELL_BEAM, ELEM_FORCE, PAY_DAMAGE, TRAJ_MID, 3, STATUS_NONE,
                                    INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, 0);
    install_spell(&w, 1, beam, 128);
    w.aftermath[0] =
        (sim_aftermath_t){AFTER_INSPECT, 80, 2, RESIDENT_INSPECT, ROOM_ALERT, OBJECT_RESIDUE};
    w.world_state = WORLD_RECOVERY;
    duel_render_t master = {0};
    duel_render_from_world(&master, &w);
    master.seed = 9;
    master.civic_phase = 12;
    duel_fb_t ml, mr;
    duel_fb_clear(&ml);
    duel_fb_clear(&mr);
    duel_scene_draw(&ml, &master, true, 0, false);
    duel_scene_draw(&mr, &master, false, 0, false);
    int beam_y = 63 + DUEL_ROOF_DY;
    bool ok = true;
    EXPECT(duel_fb_get(&ml, 21, beam_y) && duel_fb_get(&ml, 31, beam_y) &&
           duel_fb_get(&mr, 0, beam_y) && duel_fb_get(&mr, 10, beam_y) &&
           !duel_fb_get(&ml, 0, beam_y) && !duel_fb_get(&mr, 31, beam_y));

    duel_snapshot_t packet;
    test_encode_snapshot(&w, 8, 20, &packet);
    duel_rx_state_t rx = {0};
    EXPECT(duel_rx_accept(&rx, &packet, false) && duel_decode_valid(&rx.last));
    duel_render_t slave = master;
    slave.view = rx.last.view;
    slave.shared_pres = rx.last.shared_pres;
    slave.revision = rx.last.revision;
    duel_fb_t sl, sr;
    duel_fb_clear(&sl);
    duel_fb_clear(&sr);
    duel_scene_draw(&sl, &slave, true, 0, false);
    duel_scene_draw(&sr, &slave, false, 0, false);
    EXPECT(memcmp(&ml, &sl, sizeof ml) == 0 && memcmp(&mr, &sr, sizeof mr) == 0);
    CHECK(ok, "incantation_bilateral_beam_and_aftermath_split_render_convergence");
}

void run_combat_lifecycle_tests(void) {
    test_independent_accumulators_and_commit();
    test_forced_cap_and_rearm();
    test_release_and_prepared();
    test_windup_ignored_input_and_interruption();
    test_ward_capacity_semantics();
    test_regeneration_boundary_and_hit_reset();
    test_stance_entry_mechanics_and_exit();
    test_temper_drift_windup_and_ko_step();
    test_damage_heal_ward_and_status();
    test_status_dominance_and_effects();
    test_form_lifecycles();
    test_collision_precedence();
    test_mirror_form_collisions();
    test_productive_clashes();
    test_incantation_link_ordering();
    test_render_purity();
    test_real_input_reachability_and_timing_buckets();
    test_prose_typing_ko_window();
    test_max_cast_aftermath_and_wire();
    test_fireball_room_resident_object_arc();
    test_reachable_complaint_and_ward_shatter();
    test_ground_chain_summon_and_trap();
    test_swarm_gather_launch_and_tempo_motion();
    test_bilateral_beam_and_aftermath_split_render();
}
