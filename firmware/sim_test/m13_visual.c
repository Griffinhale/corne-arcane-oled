#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "duel_draw.h"
#include "duel_host.h"
#include "duel_sim.h"

typedef struct { char name[48]; uint64_t hash; } visual_case_t;
static visual_case_t cases[160];
static size_t ncases;

static uint64_t fnv1a(uint64_t hash, const void *data, size_t size) {
    const uint8_t *bytes = data;
    while (size--) { hash ^= *bytes++; hash *= UINT64_C(0x100000001b3); }
    return hash;
}

static void add_case_civic(const char *name, sim_world_t *world, uint32_t frame,
                           uint8_t flash_kind, uint8_t civic,
                           uint8_t transition) {
    duel_render_t render = {0};
    duel_render_from_world(&render, world);
    render.seed = 0x5au;
    render.civic_phase = 19u;
    render.flash_kind = flash_kind;
    render.flash_frames = flash_kind ? 8u : 0u;
    render.civic = civic;
    render.floor_transition = transition;
    duel_fb_t left, right;
    duel_fb_clear(&left); duel_fb_clear(&right);
    wiz_draw_scene(&left, &render, true, frame, false);
    wiz_draw_scene(&right, &render, false, frame, false);
    uint64_t hash = fnv1a(UINT64_C(0xcbf29ce484222325), left.bits, sizeof left.bits);
    hash = fnv1a(hash, right.bits, sizeof right.bits);
    snprintf(cases[ncases].name, sizeof cases[ncases].name, "%s", name);
    cases[ncases++].hash = hash;
}

static void add_case(const char *name, sim_world_t *world, uint32_t frame,
                     uint8_t flash_kind) {
    add_case_civic(name, world, frame, flash_kind,
                   DUEL_CIVIC_PACK(DUEL_M12_FLOOR_COMMONS,
                                   DUEL_M12_MODE_NORMAL, 0), 0u);
}

static void add_floor_city_case(const char *name, sim_world_t *world,
                                uint8_t floor, bool is_left) {
    duel_render_t render = {0};
    duel_render_from_world(&render, world);
    render.seed = 0x5au; render.civic_phase = 19u;
    render.civic = DUEL_CIVIC_PACK(floor, DUEL_M12_MODE_NORMAL, 0);
    duel_fb_t left, right;
    duel_fb_clear(&left); duel_fb_clear(&right);
    wiz_draw_scene(is_left ? &left : &right, &render, is_left, 7u, false);
    uint64_t hash = fnv1a(UINT64_C(0xcbf29ce484222325), left.bits, sizeof left.bits);
    hash = fnv1a(hash, right.bits, sizeof right.bits);
    snprintf(cases[ncases].name, sizeof cases[ncases].name, "%s", name);
    cases[ncases++].hash = hash;
}

static uint32_t descriptor(uint8_t form, uint8_t magnitude) {
    static const uint8_t element[8] = { ELEM_FORCE, ELEM_VOID, ELEM_EMBER, ELEM_FROST,
                                        ELEM_FORCE, ELEM_FORCE, ELEM_FROST, ELEM_VOID };
    uint8_t trajectory = form == SPELL_FIREBALL ? TRAJ_ROOF :
                         form == SPELL_SINGULARITY ? TRAJ_AREA : TRAJ_MID;
    if (form == SPELL_GROUND_WAVE) trajectory = TRAJ_GROUND;
    if (form == SPELL_CHAIN) trajectory = TRAJ_HOMING;
    if (form == SPELL_CONJURE) trajectory = TRAJ_RETURNING;
    uint8_t interaction = form == SPELL_SINGULARITY ? INTERACT_ABSORB : INTERACT_SOLID;
    return SPELL_DESC_PACK(form, element[form], PAY_DAMAGE, trajectory, magnitude,
                           STATUS_NONE, interaction, TEMPO_FLOWING, TREND_STEADY,
                           magnitude - 1u);
}

