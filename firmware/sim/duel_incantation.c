/* Deterministic incantation descriptor compiler.
 *
 * This current-only translation unit consumes physical positions, level
 * masks, normalized layers, and fixed ticks;
 * keycodes and text never cross this boundary. */
#include "duel_incantation.h"

#include <string.h>

#define FNV1A_OFFSET 2166136261u
#define FNV1A_PRIME  16777619u

static uint8_t min_u8(uint8_t a, uint8_t b) { return a < b ? a : b; }
static uint8_t popcount24(uint32_t v) {
    uint8_t n = 0;
    v &= 0x00ffffffu;
    while (v) { v &= v - 1u; n++; }
    return n;
}

static uint8_t sat_inc(uint8_t value) {
    return value == 0xffu ? value : (uint8_t)(value + 1u);
}

static uint8_t gap_bucket(uint8_t ticks) {
    return ticks <= 1u ? TEMPO_FRANTIC : ticks <= 2u ? TEMPO_RAPID
                                                     : ticks <= 4u ? TEMPO_FLOWING
                                                                   : TEMPO_DELIBERATE;
}

static void hash_byte(uint32_t *hash, uint8_t value) {
    *hash = (*hash ^ value) * FNV1A_PRIME;
}

static void hash_token(sim_incantation_t *incantation, bool down, uint8_t position,
                       uint8_t layer, uint8_t quantum) {
    hash_byte(&incantation->hash,
              (uint8_t)((down ? 0x80u : 0u) | (position & 0x1fu)));
    hash_byte(&incantation->hash,
              (uint8_t)(((layer & 3u) << 4) | gap_bucket(quantum)));
}

void incantation_collection_reset(sim_incantation_t *incantation) {
    memset(incantation, 0, sizeof *incantation);
    incantation->hash = FNV1A_OFFSET;
    incantation->gap_min = 0xffu;
    incantation->last_pos = 0xffu;
}

uint8_t incantation_collection_keydown(sim_incantation_t *incantation,
                                       uint8_t position, uint8_t layer) {
    uint8_t row = (uint8_t)(position / 6u);
    uint8_t column = (uint8_t)(position % 6u);
    uint8_t gap = incantation->idle;
    hash_token(incantation, true, position, layer, gap);
    incantation->seen_pos |= 1u << position;
    incantation->row_hist[row] = sat_inc(incantation->row_hist[row]);
    incantation->newest_rank = sat_inc(incantation->newest_rank);
    incantation->row_recent[row] = incantation->newest_rank;
    if (incantation->key_count) {
        incantation->transitions = sat_inc(incantation->transitions);
        if (position == incantation->last_pos)
            incantation->repetitions = sat_inc(incantation->repetitions);
        int8_t delta = (int8_t)column - (int8_t)(incantation->last_pos % 6u);
        int8_t direction = delta > 0 ? 1 : delta < 0 ? -1 : 0;
        if ((position / 6u) != (incantation->last_pos / 6u))
            incantation->turns = sat_inc(incantation->turns);
        if (direction && incantation->last_direction &&
            direction != incantation->last_direction)
            incantation->turns = sat_inc(incantation->turns);
        if (direction) incantation->last_direction = direction;
        if (delta > 0 && incantation->column_drift < 127 - delta)
            incantation->column_drift += delta;
        else if (delta < 0 && incantation->column_drift > -128 - delta)
            incantation->column_drift += delta;
        if (layer != incantation->last_layer)
            incantation->layer_transitions = sat_inc(incantation->layer_transitions);
        incantation->gap_sum = (uint16_t)(incantation->gap_sum + gap);
        if (!incantation->gap_count) incantation->first_gap = gap;
        uint8_t bucket = gap_bucket(gap);
        if (incantation->gap_count && bucket != incantation->last_gap_bucket)
            incantation->rhythm_changes = sat_inc(incantation->rhythm_changes);
        incantation->gap_count = sat_inc(incantation->gap_count);
        incantation->last_gap = gap;
        incantation->last_gap_bucket = bucket;
        if (gap < incantation->gap_min) incantation->gap_min = gap;
        if (gap > incantation->gap_max) incantation->gap_max = gap;
    }
    incantation->last_pos = position;
    incantation->last_layer = layer;
    incantation->key_count = sat_inc(incantation->key_count);
    incantation->idle = 0;
    incantation->quiet = 0;
    return row;
}

void incantation_collection_keyup(sim_incantation_t *incantation, uint8_t position) {
    hash_token(incantation, false, position, incantation->last_layer,
               incantation->idle);
}

void incantation_collection_tick(sim_incantation_t *incantation,
                                 uint32_t held_positions) {
    incantation->elapsed = sat_inc(incantation->elapsed);
    incantation->idle = sat_inc(incantation->idle);
    incantation->quiet = held_positions ? 0u : sat_inc(incantation->quiet);
    uint8_t overlap = popcount24(held_positions);
    incantation->held_ticks = (uint16_t)(incantation->held_ticks + overlap);
    if (overlap > incantation->overlap_peak) incantation->overlap_peak = overlap;
}

uint8_t incantation_complexity(const sim_incantation_t *inc) {
    uint16_t score = (uint16_t)min_u8(inc->key_count, 64u) * 2u;
    score += (uint16_t)min_u8(popcount24(inc->seen_pos), 16u) * 3u;
    score += (uint16_t)min_u8(inc->turns, 16u) * 2u;
    score += (uint16_t)min_u8(inc->layer_transitions, 8u) * 4u;
    if (inc->overlap_peak > 1u)
        score += (uint16_t)min_u8((uint8_t)(inc->overlap_peak - 1u), 4u) * 8u;
    score += (uint16_t)min_u8(inc->rhythm_changes, 8u) * 3u;
    return score > 255u ? 255u : (uint8_t)score;
}

