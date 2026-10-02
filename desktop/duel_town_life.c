/*
 * duel_town_life.c -- the desktop town's residents. See the header.
 *
 * Twelve residents, four needs each, twelve places joined by a small graph.
 * Each tick every resident's needs drift, the one with nothing to do picks
 * the need that hurts most and the nearest place that serves it with room
 * left, walks there a pixel or two per tick along breadth-first hops over the
 * graph, stays until the need is met, and decides again. Night sends people
 * home, a quiet mode empties the square, a spell in flight makes the curious
 * stop and look and the nervous go indoors, and the active district nominates
 * a place the town leans toward.
 *
 * Nothing here reads a clock, allocates, or touches a float. The only library
 * call is memset, so the file compiles freestanding for the wasm leg.
 */
#include "duel_town_life.h"

#include <string.h>

/* The civic enums the input is expressed in (personalities in duel_civic.h,
 * districts, modes and intensities in duel_host.h). Values only; nothing from
 * either header is called. */
#include "duel_civic.h"
#include "duel_host.h"

enum { TL_HOME_NEED = DUEL_TOWN_NEED_REST };

typedef struct {
    int16_t x;
    int16_t y;
    uint8_t needs[DUEL_TOWN_LIFE_NEEDS];
    uint8_t place;
    uint8_t goal;
    uint8_t next;
    uint8_t state;
    uint8_t timer;
    uint8_t personality;
    uint8_t need_top;
    uint8_t facing;
} tl_resident_t;

/* No padding anywhere: every field is at its natural alignment, so the bytes
 * of the handle are the same on every target the shells build for, and the
 * parity legs can compare them directly. */
typedef struct {
    tl_resident_t resident[DUEL_TOWN_LIFE_RESIDENTS];
    uint8_t occupancy[DUEL_TOWN_LIFE_PLACES];
    uint32_t prng;
    uint32_t tick;
    uint8_t seed;
    uint8_t pad[3];
} tl_state_t;

_Static_assert(sizeof(tl_resident_t) == 16, "a resident is sixteen bytes on every target");
_Static_assert(sizeof(tl_state_t) <= sizeof(duel_town_life_t),
               "the town life no longer fits its opaque handle");

/* ---- the town's places, stated once --------------------------------------
 * x in TOWN_W space (the square's 256 columns; the drawing maps them onto
 * either composition), y as rows below the ground line, inside the
 * forty-eight-row plaza the walkers cross. The five places at a house stand
 * at its door column, read from the same TOWN_ROW_* the drawing builds the
 * near row from. The tower and the open plaza stand to the right of the well
 * on the tower's axis (x 117-139, rows 2-33), so nobody stands on it; the
 * drawing puts a bench at the bench. */
typedef struct {
    int16_t x;
    int16_t y;
    uint8_t serves;   /* bitmask of DUEL_TOWN_NEED_* */
    uint8_t capacity; /* residents at once */
    uint8_t inside;   /* a resident here is hidden */
    uint8_t adj[4];   /* neighbours, 0xFF padded */
} tl_place_t;

#define N_(need) ((uint8_t)(1u << (need)))
#define NONE     0xFFu

