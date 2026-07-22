/* Authoritative combat, lifecycle, residue, and presentation mechanics. */
#include "duel_sim_internal.h"

#include <string.h>

#define INCANTATION_STATUS_BASE_TICKS 100u
/* Burning bites once, 25 ticks (~1 s) into the status — i.e. when the
 * countdown falls to this value. Distinct from CONJURE_TRAP_FUSE_TICKS,
 * which happens to share the number 75. */
#define STATUS_BURN_AT_TICKS (INCANTATION_STATUS_BASE_TICKS - 25u)

static uint8_t sat_inc(uint8_t v) { return v == 0xffu ? v : (uint8_t)(v + 1u); }
static uint8_t min_u8(uint8_t a, uint8_t b) { return a < b ? a : b; }

/* ---- battlefield residue ------------------------------------------------
 * Deposits overwrite the zone's element (the newest event owns the mark),
 * saturate intensity at SIM_RESIDUE_MAX_INTENSITY, and restart the ~45 s
 * decay clock. Weakening (decay, repair, transmutation) steps intensity
 * down and keeps the zone canonical: empty means element 0. */
static void residue_deposit(sim_world_t *w, uint8_t zone, uint8_t element, uint8_t amount) {
    sim_residue_t *rz = &w->residue[zone];
    rz->element = (uint8_t)(element & 3u);
    rz->intensity = min_u8((uint8_t)(rz->intensity + amount), SIM_RESIDUE_MAX_INTENSITY);
    rz->decay = SIM_RESIDUE_DECAY_UNITS;
}

static void residue_weaken(sim_world_t *w, uint8_t zone) {
    sim_residue_t *rz = &w->residue[zone];
    if (!rz->intensity)
        return;
    if (--rz->intensity) {
        rz->decay = SIM_RESIDUE_DECAY_UNITS;
    } else {
        rz->element = 0;
        rz->decay = 0;
    }
}

static uint8_t residue_doorstep_zone(uint8_t side) {
    return side == SIM_SIDE_L ? SIM_RESIDUE_DOORSTEP_L : SIM_RESIDUE_DOORSTEP_R;
}

static uint8_t residue_mid_zone(uint8_t side) {
    return side == SIM_SIDE_L ? SIM_RESIDUE_MID_L : SIM_RESIDUE_MID_R;
}

static uint8_t residue_zone_for_u(uint8_t u) {
    if (u < 8u || u > 248u)
        return SIM_RESIDUE_ZONES; /* off the battlefield */
    return u < 48u    ? SIM_RESIDUE_DOORSTEP_L
           : u < 128u ? SIM_RESIDUE_MID_L
           : u < 208u ? SIM_RESIDUE_MID_R
                      : SIM_RESIDUE_DOORSTEP_R;
}

static uint8_t desc_display_kind(uint32_t desc) {
    uint8_t tier = (uint8_t)(SPELL_DESC_MAGNITUDE(desc) - 1u);
    return DUEL_KIND_WITH_TIER(DUEL_KIND_PACK(SPELL_DESC_ELEMENT(desc), MOD_NONE, PAY_IMPACT),
                               tier);
}

static uint32_t desc_set_magnitude(uint32_t desc, uint8_t magnitude) {
    return (desc & ~(3u << 10)) | ((uint32_t)(magnitude - 1u) << 10);
}

static uint32_t desc_set_trajectory(uint32_t desc, uint8_t trajectory) {
    return (desc & ~((uint32_t)7u << 7)) | ((uint32_t)(trajectory & 7u) << 7);
}

static uint32_t desc_set_element(uint32_t desc, uint8_t element) {
    return (desc & ~((uint32_t)3u << 3)) | ((uint32_t)(element & 3u) << 3);
}

static uint8_t aftermath_duration(uint8_t kind) {
    switch (kind) {
        case AFTER_FIRE:
            return SIM_AFTER_FIRE_TICKS;
        case AFTER_MAX_CAST:
            return SIM_AFTER_MAX_CAST_TICKS;
        case AFTER_REPAIR:
            return SIM_AFTER_REPAIR_TICKS;
        default:
            return SIM_AFTER_DEFAULT_TICKS;
    }
}

