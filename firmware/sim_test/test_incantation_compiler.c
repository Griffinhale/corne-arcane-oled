#include "test_harness.h"

static void test_complexity_formula(void) {
    sim_incantation_t inc = {0};
    inc.key_count = 70;
    inc.seen_pos = 0xffffu;
    inc.turns = 20;
    inc.layer_transitions = 9;
    inc.overlap_peak = 5;
    inc.rhythm_changes = 9;
    bool ok = true;
    EXPECT(incantation_complexity(&inc) == 255);
    memset(&inc, 0, sizeof inc);
    inc.key_count = 3;         /* 6 */
    inc.seen_pos = 7;          /* 9 */
    inc.turns = 2;             /* 4 */
    inc.layer_transitions = 1; /* 4 */
    inc.overlap_peak = 2;      /* 8 */
    inc.rhythm_changes = 1;    /* 3 = 34 */
    EXPECT(incantation_complexity(&inc) == 34);
    CHECK(ok, "incantation_complexity_exact_and_clamped");
}

static sim_incantation_t incantation_at_complexity(uint8_t target) {
    sim_incantation_t inc;
    memset(&inc, 0, sizeof inc);
    inc.hash = 0x6d31332eu;
    inc.gap_min = 1u;
    for (uint8_t keys = 0; keys <= 64u; keys++) {
        for (uint8_t unique = 0; unique <= 16u; unique++) {
            for (uint8_t turns = 0; turns <= 16u; turns++) {
                inc.key_count = keys;
                inc.seen_pos = unique == 0u ? 0u : (1u << unique) - 1u;
                inc.turns = turns;
                if (incantation_complexity(&inc) == target) {
                    inc.row_hist[1] = keys ? keys : 1u;
                    inc.row_recent[1] = 1u;
                    return inc;
                }
            }
        }
    }
    inc.key_count = 0xffu; /* impossible sentinel: the test will fail */
    return inc;
}

static void test_magnitude_thresholds(void) {
    static const uint8_t complexity[] = {47u, 48u, 111u, 112u, 191u, 192u};
    static const uint8_t magnitude[] = {1u, 2u, 2u, 3u, 3u, 4u};
    bool ok = true;
    for (size_t i = 0; i < sizeof complexity; i++) {
        sim_incantation_t inc = incantation_at_complexity(complexity[i]);
        EXPECT(inc.key_count != 0xffu && incantation_complexity(&inc) == complexity[i] &&
               SPELL_DESC_MAGNITUDE(incantation_compile(&inc, 0, SIM_TEMPER_NEUTRAL)) ==
                   magnitude[i]);
    }
    sim_incantation_t saturated = {0};
    saturated.hash = 1u;
    saturated.key_count = 64u;
    saturated.seen_pos = 0xffffu;
    saturated.turns = 16u;
    saturated.layer_transitions = 8u;
    saturated.overlap_peak = 5u;
    saturated.rhythm_changes = 8u;
    saturated.row_hist[1] = 64u;
    EXPECT(SPELL_DESC_MAGNITUDE(incantation_compile(&saturated, 0, SIM_TEMPER_NEUTRAL)) == 4u);
    CHECK(ok, "incantation_magnitude_thresholds_48_112_192");
}

