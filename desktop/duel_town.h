/*
 * duel_town.h — the desktop town: one tower at the centre of a small city.
 *
 * A second drawing layer, not a reframing of the first. The panel compositor
 * in firmware/sim draws two mirrored towers into 32x128 canvases with every
 * coordinate written against that geometry; nothing there can be stretched to
 * a square. This one owns its own surface and its own composition, and shares
 * the world rather than the pixels.
 *
 * Still one bit per pixel, still chunky outlines: higher fidelity here means
 * more room, not more shades.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "duel_render.h"

#define TOWN_W      256
#define TOWN_H      256
#define TOWN_STRIDE (TOWN_W / 8)

/* Row-major, eight pixels to a byte. No hardware reads this one, so it owes
 * the OLED's page-major layout nothing. */
typedef struct {
    uint8_t bits[TOWN_STRIDE * TOWN_H];
} town_fb_t;

void town_fb_clear(town_fb_t *fb);
bool town_fb_get(const town_fb_t *fb, int x, int y);

/* One frame of the town from one projection. `frame` is the animation phase;
 * everything else is read from the render, exactly as the panel compositor
 * reads it. */
void duel_town_draw(town_fb_t *fb, const duel_render_t *render, uint32_t frame);
