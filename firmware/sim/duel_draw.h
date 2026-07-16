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

// M12 rooftop relocation. Under ARCANE_M12 the whole combat cluster (champion,
// ward, spell lanes, charge anticipation, recovery sparks, downed/medic) shifts
// UP by this many pixels to open a tower-floor band beneath it. The lift is
// bounded by the alert region: draw_charge reaches cy-6, which must stay below
// DUEL_ALERT_Y1 (15), so with cy=39 the safe maximum is -17. Zero in release
// builds, where it constant-folds away and output stays bit-identical to M11.5.
#ifdef ARCANE_M12
#define DUEL_ROOF_DY (-17)
#else
#define DUEL_ROOF_DY 0
#endif

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
void duel_fb_hline(duel_fb_t *fb, int x0, int x1, int y);

// M1 wizard silhouette. `facing` is +1 (left half) / -1 (right half) and
// chooses the side the staff rests on — both wizards face the centre gap.
// `variant` is the M5 roster cosmetic (0..SIM_ROSTER_N-1): pose-invariant
// hat/robe markings so a replacement wizard is visibly a new combatant.
void wiz_draw(duel_fb_t *fb, bool casting, int facing, uint8_t variant);

/* ------- M12 Twin Cities presentation contract (shared cross-track) ---------
 * Enums and fixed-slot state the renderer derives locally on each half. Pure
 * declarations with zero release footprint. Track R owns the drawing that
 * consumes them; the civic wire bytes that drive them are in duel_host.h. */

// One session-persistent resident per city. Personality changes action weights,
// rooftop attitude, and notification attitude; never mechanics or identity.
enum {
    DUEL_M12_PERSONALITY_DILIGENT = 0,
    DUEL_M12_PERSONALITY_CURIOUS,
    DUEL_M12_PERSONALITY_NERVOUS,
    DUEL_M12_PERSONALITY_PROUD,
    DUEL_M12_PERSONALITY_DISTRACTED,
    DUEL_M12_PERSONALITY_COUNT,
};
// Resident action vocabulary (~3-10 s each, deterministic session-seeded select).
enum {
    DUEL_M12_ACTION_WORK = 0,
    DUEL_M12_ACTION_WALK,
    DUEL_M12_ACTION_INSPECT,
    DUEL_M12_ACTION_REST,
    DUEL_M12_ACTION_WATCH_ROOF,
    DUEL_M12_ACTION_HANDLE_DELIVERY,
    DUEL_M12_ACTION_REACT,
    DUEL_M12_ACTION_COUNT,
};
// Global visitor/courier form (one slot, assigned to one city at a time).
enum {
    DUEL_M12_COURIER_NONE = 0,
    DUEL_M12_COURIER_MESSENGER,   // communication / calendar bird
    DUEL_M12_COURIER_PARCEL,      // transfer / download cart
    DUEL_M12_COURIER_BEACON,      // system / network conduit
    DUEL_M12_COURIER_SENTINEL,    // persistent critical alarm
    DUEL_M12_COURIER_COUNT,
};
// Rare-event deck families (one shared slot). Waves 6/7 allocate shared_pres/
// revision bits; this enum only fixes the family identifiers.
enum {
    DUEL_M12_EVENT_NONE = 0,
    DUEL_M12_EVENT_RUNAWAY_SCROLL,
    DUEL_M12_EVENT_JAMMED_GEAR,
    DUEL_M12_EVENT_WORK_BREAK,
    DUEL_M12_EVENT_DAMAGE_COMPLAINT,
    DUEL_M12_EVENT_DIPLOMATIC_COURIER, // shared / cross-gap
    DUEL_M12_EVENT_CIVIC_SKY,          // shared sky event
    DUEL_M12_EVENT_COUNT,
};