static const tl_place_t places[DUEL_TOWN_LIFE_PLACES] = {
    /* TOWER   */ {148, 14, N_(DUEL_TOWN_NEED_WORK), 3, 0, {3, 4, 11, NONE}},
    /* HOUSE_W */
    {TOWN_ROW_DOOR(TOWN_ROW_HOUSE_W), 16, N_(DUEL_TOWN_NEED_REST), 4, 1, {2, 8, 9, NONE}},
    /* HOUSE_M */
    {TOWN_ROW_DOOR(TOWN_ROW_HOUSE_M), 16, N_(DUEL_TOWN_NEED_REST), 4, 1, {1, 3, 8, NONE}},
    /* SMITHY  */
    {TOWN_ROW_DOOR(TOWN_ROW_SMITHY), 16, N_(DUEL_TOWN_NEED_WORK), 2, 0, {2, 0, 6, NONE}},
    /* TAVERN  */
    {TOWN_ROW_DOOR(TOWN_ROW_TAVERN),
     18,
     N_(DUEL_TOWN_NEED_FOOD) | N_(DUEL_TOWN_NEED_COMPANY),
     4,
     0,
     {0, 5, 7, NONE}},
    /* HOUSE_E */
    {TOWN_ROW_DOOR(TOWN_ROW_HOUSE_E), 16, N_(DUEL_TOWN_NEED_REST), 4, 1, {4, 10, NONE, NONE}},
    /* WELL    */ {100, 36, N_(DUEL_TOWN_NEED_FOOD), 2, 0, {3, 8, 11, NONE}},
    /* MARKET  */
    {160, 30, N_(DUEL_TOWN_NEED_FOOD) | N_(DUEL_TOWN_NEED_COMPANY), 3, 0, {4, 10, 11, NONE}},
    /* BENCH   */ {60, 44, N_(DUEL_TOWN_NEED_COMPANY), 2, 0, {1, 2, 6, 9}},
    /* GATE_W  */ {-16, 40, N_(DUEL_TOWN_NEED_WORK), 8, 1, {1, 8, NONE, NONE}},
    /* GATE_E  */ {272, 40, N_(DUEL_TOWN_NEED_WORK), 8, 1, {5, 7, NONE, NONE}},
    /* PLAZA   */ {152, 40, N_(DUEL_TOWN_NEED_COMPANY), 6, 0, {0, 6, 7, NONE}},
};

/* The place the active district leans the town toward. */
static uint8_t district_place(uint8_t district) {
    switch (district) {
        case DUEL_DISTRICT_WORKSHOP:
            return DUEL_TOWN_PLACE_SMITHY;
        case DUEL_DISTRICT_RESEARCH:
        case DUEL_DISTRICT_SCRIPTORIUM:
        case DUEL_DISTRICT_OBSERVATORY:
            return DUEL_TOWN_PLACE_TOWER;
        case DUEL_DISTRICT_ARENA:
            return DUEL_TOWN_PLACE_PLAZA;
        case DUEL_DISTRICT_STUDIO:
        case DUEL_DISTRICT_UNDERCROFT:
            return DUEL_TOWN_PLACE_MARKET;
        default:
            return DUEL_TOWN_PLACE_TAVERN;
    }
}

/* ---- deterministic plumbing ------------------------------------------- */

static tl_state_t *mutable_state(duel_town_life_t *life) { return (tl_state_t *)(void *)life; }
static const tl_state_t *readable_state(const duel_town_life_t *life) {
    return (const tl_state_t *)(const void *)life;
}

/* xorshift32, as duel_ambient.c uses: deterministic, never zero. */
static uint32_t next_random(tl_state_t *s) {
    uint32_t x = s->prng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s->prng = x;
    return x;
}

static uint8_t sat_add(uint8_t v, uint8_t d) {
    return (uint8_t)((unsigned)v + d > 255u ? 255u : (unsigned)v + d);
}
static uint8_t sat_sub(uint8_t v, uint8_t d) { return (uint8_t)(v < d ? 0u : (unsigned)v - d); }

/*
 * Breadth-first distances over the place graph from `from`. Twelve nodes and
 * at most four neighbours each, so the whole search is a few dozen steps and
 * the state stores no path: only the next hop is kept per resident, and the
 * search is repeated at every waypoint.
 */
static void bfs(uint8_t from, uint8_t dist[DUEL_TOWN_LIFE_PLACES],
                uint8_t parent[DUEL_TOWN_LIFE_PLACES]) {
    uint8_t queue[DUEL_TOWN_LIFE_PLACES];
    uint8_t head = 0, tail = 0;
    for (uint8_t i = 0; i < DUEL_TOWN_LIFE_PLACES; i++) {
        dist[i] = NONE;
        parent[i] = NONE;
    }
    dist[from] = 0;
    queue[tail++] = from;
    while (head < tail) {
        uint8_t node = queue[head++];
        for (uint8_t k = 0; k < 4u; k++) {
            uint8_t n = places[node].adj[k];
            if (n == NONE || dist[n] != NONE)
                continue;
            dist[n] = (uint8_t)(dist[node] + 1u);
            parent[n] = node;
            queue[tail++] = n;
        }
    }
}