static uint8_t row_element(uint8_t row) {
    static const uint8_t element[4] = { ELEM_FROST, ELEM_FORCE, ELEM_EMBER, ELEM_VOID };
    return element[row & 3u];
}

/* Doctrine affinity (Track B §4.2): roster variant 0-3 -> force/ember/frost/
 * void. element_row is the inverse of row_element's {frost, force, ember,
 * void} row order — NOT the ELEM_* enum order. */
uint8_t duel_incantation_affinity_element(uint8_t variant) {
    static const uint8_t element[4] = { ELEM_FORCE, ELEM_EMBER, ELEM_FROST, ELEM_VOID };
    return element[variant & 3u];
}

static uint8_t element_row(uint8_t element) {
    static const uint8_t row[4] = { 1u, 2u, 0u, 3u };
    return row[element & 3u];
}

/* Track B: exact-count ties break toward the caster's doctrine affinity row;
 * recency still breaks ties among non-affinity rows. */
static uint8_t dominant_row(const sim_incantation_t *inc, uint8_t affinity_row) {
    uint8_t best = 0, best_n = 0, best_recent = 0;
    for (uint8_t row = 0; row < 4; row++) {
        uint8_t n = inc->row_hist[row];
        if (n > best_n ||
            (n == best_n && best != affinity_row &&
             (row == affinity_row || inc->row_recent[row] >= best_recent))) {
            best = row;
            best_n = n;
            best_recent = inc->row_recent[row];
        }
    }
    return best;
}

static uint8_t choose_form(uint8_t complexity, uint8_t variant, uint8_t temper,
                           uint32_t hash) {
    static const uint8_t forms[8] = { SPELL_PROJECTILE, SPELL_FIREBALL, SPELL_SWARM,
                                      SPELL_GROUND_WAVE, SPELL_BEAM, SPELL_CHAIN,
                                      SPELL_SINGULARITY, SPELL_CONJURE };
    /* M15 Track T: SWARM/CHAIN/CONJURE raised from 2/2/1 so the exotic tail
     * appears at prose complexity. */
    uint8_t weights[8] = { 5, 2, 3, 2, 2, 3, 1, 2 };
    /* M15 Track T flattened ladder: 4 forms open by complexity 48, all 8 by
     * 160 (the old ladder needed 224, which ordinary typing never reached). */
    uint8_t eligible = complexity < 32u ? 1u : complexity < 48u ? 3u :
                       complexity < 76u ? 4u : complexity < 104u ? 5u :
                       complexity < 132u ? 6u : complexity < 160u ? 7u : 8u;
    for (uint8_t i = 0; i < eligible; i++) {
        bool preferred = (variant == 0u && (forms[i] == SPELL_PROJECTILE || forms[i] == SPELL_SWARM)) ||
                         (variant == 1u && (forms[i] == SPELL_FIREBALL || forms[i] == SPELL_GROUND_WAVE)) ||
                         (variant == 2u && (forms[i] == SPELL_BEAM || forms[i] == SPELL_CHAIN)) ||
                         (variant == 3u && (forms[i] == SPELL_SINGULARITY || forms[i] == SPELL_CONJURE));
        if (preferred) weights[i] = (uint8_t)(weights[i] * 2u);
        /* Track B temperament: a hot wizard doubles the aggressive forms, a
         * cool one the patient ones. */
        bool hot  = temper >= 6u && (forms[i] == SPELL_FIREBALL || forms[i] == SPELL_CHAIN);
        bool cool = temper <= 2u && (forms[i] == SPELL_CONJURE || forms[i] == SPELL_SINGULARITY);
        if (hot || cool) weights[i] = (uint8_t)(weights[i] * 2u);
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

uint8_t incantation_tempo_trend(const sim_incantation_t *inc) {
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
    return INCANTATION_AMBIENCE_PACK(true, tempo, trend);
}

uint8_t incantation_local_ambience(const sim_wizard_t *wizard) {
    if (wizard->inc_state == INC_COLLECTING)
        return incantation_tempo_trend(&wizard->inc);
    if (wizard->inc_state == INC_WINDUP || wizard->inc_state == INC_PREPARED) {
        uint32_t desc = wizard->inc_state == INC_PREPARED
                            ? wizard->prepared_desc : wizard->pending_desc;
        if (SPELL_DESC_VALID(desc))
            return INCANTATION_AMBIENCE_PACK(true, SPELL_DESC_TEMPO(desc),
                                             SPELL_DESC_TREND(desc));
    }
    return 0;
}

uint32_t incantation_compile(const sim_incantation_t *inc, uint8_t variant,
                             uint8_t temper) {
    uint8_t complexity = incantation_complexity(inc);
    uint8_t magnitude = complexity < 48u ? 1u : complexity < 112u ? 2u :
                        complexity < 192u ? 3u : 4u;
    uint8_t row = dominant_row(inc, element_row(duel_incantation_affinity_element(variant)));
    uint8_t element = row_element(row);
    uint8_t form = choose_form(complexity, (uint8_t)(variant & 3u), temper, inc->hash);
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
    uint8_t ambience = incantation_tempo_trend(inc);
    uint8_t tempo = INCANTATION_AMBIENCE_TEMPO(ambience);
    uint8_t trend = INCANTATION_AMBIENCE_TREND(ambience);
    return SPELL_DESC_PACK(form, element, payload, trajectory, magnitude, status,
                           interaction, tempo, trend, (inc->hash >> 22) & 3u);
}