// Fixed-slot local runtime records (spec §16.1). No coordinates: station and
// progress derive them. Field packing is implementation-tunable per track.
typedef struct { uint8_t identity_personality; uint8_t action_phase; uint8_t progress; } m12_resident_state_t;
typedef struct { uint8_t kind_phase; uint8_t progress_flags; } m12_prop_state_t;
typedef struct { uint8_t kind_target; uint8_t lifecycle_phase; uint8_t progress_flags; } m12_visitor_state_t;
typedef struct { uint8_t id_target; uint8_t phase; uint8_t progress; } m12_event_state_t;

/* Shared presentation coordination carried master->slave in the snapshot's
 * shared_pres and revision bytes (Waves 6/7). The master derives them; both
 * halves render from them. Contract owned here so the notification-ecology and
 * rare-event tracks never collide on the bit layout. */

// Global visitor/courier lifecycle (one slot). NONE is COURIER_NONE via the kind.
enum {
    DUEL_M12_VISIT_ARRIVING = 0,
    DUEL_M12_VISIT_WAITING,
    DUEL_M12_VISIT_AGING,
    DUEL_M12_VISIT_RESOLVING,
};
// shared_pres byte: bits0-2 courier kind (DUEL_M12_COURIER_*), bit3 city
// (0 left / 1 right), bits4-5 lifecycle (DUEL_M12_VISIT_*), bits6-7 reserved.
#define DUEL_VISITOR_PACK(kind, city, life) \
    ((uint8_t)(((kind) & 7u) | (((city) & 1u) << 3) | (((life) & 3u) << 4)))
#define DUEL_VISITOR_KIND(v)      ((uint8_t)((v) & 7u))
#define DUEL_VISITOR_CITY(v)      ((uint8_t)(((v) >> 3) & 1u))
#define DUEL_VISITOR_LIFECYCLE(v) ((uint8_t)(((v) >> 4) & 3u))

// Rare-event phase and target.
enum {
    DUEL_M12_EVENT_PHASE_ARMED = 0,
    DUEL_M12_EVENT_PHASE_ACTIVE,
    DUEL_M12_EVENT_PHASE_RESOLVING,
    DUEL_M12_EVENT_PHASE_COOLDOWN,
};
enum {
    DUEL_M12_EVENT_TARGET_LEFT = 0,
    DUEL_M12_EVENT_TARGET_RIGHT,
    DUEL_M12_EVENT_TARGET_SHARED,
};
// revision byte: bits0-2 event id (DUEL_M12_EVENT_*), bits3-4 phase
// (DUEL_M12_EVENT_PHASE_*), bits5-6 target (DUEL_M12_EVENT_TARGET_*), bit7 reserved.
#define DUEL_EVENT_PACK(id, phase, target) \
    ((uint8_t)(((id) & 7u) | (((phase) & 3u) << 3) | (((target) & 3u) << 5)))
#define DUEL_EVENT_ID(v)     ((uint8_t)((v) & 7u))
#define DUEL_EVENT_PHASE(v)  ((uint8_t)(((v) >> 3) & 3u))
#define DUEL_EVENT_TARGET(v) ((uint8_t)(((v) >> 5) & 3u))

#ifdef ARCANE_M13
/* While an authoritative aftermath is active, M13 temporarily owns the two
 * existing M12 coordination bytes. bit7 of revision is the discriminator;
 * ordinary courier/rare-event semantics resume automatically when it clears. */
#    define M13_AFTERMATH_WIRE       0x80u
#    define M13_AFTER_KIND(v, side)  ((uint8_t)(((v) >> ((side) * 3u)) & 7u))
#    define M13_AFTER_WORLD(v)       ((uint8_t)(((v) >> 6) & 3u))
#    define M13_AFTER_PHASE(v, side) ((uint8_t)(((v) >> ((side) * 2u)) & 3u))
#    define M13_FLOOR_TRANSITION_PACK(source, phase, active) \
        ((uint8_t)(((source) & 3u) | (((phase) & 3u) << 2) | ((active) ? 0x10u : 0u)))
