#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "duel_draw.h"
#include "duel_courier.h"
#include "duel_host.h"
#include "duel_resident.h"
#include "duel_runtime.h"
#include "duel_sim.h"
#include "scenarios.h"

typedef struct { char name[48]; uint64_t hash; } visual_case_t;
static visual_case_t cases[384];
static size_t ncases;

static uint64_t fnv1a(uint64_t hash, const void *data, size_t size) {
    const uint8_t *bytes = data;
    while (size--) { hash ^= *bytes++; hash *= UINT64_C(0x100000001b3); }
    return hash;
}

/* Every case funnels through here: one hashing/registration idiom, one
 * capacity guard. */
static void record_case(const char *name, const duel_fb_t *left,
                        const duel_fb_t *right) {
    if (ncases >= sizeof cases / sizeof cases[0]) {
        fprintf(stderr, "visual catalog overflow at %s (%zu cases)\n", name, ncases);
        abort();
    }
    uint64_t hash = fnv1a(UINT64_C(0xcbf29ce484222325), left->bits, sizeof left->bits);
    hash = fnv1a(hash, right->bits, sizeof right->bits);
    snprintf(cases[ncases].name, sizeof cases[ncases].name, "%s", name);
    cases[ncases++].hash = hash;
}

static void record_render(const char *name, const duel_render_t *render,
                          uint32_t frame, bool hud) {
    duel_fb_t left, right;
    duel_fb_clear(&left); duel_fb_clear(&right);
    wiz_draw_scene(&left, render, true, frame, hud);
    wiz_draw_scene(&right, render, false, frame, hud);
    record_case(name, &left, &right);
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
    record_render(name, &render, frame, false);
}

static void add_case(const char *name, sim_world_t *world, uint32_t frame,
                     uint8_t flash_kind) {
    add_case_civic(name, world, frame, flash_kind,
                   DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_COMMONS,
                                   DUEL_CIVIC_MODE_NORMAL, 0), 0u);
}

static void add_occupation_case(const char *name, sim_world_t *world,
                                uint8_t floor, uint8_t action, bool is_left) {
    duel_render_t render = {0};
    duel_render_from_world(&render, world);
    bool found = false;
    for (uint16_t seed = 0; seed < 256u && !found; seed++) {
        for (uint8_t slot = 0; slot < 16u; slot++) {
            uint8_t phase = (uint8_t)(slot * DUEL_CIVIC_ACTION_SLOT + 7u);
            civic_resident_t resident = civic_resident_derive((uint8_t)seed, is_left,
                floor, DUEL_CIVIC_MODE_NORMAL, phase);
            if (resident.action == action) {
                render.seed = (uint8_t)seed;
                render.civic_phase = phase;
                found = true;
                break;
            }
        }
    }
    if (!found) abort();
    render.civic = DUEL_CIVIC_PACK(floor, DUEL_CIVIC_MODE_NORMAL, 0);
    /* Only the requested half is drawn; the other framebuffer stays blank. */
    duel_fb_t left, right;
    duel_fb_clear(&left); duel_fb_clear(&right);
    wiz_draw_scene(is_left ? &left : &right, &render, is_left, 7u, false);
    record_case(name, &left, &right);
}

static void add_render_case(const char *name, const duel_render_t *render,
                            uint32_t frame) {
    record_render(name, render, frame, false);
}

static void add_render_case_diagnostics(const char *name, const duel_render_t *render,
                                        uint32_t frame) {
    record_render(name, render, frame, true);
}

