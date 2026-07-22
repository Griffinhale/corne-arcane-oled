/* Clipped 1bpp framebuffer and desk-space drawing primitives. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DUEL_CANVAS_W 32
#define DUEL_CANVAS_H 128

typedef struct {
    uint8_t bits[DUEL_CANVAS_W * DUEL_CANVAS_H / 8];
} duel_fb_t;

void duel_fb_clear(duel_fb_t *fb);
void duel_fb_px(duel_fb_t *fb, int x, int y, bool on);
bool duel_fb_get(const duel_fb_t *fb, int x, int y);
void duel_fb_hline(duel_fb_t *fb, int x0, int x1, int y);
void duel_fb_line(duel_fb_t *fb, int x0, int y0, int x1, int y1);

int duel_fb_desk_x(bool is_left, int x);
void duel_fb_desk_hline(duel_fb_t *fb, bool is_left, int x0, int x1, int y);
void duel_fb_desk_vline(duel_fb_t *fb, bool is_left, int x, int y0, int y1);
