#include <string.h>

#include "duel_framebuffer.h"

void duel_fb_clear(duel_fb_t *fb) {
    memset(fb->bits, 0, sizeof fb->bits);
}

void duel_fb_px(duel_fb_t *fb, int x, int y, bool on) {
    if (x < 0 || x >= DUEL_CANVAS_W || y < 0 || y >= DUEL_CANVAS_H) return;
    int     idx  = x + (y >> 3) * DUEL_CANVAS_W;
    uint8_t mask = (uint8_t)(1u << (y & 7));
    if (on) {
        fb->bits[idx] |= mask;
    } else {
        fb->bits[idx] &= (uint8_t)~mask;
    }
}

bool duel_fb_get(const duel_fb_t *fb, int x, int y) {
    if (x < 0 || x >= DUEL_CANVAS_W || y < 0 || y >= DUEL_CANVAS_H) return false;
    int idx = x + (y >> 3) * DUEL_CANVAS_W;
    return (fb->bits[idx] >> (y & 7)) & 1u;
}

void duel_fb_hline(duel_fb_t *fb, int x0, int x1, int y) {
    for (int x = x0; x <= x1; x++) duel_fb_px(fb, x, y, true);
}

void duel_fb_line(duel_fb_t *fb, int x0, int y0, int x1, int y1) {
    int dx = x1 - x0, dy = y1 - y0;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    dx = dx < 0 ? -dx : dx;
    dy = dy < 0 ? -dy : dy;
    int err = dx - dy;
    for (;;) {
        duel_fb_px(fb, x0, y0, true);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

int duel_fb_desk_x(bool is_left, int x) {
    return is_left ? x : DUEL_CANVAS_W - 1 - x;
}

void duel_fb_desk_hline(duel_fb_t *fb, bool is_left, int x0, int x1, int y) {
    int a = duel_fb_desk_x(is_left, x0), b = duel_fb_desk_x(is_left, x1);
    if (a > b) { int t = a; a = b; b = t; }
    duel_fb_hline(fb, a, b, y);
}

void duel_fb_desk_vline(duel_fb_t *fb, bool is_left, int x, int y0, int y1) {
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    x = duel_fb_desk_x(is_left, x);
    for (; y0 <= y1; y0++) duel_fb_px(fb, x, y0, true);
}
