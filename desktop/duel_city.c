/* Host-side city renderer; the only native-library source not in the firmware. */
#include "duel_city.h"

#include "duel_ambient.h"
#include "duel_town.h"
#include "duel_town_life.h"

#include <stddef.h>
#include <string.h>

#include "duel_civic.h"
#include "duel_diagnostics.h"
#include "duel_draw.h"
#include "duel_framebuffer.h"
#include "duel_host.h"
#include "duel_model.h"
#include "duel_proto.h"
#include "duel_render.h"
#include "duel_runtime.h"
#include "duel_view.h"

#define DUEL_CITY_PAIR_W (DUEL_CANVAS_W * 2 + DUEL_CITY_GAP_PX)

/* The floor policy is the whole of the carry-over today. It is copied in and
 * out rather than aliased through the opaque bytes, so the public struct never
 * has to promise an alignment. */
_Static_assert(sizeof(duel_floor_policy_t) <= sizeof(duel_city_state_t),
               "carry-over no longer fits the opaque city state");

/*
 * With no ambient world there is no world at all, and the renderer needs one
 * only where the shared civic derivation reads a champion's life to gate rare
 * events. An all-zero world is two standing champions and no lasting
 * aftermath, which is exactly the city's resting reading.
 */
static const sim_world_t resting_world;

int duel_city_abi_version(void) { return DUEL_CITY_ABI; }

long duel_city_wire_constant(const char *name) {
    static const struct {
        const char *name;
        long value;
    } constants[] = {
        {"REPORT_SIZE", DUEL_HOST_REPORT_SIZE},
        {"MAGIC0", DUEL_HOST_MAGIC0},
        {"MAGIC1", DUEL_HOST_MAGIC1},
        {"VERSION", DUEL_HOST_VERSION},
        {"PAYLOAD_SIZE", DUEL_HOST_PAYLOAD_SIZE},
        {"MSG_HELLO", DUEL_HOST_MSG_HELLO},
        {"MSG_HEARTBEAT", DUEL_HOST_MSG_HEARTBEAT},
        {"MSG_NOTIFY", DUEL_HOST_MSG_NOTIFY},
        {"CATEGORY_COUNT", DUEL_HOST_CATEGORY_COUNT},
        {"PRIORITY_COUNT", DUEL_HOST_PRIORITY_COUNT},
        {"SCENE_COUNT", DUEL_HOST_SCENE_COUNT},
        {"DIAG_VERSION", DUEL_HOST_DIAG_VERSION},
        {"DIAG_PAGES", DUEL_HOST_DIAG_PAGES},
        {"MSG_DIAG_REQUEST", DUEL_HOST_MSG_DIAG_REQUEST},
        {"MSG_DIAG_RESPONSE", DUEL_HOST_MSG_DIAG_RESPONSE},
        {"DIAG_FLAG_FIXED_SPLIT_CADENCE", DUEL_HOST_DIAG_FLAG_FIXED_SPLIT_CADENCE},
    };
    if (!name)
        return -1;
    /* No strcmp: the wasm build's freestanding <string.h> has only memcpy and
     * memset, and this file is compiled there too. */
    for (size_t i = 0; i < sizeof constants / sizeof constants[0]; i++) {
        const char *a = constants[i].name, *b = name;
        while (*a && *a == *b)
            a++, b++;
        if (*a == *b)
            return constants[i].value;
    }
    return -1;
}

const char *duel_city_district_name(int district) {
    static const char *const names[] = {
        "commons",     "research", "workshop", "observatory",
        "scriptorium", "studio",   "arena",    "undercroft",
    };
    _Static_assert(sizeof names / sizeof names[0] == DUEL_DISTRICT_COUNT,
                   "every district needs a name");
    if (district < 0 || district >= DUEL_DISTRICT_COUNT)
        return NULL;
    return names[district];
}

void duel_city_state_init(duel_city_state_t *state) {
    if (state)
        memset(state, 0, sizeof *state);
}

