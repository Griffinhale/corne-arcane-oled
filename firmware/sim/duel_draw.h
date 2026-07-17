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

// Screen bands (M15 Foundations & Spires). Layer order is underlay (sky,
// wizard tower) -> floor -> combat -> health -> alert instrument -> scry ->
// recovery -> diagnostics; later layers may deliberately clear/replace
// earlier pixels only inside their own region.
//
// The former reserved alert band (y1-15) and bottom health band (y111-114)
// are retired: the top strip is open sky/spell lanes beside the wizard
// tower's peak, and the bottom border is a stone course. The alert sigil
// now hangs as a banner on the wizard tower's shaft (see draw_alert_sigil).
#define DUEL_DECK_Y0      60  /* rooftop deck rows 60-61 (thickened beam) */
#define DUEL_FLOOR_BEAM_Y 61
#define DUEL_FLOOR_Y0     62
#define DUEL_FLOOR_Y1     110
#define DUEL_STONE_Y0     112 /* stone-course bottom border rails */
#define DUEL_STONE_Y1     116
// Wizard tower shaft: half-width, outer side of each canvas, from the deck
// up into a full architectural peak. Canonical (left) columns, mirrored on
// the right half: shaft edges at x1/x11, base flare to x0/x12 (the deck
// wizard at cx=16 keeps its robe, reaching x12, clear of the flare). The
// full footprint x0-12 is what full-width sky layers must skirt.
#define DUEL_TOWER_W      13  /* footprint columns 0..12 from the outer edge */
#define DUEL_TOWER_PEAK_Y 14  /* shaft top; the peak owns y0..13 */

// Twin Cities rooftop relocation (M12), kept as the deck offset: the combat
// cluster (champion, ward, spell lanes, charge, recovery sparks,
// downed/medic) sits this many pixels above its pre-M12 authoring
// coordinates, which lands the wizard's feet on the rooftop deck. The old
// alert-region bound on this value is gone with the band itself.
#define DUEL_ROOF_DY (-17)

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

/* ------- Twin Cities presentation contract (shared cross-track) -----------
 * Enums and fixed-slot state the renderer derives locally on each half. Pure
 * declarations with zero release footprint. Track R owns the drawing that
 * consumes them; the civic wire bytes that drive them are in duel_host.h. */

// One session-persistent resident per city. Personality changes action weights,
// rooftop attitude, and notification attitude; never mechanics or identity.
enum {
    DUEL_CIVIC_PERSONALITY_DILIGENT = 0,
    DUEL_CIVIC_PERSONALITY_CURIOUS,
    DUEL_CIVIC_PERSONALITY_NERVOUS,
    DUEL_CIVIC_PERSONALITY_PROUD,
    DUEL_CIVIC_PERSONALITY_DISTRACTED,
    DUEL_CIVIC_PERSONALITY_COUNT,
};
// Resident action vocabulary (~3-10 s each, deterministic session-seeded select).
enum {
    DUEL_CIVIC_ACTION_WORK = 0,
    DUEL_CIVIC_ACTION_WALK,
    DUEL_CIVIC_ACTION_INSPECT,
    DUEL_CIVIC_ACTION_REST,
    DUEL_CIVIC_ACTION_WATCH_ROOF,
    DUEL_CIVIC_ACTION_HANDLE_DELIVERY,
    DUEL_CIVIC_ACTION_REACT,
    DUEL_CIVIC_ACTION_COUNT,
};
// Global visitor/courier form (one slot, assigned to one city at a time).
enum {
    DUEL_CIVIC_COURIER_NONE = 0,
    DUEL_CIVIC_COURIER_MESSENGER,   // communication / calendar bird
    DUEL_CIVIC_COURIER_PARCEL,      // transfer / download cart
    DUEL_CIVIC_COURIER_BEACON,      // system / network conduit
    DUEL_CIVIC_COURIER_SENTINEL,    // persistent critical alarm
    DUEL_CIVIC_COURIER_COUNT,
};
// Rare-event deck families (one shared slot). Waves 6/7 allocate shared_pres/
// revision bits; this enum only fixes the family identifiers.
enum {
    DUEL_CIVIC_EVENT_NONE = 0,
    DUEL_CIVIC_EVENT_RUNAWAY_SCROLL,
    DUEL_CIVIC_EVENT_JAMMED_GEAR,
    DUEL_CIVIC_EVENT_WORK_BREAK,
    DUEL_CIVIC_EVENT_DAMAGE_COMPLAINT,
    DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER, // shared / cross-gap
    DUEL_CIVIC_EVENT_CIVIC_SKY,          // shared sky event
    DUEL_CIVIC_EVENT_COUNT,
};

