#include "test_harness.h"

static uint32_t carrier(uint8_t form, uint8_t element, uint8_t payload, uint8_t trajectory,
                        uint8_t magnitude, uint8_t status) {
    return SPELL_DESC_PACK(form, element, payload, trajectory, magnitude, status, INTERACT_SOLID,
                           TEMPO_FLOWING, TREND_STEADY, 0u);
}

static void put_field(sim_world_t *world, uint8_t slot, uint8_t kind, uint8_t zone, uint8_t owner,
                      uint32_t descriptor, uint8_t auxiliary, uint16_t timer) {
    world->field[slot] = (sim_field_t){
        .descriptor = descriptor,
        .timer = timer,
        .aux = auxiliary,
        .kind = kind,
        .zone = zone,
        .owner = owner,
    };
}

static uint32_t fixture_next(uint32_t *state) {
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

/* These seeds are compact physical-input fixtures. They pass only positions,
 * normalized layers, held levels, and fixed gaps through the real collection
 * API; no descriptor field is written by the fixture. */
static uint32_t compile_fixture(uint32_t seed, uint8_t variant, bool wall_fixture) {
    uint32_t random = seed;
    sim_incantation_t incantation;
    incantation_collection_reset(&incantation);
    uint8_t count = (uint8_t)(12u + (fixture_next(&random) >> 16) % 36u);
    for (uint8_t key = 0; key < count; key++) {
        uint8_t gap = (uint8_t)((fixture_next(&random) >> 16) % 9u);
        uint8_t position = (uint8_t)((fixture_next(&random) >> 16) % 24u);
        uint8_t layer =
            wall_fixture ? (uint8_t)(seed & 3u) : (uint8_t)((fixture_next(&random) >> 24) % 4u);
        uint8_t hold = wall_fixture ? (uint8_t)(3u + (fixture_next(&random) >> 31))
                                    : (uint8_t)(1u + (fixture_next(&random) >> 24) % 4u);
        while (gap--)
            incantation_collection_tick(&incantation, 0u);
        incantation_collection_keydown(&incantation, position, layer);
        while (hold--)
            incantation_collection_tick(&incantation, 1u << position);
        incantation_collection_keyup(&incantation, position);
    }
    return incantation_compile(&incantation, variant, SIM_TEMPER_NEUTRAL);
}

static void test_signature_predicates_and_physical_fixtures(void) {
    static const struct {
        uint32_t seed;
        uint8_t variant;
        uint8_t signature;
        bool wall_fixture;
    } fixtures[] = {
        {3u, 2u, SPELL_SIGNATURE_RUNE, false}, {11u, 3u, SPELL_SIGNATURE_FAMILIAR, false},
        {1u, 0u, SPELL_SIGNATURE_WALL, true},  {16u, 3u, SPELL_SIGNATURE_VORTEX, false},
        {1u, 0u, SPELL_SIGNATURE_ECHO, false}, {24u, 0u, SPELL_SIGNATURE_BLOOM, false},
    };
    bool ok = true;
    for (size_t i = 0; i < sizeof fixtures / sizeof fixtures[0]; i++) {
        uint32_t descriptor =
            compile_fixture(fixtures[i].seed, fixtures[i].variant, fixtures[i].wall_fixture);
        EXPECT(SPELL_DESC_VALID(descriptor) &&
               incantation_signature(descriptor) == fixtures[i].signature);
    }

    EXPECT(incantation_signature(carrier(SPELL_CONJURE, ELEM_FORCE, PAY_STATUS, TRAJ_GROUND, 2u,
                                         STATUS_MARKED)) == SPELL_SIGNATURE_RUNE);
    EXPECT(incantation_signature(carrier(SPELL_CONJURE, ELEM_FORCE, PAY_DAMAGE, TRAJ_RETURNING, 2u,
                                         STATUS_NONE)) == SPELL_SIGNATURE_FAMILIAR);
    EXPECT(incantation_signature(carrier(SPELL_GROUND_WAVE, ELEM_FROST, PAY_STATUS, TRAJ_GROUND, 2u,
                                         STATUS_FROZEN)) == SPELL_SIGNATURE_WALL);
    EXPECT(incantation_signature(carrier(SPELL_SINGULARITY, ELEM_VOID, PAY_DAMAGE, TRAJ_HOMING, 2u,
                                         STATUS_NONE)) == SPELL_SIGNATURE_VORTEX);
    EXPECT(incantation_signature(carrier(SPELL_FIREBALL, ELEM_EMBER, PAY_DAMAGE, TRAJ_ROOF, 2u,
                                         STATUS_NONE)) == SPELL_SIGNATURE_BASE);
    CHECK(ok, "magic_six_signatures_have_reachable_physical_input_fixtures_and_stable_precedence");
}

static void test_field_collision_table_and_order(void) {
    bool ok = true;
    sim_world_t world;
    uint32_t force1 = carrier(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE, TRAJ_MID, 1u, STATUS_NONE);
    uint32_t force3 = carrier(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE, TRAJ_LOW, 3u, STATUS_NONE);

    sim_init(&world, SIMF_AUTHORITATIVE, 0u);
    put_field(&world, 0u, FIELD_TRAP, SIM_RESIDUE_MID_L, SIM_SIDE_R, force3, 3u, 50u);
    install_spell(&world, SIM_SIDE_L, force1, 80u);
    idle_step(&world);
    EXPECT(world.field[0].kind == FIELD_NONE && !world.spell[0].active &&
           world.wiz[SIM_SIDE_L].hp == SIM_MAX_HP - 2u && world.fx_kind == FX_DETONATE);

    sim_init(&world, SIMF_AUTHORITATIVE, 0u);
    put_field(&world, 0u, FIELD_SINGULARITY, SIM_RESIDUE_MID_L, SIM_SIDE_R,
              carrier(SPELL_SINGULARITY, ELEM_VOID, PAY_DAMAGE, TRAJ_MID, 2u, STATUS_NONE), 0u,
              40u);
    install_spell(&world, SIM_SIDE_L, force1, 80u);
    idle_step(&world);
    EXPECT(world.field[0].kind == FIELD_SINGULARITY && world.field[0].aux == 1u &&
           !world.spell[0].active);
    install_spell(&world, SIM_SIDE_L,
                  carrier(SPELL_BEAM, ELEM_FORCE, PAY_DAMAGE, TRAJ_MID, 3u, STATUS_NONE), 80u);
    idle_step(&world);
    EXPECT(world.field[0].kind == FIELD_NONE && world.spell[0].active);

    static const uint8_t cloud_elements[] = {ELEM_EMBER, ELEM_FROST, ELEM_VOID, ELEM_FORCE};
    for (size_t element = 0; element < sizeof cloud_elements; element++) {
        sim_init(&world, SIMF_AUTHORITATIVE, 0u);
        put_field(&world, 0u, FIELD_STEAM, SIM_RESIDUE_MID_L, SIM_SIDE_R, force1, 0u, 50u);
        install_spell(&world, SIM_SIDE_L,
                      carrier(SPELL_PROJECTILE, cloud_elements[element], PAY_DAMAGE, TRAJ_MID,
                              element == 2u ? 3u : 2u, STATUS_NONE),
                      80u);
        idle_step(&world);
        if (element < 2u)
            EXPECT(world.spell[0].active && SPELL_DESC_MAGNITUDE(world.spell[0].descriptor) == 1u &&
                   world.field[0].kind == FIELD_STEAM);
        else if (element == 2u)
            EXPECT(world.spell[0].active && SPELL_DESC_MAGNITUDE(world.spell[0].descriptor) == 4u &&
                   world.field[0].kind == FIELD_STEAM);
        else
            EXPECT(world.spell[0].active && world.field[0].kind == FIELD_NONE);
    }

    sim_init(&world, SIMF_AUTHORITATIVE, 0u);
    put_field(&world, 0u, FIELD_RUNE, SIM_RESIDUE_MID_L, SIM_SIDE_L, force3, 0u, 50u);
    install_spell(&world, SIM_SIDE_L, force3, 80u);
    idle_step(&world);
    EXPECT(world.field[0].kind == FIELD_NONE &&
           SPELL_DESC_MAGNITUDE(world.spell[0].descriptor) == 4u);

    sim_init(&world, SIMF_AUTHORITATIVE, 0u);
    /* field_step runs first; timer 41 advances a right-owned familiar to its
     * mid-left interception zone before collision resolution. */
    put_field(&world, 0u, FIELD_FAMILIAR, SIM_RESIDUE_MID_L, SIM_SIDE_R, force1, 1u, 41u);
    install_spell(&world, SIM_SIDE_L, force3, 80u);
    idle_step(&world);
    EXPECT(world.field[0].kind == FIELD_NONE && !world.spell[0].active);

    sim_init(&world, SIMF_AUTHORITATIVE, 0u);
    put_field(&world, 0u, FIELD_WALL, SIM_RESIDUE_MID_L, SIM_SIDE_R, force3, 2u, 50u);
    install_spell(&world, SIM_SIDE_L, force1, 80u);
    idle_step(&world);
    EXPECT(world.field[0].kind == FIELD_WALL && world.field[0].aux == 1u && !world.spell[0].active);
    put_field(&world, 0u, FIELD_WALL, SIM_RESIDUE_MID_L, SIM_SIDE_R, force3, 2u, 50u);
    install_spell(&world, SIM_SIDE_L, force3, 80u);
    idle_step(&world);
    EXPECT(world.field[0].kind == FIELD_NONE && world.spell[0].active &&
           SPELL_DESC_MAGNITUDE(world.spell[0].descriptor) == 1u);

    sim_init(&world, SIMF_AUTHORITATIVE, 0u);
    put_field(&world, 0u, FIELD_VORTEX, SIM_RESIDUE_MID_L, SIM_SIDE_R, force1, 0u, 50u);
    install_spell(&world, SIM_SIDE_L, force3, 80u);
    idle_step(&world);
    EXPECT(world.field[0].kind == FIELD_NONE &&
           SPELL_DESC_TRAJECTORY(world.spell[0].descriptor) == TRAJ_MID);

    /* Slot order wins a tie, and one carrier reacts to no more than one field
     * per tick. On the next tick its resolved mask permits slot 1. */
    sim_init(&world, SIMF_AUTHORITATIVE, 0u);
    put_field(&world, 0u, FIELD_STEAM, SIM_RESIDUE_MID_L, SIM_SIDE_R, force1, 0u, 50u);
    put_field(&world, 1u, FIELD_WALL, SIM_RESIDUE_MID_L, SIM_SIDE_R, force3, 2u, 50u);
    install_spell(&world, SIM_SIDE_L,
                  carrier(SPELL_PROJECTILE, ELEM_EMBER, PAY_DAMAGE, TRAJ_MID, 2u, STATUS_NONE),
                  80u);
    idle_step(&world);
    EXPECT(world.spell[0].active && SPELL_DESC_MAGNITUDE(world.spell[0].descriptor) == 1u &&
           world.field[1].aux == 2u);
    idle_step(&world);
    EXPECT(!world.spell[0].active && world.field[1].aux == 1u);

    /* Travel progress orders simultaneous carriers before side identity. The
     * farther-travelled right carrier disperses the shared cloud first. */
    sim_init(&world, SIMF_AUTHORITATIVE, 0u);
    put_field(&world, 0u, FIELD_STEAM, SIM_RESIDUE_MID_L, SIM_SIDE_L, force1, 0u, 50u);
    install_spell(&world, SIM_SIDE_L, force1, 80u);
    install_spell(&world, SIM_SIDE_R, force1, 180u); /* u=75, progress wins */
    idle_step(&world);
    EXPECT(world.field[0].kind == FIELD_NONE &&
           (world.spell[SIM_SIDE_R].resolved & SPELL_RESOLVED_FIELD0) != 0u &&
           (world.spell[SIM_SIDE_L].resolved & SPELL_RESOLVED_FIELD0) == 0u);
    CHECK(ok, "fields_all_seven_collision_rules_slot_tiebreak_one_per_tick_and_travel_order");
}

static void test_field_creation_lifetime_fallback_echo_and_bloom(void) {
    bool ok = true;
    sim_world_t world;
    static const struct {
        uint32_t descriptor;
        uint8_t age;
        uint8_t kind;
        uint8_t flavor;
    } transfers[] = {
        {SPELL_DESC_PACK(SPELL_CONJURE, ELEM_FORCE, PAY_DAMAGE, TRAJ_GROUND, 2u, STATUS_NONE,
                         INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, 0u),
         9u, FIELD_TRAP, AFTER_FLAVOR_BASE},
        {SPELL_DESC_PACK(SPELL_SINGULARITY, ELEM_VOID, PAY_DAMAGE, TRAJ_MID, 2u, STATUS_NONE,
                         INTERACT_ABSORB, TEMPO_FLOWING, TREND_STEADY, 0u),
         15u, FIELD_SINGULARITY, AFTER_FLAVOR_BASE},
        {SPELL_DESC_PACK(SPELL_CONJURE, ELEM_FORCE, PAY_STATUS, TRAJ_GROUND, 2u, STATUS_MARKED,
                         INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, 0u),
         9u, FIELD_RUNE, AFTER_FLAVOR_RUNE},
        {SPELL_DESC_PACK(SPELL_CONJURE, ELEM_FORCE, PAY_DAMAGE, TRAJ_RETURNING, 2u, STATUS_NONE,
                         INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, 0u),
         9u, FIELD_FAMILIAR, AFTER_FLAVOR_FAMILIAR},
        {SPELL_DESC_PACK(SPELL_GROUND_WAVE, ELEM_FROST, PAY_STATUS, TRAJ_GROUND, 3u, STATUS_FROZEN,
                         INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, 0u),
         9u, FIELD_WALL, AFTER_FLAVOR_WALL},
        {SPELL_DESC_PACK(SPELL_SINGULARITY, ELEM_VOID, PAY_DAMAGE, TRAJ_HOMING, 2u, STATUS_NONE,
                         INTERACT_ABSORB, TEMPO_FLOWING, TREND_STEADY, 0u),
         15u, FIELD_VORTEX, AFTER_FLAVOR_VORTEX},
    };
    for (size_t i = 0; i < sizeof transfers / sizeof transfers[0]; i++) {
        sim_init(&world, SIMF_AUTHORITATIVE, 0u);
        install_spell(&world, SIM_SIDE_L, transfers[i].descriptor, 0u);
        world.spell[0].age = transfers[i].age;
        idle_step(&world);
        EXPECT(!world.spell[0].active && world.field[0].kind == transfers[i].kind &&
               world.field[0].owner == SIM_SIDE_L &&
               world.field[0].descriptor == transfers[i].descriptor &&
               world.aftermath_flavor == transfers[i].flavor);
    }

    /* A familiar advances through the canonical owner-to-opponent zones and
     * delivers one point at the far doorstep before leaving. */
    sim_init(&world, SIMF_AUTHORITATIVE, 0u);
    uint32_t familiar = transfers[3].descriptor;
    put_field(&world, 0u, FIELD_FAMILIAR, SIM_RESIDUE_DOORSTEP_L, SIM_SIDE_L, familiar, 0u,
              SIM_FIELD_FAMILIAR_TICKS);
    wait_ticks(&world, 20u);
    EXPECT(world.field[0].kind == FIELD_FAMILIAR && world.field[0].zone == SIM_RESIDUE_MID_L);
    wait_ticks(&world, 20u);
    EXPECT(world.field[0].kind == FIELD_FAMILIAR && world.field[0].zone == SIM_RESIDUE_MID_R);
    wait_ticks(&world, 20u);
    EXPECT(world.field[0].kind == FIELD_NONE && world.wiz[SIM_SIDE_R].hp == SIM_MAX_HP - 1u &&
           world.aftermath_flavor == AFTER_FLAVOR_FAMILIAR);

    /* Slot exhaustion never allocates or queues: a matured trap keeps the old
     * immediate fuse path when both authoritative slots are occupied. */
    sim_init(&world, SIMF_AUTHORITATIVE, 0u);
    put_field(&world, 0u, FIELD_RUNE, 0u, 0u, transfers[2].descriptor, 0u, 200u);
    put_field(&world, 1u, FIELD_WALL, 3u, 1u, transfers[4].descriptor, 3u, 200u);
    install_spell(&world, SIM_SIDE_L, transfers[0].descriptor, 0u);
    world.spell[0].age = 74u;
    idle_step(&world);
    EXPECT(!world.spell[0].active && world.field[0].kind == FIELD_RUNE &&
           world.field[1].kind == FIELD_WALL && world.wiz[SIM_SIDE_R].hp == SIM_MAX_HP - 2u);

    /* Steam is created directly by residue; the same full-slot case falls
     * back to the former bounded one-point pulse. */
    uint32_t ember = carrier(SPELL_PROJECTILE, ELEM_EMBER, PAY_DAMAGE, TRAJ_MID, 1u, STATUS_NONE);
    sim_init(&world, SIMF_AUTHORITATIVE, 0u);
    world.residue[SIM_RESIDUE_MID_L] = (sim_residue_t){ELEM_FROST, 2u, SIM_RESIDUE_DECAY_UNITS};
    install_spell(&world, SIM_SIDE_L, ember, 80u);
    idle_step(&world);
    EXPECT(world.field[0].kind == FIELD_STEAM && world.field[0].zone == SIM_RESIDUE_MID_L &&
           world.wiz[SIM_SIDE_R].hp == SIM_MAX_HP);
    sim_init(&world, SIMF_AUTHORITATIVE, 0u);
    put_field(&world, 0u, FIELD_RUNE, 0u, 0u, transfers[2].descriptor, 0u, 200u);
    put_field(&world, 1u, FIELD_WALL, 3u, 1u, transfers[4].descriptor, 3u, 200u);
    world.residue[SIM_RESIDUE_MID_L] = (sim_residue_t){ELEM_FROST, 2u, SIM_RESIDUE_DECAY_UNITS};
    install_spell(&world, SIM_SIDE_L, ember, 80u);
    idle_step(&world);
    EXPECT(world.wiz[SIM_SIDE_R].hp == SIM_MAX_HP - 1u && world.fx_kind == FX_DETONATE &&
           world.field[0].kind == FIELD_RUNE && world.field[1].kind == FIELD_WALL);

    /* Echo owns one pending descriptor per side and emits one reduced repeat
     * only after its original slot clears. */
    uint32_t echo =
        SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE, TRAJ_MID, 3u, STATUS_NONE,
                        INTERACT_COMBINE, TEMPO_RAPID, TREND_IRREGULAR, 0u);
    sim_init(&world, SIMF_AUTHORITATIVE, 0u);
    world.wiz[0].pending_desc = echo;
    world.wiz[0].inc_state = INC_WINDUP;
    world.wiz[0].cast_windup = 1u;
    idle_step(&world);
    uint32_t pending_echo = world.wiz[0].echo_desc;
    EXPECT(world.spell[0].active && SPELL_DESC_MAGNITUDE(pending_echo) == 2u &&
           world.wiz[0].echo_ticks == 25u);
    world.spell[0] = (sim_spell_t){0};
    /* A different element, so the second cast cannot merge (C7) either. */
    world.wiz[0].pending_desc =
        SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_EMBER, PAY_DAMAGE, TRAJ_MID, 4u, STATUS_NONE,
                        INTERACT_COMBINE, TEMPO_RAPID, TREND_IRREGULAR, 0u);
    world.wiz[0].inc_state = INC_WINDUP;
    world.wiz[0].cast_windup = 1u;
    idle_step(&world);
    EXPECT(world.wiz[0].echo_desc == pending_echo); /* second cast cannot queue another */
    world.spell[0] = (sim_spell_t){0};
    wait_ticks(&world, 24u);
    EXPECT(world.spell[0].active && world.wiz[0].echo_desc == 0u &&
           SPELL_DESC_MAGNITUDE(world.spell[0].descriptor) == 2u &&
           world.aftermath_flavor == AFTER_FLAVOR_ECHO);

    /* Bloom spends one matching residue step and emits a capped one-point
     * area pulse. */
    sim_init(&world, SIMF_AUTHORITATIVE, 0u);
    uint32_t bloom =
        SPELL_DESC_PACK(SPELL_FIREBALL, ELEM_FROST, PAY_HYBRID, TRAJ_AREA, 3u, STATUS_FROZEN,
                        INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, 0u);
    world.residue[SIM_RESIDUE_MID_L] = (sim_residue_t){ELEM_FROST, 2u, SIM_RESIDUE_DECAY_UNITS};
    install_spell(&world, SIM_SIDE_L, bloom, 80u);
    idle_step(&world);
    EXPECT(world.residue[SIM_RESIDUE_MID_L].intensity == 1u &&
           world.wiz[SIM_SIDE_R].hp == SIM_MAX_HP - 1u &&
           world.aftermath_flavor == AFTER_FLAVOR_BLOOM && world.fx_kind == FX_COMBINE);
    CHECK(ok, "fields_transfer_lifetime_slot_exhaustion_fallback_echo_bound_and_bloom_cap");
}

