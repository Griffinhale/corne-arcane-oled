/* M13 deterministic incantation compiler and combat engine.
 *
 * This translation unit is empty unless ARCANE_M13 is selected. It consumes
 * only physical positions, level masks, normalized layers, and fixed ticks;
 * keycodes and text never cross this boundary. */
#include "duel_sim.h"

#ifdef ARCANE_M13

#include <string.h>

#define FNV1A_OFFSET 2166136261u
#define FNV1A_PRIME  16777619u
#define M13_STATUS_BASE_TICKS 100u

static uint8_t sat_inc(uint8_t v) { return v == 0xffu ? v : (uint8_t)(v + 1u); }
static uint8_t min_u8(uint8_t a, uint8_t b) { return a < b ? a : b; }
static uint8_t popcount24(uint32_t v) {
    uint8_t n = 0;
    v &= 0x00ffffffu;
    while (v) { v &= v - 1u; n++; }
    return n;
}

static uint8_t gap_bucket(uint8_t ticks) {
    return ticks <= 1u ? TEMPO_FRANTIC : ticks <= 2u ? TEMPO_RAPID :
           ticks <= 4u ? TEMPO_FLOWING : TEMPO_DELIBERATE;
}

static void hash_byte(uint32_t *hash, uint8_t value) {
    *hash = (*hash ^ value) * FNV1A_PRIME;
}

static void hash_token(sim_incantation_t *inc, bool down, uint8_t pos,
                       uint8_t layer, uint8_t quantum) {
    hash_byte(&inc->hash, (uint8_t)((down ? 0x80u : 0u) | (pos & 0x1fu)));
    /* Recipe identity uses the same broad timing vocabulary exposed by the
     * descriptor. A one-tick performance wobble inside a bucket therefore
     * cannot select a completely different form. */
    hash_byte(&inc->hash, (uint8_t)(((layer & 3u) << 4) | gap_bucket(quantum)));
}

static void inc_reset(sim_incantation_t *inc) {
    memset(inc, 0, sizeof *inc);
    inc->hash = FNV1A_OFFSET;
    inc->gap_min = 0xffu;
    inc->last_pos = 0xffu;
}

uint8_t m13_complexity(const sim_incantation_t *inc) {
    uint16_t score = (uint16_t)min_u8(inc->key_count, 64u) * 2u;
    score += (uint16_t)min_u8(popcount24(inc->seen_pos), 16u) * 3u;
    score += (uint16_t)min_u8(inc->turns, 16u) * 2u;
    score += (uint16_t)min_u8(inc->layer_transitions, 8u) * 4u;
    if (inc->overlap_peak > 1u)
        score += (uint16_t)min_u8((uint8_t)(inc->overlap_peak - 1u), 4u) * 8u;
    score += (uint16_t)min_u8(inc->rhythm_changes, 8u) * 3u;
    return score > 255u ? 255u : (uint8_t)score;
}

static uint8_t dominant_row(const sim_incantation_t *inc) {
    uint8_t best = 0, best_n = 0, best_recent = 0;
    for (uint8_t row = 0; row < 4; row++) {
        if (inc->row_hist[row] > best_n ||
            (inc->row_hist[row] == best_n && inc->row_recent[row] >= best_recent)) {
            best = row;
            best_n = inc->row_hist[row];
            best_recent = inc->row_recent[row];
        }
    }
    return best;
}

static uint8_t row_element(uint8_t row) {
    static const uint8_t element[4] = { ELEM_FROST, ELEM_FORCE, ELEM_EMBER, ELEM_VOID };
    return element[row & 3u];
}

static uint8_t choose_form(uint8_t complexity, uint8_t variant, uint32_t hash) {
    uint8_t forms[8] = { SPELL_PROJECTILE, SPELL_FIREBALL, SPELL_SWARM,
                         SPELL_GROUND_WAVE, SPELL_BEAM, SPELL_CHAIN,
                         SPELL_SINGULARITY, SPELL_CONJURE };
    uint8_t weights[8] = { 5, 2, 2, 2, 2, 2, 1, 1 };
    uint8_t eligible = complexity < 64u ? 1u : complexity < 96u ? 3u :
                       complexity < 128u ? 4u : complexity < 160u ? 5u :
                       complexity < 192u ? 6u : complexity < 224u ? 7u : 8u;
    for (uint8_t i = 0; i < eligible; i++) {
        bool preferred = (variant == 0u && (forms[i] == SPELL_PROJECTILE || forms[i] == SPELL_SWARM)) ||
                         (variant == 1u && (forms[i] == SPELL_FIREBALL || forms[i] == SPELL_GROUND_WAVE)) ||
                         (variant == 2u && (forms[i] == SPELL_BEAM || forms[i] == SPELL_CHAIN)) ||
                         (variant == 3u && (forms[i] == SPELL_SINGULARITY || forms[i] == SPELL_CONJURE));
        if (preferred) weights[i] = (uint8_t)(weights[i] * 2u);
    }
    uint8_t total = 0;
    for (uint8_t i = 0; i < eligible; i++) total = (uint8_t)(total + weights[i]);
    uint32_t mixed = hash ^ (hash >> 11) ^ (hash >> 21);
    uint8_t pick = (uint8_t)(mixed % total);
    for (uint8_t i = 0; i < eligible; i++) {
        if (pick < weights[i]) return forms[i];
        pick = (uint8_t)(pick - weights[i]);
    }
    return SPELL_PROJECTILE;
}

