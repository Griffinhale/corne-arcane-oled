#pragma once

#include "duel_combat_draw.h"

enum {
    DUEL_BALCONY_MEDITATE = 0,
    DUEL_BALCONY_STUDY,
    DUEL_BALCONY_BIGCAST,
};

void duel_environment_draw_sky(duel_fb_t *fb, const duel_render_t *r, bool is_left);
void duel_environment_draw_tower(duel_fb_t *fb, const duel_render_t *r, bool is_left);
void duel_environment_draw_floor(duel_fb_t *fb, const duel_render_t *r, bool is_left);

void duel_combat_draw_wizard(duel_fb_t *fb, bool casting, int facing, uint8_t variant, int xo,
                             int yo);
void duel_combat_draw_downed(duel_fb_t *fb, int facing, uint8_t variant, int xo);
void duel_combat_draw_medic(duel_fb_t *fb, int x, int facing);
void duel_combat_draw_stance_balcony(duel_fb_t *fb, bool is_left, uint8_t posture, uint32_t frame);
int duel_combat_spell_lane_y(uint8_t kind);
void duel_combat_draw_status(duel_fb_t *fb, const duel_view_wizard_t *wz, int facing,
                             uint32_t frame);
void duel_combat_draw_reaction(duel_fb_t *fb, uint8_t outcome, bool is_left, uint8_t flash_frames);
void duel_combat_draw_charge(duel_fb_t *fb, const duel_view_wizard_t *wz, int facing,
                             uint32_t frame);
void duel_combat_draw_ward(duel_fb_t *fb, int facing, int strength, int focus, bool punctured,
                           int puncture_y);
bool duel_combat_incoming_void_at_ward(const duel_view_t *view, int defender, uint8_t session,
                                       duel_view_spell_t *found);

void duel_overlay_draw_box3(duel_fb_t *fb, int x, int y);
void duel_overlay_draw_alert(duel_fb_t *fb, const duel_render_t *r, bool is_left);
void duel_overlay_draw_scry(duel_fb_t *fb, const duel_render_t *r, bool is_left);
void duel_overlay_draw_attunement(duel_fb_t *fb, const duel_render_t *r,
                                  const duel_view_wizard_t *wz, bool is_left);
void duel_overlay_draw_health(duel_fb_t *fb, const duel_view_wizard_t *wz, bool is_left);
void duel_overlay_draw_local_fx(duel_fb_t *fb, const duel_render_t *r, const duel_view_wizard_t *wz,
                                int facing, bool is_left);
void duel_overlay_draw_residue(duel_fb_t *fb, const duel_render_t *r, bool is_left);
void duel_overlay_draw_fields(duel_fb_t *fb, const duel_render_t *r, bool is_left);
