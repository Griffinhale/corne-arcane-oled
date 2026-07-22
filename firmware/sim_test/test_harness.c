#include "test_harness.h"

int test_failures;
const char *test_expect_expr;
int test_expect_line;

uint32_t desc_set_magnitude_for_test(uint32_t desc, uint8_t magnitude) {
    return (desc & ~(3u << 10)) | ((uint32_t)(magnitude - 1u) << 10);
}

void step(sim_world_t *w, uint32_t left, uint32_t right, uint8_t llayer, uint8_t rlayer,
          const sim_event_t *events, uint8_t n) {
    sim_inputs_t in = {0};
    in.held_pos[0] = left;
    in.held_pos[1] = right;
    in.layer[0] = llayer;
    in.layer[1] = rlayer;
    if (left)
        in.down_mask |= 1u;
    if (right)
        in.down_mask |= 2u;
    sim_tick(w, in, events, n, 0);
}

void tap(sim_world_t *w, uint8_t side, uint8_t row, uint8_t col, uint8_t layer) {
    sim_event_t event = SIM_EV_PACK(SIM_EV_KEYDOWN, side, row, col);
    uint32_t held = 1u << (row * 6u + col);
    step(w, side ? 0 : held, side ? held : 0, side ? 0 : layer, side ? layer : 0, &event, 1);
    step(w, 0, 0, 0, 0, NULL, 0);
}

void wait_ticks(sim_world_t *w, unsigned ticks) {
    while (ticks--)
        step(w, 0, 0, 0, 0, NULL, 0);
}

void release_recipe(sim_world_t *w, uint8_t side, uint8_t row) {
    tap(w, side, row, 1, 0);
    wait_ticks(w, INCANTATION_IDLE_COMMIT_TICKS - 1u);
    for (unsigned guard = 0; guard < INCANTATION_WINDUP_MAX_TICKS + 2u && !w->spell[side].active;
         guard++)
        step(w, 0, 0, 0, 0, NULL, 0);
}

void install_spell(sim_world_t *w, uint8_t side, uint32_t desc, uint8_t progress) {
    sim_spell_t *sp = &w->spell[side];
    memset(sp, 0, sizeof *sp);
    sp->active = 1;
    sp->descriptor = desc;
    sp->progress = progress;
    sp->dir = side ? -1 : 1;
}

/* Installed here, one idle tick carries a ballistic spell across the
 * defender's doorstep and resolves contact. */
#define SIM_CONTACT_PROGRESS 239u

void idle_step(sim_world_t *w) { step(w, 0, 0, 0, 0, NULL, 0); }

void land_spell(sim_world_t *w, uint8_t side, uint32_t desc) {
    install_spell(w, side, desc, SIM_CONTACT_PROGRESS);
    idle_step(w);
}

bool exact_mirror(const duel_fb_t *a, const duel_fb_t *b) {
    for (int y = 0; y < DUEL_CANVAS_H; y++)
        for (int x = 0; x < DUEL_CANVAS_W; x++) {
            if (duel_fb_get(a, x, y) != duel_fb_get(b, DUEL_CANVAS_W - 1 - x, y))
                return false;
        }
    return true;
}

unsigned band_difference(const duel_fb_t *a, const duel_fb_t *b, int y0, int y1) {
    unsigned n = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = 0; x < DUEL_CANVAS_W; x++)
            n += duel_fb_get(a, x, y) != duel_fb_get(b, x, y);
    return n;
}

void render_floor_scene(uint8_t floor, bool is_left, uint8_t transition, duel_fb_t *fb) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    duel_render_t r = {0};
    duel_render_from_world(&r, &w);
    r.civic = DUEL_CIVIC_PACK(floor, DUEL_CIVIC_MODE_NORMAL, 0);
    r.seed = 0x42u;
    r.civic_phase = 19u;
    r.floor_transition = transition;
    duel_fb_clear(fb);
    duel_scene_draw(fb, &r, is_left, 7u, false);
}

unsigned framebuffer_pixels(const duel_fb_t *fb) {
    unsigned n = 0;
    for (int y = 0; y < DUEL_CANVAS_H; y++)
        for (int x = 0; x < DUEL_CANVAS_W; x++)
            n += duel_fb_get(fb, x, y);
    return n;
}

void incantation_render(duel_fb_t *fb, const duel_render_t *r, bool is_left, bool diagnostics);

void incantation_render(duel_fb_t *fb, const duel_render_t *r, bool is_left, bool diagnostics) {
    duel_fb_clear(fb);
    duel_scene_draw(fb, r, is_left, 7u, diagnostics);
}

uint64_t incantation_bytes_hash(const void *data, size_t size) {
    const uint8_t *p = data;
    uint64_t h = UINT64_C(1469598103934665603);
    while (size--) {
        h ^= *p++;
        h *= UINT64_C(1099511628211);
    }
    return h;
}
