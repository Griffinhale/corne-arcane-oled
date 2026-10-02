/*
 * duel_city.h — off-keyboard city renderer.
 *
 * One entry point for a host window on a machine whose keyboard has no
 * displays. The caller supplies the same bounded enums the daemon already
 * sends over Raw HID and gets both 32x128 canvases back as 8-bit grey pixels.
 *
 * There is no world behind the frame. The city is a pure function of host
 * semantics and a clock, exactly as the visual catalog renders a hand-built
 * projection: no tick runs, no state is authoritative, and nothing here can
 * contradict the one-authoritative-master rule.
 *
 * Deliberately absent: the duel. On the keyboard, key positions never leave
 * the firmware. Sampling them on a desktop is exactly the access this project
 * refuses, so this renderer reads no input and offers no way to supply any.
 *
 * Portable C. This file and its implementation are the only pieces of the
 * native library that are not already compiled into the firmware.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define DUEL_CITY_ABI 8

/* The three columns between the two canvases are world space that neither
 * panel can show: the battlefield axis crosses them (DUEL_U_GAP_* in
 * duel_combat_draw.c) and nothing is ever drawn there. Their width is
 * therefore part of the geometry and is kept in every layout that shows both
 * halves; only their colour changes. */
#define DUEL_CITY_GAP_PX 3
#define DUEL_CITY_GROUND 0u   /* unlit pixel */
#define DUEL_CITY_INK    255u /* lit pixel */
#define DUEL_CITY_DESK   96u  /* the desk showing between two panels */

/*
 * What the window is a window onto.
 *
 * DESK is the keyboard: two panels with the desk visible between them, the
 * geometry the golden review sheets use. CITY is the same world with the desk
 * unlit, so it reads as one continuous scene rather than two screens. LEFT and
 * RIGHT are a single tower, for a window that wants one subject.
 *
 * Those four are all the same pixels, reframed. Every coordinate behind them
 * is written against a 32x128 canvas, so 64x128 of drawn pixels plus the gap
 * is the whole of what they can show.
 *
 * TOWN and LANDSCAPE are not reframings. They are drawing layers of their own:
 * one wizard tower at the centre of a small city, with the same world read
 * into compositions the panels have no room for. TOWN is the 256x256 square;
 * LANDSCAPE is a 400x240 wide view with more world on either side rather than
 * a stretched or cropped town. They share the state, not the pixels.
 */
enum {
    DUEL_CITY_LAYOUT_DESK = 0,
    DUEL_CITY_LAYOUT_CITY = 1,
    DUEL_CITY_LAYOUT_LEFT = 2,
    DUEL_CITY_LAYOUT_RIGHT = 3,
    DUEL_CITY_LAYOUT_TOWN = 4,
    DUEL_CITY_LAYOUT_LANDSCAPE = 5,
    DUEL_CITY_LAYOUT_COUNT = 6,
};

enum {
    DUEL_CITY_OK = 0,
    DUEL_CITY_ERR_ARG = -1,    /* null pointer where one is required */
    DUEL_CITY_ERR_SCALE = -2,  /* scale outside 1..DUEL_CITY_MAX_SCALE */
    DUEL_CITY_ERR_BUFFER = -3, /* pixel buffer shorter than the geometry */
    DUEL_CITY_ERR_INPUT = -4,  /* a field outside its enum or bit width */
    DUEL_CITY_ERR_LAYOUT = -5, /* layout outside DUEL_CITY_LAYOUT_* */
};

#define DUEL_CITY_MAX_SCALE 16

/*
 * Signals from off the keyboard, for shells that have them: a typing summary
 * from the opt-in helper on a desktop whose keyboard lacks this firmware
 * (docs/typing-summary.md), and reduced health buckets on a watch. Each is a
 * small enum whose zero means "none", and none is what every shell that has no
 * such signal sends. They never reach the keyboard: Raw HID v3 and split v13
 * have no room for them and do not carry them. Values are never renumbered.
 */
/* Typing tempo: the mean gap inside a burst, per the summary's cut points. */
enum {
    DUEL_CITY_TEMPO_NONE = 0,
    DUEL_CITY_TEMPO_DELIBERATE = 1,
    DUEL_CITY_TEMPO_FLOWING = 2,
    DUEL_CITY_TEMPO_RAPID = 3,
    DUEL_CITY_TEMPO_FRANTIC = 4,
    DUEL_CITY_TEMPO_COUNT = 5,
};

/* Typing spread: how even the gaps inside a burst are. */
enum {
    DUEL_CITY_SPREAD_NONE = 0,
    DUEL_CITY_SPREAD_STEADY = 1,
    DUEL_CITY_SPREAD_VARIED = 2,
    DUEL_CITY_SPREAD_IRREGULAR = 3,
    DUEL_CITY_SPREAD_COUNT = 4,
};

