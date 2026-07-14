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

#include "duel_view.h"

#define DUEL_CANVAS_W 32
#define DUEL_CANVAS_H 128

// Protected presentation regions. Layer order is underlay -> combat -> health
// -> alert -> scry -> recovery -> diagnostics; later layers may deliberately
// clear/replace earlier pixels only inside their own protected region.
#define DUEL_ALERT_Y0 1
#define DUEL_ALERT_Y1 15
#define DUEL_SCRY_X0  3
#define DUEL_SCRY_X1 28
#define DUEL_SCRY_Y0  3
#define DUEL_SCRY_Y1 41
#define DUEL_HEALTH_Y0 111
#define DUEL_HEALTH_Y1 114
#define DUEL_DIAG_TOP_Y 0
#define DUEL_DIAG_BOTTOM_Y (DUEL_CANVAS_H - 1)

typedef enum {
    DUEL_LAYER_UNDERLAY,
    DUEL_LAYER_COMBAT,
    DUEL_LAYER_HEALTH,
    DUEL_LAYER_ALERT,
    DUEL_LAYER_SCRY,
    DUEL_LAYER_RECOVERY,
    DUEL_LAYER_DIAGNOSTICS,
} duel_render_layer_t;

// 1bpp framebuffer in QMK's native page-major OLED layout. Each byte is one
// vertical 8-pixel column: index = x + (y >> 3) * width, bit = y & 7.
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
    duel_view_t view;
    uint8_t     external;     // canonical packed host context
    uint8_t     alert;        // canonical packed host alert
    uint8_t     layer;
    uint8_t     flags;
    uint8_t     flash_frames; // remaining normalized 50 ms presentation quanta
    uint8_t     flash_kind;   // FX_* being flashed
    uint8_t     flash_spell_kind; // cached resolved spell style (M7.5, presentation-only)
    uint16_t    diag_overflow;
    uint8_t     diag_tick;
} duel_render_t;

#define DUEL_RENDER_STALE 0x01u
_Static_assert(sizeof(duel_render_t) <= 32, "render state must remain compact");

void duel_render_from_world(duel_render_t *render, const sim_world_t *world);

// Battlefield u (0..255) -> canvas x for one half. The left canvas shows
// u in [0, 95] (staff tip x=22 out to gap edge x=31), the right shows
// [160, 255] (gap edge x=0 in to staff tip x=9); the band in between is the
// physical desk gap (~25 % of the flight is deliberately invisible).
// Returns false when u is not on this canvas.
bool duel_battlefield_to_x(uint8_t u, bool is_left, int *x);

// Renders one half's full scene from a render snapshot. Presentation-only
// cosmetics key off `frame` (the render frame counter), never off w.tick,
// so render cadence cannot feed back into simulation outcomes. `debug_hud`
// adds the M2 verification overlay: a 1-px tick odometer sweeping the bottom
// row once per second and up to 4 top-corner dots for dropped events. M9's
// Archive is selected only by online external scene 1 and remains a pure
// underlay; Duel/Focus frames take the exact pre-M9 path.
void wiz_draw_scene(duel_fb_t *fb, const duel_render_t *r, bool is_left, uint32_t frame, bool debug_hud);
