#include <string.h>

#include "duel_view.h"

static uint8_t life_total(uint8_t life) {
    switch (life) {
        case LIFE_COLLAPSE:
            return SIM_COLLAPSE_TICKS;
        case LIFE_DOWNED:
            return SIM_DOWNED_TICKS;
        case LIFE_MEDIC:
            return SIM_MEDIC_TICKS;
        case LIFE_REPLACE:
            return SIM_REPLACE_TICKS;
        default:
            return 1;
    }
}

static uint8_t duration_bucket(uint8_t ticks) {
    return ticks == 0 ? 0u : ticks <= 50u ? 1u : ticks <= 100u ? 2u : 3u;
}

static uint8_t display_kind(uint32_t desc) {
    return DUEL_KIND_WITH_TIER(DUEL_KIND_PACK(SPELL_DESC_ELEMENT(desc), MOD_NONE, PAY_IMPACT),
                               SPELL_DESC_MAGNITUDE(desc) - 1u);
}

void duel_view_from_world(const sim_world_t *world, duel_view_t *view) {
    memset(view, 0, sizeof *view);
    for (uint8_t side = 0; side < 2; side++) {
        const sim_wizard_t *wz = &world->wiz[side];
        /* MEDITATE presents the ward as 0 (matching ward_covers'
         * suppression gate) without touching the stored strength, so both
         * halves hide it and a keydown restores it instantly. */
        uint8_t ward = wz->stance == DUEL_STANCE_MEDITATE ? 0u : wz->ward_strength;
        view->wizard[side][0] = VIEW_W0_PACK(wz->hp, ward, wz->rearm_lock);
        view->wizard[side][1] = VIEW_W1_PACK(wz->life, wz->variant, wz->status);
        view->wizard[side][2] = VIEW_W2_PACK(wz->pose, wz->inc_state, wz->ward_focus, wz->prepared);
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
            view->phase[side] =
                VIEW_PHASE_PACK(SPELL_DESC_FORM(desc), SPELL_DESC_ELEMENT(desc), progress);
        } else if (wz->life != LIFE_ACTIVE) {
            uint8_t total = life_total(wz->life);
            view->phase[side] = (uint8_t)(255u - ((uint16_t)wz->life_ticks * 255u / total));
        }
        view->status_visual |=
            (uint8_t)(((wz->status_intensity & 3u) | (duration_bucket(wz->status_ticks) << 2))
                      << (side * 4u));
    }
    /* The fx sequence wears the low nibble (equality-compared, wrap at 16 is
     * ample); the high nibble carries the stances. */
    view->fx_stance = VIEW_FX_PACK(world->fx_seq, world->wiz[0].stance, world->wiz[1].stance);
    view->outcome_overlay =
        VIEW_OVERLAY_PACK(world->fx_kind, scry_is_open(world), world->scry.scene);
}

void duel_residue_pack(const sim_world_t *world, uint8_t out[2]) {
    for (uint8_t half = 0; half < 2; half++) {
        const sim_residue_t *lo = &world->residue[half * 2u];
        const sim_residue_t *hi = &world->residue[half * 2u + 1u];
        out[half] = (uint8_t)((lo->element & 3u) | ((lo->intensity & 3u) << 2) |
                              ((hi->element & 3u) << 4) | ((hi->intensity & 3u) << 6));
    }
}

duel_view_wizard_t duel_view_wizard(const duel_view_t *view, uint8_t side) {
    uint8_t b0 = view->wizard[side][0], b1 = view->wizard[side][1], b2 = view->wizard[side][2];
    uint8_t nibble = (uint8_t)(view->status_visual >> (side * 4u));
    duel_view_wizard_t wz = {
        .pose = VIEW_W2_POSE(b2),
        .hp = VIEW_W0_HP(b0),
        .life = VIEW_W1_LIFE(b1),
        .variant = VIEW_W1_VARIANT(b1),
        .status = VIEW_W1_STATUS(b1),
        .inc_state = VIEW_W2_INC(b2),
        .ward_strength = VIEW_W0_WARD(b0),
        .ward_focus = VIEW_W2_FOCUS(b2),
        .prepared = VIEW_W2_PREPARED(b2),
        .rearm_lock = VIEW_W0_REARM(b0),
        .status_intensity = nibble & 3u,
        .status_duration = (nibble >> 2) & 3u,
        .stance = VIEW_FX_STANCE(view->fx_stance, side),
    };
    wz.cast_tier = wz.ward_strength ? (uint8_t)(wz.ward_strength - 1u) : 0u;
    if (wz.inc_state == INC_WINDUP)
        wz.cast_windup = (uint8_t)(7u - VIEW_PHASE_PROGRESS(view->phase[side]));
    if (wz.life != LIFE_ACTIVE) {
        uint8_t total = life_total(wz.life);
        wz.life_ticks = (uint8_t)(((uint16_t)(255u - view->phase[side]) * total) / 255u);
        if (!wz.life_ticks)
            wz.life_ticks = 1;
    }
    return wz;
}

duel_view_spell_t duel_view_spell(const duel_view_t *view, uint8_t side) {
    uint32_t desc = (uint32_t)view->spell[side][0] | ((uint32_t)view->spell[side][1] << 8) |
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

bool duel_view_valid(const duel_view_t *view) {
    if (view->outcome_overlay & 0x80u)
        return false;
    if (VIEW_OVERLAY_SCENE(view->outcome_overlay) >= SCRY_SCENES)
        return false;
    for (uint8_t side = 0; side < 2; side++) {
        uint8_t b0 = view->wizard[side][0], b1 = view->wizard[side][1], b2 = view->wizard[side][2];
        if (VIEW_W0_HP(b0) > SIM_MAX_HP || VIEW_W0_WARD(b0) > 4u)
            return false;
        if (VIEW_W1_LIFE(b1) > LIFE_REPLACE || VIEW_W1_VARIANT(b1) >= SIM_ROSTER_N ||
            VIEW_W1_STATUS(b1) > STATUS_MARKED)
            return false;
        if (VIEW_W2_POSE(b2) > POSE_RECOVER || VIEW_W2_INC(b2) > INC_REARM)
            return false;
        /* all four ward_focus values are legal; no range check needed */
        uint8_t inc_state = VIEW_W2_INC(b2);
        bool prepared = VIEW_W2_PREPARED(b2) != 0;
        if (prepared != (inc_state == INC_PREPARED))
            return false;
        uint8_t status = VIEW_W1_STATUS(b1);
        uint8_t status_nibble = (uint8_t)(view->status_visual >> (side * 4u)) & 0x0fu;
        if ((status == STATUS_NONE) != (status_nibble == 0u))
            return false;
        if (status != STATUS_NONE && (!(status_nibble & 3u) || !(status_nibble & 0x0cu)))
            return false;
        if ((inc_state == INC_WINDUP || inc_state == INC_PREPARED) &&
            VIEW_PHASE_FORM(view->phase[side]) > SPELL_CONJURE)
            return false;
        uint32_t desc = (uint32_t)view->spell[side][0] | ((uint32_t)view->spell[side][1] << 8) |
                        ((uint32_t)view->spell[side][2] << 16);
        uint8_t progress = view->spell[side][3];
        if (!desc) {
            if (progress)
                return false;
            continue;
        }
        if (!SPELL_DESC_VALID(desc) || (desc & 0xff000000u) ||
            SPELL_DESC_FORM(desc) > SPELL_CONJURE || SPELL_DESC_STATUS(desc) > STATUS_MARKED)
            return false;
    }
    return true;
}