uint32_t m13_compile(const sim_incantation_t *inc, uint8_t variant) {
    uint8_t complexity = m13_complexity(inc);
    uint8_t magnitude = complexity < 48u ? 1u : complexity < 112u ? 2u :
                        complexity < 192u ? 3u : 4u;
    uint8_t row = dominant_row(inc);
    uint8_t element = row_element(row);
    uint8_t form = choose_form(complexity, (uint8_t)(variant & 3u), inc->hash);
    bool strong_inward = inc->column_drift >= 4;
    bool held_dense = inc->key_count && inc->held_ticks >= (uint16_t)inc->key_count * 3u;
    uint8_t payload;
    if (inc->layer_transitions >= 3u) payload = PAY_HYBRID;
    else if (held_dense || inc->overlap_peak >= 3u) payload = PAY_STATUS;
    else if (strong_inward || ((inc->hash >> 17) & 7u) == 0u) payload = PAY_HEAL;
    else payload = PAY_DAMAGE;

    uint8_t trajectory;
    if (form == SPELL_FIREBALL) trajectory = TRAJ_ROOF;
    else if (form == SPELL_GROUND_WAVE) trajectory = TRAJ_GROUND;
    else if (form == SPELL_CHAIN) trajectory = TRAJ_HOMING;
    else if (form == SPELL_CONJURE)
        trajectory = (inc->hash & 1u) ? TRAJ_GROUND : TRAJ_RETURNING;
    else if (row == 3u || strong_inward) trajectory = TRAJ_RETURNING;
    else if (inc->layer_transitions >= 5u)
        trajectory = (inc->hash & 1u) ? TRAJ_AREA : TRAJ_HOMING;
    else if (row == 0u) trajectory = TRAJ_HIGH;
    else if (row == 1u) trajectory = TRAJ_MID;
    else if (complexity >= 192u && ((inc->hash >> 4) & 3u) == 0u) trajectory = TRAJ_GROUND;
    else trajectory = TRAJ_LOW;

    uint8_t interaction = form == SPELL_SINGULARITY ? INTERACT_ABSORB :
                          element == ELEM_VOID ? INTERACT_PHASE :
                          inc->layer_transitions >= 6u ? INTERACT_COMBINE : INTERACT_SOLID;
    uint8_t status = STATUS_NONE;
    if (payload == PAY_STATUS || payload == PAY_HYBRID) {
        status = element == ELEM_EMBER ? STATUS_BURNING :
                 element == ELEM_FROST ? STATUS_FROZEN :
                 element == ELEM_VOID ? STATUS_DISRUPTED : STATUS_MARKED;
    }
    uint8_t tempo = TEMPO_DELIBERATE;
    if (inc->gap_count) {
        uint8_t avg = (uint8_t)(inc->gap_sum / inc->gap_count);
        tempo = avg <= 1u ? TEMPO_FRANTIC : avg <= 2u ? TEMPO_RAPID :
                avg <= 4u ? TEMPO_FLOWING : TEMPO_DELIBERATE;
    }
    uint8_t trend = TREND_STEADY;
    if (inc->gap_count > 1u) {
        if ((uint8_t)(inc->gap_max - inc->gap_min) >= 4u) trend = TREND_IRREGULAR;
        else if (inc->last_gap < inc->first_gap) trend = TREND_ACCELERATING;
        else if (inc->last_gap > inc->first_gap) trend = TREND_DECELERATING;
    }
    return SPELL_DESC_PACK(form, element, payload, trajectory, magnitude, status,
                           interaction, tempo, trend, (inc->hash >> 22) & 3u);
}

static uint8_t desc_legacy_kind(uint32_t desc) {
    uint8_t tier = (uint8_t)(SPELL_DESC_MAGNITUDE(desc) - 1u);
    return DUEL_KIND_WITH_TIER(DUEL_KIND_PACK(SPELL_DESC_ELEMENT(desc), MOD_NONE,
                                               PAY_IMPACT), tier);
}

static uint32_t desc_set_magnitude(uint32_t desc, uint8_t magnitude) {
    return (desc & ~(3u << 10)) | ((uint32_t)(magnitude - 1u) << 10);
}

static uint8_t aftermath_duration(uint8_t kind) {
    switch (kind) {
        case AFTER_FIRE: return 175u;      /* seven seconds: panic -> response -> recovery */
        case AFTER_MAX_CAST: return 150u;  /* six-second coordinated wonder arc */
        case AFTER_REPAIR: return 125u;
        default: return 100u;
    }
}

static uint8_t aftermath_phase(const sim_aftermath_t *after) {
    if (!after->kind || !after->ticks) return 0;
    uint8_t total = aftermath_duration(after->kind);
    uint8_t elapsed = (uint8_t)(total - min_u8(after->ticks, total));
    uint16_t phase = (uint16_t)elapsed * 4u / total;
    return phase > 3u ? 3u : (uint8_t)phase;
}

static void aftermath_derive(sim_aftermath_t *after) {
    uint8_t phase = aftermath_phase(after);
    after->resident_state = RESIDENT_NORMAL;
    after->room_state = ROOM_CALM;
    after->object_state = OBJECT_NONE;
    switch (after->kind) {
        case AFTER_CHEER:
            after->resident_state = RESIDENT_CHEER;
            after->room_state = phase < 3u ? ROOM_ALERT : ROOM_RECOVERY;
            break;
        case AFTER_COMPLAINT:
            after->resident_state = RESIDENT_COMPLAIN;
            after->room_state = ROOM_ALERT;
            break;
        case AFTER_PANIC:
            after->resident_state = RESIDENT_PANIC;
            after->room_state = phase < 3u ? ROOM_DISRUPTED : ROOM_RECOVERY;
            after->object_state = phase < 3u ? OBJECT_DAMAGED : OBJECT_NONE;
            break;
        case AFTER_FIRE:
            after->resident_state = phase < 1u ? RESIDENT_PANIC :
                                    phase < 3u ? RESIDENT_FIGHT_FIRE : RESIDENT_REPAIR;
            after->room_state = phase < 3u ? ROOM_DISRUPTED : ROOM_RECOVERY;
            after->object_state = phase < 3u ? OBJECT_FIRE : OBJECT_DAMAGED;
            break;
        case AFTER_INSPECT:
            after->resident_state = RESIDENT_INSPECT;
            after->room_state = phase < 3u ? ROOM_ALERT : ROOM_RECOVERY;
            after->object_state = phase < 3u ? OBJECT_RESIDUE : OBJECT_NONE;
            break;
        case AFTER_REPAIR:
            after->resident_state = RESIDENT_REPAIR;
            after->room_state = phase < 3u ? ROOM_DISRUPTED : ROOM_RECOVERY;
            after->object_state = phase < 3u ? OBJECT_DAMAGED : OBJECT_NONE;
            break;
        case AFTER_MAX_CAST:
            after->resident_state = phase < 2u ? RESIDENT_WATCH_CAST : RESIDENT_CHEER;
            after->room_state = phase < 3u ? ROOM_ALERT : ROOM_RECOVERY;
            after->object_state = phase == 2u ? OBJECT_RESIDUE : OBJECT_NONE;
            break;
        default:
            break;
    }
}

static void aftermath_start(sim_world_t *w, uint8_t side, uint8_t kind,
                            uint8_t intensity) {
    sim_aftermath_t *after = &w->aftermath[side];
    if (after->kind && after->intensity > intensity && after->ticks > 25u) return;
    memset(after, 0, sizeof *after);
    after->kind = kind;
    after->ticks = aftermath_duration(kind);
    after->intensity = min_u8(intensity ? intensity : 1u, 4u);
    aftermath_derive(after);
}

static void aftermath_step(sim_world_t *w) {
    bool any = false, crisis = false, wonder = false;
    for (uint8_t side = 0; side < 2; side++) {
        sim_aftermath_t *after = &w->aftermath[side];
        if (after->ticks && --after->ticks == 0u) memset(after, 0, sizeof *after);
        if (!after->kind) continue;
        aftermath_derive(after);
        any = true;
        crisis |= after->kind == AFTER_FIRE || after->kind == AFTER_PANIC;
        wonder |= after->kind == AFTER_MAX_CAST;
    }
    w->world_state = crisis ? WORLD_CRISIS : wonder ? WORLD_WONDER :
                     any ? WORLD_RECOVERY : WORLD_CALM;
}

uint8_t m13_aftermath_shared(const sim_world_t *w) {
    if (!w->aftermath[0].kind && !w->aftermath[1].kind) return 0;
    return (uint8_t)((w->aftermath[0].kind & 7u) |
                     ((w->aftermath[1].kind & 7u) << 3) |
                     ((w->world_state & 3u) << 6));
}

