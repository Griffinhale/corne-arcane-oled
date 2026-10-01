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

#include "duel_city.h"
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

/*
 * The typing summary, as the town reads it: the four DUEL_CITY_* typing enums
 * and nothing else. It is not part of the projection because the keyboard
 * never sees it; only this layer draws it. All zero is "none", and none draws
 * the town exactly as it was before these fields existed.
 */
typedef struct {
    uint8_t tempo;      /* DUEL_CITY_TEMPO_*: the pennant */
    uint8_t spread;     /* DUEL_CITY_SPREAD_*: the chimney smoke */
    uint8_t row;        /* DUEL_CITY_ROW_*: which lantern on the tower is lit */
    uint8_t row_spread; /* DUEL_CITY_ROW_SPREAD_*: how many lanterns hang */
} town_typing_t;

/* One frame of the town from one projection. `frame` is the animation phase;
 * everything else is read from the render, exactly as the panel compositor
 * reads it, except the typing summary, which only the town draws. `typing`
 * may be NULL, which is the same as none. */
void duel_town_draw(town_fb_t *fb, const duel_render_t *render, const town_typing_t *typing,
                    uint32_t frame);