/* The keyboard row with the most keydowns. */
enum {
    DUEL_CITY_ROW_NONE = 0,
    DUEL_CITY_ROW_TOP = 1,
    DUEL_CITY_ROW_HOME = 2,
    DUEL_CITY_ROW_BOTTOM = 3,
    DUEL_CITY_ROW_THUMB = 4,
    DUEL_CITY_ROW_COUNT = 5,
};

/* How much of the typing that top row holds. */
enum {
    DUEL_CITY_ROW_SPREAD_NONE = 0,
    DUEL_CITY_ROW_SPREAD_FOCUSED = 1,
    DUEL_CITY_ROW_SPREAD_MIXED = 2,
    DUEL_CITY_ROW_SPREAD_EVEN = 3,
    DUEL_CITY_ROW_SPREAD_COUNT = 4,
};

/* Body activity today: activity rings closed, or a step band without them. */
enum {
    DUEL_CITY_BODY_NONE = 0,
    DUEL_CITY_BODY_RESTING = 1,
    DUEL_CITY_BODY_STIRRING = 2,
    DUEL_CITY_BODY_MOVING = 3,
    DUEL_CITY_BODY_FULL = 4,
    DUEL_CITY_BODY_COUNT = 5,
};

/* Heart rate as a mood, never a reading: no value means good or bad. */
enum {
    DUEL_CITY_HEART_NONE = 0,
    DUEL_CITY_HEART_STILL = 1,
    DUEL_CITY_HEART_LIVELY = 2,
    DUEL_CITY_HEART_COUNT = 3,
};

/* Last night's sleep, as a mood for the next day. */
enum {
    DUEL_CITY_SLEEP_NONE = 0,
    DUEL_CITY_SLEEP_RESTED = 1,
    DUEL_CITY_SLEEP_TIRED = 2,
    DUEL_CITY_SLEEP_COUNT = 3,
};

/*
 * The Raw HID v3 semantic payload, unpacked, plus the two values the firmware
 * supplies locally rather than receiving, plus the off-keyboard signals above.
 * Every field is a bounded integer: no window title, URL, path, notification
 * text, keycode, timestamp or raw sample can reach this struct, and there is no
 * field one could be smuggled through. The off-keyboard fields follow the
 * first ten bytes so those keep their offsets.
 */
typedef struct {
    uint8_t scene;       /* payload[0]: DUEL_HOST_SCENE_* */
    uint8_t notif_count; /* payload[1]: 0..15 */
    uint8_t category;    /* payload[2]: DUEL_HOST_CATEGORY_* */
    uint8_t priority;    /* payload[3]: DUEL_HOST_PRIORITY_* */
    uint8_t age;         /* payload[4]: 0..7 */
    uint8_t persistent;  /* payload[5]: 0 or 1 */
    uint8_t civic;       /* payload[6]: DUEL_CIVIC_PACK(floor, mode, intensity) */
    uint8_t secondary;   /* payload[7]: DUEL_SECONDARY_PACK(activity) */
    uint8_t online;      /* daemon link state, as the firmware's host state sees it */
    uint8_t seed;        /* presentation seed: the firmware's one-byte session */
    uint8_t tempo;       /* DUEL_CITY_TEMPO_* */
    uint8_t spread;      /* DUEL_CITY_SPREAD_* */
    uint8_t row;         /* DUEL_CITY_ROW_* */
    uint8_t row_spread;  /* DUEL_CITY_ROW_SPREAD_* */
    uint8_t body;        /* DUEL_CITY_BODY_* */
    uint8_t heart;       /* DUEL_CITY_HEART_* */
    uint8_t sleep;       /* DUEL_CITY_SLEEP_* */
} duel_city_input_t;

_Static_assert(sizeof(duel_city_input_t) == 17, "city input layout changed");

/*
 * Opaque carry-over between frames — currently the floor-transition policy,
 * which is what makes the tower slide between floors instead of snapping. The
 * caller owns the storage, so the library never allocates. Passing NULL is
 * legal and renders every frame as if the floor had always been the current
 * one.
 */
typedef struct {
    uint64_t opaque[4];
} duel_city_state_t;

void duel_city_state_init(duel_city_state_t *state);

/*
 * An autonomous world: the same deterministic duel simulation the firmware
 * runs, driven by a caster that fabricates its own key positions. Nothing is
 * sampled from anywhere — the desktop has no input at all, and this is what
 * makes the city alive rather than a still frame.
 *
 * Opaque and caller-owned, like the state above, so the library still never
 * allocates. It holds a whole simulation world, so it is large.
 */
typedef struct {
    uint64_t opaque[64];
} duel_ambient_t;

typedef struct {
    uint32_t ticks;      /* simulation ticks run */
    uint32_t casts;      /* spells launched */
    uint32_t impacts;    /* health lost, in pips */
    uint32_t knockdowns; /* champions felled; the roster replaces them */
} duel_ambient_stats_t;