uint8_t m13_aftermath_revision(const sim_world_t *w) {
    if (!w->aftermath[0].kind && !w->aftermath[1].kind) return 0;
    return (uint8_t)(0x80u | aftermath_phase(&w->aftermath[0]) |
                     (aftermath_phase(&w->aftermath[1]) << 2));
}

static void wizard_interrupt(sim_wizard_t *wz) {
    inc_reset(&wz->inc);
    wz->pending_desc = 0;
    wz->prepared_desc = 0;
    wz->prepared = 0;
    wz->cast_windup = 0;
    wz->windup_total = 0;
    wz->ward_strength = 0;
    wz->ward_capacity = 0;
    wz->inc_state = wz->rearm_lock ? INC_REARM : INC_IDLE;
}

static void wizard_ko(sim_world_t *w, uint8_t side) {
    sim_wizard_t *wz = &w->wiz[side];
    wz->hp = 0;
    wz->life = LIFE_COLLAPSE;
    wz->life_ticks = SIM_COLLAPSE_TICKS;
    wz->pose = POSE_IDLE;
    wz->pose_ticks = 0;
    wz->status = STATUS_NONE;
    wz->status_intensity = 0;
    wz->status_ticks = 0;
    wizard_interrupt(wz);
    w->spell[side].active = 0;
    w->spell[side].descriptor = 0;
}

static void apply_status(sim_wizard_t *wz, uint8_t status, uint8_t intensity) {
    if (status == STATUS_NONE) return;
    intensity = min_u8(intensity, 3u);
    if (intensity < wz->status_intensity) return;
    wz->status = status;
    wz->status_intensity = intensity;
    wz->status_ticks = (uint8_t)(M13_STATUS_BASE_TICKS + (uint8_t)(intensity - 1u) * 25u);
    wz->status_burned = 0;
}

static uint8_t trajectory_lane(uint8_t trajectory) {
    switch (trajectory) {
        case TRAJ_GROUND: return 0;
        case TRAJ_LOW: return 1;
        case TRAJ_MID: return 2;
        case TRAJ_HIGH: return 3;
        case TRAJ_ROOF: return 4;
        default: return 2;
    }
}

static bool ward_covers(const sim_wizard_t *wz, uint32_t desc) {
    uint8_t strength = wz->ward_strength;
    uint8_t trajectory = SPELL_DESC_TRAJECTORY(desc);
    if (wz->status == STATUS_MARKED &&
        (trajectory == TRAJ_HOMING || trajectory == TRAJ_AREA) && strength) strength--;
    if (!strength || SPELL_DESC_INTERACTION(desc) == INTERACT_PHASE) return false;
    if (strength >= 4u) return true;
    if (trajectory == TRAJ_GROUND || trajectory == TRAJ_ROOF || trajectory == TRAJ_AREA)
        return false;
    if (strength >= 3u) return true;
    uint8_t lane = trajectory_lane(trajectory);
    uint8_t focus = wz->ward_focus;
    uint8_t distance = lane > focus ? (uint8_t)(lane - focus) : (uint8_t)(focus - lane);
    return strength == 1u ? distance == 0u : distance <= 1u;
}

static void set_outcome(sim_world_t *w, uint8_t kind) {
    w->fx_kind = kind;
    w->fx_seq++;
}

static bool resolve_payload(sim_world_t *w, uint8_t caster, uint32_t desc,
                            uint8_t damage_override) {
    uint8_t opponent = (uint8_t)(caster ^ 1u);
    uint8_t payload = SPELL_DESC_PAYLOAD(desc);
    uint8_t magnitude = damage_override ? damage_override : SPELL_DESC_MAGNITUDE(desc);
    magnitude = min_u8(magnitude, 4u);
    if (payload == PAY_HEAL) {
        uint8_t target = SPELL_DESC_TRAJECTORY(desc) == TRAJ_RETURNING ? caster : opponent;
        sim_wizard_t *wz = &w->wiz[target];
        uint8_t before = wz->hp;
        uint8_t healed = (uint8_t)(wz->hp + magnitude);
        wz->hp = healed > SIM_MAX_HP ? SIM_MAX_HP : healed;
        if (wz->hp != before) {
            aftermath_start(w, target, AFTER_CHEER, magnitude);
            set_outcome(w, target == SIM_SIDE_L ? FX_HEAL_L : FX_HEAL_R);
        }
        return true;
    }

    sim_wizard_t *def = &w->wiz[opponent];
    if (def->life != LIFE_ACTIVE) {
        set_outcome(w, opponent == SIM_SIDE_L ? FX_FIZZLE_L : FX_FIZZLE_R);
        return true;
    }
    uint8_t direct = payload == PAY_DAMAGE ? magnitude :
                     payload == PAY_HYBRID ? (magnitude > 1u ? (uint8_t)(magnitude - 1u) : 1u) : 0u;
    uint8_t contact_power = direct ? direct : magnitude;
    bool shattered = false;
    if (ward_covers(def, desc)) {
        uint8_t absorbed = min_u8(def->ward_strength, contact_power);
        def->ward_strength = (uint8_t)(def->ward_strength - absorbed);
        shattered = def->ward_strength == 0u;
        if (absorbed >= contact_power) {
            set_outcome(w, shattered ?
                        (opponent == SIM_SIDE_L ? FX_WARD_SHATTER_L : FX_WARD_SHATTER_R) :
                        (opponent == SIM_SIDE_L ? FX_DEFLECT_L : FX_DEFLECT_R));
            return true;
        }
        if (direct) direct = direct > absorbed ? (uint8_t)(direct - absorbed) : (uint8_t)0;
    }

    wizard_interrupt(def);
    if (direct) {
        def->hp = direct >= def->hp ? 0u : (uint8_t)(def->hp - direct);
        def->regen_ticks = SIM_REGEN_TICKS;
    }
    if ((payload == PAY_STATUS || payload == PAY_HYBRID) && def->hp)
        apply_status(def, SPELL_DESC_STATUS(desc), magnitude);
    if (direct == 1u) aftermath_start(w, opponent, AFTER_COMPLAINT, 1u);
    else if (direct > 1u) aftermath_start(w, opponent, AFTER_PANIC, direct);
    set_outcome(w, shattered ?
                (opponent == SIM_SIDE_L ? FX_WARD_SHATTER_L : FX_WARD_SHATTER_R) :
                direct == 1u ? FX_COMPLAINT :
                (opponent == SIM_SIDE_L ? FX_IMPACT_L : FX_IMPACT_R));
    if (!def->hp) wizard_ko(w, opponent);
    return true;
}