static uint8_t aftermath_phase(const sim_aftermath_t *after) {
    if (!after->kind || !after->ticks)
        return 0;
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
            after->resident_state = phase < 1u   ? RESIDENT_PANIC
                                    : phase < 3u ? RESIDENT_FIGHT_FIRE
                                                 : RESIDENT_REPAIR;
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

static void aftermath_start(sim_world_t *w, uint8_t side, uint8_t kind, uint8_t intensity) {
    sim_aftermath_t *after = &w->aftermath[side];
    if (after->kind && after->intensity > intensity && after->ticks > 25u)
        return;
    memset(after, 0, sizeof *after);
    after->kind = kind;
    after->ticks = aftermath_duration(kind);
    after->intensity = min_u8(intensity ? intensity : 1u, 4u);
    aftermath_derive(after);
    /* Aftermaths that visibly change the battlefield mark it. A
     * fire scorches the side's doorstep; a repair crew also sweeps it. */
    if (kind == AFTER_FIRE)
        residue_deposit(w, residue_doorstep_zone(side), ELEM_EMBER, 1u);
    else if (kind == AFTER_REPAIR)
        residue_weaken(w, residue_doorstep_zone(side));
}

void duel_combat_aftermath_step(sim_world_t *w) {
    bool any = false, crisis = false, wonder = false;
    for (uint8_t side = 0; side < 2; side++) {
        sim_aftermath_t *after = &w->aftermath[side];
        if (after->ticks && --after->ticks == 0u)
            memset(after, 0, sizeof *after);
        if (!after->kind)
            continue;
        aftermath_derive(after);
        any = true;
        crisis |= after->kind == AFTER_FIRE || after->kind == AFTER_PANIC;
        wonder |= after->kind == AFTER_MAX_CAST;
    }
    w->world_state = crisis   ? WORLD_CRISIS
                     : wonder ? WORLD_WONDER
                     : any    ? WORLD_RECOVERY
                              : WORLD_CALM;
}

uint8_t incantation_aftermath_shared(const sim_world_t *w) {
    if (!w->aftermath[0].kind && !w->aftermath[1].kind)
        return 0;
    return (uint8_t)((w->aftermath[0].kind & 7u) | ((w->aftermath[1].kind & 7u) << 3) |
                     ((w->world_state & 3u) << 6));
}

uint8_t incantation_aftermath_revision(const sim_world_t *w) {
    if (!w->aftermath[0].kind && !w->aftermath[1].kind)
        return 0;
    return (uint8_t)(0x80u | aftermath_phase(&w->aftermath[0]) |
                     (aftermath_phase(&w->aftermath[1]) << 2));
}

/* Tear down any in-flight cast: pending/prepared descriptors, windup, and
 * ward all clear together. Deliberately does NOT touch wz->inc — the
 * incantation scratchpad is hashed world state and stays stale at release
 * (wizard_interrupt resets it separately). */
static void wizard_clear_cast(sim_wizard_t *wz) {
    wz->pending_desc = 0;
    wz->prepared_desc = 0;
    wz->prepared = 0;
    wz->cast_windup = 0;
    wz->windup_total = 0;
    wz->ward_strength = 0;
    wz->ward_capacity = 0;
    wz->inc_state = wz->rearm_lock ? INC_REARM : INC_IDLE;
}

static void wizard_interrupt(sim_wizard_t *wz) {
    incantation_collection_reset(&wz->inc);
    wizard_clear_cast(wz);
    /* An interruption (damage, KO) ends any stance and forfeits an
     * unconsumed STUDY buff. */
    wz->stance = DUEL_STANCE_NONE;
    wz->stance_ticks = 0;
    wz->studied = 0;
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
    /* KO recovery: a KO steps temper one back toward neutral, so the
     * replacement arrives calmer (or steadier) than the fallen wizard. */
    if (wz->temper > SIM_TEMPER_NEUTRAL)
        wz->temper--;
    else if (wz->temper < SIM_TEMPER_NEUTRAL)
        wz->temper++;
    wizard_interrupt(wz);
    w->spell[side].active = 0;
    w->spell[side].descriptor = 0;
}

static void apply_status(sim_wizard_t *wz, uint8_t status, uint8_t intensity) {
    if (status == STATUS_NONE)
        return;
    /* Clamp both ends: intensity 0 would wrap the duration math below.
     * (All current callers pass magnitude >= 1; this guards refactors.) */
    intensity = intensity ? min_u8(intensity, 3u) : 1u;
    if (intensity < wz->status_intensity)
        return;
    wz->status = status;
    wz->status_intensity = intensity;
    wz->status_ticks = (uint8_t)(INCANTATION_STATUS_BASE_TICKS + (uint8_t)(intensity - 1u) * 25u);
    wz->status_burned = 0;
}

static uint8_t trajectory_lane(uint8_t trajectory) {
    switch (trajectory) {
        case TRAJ_GROUND:
            return 0;
        case TRAJ_LOW:
            return 1;
        case TRAJ_MID:
            return 2;
        case TRAJ_HIGH:
            return 3;
        case TRAJ_ROOF:
            return 4;
        default:
            return 2;
    }
}

static bool ward_covers(const sim_wizard_t *wz, uint32_t desc) {
    /* MEDITATE suppresses the ward as a coverage/presentation gate:
     * stored strength survives, so any keydown restores it instantly. */
    if (wz->stance == DUEL_STANCE_MEDITATE)
        return false;
    uint8_t strength = wz->ward_strength;
    uint8_t trajectory = SPELL_DESC_TRAJECTORY(desc);
    if (wz->status == STATUS_MARKED && (trajectory == TRAJ_HOMING || trajectory == TRAJ_AREA) &&
        strength)
        strength--;
    if (!strength || SPELL_DESC_INTERACTION(desc) == INTERACT_PHASE)
        return false;
    if (strength >= 4u)
        return true;
    if (trajectory == TRAJ_GROUND || trajectory == TRAJ_ROOF || trajectory == TRAJ_AREA)
        return false;
    if (strength >= 3u)
        return true;
    uint8_t lane = trajectory_lane(trajectory);
    uint8_t focus = wz->ward_focus;
    uint8_t distance = lane > focus ? (uint8_t)(lane - focus) : (uint8_t)(focus - lane);
    return strength == 1u ? distance == 0u : distance <= 1u;
}

static void set_outcome(sim_world_t *w, uint8_t kind) {
    w->fx_kind = kind;
    w->fx_seq++;
}

/* Side-specific FX kinds come in adjacent L/R pairs, so the defender's kind
 * is just the L value plus the side index. Pinned below so the enum layout
 * is an enforced contract, not a coincidence. */
_Static_assert(FX_IMPACT_R == FX_IMPACT_L + 1 && FX_DEFLECT_R == FX_DEFLECT_L + 1 &&
                   FX_FIZZLE_R == FX_FIZZLE_L + 1 && FX_HEAL_R == FX_HEAL_L + 1 &&
                   FX_WARD_SHATTER_R == FX_WARD_SHATTER_L + 1,
               "FX_* L/R pairs must stay adjacent");
static inline uint8_t fx_for(uint8_t fx_l, uint8_t side) { return (uint8_t)(fx_l + side); }

static void resolve_payload(sim_world_t *w, uint8_t caster, uint32_t desc,
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
            set_outcome(w, fx_for(FX_HEAL_L, target));
        }
        return;
    }

    sim_wizard_t *def = &w->wiz[opponent];
    if (def->life != LIFE_ACTIVE) {
        /* The spell dissipates at the downed wizard's doorstep and
         * soaks into the stones. */
        residue_deposit(w, residue_doorstep_zone(opponent), SPELL_DESC_ELEMENT(desc), 1u);
        set_outcome(w, fx_for(FX_FIZZLE_L, opponent));
        return;
    }
    uint8_t direct = payload == PAY_DAMAGE   ? magnitude
                     : payload == PAY_HYBRID ? (magnitude > 1u ? (uint8_t)(magnitude - 1u) : 1u)
                                             : 0u;
    uint8_t contact_power = direct ? direct : magnitude;
    bool shattered = false;
    if (ward_covers(def, desc)) {
        uint8_t absorbed = min_u8(def->ward_strength, contact_power);
        def->ward_strength = (uint8_t)(def->ward_strength - absorbed);
        shattered = def->ward_strength == 0u;
        if (absorbed >= contact_power) {
            /* A fully stopped spell cools its caster one step. */
            if (w->wiz[caster].temper)
                w->wiz[caster].temper--;
            set_outcome(w, fx_for(shattered ? FX_WARD_SHATTER_L : FX_DEFLECT_L, opponent));
            return;
        }
        if (direct)
            direct = direct > absorbed ? (uint8_t)(direct - absorbed) : (uint8_t)0;
    }

    wizard_interrupt(def);
    if (direct) {
        def->hp = direct >= def->hp ? 0u : (uint8_t)(def->hp - direct);
        def->regen_ticks = SIM_REGEN_TICKS;
        /* Taking damage runs the defender one step hotter. */
        if (def->temper < 7u)
            def->temper++;
        /* A landed hit stains the defender's doorstep. */
        residue_deposit(w, residue_doorstep_zone(opponent), SPELL_DESC_ELEMENT(desc), 1u);
    }
    if ((payload == PAY_STATUS || payload == PAY_HYBRID) && def->hp)
        apply_status(def, SPELL_DESC_STATUS(desc), magnitude);
    if (direct == 1u)
        aftermath_start(w, opponent, AFTER_COMPLAINT, 1u);
    else if (direct > 1u)
        aftermath_start(w, opponent, AFTER_PANIC, direct);
    set_outcome(w, shattered      ? fx_for(FX_WARD_SHATTER_L, opponent)
                   : direct == 1u ? FX_COMPLAINT
                                  : fx_for(FX_IMPACT_L, opponent));
    if (!def->hp)
        wizard_ko(w, opponent);
}

