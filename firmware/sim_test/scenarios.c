#include <string.h>

#include "duel_courier.h" // couriers
#include "duel_event.h"   // rare events
#include "duel_host.h"
#include "duel_resident.h"
#include "duel_sim.h"
#include "scenarios.h"

#define SCENE(name_, group_, desc_, frame_, diag_) {name_, group_, desc_, frame_, diag_}

static const duel_scenario_t scenarios[] = {
    SCENE("duel-idle", "duel", "paired idle actors and health", 0, false),
    SCENE("pose-cast", "actors", "left cast and right ward silhouettes", 3, false),
    SCENE("pose-recover", "actors", "recover sparks and roster marks", 5, false),
    SCENE("recipe-force-short", "recipes", "short FORCE carrier", 0, false),
    SCENE("recipe-ember-medium", "recipes", "medium EMBER carrier", 2, false),
    SCENE("recipe-frost-long", "recipes", "long FROST carrier", 4, false),
    SCENE("recipe-void-saturated", "recipes", "saturated VOID carrier", 6, false),
    SCENE("short-cast", "recipes", "short charge grammar", 1, false),
    SCENE("long-cast", "recipes", "long charge grammar", 3, false),
    SCENE("impact", "outcomes", "local damaging impact", 0, false),
    SCENE("deflect", "outcomes", "dominant ward deflection", 0, false),
    SCENE("fizzle", "outcomes", "harmless contraction", 0, false),
    SCENE("void-pierce", "outcomes", "VOID punctures a raised ward", 2, false),
    SCENE("life-collapse", "lifecycle", "wizard collapse", 0, false),
    SCENE("life-downed", "lifecycle", "protected downed wizard", 0, false),
    SCENE("life-medic", "lifecycle", "medic drag-off", 0, false),
    SCENE("life-replace", "lifecycle", "replacement walk-in", 2, false),
    SCENE("archive-idle", "archive", "shared gap arch and asymmetric shelves", 0, false),
    SCENE("archive-pulse", "archive", "reactive paired runes", 4, false),
    SCENE("archive-cast", "archive", "Archive under a long charge", 3, false),
    SCENE("archive-impact", "archive", "Archive under combat outcome", 0, false),
    SCENE("archive-ko", "archive", "Archive under medic lifecycle", 0, false),
    SCENE("archive-scry", "archive", "scry restoration over Archive", 0, false),
    SCENE("terminal-completion", "alerts", "low-priority terminal alert", 0, false),
    SCENE("aggregated-normal", "alerts", "aggregated communication alert", 0, false),
    SCENE("persistent-critical", "alerts", "anchored critical security alert", 0, false),
    SCENE("aged-alert", "alerts", "aged transfer alert", 0, false),
    SCENE("alert-system", "alerts", "system category mark", 0, false),
    SCENE("alert-calendar", "alerts", "calendar category mark", 0, false),
    SCENE("alert-other", "alerts", "other category mark", 0, false),
    SCENE("scry", "scry", "layer, host, alert, and scene readout", 0, false),
    SCENE("stale-link", "recovery", "gap-side broken-link mark", 0, false),
    SCENE("diagnostics", "diagnostics", "tick and overflow diagnostics", 0, true),
    SCENE("alert-impact", "precedence", "alert protected above impact", 0, false),
    SCENE("archive-alert", "precedence", "alert protected above Archive", 4, false),
    SCENE("alert-under-scry", "precedence", "alert summarized inside scry", 0, false),
    SCENE("scry-stale-diagnostics", "precedence", "scry, stale link, and diagnostics", 7, true),
    // Twin Cities canonical gallery.
    SCENE("floor-commons", "floors", "Commons/post floor, both city-states", 0, false),
    SCENE("floor-research", "floors", "Archive/Research floor, both city-states", 0, false),
    SCENE("floor-workshop", "floors", "Workshop/Forge floor, both city-states", 0, false),
    SCENE("city-astral", "cities", "astral (left) vs mechanical (right) commons", 0, false),
    SCENE("city-mechanical", "cities", "astral vs mechanical workshop architecture", 0, false),
    SCENE("civic-quiet", "cities", "QUIET civic mode subdues the resident", 0, false),
    SCENE("resident-diligent", "residents", "diligent resident at work", 0, false),
    SCENE("resident-curious", "residents", "curious resident inspecting", 0, false),
    SCENE("resident-nervous", "residents", "nervous resident watching the roof", 0, false),
    SCENE("resident-proud", "residents", "proud resident", 0, false),
    SCENE("resident-distracted", "residents", "distracted resident", 0, false),
    SCENE("workshop-idle", "workshop", "forge and assembly residents at work", 0, false),
    SCENE("workshop-cast", "workshop", "combat over the workshop floor", 3, false),
    // --- couriers ---
    SCENE("courier-messenger", "couriers", "communication messenger bird (left)", 0, false),
    SCENE("courier-parcel", "couriers", "transfer parcel cart (right)", 0, false),
    SCENE("courier-beacon", "couriers", "system signal beacon (right)", 0, false),
    SCENE("courier-sentinel", "couriers", "persistent security sentinel (left)", 0, false),
    SCENE("courier-arriving", "couriers", "messenger arriving by the gap lift", 0, false),
    SCENE("courier-aging", "couriers", "parcel aged and gathering dust", 0, false),
    SCENE("courier-resolving", "couriers", "messenger departing, resolved", 0, false),
    SCENE("courier-count-few", "couriers", "beacon, 2-4 count bucket", 0, false),
    SCENE("courier-count-many", "couriers", "beacon, 5+ count bucket", 0, false),
    // --- rare events ---
    // All six families across representative phases, a QUIET-calmed case, and a
    // safety-gate-suppressed case that draws nothing extra (floor only).
    SCENE("event-scroll", "rare_events", "runaway scroll unrolling (left city)", 0, false),
    SCENE("event-gear", "rare_events", "jammed gear grinding (right city)", 0, false),
    SCENE("event-break", "rare_events", "work-break steaming mug (left city)", 0, false),
    SCENE("event-complaint", "rare_events", "damage complaint crack resolving (right city)", 0,
          false),
    SCENE("event-courier", "rare_events", "diplomatic courier banner across the gap", 0, false),
    SCENE("event-sky", "rare_events", "civic sky aurora across both cities", 0, false),
    SCENE("event-armed", "rare_events", "runaway scroll armed (pre-active)", 0, false),
    SCENE("event-cooldown", "rare_events", "jammed gear cooldown residue", 0, false),
    SCENE("event-quiet", "rare_events", "QUIET mode calms a work-break event", 0, false),
    SCENE("event-suppressed", "rare_events", "safety-gated slot draws nothing extra", 0, false),
};

