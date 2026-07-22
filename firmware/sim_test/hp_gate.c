/* Deterministic 30-minute intermittent-work A/B gate for SIM_MAX_HP.
 *
 * Build this file twice, with SIM_MAX_HP=8 and =10. It feeds only reachable
 * physical key events into the production simulator, records per-city KO
 * timestamps after a three-minute warmup, and summarizes intervals. No state
 * is allocated dynamically and neither candidate changes mechanics beyond the
 * compile-time health constant/shaft geometry under evaluation. */
#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "duel_sim.h"

#define WORKLOAD_TICKS (30u * 60u * 1000u / SIM_TICK_MS)
#define WARMUP_TICKS   (3u * 60u * 1000u / SIM_TICK_MS)
#define MAX_KOS        16u

typedef struct {
    uint16_t period[2];
    uint8_t keys[2];
    uint16_t cycle_ticks;
    uint16_t active_ticks;
} workload_t;

typedef struct {
    uint32_t ko[MAX_KOS];
    uint8_t count;
} ko_log_t;

static const workload_t workloads[3] = {
    {{165u, 183u}, {8u, 7u}, 4500u, 2000u},   /* steady phrases, then reflection */
    {{175u, 191u}, {14u, 12u}, 6000u, 1800u}, /* dense editing burst */
    {{180u, 200u}, {10u, 9u}, 7250u, 2500u},  /* mixed-layer revision */
};

#if SIM_MAX_HP == 8
static const uint64_t expected_hash[3] = {
    UINT64_C(0x7b16b82c0ee6c74b), UINT64_C(0x3e6f4099db712e83), UINT64_C(0xfc510bf332345c5a)};
#elif SIM_MAX_HP == 10
static const uint64_t expected_hash[3] = {
    UINT64_C(0xc41c75ee84725e03), UINT64_C(0xdf7662aac9a11cfc), UINT64_C(0x80a8bddf75b4efd5)};