static void add_bilateral_attunement_case(const char *name, sim_world_t *world) {
    duel_render_t left_render = {0}, right_render = {0};
    duel_render_from_world(&left_render, world);
    right_render = left_render;
    left_render.seed = right_render.seed = 0x5au;
    left_render.civic_phase = right_render.civic_phase = 23u;
    left_render.civic = right_render.civic = DUEL_CIVIC_PACK(
        DUEL_CIVIC_FLOOR_WORKSHOP, DUEL_CIVIC_MODE_NORMAL, 0);
    left_render.layer = DUEL_RENDER_LAYER_PACK(3, DUEL_RENDER_LOCAL_LEFT);
    right_render.layer = DUEL_RENDER_LAYER_PACK(3, DUEL_RENDER_LOCAL_RIGHT);
    duel_fb_t left, right;
    duel_fb_clear(&left); duel_fb_clear(&right);
    wiz_draw_scene(&left, &left_render, true, 7u, false);
    wiz_draw_scene(&right, &right_render, false, 7u, false);
    record_case(name, &left, &right);
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
    /* The default secondary byte is dawn, so this is also the explicit
     * Commons/dawn review frame. */
    add_case("sky_commons_dawn_idle_12hp", &world, 0, 0);

    static const char *floor_name[INCANTATION_OCCUPATION_FLOORS] = {
        "commons", "research", "workshop", "observatory"
    };
    static const char *action_name[DUEL_CIVIC_ACTION_COUNT] = {
        "work", "walk", "inspect", "rest", "watch", "delivery", "react"
    };
    for (uint8_t floor = 0; floor < 3u; floor++) {
        for (uint8_t action = 0; action < DUEL_CIVIC_ACTION_COUNT; action++) {
            char name[48];
            snprintf(name, sizeof name, "occupation_astral_%s_%s",
                     floor_name[floor], action_name[action]);
            add_occupation_case(name, &world, floor, action, true);
            snprintf(name, sizeof name, "occupation_mech_%s_%s",
                     floor_name[floor], action_name[action]);
            add_occupation_case(name, &world, floor, action, false);
        }
    }

    static const char *courier_name[] = {"none", "messenger", "parcel", "beacon", "sentinel"};
    static const char *event_name[] = {"none", "scroll", "jam", "break", "complaint", "diplomat", "sky"};
    /* Disposable couriers and rare events exist only on ordinary floors. */
    for (uint8_t floor = 0; floor < DUEL_CIVIC_FLOOR_SPECIAL; floor++) {
        for (uint8_t kind = DUEL_CIVIC_COURIER_MESSENGER;
             kind < DUEL_CIVIC_COURIER_COUNT; kind++) {
            duel_render_t civic = {0}; duel_render_from_world(&civic, &world);
            civic.seed = 0x5au; civic.civic_phase = 19u;
            civic.civic = DUEL_CIVIC_PACK(floor, DUEL_CIVIC_MODE_NORMAL, 0);
            civic.shared_pres = (uint8_t)(DUEL_VISITOR_PACK(kind, 0,
                DUEL_CIVIC_VISIT_WAITING) | DUEL_VISITOR_DENSITY_PACK(DUEL_CIVIC_DENSITY_SINGLE));
            char name[48]; snprintf(name, sizeof name, "courier_%s_%s",
                                    floor_name[floor], courier_name[kind]);
            add_render_case(name, &civic, 7u);
        }
        for (uint8_t id = DUEL_CIVIC_EVENT_RUNAWAY_SCROLL;
             id < DUEL_CIVIC_EVENT_COUNT; id++) {
            duel_render_t civic = {0}; duel_render_from_world(&civic, &world);
            civic.seed = 0x5au; civic.civic_phase = 19u;
            civic.civic = DUEL_CIVIC_PACK(floor, DUEL_CIVIC_MODE_NORMAL, 0);
            uint8_t target = id >= DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER ?
                DUEL_CIVIC_EVENT_TARGET_SHARED : DUEL_CIVIC_EVENT_TARGET_LEFT;
            civic.revision = DUEL_EVENT_PACK(id, DUEL_CIVIC_EVENT_PHASE_ACTIVE, target);
            char name[48];
            if (floor == DUEL_CIVIC_FLOOR_COMMONS &&
                id == DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER)
                snprintf(name, sizeof name, "diplomacy_balance");
            else
                snprintf(name, sizeof name, "event_%s_%s",
                         floor_name[floor], event_name[id]);
            add_render_case(name, &civic, 7u);
        }
    }

    /* Deliberately reviewed M14 surface: every floor under every sky phase.
     * Observatory is always quiet, matching host semantic resolution. */
    static const char *sky_name[] = {"dawn", "day", "dusk", "night"};
    for (uint8_t floor = 0; floor < INCANTATION_OCCUPATION_FLOORS; floor++) {
        for (uint8_t phase = DUEL_SKY_DAWN; phase <= DUEL_SKY_NIGHT; phase++) {
            if (floor == DUEL_CIVIC_FLOOR_COMMONS && phase == DUEL_SKY_DAWN)
                continue; /* covered by sky_commons_dawn_idle_12hp */
            duel_render_t sky = {0}; duel_render_from_world(&sky, &world);
            sky.seed = 0x5au; sky.civic_phase = 19u;
            sky.civic = DUEL_CIVIC_PACK(floor,
                floor == DUEL_CIVIC_FLOOR_SPECIAL ? DUEL_CIVIC_MODE_QUIET
                                                  : DUEL_CIVIC_MODE_NORMAL, 0);
            sky.secondary = DUEL_SECONDARY_SKY_PACK(0, phase);
            char name[48]; snprintf(name, sizeof name, "sky_%s_%s",
                                    floor_name[floor], sky_name[phase]);
            add_render_case(name, &sky, 7u);
        }
    }

    static const uint8_t ambience_trend[] = {
        TREND_DECELERATING, TREND_STEADY,
        TREND_ACCELERATING, TREND_IRREGULAR
    };
    static const char *tempo_name[] = {"deliberate", "flowing", "rapid", "frantic"};
    for (uint8_t tempo = TEMPO_DELIBERATE; tempo <= TEMPO_FRANTIC; tempo++) {
        duel_render_t ambience = {0}; duel_render_from_world(&ambience, &world);
        ambience.seed = 0x5au; ambience.civic_phase = 19u;
        ambience.civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_WORKSHOP,
                                         DUEL_CIVIC_MODE_NORMAL, 0);
        ambience.local_ambience = INCANTATION_AMBIENCE_PACK(
            true, tempo, ambience_trend[tempo]);
        char name[48]; snprintf(name, sizeof name, "typing_%s", tempo_name[tempo]);
        add_render_case(name, &ambience, 7u);
    }

    static const char *diplomacy_name[] = {"left_advantage", "right_advantage", "balance"};
    static const uint8_t diplomacy_target[] = {
        DUEL_CIVIC_EVENT_TARGET_LEFT, DUEL_CIVIC_EVENT_TARGET_RIGHT,
        DUEL_CIVIC_EVENT_TARGET_SHARED
    };
    for (size_t i = 0; i < 2u; i++) { /* balance is the ordinary event case above */
        duel_render_t diplomacy = {0}; duel_render_from_world(&diplomacy, &world);
        diplomacy.seed = 0x5au; diplomacy.civic_phase = 19u;
        diplomacy.civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_COMMONS,
                                          DUEL_CIVIC_MODE_NORMAL, 0);
        diplomacy.revision = DUEL_EVENT_PACK(DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER,
            DUEL_CIVIC_EVENT_PHASE_ACTIVE, diplomacy_target[i]);
        char name[48]; snprintf(name, sizeof name, "diplomacy_%s", diplomacy_name[i]);
        add_render_case(name, &diplomacy, 7u);
    }

    duel_render_t variant = {0}; duel_render_from_world(&variant, &world);
    variant.seed = 0x6bu; variant.civic_phase = 23u;
    variant.civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_RESEARCH, DUEL_CIVIC_MODE_NORMAL, 0);
    for (uint8_t life = DUEL_CIVIC_VISIT_ARRIVING; life <= DUEL_CIVIC_VISIT_RESOLVING; life++) {
        if (life == DUEL_CIVIC_VISIT_WAITING) continue;
        char name[48]; snprintf(name, sizeof name, "courier_lifecycle_%u", life);
        variant.shared_pres = DUEL_VISITOR_PACK(DUEL_CIVIC_COURIER_MESSENGER, 0, life);
        add_render_case(name, &variant, 7u);
    }
    variant.shared_pres = (uint8_t)(DUEL_VISITOR_PACK(DUEL_CIVIC_COURIER_BEACON, 0,
        DUEL_CIVIC_VISIT_WAITING) | DUEL_VISITOR_DENSITY_PACK(DUEL_CIVIC_DENSITY_FEW));
    add_render_case("courier_density_few", &variant, 7u);
    variant.shared_pres = (uint8_t)(DUEL_VISITOR_PACK(DUEL_CIVIC_COURIER_BEACON, 0,
        DUEL_CIVIC_VISIT_WAITING) | DUEL_VISITOR_DENSITY_PACK(DUEL_CIVIC_DENSITY_MANY));
    add_render_case("courier_density_many", &variant, 7u);
    variant.civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_RESEARCH, DUEL_CIVIC_MODE_QUIET, 0);
    variant.shared_pres = DUEL_VISITOR_PACK(DUEL_CIVIC_COURIER_MESSENGER, 0,
                                             DUEL_CIVIC_VISIT_ARRIVING);
    add_render_case("courier_quiet_arrival", &variant, 7u);
    variant.civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_WORKSHOP, DUEL_CIVIC_MODE_NORMAL, 0);
    variant.floor_transition = INCANTATION_FLOOR_TRANSITION_PACK(DUEL_CIVIC_FLOOR_COMMONS, 1, true);
    add_render_case("courier_transition", &variant, 7u);
    variant.floor_transition = 0; variant.view.outcome_overlay |= 0x10u;
    add_render_case("courier_full_scry", &variant, 7u);

    variant.view.outcome_overlay &= (uint8_t)~0x10u; variant.shared_pres = 0;
    variant.civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_RESEARCH, DUEL_CIVIC_MODE_NORMAL, 0);
    for (uint8_t phase = DUEL_CIVIC_EVENT_PHASE_ARMED;
         phase <= DUEL_CIVIC_EVENT_PHASE_COOLDOWN; phase++) {
        if (phase == DUEL_CIVIC_EVENT_PHASE_ACTIVE) continue;
        char name[48]; snprintf(name, sizeof name, "event_lifecycle_%u", phase);
        variant.revision = DUEL_EVENT_PACK(DUEL_CIVIC_EVENT_RUNAWAY_SCROLL, phase,
                                            DUEL_CIVIC_EVENT_TARGET_LEFT);
        add_render_case(name, &variant, 7u);
    }
    variant.civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_RESEARCH, DUEL_CIVIC_MODE_QUIET, 0);
    variant.revision = DUEL_EVENT_PACK(DUEL_CIVIC_EVENT_RUNAWAY_SCROLL,
        DUEL_CIVIC_EVENT_PHASE_ACTIVE, DUEL_CIVIC_EVENT_TARGET_LEFT);
    add_render_case("event_quiet", &variant, 7u);
    variant.civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_WORKSHOP, DUEL_CIVIC_MODE_NORMAL, 0);
    variant.floor_transition = INCANTATION_FLOOR_TRANSITION_PACK(DUEL_CIVIC_FLOOR_COMMONS, 1, true);
    add_render_case("event_transition", &variant, 7u);
    variant.floor_transition = 0; variant.view.outcome_overlay |= 0x10u;
    add_render_case("event_full_scry", &variant, 7u);

    /* Projection sanity: building a scenario must not erase the authored
     * courier/event coordination bytes. (The renders themselves are pinned by
     * the full scenario-gallery loop at the end of this catalog.) */
    const duel_scenario_t *scenario = duel_scenario_find("courier-messenger");
    duel_render_t projected;
    if (!scenario || !duel_scenario_build(scenario, &projected) ||
        DUEL_VISITOR_KIND(projected.shared_pres) == DUEL_CIVIC_COURIER_NONE) abort();
    scenario = duel_scenario_find("event-scroll");
    if (!scenario || !duel_scenario_build(scenario, &projected) ||
        DUEL_EVENT_ID(projected.revision) == DUEL_CIVIC_EVENT_NONE) abort();

    sim_init(&world, SIMF_AUTHORITATIVE, 0);
    world.wiz[0].hp = 0;
    world.spell[0].active = 1;
    world.spell[0].descriptor = descriptor(SPELL_PROJECTILE, 3);
    world.spell[0].kind = DUEL_KIND_WITH_TIER(
        DUEL_KIND_PACK(ELEM_FORCE, MOD_NONE, PAY_IMPACT), 2);
    world.spell[0].progress = 72u;
    duel_render_t cross = {0}; duel_render_from_world(&cross, &world);
    cross.seed = 0x39u; cross.civic_phase = 31u;
    cross.civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_WORKSHOP, DUEL_CIVIC_MODE_NORMAL, 0);
    cross.layer = DUEL_RENDER_LAYER_PACK(2, DUEL_RENDER_LOCAL_LEFT);
    cross.shared_pres = (uint8_t)(DUEL_VISITOR_PACK(DUEL_CIVIC_COURIER_PARCEL, 0,
        DUEL_CIVIC_VISIT_AGING) | DUEL_VISITOR_DENSITY_PACK(DUEL_CIVIC_DENSITY_MANY));
    add_render_case("cross_courier_combat_health_attune", &cross, 13u);
    cross.shared_pres = 0;
    cross.revision = DUEL_EVENT_PACK(DUEL_CIVIC_EVENT_DAMAGE_COMPLAINT,
        DUEL_CIVIC_EVENT_PHASE_ACTIVE, DUEL_CIVIC_EVENT_TARGET_RIGHT);
    cross.flags = DUEL_RENDER_STALE; cross.diag_tick = 17u; cross.diag_overflow = 3u;
    add_render_case_diagnostics("cross_event_stale_diagnostics", &cross, 13u);

    static const uint8_t hp_cases[] = {0, 1, 6};
    for (size_t i = 0; i < sizeof hp_cases; i++) {
        char name[48];
        sim_init(&world, SIMF_AUTHORITATIVE, 0);
        world.wiz[0].hp = world.wiz[1].hp = hp_cases[i];
        snprintf(name, sizeof name, "health_%u", hp_cases[i]);
        add_case(name, &world, 0, 0);
    }

    sim_init(&world, SIMF_AUTHORITATIVE, 0);
    duel_render_t local = {0}; duel_render_from_world(&local, &world);
    local.seed = 0x5au; local.civic_phase = 23u;
    local.civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_RESEARCH, DUEL_CIVIC_MODE_NORMAL, 0);
    local.layer = DUEL_RENDER_LAYER_PACK(1, DUEL_RENDER_LOCAL_LEFT);
    add_render_case("attunement_left", &local, 7u);
    local.layer = DUEL_RENDER_LAYER_PACK(2, DUEL_RENDER_LOCAL_RIGHT);
    add_render_case("attunement_right", &local, 7u);
    add_bilateral_attunement_case("attunement_bilateral_pending", &world);

    for (uint8_t scene = 0; scene < SCRY_SCENES; scene++) {
        char name[48];
        sim_init(&world, SIMF_AUTHORITATIVE, 0);
        world.scry.state = SCRY_ACTIVE; world.scry.scene = scene;
        duel_render_t scry = {0}; duel_render_from_world(&scry, &world);
        scry.seed = 0x5au; scry.civic_phase = 23u;
        scry.civic = DUEL_CIVIC_PACK(scene, DUEL_CIVIC_MODE_NORMAL, 0);
        scry.external = DUEL_HOST_CONTEXT_PACK(true, scene, scene + 2u, scene == 2u);
        scry.alert = DUEL_HOST_ALERT_PACK((uint8_t)(DUEL_HOST_CATEGORY_TRANSFER + scene),
                                         (uint8_t)(DUEL_HOST_PRIORITY_LOW + scene), scene * 3u);
        scry.layer = DUEL_RENDER_LAYER_PACK(scene, DUEL_RENDER_LOCAL_NONE);
        snprintf(name, sizeof name, "scry_diegetic_scene_%u", scene);
        add_render_case(name, &scry, 7u);
    }
    for (uint8_t phase = 0; phase < 4u; phase++) {
        char name[48]; snprintf(name, sizeof name, "floor_transition_phase_%u", phase);
        add_case_civic(name, &world, 7u, 0,
                       DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_WORKSHOP,
                                       DUEL_CIVIC_MODE_NORMAL, 0),
                       INCANTATION_FLOOR_TRANSITION_PACK(DUEL_CIVIC_FLOOR_COMMONS,
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
    for (uint8_t floor = 0; floor < INCANTATION_OCCUPATION_FLOORS; floor++)
        for (size_t i = 0; i < sizeof aftermath_kind; i++) {
            sim_init(&world, SIMF_AUTHORITATIVE, 0);
            world.aftermath[0].kind = aftermath_kind[i];
            world.aftermath[0].ticks = aftermath_kind[i] == AFTER_FIRE ? 130u : 75u;
            world.aftermath[0].intensity = 3u;
            world.world_state = aftermath_kind[i] == AFTER_FIRE ? WORLD_CRISIS : WORLD_RECOVERY;
            char name[48]; snprintf(name, sizeof name, "after_%s_%s",
                                    floor_name[floor], aftermath_name[i]);
            add_case_civic(name, &world, (uint32_t)(floor * 11u + i), 0,
                           DUEL_CIVIC_PACK(floor, DUEL_CIVIC_MODE_NORMAL, 0), 0);
        }

    static const char *reaction_name[] = { "heal", "complaint", "roof_panic",
                                           "void_inspect", "combine_repair", "void_collapse" };
    static const uint8_t reaction_kind[] = { 7, 9, 10, 11, 12, 13 };
    for (size_t i = 0; i < sizeof reaction_kind; i++) {
        sim_init(&world, SIMF_AUTHORITATIVE, 0);
        add_case(reaction_name[i], &world, (uint32_t)i, reaction_kind[i]);
    }

    /* Pin the ENTIRE scenario gallery under the golden determinism check —
     * previously only two scenarios were exercised, leaving the rest of
     * scenarios.c dead in the tracked tree. Each renders at its declared
     * frame with its declared diagnostics flag. */
    for (size_t i = 0; i < duel_scenario_count(); i++) {
        const duel_scenario_t *scenario = duel_scenario_at(i);
        duel_fb_t left, right;
        duel_scenario_render(scenario, scenario->frame, &left, &right);
        char name[48];
        snprintf(name, sizeof name, "scenario_%s", scenario->name);
        record_case(name, &left, &right);
    }
}

static int write_golden(const char *path) {
    FILE *file = fopen(path, "w");
    if (!file) return 1;
    for (size_t i = 0; i < ncases; i++)
        fprintf(file, "%s %016" PRIx64 "\n", cases[i].name, cases[i].hash);
    return fclose(file) != 0;
}

/* Per-case comparison so a regression names the exact scene and hashes, and a
 * missing/renamed/extra case is reported as such instead of desyncing every
 * subsequent line. */
static int verify_golden(const char *path) {
    FILE *file = fopen(path, "r");
    if (!file) { perror(path); return 1; }
    bool ok = true;
    for (size_t i = 0; i < ncases; i++) {
        char name[48]; uint64_t hash;
        if (fscanf(file, "%47s %" SCNx64, name, &hash) != 2) {
            printf("FAIL visual %s: golden file ends early (case %zu of %zu)\n",
                   cases[i].name, i, ncases);
            ok = false;
            break;
        }
        if (strcmp(name, cases[i].name) != 0) {
            printf("FAIL visual: golden names '%s' where catalog has '%s' "
                   "(case added/removed/renamed?)\n", name, cases[i].name);
            ok = false;
            break; /* the sequences are misaligned; later lines are noise */
        }
        if (hash != cases[i].hash) {
            printf("FAIL visual %s expected=%016" PRIx64 " got=%016" PRIx64 "\n",
                   name, hash, cases[i].hash);
            ok = false; /* aligned mismatch: keep going, report every one */
        }
    }
    char trailing[64];
    if (ok && fscanf(file, "%63s", trailing) == 1) {
        printf("FAIL visual: golden has extra case '%s' beyond the %zu in the catalog\n",
               trailing, ncases);
        ok = false;
    }
    fclose(file);
    printf("%s visual_incantation_exact_framebuffer_hashes (%zu scenes)\n", ok ? "PASS" : "FAIL", ncases);
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
    printf("%s visual_incantation_catalog_unique\n", unique ? "PASS" : "FAIL");
    if (argc == 3 && !strcmp(argv[1], "--write-golden"))
        return unique ? write_golden(argv[2]) : 1;
    if (argc != 2) { fprintf(stderr, "usage: %s [--write-golden] PATH\n", argv[0]); return 2; }
    return unique ? verify_golden(argv[1]) : 1;
}
