/*
 * duel_town.h — the desktop town: one tower at the centre of a small city.
 *
 * A second drawing layer, not a reframing of the first. The panel compositor
 * in firmware/sim draws two mirrored towers into 32x128 canvases with every
 * coordinate written against that geometry; nothing there can be stretched
 * into either town surface. This layer owns a square and a wide composition,
 * and shares the world rather than the pixels.
 *
 * Still one bit per pixel, still chunky outlines: higher fidelity here means
 * more room, not more shades.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "duel_render.h"

#define TOWN_W         256
#define TOWN_H         256
#define LANDSCAPE_W    400
#define LANDSCAPE_H    240
#define TOWN_FB_STRIDE (LANDSCAPE_W / 8)

/* Row-major, eight pixels to a byte, sized once for the widest composition.
 * No hardware reads this one, so it owes the OLED's page-major layout
 * nothing. */
typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t center_x;
    uint16_t ground_y;
    uint8_t bits[TOWN_FB_STRIDE * TOWN_H];
} town_fb_t;

void town_fb_clear(town_fb_t *fb, int width, int height);
bool town_fb_get(const town_fb_t *fb, int x, int y);

/* One frame of the town from one projection. `frame` is the animation phase;
 * everything else is read from the render, exactly as the panel compositor
 * reads it. */
void duel_town_draw(town_fb_t *fb, const duel_render_t *render, uint32_t frame);