/* ---- combination rules (RS-D3 C1-C7) ------------------------------------ */

static void put_status(sim_wizard_t *wizard, uint8_t status, uint8_t intensity) {
    wizard->status = status;
    wizard->status_intensity = intensity;
    wizard->status_ticks = 100u;
}

static void test_combo_shatter(void) {
    bool ok = true;
    sim_world_t world;
    sim_init(&world, SIMF_AUTHORITATIVE, 0u);
    put_status(&world.wiz[SIM_SIDE_R], STATUS_FROZEN, 1u);
    land_spell(&world, SIM_SIDE_L,
               carrier(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE, TRAJ_MID, 2u, STATUS_NONE));
    EXPECT(world.wiz[SIM_SIDE_R].hp == SIM_MAX_HP - 3u &&
           world.wiz[SIM_SIDE_R].status == STATUS_NONE);

    /* The bonus never lifts a hit past the magnitude cap. */
    sim_init(&world, SIMF_AUTHORITATIVE, 0u);
    put_status(&world.wiz[SIM_SIDE_R], STATUS_FROZEN, 1u);
    land_spell(&world, SIM_SIDE_L,
               carrier(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE, TRAJ_MID, 4u, STATUS_NONE));
    EXPECT(world.wiz[SIM_SIDE_R].hp == SIM_MAX_HP - 4u);

    /* A pure status force spell deals no direct damage, so nothing shatters. */
    sim_init(&world, SIMF_AUTHORITATIVE, 0u);
    put_status(&world.wiz[SIM_SIDE_R], STATUS_FROZEN, 2u);
    land_spell(&world, SIM_SIDE_L,
               carrier(SPELL_PROJECTILE, ELEM_FORCE, PAY_STATUS, TRAJ_MID, 1u, STATUS_MARKED));
    EXPECT(world.wiz[SIM_SIDE_R].hp == SIM_MAX_HP && world.wiz[SIM_SIDE_R].status == STATUS_FROZEN);
    CHECK(ok, "combo_c1_force_hit_shatters_frozen_for_one_extra_capped_damage");
}