#else
#error "hp_gate must be built with SIM_MAX_HP=8 or SIM_MAX_HP=10"
#endif

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t size) {
    const uint8_t *bytes = data;
    while (size--) {
        hash ^= *bytes++;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void workload_input(uint8_t profile, uint32_t tick, sim_inputs_t *input,
                           const bool suppressed[2], sim_event_t event[2], uint8_t *event_count) {
    static const uint8_t offset[2] = {0u, 37u};
    const workload_t *workload = &workloads[profile];
    *input = (sim_inputs_t){0};
    *event_count = 0u;
    if ((tick % workload->cycle_ticks) >= workload->active_ticks)
        return;
    for (uint8_t side = 0; side < 2u; side++) {
        if (suppressed[side])
            continue;
        uint16_t phase = (uint16_t)((tick + offset[side]) % workload->period[side]);
        if (phase >= (uint16_t)workload->keys[side] * 2u || (phase & 1u))
            continue;
        uint8_t rank = (uint8_t)(phase / 2u);
        uint8_t pos = (uint8_t)((rank * (side ? 7u : 5u) + profile * 3u + side * 11u +
                                 tick / workload->period[side]) %
                                24u);
        uint8_t layer = profile == 2u                 ? (uint8_t)((rank + side) & 3u)
                        : profile == 1u && rank >= 8u ? (uint8_t)(1u + side)
                                                      : 0u;
        input->held_pos[side] = 1u << pos;
        input->down_mask |= (uint8_t)(1u << side);
        input->layer[side] = layer;
        event[(*event_count)++] = SIM_EV_PACK(SIM_EV_KEYDOWN, side, pos / 6u, pos % 6u);
    }
}

static void sort_u32(uint32_t *value, uint8_t count) {
    for (uint8_t i = 1u; i < count; i++) {
        uint32_t item = value[i];
        uint8_t j = i;
        while (j && value[j - 1u] > item) {
            value[j] = value[j - 1u];
            j--;
        }
        value[j] = item;
    }
}

static uint32_t median(const uint32_t *values, uint8_t count) {
    uint32_t sorted[MAX_KOS * 2u];
    if (!count)
        return 0u;
    for (uint8_t i = 0u; i < count; i++)
        sorted[i] = values[i];
    sort_u32(sorted, count);
    return count & 1u ? sorted[count / 2u] : (sorted[count / 2u - 1u] + sorted[count / 2u]) / 2u;
}

static bool run_workload(uint8_t profile, uint32_t *all_intervals, uint8_t *all_count) {
    sim_world_t world;
    ko_log_t log[2] = {0};
    uint8_t previous_life[2] = {LIFE_ACTIVE, LIFE_ACTIVE};
    bool suppressed[2] = {false, false};
    sim_init(&world, SIMF_AUTHORITATIVE, 0u);
    for (uint32_t tick = 0u; tick < WORKLOAD_TICKS; tick++) {
        if (tick % workloads[profile].cycle_ticks == 0u)
            suppressed[0] = suppressed[1] = false;
        sim_inputs_t input;
        sim_event_t event[2];
        uint8_t event_count;
        workload_input(profile, tick, &input, suppressed, event, &event_count);
        sim_tick(&world, input, event, event_count, 0u);
        for (uint8_t side = 0u; side < 2u; side++) {
            if (previous_life[side] == LIFE_ACTIVE && world.wiz[side].life != LIFE_ACTIVE) {
                /* A KO ends the shared work burst: both typists pause for the
                 * ambient civic moment until the next deterministic cycle. */
                suppressed[0] = suppressed[1] = true;
                if (tick + 1u >= WARMUP_TICKS && log[side].count < MAX_KOS)
                    log[side].ko[log[side].count++] = tick + 1u;
            }
            previous_life[side] = world.wiz[side].life;
        }
    }

    bool ok = true;
    uint32_t intervals[MAX_KOS * 2u];
    uint32_t events[MAX_KOS * 2u];
    uint8_t event_total = 0u;
    uint8_t interval_count = 0u;
    printf("candidate=%u workload=%u ko_seconds=", SIM_MAX_HP, profile + 1u);
    for (uint8_t side = 0u; side < 2u; side++) {
        for (uint8_t i = 0u; i < log[side].count; i++)
            printf("%s%c:%lu", side || i ? "," : "", side ? 'R' : 'L',
                   (unsigned long)(log[side].ko[i] * SIM_TICK_MS / 1000u));
        for (uint8_t i = 0u; i < log[side].count; i++)
            events[event_total++] = log[side].ko[i];
    }
    sort_u32(events, event_total);
    for (uint8_t i = 1u; i < event_total; i++) {
        uint32_t interval = events[i] - events[i - 1u];
        intervals[interval_count++] = interval;
        if (interval * SIM_TICK_MS < 90000u)
            ok = false;
        if (*all_count < MAX_KOS * 2u)
            all_intervals[(*all_count)++] = interval;
    }
    uint32_t middle = median(intervals, interval_count);
    uint64_t hash = hash_bytes(UINT64_C(1469598103934665603), log, sizeof log);
    hash = hash_bytes(hash, &world, sizeof world);
    printf(" median_interval_seconds=%lu intervals=%u hash=%016" PRIx64 "\n",
           (unsigned long)(middle * SIM_TICK_MS / 1000u), interval_count, hash);
    if (hash != expected_hash[profile]) {
        fprintf(stderr, "workload %u drifted: expected hash=%016" PRIx64 "\n", profile + 1u,
                expected_hash[profile]);
        ok = false;
    }
    if (interval_count < 4u || middle * SIM_TICK_MS < 180000u || middle * SIM_TICK_MS > 300000u)
        ok = false;
    return ok;
}

int main(void) {
    uint32_t intervals[MAX_KOS * 2u] = {0};
    uint8_t count = 0u;
    bool ok = true;
    for (uint8_t profile = 0u; profile < 3u; profile++)
        ok &= run_workload(profile, intervals, &count);
    uint32_t middle = median(intervals, count);
    printf("%s hp_%u_automated_30m_gate median_seconds=%lu intervals=%u target=180..300\n",
           ok ? "PASS" : "FAIL", SIM_MAX_HP, (unsigned long)(middle * SIM_TICK_MS / 1000u), count);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