#    define M13_FLOOR_TRANSITION_SOURCE(v) ((uint8_t)((v) & 3u))
#    define M13_FLOOR_TRANSITION_PHASE(v)  ((uint8_t)(((v) >> 2) & 3u))
#    define M13_FLOOR_TRANSITION_ACTIVE(v) (((v) & 0x10u) != 0u)
#endif

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
#ifdef ARCANE_M12
    // Relayed civic semantics + master-computed shared presentation coordination
    // (Track P deposits them from the received snapshot; see duel_host.h macros).
    uint8_t     civic;        // DUEL_CIVIC_* : floor, mode, host intensity
    uint8_t     secondary;    // DUEL_SECONDARY_* : one supporting activity channel
    uint8_t     shared_pres;  // shared rare-event id/phase + visitor assignment
    uint8_t     revision;     // monotonic shared-presentation coherence counter
    // Presentation session seed (== the 1-byte split session) and the coarse
    // civic-tick phase (~250-500 ms per step). The resident/floor derive their
    // whole state from these plus (is_left, floor, personality); the render-skip
    // memcmp gate therefore only advances when civic_phase advances (plan §2 D3).
    uint8_t     seed;         // session-persistent presentation seed
    uint8_t     civic_phase;  // coarse civic-tick counter (NOT w.tick, NOT frame)
#ifdef ARCANE_M13
    uint8_t     floor_transition; // source[2], phase[2], active[1]; target is civic
#endif
#endif
} duel_render_t;

#define DUEL_RENDER_STALE 0x01u
#ifdef ARCANE_M13
/* layer stays presentation-only. The low bits are the global QMK layer; the
 * high nibble records only the raw layer thumb physically owned by this OLED. */
#    define DUEL_RENDER_GLOBAL_LAYER(v) ((uint8_t)((v) & 0x03u))
#    define DUEL_RENDER_LOCAL_SHIFT 4u
#    define DUEL_RENDER_LOCAL_NONE  0u
#    define DUEL_RENDER_LOCAL_LEFT  1u
#    define DUEL_RENDER_LOCAL_RIGHT 2u
#    define DUEL_RENDER_LOCAL_LAYER(v) \
        ((uint8_t)(((v) >> DUEL_RENDER_LOCAL_SHIFT) & 0x03u))
#    define DUEL_RENDER_LAYER_PACK(global, local) \
        ((uint8_t)(((global) & 0x03u) | (((local) & 0x03u) << DUEL_RENDER_LOCAL_SHIFT)))
#endif
#ifdef ARCANE_M12
_Static_assert(sizeof(duel_render_t) <= 38, "civic render state stays within one compact block");
#else
_Static_assert(sizeof(duel_render_t) <= 32, "render state must remain compact");
#endif

void duel_render_from_world(duel_render_t *render, const sim_world_t *world);

// Battlefield u (0..255) -> canvas x for one half. The left canvas shows
// u in [0, 95] (staff tip x=22 out to gap edge x=31), the right shows
// [160, 255] (gap edge x=0 in to staff tip x=9); the band in between is the
// physical desk gap (~25 % of the flight is deliberately invisible).
// Returns false when u is not on this canvas.
bool duel_battlefield_to_x(uint8_t u, bool is_left, int *x);

#ifdef ARCANE_M13
/* Pure carrier grammar used by the full scene and bilateral temporal tests. */
void m13_draw_spell(duel_fb_t *fb, const duel_view_spell_t *spell,
                    uint8_t caster_side, uint8_t variant, bool is_left,
                    uint32_t frame);
#endif

// Renders one half's full scene from a render snapshot. Presentation-only
// cosmetics key off `frame` (the render frame counter), never off w.tick,
// so render cadence cannot feed back into simulation outcomes. `debug_hud`
// adds the M2 verification overlay: a 1-px tick odometer sweeping the bottom
// row once per second and up to 4 top-corner dots for dropped events. M9's
// Archive is selected only by online external scene 1 and remains a pure
// underlay; Duel/Focus frames take the exact pre-M9 path.
void wiz_draw_scene(duel_fb_t *fb, const duel_render_t *r, bool is_left, uint32_t frame, bool debug_hud);