static void build_catalog(void) {
    sim_world_t world;
    sim_init(&world, SIMF_AUTHORITATIVE, 0);
    add_case("idle_12hp", &world, 0, 0);

    static const char *floor_name[3] = {"commons", "research", "workshop"};
    for (uint8_t floor = 0; floor < 3u; floor++) {
        char name[48];
        snprintf(name, sizeof name, "floor_astral_%s", floor_name[floor]);
        add_floor_city_case(name, &world, floor, true);
        snprintf(name, sizeof name, "floor_mechanical_%s", floor_name[floor]);
        add_floor_city_case(name, &world, floor, false);
    }
    for (uint8_t phase = 0; phase < 4u; phase++) {
        char name[48]; snprintf(name, sizeof name, "floor_transition_phase_%u", phase);
        add_case_civic(name, &world, 7u, 0,
                       DUEL_CIVIC_PACK(DUEL_M12_FLOOR_WORKSHOP,
                                       DUEL_M12_MODE_NORMAL, 0),
                       M13_FLOOR_TRANSITION_PACK(DUEL_M12_FLOOR_COMMONS,
                                                 phase, true));
    }

    for (uint8_t tier = 1; tier <= 4; tier++) {
        char name[48]; snprintf(name, sizeof name, "ward_tier_%u", tier);
        sim_init(&world, SIMF_AUTHORITATIVE, 0);
        world.wiz[0].ward_strength = tier; world.wiz[0].ward_focus = tier - 1u;
        add_case(name, &world, tier, 0);
    }
    for (uint8_t status = STATUS_BURNING; status <= STATUS_MARKED; status++) {
        static const char *names[] = { "", "burning", "frozen", "disrupted", "marked" };
        sim_init(&world, SIMF_AUTHORITATIVE, 0);
        world.wiz[0].status = status; world.wiz[0].status_intensity = 3; world.wiz[0].status_ticks = 125;
        add_case(names[status], &world, status, 0);
    }

    sim_init(&world, SIMF_AUTHORITATIVE, 0);
    world.wiz[0].inc_state = INC_COLLECTING; world.wiz[0].ward_strength = 1;
    world.wiz[0].inc.key_count = 3; world.wiz[0].inc.seen_pos = 7;
    add_case("collecting_short", &world, 3, 0);
    world.wiz[0].inc.key_count = 64; world.wiz[0].inc.seen_pos = 0xffffu;
    world.wiz[0].inc.turns = 16; world.wiz[0].inc.layer_transitions = 8;
    world.wiz[0].inc.overlap_peak = 5; world.wiz[0].inc.rhythm_changes = 8;
    world.wiz[0].ward_strength = 4;
    add_case("collecting_saturated", &world, 9, 0);

    sim_init(&world, SIMF_AUTHORITATIVE, 0);
    world.wiz[0].inc_state = INC_WINDUP; world.wiz[0].cast_windup = 6;
    world.wiz[0].windup_total = 12; world.wiz[0].ward_strength = 3;
    world.wiz[0].pending_desc = descriptor(SPELL_BEAM, 3);
    add_case("windup_beam", &world, 5, 0);
    world.wiz[0].inc_state = INC_PREPARED; world.wiz[0].prepared = 1;
    world.wiz[0].prepared_desc = world.wiz[0].pending_desc; world.wiz[0].pending_desc = 0;
    add_case("prepared_beam", &world, 6, 0);

    for (uint8_t side = 0; side < 2u; side++) {
        for (uint8_t variant = 0; variant < 4; variant++) {
            for (uint8_t form = 0; form <= SPELL_CONJURE; form++) {
                char name[48];
                snprintf(name, sizeof name, "side_%u_voice_%u_form_%u", side, variant, form);
                sim_init(&world, SIMF_AUTHORITATIVE, 0);
                world.wiz[side].variant = variant;
                world.spell[side].active = 1;
                world.spell[side].descriptor = descriptor(form, (uint8_t)(variant + 1u));
                world.spell[side].kind = DUEL_KIND_WITH_TIER(
                    DUEL_KIND_PACK(SPELL_DESC_ELEMENT(world.spell[side].descriptor), MOD_NONE, PAY_IMPACT), variant);
                world.spell[side].progress = form == SPELL_BEAM ? 128u :
                                             form == SPELL_SINGULARITY ? 144u :
                                             form == SPELL_SWARM ? (uint8_t)(((3u + variant) << 5) | 14u) :
                                             form == SPELL_CHAIN ? 160u :
                                             form == SPELL_CONJURE ? (uint8_t)(((2u + variant) << 5) | 14u) : 72u;
                add_case(name, &world, (uint32_t)(side * 37u + variant * 7u + form), 0);
            }
        }
    }

    struct temporal_case { const char *name; uint8_t form, progress, trajectory, tempo, trend; } temporal[] = {
        {"beam_small", SPELL_BEAM, 32, TRAJ_MID, TEMPO_DELIBERATE, TREND_STEADY},
        {"beam_full", SPELL_BEAM, 128, TRAJ_MID, TEMPO_RAPID, TREND_ACCELERATING},
        {"beam_fizzle", SPELL_BEAM, 236, TRAJ_MID, TEMPO_FRANTIC, TREND_DECELERATING},
        {"fireball_ascent", SPELL_FIREBALL, 72, TRAJ_ROOF, TEMPO_FLOWING, TREND_STEADY},
        {"fireball_descent", SPELL_FIREBALL, 216, TRAJ_ROOF, TEMPO_RAPID, TREND_ACCELERATING},
        {"swarm_gather", SPELL_SWARM, (uint8_t)((6u << 5) | 5u), TRAJ_MID, TEMPO_FRANTIC, TREND_ACCELERATING},
        {"swarm_launch", SPELL_SWARM, (uint8_t)((5u << 5) | 14u), TRAJ_MID, TEMPO_FRANTIC, TREND_ACCELERATING},
        {"return_out", SPELL_PROJECTILE, 64, TRAJ_RETURNING, TEMPO_FLOWING, TREND_STEADY},
        {"return_in", SPELL_PROJECTILE, 192, TRAJ_RETURNING, TEMPO_FLOWING, TREND_STEADY},
        {"homing_early", SPELL_PROJECTILE, 64, TRAJ_HOMING, TEMPO_RAPID, TREND_ACCELERATING},
        {"homing_late", SPELL_PROJECTILE, 192, TRAJ_HOMING, TEMPO_RAPID, TREND_ACCELERATING},
        {"ground_wave", SPELL_GROUND_WAVE, 88, TRAJ_GROUND, TEMPO_FLOWING, TREND_STEADY},
        {"chain_arc", SPELL_CHAIN, 176, TRAJ_HOMING, TEMPO_RAPID, TREND_IRREGULAR},
        {"trap_set", SPELL_CONJURE, (uint8_t)((3u << 5) | 16u), TRAJ_GROUND, TEMPO_DELIBERATE, TREND_STEADY},
    };
    for (size_t i = 0; i < sizeof temporal / sizeof temporal[0]; i++) {
        sim_init(&world, SIMF_AUTHORITATIVE, 0);
        uint32_t desc = SPELL_DESC_PACK(temporal[i].form, ELEM_FORCE,
                                        !strncmp(temporal[i].name, "return", 6) ? PAY_HEAL : PAY_DAMAGE,
                                        temporal[i].trajectory, 3, STATUS_NONE, INTERACT_SOLID,
                                        temporal[i].tempo, temporal[i].trend, 2);
        world.spell[0].active = 1; world.spell[0].descriptor = desc;
        world.spell[0].kind = DUEL_KIND_WITH_TIER(DUEL_KIND_PACK(ELEM_FORCE, MOD_NONE, PAY_IMPACT), 2);
        world.spell[0].progress = temporal[i].progress;
        add_case(temporal[i].name, &world, (uint32_t)i * 3u, 0);
    }

    struct gap_family { const char *name; uint8_t form, element, trajectory, interaction; } gap[] = {
        {"ordinary", SPELL_PROJECTILE, ELEM_FORCE, TRAJ_MID, INTERACT_SOLID},
        {"edge_trail", SPELL_PROJECTILE, ELEM_FROST, TRAJ_HOMING, INTERACT_SOLID},
        {"portal", SPELL_PROJECTILE, ELEM_VOID, TRAJ_MID, INTERACT_PHASE},
        {"beam", SPELL_BEAM, ELEM_EMBER, TRAJ_MID, INTERACT_SOLID},
        {"chain", SPELL_CHAIN, ELEM_FORCE, TRAJ_HOMING, INTERACT_SOLID},
    };
    static const char *stage_name[3] = {"departure", "midpoint", "arrival"};
    static const uint8_t stage_progress[3] = {105u, 130u, 155u};
    for (size_t family = 0; family < sizeof gap / sizeof gap[0]; family++)
        for (uint8_t side = 0; side < 2u; side++)
            for (uint8_t stage = 0; stage < 3u; stage++) {
                char name[48];
                snprintf(name, sizeof name, "gap_%s_%c_%s", gap[family].name,
                         side ? 'r' : 'l', stage_name[stage]);
                sim_init(&world, SIMF_AUTHORITATIVE, 0);
                world.spell[side].active = 1u;
                world.spell[side].descriptor = SPELL_DESC_PACK(
                    gap[family].form, gap[family].element, PAY_DAMAGE,
                    gap[family].trajectory, 2, STATUS_NONE,
                    gap[family].interaction, TEMPO_RAPID, TREND_STEADY, 1);
                world.spell[side].kind = DUEL_KIND_WITH_TIER(
                    DUEL_KIND_PACK(gap[family].element, MOD_NONE, PAY_IMPACT), 1u);
                world.spell[side].progress = stage_progress[stage];
                add_case(name, &world, 9u, 0);
            }

    static const uint8_t aftermath_kind[] = { AFTER_CHEER, AFTER_COMPLAINT, AFTER_PANIC,
                                               AFTER_FIRE, AFTER_INSPECT, AFTER_REPAIR,
                                               AFTER_MAX_CAST };
    static const char *aftermath_name[] = { "after_cheer", "after_complaint", "after_panic",
                                            "after_fire", "after_inspect", "after_repair",
                                            "after_max" };
    for (size_t i = 0; i < sizeof aftermath_kind; i++) {
        sim_init(&world, SIMF_AUTHORITATIVE, 0);
        world.aftermath[0].kind = aftermath_kind[i];
        world.aftermath[0].ticks = aftermath_kind[i] == AFTER_FIRE ? 130u : 75u;
        world.aftermath[0].intensity = 3u;
        world.world_state = aftermath_kind[i] == AFTER_FIRE ? WORLD_CRISIS : WORLD_RECOVERY;
        add_case(aftermath_name[i], &world, (uint32_t)i * 5u, 0);
    }

    static const char *reaction_name[] = { "heal", "complaint", "roof_panic",
                                           "void_inspect", "combine_repair", "void_collapse" };
    static const uint8_t reaction_kind[] = { 7, 9, 10, 11, 12, 13 };
    for (size_t i = 0; i < sizeof reaction_kind; i++) {
        sim_init(&world, SIMF_AUTHORITATIVE, 0);
        add_case(reaction_name[i], &world, (uint32_t)i, reaction_kind[i]);
    }
}