/* What one layout shows and how wide it is. The gap keeps its three columns
 * wherever both halves appear, because that width is world space rather than
 * decoration; only its shade distinguishes a desk from a night sky. */
typedef struct {
    int width;
    int height;
    bool left;
    bool right;
    bool town;
    uint8_t gap;
} city_layout_t;

static bool layout_plan(int layout, city_layout_t *plan) {
    switch (layout) {
        case DUEL_CITY_LAYOUT_DESK:
            *plan =
                (city_layout_t){DUEL_CITY_PAIR_W, DUEL_CANVAS_H, true, true, false, DUEL_CITY_DESK};
            return true;
        case DUEL_CITY_LAYOUT_CITY:
            *plan = (city_layout_t){DUEL_CITY_PAIR_W, DUEL_CANVAS_H, true, true, false,
                                    DUEL_CITY_GROUND};
            return true;
        case DUEL_CITY_LAYOUT_LEFT:
            *plan =
                (city_layout_t){DUEL_CANVAS_W, DUEL_CANVAS_H, true, false, false, DUEL_CITY_GROUND};
            return true;
        case DUEL_CITY_LAYOUT_RIGHT:
            *plan =
                (city_layout_t){DUEL_CANVAS_W, DUEL_CANVAS_H, false, true, false, DUEL_CITY_GROUND};
            return true;
        case DUEL_CITY_LAYOUT_TOWN:
            *plan = (city_layout_t){TOWN_W, TOWN_H, false, false, true, DUEL_CITY_GROUND};
            return true;
        case DUEL_CITY_LAYOUT_LANDSCAPE:
            *plan = (city_layout_t){LANDSCAPE_W, LANDSCAPE_H, false, false, true, DUEL_CITY_GROUND};
            return true;
        default:
            return false;
    }
}

int duel_city_geometry(int layout, int scale, int *width, int *height) {
    city_layout_t plan;
    if (!layout_plan(layout, &plan))
        return DUEL_CITY_ERR_LAYOUT;
    if (scale < 1 || scale > DUEL_CITY_MAX_SCALE)
        return DUEL_CITY_ERR_SCALE;
    if (width)
        *width = plan.width * scale;
    if (height)
        *height = plan.height * scale;
    return DUEL_CITY_OK;
}

/*
 * Semantics enter through the firmware's own acceptance path: the input is
 * assembled into the Raw HID greeting it would have arrived as and handed to
 * duel_host_accept. Every enum range, reserved bit, and empty-summary rule is
 * therefore the one the keyboard enforces, and the window cannot render a
 * state the firmware would have rejected. Offline runs the same expiry the
 * heartbeat timeout runs.
 */
static bool ingest(duel_host_state_t *host, const duel_city_input_t *in) {
    /* The off-keyboard fields have no wire to be checked on, so they are
     * checked here, against the same rule: each inside its enum. The three
     * tallies are counters and any byte is a count; ABI 10's host signals
     * are enums like the rest. */
    if (in->tempo >= DUEL_CITY_TEMPO_COUNT || in->spread >= DUEL_CITY_SPREAD_COUNT ||
        in->row >= DUEL_CITY_ROW_COUNT || in->row_spread >= DUEL_CITY_ROW_SPREAD_COUNT ||
        in->body >= DUEL_CITY_BODY_COUNT || in->heart >= DUEL_CITY_HEART_COUNT ||
        in->sleep >= DUEL_CITY_SLEEP_COUNT || in->season >= DUEL_CITY_SEASON_COUNT ||
        in->presence >= DUEL_CITY_PRESENCE_COUNT || in->load >= DUEL_CITY_LOAD_COUNT ||
        in->strain >= DUEL_CITY_STRAIN_COUNT || in->call >= DUEL_CITY_CALL_COUNT ||
        in->alert >= DUEL_CITY_ALERT_COUNT || in->command >= DUEL_CITY_COMMAND_COUNT)
        return false;

    duel_host_packet_t packet;
    memset(&packet, 0, sizeof packet);
    packet.magic0 = DUEL_HOST_MAGIC0;
    packet.magic1 = DUEL_HOST_MAGIC1;
    packet.version = DUEL_HOST_VERSION;
    packet.type = DUEL_HOST_MSG_HELLO;
    packet.session = 1u; /* a fresh state per call: one greeting, sequence zero */
    packet.seq = 0u;
    packet.payload_len = DUEL_HOST_PAYLOAD_LEN;
    packet.payload[0] = in->scene;
    packet.payload[1] = in->notif_count;
    packet.payload[2] = in->category;
    packet.payload[3] = in->priority;
    packet.payload[4] = in->age;
    packet.payload[5] = in->persistent;
    packet.payload[DUEL_HOST_PAYLOAD_CIVIC] = in->civic;
    packet.payload[DUEL_HOST_PAYLOAD_SECONDARY] = in->secondary;
    packet.crc = duel_crc8(&packet, offsetof(duel_host_packet_t, crc));

    memset(host, 0, sizeof *host);
    if (!duel_host_accept(host, &packet))
        return false;
    if (!in->online)
        duel_host_expire(host);
    return true;
}

