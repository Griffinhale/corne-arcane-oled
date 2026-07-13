/* Canonical M11 visual scenarios shared by preview, gallery, and tests. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "duel_draw.h"

typedef struct {
    const char *name;
    const char *group;
    const char *description;
    uint32_t    frame;
    bool        diagnostics;
} duel_scenario_t;

size_t duel_scenario_count(void);
const duel_scenario_t *duel_scenario_at(size_t index);
const duel_scenario_t *duel_scenario_find(const char *name);
bool duel_scenario_build(const duel_scenario_t *scenario, duel_render_t *render);
void duel_scenario_render(const duel_scenario_t *scenario, uint32_t frame,
                          duel_fb_t *left, duel_fb_t *right);
