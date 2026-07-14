/*
 * duel_event.h — M12 rare-event deck (Wave 7, presentation-only).
 *
 * One shared rare-event slot. A deterministic, safety-gated weighted deck
 * selects a family; the master advances it and packs (id/phase/target) into the
 * snapshot's revision byte (DUEL_EVENT_* in duel_draw.h). Both halves render it
 * locally. Never alters combat, host state, notification policy, or saved data.
 * Compiles out entirely when ARCANE_M12 is off.
 */
#pragma once

#include "duel_draw.h"

#ifdef ARCANE_M12

// Advance the deterministic deck one civic phase. `eligible` folds the safety
// gates (no critical visitor, no transition, no KO/replacement, family cooldown).
// Master-side; the result is packed into revision via DUEL_EVENT_PACK.
m12_event_state_t m12_event_derive(uint8_t seed, uint8_t phase, bool eligible);

// Draw the active rare event (decoded from r->revision) into or around the
// floor (local families) or across the desk gap (shared families).
void draw_rare_event(duel_fb_t *fb, const duel_render_t *r, bool is_left);

#endif // ARCANE_M12
