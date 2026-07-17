/* Pure RGB world-surface priority policy. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "duel_display.h"
#include "duel_sim.h"

enum {
    DUEL_RGB_LED_UNDERGLOW = 0x02u,
    DUEL_RGB_LED_KEYLIGHT = 0x04u,
};

typedef struct { uint8_t r, g, b; } duel_rgb_t;

typedef struct {
    duel_display_phase_t display_phase;
    bool stale;
    bool observatory;
    uint8_t flash_kind;
    bool flash_active;
    bool prepared[2];
    uint8_t prepared_element[2];
} duel_rgb_world_t;

duel_rgb_t duel_rgb_policy(const duel_rgb_world_t *world, uint8_t led_flags,
                           bool led_is_left);

