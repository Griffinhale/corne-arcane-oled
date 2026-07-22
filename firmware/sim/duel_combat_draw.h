/* Combat geometry and carrier drawing contracts used by focused tests. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "duel_framebuffer.h"
#include "duel_render.h"

bool duel_combat_battlefield_to_x(uint8_t u, bool is_left, int *x);
void duel_combat_draw_spell(duel_fb_t *fb, const duel_view_spell_t *spell,
                            uint8_t caster_side, uint8_t variant, bool is_left,
                            uint32_t frame);