void duel_ambient_init(duel_ambient_t *ambient, uint8_t seed);

/* Advance the world to `now_ms` at the firmware's own 25 Hz cadence, with the
 * same bounded catch-up: a long stall resynchronises instead of replaying
 * unbounded history. Returns the number of ticks run. */
uint8_t duel_ambient_advance(duel_ambient_t *ambient, uint32_t now_ms);

/* Evidence that the city is alive, for tests and for watching it run. */
duel_ambient_stats_t duel_ambient_stats(const duel_ambient_t *ambient);

int duel_city_abi_version(void);

/* A wire constant from duel_host.h or duel_diagnostics.h, by its name without
 * the DUEL_HOST_ prefix ("VERSION", "MSG_HELLO", ...), or -1 for an unknown
 * name. The Python host restates these; its tests read them from here so a
 * change on one side cannot pass unnoticed. Not used by any renderer. */
long duel_city_wire_constant(const char *name);

/* The lowercase name of DUEL_DISTRICT_* `district`, as the visual catalog names
 * its cases ("commons", ...), or NULL past the last one. Tools read the list
 * from here instead of restating it. Not used by any renderer. */
const char *duel_city_district_name(int district);

/* Pixel size of one rendered image in this layout at this scale. Returns an
 * error code without touching the outputs if either argument is out of
 * range. */
int duel_city_geometry(int layout, int scale, int *width, int *height);

/*
 * Presentation policy, kept here rather than in whichever shell is showing the
 * window. Every one of these is a decision a second shell would otherwise have
 * to make identically or drift on, so they belong to the renderer. All four
 * return a negative DUEL_CITY_ERR_* for a layout outside the enum.
 */

/* The scale to use when nobody asked for one: the largest whole-pixel scale
 * that keeps the image near DUEL_CITY_TARGET_HEIGHT. The panels are 128 tall,
 * the square town 256 and the landscape 240, so the same rule suits all. */
#define DUEL_CITY_TARGET_HEIGHT 512
int duel_city_default_scale(int layout);

/* The largest whole-pixel scale fitting a window this size. Never below 1: a
 * window smaller than the world overflows rather than rendering a fraction of
 * a pixel. */
int duel_city_fit_scale(int layout, int width, int height);

/* The grey a shell should paint around the image, as an 8-bit level. The desk
 * layout sits on desk grey because the desk continues past the panels;
 * everything else sits on unlit ground. */
int duel_city_backdrop(int layout);

/* The world's own cadence, so a shell's redraw timer is derived from the
 * simulation rather than guessed alongside it. */
uint32_t duel_city_frame_interval_ms(void);

/*
 * How much of a run-up a shell that seeks has to render, and not merely
 * simulate, before the frame it actually means to show.
 *
 * Two policies carry between frames instead of living in the world: the floor
 * transition that slides the tower between storeys, and the outcome flash,
 * which arms on a change in the view's sequence number. A shell that jumps
 * straight to a moment composes both from a standing start -- the slide
 * begins where it should already have finished, and the first frame after the
 * jump reads a sequence number it has never seen before and draws an impact
 * burst for something that may have happened a quarter of an hour ago. A
 * shell that renders once a minute would draw that burst every single time.
 *
 * Rendering this many frames before the target settles both, so it is policy
 * rather than an implementation detail of whichever shell noticed first. Warm
 * through the cheapest layout: the two policies are composed before any
 * layout-specific drawing, so any layout settles them and
 * DUEL_CITY_LAYOUT_LEFT is a quarter of the pixels of the town.
 */
int duel_city_seek_warm_frames(void);

/*
 * A tour of the city with no daemon attached: every civic floor once, so that
 * running it proves the tower actually changes rooms. Districts that share a
 * floor draw the same room and differ only in host scene, so the tour selects
 * across floors rather than across district names -- the trap anything
 * choosing scenery by district name falls into.
 */
int duel_city_tour_length(void);
int duel_city_tour_stop(int index, uint8_t seed, duel_city_input_t *out);

/*
 * Render both halves into `pixels`, 8-bit grey, row-major, no padding.
 *
 * `elapsed_ms` is the window's own uptime and drives everything the firmware
 * derives from its clock: the sky phase, the celestial arc step, and the civic
 * tick that paces residents and couriers. `frame` is the animation phase and
 * belongs to the caller's redraw cadence.
 *
 * `ambient` is optional. With it, the champions duel and the city reacts to
 * what they do; without it, they rest and only the host's semantics move.
 */
int duel_city_render(duel_city_state_t *state, const duel_city_input_t *input,
                     duel_ambient_t *ambient, uint32_t elapsed_ms, uint32_t frame, int layout,
                     int scale, uint8_t *pixels, size_t length);