// Fixed-slot local runtime records (spec §16.1). No coordinates: station and
// progress derive them. Field packing is implementation-tunable per track.
typedef struct { uint8_t kind_target; uint8_t lifecycle_phase; uint8_t progress_flags; } civic_visitor_state_t;
typedef struct { uint8_t id_target; uint8_t phase; uint8_t progress; } civic_event_state_t;

/* Shared presentation coordination carried master->slave in the snapshot's
 * shared_pres and revision bytes (Waves 6/7). The master derives them; both
 * halves render from them. Contract owned here so the notification-ecology and
 * rare-event tracks never collide on the bit layout. */

// Global visitor/courier lifecycle (one slot). NONE is COURIER_NONE via the kind.
enum {
    DUEL_CIVIC_VISIT_ARRIVING = 0,
    DUEL_CIVIC_VISIT_WAITING,
    DUEL_CIVIC_VISIT_AGING,
    DUEL_CIVIC_VISIT_RESOLVING,
};
// shared_pres byte: bits0-2 courier kind (DUEL_CIVIC_COURIER_*), bit3 city
// (0 left / 1 right), bits4-5 lifecycle (DUEL_CIVIC_VISIT_*), bits6-7 reserved.
#define DUEL_VISITOR_PACK(kind, city, life) \
    ((uint8_t)(((kind) & 7u) | (((city) & 1u) << 3) | (((life) & 3u) << 4)))
#define DUEL_VISITOR_KIND(v)      ((uint8_t)((v) & 7u))
#define DUEL_VISITOR_CITY(v)      ((uint8_t)(((v) >> 3) & 1u))
#define DUEL_VISITOR_LIFECYCLE(v) ((uint8_t)(((v) >> 4) & 3u))

// Rare-event phase and target.
enum {
    DUEL_CIVIC_EVENT_PHASE_ARMED = 0,
    DUEL_CIVIC_EVENT_PHASE_ACTIVE,
    DUEL_CIVIC_EVENT_PHASE_RESOLVING,
    DUEL_CIVIC_EVENT_PHASE_COOLDOWN,
};
enum {
    DUEL_CIVIC_EVENT_TARGET_LEFT = 0,
    DUEL_CIVIC_EVENT_TARGET_RIGHT,
    DUEL_CIVIC_EVENT_TARGET_SHARED,
};
// revision byte: bits0-2 event id (DUEL_CIVIC_EVENT_*), bits3-4 phase
// (DUEL_CIVIC_EVENT_PHASE_*), bits5-6 target (DUEL_CIVIC_EVENT_TARGET_*), bit7 reserved.
#define DUEL_EVENT_PACK(id, phase, target) \
    ((uint8_t)(((id) & 7u) | (((phase) & 3u) << 3) | (((target) & 3u) << 5)))
#define DUEL_EVENT_ID(v)     ((uint8_t)((v) & 7u))
#define DUEL_EVENT_PHASE(v)  ((uint8_t)(((v) >> 3) & 3u))
#define DUEL_EVENT_TARGET(v) ((uint8_t)(((v) >> 5) & 3u))

/* While an authoritative aftermath is active, current temporarily owns the two
 * existing Twin Cities coordination bytes. bit7 of revision is the discriminator;
 * ordinary courier/rare-event semantics resume automatically when it clears. */
#define INCANTATION_AFTERMATH_WIRE       0x80u
#define INCANTATION_AFTERMATH_REV_RESERVED 0x70u /* bits 4-6 must be clear while aftermath owns the byte */
#define INCANTATION_AFTER_KIND(v, side)  ((uint8_t)(((v) >> ((side) * 3u)) & 7u))
#define INCANTATION_AFTER_WORLD(v)       ((uint8_t)(((v) >> 6) & 3u))
#define INCANTATION_AFTER_PHASE(v, side) ((uint8_t)(((v) >> ((side) * 2u)) & 3u))
#define INCANTATION_FLOOR_TRANSITION_PACK(source, phase, active) \
        ((uint8_t)(((source) & 3u) | (((phase) & 3u) << 2) | ((active) ? 0x10u : 0u)))
