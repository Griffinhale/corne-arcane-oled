/*
 * The desktop town: one wizard tower at the centre of a small city.
 *
 * Everything here is read from the same duel_render_t the panels read. The
 * town is a second opinion about how to show that state, not a second state:
 * the floor still decides which windows are lit, the sky phase still decides
 * the hour, the civic clock still paces the residents, and a spell in flight
 * is the same spell.
 *
 * One bit per pixel. Depth is carried by dither and by where a roofline sits
 * against the horizon, because that is all a one-bit town has to work with.
 */
#include "duel_town.h"

#include <string.h>

#include "duel_civic.h"
#include "duel_host.h"
#include "duel_runtime.h"
#include "duel_view.h"

/* ---- the town's geometry, stated once -----------------------------------
 * The near ground is where the town stands; the far ground sits higher and
 * carries the smaller roofs behind it. The tower is centred because it is the
 * reason the town is here. */
#define GROUND_Y     208
#define FAR_GROUND_Y 196
#define TOWER_CX     128
#define TOWER_HALF   19
#define TOWER_X0     (TOWER_CX - TOWER_HALF)
#define TOWER_X1     (TOWER_CX + TOWER_HALF)
#define TOWER_TOP_Y  64
#define ROOF_APEX_Y  30
#define SPIRE_TIP_Y  13
#define BALCONY_Y    102
#define BALCONY_HALF 27
#define DOOR_W       7

void town_fb_clear(town_fb_t *fb) { memset(fb->bits, 0, sizeof fb->bits); }

static void px(town_fb_t *fb, int x, int y, bool on) {
    if (x < 0 || x >= TOWN_W || y < 0 || y >= TOWN_H)
        return;
    uint8_t *byte = &fb->bits[y * TOWN_STRIDE + (x >> 3)];
    uint8_t mask = (uint8_t)(1u << (x & 7));
    if (on)
        *byte |= mask;
    else
        *byte = (uint8_t)(*byte & ~mask);
}

bool town_fb_get(const town_fb_t *fb, int x, int y) {
    if (x < 0 || x >= TOWN_W || y < 0 || y >= TOWN_H)
        return false;
    return (fb->bits[y * TOWN_STRIDE + (x >> 3)] >> (x & 7)) & 1u;
}

static void hline(town_fb_t *fb, int x0, int x1, int y) {
    for (int x = x0; x <= x1; x++)
        px(fb, x, y, true);
}

static void vline(town_fb_t *fb, int x, int y0, int y1) {
    for (int y = y0; y <= y1; y++)
        px(fb, x, y, true);
}

static void frame_rect(town_fb_t *fb, int x0, int y0, int x1, int y1) {
    hline(fb, x0, x1, y0);
    hline(fb, x0, x1, y1);
    vline(fb, x0, y0, y1);
    vline(fb, x1, y0, y1);
}

static void fill_rect(town_fb_t *fb, int x0, int y0, int x1, int y1, bool on) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            px(fb, x, y, on);
}

/* Half-tone fill. The only way a one-bit town says "further away". */
static void dither_rect(town_fb_t *fb, int x0, int y0, int x1, int y1) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (((x + y) & 1) == 0)
                px(fb, x, y, true);
}

/* A rounded arch over a span. Doors and the study window both want one, and
 * a one-bit arch has to be drawn rather than approximated with a diagonal. */
static void arch(town_fb_t *fb, int cx, int y, int radius) {
    for (int x = -radius; x <= radius; x++) {
        int rise = 0;
        while ((rise + 1) * (rise + 1) + x * x <= radius * radius)
            rise++;
        px(fb, cx + x, y - rise, true);
    }
}

static uint32_t town_hash(uint32_t a, uint32_t b) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x85EBCA6Bu);
    h ^= h >> 15;
    h *= 0x2545F491u;
    h ^= h >> 13;
    return h;
}

/* ---- sky ----------------------------------------------------------------- */

/* The celestial body rides a sixteen-step arc, the same one the panels draw:
 * four sky phases of four sub-phases each. */
