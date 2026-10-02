/*
 * duel_town_life.h -- the desktop town's residents: needs, places and paths.
 *
 * Desktop only (owner ruling DC-D2). Twelve residents with four needs each
 * walk between twelve places on a small graph, in caller-owned opaque storage
 * (duel_town_life_t, in duel_city.h, beside duel_ambient_t). The module reads
 * one small struct of bounded bytes that the city glue fills from the host
 * semantics, the sky clock and the ambient world. It never sees a
 * duel_render_t, a duel_host_state_t or a sim_world_t, so it cannot write the
 * civic, shared_pres or revision bytes the keyboard derives: that derivation
 * is upstream of it and nothing flows back. It is never compiled into the
 * firmware.
 *
 * Portable C11: integer arithmetic only, fixed ticks, no allocation, no
 * clock reads. memset is the only library call. Prototyped and measured in
 * the DC-S2 spike (docs/dags/spikes/dc-s2-town-life.md).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "duel_city.h"

/* The pool and the clock, stated once. The tick is the civic clock the
 * plaza's walkers already moved on (DUEL_CIVIC_TICK_MS). */
#define DUEL_TOWN_LIFE_TICK_MS   300u
#define DUEL_TOWN_LIFE_RESIDENTS 12u
#define DUEL_TOWN_LIFE_PLACES    12u
#define DUEL_TOWN_LIFE_NEEDS     4u

/* The sky phase the module reads as night: DUEL_SKY_NIGHT, which the glue
 * asserts so the module needs no runtime header. */
#define DUEL_TOWN_LIFE_SKY_NIGHT 3u

enum {
    DUEL_TOWN_NEED_REST = 0,
    DUEL_TOWN_NEED_WORK,
    DUEL_TOWN_NEED_FOOD,
    DUEL_TOWN_NEED_COMPANY,
};

enum {
    DUEL_TOWN_RES_IDLE = 0, /* standing at a place, deciding */
    DUEL_TOWN_RES_WALK,     /* on a path toward `goal` */
    DUEL_TOWN_RES_STAY,     /* at `place`, satisfying a need; hidden if inside */
    DUEL_TOWN_RES_WATCH,    /* stopped, looking up at the duel */
};

enum {
    DUEL_TOWN_PLACE_TOWER = 0,
    DUEL_TOWN_PLACE_HOUSE_W,
    DUEL_TOWN_PLACE_HOUSE_M,
    DUEL_TOWN_PLACE_SMITHY,
    DUEL_TOWN_PLACE_TAVERN,
    DUEL_TOWN_PLACE_HOUSE_E,
    DUEL_TOWN_PLACE_WELL,
    DUEL_TOWN_PLACE_MARKET,
    DUEL_TOWN_PLACE_BENCH,
    DUEL_TOWN_PLACE_GATE_W,
    DUEL_TOWN_PLACE_GATE_E,
    DUEL_TOWN_PLACE_PLAZA,
};

/*
 * The near row of houses, stated once for the drawing and for the places
 * whose doors it has. Each is (x0, width, height, chimney, roof, sign) in
 * the square's 256 columns, as town_building_t reads them; a place at a
 * house stands at that house's door column. The drawing builds near_row[]
 * from these and the module builds its place table from the same macros, so
 * a house cannot move without its door moving with it.
 */
#define TOWN_ROW_HOUSE_W               12, 42, 40, 9, 0, 1
#define TOWN_ROW_HOUSE_M               58, 28, 27, 0, 1, 0
#define TOWN_ROW_SMITHY                88, 18, 20, 0, 2, 0
#define TOWN_ROW_TAVERN                166, 33, 33, 24, 0, 1
#define TOWN_ROW_HOUSE_E               203, 41, 46, 8, 1, 0
#define TOWN_ROW_APPLY(f, ...)         f(__VA_ARGS__)
#define TOWN_ROW_DOOR_(x0, width, ...) ((int16_t)((x0) + (width) / 2))
#define TOWN_ROW_DOOR(row)             TOWN_ROW_APPLY(TOWN_ROW_DOOR_, row)

/*
 * What the town life reads: bounded enums the glue already has. Zero is
 * "none" everywhere. The reserved bytes are for DC-D3's tallies and DC-D4's
 * season, should they reach the residents.
 */
typedef struct {
    uint8_t district;  /* DUEL_DISTRICT_* (0..7) */
    uint8_t mode;      /* DUEL_CIVIC_MODE_* */
    uint8_t intensity; /* DUEL_CIVIC_INTENSITY_* */
    uint8_t sky_phase; /* DUEL_SKY_* (0..3) for the tick being run */
    uint8_t spell_up;  /* 0 or 1: a spell is in flight on either side */
    uint8_t courier;   /* DUEL_CIVIC_COURIER_*; carried, not yet acted on */
    uint8_t reserved[2];
} duel_town_life_input_t;

/* A read-only view of one resident, copied out of the opaque state. */
typedef struct {
    int16_t x;           /* square x (TOWN_W space; the gates sit outside 0..255) */
    int16_t y;           /* rows below the plaza's ground line (14..47) */
    uint8_t state;       /* DUEL_TOWN_RES_* */
    uint8_t place;       /* the place stood at or last left */
    uint8_t goal;        /* where the path leads */
    uint8_t personality; /* DUEL_CIVIC_PERSONALITY_* (0..4) */
    uint8_t need_top;    /* the need driving the current goal */
    uint8_t need_level;  /* its level, 0..255 */
    uint8_t visible;     /* 0 when inside a house or out of a gate */
    uint8_t facing;      /* 0 left, 1 right */
} duel_town_life_view_t;

/* duel_town_life_init and duel_town_life_ticks are declared in duel_city.h,
 * where the shells reach them. */

/*
 * Run every tick from the last one run up to now_ms / DUEL_TOWN_LIFE_TICK_MS,
 * with `in` applied to all of them. A now_ms behind the current tick
 * re-derives from the seed first, so a backward seek is exact. Returns the
 * number of ticks run. The city glue calls this one tick at a time so it can
 * give each tick its own sky (duel_city_life_advance).
 */
uint32_t duel_town_life_advance(duel_town_life_t *life, const duel_town_life_input_t *in,
                                uint32_t now_ms);

bool duel_town_life_resident(const duel_town_life_t *life, uint8_t index,
                             duel_town_life_view_t *out);

/* How many residents are stood at or inside `place` this tick. */
uint8_t duel_town_life_occupancy(const duel_town_life_t *life, uint8_t place);

/* Where a place is, for a drawing layer. Returns false past the last one. */
bool duel_town_life_place(uint8_t place, int16_t *x, int16_t *y, uint8_t *serves, bool *inside);
