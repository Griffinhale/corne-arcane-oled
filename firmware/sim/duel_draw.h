/* Narrow full-scene compositor entry point. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "duel_framebuffer.h"
#include "duel_render.h"

void duel_scene_draw(duel_fb_t *fb, const duel_render_t *render, bool is_left, uint32_t frame,
                     bool debug_hud);