static void spell_release(sim_world_t *w, uint8_t side, uint32_t desc) {
    sim_wizard_t *wz = &w->wiz[side];
    if (wz->status == STATUS_DISRUPTED) {
        uint8_t mag = SPELL_DESC_MAGNITUDE(desc);
        if (mag > 1u) desc = desc_set_magnitude(desc, (uint8_t)(mag - 1u));
        wz->status = STATUS_NONE;
        wz->status_intensity = 0;
        wz->status_ticks = 0;
    }
    sim_spell_t *sp = &w->spell[side];
    memset(sp, 0, sizeof *sp);
    sp->active = 1;
    sp->pos = side == SIM_SIDE_L ? SIM_SPAWN_L : SIM_SPAWN_R;
    sp->dir = side == SIM_SIDE_L ? 1 : -1;
    sp->descriptor = desc;
    sp->kind = desc_legacy_kind(desc);
    if (SPELL_DESC_FORM(desc) == SPELL_SWARM) {
        sp->aux = (uint8_t)(2u + SPELL_DESC_MAGNITUDE(desc));
        sp->progress = (uint8_t)(sp->aux << 5);
    } else if (SPELL_DESC_FORM(desc) == SPELL_CONJURE) {
        sp->aux = (uint8_t)(1u + SPELL_DESC_MAGNITUDE(desc));
        sp->progress = (uint8_t)(sp->aux << 5);
    }
    wz->pending_desc = 0;
    wz->prepared_desc = 0;
    wz->prepared = 0;
    wz->inc_state = wz->rearm_lock ? INC_REARM : INC_IDLE;
    wz->cast_windup = 0;
    wz->windup_total = 0;
    wz->ward_strength = 0;
    wz->ward_capacity = 0;
}

static uint8_t ward_capacity_for(uint8_t complexity) {
    return complexity >= 224u ? 4u : complexity >= 160u ? 3u :
           complexity >= 80u ? 2u : 1u;
}

static void ward_grow_to(sim_wizard_t *wz, uint8_t capacity) {
    if (capacity <= wz->ward_capacity) return;
    uint8_t gained = (uint8_t)(capacity - wz->ward_capacity);
    wz->ward_capacity = capacity;
    wz->ward_strength = min_u8((uint8_t)(wz->ward_strength + gained), 4u);
}

static void inc_keydown(sim_wizard_t *wz, uint8_t pos, uint8_t layer) {
    if (wz->life != LIFE_ACTIVE || wz->rearm_lock || wz->inc_state == INC_WINDUP ||
        wz->inc_state == INC_PREPARED) return;
    if (wz->inc_state == INC_IDLE) {
        inc_reset(&wz->inc);
        wz->inc_state = INC_COLLECTING;
    }
    if (wz->inc_state != INC_COLLECTING) return;
    sim_incantation_t *inc = &wz->inc;
    uint8_t row = (uint8_t)(pos / 6u);
    uint8_t col = (uint8_t)(pos % 6u);
    uint8_t gap = inc->idle;
    hash_token(inc, true, pos, layer, gap);
    inc->seen_pos |= 1u << pos;
    inc->row_hist[row] = sat_inc(inc->row_hist[row]);
    inc->newest_rank = sat_inc(inc->newest_rank);
    inc->row_recent[row] = inc->newest_rank;
    if (inc->key_count) {
        inc->transitions = sat_inc(inc->transitions);
        if (pos == inc->last_pos) inc->repetitions = sat_inc(inc->repetitions);
        int8_t delta = (int8_t)col - (int8_t)(inc->last_pos % 6u);
        int8_t direction = delta > 0 ? 1 : delta < 0 ? -1 : 0;
        if ((pos / 6u) != (inc->last_pos / 6u)) inc->turns = sat_inc(inc->turns);
        if (direction && inc->last_direction && direction != inc->last_direction)
            inc->turns = sat_inc(inc->turns);
        if (direction) inc->last_direction = direction;
        if (delta > 0 && inc->column_drift < 127 - delta) inc->column_drift += delta;
        else if (delta < 0 && inc->column_drift > -128 - delta) inc->column_drift += delta;
        if (layer != inc->last_layer) inc->layer_transitions = sat_inc(inc->layer_transitions);
        inc->gap_sum = (uint16_t)(inc->gap_sum + gap);
        if (!inc->gap_count) inc->first_gap = gap;
        uint8_t bucket = gap_bucket(gap);
        if (inc->gap_count && bucket != inc->last_gap_bucket)
            inc->rhythm_changes = sat_inc(inc->rhythm_changes);
        inc->gap_count = sat_inc(inc->gap_count);
        inc->last_gap = gap;
        inc->last_gap_bucket = bucket;
        if (gap < inc->gap_min) inc->gap_min = gap;
        if (gap > inc->gap_max) inc->gap_max = gap;
    }
    inc->last_pos = pos;
    inc->last_layer = layer;
    inc->key_count = sat_inc(inc->key_count);
    inc->idle = 0;
    inc->quiet = 0;
    ward_grow_to(wz, ward_capacity_for(m13_complexity(inc)));
    wz->ward_focus = trajectory_lane(row == 0u ? TRAJ_HIGH : row == 1u ? TRAJ_MID :
                                     row == 2u ? TRAJ_LOW : TRAJ_MID);
}

static void inc_commit(sim_world_t *w, uint8_t side, bool forced) {
    sim_wizard_t *wz = &w->wiz[side];
    uint8_t complexity = m13_complexity(&wz->inc);
    wz->pending_desc = m13_compile(&wz->inc, wz->variant);
    ward_grow_to(wz, ward_capacity_for(complexity));
    uint16_t windup = (uint16_t)M13_WINDUP_MIN_TICKS +
                      ((uint16_t)complexity * 42u + 254u) / 255u;
    if (wz->status == STATUS_FROZEN) windup += (uint16_t)wz->status_intensity * 3u;
    if (windup > M13_WINDUP_MAX_TICKS) windup = M13_WINDUP_MAX_TICKS;
    wz->cast_windup = (uint8_t)windup;
    wz->windup_total = (uint8_t)windup;
    wz->inc_state = INC_WINDUP;
    if (forced) {
        wz->rearm_lock = 1;
        /* A full ten-second incantation is a civic-scale event on both towers,
         * even before its carrier resolves in combat. */
        aftermath_start(w, side, AFTER_MAX_CAST, 4u);
        aftermath_start(w, (uint8_t)(side ^ 1u), AFTER_MAX_CAST, 3u);
    }
}

static void pose_step(sim_wizard_t *wz, bool down, bool was_down) {
    bool rising = down && !was_down;
    if (wz->life != LIFE_ACTIVE) return;
    if (wz->pose == POSE_IDLE && rising) { wz->pose = POSE_CAST; wz->pose_ticks = SIM_CAST_TICKS; }
    else if (wz->pose == POSE_CAST) {
        if (down) wz->pose_ticks = SIM_CAST_TICKS;
        else if (wz->pose_ticks && --wz->pose_ticks == 0) { wz->pose = POSE_RECOVER; wz->pose_ticks = SIM_RECOVER_TICKS; }
    } else if (wz->pose == POSE_RECOVER) {
        if (rising) { wz->pose = POSE_CAST; wz->pose_ticks = SIM_CAST_TICKS; }
        else if (wz->pose_ticks && --wz->pose_ticks == 0) wz->pose = POSE_IDLE;
    }
}

static uint8_t tempo_interval(uint32_t desc, uint8_t deliberate,
                              uint8_t flowing, uint8_t rapid, uint8_t frantic);