static void spell_release(sim_world_t *w, uint8_t side, uint32_t desc) {
    sim_wizard_t *wz = &w->wiz[side];
    if (wz->status == STATUS_DISRUPTED) {
        uint8_t mag = SPELL_DESC_MAGNITUDE(desc);
        if (mag > 1u)
            desc = desc_set_magnitude(desc, (uint8_t)(mag - 1u));
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
    sp->kind = desc_display_kind(desc);
    if (SPELL_DESC_FORM(desc) == SPELL_SWARM) {
        sp->aux = (uint8_t)(2u + SPELL_DESC_MAGNITUDE(desc));
        sp->progress = (uint8_t)(sp->aux << 5);
    } else if (SPELL_DESC_FORM(desc) == SPELL_CONJURE) {
        sp->aux = (uint8_t)(1u + SPELL_DESC_MAGNITUDE(desc));
        sp->progress = (uint8_t)(sp->aux << 5);
    }
    wizard_clear_cast(wz);
}

static uint8_t ward_capacity_for(uint8_t complexity) {
    return complexity >= 224u ? 4u : complexity >= 160u ? 3u : complexity >= 80u ? 2u : 1u;
}

static void ward_grow_to(sim_wizard_t *wz, uint8_t capacity) {
    if (capacity <= wz->ward_capacity)
        return;
    uint8_t gained = (uint8_t)(capacity - wz->ward_capacity);
    wz->ward_capacity = capacity;
    wz->ward_strength = min_u8((uint8_t)(wz->ward_strength + gained), 4u);
}

static void inc_keydown(sim_wizard_t *wz, uint8_t pos, uint8_t layer) {
    /* Any own keydown ends a stance instantly — exit is this byte
     * write, before every gate below, so the typing path never carries
     * stance logic. (The STUDY buff deliberately survives into the commit.) */
    wz->stance = DUEL_STANCE_NONE;
    wz->stance_ticks = 0;
    if (wz->life != LIFE_ACTIVE || wz->rearm_lock || wz->inc_state == INC_WINDUP ||
        wz->inc_state == INC_PREPARED)
        return;
    if (wz->inc_state == INC_IDLE) {
        incantation_collection_reset(&wz->inc);
        wz->inc_state = INC_COLLECTING;
    }
    if (wz->inc_state != INC_COLLECTING)
        return;
    uint8_t row = incantation_collection_keydown(&wz->inc, pos, layer);
    ward_grow_to(wz, ward_capacity_for(incantation_complexity(&wz->inc)));
    /* Ward focus by physical row: top row guards the HIGH lane down to the
     * bottom finger row guarding LOW; the thumb row (3) deliberately aliases
     * to the MID lane. (Same values as trajectory_lane(TRAJ_HIGH/MID/LOW).) */
    static const uint8_t focus_for_row[4] = {3u, 2u, 1u, 2u};
    wz->ward_focus = focus_for_row[row & 3u];
}

static void inc_commit(sim_world_t *w, uint8_t side, bool forced) {
    sim_wizard_t *wz = &w->wiz[side];
    uint8_t complexity = incantation_complexity(&wz->inc);
    wz->pending_desc = incantation_compile(&wz->inc, wz->variant, wz->temper);
    /* STUDY: the pending buff shifts this cast's element to the
     * doctrine affinity, or deepens the cast (+1 magnitude, cap 4) when the
     * element is already aligned. Consumed exactly once, here. */
    if (wz->studied) {
        wz->studied = 0;
        uint32_t desc = wz->pending_desc;
        uint8_t affinity = duel_incantation_affinity_element(wz->variant);
        if (SPELL_DESC_ELEMENT(desc) != affinity) {
            desc = desc_set_element(desc, affinity);
        } else if (SPELL_DESC_MAGNITUDE(desc) < 4u) {
            desc = desc_set_magnitude(desc, (uint8_t)(SPELL_DESC_MAGNITUDE(desc) + 1u));
        }
        wz->pending_desc = desc;
    }
    ward_grow_to(wz, ward_capacity_for(complexity));
    uint16_t windup =
        (uint16_t)INCANTATION_WINDUP_MIN_TICKS + ((uint16_t)complexity * 42u + 254u) / 255u;
    if (wz->status == STATUS_FROZEN)
        windup += (uint16_t)wz->status_intensity * 3u;
    /* Temperament: hot wizards wind up 2 ticks faster, cool ones 2
     * slower, always inside the existing clamps. */
    if (wz->temper >= 6u)
        windup = windup > INCANTATION_WINDUP_MIN_TICKS + 2u ? (uint16_t)(windup - 2u)
                                                            : INCANTATION_WINDUP_MIN_TICKS;
    else if (wz->temper <= 2u)
        windup += 2u;
    if (windup > INCANTATION_WINDUP_MAX_TICKS)
        windup = INCANTATION_WINDUP_MAX_TICKS;
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

void duel_combat_pose_step(sim_wizard_t *wz, bool down, bool was_down) {
    bool rising = down && !was_down;
    if (wz->life != LIFE_ACTIVE)
        return;
    if (wz->pose == POSE_IDLE && rising) {
        wz->pose = POSE_CAST;
        wz->pose_ticks = SIM_CAST_TICKS;
    } else if (wz->pose == POSE_CAST) {
        if (down)
            wz->pose_ticks = SIM_CAST_TICKS;
        else if (wz->pose_ticks && --wz->pose_ticks == 0) {
            wz->pose = POSE_RECOVER;
            wz->pose_ticks = SIM_RECOVER_TICKS;
        }
    } else if (wz->pose == POSE_RECOVER) {
        if (rising) {
            wz->pose = POSE_CAST;
            wz->pose_ticks = SIM_CAST_TICKS;
        } else if (wz->pose_ticks && --wz->pose_ticks == 0)
            wz->pose = POSE_IDLE;
    }
}

static uint8_t tempo_interval(uint32_t desc, uint8_t deliberate, uint8_t flowing, uint8_t rapid,
                              uint8_t frantic);

/* Swarm/conjure pulse cadence, shared by the encoder (spell_step packs
 * progress = (aux<<5) | (prelude + cycle)) and the decoder (spell_u maps
 * phase - prelude onto 0..240 via interval - 1). Both sides MUST read these
 * helpers: if the tables drift apart, the decode exceeds 240 and wraps,
 * corrupting collision distances with no compile-time signal. */
#define SWARM_PRELUDE_TICKS   12u
#define CONJURE_PRELUDE_TICKS 10u

/* Named thresholds for the flight timelines (shared by the spell_u decoder
 * and the per-form steppers). SPELL_PROGRESS_IMPACT is the ballistic contact
 * point at the defender; SPELL_PROGRESS_APEX folds a RETURNING trajectory
 * back toward its caster. */
#define SPELL_PROGRESS_IMPACT      240u
#define SPELL_PROGRESS_APEX        128u
#define CONJURE_TRAP_FUSE_TICKS    75u
#define SINGULARITY_GROW_TICKS     16u
#define SINGULARITY_CHARGED_AGE    20u /* collision capture rewinds age here */
#define SINGULARITY_COLLAPSE_TICKS 28u
#define SINGULARITY_RESOLVE_TICKS  36u
static uint8_t swarm_interval(uint32_t desc) { return tempo_interval(desc, 10u, 8u, 6u, 4u); }
static uint8_t conjure_interval(uint32_t desc) { return tempo_interval(desc, 15u, 12u, 9u, 6u); }

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
        uint8_t interval = swarm_interval(sp->descriptor);
        p = phase < SWARM_PRELUDE_TICKS
                ? 8u
                : trend_flight(sp->descriptor, (uint8_t)((uint16_t)(phase - SWARM_PRELUDE_TICKS) *
                                                         240u / (interval - 1u)));
    }
    if (form == SPELL_CONJURE) {
        uint8_t phase = p & 31u;
        uint8_t interval = conjure_interval(sp->descriptor);
        p = SPELL_DESC_TRAJECTORY(sp->descriptor) == TRAJ_GROUND
                ? min_u8((uint8_t)(phase * 5u), 80u)
                : (phase < CONJURE_PRELUDE_TICKS
                       ? 8u
                       : trend_flight(sp->descriptor,
                                      (uint8_t)((uint16_t)(phase - CONJURE_PRELUDE_TICKS) * 240u /
                                                (interval - 1u))));
    }
    if (SPELL_DESC_TRAJECTORY(sp->descriptor) == TRAJ_RETURNING && form != SPELL_CONJURE)
        p = p < SPELL_PROGRESS_APEX ? p : (uint8_t)(255u - p);
    if (SPELL_DESC_FORM(sp->descriptor) == SPELL_SINGULARITY &&
        sp->age < SINGULARITY_COLLAPSE_TICKS)
        p = 48u;
    return side == SIM_SIDE_L ? p : (uint8_t)(255u - p);
}