static int write_golden(const char *path) {
    FILE *file = fopen(path, "w");
    if (!file) return 1;
    for (size_t i = 0; i < ncases; i++)
        fprintf(file, "%s %016" PRIx64 "\n", cases[i].name, cases[i].hash);
    return fclose(file) != 0;
}

static int verify_golden(const char *path) {
    FILE *file = fopen(path, "r");
    if (!file) { perror(path); return 1; }
    bool ok = true;
    for (size_t i = 0; i < ncases; i++) {
        char name[48]; uint64_t hash;
        if (fscanf(file, "%47s %" SCNx64, name, &hash) != 2 ||
            strcmp(name, cases[i].name) || hash != cases[i].hash) ok = false;
    }
    char trailing[2];
    if (fscanf(file, "%1s", trailing) == 1) ok = false;
    fclose(file);
    printf("%s visual_m13_exact_framebuffer_hashes (%zu scenes)\n", ok ? "PASS" : "FAIL", ncases);
    return ok ? 0 : 1;
}

int main(int argc, char **argv) {
    build_catalog();
    bool unique = true;
    for (size_t i = 0; i < ncases; i++)
        for (size_t j = i + 1; j < ncases; j++) {
            if (cases[i].hash == cases[j].hash) {
                fprintf(stderr, "duplicate visual: %s / %s\n", cases[i].name, cases[j].name);
                unique = false;
            }
        }
    printf("%s visual_m13_catalog_unique\n", unique ? "PASS" : "FAIL");
    if (argc == 3 && !strcmp(argv[1], "--write-golden"))
        return unique ? write_golden(argv[2]) : 1;
    if (argc != 2) { fprintf(stderr, "usage: %s [--write-golden] PATH\n", argv[0]); return 2; }
    return unique ? verify_golden(argv[1]) : 1;
}