static void test_compiler_determinism_and_gates(void) {
    sim_incantation_t inc = {0};
    inc.hash = 0x12345678u;
    inc.key_count = 1;
    inc.seen_pos = 1;
    inc.row_hist[0] = 1;
    inc.row_recent[0] = 1;
    inc.gap_min = 1;
    uint32_t a = incantation_compile(&inc, 0, SIM_TEMPER_NEUTRAL),
             b = incantation_compile(&inc, 0, SIM_TEMPER_NEUTRAL);
    bool ok = true;
    EXPECT(a == b && SPELL_DESC_FORM(a) == SPELL_PROJECTILE &&
           SPELL_DESC_ELEMENT(a) == ELEM_FROST && (a & 0xff000000u) == 0 && SPELL_DESC_VALID(a));

    /* Track T ladder: complexity 64 opens the first four forms (ground wave
     * joined at the new 48 gate) but never beam/singularity. */
    memset(&inc, 0, sizeof inc);
    inc.key_count = 8;      /* 16 */
    inc.seen_pos = 0xffffu; /* 48 => 64 */
    inc.row_hist[1] = 8;
    inc.row_recent[1] = 1;
    inc.gap_min = 0;
    bool saw_non_projectile = false;
    for (uint32_t h = 1; h < 500; h++) {
        inc.hash = h * 2654435761u;
        uint8_t form = SPELL_DESC_FORM(incantation_compile(&inc, 1, SIM_TEMPER_NEUTRAL));
        EXPECT(form == SPELL_PROJECTILE || form == SPELL_FIREBALL || form == SPELL_SWARM ||
               form == SPELL_GROUND_WAVE);
        saw_non_projectile |= form != SPELL_PROJECTILE;
    }
    EXPECT(saw_non_projectile);

    /* Track T's headline promise: every form is reachable once complexity
     * hits 160 (the old ladder held the full roster hostage above 224). */
    sim_incantation_t open = incantation_at_complexity(160u);
    uint32_t seen_forms = 0;
    for (uint32_t h = 1; h < 2000; h++) {
        open.hash = h * 2654435761u;
        seen_forms |= 1u << SPELL_DESC_FORM(incantation_compile(&open, 0, SIM_TEMPER_NEUTRAL));
    }
    EXPECT(open.key_count != 0xffu && seen_forms == 0xffu);
    CHECK(ok, "incantation_compiler_determinism_privacy_and_complexity_gate");
}

static void test_compiler_reachability(void) {
    uint32_t forms = 0, elements = 0, payloads = 0, trajectories = 0;
    uint32_t magnitudes = 0, statuses = 0, interactions = 0, tempos = 0, trends = 0;
    for (uint32_t i = 0; i < 8192u; i++) {
        for (uint8_t row = 0; row < 4; row++) {
            sim_incantation_t inc = {0};
            inc.hash = i * 2654435761u + row * 0x9e37u;
            inc.key_count = (uint8_t)(1u + i % 80u);
            uint8_t unique = (uint8_t)(1u + (i / 3u) % 24u);
            inc.seen_pos = unique == 24u ? 0x00ffffffu : ((1u << unique) - 1u);
            inc.turns = (uint8_t)((i / 5u) % 20u);
            inc.layer_transitions = (uint8_t)((i / 7u) % 10u);
            inc.overlap_peak = (uint8_t)(1u + (i / 11u) % 6u);
            inc.rhythm_changes = (uint8_t)((i / 13u) % 10u);
            inc.row_hist[row] = inc.key_count;
            inc.row_recent[row] = 4;
            inc.held_ticks = (i & 8u) ? (uint16_t)inc.key_count * 4u : 0u;
            inc.column_drift = (i & 16u) ? 6 : -2;
            uint8_t avg = (uint8_t)(1u + (i / 17u) % 6u);
            inc.gap_count = 3;
            inc.gap_sum = (uint16_t)avg * 3u;
            inc.gap_min = avg;
            inc.gap_max = (i & 32u) ? (uint8_t)(avg + 4u) : avg;
            inc.first_gap = (uint8_t)(avg + ((i >> 6) & 1u));
            inc.last_gap = (uint8_t)(avg + ((i >> 7) & 1u));
            uint32_t desc = incantation_compile(&inc, (uint8_t)(i & 3u), SIM_TEMPER_NEUTRAL);
            forms |= 1u << SPELL_DESC_FORM(desc);
            elements |= 1u << SPELL_DESC_ELEMENT(desc);
            payloads |= 1u << SPELL_DESC_PAYLOAD(desc);
            trajectories |= 1u << SPELL_DESC_TRAJECTORY(desc);
            magnitudes |= 1u << (SPELL_DESC_MAGNITUDE(desc) - 1u);
            statuses |= 1u << SPELL_DESC_STATUS(desc);
            interactions |= 1u << SPELL_DESC_INTERACTION(desc);
            tempos |= 1u << SPELL_DESC_TEMPO(desc);
            trends |= 1u << SPELL_DESC_TREND(desc);
        }
    }
    bool ok = true;
    EXPECT(forms == 0xffu && elements == 0x0fu && payloads == 0x0fu && trajectories == 0xffu &&
           magnitudes == 0x0fu && (statuses & 0x1fu) == 0x1fu && interactions == 0x0fu &&
           tempos == 0x0fu && trends == 0x0fu);
    CHECK(ok, "incantation_initial_descriptor_attribute_reachability");
}

void run_incantation_compiler_tests(void) {
    test_complexity_formula();
    test_magnitude_thresholds();
    test_compiler_determinism_and_gates();
    test_compiler_reachability();
}