static void test_combo_thaw(void) {
    bool ok = true;
    sim_world_t world;
    sim_init(&world, SIMF_AUTHORITATIVE, 0u);
    put_status(&world.wiz[SIM_SIDE_R], STATUS_FROZEN, 3u);
    land_spell(&world, SIM_SIDE_L,
               carrier(SPELL_PROJECTILE, ELEM_EMBER, PAY_STATUS, TRAJ_MID, 1u, STATUS_BURNING));
    EXPECT(world.wiz[SIM_SIDE_R].status == STATUS_NONE &&
           world.wiz[SIM_SIDE_R].status_intensity == 0u && world.field[0].kind == FIELD_STEAM &&
           world.field[0].zone == SIM_RESIDUE_DOORSTEP_R);

    sim_init(&world, SIMF_AUTHORITATIVE, 0u);
    put_status(&world.wiz[SIM_SIDE_L], STATUS_BURNING, 1u);
    land_spell(&world, SIM_SIDE_R,
               carrier(SPELL_PROJECTILE, ELEM_FROST, PAY_HYBRID, TRAJ_MID, 2u, STATUS_FROZEN));
    EXPECT(world.wiz[SIM_SIDE_L].status == STATUS_NONE &&
           world.wiz[SIM_SIDE_L].hp == SIM_MAX_HP - 1u && world.field[0].kind == FIELD_STEAM &&
           world.field[0].zone == SIM_RESIDUE_DOORSTEP_L);

    /* Both slots full: the status still clears, but no third field appears. */
    sim_init(&world, SIMF_AUTHORITATIVE, 0u);
    uint32_t force1 = carrier(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE, TRAJ_MID, 1u, STATUS_NONE);
    put_field(&world, 0u, FIELD_RUNE, SIM_RESIDUE_DOORSTEP_L, SIM_SIDE_L, force1, 0u, 200u);
    put_field(&world, 1u, FIELD_RUNE, SIM_RESIDUE_DOORSTEP_L, SIM_SIDE_L, force1, 0u, 200u);
    put_status(&world.wiz[SIM_SIDE_R], STATUS_FROZEN, 1u);
    land_spell(&world, SIM_SIDE_L,
               carrier(SPELL_PROJECTILE, ELEM_EMBER, PAY_STATUS, TRAJ_MID, 1u, STATUS_BURNING));
    EXPECT(world.wiz[SIM_SIDE_R].status == STATUS_NONE && world.field[0].kind == FIELD_RUNE &&
           world.field[1].kind == FIELD_RUNE);
    CHECK(ok, "combo_c2_ember_thaws_frozen_frost_quenches_burning_into_doorstep_steam");
}