static bool broad_collision(uint32_t desc) {
    uint8_t form = SPELL_DESC_FORM(desc), trajectory = SPELL_DESC_TRAJECTORY(desc);
    return form == SPELL_BEAM || form == SPELL_SINGULARITY || form == SPELL_CHAIN ||
           form == SPELL_CONJURE || trajectory == TRAJ_AREA || trajectory == TRAJ_HOMING;
}

static bool conjure_is_trap(uint32_t desc) {
    uint8_t trajectory = SPELL_DESC_TRAJECTORY(desc);
    return trajectory == TRAJ_GROUND || trajectory == TRAJ_AREA;
}

static uint32_t area_pulse_desc(uint32_t desc) {
    desc &= ~((uint32_t)3u << 5);  /* payload */
    desc &= ~((uint32_t)7u << 7);  /* trajectory */
    desc &= ~((uint32_t)7u << 12); /* status */
    desc &= ~((uint32_t)3u << 15); /* interaction */
    desc |= (uint32_t)PAY_DAMAGE << 5;
    desc |= (uint32_t)TRAJ_AREA << 7;
    desc |= (uint32_t)INTERACT_SOLID << 15;
    return desc_set_magnitude(desc, 1u);
}

static void symmetric_area_pulse(sim_world_t *w, uint32_t left, uint32_t right, uint8_t outcome) {
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
    if (lt != rt)
        return lt > rt ? 1 : -1;
    uint8_t lr = SPELL_DESC_TREND(left), rr = SPELL_DESC_TREND(right);
    if (lr != rr)
        return lr > rr ? 1 : -1;
    return 0;
}

static void spell_despawn(sim_spell_t *sp) {
    sp->active = 0;
    sp->descriptor = 0;
}

/* Mirror matches (both spells carrying the priority form) resolve
 * symmetrically: magnitude first, then the same tempo/trend tiebreak as the
 * elemental clash, with a dead tie annihilating both. Historically the left
 * spell silently won these. */

// A stronger beam pops the singularity; anything else (except another
// singularity) is captured, feeding its magnitude into the charge.
static void collide_singularity(sim_world_t *w, sim_spell_t *sing, sim_spell_t *other) {
    uint8_t oform = SPELL_DESC_FORM(other->descriptor);
    if (oform == SPELL_BEAM &&
        SPELL_DESC_MAGNITUDE(other->descriptor) > SPELL_DESC_MAGNITUDE(sing->descriptor)) {
        spell_despawn(sing);
        return;
    }
    if (oform != SPELL_SINGULARITY) {
        uint8_t captured = SPELL_DESC_MAGNITUDE(other->descriptor);
        if (oform == SPELL_SWARM)
            captured = 1;
        sing->aux = min_u8((uint8_t)(captured * 2u), 4u);
        sing->age = SINGULARITY_CHARGED_AGE;
        sing->descriptor = desc_set_magnitude(sing->descriptor, sing->aux);
        spell_despawn(other);
        aftermath_start(w, 0, AFTER_INSPECT, captured);
        aftermath_start(w, 1, AFTER_INSPECT, captured);
        set_outcome(w, FX_RESIDUE);
    }
}

// Anything touching a set trap detonates it against the trap's own target.
static void collide_trap(sim_world_t *w, uint8_t trap_side) {
    sim_spell_t *trap = &w->spell[trap_side];
    sim_spell_t *other = &w->spell[trap_side ^ 1u];
    uint32_t trap_desc = trap->descriptor;
    spell_despawn(other);
    spell_despawn(trap);
    resolve_payload(w, trap_side, trap_desc, min_u8(SPELL_DESC_MAGNITUDE(trap_desc), 2u));
    aftermath_start(w, trap_side ^ 1u, AFTER_PANIC, 2u);
    set_outcome(w, FX_DETONATE);
}

/* Winner of a same-form mirror duel: +1 left survives, -1 right survives,
 * 0 mutual annihilation. Magnitude decides; equal magnitudes fall through to
 * the shared tempo/trend tiebreak. */
static int8_t mirror_winner(uint32_t da, uint32_t db) {
    uint8_t ma = SPELL_DESC_MAGNITUDE(da), mb = SPELL_DESC_MAGNITUDE(db);
    if (ma != mb)
        return ma > mb ? 1 : -1;
    return clash_tiebreak(da, db);
}

// Crossed beams: the stronger (or better-paced) beam burns through; a dead
// tie annihilates both mid-gap.
static void collide_mirror_beams(sim_spell_t *a, sim_spell_t *b, uint32_t da, uint32_t db) {
    int8_t winner = mirror_winner(da, db);
    if (winner >= 0)
        spell_despawn(b);
    if (winner <= 0)
        spell_despawn(a);
}

// Crossed chains: the losing chain is consumed like any other victim, and an
// equal-magnitude survivor weakens one step (the ordinary chain toll). A dead
// tie consumes both.
static void collide_mirror_chains(sim_world_t *w, sim_spell_t *a, sim_spell_t *b, uint32_t da,
                                  uint32_t db) {
    uint8_t ma = SPELL_DESC_MAGNITUDE(da), mb = SPELL_DESC_MAGNITUDE(db);
    int8_t winner = mirror_winner(da, db);
    if (winner == 0) {
        spell_despawn(a);
        spell_despawn(b);
    } else {
        sim_spell_t *survivor = winner > 0 ? a : b;
        uint8_t mag = winner > 0 ? ma : mb;
        spell_despawn(winner > 0 ? b : a);
        if (ma == mb && mag > 1u)
            survivor->descriptor = desc_set_magnitude(survivor->descriptor, (uint8_t)(mag - 1u));
    }
    aftermath_start(w, 0, AFTER_INSPECT, ma);
    aftermath_start(w, 1, AFTER_INSPECT, mb);
    set_outcome(w, FX_RESIDUE);
}

// Crossed swarms trade one mote each per contact tick; each side dies when
// its own motes run out. (Historically the left swarm bled motes while the
// right one survived untouched.)
static void collide_mirror_swarms(sim_spell_t *a, sim_spell_t *b) {
    if (a->aux)
        a->aux--;
    if (b->aux)
        b->aux--;
    if (!a->aux)
        spell_despawn(a);
    if (!b->aux)
        spell_despawn(b);
}

// A beam burns through whatever crosses it; a fireball detonates in the
// crossing, and a swarm merely loses one mote per pass.
static void collide_beam(sim_world_t *w, sim_spell_t *other, uint32_t da, uint32_t db) {
    if (SPELL_DESC_FORM(other->descriptor) == SPELL_FIREBALL) {
        aftermath_start(w, 0, AFTER_FIRE, SPELL_DESC_MAGNITUDE(da));
        aftermath_start(w, 1, AFTER_FIRE, SPELL_DESC_MAGNITUDE(db));
        set_outcome(w, FX_DETONATE);
    }
    if (SPELL_DESC_FORM(other->descriptor) == SPELL_SWARM && other->aux > 1u)
        other->aux--;
    else
        spell_despawn(other);
}