size_t duel_scenario_count(void) { return sizeof scenarios / sizeof scenarios[0]; }

const duel_scenario_t *duel_scenario_at(size_t index) {
    return index < duel_scenario_count() ? &scenarios[index] : NULL;
}

const duel_scenario_t *duel_scenario_find(const char *name) {
    for (size_t i = 0; i < duel_scenario_count(); i++)
        if (strcmp(scenarios[i].name, name) == 0)
            return &scenarios[i];
    return NULL;
}

static uint8_t spell_kind(uint8_t element, uint8_t modifier, uint8_t tier) {
    return DUEL_KIND_WITH_TIER(DUEL_KIND_PACK(element, modifier, PAY_IMPACT), tier);
}

static void set_life(sim_world_t *world, uint8_t life, uint8_t ticks) {
    world->wiz[SIM_SIDE_R].life = life;
    world->wiz[SIM_SIDE_R].life_ticks = ticks;
    world->wiz[SIM_SIDE_R].hp = 0;
    if (life == LIFE_REPLACE)
        world->wiz[SIM_SIDE_R].variant = 1;
}

static void set_alert(duel_render_t *r, uint8_t count, uint8_t category, uint8_t priority,
                      uint8_t age, bool persistent) {
    uint8_t scene = DUEL_HOST_CONTEXT_SCENE(r->external);
    r->external = DUEL_HOST_CONTEXT_PACK(1, scene, count, persistent);
    r->alert = DUEL_HOST_ALERT_PACK(category, priority, age);
}