static void test_combo_deepen(void) {
    bool ok = true;
    sim_world_t world;
    sim_init(&world, SIMF_AUTHORITATIVE, 0u);
    uint32_t mark = carrier(SPELL_PROJECTILE, ELEM_VOID, PAY_STATUS, TRAJ_MID, 1u, STATUS_MARKED);
    land_spell(&world, SIM_SIDE_L, mark);
    EXPECT(world.wiz[SIM_SIDE_R].status_intensity == 1u);
    land_spell(&world, SIM_SIDE_L, mark);
    EXPECT(world.wiz[SIM_SIDE_R].status_intensity == 2u &&
           world.wiz[SIM_SIDE_R].status_ticks > 100u);
    land_spell(&world, SIM_SIDE_L, mark);
    land_spell(&world, SIM_SIDE_L, mark);
    EXPECT(world.wiz[SIM_SIDE_R].status == STATUS_MARKED &&
           world.wiz[SIM_SIDE_R].status_intensity == 3u &&
           world.wiz[SIM_SIDE_R].status_ticks <= 150u);

    /* A weaker different status is still ignored. */
    land_spell(&world, SIM_SIDE_L,
               carrier(SPELL_PROJECTILE, ELEM_VOID, PAY_STATUS, TRAJ_MID, 1u, STATUS_DISRUPTED));
    EXPECT(world.wiz[SIM_SIDE_R].status == STATUS_MARKED &&
           world.wiz[SIM_SIDE_R].status_intensity == 3u);
    CHECK(ok, "combo_c3_same_status_deepens_one_step_capped_at_three");
}