#define INCANTATION_FLOOR_TRANSITION_SOURCE(v) ((uint8_t)((v) & 3u))
#define INCANTATION_FLOOR_TRANSITION_PHASE(v)  ((uint8_t)(((v) >> 2) & 3u))
#define INCANTATION_FLOOR_TRANSITION_ACTIVE(v) (((v) & 0x10u) != 0u)

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
    uint8_t     flash_spell_kind; // cached resolved spell style (scry.5, presentation-only)
    uint16_t    diag_overflow;
    uint8_t     diag_tick;
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
    uint8_t     floor_transition; // source[2], phase[2], active[1]; target is civic
    uint8_t     local_ambience; // active[1], tempo[2], trend[2], local wizard only
    // Battlefield residue (Track A), two nibble-pair bytes in the
    // duel_residue_pack grammar: [0] zones 0-1 (== the wire residue byte),
    // [1] zones 2-3. Master packs from its world, slave from the snapshot.
    uint8_t     residue[2];
} duel_render_t;

#define DUEL_RENDER_RESIDUE_ELEMENT(r, zone) \
        ((uint8_t)(((r)->residue[(zone) >> 1] >> (((zone) & 1u) * 4u)) & 3u))
#define DUEL_RENDER_RESIDUE_INTENSITY(r, zone) \
        ((uint8_t)(((r)->residue[(zone) >> 1] >> (((zone) & 1u) * 4u + 2u)) & 3u))

#define DUEL_RENDER_STALE 0x01u
/* layer stays presentation-only. The low bits are the global QMK layer; the
 * high nibble records only the raw layer thumb physically owned by this OLED. */
#define DUEL_RENDER_GLOBAL_LAYER(v) ((uint8_t)((v) & 0x03u))
#define DUEL_RENDER_LOCAL_SHIFT 4u
#define DUEL_RENDER_LOCAL_NONE  0u
#define DUEL_RENDER_LOCAL_LEFT  1u
#define DUEL_RENDER_LOCAL_RIGHT 2u
#define DUEL_RENDER_LOCAL_LAYER(v) \
        ((uint8_t)(((v) >> DUEL_RENDER_LOCAL_SHIFT) & 0x03u))
#define DUEL_RENDER_LAYER_PACK(global, local) \
        ((uint8_t)(((global) & 0x03u) | (((local) & 0x03u) << DUEL_RENDER_LOCAL_SHIFT)))
_Static_assert(sizeof(duel_render_t) <= 40, "render state stays within the M14 compact budget");

void duel_render_from_world(duel_render_t *render, const sim_world_t *world);

// Battlefield u (0..255) -> canvas x for one half. The left canvas shows
// u in [0, 95] (staff tip x=22 out to gap edge x=31), the right shows
// [160, 255] (gap edge x=0 in to staff tip x=9); the band in between is the
// physical desk gap (~25 % of the flight is deliberately invisible).
// Returns false when u is not on this canvas.
bool duel_battlefield_to_x(uint8_t u, bool is_left, int *x);

/* Pure carrier grammar used by the full scene and bilateral temporal tests. */
void incantation_draw_spell(duel_fb_t *fb, const duel_view_spell_t *spell,
                    uint8_t caster_side, uint8_t variant, bool is_left,
                    uint32_t frame);

// Renders one half's full scene from a render snapshot. Presentation-only
// cosmetics key off `frame` (the render frame counter), never off w.tick,
// so render cadence cannot feed back into simulation outcomes. `debug_hud`
// adds the M2 verification overlay: a 1-px tick odometer sweeping the bottom
// row once per second and up to 4 top-corner dots for dropped events. The
// Archive underlay is selected only by online external scene 1; Duel/Focus
// frames take the exact pre-Archive path.
void wiz_draw_scene(duel_fb_t *fb, const duel_render_t *r, bool is_left, uint32_t frame, bool debug_hud);