static uint8_t trend_flight(uint32_t desc, uint8_t linear) {
    uint16_t v = linear;
    if (SPELL_DESC_TREND(desc) == TREND_ACCELERATING)
        v = (uint16_t)linear * linear / 240u;
    else if (SPELL_DESC_TREND(desc) == TREND_DECELERATING) {
        uint16_t remain = (uint16_t)(240u - linear);
        v = 240u - remain * remain / 240u;
    } else if (SPELL_DESC_TREND(desc) == TREND_IRREGULAR && linear > 8u)
        v = (uint16_t)(linear + ((linear / 16u) & 1u ? 7u : 0u));
    return v > 240u ? 240u : (uint8_t)v;
}

static uint8_t spell_u(const sim_spell_t *sp, uint8_t side) {
    uint8_t p = sp->progress;
    uint8_t form = SPELL_DESC_FORM(sp->descriptor);
    if (form == SPELL_SWARM) {
        uint8_t phase = p & 31u;
        uint8_t interval = tempo_interval(sp->descriptor, 10u, 8u, 6u, 4u);
        p = phase < 12u ? 8u : trend_flight(sp->descriptor,
            (uint8_t)((uint16_t)(phase - 12u) * 240u / (interval - 1u)));
    }
    if (form == SPELL_CONJURE) {
        uint8_t phase = p & 31u;
        uint8_t interval = tempo_interval(sp->descriptor, 15u, 12u, 9u, 6u);
        p = SPELL_DESC_TRAJECTORY(sp->descriptor) == TRAJ_GROUND ?
            min_u8((uint8_t)(phase * 5u), 80u) :
            (phase < 10u ? 8u : trend_flight(sp->descriptor,
             (uint8_t)((uint16_t)(phase - 10u) * 240u / (interval - 1u))));
    }
    if (SPELL_DESC_TRAJECTORY(sp->descriptor) == TRAJ_RETURNING &&
        form != SPELL_CONJURE)
        p = p < 128u ? p : (uint8_t)(255u - p);
    if (SPELL_DESC_FORM(sp->descriptor) == SPELL_SINGULARITY && sp->age < 28u)
        p = 48u;
    return side == SIM_SIDE_L ? p : (uint8_t)(255u - p);
}

static bool broad_collision(uint32_t desc) {
    uint8_t form = SPELL_DESC_FORM(desc), trajectory = SPELL_DESC_TRAJECTORY(desc);
    return form == SPELL_BEAM || form == SPELL_SINGULARITY ||
           form == SPELL_CHAIN || form == SPELL_CONJURE ||
           trajectory == TRAJ_AREA || trajectory == TRAJ_HOMING;
}

static bool conjure_is_trap(uint32_t desc) {
    uint8_t trajectory = SPELL_DESC_TRAJECTORY(desc);
    return trajectory == TRAJ_GROUND || trajectory == TRAJ_AREA;
}

static uint32_t area_pulse_desc(uint32_t desc) {
    desc &= ~((uint32_t)3u << 5);   /* payload */
    desc &= ~((uint32_t)7u << 7);   /* trajectory */
    desc &= ~((uint32_t)7u << 12);  /* status */
    desc &= ~((uint32_t)3u << 15);  /* interaction */
    desc |= (uint32_t)PAY_DAMAGE << 5;
    desc |= (uint32_t)TRAJ_AREA << 7;
    desc |= (uint32_t)INTERACT_SOLID << 15;
    return desc_set_magnitude(desc, 1u);
}

static void symmetric_area_pulse(sim_world_t *w, uint32_t left,
                                 uint32_t right, uint8_t outcome) {
    /* Resolve through the ordinary area/ward path. Caster 0 hits the right
     * wizard and caster 1 hits the left; neither pulse can exceed one HP. */
    resolve_payload(w, SIM_SIDE_L, area_pulse_desc(left), 1u);
    resolve_payload(w, SIM_SIDE_R, area_pulse_desc(right), 1u);
    aftermath_start(w, SIM_SIDE_L, AFTER_PANIC, 1u);
    aftermath_start(w, SIM_SIDE_R, AFTER_PANIC, 1u);
    set_outcome(w, outcome);
}

static int8_t clash_tiebreak(uint32_t left, uint32_t right) {
    uint8_t lt = SPELL_DESC_TEMPO(left), rt = SPELL_DESC_TEMPO(right);
    if (lt != rt) return lt > rt ? 1 : -1;
    uint8_t lr = SPELL_DESC_TREND(left), rr = SPELL_DESC_TREND(right);
    if (lr != rr) return lr > rr ? 1 : -1;
    return 0;
}

