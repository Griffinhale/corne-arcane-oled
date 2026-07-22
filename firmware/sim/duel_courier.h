/*
 * duel_courier.h — notification ecology (presentation-only).
 *
 * One global visitor/courier occupies the active floor at a time, driven by the
 * normalized notification summary (category/count/age/persistence). The master
 * derives the visitor state and packs it into the snapshot's shared_pres byte
 * (DUEL_VISITOR_* in duel_draw.h); both halves render it locally. No coordinates
 * or sprites cross the split link.
 */
#pragma once

#include "duel_draw.h"


// Count-bucket density (spec §11: count buckets 1 / 2-4 / 5+). The notification
// count scales the ONE visitor's object density (plumage / stacked parcels /
// conduit rays), never the number of actors.
enum {
    DUEL_CIVIC_DENSITY_SINGLE = 0, // 1 notification
    DUEL_CIVIC_DENSITY_FEW    = 1, // 2-4
    DUEL_CIVIC_DENSITY_MANY   = 2, // 5+
};

// The shared DUEL_VISITOR_PACK only carries kind/city/lifecycle; the count bucket
// rides the two reserved shared_pres bits (6-7). The full visitor byte is
// DUEL_VISITOR_PACK(kind,city,life) | DUEL_VISITOR_DENSITY_PACK(bucket), so the
// packing stays byte-compatible with a kind/city/lifecycle-only master.
#define DUEL_VISITOR_DENSITY_PACK(d) ((uint8_t)(((d) & 3u) << 6))
#define DUEL_VISITOR_DENSITY(v)      ((uint8_t)(((v) >> 6) & 3u))

// civic_visitor_state_t field packing (Wave 6 owns it per the §16.1 note):
//   kind_target     : bits0-2 courier kind (DUEL_CIVIC_COURIER_*), bit3 city (0 L / 1 R)
//   lifecycle_phase : DUEL_CIVIC_VISIT_*
//   progress_flags  : bits0-1 density bucket (DUEL_CIVIC_DENSITY_*), bit2 persistent
#define DUEL_VISITOR_STATE_KIND(s)       ((uint8_t)((s).kind_target & 7u))
#define DUEL_VISITOR_STATE_CITY(s)       ((uint8_t)(((s).kind_target >> 3) & 1u))
#define DUEL_VISITOR_STATE_DENSITY(s)    ((uint8_t)((s).progress_flags & 3u))

// Derive the global visitor from the notification summary + session seed + civic
// phase. Master-side, deterministic (keys off seed/phase, never w.tick). The
// result is packed into shared_pres via civic_visitor_shared_pres().
civic_visitor_state_t civic_visitor_derive(uint8_t seed, uint8_t phase,
                                       uint8_t category, uint8_t count, uint8_t age,
                                       bool persistent);

// Pack a derived visitor state into the shared_pres wire byte the renderer reads:
// DUEL_VISITOR_PACK(kind,city,life) | DUEL_VISITOR_DENSITY_PACK(bucket).
uint8_t civic_visitor_shared_pres(civic_visitor_state_t state);

// Draw the current visitor/courier (decoded from r->shared_pres) into the floor
// band, distinguishable from the resident and the static anchors.
void draw_courier(duel_fb_t *fb, const duel_render_t *r, bool is_left);