// A chain consumes the crossing spell; an equal-or-stronger victim weakens
// the chain by one magnitude step.
static void collide_chain(sim_world_t *w, sim_spell_t *chain, sim_spell_t *other) {
    uint8_t mag = SPELL_DESC_MAGNITUDE(chain->descriptor);
    uint8_t other_mag = SPELL_DESC_MAGNITUDE(other->descriptor);
    spell_despawn(other);
    if (other_mag >= mag && mag > 1u)
        chain->descriptor = desc_set_magnitude(chain->descriptor, (uint8_t)(mag - 1u));
    aftermath_start(w, 0, AFTER_INSPECT, mag);
    aftermath_start(w, 1, AFTER_INSPECT, mag);
    set_outcome(w, FX_RESIDUE);
}

// A swarm trades one mote; only a magnitude-1 carrier dies in the exchange.
static void collide_swarm(sim_spell_t *swarm, sim_spell_t *other) {
    if (swarm->aux)
        swarm->aux--;
    if (!swarm->aux)
        spell_despawn(swarm);
    if (SPELL_DESC_MAGNITUDE(other->descriptor) <= 1u)
        spell_despawn(other);
}

// Elemental clash between two ordinary carriers: same element combines (or
// annihilates symmetrically on a dead tie), ember/frost detonates, otherwise
// plain magnitude attrition.
static void collide_clash(sim_world_t *w, sim_spell_t *a, sim_spell_t *b, uint32_t da,
                          uint32_t db) {
    uint8_t ea = SPELL_DESC_ELEMENT(da), eb = SPELL_DESC_ELEMENT(db);
    uint8_t ma = SPELL_DESC_MAGNITUDE(da), mb = SPELL_DESC_MAGNITUDE(db);
    if (ea == eb && (SPELL_DESC_INTERACTION(da) == INTERACT_COMBINE ||
                     SPELL_DESC_INTERACTION(db) == INTERACT_COMBINE ||
                     (SPELL_DESC_INTERACTION(da) == INTERACT_SOLID &&
                      SPELL_DESC_INTERACTION(db) == INTERACT_SOLID))) {
        int8_t winner = ma > mb ? 1 : ma < mb ? -1 : clash_tiebreak(da, db);
        if (!winner) {
            spell_despawn(a);
            spell_despawn(b);
            symmetric_area_pulse(w, da, db, FX_COMBINE);
            return;
        }
        uint8_t combined = min_u8((uint8_t)(ma + mb), 4u);
        if (winner > 0) {
            a->descriptor = desc_set_magnitude(da, combined);
            spell_despawn(b);
        } else {
            b->descriptor = desc_set_magnitude(db, combined);
            spell_despawn(a);
        }
        aftermath_start(w, 0, AFTER_REPAIR, ma);
        aftermath_start(w, 1, AFTER_REPAIR, mb);
        set_outcome(w, FX_COMBINE);
    } else if ((ea == ELEM_EMBER && eb == ELEM_FROST) || (ea == ELEM_FROST && eb == ELEM_EMBER)) {
        spell_despawn(a);
        spell_despawn(b);
        symmetric_area_pulse(w, da, db, FX_DETONATE);
        aftermath_start(w, 0, AFTER_FIRE, 1u);
        aftermath_start(w, 1, AFTER_FIRE, 1u);
        /* The mid-gap detonation showers both mid zones, each with
         * its own caster's element. */
        residue_deposit(w, SIM_RESIDUE_MID_L, ea, 1u);
        residue_deposit(w, SIM_RESIDUE_MID_R, eb, 1u);
    } else if (ma == mb) {
        spell_despawn(a);
        spell_despawn(b);
    } else if (ma > mb) {
        a->descriptor = desc_set_magnitude(da, (uint8_t)(ma - mb));
        spell_despawn(b);
    } else {
        b->descriptor = desc_set_magnitude(db, (uint8_t)(mb - ma));
        spell_despawn(a);
    }
}

/* Form precedence when both slots are live and in contact, first match wins:
 * singularity > set trap > beam > chain > swarm > elemental clash. The order
 * of this ladder is hash-pinned — reordering it changes duel outcomes. */
void duel_combat_collision_step(sim_world_t *w) {
    sim_spell_t *a = &w->spell[0], *b = &w->spell[1];
    if (!a->active || !b->active)
        return;
    uint32_t da = a->descriptor, db = b->descriptor;
    if (SPELL_DESC_INTERACTION(da) == INTERACT_PHASE ||
        SPELL_DESC_INTERACTION(db) == INTERACT_PHASE)
        return;
    if (!broad_collision(da) && !broad_collision(db) &&
        trajectory_lane(SPELL_DESC_TRAJECTORY(da)) != trajectory_lane(SPELL_DESC_TRAJECTORY(db)))
        return;
    uint8_t ua = spell_u(a, 0), ub = spell_u(b, 1);
    uint8_t distance = ua > ub ? (uint8_t)(ua - ub) : (uint8_t)(ub - ua);
    if (distance > 16u && SPELL_DESC_FORM(da) != SPELL_BEAM && SPELL_DESC_FORM(db) != SPELL_BEAM)
        return;

    uint8_t fa = SPELL_DESC_FORM(da), fb = SPELL_DESC_FORM(db);
    if (fa == SPELL_SINGULARITY || fb == SPELL_SINGULARITY) {
        /* singularity-vs-singularity is deliberately inert (both persist),
         * so the mirror case needs no special handling. */
        collide_singularity(w, fa == SPELL_SINGULARITY ? a : b, fa == SPELL_SINGULARITY ? b : a);
    } else if ((fa == SPELL_CONJURE && conjure_is_trap(da)) ||
               (fb == SPELL_CONJURE && conjure_is_trap(db))) {
        /* trap-vs-trap is unreachable: each trap sits at its caster's own
         * doorstep, so the distance gate above never lets them touch. */
        collide_trap(w, (fa == SPELL_CONJURE && conjure_is_trap(da)) ? 0u : 1u);
    } else if (fa == SPELL_BEAM && fb == SPELL_BEAM) {
        collide_mirror_beams(a, b, da, db);
    } else if (fa == SPELL_BEAM || fb == SPELL_BEAM) {
        collide_beam(w, fa == SPELL_BEAM ? b : a, da, db);
    } else if (fa == SPELL_CHAIN && fb == SPELL_CHAIN) {
        collide_mirror_chains(w, a, b, da, db);
    } else if (fa == SPELL_CHAIN || fb == SPELL_CHAIN) {
        collide_chain(w, fa == SPELL_CHAIN ? a : b, fa == SPELL_CHAIN ? b : a);
    } else if (fa == SPELL_SWARM && fb == SPELL_SWARM) {
        collide_mirror_swarms(a, b);
    } else if (fa == SPELL_SWARM || fb == SPELL_SWARM) {
        collide_swarm(fa == SPELL_SWARM ? a : b, fa == SPELL_SWARM ? b : a);
    } else {
        collide_clash(w, a, b, da, db);
    }
}

static uint8_t motion_delta(const sim_spell_t *sp, uint8_t base) {
    int delta = base;
    switch (SPELL_DESC_TEMPO(sp->descriptor)) {
        case TEMPO_DELIBERATE:
            delta -= 2;
            break;
        case TEMPO_RAPID:
            delta += 2;
            break;
        case TEMPO_FRANTIC:
            delta += 4;
            break;
        default:
            break;
    }
    uint8_t trend = SPELL_DESC_TREND(sp->descriptor);
    uint8_t stage = min_u8((uint8_t)(sp->age / 8u), 3u);
    if (trend == TREND_ACCELERATING)
        delta += stage;
    else if (trend == TREND_DECELERATING)
        delta += 3 - stage;
    else if (trend == TREND_IRREGULAR)
        delta += (sp->age & 1u) ? 2 : -1;
    if (delta < 1)
        delta = 1;
    if (delta > 15)
        delta = 15;
    return (uint8_t)delta;
}