static void test_combo_singularity_swallows(void) {
    bool ok = true;
    sim_world_t world;
    uint32_t sing = carrier(SPELL_SINGULARITY, ELEM_VOID, PAY_DAMAGE, TRAJ_MID, 1u, STATUS_NONE);
    uint32_t force2 = carrier(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE, TRAJ_MID, 2u, STATUS_NONE);
    sim_init(&world, SIMF_AUTHORITATIVE, 0u);
    put_field(&world, 0u, FIELD_RUNE, SIM_RESIDUE_MID_L, SIM_SIDE_R, force2, 0u, 50u);
    put_field(&world, 1u, FIELD_SINGULARITY, SIM_RESIDUE_MID_L, SIM_SIDE_L, sing, 1u, 50u);
    idle_step(&world);
    EXPECT(world.field[0].kind == FIELD_NONE && world.field[1].kind == FIELD_SINGULARITY &&
           world.field[1].aux == 2u && world.fx_kind == FX_RESIDUE);

    /* The charge caps at 4, and fields in different zones are left alone. */
    sim_init(&world, SIMF_AUTHORITATIVE, 0u);
    put_field(&world, 0u, FIELD_SINGULARITY, SIM_RESIDUE_MID_R, SIM_SIDE_R, sing, 4u, 50u);
    put_field(&world, 1u, FIELD_WALL, SIM_RESIDUE_MID_R, SIM_SIDE_R, force2, 2u, 50u);
    idle_step(&world);
    EXPECT(world.field[0].aux == 4u && world.field[1].kind == FIELD_NONE);
    sim_init(&world, SIMF_AUTHORITATIVE, 0u);
    put_field(&world, 0u, FIELD_SINGULARITY, SIM_RESIDUE_MID_R, SIM_SIDE_R, sing, 1u, 50u);
    put_field(&world, 1u, FIELD_WALL, SIM_RESIDUE_MID_L, SIM_SIDE_L, force2, 2u, 50u);
    idle_step(&world);
    EXPECT(world.field[0].aux == 1u && world.field[1].kind == FIELD_WALL);
    CHECK(ok, "combo_c4_singularity_swallows_a_same_zone_field_for_capped_charge");
}