/* The first hop from `from` toward `to`: BFS rooted at the goal, read back. */
static uint8_t next_hop(uint8_t from, uint8_t to) {
    uint8_t dist[DUEL_TOWN_LIFE_PLACES], parent[DUEL_TOWN_LIFE_PLACES];
    if (from == to)
        return to;
    bfs(to, dist, parent);
    return parent[from] == NONE ? to : parent[from];
}

static void recount(tl_state_t *s) {
    memset(s->occupancy, 0, sizeof s->occupancy);
    for (uint8_t i = 0; i < DUEL_TOWN_LIFE_RESIDENTS; i++) {
        const tl_resident_t *r = &s->resident[i];
        if (r->state == DUEL_TOWN_RES_STAY || r->state == DUEL_TOWN_RES_IDLE)
            s->occupancy[r->place]++;
    }
}

static void reset(duel_town_life_t *life, uint8_t seed) {
    /* Carried over from DC-S2, where clearing only the state let stack
     * garbage into the handle's tail and the seek checks failed at -O2. */
    /* The whole handle, not just the state it holds: the trailing bytes are
     * part of what a caller may hash or memcmp, so they must be defined. */
    memset(life, 0, sizeof *life);
    tl_state_t *s = mutable_state(life);
    s->seed = seed;
    s->prng = 0x9E3779B9u ^ ((uint32_t)seed * 0x01000193u) ^ 0x5A5A0000u;
    static const uint8_t homes[3] = {DUEL_TOWN_PLACE_HOUSE_W, DUEL_TOWN_PLACE_HOUSE_M,
                                     DUEL_TOWN_PLACE_HOUSE_E};
    for (uint8_t i = 0; i < DUEL_TOWN_LIFE_RESIDENTS; i++) {
        tl_resident_t *r = &s->resident[i];
        r->place = homes[i % 3u];
        r->goal = r->place;
        r->next = r->place;
        r->x = places[r->place].x;
        r->y = places[r->place].y;
        r->state = DUEL_TOWN_RES_STAY;
        r->timer = (uint8_t)(next_random(s) % 40u);
        r->personality = (uint8_t)(next_random(s) % DUEL_CIVIC_PERSONALITY_COUNT);
        for (uint8_t n = 0; n < DUEL_TOWN_LIFE_NEEDS; n++)
            r->needs[n] = (uint8_t)(next_random(s) % 128u);
        r->needs[DUEL_TOWN_NEED_REST] = (uint8_t)(next_random(s) % 32u);
        r->need_top = DUEL_TOWN_NEED_REST;
        r->facing = (uint8_t)(i & 1u);
    }
    recount(s);
}

void duel_town_life_init(duel_town_life_t *life, uint8_t seed) {
    if (!life)
        return;
    reset(life, seed);
}

/* ---- needs ------------------------------------------------------------- */

/* How fast each need climbs per tick, in 1/16 units, by personality and the
 * hour. Integer rates applied through an accumulator in the tick count: a
 * need with rate k rises by one on k of every 16 ticks, exactly, so rate 1
 * climbs 0 to 255 in about 20 minutes at the 300 ms tick and rate 2 in 10. */