static uint8_t tempo_interval(uint32_t desc, uint8_t deliberate, uint8_t flowing, uint8_t rapid,
                              uint8_t frantic) {
    switch (SPELL_DESC_TEMPO(desc)) {
        case TEMPO_DELIBERATE:
            return deliberate;
        case TEMPO_RAPID:
            return rapid;
        case TEMPO_FRANTIC:
            return frantic;
        default:
            return flowing;
    }
}

// Simple carriers: tempo/trend-modulated flight to the impact threshold.
// A fireball detonates on arrival; an area carrier panics the room.
static void step_ballistic(sim_world_t *w, uint8_t side, sim_spell_t *sp, uint8_t form) {
    uint8_t base = form == SPELL_FIREBALL ? 7u : form == SPELL_GROUND_WAVE ? 6u : 9u;
    uint16_t p = (uint16_t)sp->progress + motion_delta(sp, base);
    sp->progress = p > 255u ? 255u : (uint8_t)p;
    uint8_t flight = sp->progress;
    if (SPELL_DESC_TRAJECTORY(sp->descriptor) == TRAJ_RETURNING)
        flight = flight < SPELL_PROGRESS_APEX ? flight : (uint8_t)(255u - flight);
    sp->pos = side == SIM_SIDE_L ? flight : (uint8_t)(255u - flight);
    if (sp->progress >= SPELL_PROGRESS_IMPACT) {
        uint32_t desc = sp->descriptor;
        resolve_payload(w, side, desc, 0);
        if (form == SPELL_FIREBALL) {
            aftermath_start(w, side ^ 1u, AFTER_FIRE, SPELL_DESC_MAGNITUDE(desc));
            set_outcome(w, FX_DETONATE);
        } else if (SPELL_DESC_TRAJECTORY(desc) == TRAJ_AREA) {
            aftermath_start(w, side ^ 1u, AFTER_PANIC, SPELL_DESC_MAGNITUDE(desc));
        }
        spell_despawn(sp);
    }
}

// Beam: tempo-paced build, trend-paced sustain, fixed 8-tick decay. Damage
// lands once, at the first sustain tick.
static void step_beam(sim_world_t *w, uint8_t side, sim_spell_t *sp) {
    uint8_t build = SPELL_DESC_TEMPO(sp->descriptor) == TEMPO_DELIBERATE ? 7u
                    : SPELL_DESC_TEMPO(sp->descriptor) == TEMPO_FLOWING  ? 5u
                    : SPELL_DESC_TEMPO(sp->descriptor) == TEMPO_RAPID    ? 4u
                                                                         : 3u;
    uint8_t sustain = SPELL_DESC_TREND(sp->descriptor) == TREND_ACCELERATING   ? 30u
                      : SPELL_DESC_TREND(sp->descriptor) == TREND_DECELERATING ? 20u
                                                                               : 25u;
    if (sp->age <= build)
        sp->progress = (uint8_t)((uint16_t)sp->age * 63u / build);
    else if (sp->age <= (uint8_t)(build + sustain))
        sp->progress = (uint8_t)(64u + (uint16_t)(sp->age - build) * 159u / sustain);
    else
        sp->progress = (uint8_t)(224u + min_u8((uint8_t)(sp->age - build - sustain), 7u) * 4u);
    if (sp->age == (uint8_t)(build + 1u) && !(sp->resolved & SPELL_RESOLVED_PAYLOAD)) {
        resolve_payload(w, side, sp->descriptor, 0);
        sp->resolved |= SPELL_RESOLVED_PAYLOAD;
    }
    if (sp->age >= (uint8_t)(build + sustain + 8u))
        spell_despawn(sp);
}

// Singularity: uncharged, it grows then collapses harmlessly; charged (aux
// set by a collision capture), it advances and resolves on a fixed timeline.
static void step_singularity(sim_world_t *w, uint8_t side, sim_spell_t *sp) {
    if (!sp->aux) {
        sp->progress = sp->age < SINGULARITY_GROW_TICKS
                           ? (uint8_t)(sp->age * 8u)
                           : (uint8_t)(128u + (sp->age - SINGULARITY_GROW_TICKS) * 8u);
        if (sp->age >= SINGULARITY_COLLAPSE_TICKS) {
            spell_despawn(sp);
            /* The collapse leaves a void scar where it hung. */
            residue_deposit(w, residue_mid_zone(side), ELEM_VOID, 2u);
            aftermath_start(w, side, AFTER_INSPECT, 2u);
            set_outcome(w, FX_COLLAPSE);
        }
    } else if (sp->age < SINGULARITY_COLLAPSE_TICKS) {
        sp->progress = (uint8_t)(128u + (sp->age - SINGULARITY_CHARGED_AGE) * 8u);
    } else {
        sp->progress =
            (uint8_t)(192u + min_u8((uint8_t)(sp->age - SINGULARITY_COLLAPSE_TICKS), 7u) * 8u);
        if (sp->age >= SINGULARITY_RESOLVE_TICKS) {
            resolve_payload(w, side, sp->descriptor, 0);
            spell_despawn(sp);
        }
    }
}

// Chain: instant arc, damage at a fixed early tick, tempo-scaled linger.
static void step_chain(sim_world_t *w, uint8_t side, sim_spell_t *sp) {
    uint8_t end = SPELL_DESC_TEMPO(sp->descriptor) >= TEMPO_RAPID ? 14u : 18u;
    uint16_t chain_progress = (uint16_t)sp->age * 16u;
    sp->progress = chain_progress > 255u ? 255u : (uint8_t)chain_progress;
    if (sp->age == 6u && !(sp->resolved & SPELL_RESOLVED_PAYLOAD)) {
        resolve_payload(w, side, sp->descriptor, 0);
        sp->resolved |= SPELL_RESOLVED_PAYLOAD;
        aftermath_start(w, side ^ 1u, AFTER_INSPECT, SPELL_DESC_MAGNITUDE(sp->descriptor));
    }
    if (sp->age >= end)
        spell_despawn(sp);
}

/* Shared prelude-then-periodic-pulse flight for swarm and non-trap conjure
 * carriers: progress packs (aux << 5) | (prelude + cycle) — the decoder in
 * spell_u depends on exactly this layout. `count_resolved` is the swarm's
 * hashed pulse counter (never read, but part of the pinned world state).
 * Returns true while still in the prelude. */
static bool spell_pulse_step(sim_world_t *w, uint8_t side, sim_spell_t *sp, uint8_t prelude,
                             uint8_t interval, bool count_resolved) {
    if (sp->age < prelude) {
        sp->progress = (uint8_t)((sp->aux << 5) | sp->age);
        return true;
    }
    uint8_t cycle = (uint8_t)((sp->age - prelude) % interval);
    if (cycle == 0u && sp->age > prelude && sp->aux) {
        uint8_t payload = SPELL_DESC_PAYLOAD(sp->descriptor);
        resolve_payload(w, side, sp->descriptor,
                        (payload == PAY_DAMAGE || payload == PAY_HYBRID) ? 1u : 0u);
        sp->aux--;
        if (count_resolved)
            sp->resolved++;
    }
    sp->progress = (uint8_t)((sp->aux << 5) | (prelude + cycle));
    return false;
}

