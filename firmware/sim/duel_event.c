/*
 * duel_event.c — M12 rare-event deck (Wave 7). Stub pending Track B.
 */
#include "duel_event.h"

#ifdef ARCANE_M12

m12_event_state_t m12_event_derive(uint8_t seed, uint8_t phase, bool eligible) {
    (void)seed; (void)phase; (void)eligible;
    return (m12_event_state_t){0};
}

void draw_rare_event(duel_fb_t *fb, const duel_render_t *r, bool is_left) {
    (void)fb; (void)r; (void)is_left; // Wave 7 fills this.
}

#endif // ARCANE_M12
