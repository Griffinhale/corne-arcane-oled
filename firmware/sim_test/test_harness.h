/* Shared mechanics-test assertions and deterministic helpers. */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "duel_draw.h"
#include "duel_combat_draw.h"
#include "duel_courier.h"
#include "duel_event.h"
#include "duel_host.h"
#include "duel_proto.h"
#include "duel_resident.h"
#include "duel_runtime.h"
#include "duel_rgb.h"
#include "duel_sim.h"
#include "duel_view.h"
#include "test_support.h"

extern int test_failures;
extern const char *test_expect_expr;
extern int test_expect_line;

#define CHECK(condition, name)                                                                     \
    do {                                                                                           \
        if (condition)                                                                             \
            printf("PASS %s\n", name);                                                             \
        else {                                                                                     \
            printf("FAIL %s (%s:%d)\n", name, __FILE__, __LINE__);                                 \
            if (test_expect_expr)                                                                  \
                printf("     first failing sub-assertion (line %d): %s\n", test_expect_line,       \
                       test_expect_expr);                                                          \
            test_failures++;                                                                       \
        }                                                                                          \
        test_expect_expr = NULL;                                                                   \
    } while (0)

#define EXPECT(condition)                                                                          \
    do {                                                                                           \
        bool test_expect_ok_ = (condition);                                                        \
        if (!test_expect_ok_ && !test_expect_expr) {                                               \
            test_expect_expr = #condition;                                                         \
            test_expect_line = __LINE__;                                                           \
        }                                                                                          \
        ok &= test_expect_ok_;                                                                     \
    } while (0)

uint32_t desc_set_magnitude_for_test(uint32_t desc, uint8_t magnitude);
void step(sim_world_t *world, uint32_t left, uint32_t right, uint8_t left_layer,
          uint8_t right_layer, const sim_event_t *events, uint8_t event_count);
void tap(sim_world_t *world, uint8_t side, uint8_t row, uint8_t column, uint8_t layer);
void wait_ticks(sim_world_t *world, unsigned ticks);
void release_recipe(sim_world_t *world, uint8_t side, uint8_t row);
void install_spell(sim_world_t *world, uint8_t side, uint32_t descriptor, uint8_t progress);
void land_spell(sim_world_t *world, uint8_t side, uint32_t descriptor);
void idle_step(sim_world_t *world);
uint64_t incantation_bytes_hash(const void *data, size_t size);
bool exact_mirror(const duel_fb_t *left, const duel_fb_t *right);
unsigned band_difference(const duel_fb_t *left, const duel_fb_t *right, int y0, int y1);
unsigned framebuffer_pixels(const duel_fb_t *framebuffer);
void incantation_render(duel_fb_t *framebuffer, const duel_render_t *render, bool is_left,
                        bool diagnostics);
void render_floor_scene(uint8_t floor, bool is_left, uint8_t transition, duel_fb_t *framebuffer);
