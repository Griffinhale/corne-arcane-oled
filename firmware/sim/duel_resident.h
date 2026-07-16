/*
 * duel_resident.h — Twin Cities resident engine (presentation-only).
 *
 * One resident per city lives in the active tower floor. Its identity, current
 * action, station, and pose are DERIVED LOCALLY and DETERMINISTICALLY from
 * (session seed, is_left, floor, personality, civic phase) — no coordinates or
 * sprites ever cross the split link (plan §2 D1). The resident advances on the
 * bounded civic tick (a coarse phase byte in duel_render_t), NOT per render
 * frame, so the render-skip memcmp gate only redraws when the phase advances
 * (plan §2 D3).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "duel_draw.h"


// Number of civic ticks one resident action occupies. A civic tick is
// ~250-500 ms, so ~16 ticks holds each action in the spec's 3-10 s window.
#define DUEL_CIVIC_ACTION_SLOT 16u

/* Compact floor/action occupation key stored in the existing station byte. */
#    define INCANTATION_OCCUPATION_FLOORS 3u
#    define INCANTATION_OCCUPATION_KEY(floor, action) \
        ((uint8_t)(((floor) * DUEL_CIVIC_ACTION_COUNT) + (action)))
#    define INCANTATION_OCCUPATION_FLOOR(key) \
        ((uint8_t)((key) / DUEL_CIVIC_ACTION_COUNT))
#    define INCANTATION_OCCUPATION_ACTION(key) \
        ((uint8_t)((key) % DUEL_CIVIC_ACTION_COUNT))

/* Canonical desk-space point shared by every current civic layer. Coordinates are
 * authored for the left/desk view; callers mirror x at the final draw site. */
typedef struct {
    int8_t x;
    int8_t y;
} incantation_point_t;

incantation_point_t incantation_occupation_anchor(uint8_t floor, uint8_t action);
int incantation_desk_x(bool is_left, int x);
void incantation_civic_hline(duel_fb_t *fb, bool is_left, int x0, int x1, int y);
void incantation_civic_vline(duel_fb_t *fb, bool is_left, int x, int y0, int y1);

// Fully-derived resident record for one city at one civic phase. No stored
// coordinates: the station index maps to a fixed spot in the floor band.
typedef struct {
    uint8_t personality; // DUEL_CIVIC_PERSONALITY_*
    uint8_t action;      // DUEL_CIVIC_ACTION_*
    uint8_t station;     // action-derived fixed station index
    uint8_t progress;    // 0..DUEL_CIVIC_ACTION_SLOT-1 within the current action
    uint8_t task;        // authoritative RESIDENT_* aftermath assignment
} civic_resident_t;

// Session-seeded personality for one city. Stable for a whole session.
uint8_t civic_resident_personality(uint8_t seed, bool is_left);

// Derive the resident's full state for a floor at a given civic phase. `mode`
// is DUEL_CIVIC_MODE_* (QUIET calms motion; see the draw routine).
civic_resident_t civic_resident_derive(uint8_t seed, bool is_left, uint8_t floor,
                                   uint8_t mode, uint8_t phase);

// Draw the resident into the floor band (y61-110), distinguishable from the
// static occupation anchors and from the rooftop champion. All primary motion
// keys off the resident's phase-derived state; `frame` only drives an optional
// 1 px idle sub-motion that is suppressed in QUIET mode.
void civic_resident_draw(duel_fb_t *fb, const civic_resident_t *res, bool is_left,
                       uint8_t mode, uint32_t frame);

/* Stable local-layer mark anchored through the same WORK descriptor that
 * drives the resident's ordinary interaction with the dominant floor object. */
void incantation_resident_draw_attunement(duel_fb_t *fb, bool is_left, uint8_t floor);
