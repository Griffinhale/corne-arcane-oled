/*
 * duel_draw.h — presentation drawing for the Corne Arcane OLED system.
 *
 * Hardware-agnostic: draws into a caller-provided 1bpp framebuffer, no QMK
 * includes. The same code is compiled into the firmware (blitted to the OLED
 * in keymap.c) and into the host test harness / previewer (sim_test/), so
 * golden frame tests exercise literally the bytes the hardware shows.
 *
 * ORIENTATION: the OLEDs run portrait (OLED_ROTATION_270 on both halves), so
 * the logical canvas is 32 wide x 128 tall. Logical x is the desk left-right
 * axis and runs the SAME desk direction on both canvases (both panels are
 * mounted identically): hardware-verified in M4, the centre-gap edge is
 * x = 31 on the LEFT half and x = 0 on the RIGHT half. Facing the gap is
 * therefore +1 on the left wizard and -1 on the right.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "duel_sim.h"

#define DUEL_CANVAS_W 32
#define DUEL_CANVAS_H 128

// 1bpp framebuffer, bit index = y * DUEL_CANVAS_W + x. 512 bytes.
typedef struct {
    uint8_t bits[DUEL_CANVAS_W * DUEL_CANVAS_H / 8];
} duel_fb_t;

void duel_fb_clear(duel_fb_t *fb);
void duel_fb_px(duel_fb_t *fb, int x, int y, bool on); // clips out-of-range
bool duel_fb_get(const duel_fb_t *fb, int x, int y);

// M1 wizard silhouette. `facing` is +1 (left half) / -1 (right half) and
// chooses the side the staff rests on — both wizards face the centre gap.
// `variant` is the M5 roster cosmetic (0..SIM_ROSTER_N-1): pose-invariant
// hat/robe markings so a replacement wizard is visibly a new combatant.
void wiz_draw(duel_fb_t *fb, bool casting, int facing, uint8_t variant);

// Everything the renderer needs for one frame: a stable world snapshot plus
// presentation-only state the glue layer maintains (never fed back to the sim).
typedef struct {
    sim_world_t w;
    bool        stale_link;   // slave stopped hearing the master (M3)
    uint8_t     flash_frames; // fx flash countdown, in RENDER frames (M4)
    uint8_t     flash_kind;   // FX_* being flashed
    // M7 scry-overlay content — presentation-only, filled by the glue and never
    // fed back to the sim. Whether the overlay draws at all comes from the
    // world (scry_is_open); these only populate its concise readout.
    uint8_t     overlay_layer; // current highest active QMK layer
    uint8_t     overlay_host;  // host link: 0 offline (M8 stub), 1 online
    uint8_t     overlay_notif; // pending notification count (M8 stub)
} duel_render_t;

// Battlefield u (0..255) -> canvas x for one half. The left canvas shows
// u in [0, 95] (wizard x=16 out to gap edge x=31), the right shows
// [160, 255] (gap edge x=1 in to wizard x=16); the band in between is the
// physical desk gap (~25 % of the flight is deliberately invisible).
// Returns false when u is not on this canvas.
bool duel_battlefield_to_x(uint8_t u, bool is_left, int *x);

// Renders one half's full scene from a render snapshot. Presentation-only
// cosmetics key off `frame` (the render frame counter), never off w.tick,
// so render cadence cannot feed back into simulation outcomes. `debug_hud`
// adds the M2 verification overlay: a 1-px tick odometer sweeping the bottom
// row once per second and up to 4 top-corner dots for dropped events.
void wiz_draw_scene(duel_fb_t *fb, const duel_render_t *r, bool is_left, uint32_t frame, bool debug_hud);
