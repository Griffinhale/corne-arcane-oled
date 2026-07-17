#include "duel_rgb.h"

static duel_rgb_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    duel_rgb_t color = {r, g, b};
    return color;
}

static duel_rgb_t prepared_color(uint8_t element) {
    switch (element) {
        case ELEM_EMBER: return rgb(24, 3, 0);
        case ELEM_FROST: return rgb(0, 10, 24);
        case ELEM_VOID:  return rgb(8, 0, 20);
        default:         return rgb(12, 12, 16);
    }
}

static duel_rgb_t scale_for_display(duel_rgb_t color, duel_display_phase_t phase) {
    if (phase == DUEL_DISPLAY_SLEEP) return rgb(0, 0, 0);
    if (phase == DUEL_DISPLAY_DIM) {
        color.r = (uint8_t)(color.r / 4u);
        color.g = (uint8_t)(color.g / 4u);
        color.b = (uint8_t)(color.b / 4u);
    }
    return color;
}

duel_rgb_t duel_rgb_policy(const duel_rgb_world_t *world, uint8_t led_flags,
                           bool led_is_left) {
    if (world->display_phase == DUEL_DISPLAY_SLEEP) return rgb(0, 0, 0);
    bool underglow = (led_flags & DUEL_RGB_LED_UNDERGLOW) != 0;
    /* Corne modifier/thumb LEDs carry MODIFIER rather than KEYLIGHT. Every
     * non-underglow physical LED is nevertheless part of the 42-key surface. */
    bool keylight = !underglow && led_flags != 0;
    duel_rgb_t color = rgb(0, 0, 0);
    if (world->stale) {
        if (underglow) color = rgb(0, 4, 10);
        return scale_for_display(color, world->display_phase);
    }

    uint8_t affected = 0xffu;
    if (world->flash_kind == FX_IMPACT_L || world->flash_kind == FX_WARD_SHATTER_L)
        affected = SIM_SIDE_L;
    else if (world->flash_kind == FX_IMPACT_R ||
             world->flash_kind == FX_WARD_SHATTER_R)
        affected = SIM_SIDE_R;
    uint8_t led_side = led_is_left ? SIM_SIDE_L : SIM_SIDE_R;
    if (world->flash_active && affected == led_side) {
        color = world->flash_kind == FX_IMPACT_L || world->flash_kind == FX_IMPACT_R
                    ? rgb(32, 0, 0) : rgb(20, 20, 32);
    } else if (keylight && world->prepared[led_side]) {
        color = prepared_color(world->prepared_element[led_side]);
    } else if (underglow && world->observatory) {
        color = rgb(4, 0, 20);
    } else if (underglow) {
        color = led_is_left ? rgb(6, 0, 18) : rgb(18, 6, 0);
    }
    return scale_for_display(color, world->display_phase);
}
