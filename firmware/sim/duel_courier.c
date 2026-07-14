/*
 * duel_courier.c — M12 notification ecology (Wave 6). Stub pending Track A.
 */
#include "duel_courier.h"

#ifdef ARCANE_M12

m12_visitor_state_t m12_visitor_derive(uint8_t seed, uint8_t phase, uint8_t category,
                                       uint8_t count, uint8_t age, bool persistent) {
    (void)seed; (void)phase; (void)category; (void)count; (void)age; (void)persistent;
    return (m12_visitor_state_t){0};
}

void draw_courier(duel_fb_t *fb, const duel_render_t *r, bool is_left) {
    (void)fb; (void)r; (void)is_left; // Wave 6 fills this.
}

#endif // ARCANE_M12
