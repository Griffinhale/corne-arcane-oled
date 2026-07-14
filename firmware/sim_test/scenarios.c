#include <string.h>

#include "duel_host.h"
#include "duel_resident.h"
#include "duel_sim.h"
#include "scenarios.h"

#define SCENE(name_, group_, desc_, frame_, diag_) \
    {name_, group_, desc_, frame_, diag_}

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
#ifdef ARCANE_M12
    // M12 Twin Cities canonical gallery. These entries exist only under
    // ARCANE_M12 so the accepted M11.5 visual golden (visual.hashes) is
    // untouched; they flow into visual_m12.hashes.
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
#endif
};

size_t duel_scenario_count(void) {
    return sizeof scenarios / sizeof scenarios[0];
}

const duel_scenario_t *duel_scenario_at(size_t index) {
    return index < duel_scenario_count() ? &scenarios[index] : NULL;
}

const duel_scenario_t *duel_scenario_find(const char *name) {
    for (size_t i = 0; i < duel_scenario_count(); i++)
        if (strcmp(scenarios[i].name, name) == 0) return &scenarios[i];
    return NULL;
}

static uint8_t spell_kind(uint8_t element, uint8_t modifier, uint8_t tier) {
    return DUEL_KIND_WITH_TIER(DUEL_KIND_PACK(element, modifier, PAY_IMPACT), tier);
}

static void set_life(sim_world_t *world, uint8_t life, uint8_t ticks) {
    world->wiz[SIM_SIDE_R].life = life;
    world->wiz[SIM_SIDE_R].life_ticks = ticks;
    world->wiz[SIM_SIDE_R].hp = 0;
    if (life == LIFE_REPLACE) world->wiz[SIM_SIDE_R].variant = 1;
}

static void set_alert(duel_render_t *r, uint8_t count, uint8_t category,
                      uint8_t priority, uint8_t age, bool persistent) {
    uint8_t scene = DUEL_HOST_CONTEXT_SCENE(r->external);
    r->external = DUEL_HOST_CONTEXT_PACK(1, scene, count, persistent);
    r->alert = DUEL_HOST_ALERT_PACK(category, priority, age);
}

#ifdef ARCANE_M12
// Smallest seed whose LEFT city resident carries the wanted personality, so the
// personality gallery shows all five deterministically without hand-tuning hashes.
static uint8_t seed_for_personality(uint8_t want) {
    for (int s = 0; s < 256; s++)
        if (m12_resident_personality((uint8_t)s, true) == want) return (uint8_t)s;
    return 0;
}
#endif

