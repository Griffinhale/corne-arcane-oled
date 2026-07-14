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

// Compose the snapshot revision byte from a derived event state. The deck packs
// the family id (bits 0-2) and target (bits 5-6) into id_target already, so this
// just folds in the lifecycle phase (bits 3-4): equivalent to
// DUEL_EVENT_PACK(id, phase, target). The master deposits the result in
// r->revision; both halves decode it via DUEL_EVENT_ID/PHASE/TARGET.
static inline uint8_t m12_event_revision(m12_event_state_t st) {
    return (uint8_t)(st.id_target | ((st.phase & 3u) << 3));
}

// Draw the active rare event (decoded from r->revision) into or around the
// floor (local families) or across the desk gap (shared families).
void draw_rare_event(duel_fb_t *fb, const duel_render_t *r, bool is_left);

#endif // ARCANE_M12