/*
 * The projection the catalog builds by hand, built from host semantics and a
 * clock instead. Both halves come from this one struct by flipping is_left.
 */
static void compose(duel_render_t *r, duel_city_state_t *state, const duel_host_state_t *host,
                    const duel_city_input_t *in, duel_ambient_t *ambient, uint32_t elapsed_ms) {
    memset(r, 0, sizeof *r);

    if (ambient) {
        /* A live world: poses, spells in flight, fields, residue, and the
         * one-shot outcome flashes, all projected the way the master projects
         * its own half. */
        duel_ambient_project(ambient, r, elapsed_ms);
    } else {
        /* Nothing is simulated, so the champions rest: standing, whole, idle.
         * Leaving the view zeroed would instead read as two wizards at zero
         * health, which is a duel state and a false one. */
        for (uint8_t side = 0; side < 2u; side++) {
            r->view.wizard[side][0] = VIEW_W0_PACK(SIM_MAX_HP, 0, 0);
            r->view.wizard[side][1] = VIEW_W1_PACK(LIFE_ACTIVE, 0, 0);
            r->view.wizard[side][2] = VIEW_W2_PACK(POSE_IDLE, INC_IDLE, 0, 0);
        }
    }

    r->external = duel_host_context(host);
    r->alert = duel_host_alert(host);
    r->civic = duel_host_civic(host);
    /* Same composition as the master's own half: host activity bits plus the
     * sky phase and celestial arc step sampled from the shared sky clock. */
    r->secondary = DUEL_SECONDARY_SKY_SUB_PACK(
        DUEL_SECONDARY_SKY_PACK(duel_host_secondary(host), duel_sky_phase(elapsed_ms)),
        duel_sky_subphase(elapsed_ms));
    r->seed = in->seed;
    r->civic_phase = (uint8_t)(elapsed_ms / DUEL_CIVIC_TICK_MS);

    /* The master's derivation, over whichever world there is: a live one
     * gates rare events on a standing champion, weights them by the session's
     * diplomacy balance and lets a lasting aftermath take the shared bytes,
     * exactly as the keyboard does. The resting city has no duel, so its
     * balance is zero. */
    duel_civic_shared_t shared = duel_civic_shared_derive(
        in->seed, elapsed_ms, host, ambient ? duel_ambient_world(ambient) : &resting_world,
        ambient ? duel_ambient_diplomacy_balance(ambient) : 0);
    r->shared_pres = shared.shared_pres;
    r->revision = shared.revision;

    if (state) {
        duel_floor_policy_t floor;
        memcpy(&floor, state, sizeof floor);
        duel_floor_note_target(&floor, r->civic, r->external, elapsed_ms, DUEL_DISPLAY_ACTIVE);
        r->floor_transition = duel_floor_presentation(&floor, elapsed_ms);
        memcpy(state, &floor, sizeof floor);
    }
}

/* Scale expansion, shared by both drawing layers: one source row is built by
 * the caller's filler, then replicated `scale` times in both axes. */
