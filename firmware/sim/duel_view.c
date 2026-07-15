#include <string.h>

#include "duel_view.h"

#ifdef ARCANE_M13

static uint8_t life_total(uint8_t life) {
    switch (life) {
        case LIFE_COLLAPSE: return SIM_COLLAPSE_TICKS;
        case LIFE_DOWNED: return SIM_DOWNED_TICKS;
        case LIFE_MEDIC: return SIM_MEDIC_TICKS;
        case LIFE_REPLACE: return SIM_REPLACE_TICKS;
        default: return 1;
    }
}

static uint8_t duration_bucket(uint8_t ticks) {
    return ticks == 0 ? 0u : ticks <= 50u ? 1u : ticks <= 100u ? 2u : 3u;
}

static uint8_t legacy_kind(uint32_t desc) {
    return DUEL_KIND_WITH_TIER(DUEL_KIND_PACK(SPELL_DESC_ELEMENT(desc), MOD_NONE,
                                               PAY_IMPACT), SPELL_DESC_MAGNITUDE(desc) - 1u);
}

void duel_view_from_world(const sim_world_t *world, duel_view_t *view) {
    memset(view, 0, sizeof *view);
    for (uint8_t side = 0; side < 2; side++) {
        const sim_wizard_t *wz = &world->wiz[side];
        view->wizard[side][0] = (uint8_t)((wz->hp & 0x0fu) |
                                  ((wz->ward_strength & 7u) << 4) |
                                  (wz->rearm_lock ? 0x80u : 0u));
        view->wizard[side][1] = (uint8_t)((wz->life & 7u) |
                                  ((wz->variant & 3u) << 3) |
                                  ((wz->status & 7u) << 5));
        view->wizard[side][2] = (uint8_t)((wz->pose & 3u) |
                                  ((wz->inc_state & 7u) << 2) |
                                  ((wz->ward_focus & 3u) << 5) |
                                  (wz->prepared ? 0x80u : 0u));
        const sim_spell_t *sp = &world->spell[side];
        if (sp->active) {
            view->spell[side][0] = (uint8_t)sp->descriptor;
            view->spell[side][1] = (uint8_t)(sp->descriptor >> 8);
            view->spell[side][2] = (uint8_t)(sp->descriptor >> 16);
            view->spell[side][3] = sp->progress;
        }
        if (wz->inc_state == INC_COLLECTING) {
            view->phase[side] = m13_complexity(&wz->inc);
        } else if (wz->inc_state == INC_WINDUP || wz->inc_state == INC_PREPARED) {
            uint32_t desc = wz->inc_state == INC_PREPARED ? wz->prepared_desc : wz->pending_desc;
            uint8_t progress = 7u;
            if (wz->inc_state == INC_WINDUP && wz->windup_total)
                progress = (uint8_t)(((uint16_t)(wz->windup_total - wz->cast_windup) * 7u) /
                                     wz->windup_total);
            view->phase[side] = (uint8_t)(SPELL_DESC_FORM(desc) |
                                  (SPELL_DESC_ELEMENT(desc) << 3) | (progress << 5));
        } else if (wz->life != LIFE_ACTIVE) {
            uint8_t total = life_total(wz->life);
            view->phase[side] = (uint8_t)(255u - ((uint16_t)wz->life_ticks * 255u / total));
        }
        view->status_visual |= (uint8_t)(((wz->status_intensity & 3u) |
                                  (duration_bucket(wz->status_ticks) << 2)) << (side * 4u));
    }
    view->fx_seq = world->fx_seq;
    view->outcome_overlay = (uint8_t)((world->fx_kind & 0x0fu) |
                              (scry_is_open(world) ? 0x10u : 0u) |
                              ((world->scry.scene & 3u) << 5));
}

duel_view_wizard_t duel_view_wizard(const duel_view_t *view, uint8_t side) {
    uint8_t b0 = view->wizard[side][0], b1 = view->wizard[side][1], b2 = view->wizard[side][2];
    uint8_t nibble = (uint8_t)(view->status_visual >> (side * 4u));
    duel_view_wizard_t wz = {
        .pose = b2 & 3u,
        .hp = b0 & 0x0fu,
        .shield_ticks = (b0 >> 4) & 7u,
        .life = b1 & 7u,
        .variant = (b1 >> 3) & 3u,
        .status = (b1 >> 5) & 7u,
        .inc_state = (b2 >> 2) & 7u,
        .ward_strength = (b0 >> 4) & 7u,
        .ward_focus = (b2 >> 5) & 3u,
        .prepared = (b2 >> 7) & 1u,
        .rearm_lock = (b0 >> 7) & 1u,
        .status_intensity = nibble & 3u,
        .status_duration = (nibble >> 2) & 3u,
    };
    wz.cast_tier = wz.ward_strength ? (uint8_t)(wz.ward_strength - 1u) : 0u;
    if (wz.inc_state == INC_WINDUP) wz.cast_windup = (uint8_t)(7u - (view->phase[side] >> 5));
    if (wz.life != LIFE_ACTIVE) {
        uint8_t total = life_total(wz.life);
        wz.life_ticks = (uint8_t)(((uint16_t)(255u - view->phase[side]) * total) / 255u);
        if (!wz.life_ticks) wz.life_ticks = 1;
    }
    return wz;
}