static void drift_needs(tl_state_t *s, tl_resident_t *r, const duel_town_life_input_t *in) {
    bool night = in->sky_phase == DUEL_TOWN_LIFE_SKY_NIGHT;
    bool quiet = in->mode == DUEL_CIVIC_MODE_QUIET;
    uint8_t rate[DUEL_TOWN_LIFE_NEEDS] = {1, 2, 2, 1}; /* rest, work, food, company */

    switch (r->personality) {
        case DUEL_CIVIC_PERSONALITY_DILIGENT:
            rate[DUEL_TOWN_NEED_WORK] = 3;
            break;
        case DUEL_CIVIC_PERSONALITY_CURIOUS:
            rate[DUEL_TOWN_NEED_COMPANY] = 3;
            break;
        case DUEL_CIVIC_PERSONALITY_NERVOUS:
            rate[DUEL_TOWN_NEED_REST] = 2;
            break;
        case DUEL_CIVIC_PERSONALITY_PROUD:
            rate[DUEL_TOWN_NEED_FOOD] = 1;
            rate[DUEL_TOWN_NEED_WORK] = 3;
            break;
        default: /* distracted */
            rate[DUEL_TOWN_NEED_COMPANY] = 2;
            break;
    }
    if (night) {
        rate[DUEL_TOWN_NEED_REST] = 16;
        rate[DUEL_TOWN_NEED_WORK] = 0;
    }
    if (quiet) {
        rate[DUEL_TOWN_NEED_COMPANY] = 0;
        rate[DUEL_TOWN_NEED_WORK] = (uint8_t)(rate[DUEL_TOWN_NEED_WORK] / 2u);
    }
    if (in->intensity >= DUEL_CIVIC_INTENSITY_BUSY)
        rate[DUEL_TOWN_NEED_WORK] = (uint8_t)(rate[DUEL_TOWN_NEED_WORK] * 2u);

    for (uint8_t n = 0; n < DUEL_TOWN_LIFE_NEEDS; n++) {
        /* rate/16 per tick without a fraction in state: the tick counter is
         * the accumulator, so rate k fires on k of every 16 ticks. */
        uint8_t slot = (uint8_t)(s->tick & 15u);
        if (rate[n] != 0u && ((slot * rate[n]) & 15u) < rate[n])
            r->needs[n] = sat_add(r->needs[n], 1u);
    }
}

/* Satisfy what the place serves; faster at a home, slower at work. */
static bool satisfy(tl_resident_t *r, uint8_t place) {
    uint8_t serves = places[place].serves;
    bool done = true;
    for (uint8_t n = 0; n < DUEL_TOWN_LIFE_NEEDS; n++) {
        if (!(serves & N_(n)))
            continue;
        uint8_t step = n == DUEL_TOWN_NEED_WORK ? 2u : n == DUEL_TOWN_NEED_REST ? 2u : 4u;
        r->needs[n] = sat_sub(r->needs[n], step);
        if (r->needs[n] > 16u)
            done = false;
    }
    return done;
}

/* ---- deciding ---------------------------------------------------------- */

static uint8_t pick_need(tl_state_t *s, const tl_resident_t *r, bool night) {
    uint8_t best = 0;
    uint16_t best_score = 0;
    for (uint8_t n = 0; n < DUEL_TOWN_LIFE_NEEDS; n++) {
        uint16_t score = r->needs[n];
        if (night && n == DUEL_TOWN_NEED_REST)
            score = (uint16_t)(score * 2u);
        score = (uint16_t)(score * 4u + (next_random(s) & 3u)); /* tie-break */
        if (score > best_score) {
            best_score = score;
            best = n;
        }
    }
    return best;
}

/* The nearest place with room that serves `need`, leaning toward the
 * district's own place half the time it qualifies. */