// Conjure: a set trap arms in place and detonates on its fuse; the returning
// variant pulses charges like a slower swarm.
static void step_conjure(sim_world_t *w, uint8_t side, sim_spell_t *sp) {
    if (conjure_is_trap(sp->descriptor)) {
        sp->progress = min_u8(sp->age, 31u);
        if (sp->age >= CONJURE_TRAP_FUSE_TICKS) {
            uint32_t desc = sp->descriptor;
            resolve_payload(w, side, desc, min_u8(SPELL_DESC_MAGNITUDE(desc), 2u));
            aftermath_start(w, side ^ 1u, AFTER_PANIC, 2u);
            set_outcome(w, FX_DETONATE);
            spell_despawn(sp);
        }
        return;
    }
    bool in_prelude = spell_pulse_step(w, side, sp, CONJURE_PRELUDE_TICKS,
                                       conjure_interval(sp->descriptor), false);
    if (!in_prelude && !sp->aux)
        spell_despawn(sp);
}

void duel_combat_spell_step(sim_world_t *w, uint8_t side) {
    sim_spell_t *sp = &w->spell[side];
    if (!sp->active)
        return;
    sim_wizard_t *caster = &w->wiz[side];
    if (caster->status == STATUS_FROZEN && (w->tick & 1u))
        return;
    uint8_t form = SPELL_DESC_FORM(sp->descriptor);
    sp->age = sat_inc(sp->age);
    switch (form) {
        case SPELL_PROJECTILE:
        case SPELL_FIREBALL:
        case SPELL_GROUND_WAVE:
            step_ballistic(w, side, sp, form);
            break;
        case SPELL_BEAM:
            step_beam(w, side, sp);
            break;
        case SPELL_SINGULARITY:
            step_singularity(w, side, sp);
            break;
        case SPELL_SWARM:
            spell_pulse_step(w, side, sp, SWARM_PRELUDE_TICKS, swarm_interval(sp->descriptor),
                             true);
            if (!sp->aux)
                spell_despawn(sp);
            break;
        case SPELL_CHAIN:
            step_chain(w, side, sp);
            break;
        case SPELL_CONJURE:
            step_conjure(w, side, sp);
            break;
        default:
            break;
    }
}

static void status_step(sim_world_t *w, uint8_t side) {
    sim_wizard_t *wz = &w->wiz[side];
    if (!wz->status_ticks || wz->status == STATUS_NONE)
        return;
    if (wz->status == STATUS_BURNING && !wz->status_burned &&
        wz->status_ticks <= STATUS_BURN_AT_TICKS) {
        wz->status_burned = 1;
        if (wz->hp) {
            wz->hp--;
            wz->regen_ticks = SIM_REGEN_TICKS;
        }
        if (!wz->hp)
            wizard_ko(w, side); /* delayed tick deliberately does not interrupt otherwise */
    }
    if (wz->status_ticks && --wz->status_ticks == 0u) {
        wz->status = STATUS_NONE;
        wz->status_intensity = 0;
        wz->status_burned = 0;
    }
}

void duel_combat_lifecycle_step(sim_wizard_t *wz) {
    if (wz->life == LIFE_ACTIVE || !wz->life_ticks || --wz->life_ticks)
        return;
    if (wz->life == LIFE_COLLAPSE) {
        wz->life = LIFE_DOWNED;
        wz->life_ticks = SIM_DOWNED_TICKS;
    } else if (wz->life == LIFE_DOWNED) {
        wz->life = LIFE_MEDIC;
        wz->life_ticks = SIM_MEDIC_TICKS;
    } else if (wz->life == LIFE_MEDIC) {
        wz->life = LIFE_REPLACE;
        wz->life_ticks = SIM_REPLACE_TICKS;
        wz->variant = (uint8_t)((wz->variant + 1u) % SIM_ROSTER_N);
    } else {
        wz->life = LIFE_ACTIVE;
        wz->hp = SIM_MAX_HP;
        wz->regen_ticks = SIM_REGEN_TICKS;
    }
}

/* Stance machine (authoritative only, pinned after lifecycle and
 * before regen). Entry is evaluated on idle ticks — after
 * SIM_STANCE_ENTRY_TICKS of LIFE_ACTIVE + INC_IDLE — and keeps re-evaluating
 * each tick until a stance opens, so a later-appearing trigger (an opponent
 * windup) still catches. Every rule reads only authoritative fields already
 * on the wire's view, so the slave renders stance without ever deciding one.
 * Exit lives in inc_keydown. Entry priority is MEDITATE, STUDY, FORTIFY,
 * otherwise NONE (the renderer's PACE/TAUNT). */
void duel_combat_stance_step(sim_world_t *w, uint8_t side) {
    sim_wizard_t *wz = &w->wiz[side];
    if (wz->life != LIFE_ACTIVE || wz->inc_state != INC_IDLE) {
        wz->stance_ticks = 0;
        return;
    }
    wz->stance_ticks = sat_inc(wz->stance_ticks);
    if (wz->stance == DUEL_STANCE_NONE) {
        if (wz->stance_ticks < SIM_STANCE_ENTRY_TICKS)
            return;
        bool hp_low = wz->hp <= SIM_MAX_HP / 2u;
        uint8_t entered = DUEL_STANCE_NONE;
        if (hp_low && wz->temper <= 2u)
            entered = DUEL_STANCE_MEDITATE;
        /* "hp fine" reads as unhurt: mid-duel (any pip missing) an idle
         * wizard paces or fortifies instead, so the STUDY buff stays idle
         * flavour rather than a flat damage inflation — measured to triple
         * the prose KO cadence under the >half reading. */
        else if (wz->hp == SIM_MAX_HP && wz->temper >= 3u && wz->temper <= 5u)
            entered = DUEL_STANCE_STUDY;
        else if (wz->temper >= 6u || w->wiz[side ^ 1u].inc_state == INC_WINDUP)
            entered = DUEL_STANCE_FORTIFY;
        if (entered != DUEL_STANCE_NONE) {
            wz->stance = entered;
            wz->stance_ticks = 0; /* now counts held ticks in-stance */
            if (entered == DUEL_STANCE_STUDY)
                wz->studied = 1;
        }
    } else if (wz->stance == DUEL_STANCE_FORTIFY &&
               wz->stance_ticks == SIM_STANCE_FORTIFY_HOLD_TICKS) {
        /* Exactly-once: the counter passes this value a single time. */
        wz->ward_strength = min_u8((uint8_t)(wz->ward_strength + 1u), 4u);
    }
}

/* Battlefield residue tick, pinned between collision_step and
 * spell_step. First the transmutations: an active spell whose u sits in a
 * zone charged to intensity >= 2 reacts once per spell lifetime
 * (SPELL_RESOLVED_REACTED). Reaction table, first match wins:
 *   ember x frost (either way)  steam burst: zone cleared, 1-damage area
 *                               pulse through the ordinary ward path
 *   void x any                  absorb: zone -1, spell magnitude +1 (cap 4)
 *   force x force               rubble: zone -1, trajectory bumps one lane
 *   same element                feed: zone -1, spell magnitude +1 (cap 4)
 * Unmatched pairs do not react and do not consume the flag. Then the decay
 * clock: one prescaled unit per SIM_RESIDUE_DECAY_PRESCALE ticks, ~45 s per
 * intensity step. Residue never touches ward_covers. */