bool duel_scenario_build(const duel_scenario_t *scenario, duel_render_t *r) {
    if (!scenario || !r) return false;
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    *r = (duel_render_t){0};
    const char *name = scenario->name;
    uint8_t force = spell_kind(ELEM_FORCE, MOD_NONE, SPELL_TIER_MEDIUM);

    if (strncmp(name, "archive-", 8) == 0) {
        r->external = DUEL_HOST_CONTEXT_PACK(1, DUEL_HOST_SCENE_ARCHIVE, 0, false);
#ifdef ARCANE_M12
        // M12 drives the floor occupation from the civic byte, not the scene:
        // the Archive scenarios now name the Research floor explicitly.
        r->civic = DUEL_CIVIC_PACK(DUEL_M12_FLOOR_RESEARCH, DUEL_M12_MODE_NORMAL,
                                   DUEL_M12_INTENSITY_CALM);
#endif
    }

    if (strcmp(name, "duel-idle") == 0 || strcmp(name, "archive-idle") == 0) {
    } else if (strcmp(name, "pose-cast") == 0) {
        w.wiz[SIM_SIDE_L].pose = POSE_CAST;
        w.wiz[SIM_SIDE_L].cast_windup = 5;
        w.wiz[SIM_SIDE_L].cast_tier = SPELL_TIER_MEDIUM;
        w.wiz[SIM_SIDE_R].shield_ticks = SIM_SHIELD_TICKS;
    } else if (strcmp(name, "pose-recover") == 0) {
        w.wiz[SIM_SIDE_L].pose = POSE_RECOVER;
        w.wiz[SIM_SIDE_L].pose_ticks = 2;
        w.wiz[SIM_SIDE_L].variant = 1;
        w.wiz[SIM_SIDE_R].variant = 3;
    } else if (strncmp(name, "recipe-", 7) == 0) {
        uint8_t kind = strcmp(name, "recipe-force-short") == 0 ? spell_kind(ELEM_FORCE, MOD_NONE, SPELL_TIER_SHORT)
                     : strcmp(name, "recipe-ember-medium") == 0 ? spell_kind(ELEM_EMBER, MOD_NONE, SPELL_TIER_MEDIUM)
                     : strcmp(name, "recipe-frost-long") == 0 ? spell_kind(ELEM_FROST, MOD_SWIFT, SPELL_TIER_LONG)
                                                               : spell_kind(ELEM_VOID, MOD_HEAVY, SPELL_TIER_SATURATED);
        w.spell[SIM_SIDE_L] = (sim_spell_t){.active = 1, .pos = 55, .dir = 4, .kind = kind};
        w.spell[SIM_SIDE_R] = (sim_spell_t){.active = 1, .pos = 200, .dir = -4, .kind = kind};
    } else if (strcmp(name, "short-cast") == 0 || strcmp(name, "long-cast") == 0) {
        w.wiz[SIM_SIDE_L].pose = POSE_CAST;
        w.wiz[SIM_SIDE_L].cast_windup = 2;
        w.wiz[SIM_SIDE_L].cast_tier = strcmp(name, "short-cast") == 0 ? SPELL_TIER_SHORT : SPELL_TIER_LONG;
    } else if (strcmp(name, "impact") == 0 || strcmp(name, "archive-impact") == 0 || strcmp(name, "alert-impact") == 0) {
        w.wiz[SIM_SIDE_R].hp = SIM_MAX_HP - 1;
        r->flash_frames = 10; r->flash_kind = FX_IMPACT_R; r->flash_spell_kind = force;
        if (strcmp(name, "alert-impact") == 0)
            set_alert(r, 2, DUEL_HOST_CATEGORY_SYSTEM, DUEL_HOST_PRIORITY_CRITICAL, 0, false);
    } else if (strcmp(name, "deflect") == 0) {
        w.wiz[SIM_SIDE_R].shield_ticks = SIM_SHIELD_TICKS;
        r->flash_frames = 7; r->flash_kind = FX_DEFLECT_R; r->flash_spell_kind = force;
    } else if (strcmp(name, "fizzle") == 0) {
        set_life(&w, LIFE_DOWNED, SIM_DOWNED_TICKS / 2);
        r->flash_frames = 7; r->flash_kind = FX_FIZZLE_R; r->flash_spell_kind = force;
    } else if (strcmp(name, "void-pierce") == 0) {
        w.wiz[SIM_SIDE_R].shield_ticks = SIM_SHIELD_TICKS;
        w.spell[SIM_SIDE_L] = (sim_spell_t){.active = 1, .pos = 236, .dir = 4,
            .kind = spell_kind(ELEM_VOID, MOD_NONE, SPELL_TIER_LONG)};
    } else if (strcmp(name, "life-collapse") == 0) {
        set_life(&w, LIFE_COLLAPSE, SIM_COLLAPSE_TICKS / 2);
    } else if (strcmp(name, "life-downed") == 0) {
        set_life(&w, LIFE_DOWNED, SIM_DOWNED_TICKS / 2);
    } else if (strcmp(name, "life-medic") == 0 || strcmp(name, "archive-ko") == 0) {
        set_life(&w, LIFE_MEDIC, SIM_MEDIC_TICKS / 2);
    } else if (strcmp(name, "life-replace") == 0) {
        set_life(&w, LIFE_REPLACE, SIM_REPLACE_TICKS / 2);
    } else if (strcmp(name, "archive-pulse") == 0) {
        w.wiz[0].shield_ticks = w.wiz[1].shield_ticks = SIM_SHIELD_TICKS / 2;
    } else if (strcmp(name, "archive-cast") == 0) {
        w.wiz[0].pose = POSE_CAST; w.wiz[0].cast_windup = 3;
        w.wiz[0].cast_tier = SPELL_TIER_LONG;
    } else if (strcmp(name, "archive-scry") == 0) {
        w.scry.state = SCRY_ACTIVE; w.scry.scene = DUEL_HOST_SCENE_ARCHIVE;
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
    } else if (strcmp(name, "scry") == 0 || strcmp(name, "alert-under-scry") == 0 || strcmp(name, "scry-stale-diagnostics") == 0) {
        w.scry.state = SCRY_ACTIVE; w.scry.scene = DUEL_HOST_SCENE_FOCUS;
        r->layer = 3;
        r->external = DUEL_HOST_CONTEXT_PACK(1, DUEL_HOST_SCENE_FOCUS, 0, false);
        if (strcmp(name, "scry") != 0)
            set_alert(r, 3, DUEL_HOST_CATEGORY_CALENDAR, DUEL_HOST_PRIORITY_CRITICAL, 1, true);
        if (strcmp(name, "scry-stale-diagnostics") == 0) {
            r->flags |= DUEL_RENDER_STALE; w.overflow_count = 3; w.tick = 17;
        }
    } else if (strcmp(name, "stale-link") == 0) {
        r->flags |= DUEL_RENDER_STALE;
    } else if (strcmp(name, "diagnostics") == 0) {
        w.overflow_count = 3; w.tick = 17;
    } else if (strcmp(name, "archive-alert") == 0) {
        r->external = DUEL_HOST_CONTEXT_PACK(1, DUEL_HOST_SCENE_ARCHIVE, 0, false);
        w.wiz[0].shield_ticks = SIM_SHIELD_TICKS / 2;
        set_alert(r, 2, DUEL_HOST_CATEGORY_SECURITY, DUEL_HOST_PRIORITY_CRITICAL, 0, true);
        r->external = DUEL_HOST_CONTEXT_PACK(1, DUEL_HOST_SCENE_ARCHIVE,
                                             DUEL_HOST_CONTEXT_NOTIF(r->external),
                                             DUEL_HOST_CONTEXT_PERSISTENT(r->external));
#ifdef ARCANE_M12
    } else if (strcmp(name, "floor-commons") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_M12_FLOOR_COMMONS, DUEL_M12_MODE_NORMAL, DUEL_M12_INTENSITY_CALM);
        r->seed = 3; r->civic_phase = 8;
    } else if (strcmp(name, "floor-research") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_M12_FLOOR_RESEARCH, DUEL_M12_MODE_NORMAL, DUEL_M12_INTENSITY_CALM);
        r->seed = 3; r->civic_phase = 8;
    } else if (strcmp(name, "floor-workshop") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_M12_FLOOR_WORKSHOP, DUEL_M12_MODE_NORMAL, DUEL_M12_INTENSITY_CALM);
        r->seed = 3; r->civic_phase = 8;
    } else if (strcmp(name, "city-astral") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_M12_FLOOR_COMMONS, DUEL_M12_MODE_NORMAL, DUEL_M12_INTENSITY_ACTIVE);
        r->seed = 17; r->civic_phase = 72;
    } else if (strcmp(name, "city-mechanical") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_M12_FLOOR_WORKSHOP, DUEL_M12_MODE_NORMAL, DUEL_M12_INTENSITY_ACTIVE);
        r->seed = 17; r->civic_phase = 88;
    } else if (strcmp(name, "civic-quiet") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_M12_FLOOR_RESEARCH, DUEL_M12_MODE_QUIET, DUEL_M12_INTENSITY_CALM);
        r->seed = 5; r->civic_phase = 104;
    } else if (strcmp(name, "resident-diligent") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_M12_FLOOR_COMMONS, DUEL_M12_MODE_NORMAL, DUEL_M12_INTENSITY_CALM);
        r->seed = seed_for_personality(DUEL_M12_PERSONALITY_DILIGENT); r->civic_phase = 40;
    } else if (strcmp(name, "resident-curious") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_M12_FLOOR_COMMONS, DUEL_M12_MODE_NORMAL, DUEL_M12_INTENSITY_CALM);
        r->seed = seed_for_personality(DUEL_M12_PERSONALITY_CURIOUS); r->civic_phase = 40;
    } else if (strcmp(name, "resident-nervous") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_M12_FLOOR_COMMONS, DUEL_M12_MODE_NORMAL, DUEL_M12_INTENSITY_CALM);
        r->seed = seed_for_personality(DUEL_M12_PERSONALITY_NERVOUS); r->civic_phase = 40;
    } else if (strcmp(name, "resident-proud") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_M12_FLOOR_COMMONS, DUEL_M12_MODE_NORMAL, DUEL_M12_INTENSITY_CALM);
        r->seed = seed_for_personality(DUEL_M12_PERSONALITY_PROUD); r->civic_phase = 40;
    } else if (strcmp(name, "resident-distracted") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_M12_FLOOR_COMMONS, DUEL_M12_MODE_NORMAL, DUEL_M12_INTENSITY_CALM);
        r->seed = seed_for_personality(DUEL_M12_PERSONALITY_DISTRACTED); r->civic_phase = 40;
    } else if (strcmp(name, "workshop-idle") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_M12_FLOOR_WORKSHOP, DUEL_M12_MODE_NORMAL, DUEL_M12_INTENSITY_ACTIVE);
        r->seed = 9; r->civic_phase = 24;
    } else if (strcmp(name, "workshop-cast") == 0) {
        r->civic = DUEL_CIVIC_PACK(DUEL_M12_FLOOR_WORKSHOP, DUEL_M12_MODE_URGENT, DUEL_M12_INTENSITY_BUSY);
        r->seed = 9; r->civic_phase = 24;
        w.wiz[SIM_SIDE_L].pose = POSE_CAST;
        w.wiz[SIM_SIDE_L].cast_windup = 3;
        w.wiz[SIM_SIDE_L].cast_tier = SPELL_TIER_LONG;
#endif
    } else {
        return false;
    }
    duel_render_from_world(r, &w);
    r->diag_tick = (uint8_t)(w.tick % 25u);
    r->diag_overflow = w.overflow_count;
    return true;
}

void duel_scenario_render(const duel_scenario_t *scenario, uint32_t frame,
                          duel_fb_t *left, duel_fb_t *right) {
    duel_render_t render;
    duel_scenario_build(scenario, &render);
    duel_fb_clear(left);
    duel_fb_clear(right);
    wiz_draw_scene(left, &render, true, frame, scenario->diagnostics);
    wiz_draw_scene(right, &render, false, frame, scenario->diagnostics);
}