static uint8_t pick_place(tl_state_t *s, const tl_resident_t *r, uint8_t need,
                          const duel_town_life_input_t *in) {
    uint8_t dist[DUEL_TOWN_LIFE_PLACES], parent[DUEL_TOWN_LIFE_PLACES];
    bfs(r->place, dist, parent);
    uint8_t lean = district_place(in->district);
    if ((places[lean].serves & N_(need)) && s->occupancy[lean] < places[lean].capacity &&
        (next_random(s) & 1u))
        return lean;
    uint8_t best = r->place, best_d = NONE;
    for (uint8_t p = 0; p < DUEL_TOWN_LIFE_PLACES; p++) {
        if (!(places[p].serves & N_(need)))
            continue;
        /* Gates are errands: only the proud and diligent take the road. */
        if (places[p].inside && need == DUEL_TOWN_NEED_WORK &&
            r->personality != DUEL_CIVIC_PERSONALITY_PROUD &&
            r->personality != DUEL_CIVIC_PERSONALITY_DILIGENT)
            continue;
        uint8_t room = s->occupancy[p] < places[p].capacity ? 0u : 3u; /* full costs 3 hops */
        uint8_t d = (uint8_t)(dist[p] + room + (next_random(s) & 1u));
        if (d < best_d) {
            best_d = d;
            best = p;
        }
    }
    return best;
}

static void set_goal(tl_resident_t *r, uint8_t goal) {
    bool at_place = r->x == places[r->place].x && r->y == places[r->place].y;
    r->goal = goal;
    if (goal == r->place && at_place) {
        r->state = DUEL_TOWN_RES_STAY;
        r->timer = 200u;
        r->next = goal;
    } else {
        /* Mid-street (after a watch or a fright) the first hop is back to the
         * node last stood at; from a node it is the breadth-first hop. */
        r->state = DUEL_TOWN_RES_WALK;
        r->next = at_place ? next_hop(r->place, goal) : r->place;
        r->timer = 0u;
    }
}

static void decide(tl_state_t *s, tl_resident_t *r, const duel_town_life_input_t *in) {
    bool night = in->sky_phase == DUEL_TOWN_LIFE_SKY_NIGHT;
    uint8_t need = pick_need(s, r, night);
    r->need_top = need;
    /* Content at home: stay in, lamplit. */
    if (r->needs[need] < 96u && places[r->place].inside) {
        r->state = DUEL_TOWN_RES_STAY;
        r->timer = 40u;
        return;
    }
    /* Content outside: loiter a little before the next errand. */
    if (r->needs[need] < 64u) {
        r->state = DUEL_TOWN_RES_IDLE;
        r->timer = (uint8_t)(8u + (next_random(s) % 32u));
        return;
    }
    set_goal(r, pick_place(s, r, need, in));
}

/* ---- moving ------------------------------------------------------------ */

static int16_t toward(int16_t v, int16_t target, int16_t step) {
    if (v < target)
        return (int16_t)(target - v < step ? target : v + step);
    if (v > target)
        return (int16_t)(v - target < step ? target : v - step);
    return v;
}

static void walk(tl_state_t *s, tl_resident_t *r, const duel_town_life_input_t *in) {
    const tl_place_t *w = &places[r->next];
    int16_t speed = r->personality == DUEL_CIVIC_PERSONALITY_NERVOUS ? 2 : 1;
    if (r->x != w->x)
        r->facing = w->x > r->x ? 1u : 0u;
    r->x = toward(r->x, w->x, speed);
    r->y = toward(r->y, w->y, 1);
    r->timer = sat_add(r->timer, 1u); /* ticks on this hop, for the stuck guard */
    if (r->x == w->x && r->y == w->y) {
        r->place = r->next;
        r->timer = 0u;
        if (r->place == r->goal) {
            r->state = DUEL_TOWN_RES_STAY;
            r->timer = 200u;
        } else {
            r->next = next_hop(r->place, r->goal);
        }
    }
    /* The distracted re-think mid-street now and then. */
    if (r->personality == DUEL_CIVIC_PERSONALITY_DISTRACTED && (next_random(s) & 127u) == 0u)
        decide(s, r, in);
}