void duel_combat_residue_step(sim_world_t *w) {
    for (uint8_t side = 0; side < 2; side++) {
        sim_spell_t *sp = &w->spell[side];
        if (!sp->active || (sp->resolved & SPELL_RESOLVED_REACTED))
            continue;
        uint8_t zone = residue_zone_for_u(spell_u(sp, side));
        if (zone >= SIM_RESIDUE_ZONES)
            continue;
        sim_residue_t *rz = &w->residue[zone];
        if (rz->intensity < 2u)
            continue;
        uint8_t se = SPELL_DESC_ELEMENT(sp->descriptor);
        uint8_t ze = rz->element;
        bool reacted = true;
        if ((se == ELEM_EMBER && ze == ELEM_FROST) || (se == ELEM_FROST && ze == ELEM_EMBER)) {
            memset(rz, 0, sizeof *rz);
            resolve_payload(w, side, area_pulse_desc(sp->descriptor), 1u);
            set_outcome(w, FX_DETONATE);
        } else if (se == ELEM_VOID) {
            residue_weaken(w, zone);
            uint8_t mag = SPELL_DESC_MAGNITUDE(sp->descriptor);
            if (mag < 4u)
                sp->descriptor = desc_set_magnitude(sp->descriptor, (uint8_t)(mag + 1u));
        } else if (se == ELEM_FORCE && ze == ELEM_FORCE) {
            residue_weaken(w, zone);
            uint8_t traj = SPELL_DESC_TRAJECTORY(sp->descriptor);
            if (traj < TRAJ_ROOF) /* pure lanes bump; ROOF and specials hold */
                sp->descriptor = desc_set_trajectory(sp->descriptor, (uint8_t)(traj + 1u));
        } else if (se == ze) {
            residue_weaken(w, zone);
            uint8_t mag = SPELL_DESC_MAGNITUDE(sp->descriptor);
            if (mag < 4u)
                sp->descriptor = desc_set_magnitude(sp->descriptor, (uint8_t)(mag + 1u));
        } else {
            reacted = false;
        }
        if (reacted)
            sp->resolved |= SPELL_RESOLVED_REACTED;
    }
    if (w->tick % SIM_RESIDUE_DECAY_PRESCALE == 0u) {
        for (uint8_t zone = 0; zone < SIM_RESIDUE_ZONES; zone++) {
            sim_residue_t *rz = &w->residue[zone];
            if (rz->intensity && rz->decay && --rz->decay == 0u)
                residue_weaken(w, zone);
        }
    }
}

void duel_combat_scry_step(sim_scry_t *sc, uint8_t mask) {
    bool l = (mask & SCRY_M_L) != 0, r = (mask & SCRY_M_R) != 0;
    bool both = l && r, any = l || r, other = (mask & SCRY_M_OTHER) != 0;
    switch (sc->state) {
        case SCRY_IDLE:
            if (both && !other) {
                sc->state = SCRY_PENDING;
                sc->timer = SCRY_PENDING_TICKS;
            } else if (any)
                sc->state = SCRY_FIRST_HELD;
            break;
        case SCRY_FIRST_HELD:
            if (!any)
                sc->state = SCRY_IDLE;
            else if (both && !other) {
                sc->state = SCRY_PENDING;
                sc->timer = SCRY_PENDING_TICKS;
            }
            break;
        case SCRY_PENDING:
            if (other)
                sc->state = SCRY_CANCELLED;
            else if (!both)
                sc->state = any ? SCRY_FIRST_HELD : SCRY_IDLE;
            else if (sc->timer && --sc->timer == 0)
                sc->state = SCRY_ACTIVE;
            break;
        case SCRY_ACTIVE:
            if (!both)
                sc->state = any ? SCRY_FIRST_HELD : SCRY_IDLE;
            else if (other) {
                sc->state = SCRY_SELECT;
                sc->scene = (uint8_t)((sc->scene + 1u) % SCRY_SCENES);
            }
            break;
        case SCRY_SELECT:
            if (!both)
                sc->state = any ? SCRY_FIRST_HELD : SCRY_IDLE;
            else if (!other)
                sc->state = SCRY_ACTIVE;
            break;
        default: /* SCRY_CANCELLED: latched until full release */
            if (!any)
                sc->state = SCRY_IDLE;
            break;
    }
}

void duel_combat_ingest_events(sim_world_t *w, sim_inputs_t in, const sim_event_t *ev, uint8_t n,
                               uint32_t event_down[2]) {
    for (uint8_t i = 0; i < n; i++) {
        if (SIM_EV_KIND(ev[i]) != SIM_EV_KEYDOWN || SIM_EV_COL(ev[i]) >= 6u)
            continue;
        uint8_t side = SIM_EV_SIDE(ev[i]);
        uint8_t pos = (uint8_t)(SIM_EV_ROW(ev[i]) * 6u + SIM_EV_COL(ev[i]));
        inc_keydown(&w->wiz[side], pos, in.layer[side] & 3u);
        event_down[side] |= 1u << pos;
    }
}

void duel_combat_collect_side(sim_world_t *w, sim_inputs_t in, uint8_t side, uint32_t event_down) {
    sim_wizard_t *wz = &w->wiz[side];
    uint32_t held = in.held_pos[side] & 0x00ffffffu;
    uint32_t rising = held & ~wz->prev_held & ~event_down;
    for (uint8_t pos = 0; pos < 24u; pos++)
        if (rising & (1u << pos))
            inc_keydown(wz, pos, in.layer[side] & 3u);
    if (wz->inc_state == INC_COLLECTING) {
        uint32_t released = wz->prev_held & ~held;
        for (uint8_t pos = 0; pos < 24u; pos++)
            if (released & (1u << pos))
                incantation_collection_keyup(&wz->inc, pos);
        incantation_collection_tick(&wz->inc, held);
        if (wz->inc.elapsed >= INCANTATION_FORCE_COMMIT_TICKS)
            inc_commit(w, side, true);
        else if (wz->inc.quiet >= INCANTATION_IDLE_COMMIT_TICKS)
            inc_commit(w, side, false);
    }
    if (wz->rearm_lock && !held) {
        wz->rearm_lock = 0;
        if (wz->inc_state == INC_REARM)
            wz->inc_state = INC_IDLE;
    }
}

void duel_combat_regeneration_step(sim_world_t *w) {
    for (uint8_t side = 0; side < 2; side++) {
        sim_wizard_t *wz = &w->wiz[side];
        if (wz->life != LIFE_ACTIVE || wz->hp >= SIM_MAX_HP || !wz->regen_ticks)
            continue;
        uint16_t burn = wz->stance == DUEL_STANCE_MEDITATE ? 2u : 1u;
        wz->regen_ticks = wz->regen_ticks > burn ? (uint16_t)(wz->regen_ticks - burn) : 0u;
        if (!wz->regen_ticks) {
            wz->hp++;
            wz->regen_ticks = SIM_REGEN_TICKS;
        }
    }
}

void duel_combat_status_release_step(sim_world_t *w) {
    for (uint8_t side = 0; side < 2; side++) {
        sim_wizard_t *wz = &w->wiz[side];
        status_step(w, side);
        if (wz->inc_state == INC_WINDUP && wz->cast_windup && --wz->cast_windup == 0u) {
            if (w->spell[side].active) {
                wz->prepared_desc = wz->pending_desc;
                wz->pending_desc = 0;
                wz->prepared = 1;
                wz->inc_state = INC_PREPARED;
            } else {
                spell_release(w, side, wz->pending_desc);
            }
        } else if (wz->inc_state == INC_PREPARED && !w->spell[side].active) {
            spell_release(w, side, wz->prepared_desc);
        }
    }
}