static void collision_step(sim_world_t *w) {
    sim_spell_t *a = &w->spell[0], *b = &w->spell[1];
    if (!a->active || !b->active) return;
    uint32_t da = a->descriptor, db = b->descriptor;
    if (SPELL_DESC_INTERACTION(da) == INTERACT_PHASE ||
        SPELL_DESC_INTERACTION(db) == INTERACT_PHASE) return;
    if (!broad_collision(da) && !broad_collision(db) &&
        trajectory_lane(SPELL_DESC_TRAJECTORY(da)) != trajectory_lane(SPELL_DESC_TRAJECTORY(db))) return;
    uint8_t ua = spell_u(a, 0), ub = spell_u(b, 1);
    uint8_t distance = ua > ub ? (uint8_t)(ua - ub) : (uint8_t)(ub - ua);
    if (distance > 16u && SPELL_DESC_FORM(da) != SPELL_BEAM && SPELL_DESC_FORM(db) != SPELL_BEAM) return;

    uint8_t fa = SPELL_DESC_FORM(da), fb = SPELL_DESC_FORM(db);
    if (fa == SPELL_SINGULARITY || fb == SPELL_SINGULARITY) {
        sim_spell_t *sing = fa == SPELL_SINGULARITY ? a : b;
        sim_spell_t *other = fa == SPELL_SINGULARITY ? b : a;
        uint8_t oform = SPELL_DESC_FORM(other->descriptor);
        if (oform == SPELL_BEAM && SPELL_DESC_MAGNITUDE(other->descriptor) > SPELL_DESC_MAGNITUDE(sing->descriptor)) {
            sing->active = 0; sing->descriptor = 0;
            return;
        }
        if (oform != SPELL_SINGULARITY) {
            uint8_t captured = SPELL_DESC_MAGNITUDE(other->descriptor);
            if (oform == SPELL_SWARM) captured = 1;
            sing->aux = min_u8((uint8_t)(captured * 2u), 4u);
            sing->age = 20u;
            sing->descriptor = desc_set_magnitude(sing->descriptor, sing->aux);
            other->active = 0; other->descriptor = 0;
            aftermath_start(w, 0, AFTER_INSPECT, captured);
            aftermath_start(w, 1, AFTER_INSPECT, captured);
            set_outcome(w, FX_RESIDUE);
        }
        return;
    }
    if ((fa == SPELL_CONJURE && conjure_is_trap(da)) ||
        (fb == SPELL_CONJURE && conjure_is_trap(db))) {
        uint8_t trap_side = (fa == SPELL_CONJURE && conjure_is_trap(da)) ? 0u : 1u;
        sim_spell_t *trap = &w->spell[trap_side];
        sim_spell_t *other = &w->spell[trap_side ^ 1u];
        uint32_t trap_desc = trap->descriptor;
        other->active = 0; other->descriptor = 0;
        trap->active = 0; trap->descriptor = 0;
        resolve_payload(w, trap_side, trap_desc,
                        min_u8(SPELL_DESC_MAGNITUDE(trap_desc), 2u));
        aftermath_start(w, trap_side ^ 1u, AFTER_PANIC, 2u);
        set_outcome(w, FX_DETONATE);
        return;
    }
    if (fa == SPELL_BEAM || fb == SPELL_BEAM) {
        sim_spell_t *other = fa == SPELL_BEAM ? b : a;
        if (SPELL_DESC_FORM(other->descriptor) == SPELL_FIREBALL) {
            aftermath_start(w, 0, AFTER_FIRE, SPELL_DESC_MAGNITUDE(da));
            aftermath_start(w, 1, AFTER_FIRE, SPELL_DESC_MAGNITUDE(db));
            set_outcome(w, FX_DETONATE);
        }
        if (SPELL_DESC_FORM(other->descriptor) == SPELL_SWARM && other->aux > 1u) other->aux--;
        else { other->active = 0; other->descriptor = 0; }
        return;
    }
    if (fa == SPELL_CHAIN || fb == SPELL_CHAIN) {
        sim_spell_t *chain = fa == SPELL_CHAIN ? a : b;
        sim_spell_t *other = fa == SPELL_CHAIN ? b : a;
        uint8_t mag = SPELL_DESC_MAGNITUDE(chain->descriptor);
        uint8_t other_mag = SPELL_DESC_MAGNITUDE(other->descriptor);
        other->active = 0; other->descriptor = 0;
        if (other_mag >= mag && mag > 1u)
            chain->descriptor = desc_set_magnitude(chain->descriptor, (uint8_t)(mag - 1u));
        aftermath_start(w, 0, AFTER_INSPECT, mag);
        aftermath_start(w, 1, AFTER_INSPECT, mag);
        set_outcome(w, FX_RESIDUE);
        return;
    }
    if (fa == SPELL_SWARM || fb == SPELL_SWARM) {
        sim_spell_t *swarm = fa == SPELL_SWARM ? a : b;
        sim_spell_t *other = fa == SPELL_SWARM ? b : a;
        if (swarm->aux) swarm->aux--;
        if (!swarm->aux) { swarm->active = 0; swarm->descriptor = 0; }
        if (SPELL_DESC_MAGNITUDE(other->descriptor) <= 1u) { other->active = 0; other->descriptor = 0; }
        return;
    }
    uint8_t ea = SPELL_DESC_ELEMENT(da), eb = SPELL_DESC_ELEMENT(db);
    uint8_t ma = SPELL_DESC_MAGNITUDE(da), mb = SPELL_DESC_MAGNITUDE(db);
    if (ea == eb && (SPELL_DESC_INTERACTION(da) == INTERACT_COMBINE ||
                     SPELL_DESC_INTERACTION(db) == INTERACT_COMBINE ||
                     (SPELL_DESC_INTERACTION(da) == INTERACT_SOLID && SPELL_DESC_INTERACTION(db) == INTERACT_SOLID))) {
        int8_t winner = ma > mb ? 1 : ma < mb ? -1 : clash_tiebreak(da, db);
        if (!winner) {
            a->active = b->active = 0; a->descriptor = b->descriptor = 0;
            symmetric_area_pulse(w, da, db, FX_COMBINE);
            return;
        }
        uint8_t combined = min_u8((uint8_t)(ma + mb), 4u);
        if (winner > 0) {
            a->descriptor = desc_set_magnitude(da, combined);
            b->active = 0; b->descriptor = 0;
        } else {
            b->descriptor = desc_set_magnitude(db, combined);
            a->active = 0; a->descriptor = 0;
        }
        aftermath_start(w, 0, AFTER_REPAIR, ma);
        aftermath_start(w, 1, AFTER_REPAIR, mb);
        set_outcome(w, FX_COMBINE);
    } else if ((ea == ELEM_EMBER && eb == ELEM_FROST) || (ea == ELEM_FROST && eb == ELEM_EMBER)) {
        a->active = b->active = 0; a->descriptor = b->descriptor = 0;
        symmetric_area_pulse(w, da, db, FX_DETONATE);
        aftermath_start(w, 0, AFTER_FIRE, 1u);
        aftermath_start(w, 1, AFTER_FIRE, 1u);
    } else if (ma == mb) {
        a->active = b->active = 0; a->descriptor = b->descriptor = 0;
    } else if (ma > mb) {
        a->descriptor = desc_set_magnitude(da, (uint8_t)(ma - mb)); b->active = 0; b->descriptor = 0;
    } else {
        b->descriptor = desc_set_magnitude(db, (uint8_t)(mb - ma)); a->active = 0; a->descriptor = 0;
    }
}

static uint8_t motion_delta(const sim_spell_t *sp, uint8_t base) {
    int delta = base;
    switch (SPELL_DESC_TEMPO(sp->descriptor)) {
        case TEMPO_DELIBERATE: delta -= 2; break;
        case TEMPO_RAPID: delta += 2; break;
        case TEMPO_FRANTIC: delta += 4; break;
        default: break;
    }
    uint8_t trend = SPELL_DESC_TREND(sp->descriptor);
    uint8_t stage = min_u8((uint8_t)(sp->age / 8u), 3u);
    if (trend == TREND_ACCELERATING) delta += stage;
    else if (trend == TREND_DECELERATING) delta += 3 - stage;
    else if (trend == TREND_IRREGULAR) delta += (sp->age & 1u) ? 2 : -1;
    if (delta < 1) delta = 1;
    if (delta > 15) delta = 15;
    return (uint8_t)delta;
}

static uint8_t tempo_interval(uint32_t desc, uint8_t deliberate,
                              uint8_t flowing, uint8_t rapid, uint8_t frantic) {
    switch (SPELL_DESC_TEMPO(desc)) {
        case TEMPO_DELIBERATE: return deliberate;
        case TEMPO_RAPID: return rapid;
        case TEMPO_FRANTIC: return frantic;
        default: return flowing;
    }
}