static void test_combo_opposed_fields_grind(void) {
    bool ok = true;
    sim_world_t world;
    uint32_t wall3 =
        carrier(SPELL_GROUND_WAVE, ELEM_FROST, PAY_STATUS, TRAJ_GROUND, 3u, STATUS_FROZEN);
    uint32_t trap2 = carrier(SPELL_CONJURE, ELEM_FORCE, PAY_DAMAGE, TRAJ_GROUND, 2u, STATUS_NONE);
    sim_init(&world, SIMF_AUTHORITATIVE, 0u);
    put_field(&world, 0u, FIELD_WALL, SIM_RESIDUE_MID_R, SIM_SIDE_L, wall3, 3u, 50u);
    put_field(&world, 1u, FIELD_TRAP, SIM_RESIDUE_MID_R, SIM_SIDE_R, trap2, 2u, 50u);
    idle_step(&world);
    EXPECT(world.field[0].kind == FIELD_WALL && world.field[0].aux == 3u &&
           world.field[1].kind == FIELD_NONE && world.aftermath[SIM_SIDE_L].kind == AFTER_INSPECT &&
           world.aftermath[SIM_SIDE_R].kind == AFTER_INSPECT);

    /* A tie clears both; a shared owner leaves both standing. */
    sim_init(&world, SIMF_AUTHORITATIVE, 0u);
    put_field(&world, 0u, FIELD_WALL, SIM_RESIDUE_MID_R, SIM_SIDE_L,
              desc_set_magnitude_for_test(wall3, 2u), 2u, 50u);
    put_field(&world, 1u, FIELD_TRAP, SIM_RESIDUE_MID_R, SIM_SIDE_R, trap2, 2u, 50u);
    idle_step(&world);
    EXPECT(world.field[0].kind == FIELD_NONE && world.field[1].kind == FIELD_NONE);
    sim_init(&world, SIMF_AUTHORITATIVE, 0u);
    put_field(&world, 0u, FIELD_WALL, SIM_RESIDUE_MID_R, SIM_SIDE_R, wall3, 3u, 50u);
    put_field(&world, 1u, FIELD_TRAP, SIM_RESIDUE_MID_R, SIM_SIDE_R, trap2, 2u, 50u);
    idle_step(&world);
    EXPECT(world.field[0].kind == FIELD_WALL && world.field[1].kind == FIELD_TRAP);
    CHECK(ok, "combo_c5_opposed_same_zone_fields_grind_lower_magnitude_clears");
}

