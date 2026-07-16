#include <string.h>

#include "duel_view.h"


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

static uint8_t display_kind(uint32_t desc) {
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
            view->phase[side] = incantation_complexity(&wz->inc);
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
        .kind = desc ? display_kind(desc) : 0,
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