static void spell_step(sim_world_t *w, uint8_t side) {
    sim_spell_t *sp = &w->spell[side];
    if (!sp->active) return;
    sim_wizard_t *caster = &w->wiz[side];
    if (caster->status == STATUS_FROZEN && (w->tick & 1u)) return;
    uint8_t form = SPELL_DESC_FORM(sp->descriptor);
    sp->age = sat_inc(sp->age);
    if (form == SPELL_PROJECTILE || form == SPELL_FIREBALL ||
        form == SPELL_GROUND_WAVE) {
        uint8_t base = form == SPELL_FIREBALL ? 7u :
                       form == SPELL_GROUND_WAVE ? 6u : 9u;
        uint16_t p = (uint16_t)sp->progress + motion_delta(sp, base);
        sp->progress = p > 255u ? 255u : (uint8_t)p;
        uint8_t flight = sp->progress;
        if (SPELL_DESC_TRAJECTORY(sp->descriptor) == TRAJ_RETURNING)
            flight = flight < 128u ? flight : (uint8_t)(255u - flight);
        sp->pos = side == SIM_SIDE_L ? flight : (uint8_t)(255u - flight);
        if (sp->progress >= 240u) {
            uint32_t desc = sp->descriptor;
            resolve_payload(w, side, desc, 0);
            if (form == SPELL_FIREBALL) {
                aftermath_start(w, side ^ 1u, AFTER_FIRE, SPELL_DESC_MAGNITUDE(desc));
                set_outcome(w, FX_DETONATE);
            } else if (SPELL_DESC_TRAJECTORY(desc) == TRAJ_AREA) {
                aftermath_start(w, side ^ 1u, AFTER_PANIC, SPELL_DESC_MAGNITUDE(desc));
            }
            sp->active = 0; sp->descriptor = 0;
        }
    } else if (form == SPELL_BEAM) {
        uint8_t build = SPELL_DESC_TEMPO(sp->descriptor) == TEMPO_DELIBERATE ? 7u :
                        SPELL_DESC_TEMPO(sp->descriptor) == TEMPO_FLOWING ? 5u :
                        SPELL_DESC_TEMPO(sp->descriptor) == TEMPO_RAPID ? 4u : 3u;
        uint8_t sustain = SPELL_DESC_TREND(sp->descriptor) == TREND_ACCELERATING ? 30u :
                          SPELL_DESC_TREND(sp->descriptor) == TREND_DECELERATING ? 20u : 25u;
        if (sp->age <= build) sp->progress = (uint8_t)((uint16_t)sp->age * 63u / build);
        else if (sp->age <= (uint8_t)(build + sustain))
            sp->progress = (uint8_t)(64u +
                (uint16_t)(sp->age - build) * 159u / sustain);
        else sp->progress = (uint8_t)(224u +
                min_u8((uint8_t)(sp->age - build - sustain), 7u) * 4u);
        if (sp->age == (uint8_t)(build + 1u) && !sp->resolved) {
            resolve_payload(w, side, sp->descriptor, 0); sp->resolved = 1;
        }
        if (sp->age >= (uint8_t)(build + sustain + 8u)) {
            sp->active = 0; sp->descriptor = 0;
        }
    } else if (form == SPELL_SINGULARITY) {
        if (!sp->aux) {
            sp->progress = sp->age < 16u ? (uint8_t)(sp->age * 8u) : (uint8_t)(128u + (sp->age - 16u) * 8u);
            if (sp->age >= 28u) {
                sp->active = 0; sp->descriptor = 0;
                aftermath_start(w, side, AFTER_INSPECT, 2u);
                set_outcome(w, FX_COLLAPSE);
            }
        } else if (sp->age < 28u) {
            sp->progress = (uint8_t)(128u + (sp->age - 20u) * 8u);
        } else {
            sp->progress = (uint8_t)(192u + min_u8((uint8_t)(sp->age - 28u), 7u) * 8u);
            if (sp->age >= 36u) { resolve_payload(w, side, sp->descriptor, 0); sp->active = 0; sp->descriptor = 0; }
        }
    } else if (form == SPELL_SWARM) {
        if (sp->age < 12u) {
            sp->progress = (uint8_t)((sp->aux << 5) | sp->age);
        } else {
            uint8_t interval = tempo_interval(sp->descriptor, 10u, 8u, 6u, 4u);
            uint8_t cycle = (uint8_t)((sp->age - 12u) % interval);
            if (cycle == 0u && sp->age > 12u && sp->aux) {
                uint8_t payload = SPELL_DESC_PAYLOAD(sp->descriptor);
                resolve_payload(w, side, sp->descriptor,
                                (payload == PAY_DAMAGE || payload == PAY_HYBRID) ? 1u : 0u);
                sp->aux--;
                sp->resolved++;
            }
            sp->progress = (uint8_t)((sp->aux << 5) | (12u + cycle));
        }
        if (!sp->aux) { sp->active = 0; sp->descriptor = 0; }
    } else if (form == SPELL_CHAIN) {
        uint8_t end = SPELL_DESC_TEMPO(sp->descriptor) >= TEMPO_RAPID ? 14u : 18u;
        uint16_t chain_progress = (uint16_t)sp->age * 16u;
        sp->progress = chain_progress > 255u ? 255u : (uint8_t)chain_progress;
        if (sp->age == 6u && !sp->resolved) {
            resolve_payload(w, side, sp->descriptor, 0);
            sp->resolved = 1;
            aftermath_start(w, side ^ 1u, AFTER_INSPECT,
                            SPELL_DESC_MAGNITUDE(sp->descriptor));
        }
        if (sp->age >= end) { sp->active = 0; sp->descriptor = 0; }
    } else if (form == SPELL_CONJURE) {
        bool trap = conjure_is_trap(sp->descriptor);
        if (trap) {
            sp->progress = min_u8(sp->age, 31u);
            if (sp->age >= 75u) {
                uint32_t desc = sp->descriptor;
                resolve_payload(w, side, desc, min_u8(SPELL_DESC_MAGNITUDE(desc), 2u));
                aftermath_start(w, side ^ 1u, AFTER_PANIC, 2u);
                set_outcome(w, FX_DETONATE);
                sp->active = 0; sp->descriptor = 0;
            }
        } else if (sp->age < 10u) {
            sp->progress = (uint8_t)((sp->aux << 5) | sp->age);
        } else {
            uint8_t interval = tempo_interval(sp->descriptor, 15u, 12u, 9u, 6u);
            uint8_t cycle = (uint8_t)((sp->age - 10u) % interval);
            if (cycle == 0u && sp->age > 10u && sp->aux) {
                uint8_t payload = SPELL_DESC_PAYLOAD(sp->descriptor);
                resolve_payload(w, side, sp->descriptor,
                                (payload == PAY_DAMAGE || payload == PAY_HYBRID) ? 1u : 0u);
                sp->aux--;
            }
            sp->progress = (uint8_t)((sp->aux << 5) | (10u + cycle));
            if (!sp->aux) { sp->active = 0; sp->descriptor = 0; }
        }
    }
}

static void status_step(sim_world_t *w, uint8_t side) {
    sim_wizard_t *wz = &w->wiz[side];
    if (!wz->status_ticks || wz->status == STATUS_NONE) return;
    if (wz->status == STATUS_BURNING && !wz->status_burned && wz->status_ticks <= 75u) {
        wz->status_burned = 1;
        if (wz->hp) {
            wz->hp--;
            wz->regen_ticks = SIM_REGEN_TICKS;
        }
        if (!wz->hp) wizard_ko(w, side); /* delayed tick deliberately does not interrupt otherwise */
    }
    if (wz->status_ticks && --wz->status_ticks == 0u) {
        wz->status = STATUS_NONE;
        wz->status_intensity = 0;
        wz->status_burned = 0;
    }
}