static void test_combo_follow_through(void) {
    bool ok = true;
    sim_world_t world;
    uint32_t ember2 = carrier(SPELL_PROJECTILE, ELEM_EMBER, PAY_DAMAGE, TRAJ_MID, 2u, STATUS_NONE);
    static const uint8_t elements[] = {ELEM_EMBER, ELEM_FROST};
    for (size_t i = 0; i < sizeof elements; i++) {
        sim_init(&world, SIMF_AUTHORITATIVE, 0u);
        world.wiz[SIM_SIDE_L].prepared_desc =
            carrier(SPELL_PROJECTILE, elements[i], PAY_DAMAGE, TRAJ_MID, 2u, STATUS_NONE);
        world.wiz[SIM_SIDE_L].prepared = 1u;
        world.wiz[SIM_SIDE_L].inc_state = INC_PREPARED;
        land_spell(&world, SIM_SIDE_L, ember2);
        EXPECT(world.wiz[SIM_SIDE_R].hp == SIM_MAX_HP - 2u && world.spell[SIM_SIDE_L].active &&
               SPELL_DESC_MAGNITUDE(world.spell[SIM_SIDE_L].descriptor) == (i ? 2u : 3u));
    }
    CHECK(ok, "combo_c6_landed_hit_feeds_a_same_element_prepared_spell_one_magnitude");
}