typedef void (*row_filler_t)(uint8_t *row, int y, const void *source);

static void expand(const city_layout_t *plan, int scale, uint8_t *pixels, row_filler_t fill,
                   const void *source) {
    uint8_t row[LANDSCAPE_W];
    size_t stride = (size_t)plan->width * (size_t)scale;
    for (int y = 0; y < plan->height; y++) {
        fill(row, y, source);
        uint8_t *first = pixels + (size_t)y * (size_t)scale * stride;
        for (int x = 0; x < plan->width; x++)
            memset(first + (size_t)x * (size_t)scale, row[x], (size_t)scale);
        for (int copy = 1; copy < scale; copy++)
            memcpy(first + (size_t)copy * stride, first, stride);
    }
}

/* The panel layers: one framebuffer for a single tower, or left, gap, right. */
typedef struct {
    const city_layout_t *plan;
    const duel_fb_t *left;
    const duel_fb_t *right;
} panel_source_t;

static void fill_panel_row(uint8_t *row, int y, const void *source) {
    const panel_source_t *panels = source;
    const city_layout_t *plan = panels->plan;
    for (int x = 0; x < plan->width; x++) {
        const duel_fb_t *half = NULL;
        int sample = x;
        if (plan->width == DUEL_CANVAS_W) {
            half = plan->left ? panels->left : panels->right;
        } else if (x < DUEL_CANVAS_W) {
            half = panels->left;
        } else if (x >= DUEL_CANVAS_W + DUEL_CITY_GAP_PX) {
            half = panels->right;
            sample = x - DUEL_CANVAS_W - DUEL_CITY_GAP_PX;
        }
        row[x] = half == NULL                   ? plan->gap
                 : duel_fb_get(half, sample, y) ? DUEL_CITY_INK
                                                : DUEL_CITY_GROUND;
    }
}

/*
 * The residents' side of the seam. Everything the town life reads is filled
 * here, as bounded bytes, from what the renderer already has: the district,
 * mode and intensity from the host semantics; whether a spell is in flight
 * from the ambient world, read and never written; the courier from the same
 * shared derivation the panels use; and the sky phase per tick. The module
 * gets this struct and nothing else -- no projection, no host state, no world
 * -- so it cannot write the civic bytes the keyboard derives (DC-D2).
 */
_Static_assert(DUEL_TOWN_LIFE_SKY_NIGHT == DUEL_SKY_NIGHT, "the residents' night is the sky's");
_Static_assert(DUEL_TOWN_LIFE_TICK_MS == DUEL_CIVIC_TICK_MS, "the residents keep the civic clock");

int duel_city_life_advance(duel_town_life_t *life, const duel_city_input_t *input,
                           const duel_ambient_t *ambient, uint32_t elapsed_ms) {
    if (!life || !input)
        return DUEL_CITY_ERR_ARG;
    duel_host_state_t host;
    if (!ingest(&host, input))
        return DUEL_CITY_ERR_INPUT;

    uint8_t civic = duel_host_civic(&host);
    const sim_world_t *world = ambient ? duel_ambient_world(ambient) : &resting_world;
    duel_civic_shared_t shared =
        duel_civic_shared_derive(input->seed, elapsed_ms, &host, world,
                                 ambient ? duel_ambient_diplomacy_balance(ambient) : 0);

    duel_town_life_input_t in;
    memset(&in, 0, sizeof in);
    in.district = duel_civic_district(civic, duel_host_context(&host));
    in.mode = DUEL_CIVIC_MODE(civic);
    in.intensity = DUEL_CIVIC_INTENSITY(civic);
    in.spell_up = (world->spell[0].active || world->spell[1].active) ? 1u : 0u;
    in.courier = (shared.revision & INCANTATION_AFTERMATH_WIRE)
                     ? (uint8_t)DUEL_CIVIC_COURIER_NONE
                     : DUEL_VISITOR_KIND(shared.shared_pres);

    /* A time behind the last tick re-derives from the seed (the module does
     * that on any backward call); then one tick at a time, each with the sky
     * of its own moment, so one long call is the same as many short ones. */
    uint32_t due = elapsed_ms / DUEL_TOWN_LIFE_TICK_MS;
    uint32_t at = duel_town_life_ticks(life);
    if (due < at) {
        duel_town_life_advance(life, &in, 0u);
        at = 0u;
    }
    uint32_t ran = 0u;
    for (; at < due; at++, ran++) {
        in.sky_phase = duel_sky_phase(at * DUEL_TOWN_LIFE_TICK_MS);
        duel_town_life_advance(life, &in, (at + 1u) * DUEL_TOWN_LIFE_TICK_MS);
    }
    return (int)ran;
}