static void lifecycle_step(sim_wizard_t *wz) {
    if (wz->life == LIFE_ACTIVE || !wz->life_ticks || --wz->life_ticks) return;
    if (wz->life == LIFE_COLLAPSE) { wz->life = LIFE_DOWNED; wz->life_ticks = SIM_DOWNED_TICKS; }
    else if (wz->life == LIFE_DOWNED) { wz->life = LIFE_MEDIC; wz->life_ticks = SIM_MEDIC_TICKS; }
    else if (wz->life == LIFE_MEDIC) {
        wz->life = LIFE_REPLACE; wz->life_ticks = SIM_REPLACE_TICKS;
        wz->variant = (uint8_t)((wz->variant + 1u) % SIM_ROSTER_N);
    } else {
        wz->life = LIFE_ACTIVE; wz->hp = SIM_MAX_HP; wz->regen_ticks = SIM_REGEN_TICKS;
    }
}

static void scry_step_m13(sim_scry_t *sc, uint8_t mask) {
    bool l = (mask & SCRY_M_L) != 0, r = (mask & SCRY_M_R) != 0;
    bool both = l && r, any = l || r, other = (mask & SCRY_M_OTHER) != 0;
    switch (sc->state) {
        case SCRY_IDLE: if (both && !other) { sc->state = SCRY_PENDING; sc->timer = SCRY_PENDING_TICKS; } else if (any) sc->state = SCRY_FIRST_HELD; break;
        case SCRY_FIRST_HELD: if (!any) sc->state = SCRY_IDLE; else if (both && !other) { sc->state = SCRY_PENDING; sc->timer = SCRY_PENDING_TICKS; } break;
        case SCRY_PENDING: if (other) sc->state = SCRY_CANCELLED; else if (!both) sc->state = any ? SCRY_FIRST_HELD : SCRY_IDLE; else if (sc->timer && --sc->timer == 0) sc->state = SCRY_ACTIVE; break;
        case SCRY_ACTIVE: if (!both) sc->state = any ? SCRY_FIRST_HELD : SCRY_IDLE; else if (other) { sc->state = SCRY_SELECT; sc->scene = (uint8_t)((sc->scene + 1u) % SCRY_SCENES); } break;
        case SCRY_SELECT: if (!both) sc->state = any ? SCRY_FIRST_HELD : SCRY_IDLE; else if (!other) sc->state = SCRY_ACTIVE; break;
        default: if (!any) sc->state = SCRY_IDLE; break;
    }
}

void sim_tick(sim_world_t *w, sim_inputs_t in, const sim_event_t *ev, uint8_t n,
              uint8_t dropped) {
    if (dropped) {
        uint32_t sum = (uint32_t)w->overflow_count + dropped;
        w->overflow_count = sum > 0xffffu ? 0xffffu : (uint16_t)sum;
    }
    uint32_t event_down[2] = {0, 0};
    if (w->flags & SIMF_AUTHORITATIVE) {
        for (uint8_t i = 0; i < n; i++) {
            if (SIM_EV_KIND(ev[i]) != SIM_EV_KEYDOWN || SIM_EV_COL(ev[i]) >= 6u) continue;
            uint8_t side = SIM_EV_SIDE(ev[i]);
            uint8_t pos = (uint8_t)(SIM_EV_ROW(ev[i]) * 6u + SIM_EV_COL(ev[i]));
            inc_keydown(&w->wiz[side], pos, in.layer[side] & 3u);
            event_down[side] |= 1u << pos;
        }
    }

    for (uint8_t side = 0; side < 2; side++) {
        sim_wizard_t *wz = &w->wiz[side];
        uint32_t held = in.held_pos[side] & 0x00ffffffu;
        uint32_t rising = held & ~wz->prev_held & ~event_down[side];
        if (w->flags & SIMF_AUTHORITATIVE) {
            for (uint8_t pos = 0; pos < 24u; pos++)
                if (rising & (1u << pos)) inc_keydown(wz, pos, in.layer[side] & 3u);
            if (wz->inc_state == INC_COLLECTING) {
                uint32_t released = wz->prev_held & ~held;
                for (uint8_t pos = 0; pos < 24u; pos++)
                    if (released & (1u << pos)) hash_token(&wz->inc, false, pos,
                                                          wz->inc.last_layer, wz->inc.idle);
                wz->inc.elapsed = sat_inc(wz->inc.elapsed);
                wz->inc.idle = sat_inc(wz->inc.idle);
                wz->inc.quiet = held ? 0u : sat_inc(wz->inc.quiet);
                wz->inc.held_ticks = (uint16_t)(wz->inc.held_ticks + popcount24(held));
                uint8_t overlap = popcount24(held);
                if (overlap > wz->inc.overlap_peak) wz->inc.overlap_peak = overlap;
                if (wz->inc.elapsed >= M13_FORCE_COMMIT_TICKS) inc_commit(w, side, true);
                else if (wz->inc.quiet >= M13_IDLE_COMMIT_TICKS) inc_commit(w, side, false);
            }
            if (wz->rearm_lock && !held) {
                wz->rearm_lock = 0;
                if (wz->inc_state == INC_REARM) wz->inc_state = INC_IDLE;
            }
        }
        pose_step(wz, (in.down_mask & (1u << side)) != 0,
                  (w->prev_down_mask & (1u << side)) != 0);
        wz->prev_held = held;
    }

    if (w->flags & SIMF_AUTHORITATIVE) {
        for (uint8_t side = 0; side < 2; side++) lifecycle_step(&w->wiz[side]);
        /* Regeneration runs before contacts. A hit later in this tick resets
         * the full countdown, so the boundary is exactly 30 seconds after the
         * damaging event rather than one simulation quantum early. */
        for (uint8_t side = 0; side < 2; side++) {
            sim_wizard_t *wz = &w->wiz[side];
            if (wz->life == LIFE_ACTIVE && wz->hp < SIM_MAX_HP &&
                wz->regen_ticks && --wz->regen_ticks == 0u) {
                wz->hp++;
                wz->regen_ticks = SIM_REGEN_TICKS;
            }
        }
        collision_step(w);
        spell_step(w, SIM_SIDE_L);
        spell_step(w, SIM_SIDE_R);
        for (uint8_t side = 0; side < 2; side++) {
            sim_wizard_t *wz = &w->wiz[side];
            status_step(w, side);
            if (wz->inc_state == INC_WINDUP && wz->cast_windup && --wz->cast_windup == 0u) {
                if (w->spell[side].active) {
                    wz->prepared_desc = wz->pending_desc;
                    wz->pending_desc = 0;
                    wz->prepared = 1;
                    wz->inc_state = INC_PREPARED;
                } else spell_release(w, side, wz->pending_desc);
            } else if (wz->inc_state == INC_PREPARED && !w->spell[side].active)
                spell_release(w, side, wz->prepared_desc);
        }
        aftermath_step(w);
        scry_step_m13(&w->scry, in.scry_mask);
    }
    w->prev_down_mask = in.down_mask;
    w->tick++;
}

#endif /* ARCANE_M13 */
