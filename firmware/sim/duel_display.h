/*
 * duel_display.h — synchronized, keys-only OLED power policy.
 *
 * This module is deliberately hardware-agnostic. QMK owns the actual OLED
 * commands; the policy only turns a monotonic millisecond clock plus physical
 * key activity into a phase, brightness, and redraw interval. Host context,
 * focus changes, notifications, and simulation activity never enter here.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DUEL_DISPLAY_ACTIVE_BRIGHTNESS 128u
#define DUEL_DISPLAY_DIM_BRIGHTNESS    32u
#define DUEL_DISPLAY_DIM_MS            60000u
#define DUEL_DISPLAY_SLEEP_MS          300000u
#define DUEL_DISPLAY_FADE_MS           1000u

// Cosmetic outcomes retain their active-cadence grammar, but elapsed wall
// time—not the number of OLED callbacks—owns their duration.
#define DUEL_PRESENTATION_QUANTUM_MS 50u
#define DUEL_PRESENTATION_IMPACT_MS  600u
#define DUEL_PRESENTATION_OTHER_MS   400u

typedef enum {
    DUEL_DISPLAY_ACTIVE = 0,
    DUEL_DISPLAY_DIM = 1,
    DUEL_DISPLAY_SLEEP = 2,
} duel_display_phase_t;

typedef struct {
    uint32_t last_key_ms;
    uint32_t phase_since_ms;
    duel_display_phase_t phase;
    bool initialized;
} duel_display_policy_t;

void duel_display_init(duel_display_policy_t *policy, uint32_t now_ms);
void duel_display_note_key(duel_display_policy_t *policy, uint32_t now_ms);
duel_display_phase_t duel_display_update(duel_display_policy_t *policy, uint32_t now_ms);
void duel_display_follow(duel_display_policy_t *policy, duel_display_phase_t phase,
                         uint32_t now_ms);
uint8_t duel_display_brightness(const duel_display_policy_t *policy, uint32_t now_ms);
uint8_t duel_presentation_remaining(uint32_t started_ms, uint16_t duration_ms, uint32_t now_ms);