duel_view_spell_t duel_view_spell(const duel_view_t *view, uint8_t side) {
    uint32_t desc = (uint32_t)view->spell[side][0] |
                    ((uint32_t)view->spell[side][1] << 8) |
                    ((uint32_t)view->spell[side][2] << 16);
    uint8_t progress = view->spell[side][3];
    duel_view_spell_t spell = {
        .active = desc != 0,
        .pos = side == SIM_SIDE_L ? progress : (uint8_t)(255u - progress),
        .dir = side == SIM_SIDE_L ? 4 : -4,
        .kind = desc ? legacy_kind(desc) : 0,
        .descriptor = desc,
        .progress = progress,
    };
    return spell;
}

void duel_view_to_render_world(const duel_view_t *view, sim_world_t *world) {
    memset(world, 0, sizeof *world);
    for (uint8_t side = 0; side < 2; side++) {
        duel_view_wizard_t wz = duel_view_wizard(view, side);
        world->wiz[side].pose = wz.pose;
        world->wiz[side].hp = wz.hp;
        world->wiz[side].ward_strength = wz.ward_strength;
        world->wiz[side].shield_ticks = wz.ward_strength;
        world->wiz[side].life = wz.life;
        world->wiz[side].variant = wz.variant;
        world->wiz[side].inc_state = wz.inc_state;
        world->wiz[side].ward_focus = wz.ward_focus;
        world->wiz[side].prepared = wz.prepared;
        world->wiz[side].rearm_lock = wz.rearm_lock;
        world->wiz[side].status = wz.status;
        world->wiz[side].status_intensity = wz.status_intensity;
        duel_view_spell_t sp = duel_view_spell(view, side);
        world->spell[side].active = sp.active;
        world->spell[side].pos = sp.pos;
        world->spell[side].dir = sp.dir;
        world->spell[side].kind = sp.kind;
        world->spell[side].descriptor = sp.descriptor;
        world->spell[side].progress = sp.progress;
    }
    world->fx_seq = view->fx_seq;
    world->fx_kind = view->outcome_overlay & 0x0fu;
    world->scry.state = duel_view_scry_open(view) ? SCRY_ACTIVE : SCRY_IDLE;
    world->scry.scene = (view->outcome_overlay >> 5) & 3u;
}

bool duel_view_valid(const duel_view_t *view) {
    if (view->outcome_overlay & 0x80u) return false;
    if (((view->outcome_overlay >> 5) & 3u) >= SCRY_SCENES) return false;
    for (uint8_t side = 0; side < 2; side++) {
        uint8_t b0 = view->wizard[side][0], b1 = view->wizard[side][1], b2 = view->wizard[side][2];
        if ((b0 & 0x0fu) > SIM_MAX_HP || ((b0 >> 4) & 7u) > 4u) return false;
        if ((b1 & 7u) > LIFE_REPLACE || ((b1 >> 3) & 3u) >= SIM_ROSTER_N ||
            ((b1 >> 5) & 7u) > STATUS_MARKED) return false;
        if ((b2 & 3u) > POSE_RECOVER || ((b2 >> 2) & 7u) > INC_REARM ||
            ((b2 >> 5) & 3u) > 3u) return false;
        uint8_t inc_state = (b2 >> 2) & 7u;
        bool prepared = (b2 & 0x80u) != 0;
        if (prepared != (inc_state == INC_PREPARED)) return false;
        uint8_t status = (b1 >> 5) & 7u;
        uint8_t status_nibble = (uint8_t)(view->status_visual >> (side * 4u)) & 0x0fu;
        if ((status == STATUS_NONE) != (status_nibble == 0u)) return false;
        if (status != STATUS_NONE && (!(status_nibble & 3u) || !(status_nibble & 0x0cu))) return false;
        if ((inc_state == INC_WINDUP || inc_state == INC_PREPARED) &&
            (view->phase[side] & 7u) > SPELL_CONJURE) return false;
        uint32_t desc = (uint32_t)view->spell[side][0] |
                        ((uint32_t)view->spell[side][1] << 8) |
                        ((uint32_t)view->spell[side][2] << 16);
        uint8_t progress = view->spell[side][3];
        if (!desc) { if (progress) return false; continue; }
        if (!SPELL_DESC_VALID(desc) || (desc & 0xff000000u) ||
            SPELL_DESC_FORM(desc) > SPELL_CONJURE || SPELL_DESC_STATUS(desc) > STATUS_MARKED)
            return false;
    }
    return true;
}

#else /* ARCANE_M13 */

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

#endif /* ARCANE_M13 */