static void test_combo_echo_merge(void) {
    bool ok = true;
    sim_world_t world;
    uint32_t echo = carrier(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE, TRAJ_MID, 2u, STATUS_NONE);
    static const uint8_t elements[] = {ELEM_FORCE, ELEM_EMBER};
    for (size_t i = 0; i < sizeof elements; i++) {
        sim_init(&world, SIMF_AUTHORITATIVE, 0u);
        world.wiz[SIM_SIDE_L].echo_desc = echo;
        world.wiz[SIM_SIDE_L].echo_ticks = 10u;
        world.wiz[SIM_SIDE_L].pending_desc =
            carrier(SPELL_PROJECTILE, elements[i], PAY_DAMAGE, TRAJ_MID, 2u, STATUS_NONE);
        world.wiz[SIM_SIDE_L].inc_state = INC_WINDUP;
        world.wiz[SIM_SIDE_L].cast_windup = 1u;
        idle_step(&world);
        if (!i)
            EXPECT(world.spell[SIM_SIDE_L].active &&
                   SPELL_DESC_MAGNITUDE(world.spell[SIM_SIDE_L].descriptor) == 3u &&
                   world.wiz[SIM_SIDE_L].echo_desc == 0u &&
                   world.aftermath_flavor == AFTER_FLAVOR_ECHO);
        else
            EXPECT(SPELL_DESC_MAGNITUDE(world.spell[SIM_SIDE_L].descriptor) == 2u &&
                   world.wiz[SIM_SIDE_L].echo_desc == echo);
    }
    CHECK(ok, "combo_c7_release_folds_a_same_element_pending_echo_into_one_bigger_spell");
}

void run_fields_magic_tests(void) {
    test_signature_predicates_and_physical_fixtures();
    test_field_collision_table_and_order();
    test_field_creation_lifetime_fallback_echo_and_bloom();
    test_combo_shatter();
    test_combo_thaw();
    test_combo_deepen();
    test_combo_singularity_swallows();
    test_combo_opposed_fields_grind();
    test_combo_follow_through();
    test_combo_echo_merge();
}
