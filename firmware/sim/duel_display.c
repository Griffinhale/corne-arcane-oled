#include "duel_display.h"

void duel_display_init(duel_display_policy_t *policy, uint32_t now_ms) {
    policy->last_key_ms = now_ms;
    policy->phase_since_ms = now_ms;
    policy->phase = DUEL_DISPLAY_ACTIVE;
    policy->initialized = true;
}

void duel_display_note_key(duel_display_policy_t *policy, uint32_t now_ms) {
    if (!policy->initialized)
        duel_display_init(policy, now_ms);
    policy->last_key_ms = now_ms;
    policy->phase_since_ms = now_ms;
    policy->phase = DUEL_DISPLAY_ACTIVE;
}

duel_display_phase_t duel_display_update(duel_display_policy_t *policy, uint32_t now_ms) {
    if (!policy->initialized)
        duel_display_init(policy, now_ms);
    uint32_t idle_ms = now_ms - policy->last_key_ms; /* wrap-safe unsigned age */
    duel_display_phase_t next = idle_ms >= DUEL_DISPLAY_SLEEP_MS ? DUEL_DISPLAY_SLEEP
                                : idle_ms >= DUEL_DISPLAY_DIM_MS ? DUEL_DISPLAY_DIM
                                                                 : DUEL_DISPLAY_ACTIVE;
    if (next != policy->phase)
        policy->phase_since_ms = now_ms;
    policy->phase = next;
    return policy->phase;
}

void duel_display_follow(duel_display_policy_t *policy, duel_display_phase_t phase,
                         uint32_t now_ms) {
    if (!policy->initialized)
        duel_display_init(policy, now_ms);
    if (phase > DUEL_DISPLAY_SLEEP)
        phase = DUEL_DISPLAY_ACTIVE;
    if (phase != policy->phase)
        policy->phase_since_ms = now_ms;
    policy->phase = phase;
    if (phase == DUEL_DISPLAY_ACTIVE)
        policy->last_key_ms = now_ms;
}

uint8_t duel_display_brightness(const duel_display_policy_t *policy, uint32_t now_ms) {
    if (!policy->initialized || policy->phase == DUEL_DISPLAY_ACTIVE)
        return DUEL_DISPLAY_ACTIVE_BRIGHTNESS;
    if (policy->phase == DUEL_DISPLAY_SLEEP)
        return 0;

    uint32_t fade_ms = now_ms - policy->phase_since_ms;
    if (fade_ms >= DUEL_DISPLAY_FADE_MS)
        return DUEL_DISPLAY_DIM_BRIGHTNESS;
    uint32_t span = DUEL_DISPLAY_ACTIVE_BRIGHTNESS - DUEL_DISPLAY_DIM_BRIGHTNESS;
    return (uint8_t)(DUEL_DISPLAY_ACTIVE_BRIGHTNESS - (span * fade_ms) / DUEL_DISPLAY_FADE_MS);
}

uint8_t duel_presentation_remaining(uint32_t started_ms, uint16_t duration_ms, uint32_t now_ms) {
    uint32_t elapsed = now_ms - started_ms; // wrap-safe unsigned age
    if (elapsed >= duration_ms)
        return 0;
    uint32_t remaining = duration_ms - elapsed;
    return (uint8_t)((remaining + DUEL_PRESENTATION_QUANTUM_MS - 1u) /
                     DUEL_PRESENTATION_QUANTUM_MS);
}