static void step_resident(tl_state_t *s, uint8_t i, const duel_town_life_input_t *in) {
    tl_resident_t *r = &s->resident[i];
    drift_needs(s, r, in);

    bool outside = !places[r->place].inside || r->state == DUEL_TOWN_RES_WALK;
    if (in->spell_up && outside && r->state != DUEL_TOWN_RES_WATCH) {
        uint32_t roll = next_random(s);
        if (r->personality == DUEL_CIVIC_PERSONALITY_CURIOUS ? (roll & 31u) == 0u
                                                             : (roll & 127u) == 0u) {
            r->state = DUEL_TOWN_RES_WATCH;
            r->timer = 12u;
            r->facing = r->x < 128 ? 1u : 0u;
            return;
        }
        if (r->personality == DUEL_CIVIC_PERSONALITY_NERVOUS && (roll & 31u) == 1u &&
            !places[r->goal].inside) {
            r->needs[DUEL_TOWN_NEED_REST] = sat_add(r->needs[DUEL_TOWN_NEED_REST], 64u);
            set_goal(r, pick_place(s, r, DUEL_TOWN_NEED_REST, in));
            return;
        }
    }

    switch (r->state) {
        case DUEL_TOWN_RES_WALK:
            walk(s, r, in);
            break;
        case DUEL_TOWN_RES_STAY:
            if (r->timer > 0u)
                r->timer--;
            if (satisfy(r, r->place) || r->timer == 0u)
                decide(s, r, in);
            break;
        case DUEL_TOWN_RES_WATCH:
            if (r->timer > 0u)
                r->timer--;
            else
                r->state = places[r->place].inside && r->x == places[r->place].x
                               ? DUEL_TOWN_RES_STAY
                               : DUEL_TOWN_RES_IDLE;
            break;
        default: /* idle */
            if (r->timer > 0u)
                r->timer--;
            else
                decide(s, r, in);
            break;
    }
}

static void run_tick(tl_state_t *s, const duel_town_life_input_t *in) {
    for (uint8_t i = 0; i < DUEL_TOWN_LIFE_RESIDENTS; i++)
        step_resident(s, i, in);
    recount(s);
    s->tick++;
}

uint32_t duel_town_life_advance(duel_town_life_t *life, const duel_town_life_input_t *in,
                                uint32_t now_ms) {
    if (!life || !in)
        return 0u;
    tl_state_t *s = mutable_state(life);
    uint32_t due = now_ms / DUEL_TOWN_LIFE_TICK_MS;
    if (due < s->tick)
        reset(life, s->seed); /* a backward seek re-derives from the seed */
    uint32_t ran = 0u;
    while (s->tick < due) {
        run_tick(s, in);
        ran++;
    }
    return ran;
}

uint32_t duel_town_life_ticks(const duel_town_life_t *life) {
    return life ? readable_state(life)->tick : 0u;
}

bool duel_town_life_resident(const duel_town_life_t *life, uint8_t index,
                             duel_town_life_view_t *out) {
    if (!life || !out || index >= DUEL_TOWN_LIFE_RESIDENTS)
        return false;
    const tl_resident_t *r = &readable_state(life)->resident[index];
    out->x = r->x;
    out->y = r->y;
    out->state = r->state;
    out->place = r->place;
    out->goal = r->goal;
    out->personality = r->personality;
    out->need_top = r->need_top;
    out->need_level = r->needs[r->need_top];
    out->visible = !(places[r->place].inside && r->state != DUEL_TOWN_RES_WALK &&
                     r->x == places[r->place].x && r->y == places[r->place].y);
    out->facing = r->facing;
    return true;
}

uint8_t duel_town_life_occupancy(const duel_town_life_t *life, uint8_t place) {
    if (!life || place >= DUEL_TOWN_LIFE_PLACES)
        return 0u;
    return readable_state(life)->occupancy[place];
}

bool duel_town_life_place(uint8_t place, int16_t *x, int16_t *y, uint8_t *serves, bool *inside) {
    if (place >= DUEL_TOWN_LIFE_PLACES)
        return false;
    if (x)
        *x = places[place].x;
    if (y)
        *y = places[place].y;
    if (serves)
        *serves = places[place].serves;
    if (inside)
        *inside = places[place].inside != 0u;
    return true;
}