static void fill_town_row(uint8_t *row, int y, const void *source) {
    const town_fb_t *town = source;
    for (int x = 0; x < town->width; x++)
        row[x] = town_fb_get(town, x, y) ? DUEL_CITY_INK : DUEL_CITY_GROUND;
}

int duel_city_render(duel_city_state_t *state, const duel_city_input_t *input,
                     duel_ambient_t *ambient, const duel_town_life_t *life, uint32_t elapsed_ms,
                     uint32_t frame, int layout, int scale, uint8_t *pixels, size_t length) {
    if (!input || !pixels)
        return DUEL_CITY_ERR_ARG;
    city_layout_t plan;
    if (!layout_plan(layout, &plan))
        return DUEL_CITY_ERR_LAYOUT;
    int width, height;
    int geometry = duel_city_geometry(layout, scale, &width, &height);
    if (geometry != DUEL_CITY_OK)
        return geometry;
    if (length < (size_t)width * (size_t)height)
        return DUEL_CITY_ERR_BUFFER;

    duel_host_state_t host;
    if (!ingest(&host, input))
        return DUEL_CITY_ERR_INPUT;

    duel_render_t render;
    compose(&render, state, &host, input, ambient, elapsed_ms);

    if (plan.town) {
        /* The town layers draw the same projection into their own surfaces. */
        town_fb_t town;
        town_fb_clear(&town, plan.width, plan.height);
        town_typing_t typing = {input->tempo, input->spread, input->row, input->row_spread};
        town_health_t health = {input->body, input->heart, input->sleep};
        town_day_t day = {input->tally_casts, input->tally_impacts, input->tally_knockdowns};
        town_signals_t signals = {input->strain};
        duel_town_draw(&town, &render, &typing, &health, &day, &signals, life, frame);
        expand(&plan, scale, pixels, fill_town_row, &town);
        return DUEL_CITY_OK;
    }

    /* Both halves come from this one projection by flipping is_left, so a
     * layout showing one tower simply never draws the other. */
    duel_fb_t left, right;
    duel_fb_clear(&left);
    duel_fb_clear(&right);
    if (plan.left)
        duel_scene_draw(&left, &render, true, frame, false);
    if (plan.right)
        duel_scene_draw(&right, &render, false, frame, false);
    panel_source_t panels = {&plan, &left, &right};
    expand(&plan, scale, pixels, fill_panel_row, &panels);
    return DUEL_CITY_OK;
}

/* ---- presentation policy -------------------------------------------------
 * These live here, not in the shell, because each is a decision every future
 * shell would otherwise repeat. A shell's whole job should be: hand in
 * semantics, take pixels, put them on a screen, keep a clock. */

/* The desk around the panels reads a shade darker than the gap between them,
 * so the eye finds the canvases before it finds the furniture. */
#define DUEL_CITY_SURROUND_DESK 48

int duel_city_default_scale(int layout) {
    int height;
    int geometry = duel_city_geometry(layout, 1, NULL, &height);
    if (geometry != DUEL_CITY_OK)
        return geometry;
    int scale = DUEL_CITY_TARGET_HEIGHT / height;
    return scale < 1 ? 1 : scale;
}

