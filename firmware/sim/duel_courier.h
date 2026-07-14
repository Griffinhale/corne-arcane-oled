/*
 * duel_courier.h — M12 notification ecology (Wave 6, presentation-only).
 *
 * One global visitor/courier occupies the active floor at a time, driven by the
 * normalized notification summary (category/count/age/persistence). The master
 * derives the visitor state and packs it into the snapshot's shared_pres byte
 * (DUEL_VISITOR_* in duel_draw.h); both halves render it locally. No coordinates
 * or sprites cross the split link. Compiles out entirely when ARCANE_M12 is off.
 */
#pragma once

#include "duel_draw.h"

#ifdef ARCANE_M12

// Derive the global visitor from the notification summary + session seed + civic
// phase. Master-side; the result is packed into shared_pres via DUEL_VISITOR_PACK.
m12_visitor_state_t m12_visitor_derive(uint8_t seed, uint8_t phase,
                                       uint8_t category, uint8_t count, uint8_t age,
                                       bool persistent);

// Draw the current visitor/courier (decoded from r->shared_pres) into the floor
// band, distinguishable from the resident and the static anchors.
void draw_courier(duel_fb_t *fb, const duel_render_t *r, bool is_left);

#endif // ARCANE_M12