// Smallest seed whose LEFT city resident carries the wanted personality, so the
// personality gallery shows all five deterministically without hand-tuning hashes.
static uint8_t seed_for_personality(uint8_t want) {
    for (int s = 0; s < 256; s++)
        if (civic_resident_personality((uint8_t)s, true) == want)
            return (uint8_t)s;
    return 0;
}

// --- couriers ---
// Drive the courier scenarios through the real derivation engine, then pack the
// result into shared_pres exactly as the master relays it.
static void set_courier(duel_render_t *r, uint8_t category, uint8_t count, uint8_t age,
                        bool persistent) {
    r->shared_pres = civic_visitor_shared_pres(
        civic_visitor_derive(r->seed, r->civic_phase, category, count, age, persistent));
}

bool duel_scenario_build(const duel_scenario_t *scenario, duel_render_t *r) {
    if (!scenario || !r)
        return false;
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    *r = (duel_render_t){0};
    const char *name = scenario->name;
    uint8_t force = spell_kind(ELEM_FORCE, MOD_NONE, SPELL_TIER_MEDIUM);

    if (strncmp(name, "archive-", 8) == 0) {
        r->external = DUEL_HOST_CONTEXT_PACK(1, DUEL_HOST_SCENE_ARCHIVE, 0, false);
        // Twin Cities drives the floor occupation from the civic byte, not the scene:
        // the Archive scenarios now name the Research floor explicitly.
        r->civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_RESEARCH, DUEL_CIVIC_MODE_NORMAL,
                                   DUEL_CIVIC_INTENSITY_CALM);
    }

    /* NOTE: these worlds are projected through the 18-byte canonical view
     * before rendering, so only view-visible fields matter: a spell needs a
     * nonzero descriptor + progress (pos/dir/kind are re-derived), and a ward
     * needs ward_strength (the sim-side shield_ticks never reaches the wire). */
    if (strcmp(name, "duel-idle") == 0 || strcmp(name, "archive-idle") == 0) {
    } else if (strcmp(name, "pose-cast") == 0) {
        w.wiz[SIM_SIDE_L].pose = POSE_CAST;
        w.wiz[SIM_SIDE_L].inc_state = INC_WINDUP;
        w.wiz[SIM_SIDE_L].windup_total = 10;
        w.wiz[SIM_SIDE_L].cast_windup = 5;
        w.wiz[SIM_SIDE_L].pending_desc =
            SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE, TRAJ_MID, 2, STATUS_NONE,
                            INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, 1);
        w.wiz[SIM_SIDE_R].ward_strength = 2;
        w.wiz[SIM_SIDE_R].ward_capacity = 2;
    } else if (strcmp(name, "pose-recover") == 0) {
        w.wiz[SIM_SIDE_L].pose = POSE_RECOVER;
        w.wiz[SIM_SIDE_L].variant = 1;
        w.wiz[SIM_SIDE_R].variant = 3;
    } else if (strncmp(name, "recipe-", 7) == 0) {
        uint8_t elem = strcmp(name, "recipe-force-short") == 0    ? ELEM_FORCE
                       : strcmp(name, "recipe-ember-medium") == 0 ? ELEM_EMBER
                       : strcmp(name, "recipe-frost-long") == 0   ? ELEM_FROST
                                                                  : ELEM_VOID;
        uint8_t mag = strcmp(name, "recipe-force-short") == 0    ? 1u
                      : strcmp(name, "recipe-ember-medium") == 0 ? 2u
                      : strcmp(name, "recipe-frost-long") == 0   ? 3u
                                                                 : 4u;
        uint32_t desc =
            SPELL_DESC_PACK(SPELL_PROJECTILE, elem, PAY_DAMAGE, TRAJ_MID, mag, STATUS_NONE,
                            INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, mag - 1u);
        w.spell[SIM_SIDE_L] =
            (sim_spell_t){.active = 1, .progress = 55, .dir = 4, .descriptor = desc};
        w.spell[SIM_SIDE_R] =
            (sim_spell_t){.active = 1, .progress = 200, .dir = -4, .descriptor = desc};
    } else if (strcmp(name, "short-cast") == 0 || strcmp(name, "long-cast") == 0) {
        bool small = strcmp(name, "short-cast") == 0;
        w.wiz[SIM_SIDE_L].pose = POSE_CAST;
        w.wiz[SIM_SIDE_L].inc_state = INC_WINDUP;
        w.wiz[SIM_SIDE_L].windup_total = 10;
        w.wiz[SIM_SIDE_L].cast_windup = 8;
        w.wiz[SIM_SIDE_L].pending_desc = SPELL_DESC_PACK(
            SPELL_PROJECTILE, small ? ELEM_FORCE : ELEM_FROST, PAY_DAMAGE, TRAJ_MID, small ? 1 : 3,
            STATUS_NONE, INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, 1);
        /* cast_tier rides the ward bits on the wire: strength - 1. */
        w.wiz[SIM_SIDE_L].ward_strength = small ? 1u : 3u;
        w.wiz[SIM_SIDE_L].ward_capacity = w.wiz[SIM_SIDE_L].ward_strength;
    } else if (strcmp(name, "impact") == 0 || strcmp(name, "archive-impact") == 0 ||
               strcmp(name, "alert-impact") == 0) {
        w.wiz[SIM_SIDE_R].hp = SIM_MAX_HP - 1;
        r->flash_frames = 10;
        r->flash_kind = FX_IMPACT_R;
        r->flash_spell_kind = force;
        if (strcmp(name, "alert-impact") == 0)
            set_alert(r, 2, DUEL_HOST_CATEGORY_SYSTEM, DUEL_HOST_PRIORITY_CRITICAL, 0, false);
    } else if (strcmp(name, "deflect") == 0) {
        w.wiz[SIM_SIDE_R].shield_ticks = SIM_SHIELD_TICKS;
        r->flash_frames = 7;
        r->flash_kind = FX_DEFLECT_R;
        r->flash_spell_kind = force;
    } else if (strcmp(name, "fizzle") == 0) {
        set_life(&w, LIFE_DOWNED, SIM_DOWNED_TICKS / 2);
        r->flash_frames = 7;
        r->flash_kind = FX_FIZZLE_R;
        r->flash_spell_kind = force;
    } else if (strcmp(name, "void-pierce") == 0) {
        w.wiz[SIM_SIDE_R].ward_strength = 3;
        w.wiz[SIM_SIDE_R].ward_capacity = 3;
        w.spell[SIM_SIDE_L] =
            (sim_spell_t){.active = 1,
                          .progress = 236,
                          .dir = 4,
                          .descriptor = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_VOID, PAY_DAMAGE,
                                                        TRAJ_MID, 3, STATUS_NONE, INTERACT_PHASE,
                                                        TEMPO_RAPID, TREND_STEADY, 2)};
    } else if (strcmp(name, "life-collapse") == 0) {
        set_life(&w, LIFE_COLLAPSE, SIM_COLLAPSE_TICKS / 2);
    } else if (strcmp(name, "life-downed") == 0) {
        set_life(&w, LIFE_DOWNED, SIM_DOWNED_TICKS / 2);
    } else if (strcmp(name, "life-medic") == 0 || strcmp(name, "archive-ko") == 0) {
        set_life(&w, LIFE_MEDIC, SIM_MEDIC_TICKS / 2);
    } else if (strcmp(name, "life-replace") == 0) {
        set_life(&w, LIFE_REPLACE, SIM_REPLACE_TICKS / 2);
    } else if (strcmp(name, "archive-pulse") == 0) {
        w.wiz[0].ward_strength = w.wiz[1].ward_strength = 2;
        w.wiz[0].ward_capacity = w.wiz[1].ward_capacity = 2;
    } else if (strcmp(name, "archive-cast") == 0) {
        w.wiz[0].pose = POSE_CAST;
        w.wiz[0].cast_windup = 3;
        w.wiz[0].cast_tier = SPELL_TIER_LONG;
    } else if (strcmp(name, "archive-scry") == 0) {
        w.scry.state = SCRY_ACTIVE;
        w.scry.scene = DUEL_HOST_SCENE_ARCHIVE;
        r->layer = 3;
    } else if (strcmp(name, "terminal-completion") == 0) {
        set_alert(r, 1, DUEL_HOST_CATEGORY_TERMINAL, DUEL_HOST_PRIORITY_LOW, 0, false);
    } else if (strcmp(name, "aggregated-normal") == 0) {
        set_alert(r, 4, DUEL_HOST_CATEGORY_COMMUNICATION, DUEL_HOST_PRIORITY_NORMAL, 0, false);
    } else if (strcmp(name, "persistent-critical") == 0) {
        set_alert(r, 2, DUEL_HOST_CATEGORY_SECURITY, DUEL_HOST_PRIORITY_CRITICAL, 0, true);
    } else if (strcmp(name, "aged-alert") == 0) {
        set_alert(r, 1, DUEL_HOST_CATEGORY_TRANSFER, DUEL_HOST_PRIORITY_NORMAL, 6, false);
    } else if (strcmp(name, "alert-system") == 0) {
        set_alert(r, 1, DUEL_HOST_CATEGORY_SYSTEM, DUEL_HOST_PRIORITY_NORMAL, 0, false);
    } else if (strcmp(name, "alert-calendar") == 0) {
        set_alert(r, 1, DUEL_HOST_CATEGORY_CALENDAR, DUEL_HOST_PRIORITY_NORMAL, 0, false);
    } else if (strcmp(name, "alert-other") == 0) {
        set_alert(r, 1, DUEL_HOST_CATEGORY_OTHER, DUEL_HOST_PRIORITY_NORMAL, 0, false);
    } else if (strcmp(name, "scry") == 0 || strcmp(name, "alert-under-scry") == 0 ||
               strcmp(name, "scry-stale-diagnostics") == 0) {
        w.scry.state = SCRY_ACTIVE;
        w.scry.scene = DUEL_HOST_SCENE_FOCUS;
        r->layer = 3;
        r->external = DUEL_HOST_CONTEXT_PACK(1, DUEL_HOST_SCENE_FOCUS, 0, false);
        if (strcmp(name, "scry") != 0)
            set_alert(r, 3, DUEL_HOST_CATEGORY_CALENDAR, DUEL_HOST_PRIORITY_CRITICAL, 1, true);
        if (strcmp(name, "scry-stale-diagnostics") == 0) {
            r->flags |= DUEL_RENDER_STALE;
            w.overflow_count = 3;
            w.tick = 17;
        }
    } else if (strcmp(name, "stale-link") == 0) {
        r->flags |= DUEL_RENDER_STALE;
    } else if (strcmp(name, "diagnostics") == 0) {
        w.overflow_count = 3;
        w.tick = 17;
    } else if (strcmp(name, "archive-alert") == 0) {
        r->external = DUEL_HOST_CONTEXT_PACK(1, DUEL_HOST_SCENE_ARCHIVE, 0, false);
        w.wiz[0].shield_ticks = SIM_SHIELD_TICKS / 2;
        set_alert(r, 2, DUEL_HOST_CATEGORY_SECURITY, DUEL_HOST_PRIORITY_CRITICAL, 0, true);
        r->external =
            DUEL_HOST_CONTEXT_PACK(1, DUEL_HOST_SCENE_ARCHIVE, DUEL_HOST_CONTEXT_NOTIF(r->external),
                                   DUEL_HOST_CONTEXT_PERSISTENT(r->external));
    } else if (strcmp(name, "floor-commons") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_COMMONS, DUEL_CIVIC_MODE_NORMAL,
                                   DUEL_CIVIC_INTENSITY_CALM);
        r->seed = 3;
        r->civic_phase = 8;
    } else if (strcmp(name, "floor-research") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_RESEARCH, DUEL_CIVIC_MODE_NORMAL,
                                   DUEL_CIVIC_INTENSITY_CALM);
        r->seed = 3;
        r->civic_phase = 8;
    } else if (strcmp(name, "floor-workshop") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_WORKSHOP, DUEL_CIVIC_MODE_NORMAL,
                                   DUEL_CIVIC_INTENSITY_CALM);
        r->seed = 3;
        r->civic_phase = 8;
    } else if (strcmp(name, "city-astral") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_COMMONS, DUEL_CIVIC_MODE_NORMAL,
                                   DUEL_CIVIC_INTENSITY_ACTIVE);
        r->seed = 17;
        r->civic_phase = 72;
    } else if (strcmp(name, "city-mechanical") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_WORKSHOP, DUEL_CIVIC_MODE_NORMAL,
                                   DUEL_CIVIC_INTENSITY_ACTIVE);
        r->seed = 17;
        r->civic_phase = 88;
    } else if (strcmp(name, "civic-quiet") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_RESEARCH, DUEL_CIVIC_MODE_QUIET,
                                   DUEL_CIVIC_INTENSITY_CALM);
        r->seed = 5;
        r->civic_phase = 104;
    } else if (strcmp(name, "resident-diligent") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_COMMONS, DUEL_CIVIC_MODE_NORMAL,
                                   DUEL_CIVIC_INTENSITY_CALM);
        r->seed = seed_for_personality(DUEL_CIVIC_PERSONALITY_DILIGENT);
        r->civic_phase = 40;
    } else if (strcmp(name, "resident-curious") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_COMMONS, DUEL_CIVIC_MODE_NORMAL,
                                   DUEL_CIVIC_INTENSITY_CALM);
        r->seed = seed_for_personality(DUEL_CIVIC_PERSONALITY_CURIOUS);
        r->civic_phase = 40;
    } else if (strcmp(name, "resident-nervous") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_COMMONS, DUEL_CIVIC_MODE_NORMAL,
                                   DUEL_CIVIC_INTENSITY_CALM);
        r->seed = seed_for_personality(DUEL_CIVIC_PERSONALITY_NERVOUS);
        r->civic_phase = 40;
    } else if (strcmp(name, "resident-proud") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_COMMONS, DUEL_CIVIC_MODE_NORMAL,
                                   DUEL_CIVIC_INTENSITY_CALM);
        r->seed = seed_for_personality(DUEL_CIVIC_PERSONALITY_PROUD);
        r->civic_phase = 40;
    } else if (strcmp(name, "resident-distracted") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_COMMONS, DUEL_CIVIC_MODE_NORMAL,
                                   DUEL_CIVIC_INTENSITY_CALM);
        r->seed = seed_for_personality(DUEL_CIVIC_PERSONALITY_DISTRACTED);
        r->civic_phase = 40;
    } else if (strcmp(name, "workshop-idle") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_WORKSHOP, DUEL_CIVIC_MODE_NORMAL,
                                   DUEL_CIVIC_INTENSITY_ACTIVE);
        r->seed = 9;
        r->civic_phase = 24;
    } else if (strcmp(name, "workshop-cast") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_WORKSHOP, DUEL_CIVIC_MODE_URGENT,
                                   DUEL_CIVIC_INTENSITY_BUSY);
        r->seed = 9;
        r->civic_phase = 24;
        w.wiz[SIM_SIDE_L].pose = POSE_CAST;
        w.wiz[SIM_SIDE_L].cast_windup = 3;
        w.wiz[SIM_SIDE_L].cast_tier = SPELL_TIER_LONG;
        // --- couriers --- (all on a shared COMMONS/seed/phase base so the
        // courier itself is the only difference between the pairs).
    } else if (strncmp(name, "courier-", 8) == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_COMMONS, DUEL_CIVIC_MODE_NORMAL,
                                   DUEL_CIVIC_INTENSITY_CALM);
        r->seed = 42;
        r->civic_phase = 48;
        if (strcmp(name, "courier-messenger") == 0) {
            // communication, 1, pending -> messenger / left / WAITING / single
            set_courier(r, DUEL_HOST_CATEGORY_COMMUNICATION, 1, 1, false);
        } else if (strcmp(name, "courier-parcel") == 0) {
            // transfer, 1, pending -> parcel / right / WAITING / single
            set_courier(r, DUEL_HOST_CATEGORY_TRANSFER, 1, 1, false);
        } else if (strcmp(name, "courier-beacon") == 0) {
            // system, 1, pending -> beacon / right / WAITING / single
            set_courier(r, DUEL_HOST_CATEGORY_SYSTEM, 1, 1, false);
        } else if (strcmp(name, "courier-sentinel") == 0) {
            // persistent security, 2, old -> sentinel / left / AGING / few
            set_courier(r, DUEL_HOST_CATEGORY_SECURITY, 2, 4, true);
        } else if (strcmp(name, "courier-arriving") == 0) {
            // communication, 1, new -> messenger / left / ARRIVING / single
            set_courier(r, DUEL_HOST_CATEGORY_COMMUNICATION, 1, 0, false);
        } else if (strcmp(name, "courier-aging") == 0) {
            // transfer, 3, old -> parcel / right / AGING / few
            set_courier(r, DUEL_HOST_CATEGORY_TRANSFER, 3, 5, false);
        } else if (strcmp(name, "courier-resolving") == 0) {
            // communication, 1, dismissed -> messenger / left / RESOLVING / single
            set_courier(r, DUEL_HOST_CATEGORY_COMMUNICATION, 1, 7, false);
        } else if (strcmp(name, "courier-count-few") == 0) {
            // system, 4 -> beacon / right / WAITING / few
            set_courier(r, DUEL_HOST_CATEGORY_SYSTEM, 4, 1, false);
        } else if (strcmp(name, "courier-count-many") == 0) {
            // system, 15 -> beacon / right / WAITING / many
            set_courier(r, DUEL_HOST_CATEGORY_SYSTEM, 15, 1, false);
        } else {
            return false;
        }
        // --- rare events ---
        // Each event scenario stands the floor up (so a suppressed slot still shows
        // the room) and drives the rare-event slot through r->revision. Distinct
        // seeds keep both the floor/resident and the event art unique per scenario.
    } else if (strcmp(name, "event-scroll") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_COMMONS, DUEL_CIVIC_MODE_NORMAL,
                                   DUEL_CIVIC_INTENSITY_CALM);
        r->seed = 41;
        r->civic_phase = 8;
        r->revision = DUEL_EVENT_PACK(DUEL_CIVIC_EVENT_RUNAWAY_SCROLL,
                                      DUEL_CIVIC_EVENT_PHASE_ACTIVE, DUEL_CIVIC_EVENT_TARGET_LEFT);
    } else if (strcmp(name, "event-gear") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_WORKSHOP, DUEL_CIVIC_MODE_NORMAL,
                                   DUEL_CIVIC_INTENSITY_CALM);
        r->seed = 42;
        r->civic_phase = 8;
        r->revision = DUEL_EVENT_PACK(DUEL_CIVIC_EVENT_JAMMED_GEAR, DUEL_CIVIC_EVENT_PHASE_ACTIVE,
                                      DUEL_CIVIC_EVENT_TARGET_RIGHT);
    } else if (strcmp(name, "event-break") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_COMMONS, DUEL_CIVIC_MODE_NORMAL,
                                   DUEL_CIVIC_INTENSITY_CALM);
        r->seed = 43;
        r->civic_phase = 8;
        r->revision = DUEL_EVENT_PACK(DUEL_CIVIC_EVENT_WORK_BREAK, DUEL_CIVIC_EVENT_PHASE_ACTIVE,
                                      DUEL_CIVIC_EVENT_TARGET_LEFT);
    } else if (strcmp(name, "event-complaint") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_RESEARCH, DUEL_CIVIC_MODE_NORMAL,
                                   DUEL_CIVIC_INTENSITY_CALM);
        r->seed = 44;
        r->civic_phase = 8;
        r->revision =
            DUEL_EVENT_PACK(DUEL_CIVIC_EVENT_DAMAGE_COMPLAINT, DUEL_CIVIC_EVENT_PHASE_RESOLVING,
                            DUEL_CIVIC_EVENT_TARGET_RIGHT);
    } else if (strcmp(name, "event-courier") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_COMMONS, DUEL_CIVIC_MODE_NORMAL,
                                   DUEL_CIVIC_INTENSITY_CALM);
        r->seed = 45;
        r->civic_phase = 8;
        r->revision =
            DUEL_EVENT_PACK(DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER, DUEL_CIVIC_EVENT_PHASE_ACTIVE,
                            DUEL_CIVIC_EVENT_TARGET_SHARED);
    } else if (strcmp(name, "event-sky") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_COMMONS, DUEL_CIVIC_MODE_NORMAL,
                                   DUEL_CIVIC_INTENSITY_CALM);
        r->seed = 46;
        r->civic_phase = 8;
        r->revision = DUEL_EVENT_PACK(DUEL_CIVIC_EVENT_CIVIC_SKY, DUEL_CIVIC_EVENT_PHASE_ACTIVE,
                                      DUEL_CIVIC_EVENT_TARGET_SHARED);
    } else if (strcmp(name, "event-armed") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_COMMONS, DUEL_CIVIC_MODE_NORMAL,
                                   DUEL_CIVIC_INTENSITY_CALM);
        r->seed = 47;
        r->civic_phase = 8;
        r->revision = DUEL_EVENT_PACK(DUEL_CIVIC_EVENT_RUNAWAY_SCROLL, DUEL_CIVIC_EVENT_PHASE_ARMED,
                                      DUEL_CIVIC_EVENT_TARGET_LEFT);
    } else if (strcmp(name, "event-cooldown") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_WORKSHOP, DUEL_CIVIC_MODE_NORMAL,
                                   DUEL_CIVIC_INTENSITY_CALM);
        r->seed = 48;
        r->civic_phase = 8;
        r->revision = DUEL_EVENT_PACK(DUEL_CIVIC_EVENT_JAMMED_GEAR, DUEL_CIVIC_EVENT_PHASE_COOLDOWN,
                                      DUEL_CIVIC_EVENT_TARGET_RIGHT);
    } else if (strcmp(name, "event-quiet") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_COMMONS, DUEL_CIVIC_MODE_QUIET,
                                   DUEL_CIVIC_INTENSITY_CALM);
        r->seed = 49;
        r->civic_phase = 8;
        r->revision = DUEL_EVENT_PACK(DUEL_CIVIC_EVENT_WORK_BREAK, DUEL_CIVIC_EVENT_PHASE_ACTIVE,
                                      DUEL_CIVIC_EVENT_TARGET_LEFT);
    } else if (strcmp(name, "event-suppressed") == 0) {
        // Ineligible (a safety gate fired): the deck returns NONE and the slot
        // draws nothing; only the floor room remains. Derived, not hand-packed,
        // to exercise the real engine path.
        r->civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_COMMONS, DUEL_CIVIC_MODE_NORMAL,
                                   DUEL_CIVIC_INTENSITY_CALM);
        r->seed = 50;
        r->civic_phase = 20;
        r->revision = civic_event_revision(civic_event_derive(r->seed, r->civic_phase, false, 0));
    } else {
        return false;
    }
    /* World projection owns these bytes only while authoritative aftermath is
     * present. Preserve host-authored disposable presentation for exact current
     * courier/event scenarios; production projection still clears expired
     * aftermath normally. */
    uint8_t authored_shared_pres = r->shared_pres;
    uint8_t authored_revision = r->revision;
    duel_render_from_world(r, &w);
    if (!(r->revision & INCANTATION_AFTERMATH_WIRE)) {
        r->shared_pres = authored_shared_pres;
        r->revision = authored_revision;
    }
    r->diag_tick = (uint8_t)(w.tick % 25u);
    r->diag_overflow = w.overflow_count;
    return true;
}

void duel_scenario_render(const duel_scenario_t *scenario, uint32_t frame, duel_fb_t *left,
                          duel_fb_t *right) {
    duel_render_t render;
    duel_scenario_build(scenario, &render);
    duel_fb_clear(left);
    duel_fb_clear(right);
    duel_scene_draw(left, &render, true, frame, scenario->diagnostics);
    duel_scene_draw(right, &render, false, frame, scenario->diagnostics);
}