static void draw_celestial(town_fb_t *fb, uint8_t phase, uint8_t sub) {
    int step = phase * 4 + sub;
    int offset = 2 * step - 15;
    int cx = 18 + step * 14;
    int cy = 38 + offset * offset * 110 / 225;
    bool night = phase == DUEL_SKY_DUSK || phase == DUEL_SKY_NIGHT;

    if (night) {
        /* Crescent: a disc with a second disc bitten out of its gap side. */
        for (int y = -7; y <= 7; y++)
            for (int x = -7; x <= 7; x++)
                if (x * x + y * y <= 49)
                    px(fb, cx + x, cy + y, true);
        for (int y = -7; y <= 7; y++)
            for (int x = -7; x <= 7; x++)
                if (x * x + y * y <= 42)
                    px(fb, cx + x + 4, cy + y - 1, false);
        return;
    }
    for (int y = -6; y <= 6; y++)
        for (int x = -6; x <= 6; x++)
            if (x * x + y * y <= 36)
                px(fb, cx + x, cy + y, true);
    /* Eight rays, drawn away from the disc so the sun reads as lit. */
    static const int8_t ray[8][2] = {{0, -1},  {0, 1},  {-1, 0}, {1, 0},
                                     {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
    for (int i = 0; i < 8; i++)
        for (int d = 9; d <= 12; d++)
            px(fb, cx + ray[i][0] * d, cy + ray[i][1] * d, true);
}

static void draw_stars(town_fb_t *fb, const duel_render_t *r, uint8_t phase, uint32_t frame) {
    if (phase == DUEL_SKY_DAWN || phase == DUEL_SKY_DAY)
        return;
    /* Fixed positions from the session seed, so a city's sky is its own and
     * stays put; only the twinkle moves. */
    for (uint32_t i = 0; i < 46u; i++) {
        uint32_t h = town_hash(r->seed, i);
        int x = (int)(h % 250u) + 3;
        int y = (int)((h >> 9) % 150u) + 4;
        if (((h >> 20) & 7u) == 0u && ((frame >> 3) + i) % 9u == 0u)
            continue; /* an occasional slow twinkle */
        px(fb, x, y, true);
        if (((h >> 24) & 3u) == 0u) {
            px(fb, x + 1, y, true);
            px(fb, x, y + 1, true);
        }
    }
}

static void draw_clouds(town_fb_t *fb, const duel_render_t *r, uint8_t phase, uint32_t frame) {
    if (phase == DUEL_SKY_NIGHT)
        return;
    /* Two banks, drifting slowly and wrapping. Outlined, not filled: a filled
     * cloud at this size reads as a building. */
    for (int bank = 0; bank < 2; bank++) {
        int width = bank ? 46 : 34;
        int y = bank ? 44 : 72;
        int x = (int)(((frame >> 4) + (uint32_t)bank * 130u + r->seed) % 320u) - 40;
        hline(fb, x + 4, x + width - 4, y);
        hline(fb, x, x + width, y + 7);
        vline(fb, x, y + 4, y + 7);
        vline(fb, x + width, y + 4, y + 7);
        px(fb, x + 2, y + 2, true);
        px(fb, x + 3, y + 1, true);
        px(fb, x + width - 3, y + 1, true);
        px(fb, x + width - 2, y + 2, true);
    }
}

/* ---- the town around the tower ------------------------------------------ */

typedef struct {
    int16_t x0;
    int16_t width;
    int16_t height;
    uint8_t chimney; /* 0 none, otherwise the offset from x0 */
} town_building_t;

/* Two rows. The far row stands on the higher ground line and is half-toned;
 * the near row is solid and outlined. The gaps either side of the centre are
 * the approach to the tower door. */
/* Placed to show through the approaches either side of the tower and past the
 * ends of the near row, since a far building nothing can see is just cost. */
static const town_building_t far_row[] = {
    {2, 18, 16, 0}, {88, 19, 22, 0}, {148, 16, 18, 0}, {196, 15, 14, 0}, {236, 18, 20, 0},
};
static const town_building_t near_row[] = {
    {12, 42, 40, 9},
    {58, 28, 27, 0},
    {166, 33, 33, 24},
    {203, 41, 46, 8},
};

static void draw_far_row(town_fb_t *fb) {
    for (size_t i = 0; i < sizeof far_row / sizeof far_row[0]; i++) {
        const town_building_t *b = &far_row[i];
        int x1 = b->x0 + b->width;
        int top = FAR_GROUND_Y - b->height;
        dither_rect(fb, b->x0, top, x1, FAR_GROUND_Y);
        hline(fb, b->x0, x1, top);
    }
    /* The far ground itself: a broken line, so it reads as distance rather
     * than as a second street. */
    for (int x = 0; x < TOWN_W; x += 3)
        px(fb, x, FAR_GROUND_Y, true);
}

/* Puffs, not a plume: three of them, well separated, leaning further as they
 * rise and thinning out at the top. A continuous column reads as a mast. */
static void draw_smoke(town_fb_t *fb, int x, int base_y, uint32_t frame, uint32_t salt) {
    for (int puff = 0; puff < 3; puff++) {
        uint32_t age = ((frame >> 3) + (uint32_t)puff * 9u + salt) % 27u;
        int y = base_y - 4 - (int)age;
        int drift = (int)(age * age / 90u) + (int)(age / 5u);
        int wobble = ((age + salt) & 3u) == 0u ? 1 : 0;
        px(fb, x + drift + wobble, y, true);
        if (age < 16u) {
            px(fb, x + drift + wobble + 1, y, true);
            if (age < 8u)
                px(fb, x + drift + wobble, y - 1, true);
        }
    }
}

static void draw_near_row(town_fb_t *fb, const duel_render_t *r, uint32_t frame) {
    uint8_t mode = DUEL_CIVIC_MODE(r->civic);
    for (size_t i = 0; i < sizeof near_row / sizeof near_row[0]; i++) {
        const town_building_t *b = &near_row[i];
        int x1 = b->x0 + b->width;
        int top = GROUND_Y - b->height;
        fill_rect(fb, b->x0, top, x1, GROUND_Y, false); /* clear the far row behind */
        frame_rect(fb, b->x0, top, x1, GROUND_Y);
        /* A pitched roof over the box, and a lintel line under it. */
        for (int step = 0; step <= b->width / 2; step++) {
            px(fb, b->x0 + step, top - step / 2, true);
            px(fb, x1 - step, top - step / 2, true);
        }
        hline(fb, b->x0, x1, top);

        /* Two shuttered windows and a door, lit unless the town is quiet. */
        int wy = top + 8;
        bool lit = mode != DUEL_CIVIC_MODE_QUIET;
        for (int w = 0; w < 2; w++) {
            int wx = b->x0 + 6 + w * (b->width - 16);
            frame_rect(fb, wx, wy, wx + 5, wy + 5);
            if (lit && ((town_hash((uint32_t)i, (uint32_t)w) >> 3) & 3u) != 0u)
                fill_rect(fb, wx + 1, wy + 1, wx + 4, wy + 4, true);
        }
        int dx = b->x0 + b->width / 2;
        frame_rect(fb, dx - 3, GROUND_Y - 11, dx + 3, GROUND_Y);
        arch(fb, dx, GROUND_Y - 11, 3);

        if (b->chimney) {
            int cx = b->x0 + b->chimney;
            int ctop = top - b->width / 4 - 6;
            fill_rect(fb, cx, ctop, cx + 4, top - 2, true);
            draw_smoke(fb, cx + 2, ctop, frame, (uint32_t)i * 13u);
        }
    }
}

/* ---- the tower ----------------------------------------------------------- */

/* Which window rows are lit. The floor picks a band and the host's intensity
 * widens it, so switching applications moves the light up and down the tower
 * exactly as it moves the room on the panels. */
#define TOWER_ROWS      5
#define TOWER_ROW0_Y    114
#define TOWER_ROW_PITCH 17

static bool row_is_lit(const duel_render_t *r, int row) {
    uint8_t floor = DUEL_CIVIC_FLOOR(r->civic);
    uint8_t intensity = DUEL_CIVIC_INTENSITY(r->civic);
    /* Floor 0 is the ground storey, floor 3 the top one. */
    int band = (TOWER_ROWS - 1) - (int)floor * (TOWER_ROWS - 1) / 3;
    int reach = (int)intensity / 2;
    return row >= band - reach && row <= band + reach;
}

static void draw_tower(town_fb_t *fb, const duel_render_t *r, uint32_t frame) {
    uint8_t mode = DUEL_CIVIC_MODE(r->civic);

    fill_rect(fb, TOWER_X0, TOWER_TOP_Y, TOWER_X1, GROUND_Y, false);
    frame_rect(fb, TOWER_X0, TOWER_TOP_Y, TOWER_X1, GROUND_Y);

    /* Conical roof, then the spire and its finial. */
    for (int y = TOWER_TOP_Y; y >= ROOF_APEX_Y; y--) {
        int span = (y - ROOF_APEX_Y) * (TOWER_HALF + 4) / (TOWER_TOP_Y - ROOF_APEX_Y);
        px(fb, TOWER_CX - span, y, true);
        px(fb, TOWER_CX + span, y, true);
    }
    hline(fb, TOWER_CX - TOWER_HALF - 4, TOWER_CX + TOWER_HALF + 4, TOWER_TOP_Y);
    vline(fb, TOWER_CX, SPIRE_TIP_Y, ROOF_APEX_Y);
    hline(fb, TOWER_CX - 3, TOWER_CX + 3, SPIRE_TIP_Y + 4);
    px(fb, TOWER_CX, SPIRE_TIP_Y - 2, true);

    /* An urgent town lights its beacon; the pulse is the only thing on the
     * tower that moves without the world moving. */
    if (mode == DUEL_CIVIC_MODE_URGENT && ((frame >> 3) & 1u) == 0u) {
        for (int y = -3; y <= 3; y++)
            for (int x = -3; x <= 3; x++)
                if (x * x + y * y <= 9)
                    px(fb, TOWER_CX + x, SPIRE_TIP_Y - 6 + y, true);
    }

    /* The study: one tall arched opening under the roof, giving onto the
     * balcony. It is the room the wizard works in, so it is lit whenever a
     * champion is standing. */
    int study_top = TOWER_TOP_Y + 10;
    frame_rect(fb, TOWER_CX - 8, study_top, TOWER_CX + 8, BALCONY_Y - 6);
    arch(fb, TOWER_CX, study_top, 8);
    duel_view_wizard_t occupant = duel_view_wizard(&r->view, SIM_SIDE_L);
    if (occupant.life == LIFE_ACTIVE) {
        fill_rect(fb, TOWER_CX - 7, study_top + 1, TOWER_CX + 7, BALCONY_Y - 7, true);
        vline(fb, TOWER_CX, study_top - 5, BALCONY_Y - 7); /* the mullion, unlit */
        for (int x = TOWER_CX - 7; x <= TOWER_CX + 7; x++)
            px(fb, x, study_top - 5, false);
        arch(fb, TOWER_CX, study_top, 8);
    }

    /* Balcony: a slab wider than the shaft, with a rail. It is where the
     * wizard stands and where a spell leaves from. */
    hline(fb, TOWER_CX - BALCONY_HALF, TOWER_CX + BALCONY_HALF, BALCONY_Y);
    hline(fb, TOWER_CX - BALCONY_HALF, TOWER_CX + BALCONY_HALF, BALCONY_Y + 1);
    hline(fb, TOWER_CX - BALCONY_HALF, TOWER_CX + BALCONY_HALF, BALCONY_Y - 5);
    for (int x = TOWER_CX - BALCONY_HALF; x <= TOWER_CX + BALCONY_HALF; x += 5)
        vline(fb, x, BALCONY_Y - 4, BALCONY_Y - 1);

    /* Dwellings below: courses of stone between the storeys, two windows each. */
    for (int row = 0; row < TOWER_ROWS; row++) {
        int wy = TOWER_ROW0_Y + row * TOWER_ROW_PITCH;
        bool lit = row_is_lit(r, row);
        for (int x = TOWER_X0 + 2; x < TOWER_X1; x += 4)
            px(fb, x, wy - 4, true);
        for (int col = 0; col < 2; col++) {
            int wx = TOWER_CX - 12 + col * 15;
            frame_rect(fb, wx, wy, wx + 8, wy + 9);
            if (lit)
                fill_rect(fb, wx + 1, wy + 1, wx + 7, wy + 8, true);
            else
                vline(fb, wx + 4, wy + 1, wy + 8); /* an unlit mullion */
        }
    }

    frame_rect(fb, TOWER_CX - DOOR_W, GROUND_Y - 20, TOWER_CX + DOOR_W, GROUND_Y);
    arch(fb, TOWER_CX, GROUND_Y - 20, DOOR_W);
    vline(fb, TOWER_CX, GROUND_Y - 18, GROUND_Y - 1);
}

/* ---- the wizard on the balcony ------------------------------------------- */

static void draw_wizard(town_fb_t *fb, const duel_render_t *r, uint32_t frame) {
    duel_view_wizard_t wz = duel_view_wizard(&r->view, SIM_SIDE_L);
    if (wz.life != LIFE_ACTIVE)
        return;
    bool casting = wz.pose == POSE_CAST;
    /* A slow shuffle along the balcony while nothing is brewing. */
    int sway = wz.inc_state == INC_IDLE && !casting ? (int)((frame >> 5) & 3u) - 1 : 0;
    int cx = TOWER_CX + sway;
    int feet = BALCONY_Y - 1;

    /* Stand clear of the lit study behind and the rail in front: the figure
     * owns its own column of pixels or it is a smudge on the balcony. */
    fill_rect(fb, cx - 7, feet - 22, cx + 8, feet, false);

    fill_rect(fb, cx - 3, feet - 11, cx + 3, feet - 1, true); /* robe */
    px(fb, cx - 4, feet - 2, true);
    px(fb, cx + 4, feet - 2, true);
    px(fb, cx - 4, feet - 1, true);
    px(fb, cx + 4, feet - 1, true);
    fill_rect(fb, cx - 2, feet - 15, cx + 2, feet - 12, true); /* head */
    hline(fb, cx - 5, cx + 5, feet - 16);                      /* hat brim */
    for (int step = 0; step <= 4; step++)
        hline(fb, cx - 3 + step, cx + 3 - step, feet - 17 - step);

    int staff_x = cx + 6;
    if (casting) {
        vline(fb, staff_x, feet - 22, feet - 6);
        for (int y = -2; y <= 2; y++)
            for (int x = -2; x <= 2; x++)
                if (x * x + y * y <= 4)
                    px(fb, staff_x + x, feet - 24 + y, true);
    } else {
        vline(fb, staff_x, feet - 17, feet - 1);
        px(fb, staff_x, feet - 19, true);
    }

    /* Charging a big cast lights the shaft below the balcony. */
    if (wz.rearm_lock && (wz.inc_state == INC_WINDUP || wz.inc_state == INC_PREPARED)) {
        for (int i = 0; i < 4; i++) {
            int my = BALCONY_Y + 12 + (int)((frame * 2u + (uint32_t)i * 11u) % 44u);
            px(fb, TOWER_X0 - 3, my, true);
            px(fb, TOWER_X1 + 3, my, true);
        }
    }
}

/* ---- spells over the town ------------------------------------------------ */

/* A carrier leaves the balcony and arcs out over the roofs. The panels send it
 * along a desk between two towers; here there is one tower, so the two slots
 * throw in opposite directions and the town is what they fly over. */
static void draw_spells(town_fb_t *fb, const duel_render_t *r) {
    for (uint8_t side = 0; side < 2u; side++) {
        duel_view_spell_t spell = duel_view_spell(&r->view, side, r->seed);
        if (!spell.active)
            continue;
        int travel = side == SIM_SIDE_L ? spell.pos : 255 - spell.pos;
        int reach = travel * 118 / 255;
        int x = TOWER_CX + (side == SIM_SIDE_L ? reach : -reach);
        int arc = 4 * travel * (255 - travel) / 255;
        int y = BALCONY_Y - 12 - arc * 52 / 255;

        uint8_t element = DUEL_KIND_ELEMENT(spell.kind);
        px(fb, x, y, true);
        px(fb, x + 1, y, true);
        px(fb, x, y + 1, true);
        px(fb, x + 1, y + 1, true);
        for (int t = 1; t <= 3; t++) {
            int back = side == SIM_SIDE_L ? -t * 3 : t * 3;
            /* Void breaks up, frost falls away, ember doubles: enough to
             * tell them apart at a glance without reading the descriptor. */
            if (element == ELEM_VOID && (t & 1))
                continue;
            px(fb, x + back, y + (element == ELEM_FROST ? t : 0), true);
            if (element == ELEM_EMBER)
                px(fb, x + back, y - 1, true);
        }
    }
}

/* ---- the plaza ----------------------------------------------------------- */

static void draw_plaza(town_fb_t *fb, const duel_render_t *r, uint32_t frame) {
    hline(fb, 0, TOWN_W - 1, GROUND_Y);
    hline(fb, 0, TOWN_W - 1, GROUND_Y + 1);
    /* Cobbles, offset row to row, thinning toward the bottom edge so the eye
     * reads the plaza as receding rather than as a wall. */
    for (int row = 0; row < 5; row++) {
        int y = GROUND_Y + 8 + row * 9;
        for (int x = (row & 1) ? 4 : 10; x < TOWN_W; x += row < 3 ? 12 : 16)
            hline(fb, x, x + (row < 3 ? 5 : 7), y);
    }
    (void)r;
    (void)frame;
}

/* Residents cross the plaza on the civic clock, the same clock that paces the
 * occupation on the panels. Two walkers and one who stands and watches. */
static void draw_residents(town_fb_t *fb, const duel_render_t *r, uint32_t frame) {
    uint8_t mode = DUEL_CIVIC_MODE(r->civic);
    int walkers = mode == DUEL_CIVIC_MODE_QUIET ? 1 : 3;
    for (int i = 0; i < walkers; i++) {
        uint32_t h = town_hash(r->seed, (uint32_t)i + 40u);
        int span = TOWN_W + 40;
        int speed = 1 + (int)(h & 1u);
        int phase = (int)(((uint32_t)r->civic_phase * (uint32_t)speed + (h >> 4)) % (uint32_t)span);
        int x = (h & 2u) ? phase - 20 : span - phase - 20;
        int y = GROUND_Y + 16 + (int)((h >> 6) % 20u);
        bool stepping = ((r->civic_phase + (uint8_t)i) & 2u) == 0u;

        /* Cloak flaring to the hem, a head above it, and legs that alternate.
         * Four pixels of shoulder is what makes it a person and not a post. */
        fill_rect(fb, x - 1, y - 8, x + 1, y - 6, true);
        fill_rect(fb, x - 2, y - 5, x + 2, y - 3, true);
        px(fb, x - 3, y - 3, true);
        px(fb, x + 3, y - 3, true);
        fill_rect(fb, x - 1, y - 11, x + 1, y - 9, true);
        px(fb, x + (stepping ? 1 : -1), y - 2, true);
        px(fb, x - 2, y - 1, true);
        px(fb, x + 2, y - 1, true);
        px(fb, stepping ? x - 3 : x - 2, y, true);
        px(fb, stepping ? x + 2 : x + 3, y, true);
    }
    (void)frame;
}

void duel_town_draw(town_fb_t *fb, const duel_render_t *r, uint32_t frame) {
    uint8_t phase = DUEL_SECONDARY_SKY_PHASE(r->secondary);
    uint8_t sub = DUEL_SECONDARY_SKY_SUBPHASE(r->secondary);

    draw_stars(fb, r, phase, frame);
    draw_celestial(fb, phase, sub);
    draw_clouds(fb, r, phase, frame);
    draw_far_row(fb);
    draw_near_row(fb, r, frame);
    draw_tower(fb, r, frame);
    draw_wizard(fb, r, frame);
    draw_spells(fb, r);
    draw_plaza(fb, r, frame);
    draw_residents(fb, r, frame);
}
