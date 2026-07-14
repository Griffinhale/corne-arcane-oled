#include <string.h>

#include "duel_view.h"

void duel_view_from_world(const sim_world_t *world, duel_view_t *view) {
    memset(view, 0, sizeof *view);
    view->hp_pair = DUEL_HP_PAIR(world->wiz[0].hp, world->wiz[1].hp);
    view->shield_pair = DUEL_SHIELD_PAIR(world->wiz[0].shield_ticks,
                                         world->wiz[1].shield_ticks);
    for (int side = 0; side < 2; side++) {
        view->pose[side] = DUEL_POSE_PACK(world->wiz[side].pose,
                                         world->wiz[side].pose_ticks);
        view->spell_pos[side] = world->spell[side].pos;
        view->spell_kind[side] = world->spell[side].kind;
        if (world->spell[side].active)
            view->spell_state |= DUEL_SPELLSTATE_ACTIVE(side);
        if (world->spell[side].dir < 0)
            view->spell_state |= DUEL_SPELLSTATE_NEG(side);
        view->life[side] = DUEL_LIFE_PACK(world->wiz[side].life,
                                          world->wiz[side].variant);
        view->life_ticks[side] = world->wiz[side].life_ticks;
        view->charge[side] = DUEL_CHARGE_PACK(world->wiz[side].cast_windup,
                                              world->wiz[side].cast_tier);
    }
    view->fx_seq = world->fx_seq;
    view->fx_kind = world->fx_kind;
    view->scry = DUEL_SCRY_PACK(scry_is_open(world), world->scry.scene);
}

void duel_view_to_render_world(const duel_view_t *view, sim_world_t *world) {
    memset(world, 0, sizeof *world);
    for (int side = 0; side < 2; side++) {
        world->wiz[side].pose = DUEL_POSE_STATE(view->pose[side]);
        world->wiz[side].pose_ticks = DUEL_POSE_TICKS(view->pose[side]);
        world->wiz[side].hp = DUEL_HP(view->hp_pair, side);
        world->wiz[side].shield_ticks = DUEL_SHIELD(view->shield_pair, side);
        world->spell[side].pos = view->spell_pos[side];
        world->spell[side].active = (view->spell_state & DUEL_SPELLSTATE_ACTIVE(side)) != 0;
        world->spell[side].dir = (view->spell_state & DUEL_SPELLSTATE_NEG(side)) ? -4 : 4;
        world->spell[side].kind = view->spell_kind[side];
        world->wiz[side].life = DUEL_LIFE_STATE(view->life[side]);
        world->wiz[side].variant = DUEL_LIFE_VARIANT(view->life[side]);
        world->wiz[side].life_ticks = view->life_ticks[side];
        world->wiz[side].cast_windup = DUEL_CHARGE_WINDUP(view->charge[side]);
        world->wiz[side].cast_tier = DUEL_CHARGE_TIER(view->charge[side]);
    }
    world->fx_seq = view->fx_seq;
    world->fx_kind = view->fx_kind;
    world->scry.state = duel_view_scry_open(view) ? SCRY_ACTIVE : SCRY_IDLE;
    world->scry.scene = DUEL_SCRY_SCENE(view->scry);
}

bool duel_view_valid(const duel_view_t *view) {
    if (view->hp_pair & 0xC0u) return false;
    if (DUEL_HP(view->hp_pair, 0) > SIM_MAX_HP ||
        DUEL_HP(view->hp_pair, 1) > SIM_MAX_HP) return false;
    if (DUEL_SHIELD(view->shield_pair, 0) > SIM_SHIELD_TICKS ||
        DUEL_SHIELD(view->shield_pair, 1) > SIM_SHIELD_TICKS) return false;
    if (view->spell_state & 0xF0u) return false;
    if (view->fx_kind > FX_FIZZLE_R) return false;
    if (view->scry & 0xF8u || DUEL_SCRY_SCENE(view->scry) >= SCRY_SCENES) return false;
    for (int side = 0; side < 2; side++) {
        if (DUEL_POSE_STATE(view->pose[side]) > POSE_RECOVER) return false;
        if (view->life[side] & 0xC0u ||
            DUEL_LIFE_STATE(view->life[side]) > LIFE_REPLACE ||
            DUEL_LIFE_VARIANT(view->life[side]) >= SIM_ROSTER_N) return false;
        if (view->charge[side] & 0xC0u ||
            DUEL_CHARGE_WINDUP(view->charge[side]) > SIM_CAST_WINDUP_TICKS) return false;
    }
    return true;
}