int duel_city_fit_scale(int layout, int width, int height) {
    int base_w, base_h;
    int geometry = duel_city_geometry(layout, 1, &base_w, &base_h);
    if (geometry != DUEL_CITY_OK)
        return geometry;
    if (width < 1 || height < 1)
        return DUEL_CITY_ERR_ARG;
    int by_width = width / base_w;
    int by_height = height / base_h;
    int scale = by_width < by_height ? by_width : by_height;
    return scale < 1 ? 1 : scale;
}

int duel_city_backdrop(int layout) {
    city_layout_t plan;
    if (!layout_plan(layout, &plan))
        return DUEL_CITY_ERR_LAYOUT;
    return layout == DUEL_CITY_LAYOUT_DESK ? DUEL_CITY_SURROUND_DESK : DUEL_CITY_GROUND;
}

/* The simulation's own tick, not the slower civic clock: a redraw that
 * sampled at the civic cadence would drop three frames of every four in a
 * spell's flight. */
uint32_t duel_city_frame_interval_ms(void) { return (uint32_t)SIM_TICK_MS; }

/*
 * One second of frames at that interval.
 *
 * Two measurements set it. The floor slide converges in about ten frames --
 * 29 pixels of 8576 at the arrival frame without a run-up. The outcome flash
 * takes twelve frames to count itself out, and the first warmed frame arms
 * one by construction, because a policy that has seen no sequence number yet
 * sees every sequence number as a change. Twenty-five clears the larger of
 * the two with margin and costs about a millisecond.
 */
int duel_city_seek_warm_frames(void) { return 25; }

typedef struct {
    uint8_t scene;
    uint8_t floor;
    uint8_t mode;
    uint8_t intensity;
    uint8_t secondary;
} city_tour_stop_t;

static const city_tour_stop_t city_tour[] = {
    /* Commons */
    {DUEL_HOST_SCENE_DUEL, DUEL_CIVIC_FLOOR_COMMONS, DUEL_CIVIC_MODE_NORMAL,
     DUEL_CIVIC_INTENSITY_CALM, DUEL_CIVIC_SECONDARY_NONE},
    /* Scriptorium */
    {DUEL_HOST_SCENE_DUEL, DUEL_CIVIC_FLOOR_RESEARCH, DUEL_CIVIC_MODE_NORMAL,
     DUEL_CIVIC_INTENSITY_ACTIVE, DUEL_CIVIC_SECONDARY_PAGE},
    /* Workshop */
    {DUEL_HOST_SCENE_DUEL, DUEL_CIVIC_FLOOR_WORKSHOP, DUEL_CIVIC_MODE_NORMAL,
     DUEL_CIVIC_INTENSITY_BUSY, DUEL_CIVIC_SECONDARY_NONE},
    /* Observatory */
    {DUEL_HOST_SCENE_FOCUS, DUEL_CIVIC_FLOOR_SPECIAL, DUEL_CIVIC_MODE_QUIET,
     DUEL_CIVIC_INTENSITY_CALM, DUEL_CIVIC_SECONDARY_CALENDAR},
    /* Arena */
    {DUEL_HOST_SCENE_REVEL, DUEL_CIVIC_FLOOR_COMMONS, DUEL_CIVIC_MODE_NORMAL,
     DUEL_CIVIC_INTENSITY_SATURATED, DUEL_CIVIC_SECONDARY_MEDIA},
};

int duel_city_tour_length(void) { return (int)(sizeof city_tour / sizeof city_tour[0]); }

int duel_city_tour_stop(int index, uint8_t seed, duel_city_input_t *out) {
    if (!out)
        return DUEL_CITY_ERR_ARG;
    int length = duel_city_tour_length();
    if (index < 0)
        return DUEL_CITY_ERR_ARG;
    const city_tour_stop_t *stop = &city_tour[index % length];
    memset(out, 0, sizeof *out);
    out->scene = stop->scene;
    out->civic = DUEL_CIVIC_PACK(stop->floor, stop->mode, stop->intensity);
    out->secondary = DUEL_SECONDARY_PACK(stop->secondary);
    out->online = 1u;
    out->seed = seed;
    return DUEL_CITY_OK;
}
