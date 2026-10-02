/*
 * The desktop town: one wizard tower at the centre of a small city.
 *
 * Everything here is read from the same duel_render_t the panels read. The
 * town is a second opinion about how to show that state, not a second state:
 * the floor still decides which windows are lit, the sky phase still decides
 * the hour, the civic clock still paces the residents, and a spell in flight
 * is the same spell.
 *
 * One bit per pixel. Depth is carried by ordered dither and by where a
 * roofline sits against the horizon, because that is all a one-bit town has
 * to work with.
 *
 * The town is composed at 256x256 or 400x240 and the panels' 32x128 habits do
 * not scale into either. Two rules keep it from reading as a small drawing on
 * a large canvas:
 *
 *   - The sky is not background. Roughly the top half of the surface is air,
 *     and the spells are what live there -- a cast climbs into it on the arc
 *     its own trajectory names, so the emptiest region of the canvas is the
 *     one the duel is fought across.
 *   - Every band is drawn at the density its distance earns. Hills are a
 *     sixteenth-tone, the far row an eighth, the near row solid outline. The
 *     ordered dither below is what makes that a continuum rather than three
 *     unrelated decisions.
 */
#include "duel_town.h"

#include <string.h>

#include "duel_civic.h"
#include "duel_courier.h"
#include "duel_host.h"
#include "duel_incantation.h"
#include "duel_runtime.h"
#include "duel_town_life.h"
#include "duel_view.h"

/* ---- the town's geometry, stated once -----------------------------------
 * The near ground is where the town stands; the far ground sits higher and
 * carries the smaller roofs behind it, and the hills sit higher again. The
 * tower is centred because it is the reason the town is here. */
#define TOWN_GROUND_Y 208
#define CANVAS_W      ((int)fb->width)
#define CANVAS_H      ((int)fb->height)
#define GROUND_Y      ((int)fb->ground_y)
#define FAR_GROUND_Y  (GROUND_Y - 12)
#define HILL_BASE_Y   (GROUND_Y - 18)
#define TOWER_CX      ((int)fb->center_x)
#define TOWN_X(x)     ((x) + TOWER_CX - TOWN_W / 2)
#define TOWER_HALF    19
#define TOWER_X0      (TOWER_CX - TOWER_HALF)
#define TOWER_X1      (TOWER_CX + TOWER_HALF)
#define TOWER_TOP_Y   64
#define ROOF_APEX_Y   30
#define SPIRE_TIP_Y   13
#define BALCONY_Y     102
#define BALCONY_HALF  27
/* The ward dome hangs over the tower's upper half rather than round the
 * balcony: centred on the balcony it apexed inside the roof cone, and the
 * tower -- drawn afterwards -- erased the top of its own shield. */
#define WARD_CY 124
#define DOOR_W  7

void town_fb_clear(town_fb_t *fb, int width, int height) {
    memset(fb, 0, sizeof *fb);
    fb->width = (uint16_t)width;
    fb->height = (uint16_t)height;
    fb->center_x = (uint16_t)(width / 2);
    /* Keep the square's forty-eight-pixel plaza in both compositions. The
     * wide view gains sky and streets at the sides; it does not squeeze or
     * crop the town to fit its shorter surface. */
    fb->ground_y = (uint16_t)(height - (TOWN_H - TOWN_GROUND_Y));
}

static void px(town_fb_t *fb, int x, int y, bool on) {
    if (x < 0 || x >= CANVAS_W || y < 0 || y >= CANVAS_H)
        return;
    uint8_t *byte = &fb->bits[y * TOWN_FB_STRIDE + (x >> 3)];
    uint8_t mask = (uint8_t)(1u << (x & 7));
    if (on)
        *byte |= mask;
    else
        *byte = (uint8_t)(*byte & ~mask);
}

bool town_fb_get(const town_fb_t *fb, int x, int y) {
    if (x < 0 || x >= (int)fb->width || y < 0 || y >= (int)fb->height)
        return false;
    return (fb->bits[y * TOWN_FB_STRIDE + (x >> 3)] >> (x & 7)) & 1u;
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

/*
 * Ordered dither, 4x4, seventeen levels. The only way a one-bit town says
 * "further away" or "dimmer", and the reason it can say it in more than one
 * voice: a checkerboard is the single tone the old half-tone fill could
 * reach, and a hill behind a hill behind a roof needs three.
 *
 * The threshold matrix is a position function, not a running state, so two
 * shapes that overlap agree about every pixel they share and the seam does
 * not shimmer when one of them moves.
 */
static const uint8_t BAYER4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

static bool shade_on(int x, int y, int level) {
    if (level <= 0)
        return false;
    if (level >= 16)
        return true;
    return level > (int)BAYER4[(((y & 3) << 2) | (x & 3))];
}

static void shade_rect(town_fb_t *fb, int x0, int y0, int x1, int y1, int level) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (shade_on(x, y, level))
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

/* Filled and outlined circles, integer only. A spell is a round thing seen
 * against the sky and every one of them is drawn out of these two. */
static void disc(town_fb_t *fb, int cx, int cy, int radius, bool on) {
    for (int y = -radius; y <= radius; y++)
        for (int x = -radius; x <= radius; x++)
            if (x * x + y * y <= radius * radius)
                px(fb, cx + x, cy + y, on);
}

static void shade_disc(town_fb_t *fb, int cx, int cy, int radius, int level) {
    for (int y = -radius; y <= radius; y++)
        for (int x = -radius; x <= radius; x++)
            if (x * x + y * y <= radius * radius && shade_on(cx + x, cy + y, level))
                px(fb, cx + x, cy + y, true);
}

/* The outline only: every cell whose centre is inside the radius but which
 * has a neighbour outside it. Cheaper than a midpoint circle to read, and it
 * closes at every radius, which a stepped one does not. */
static void ring(town_fb_t *fb, int cx, int cy, int radius, bool on) {
    int rr = radius * radius;
    int inner = (radius - 1) * (radius - 1);
    for (int y = -radius; y <= radius; y++)
        for (int x = -radius; x <= radius; x++) {
            int d = x * x + y * y;
            if (d <= rr && d > inner)
                px(fb, cx + x, cy + y, on);
        }
}

/* A line, Bresenham, optionally broken. `period` 1 draws every pixel, 3 draws
 * one in three -- which is how a constellation joins two stars without
 * looking like a wire. */
static void line_step(town_fb_t *fb, int x0, int y0, int x1, int y1, int period, int offset) {
    int dx = x1 - x0 < 0 ? x0 - x1 : x1 - x0;
    int dy = y1 - y0 < 0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    int step = 0;
    for (;;) {
        if (period <= 1 || ((step + offset) % period) == 0)
            px(fb, x0, y0, true);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
        step++;
    }
}

static uint32_t town_hash(uint32_t a, uint32_t b) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x85EBCA6Bu);
    h ^= h >> 15;
    h *= 0x2545F491u;
    h ^= h >> 13;
    return h;
}

/*
 * A sine over a 256-step turn, scaled to +/-127, linearly interpolated
 * between sixteen samples. There are no floats anywhere in this build and a
 * ridge line drawn out of hashes is noise rather than landscape, so the one
 * smooth periodic function the town needs is spelled out here.
 */
static int isin(uint32_t phase) {
    static const int16_t table[17] = {0,   49,  90,   117,  127,  117, 90,  49, 0,
                                      -49, -90, -117, -127, -117, -90, -49, 0};
    uint32_t p = phase & 255u;
    uint32_t i = p >> 4;
    uint32_t f = p & 15u;
    return (int)((table[i] * (int)(16u - f) + table[i + 1] * (int)f) / 16);
}

/* ---- sky ----------------------------------------------------------------- */

static bool sky_is_night(uint8_t phase) {
    return phase == DUEL_SKY_DUSK || phase == DUEL_SKY_NIGHT;
}

/*
 * The celestial body rides a sixteen-step arc, the same one the panels draw:
 * four sky phases of four sub-phases each. It is drawn large because it is
 * the only thing in the upper sky that is there every frame, and a six-pixel
 * disc on a 256-pixel square is a punctuation mark rather than a sun.
 */
static void draw_celestial(town_fb_t *fb, uint8_t phase, uint8_t sub, uint32_t frame) {
    int step = phase * 4 + sub;
    int offset = 2 * step - 15;
    int cx = 26 + step * 13 + (CANVAS_W - TOWN_W) * step / 15;
    int cy = 38 + offset * offset * 110 / 225;

    if (sky_is_night(phase)) {
        /* A disc with a second disc bitten out of it, and craters punched
         * into what is left. The bite tracks the sub-phase, so the moon is a
         * different moon at dusk and at midnight. */
        int bite = 3 + sub;
        disc(fb, cx, cy, 10, true);
        ring(fb, cx, cy, 11, true);
        disc(fb, cx + bite + 2, cy - 2, 9, false);
        static const int8_t crater[3][3] = {{-4, 1, 2}, {-2, -5, 1}, {-6, -1, 1}};
        for (int i = 0; i < 3; i++)
            disc(fb, cx + crater[i][0], cy + crater[i][1], crater[i][2], false);
        return;
    }

    disc(fb, cx, cy, 9, true);
    /* A corona rather than eight ticks: twelve rays around the turn, the odd
     * ones short, all of them breathing on a slow frame count so the sun
     * reads as burning and not as a printed asterisk. */
    for (int i = 0; i < 12; i++) {
        uint32_t a = (uint32_t)i * 256u / 12u;
        int dx = isin(a + 64u);
        int dy = isin(a);
        int len = (i & 1) ? 15 : 19;
        len += (int)(((frame >> 2) + (uint32_t)i * 5u) % 3u);
        for (int d = 12; d <= len; d++)
            px(fb, cx + dx * d / 127, cy + dy * d / 127, true);
    }
    shade_disc(fb, cx, cy, 12, 4);
}

/*
 * Stars, and the figures a city draws between them.
 *
 * Fixed positions from the session seed, so a city's sky is its own and stays
 * put; only the twinkle moves. The constellations are struck from the same
 * seed and joined with a broken line, which is what keeps four dots reading
 * as a figure instead of as four more dots.
 */
static void draw_stars(town_fb_t *fb, const duel_render_t *r, uint8_t phase, uint32_t frame) {
    if (!sky_is_night(phase))
        return;
    for (uint32_t i = 0; i < 70u; i++) {
        uint32_t h = town_hash(r->seed, i);
        int x = (int)(h % (uint32_t)(CANVAS_W - 6)) + 3;
        int y = (int)((h >> 9) % 168u) + 4;
        if (((h >> 20) & 7u) == 0u && ((frame >> 3) + i) % 9u == 0u)
            continue; /* an occasional slow twinkle */
        px(fb, x, y, true);
        if (((h >> 24) & 3u) == 0u) {
            /* The bright ones get four points, so the sky has two magnitudes
             * and not one. */
            px(fb, x + 1, y, true);
            px(fb, x - 1, y, true);
            px(fb, x, y + 1, true);
            px(fb, x, y - 1, true);
        }
    }

    for (uint32_t c = 0; c < 3u; c++) {
        uint32_t h = town_hash(r->seed ^ 0xC0FFEEu, c);
        int ox = (int)(h % (uint32_t)(CANVAS_W - 86)) + 20;
        int oy = (int)((h >> 8) % 90u) + 14;
        int px_prev = 0, py_prev = 0;
        int count = 4 + (int)((h >> 17) & 1u);
        for (int s = 0; s < count; s++) {
            uint32_t g = town_hash(h, (uint32_t)s);
            int sx = ox + (int)(g % 46u) - 23;
            int sy = oy + (int)((g >> 7) % 34u) - 17;
            disc(fb, sx, sy, 1, true);
            if (s > 0)
                line_step(fb, px_prev, py_prev, sx, sy, 3, 0);
            px_prev = sx;
            py_prev = sy;
        }
    }
}

/*
 * Clouds as silhouettes rather than outlines. Each bank is a run of
 * overlapping lobes with a flat base; the top edge of their union is drawn
 * solid and the body under it is shaded light, which is what tells a cloud
 * from a building at this size -- the old outlined lozenge did not.
 */
static void draw_clouds(town_fb_t *fb, const duel_render_t *r, uint8_t phase, uint32_t frame) {
    if (phase == DUEL_SKY_NIGHT)
        return;
    static const uint8_t bank_y[3] = {22, 52, 82};
    static const uint8_t bank_lobes[3] = {4, 3, 5};
    for (int bank = 0; bank < 3; bank++) {
        uint32_t h = town_hash(r->seed, (uint32_t)bank + 300u);
        int lobes = (int)bank_lobes[bank];
        int base = (int)bank_y[bank];
        /* Slower banks sit lower, so the sky has parallax as well as depth. */
        int speed = 3 + bank;
        int width = lobes * 13 + 14;
        int x0 =
            (int)((((frame * (uint32_t)speed) >> 6) + (h & 511u)) % (uint32_t)(CANVAS_W + 120)) -
            60;

        int top[LANDSCAPE_W];
        for (int i = 0; i < CANVAS_W; i++)
            top[i] = -1;
        for (int lobe = 0; lobe < lobes; lobe++) {
            uint32_t g = town_hash(h, (uint32_t)lobe);
            int cx = x0 + 9 + lobe * 13;
            int radius = 6 + (int)(g % 4u);
            int cy = base - (int)((g >> 5) % 4u);
            for (int x = cx - radius; x <= cx + radius; x++) {
                if (x < 0 || x >= CANVAS_W)
                    continue;
                int dx = x - cx;
                int rise = 0;
                while ((rise + 1) * (rise + 1) + dx * dx <= radius * radius)
                    rise++;
                int y = cy - rise;
                if (top[x] < 0 || y < top[x])
                    top[x] = y;
            }
        }
        for (int x = x0; x <= x0 + width; x++) {
            if (x < 0 || x >= CANVAS_W || top[x] < 0)
                continue;
            px(fb, x, top[x], true);
            for (int y = top[x] + 1; y <= base + 2; y++)
                if (shade_on(x, y, y > base ? 2 : 4))
                    px(fb, x, y, true);
        }
        hline(fb, x0 + 4 > 0 ? x0 + 4 : 0, x0 + width - 4, base + 3);
    }
}

/* Birds, in the hours a bird is up. Two strokes each, the wing angle from the
 * frame, drifting across and wrapping. They cost eight pixels and they are
 * the difference between air and empty space. */
static void draw_birds(town_fb_t *fb, const duel_render_t *r, uint8_t phase, uint32_t frame) {
    if (sky_is_night(phase))
        return;
    for (uint32_t i = 0; i < 5u; i++) {
        uint32_t h = town_hash(r->seed, i + 700u);
        int y = 62 + (int)(h % 56u);
        int x = (int)((((frame >> 3) + (h >> 6)) % (uint32_t)(CANVAS_W + 40))) - 20;
        int flap = (int)(((frame >> 2) + i * 3u) & 3u);
        int rise = flap == 0 || flap == 2 ? 1 : 2;
        px(fb, x, y, true);
        px(fb, x - 1, y - rise, true);
        px(fb, x + 1, y - rise, true);
        px(fb, x - 2, y - rise - (rise > 1 ? 1 : 0), true);
        px(fb, x + 2, y - rise - (rise > 1 ? 1 : 0), true);
    }
}

/*
 * Hills, and the rival's spire on them.
 *
 * Two ridges of summed sines, shaded a sixteenth and an eighth, standing
 * behind the far row. They are what stops the middle of the square from being
 * a horizontal join between a sky and a street: the town now sits in a
 * landscape, and the landscape is the same one every frame because the ridge
 * is a function of x and the seed alone.
 */
static void draw_hills(town_fb_t *fb, const duel_render_t *r) {
    /*
     * The ridges have to clear the near row's rooflines or they are a texture
     * nobody sees: a hill that only shows through the gaps between houses is
     * not a horizon. The far crest runs about forty pixels above the eaves,
     * which puts it against the sky along its whole length.
     */
    uint32_t seed = r->seed;
    for (int x = 0; x < CANVAS_W; x++) {
        int far = HILL_BASE_Y - 44 - (isin((uint32_t)x * 2u + seed * 7u) * 14) / 127 -
                  (isin((uint32_t)x * 5u + seed * 3u) * 5) / 127;
        int near = HILL_BASE_Y - 22 - (isin((uint32_t)x * 3u + seed * 11u + 90u) * 11) / 127 -
                   (isin((uint32_t)x * 7u + seed) * 4) / 127;
        for (int y = far; y <= FAR_GROUND_Y; y++)
            if (shade_on(x, y, y < near ? 1 : 3))
                px(fb, x, y, true);
        /* The crest itself is solid: a shaded mass with no edge reads as
         * dirt on the sky rather than as a skyline. */
        px(fb, x, far, true);
        px(fb, x, near, true);
    }

    /* One far spire on the ridge: the other champion is somewhere, and a
     * horizon with a landmark on it is a place rather than a backdrop. */
    /* Clear of the near row's chimneys either side: the spire and a smoking
     * chimney at the same x read as one confused object. */
    int sx = TOWN_X((r->seed & 1u) ? 224 : 70);
    int sy = HILL_BASE_Y - 22 - (isin((uint32_t)sx * 3u + seed * 11u + 90u) * 11) / 127 -
             (isin((uint32_t)sx * 7u + seed) * 4) / 127;
    shade_rect(fb, sx - 3, sy - 16, sx + 3, sy, 9);
    frame_rect(fb, sx - 3, sy - 16, sx + 3, sy);
    for (int i = 0; i <= 4; i++)
        hline(fb, sx - 3 + i, sx + 3 - i, sy - 16 - i);
    /* Its beacon answers when the other champion is working. */
    duel_view_wizard_t rival = duel_view_wizard(&r->view, SIM_SIDE_R);
    if (rival.pose == POSE_CAST || rival.inc_state == INC_WINDUP)
        disc(fb, sx, sy - 23, 2, true);
}

/* ---- the town around the tower ------------------------------------------ */

typedef struct {
    int16_t x0;
    int16_t width;
    int16_t height;
    uint8_t chimney; /* 0 none, otherwise the offset from x0 */
    uint8_t roof;    /* 0 pitched, 1 stepped gable, 2 flat with a parapet */
    uint8_t sign;    /* hangs a bracket and a board off the facade */
} town_building_t;

/* Two rows. The far row stands on the higher ground line and is eighth-toned;
 * the near row is solid and outlined. The gaps either side of the centre are
 * the approach to the tower door. */
/* Placed to show through the approaches either side of the tower and past the
 * ends of the near row, since a far building nothing can see is just cost. */
static const town_building_t far_row[] = {
    {2, 18, 16, 0, 0, 0},   {28, 13, 24, 0, 1, 0},  {88, 19, 22, 0, 0, 0},  {148, 16, 18, 0, 2, 0},
    {172, 12, 27, 0, 1, 0}, {196, 15, 14, 0, 0, 0}, {236, 18, 20, 0, 0, 0},
};
/* From duel_town_life.h, which the residents' places read too, so a door the
 * residents walk to is always where its house is drawn. */
static const town_building_t near_row[] = {
    {TOWN_ROW_HOUSE_W}, {TOWN_ROW_HOUSE_M}, {TOWN_ROW_SMITHY},
    {TOWN_ROW_TAVERN},  {TOWN_ROW_HOUSE_E},
};

static void draw_far_row(town_fb_t *fb) {
    for (size_t i = 0; i < sizeof far_row / sizeof far_row[0]; i++) {
        town_building_t placed = far_row[i];
        placed.x0 = (int16_t)TOWN_X(placed.x0);
        const town_building_t *b = &placed;
        int x1 = b->x0 + b->width;
        int top = FAR_GROUND_Y - b->height;
        shade_rect(fb, b->x0, top, x1, FAR_GROUND_Y, 8);
        hline(fb, b->x0, x1, top);
        vline(fb, b->x0, top, FAR_GROUND_Y);
        vline(fb, x1, top, FAR_GROUND_Y);
        /* A roof shape even at this distance: the silhouette is the only
         * thing a far building gets to say. */
        if (b->roof == 1)
            for (int s = 0; s <= b->width / 2; s++)
                hline(fb, b->x0 + s, x1 - s, top - s / 2);
        else if (b->roof == 2)
            for (int x = b->x0; x <= x1; x += 3)
                px(fb, x, top - 2, true);
    }
    /* The far ground itself: a broken line, so it reads as distance rather
     * than as a second street. */
    for (int x = 0; x < CANVAS_W; x += 3)
        px(fb, x, FAR_GROUND_Y, true);
}

/* Puffs, not a plume: three of them, well separated, leaning further as they
 * rise and thinning out at the top. A continuous column reads as a mast. */
static void draw_smoke_typed(town_fb_t *fb, int x, int base_y, uint32_t frame, uint32_t salt,
                             uint8_t spread);

static void draw_smoke(town_fb_t *fb, int x, int base_y, uint32_t frame, uint32_t salt,
                       uint8_t spread) {
    if (spread != DUEL_CITY_SPREAD_NONE) {
        draw_smoke_typed(fb, x, base_y, frame, salt, spread);
        return;
    }
    /*
     * Four puffs on a short run. The run is short deliberately: a long one
     * with the old quadratic drift left a dotted diagonal reaching halfway up
     * the sky, which read as a spell trail rather than as a chimney.
     */
    for (int puff = 0; puff < 4; puff++) {
        uint32_t age = ((frame >> 3) + (uint32_t)puff * 4u + salt) % 16u;
        int y = base_y - 3 - (int)age;
        int drift = (int)(age / 3u);
        int wobble = ((age + salt) & 3u) == 0u ? 1 : 0;
        int radius = age < 5u ? 1 : 2;
        /* It thins as it climbs: solid at the chimney, shaded above it,
         * nothing at all by the top of the run. */
        if (age < 6u)
            disc(fb, x + drift + wobble, y, radius, true);
        else
            shade_disc(fb, x + drift + wobble, y, radius, age < 11u ? 9 : 5);
    }
}

static void draw_near_row(town_fb_t *fb, const duel_render_t *r, uint8_t spread, uint32_t frame) {
    uint8_t mode = DUEL_CIVIC_MODE(r->civic);
    bool night = sky_is_night(DUEL_SECONDARY_SKY_PHASE(r->secondary));
    for (size_t i = 0; i < sizeof near_row / sizeof near_row[0]; i++) {
        town_building_t placed = near_row[i];
        placed.x0 = (int16_t)TOWN_X(placed.x0);
        const town_building_t *b = &placed;
        int x1 = b->x0 + b->width;
        int top = GROUND_Y - b->height;
        fill_rect(fb, b->x0, top, x1, GROUND_Y, false); /* clear the hills behind */
        frame_rect(fb, b->x0, top, x1, GROUND_Y);

        if (b->roof == 1) {
            /* A stepped gable, which is a different town from a row of
             * identical pitched boxes. */
            int steps = 4;
            for (int s = 0; s < steps; s++) {
                int inset = s * b->width / (2 * steps);
                int ry = top - 3 - s * 3;
                hline(fb, b->x0 + inset, x1 - inset, ry);
                vline(fb, b->x0 + inset, ry, ry + 3);
                vline(fb, x1 - inset, ry, ry + 3);
            }
        } else if (b->roof == 2) {
            hline(fb, b->x0 - 1, x1 + 1, top - 3);
            vline(fb, b->x0 - 1, top - 3, top);
            vline(fb, x1 + 1, top - 3, top);
            for (int x = b->x0; x <= x1; x += 4)
                vline(fb, x, top - 6, top - 4); /* a parapet with merlons */
        } else {
            for (int step = 0; step <= b->width / 2; step++) {
                px(fb, b->x0 + step, top - step / 2, true);
                px(fb, x1 - step, top - step / 2, true);
            }
            /* Courses of tile under the ridge, so the pitch has a surface. */
            for (int step = 2; step <= b->width / 2; step += 3)
                hline(fb, b->x0 + step, x1 - step, top - step / 2);
            /* A dormer, because a long roof with nothing on it reads as a
             * wedge rather than as somewhere anybody lives. */
            if (b->width > 34) {
                int dx = b->x0 + b->width / 2;
                fill_rect(fb, dx - 3, top - 9, dx + 3, top - 4, false);
                frame_rect(fb, dx - 3, top - 9, dx + 3, top - 4);
                for (int s = 0; s <= 3; s++)
                    hline(fb, dx - 3 + s, dx + 3 - s, top - 9 - s);
                if (mode != DUEL_CIVIC_MODE_QUIET)
                    fill_rect(fb, dx - 2, top - 8, dx + 2, top - 5, true);
            }
        }
        hline(fb, b->x0, x1, top);

        /* Half-timbering: a beam course and a few uprights. The plaster
         * between them is left dark on purpose -- shading it as well turned
         * every house into a textured slab and the row lost its silhouettes,
         * which are the only thing at this size that says 'houses'. */
        shade_rect(fb, b->x0 + 1, top + 7, x1 - 1, GROUND_Y - 1, 1);
        hline(fb, b->x0, x1, top + 6);
        for (int x = b->x0 + 7; x < x1 - 3; x += 13)
            vline(fb, x, top + 7, GROUND_Y - 1);

        /* Two shuttered windows and a door, lit unless the town is quiet. */
        int wy = top + 9;
        bool lit = mode != DUEL_CIVIC_MODE_QUIET;
        for (int w = 0; w < 2; w++) {
            int wx = b->x0 + 6 + w * (b->width - 16);
            fill_rect(fb, wx, wy, wx + 5, wy + 6, false);
            frame_rect(fb, wx, wy, wx + 5, wy + 6);
            if (lit && ((town_hash((uint32_t)i, (uint32_t)w) >> 3) & 3u) != 0u) {
                fill_rect(fb, wx + 1, wy + 1, wx + 4, wy + 5, true);
                px(fb, wx + 2, wy + 3, false); /* a mullion, so it is glazed */
                px(fb, wx + 3, wy + 3, false);
            } else {
                vline(fb, wx + 2, wy + 1, wy + 5);
            }
            /* A sill, which is one line instead of the two shutters that
             * were crowding the window from both sides. */
            hline(fb, wx - 1, wx + 6, wy + 7);
        }
        int dx = b->x0 + b->width / 2;
        fill_rect(fb, dx - 3, GROUND_Y - 11, dx + 3, GROUND_Y, false);
        frame_rect(fb, dx - 3, GROUND_Y - 11, dx + 3, GROUND_Y);
        arch(fb, dx, GROUND_Y - 11, 3);
        px(fb, dx + 2, GROUND_Y - 5, true); /* a handle */

        /* A hanging sign on a bracket: the near row is a street of trades. */
        if (b->sign) {
            int gx = b->x0 + b->width - 4;
            hline(fb, gx - 6, gx, top + 13);
            vline(fb, gx - 6, top + 13, top + 15);
            frame_rect(fb, gx - 9, top + 15, gx - 3, top + 20);
            shade_rect(fb, gx - 8, top + 16, gx - 4, top + 19, 6);
        }

        if (b->chimney) {
            int cx = b->x0 + b->chimney;
            int ctop = top - b->width / 4 - 6;
            fill_rect(fb, cx, ctop, cx + 4, top - 2, true);
            hline(fb, cx - 1, cx + 5, ctop); /* a cap */
            draw_smoke(fb, cx + 2, ctop, frame, (uint32_t)i * 13u, spread);
        }
        (void)night;
    }
}

/* ---- the tower ----------------------------------------------------------- */

/*
 * The shaft is three storeys deep, and the middle one is the one you are on.
 *
 * Four equal rooms gave every floor the same fifteen pixels and made the
 * lit one no more important than the three it was stacked with. The tower
 * shows the active floor and its two neighbours instead, and the active one
 * is nearly twice the height of either -- so the storey the host is actually
 * on is the storey with room in it for detail, and changing floors re-cuts
 * the whole shaft rather than moving a highlight up a ladder.
 *
 * The ends of the tower are still ends. Above the top floor is the loft and
 * below the ground floor is the cellar, so the three slots are always filled
 * and the geometry never shifts under the composition.
 */
#define TOWER_SLOTS  3
#define ROOM_TOP_Y   104
#define ROOM_BAND_H  6
#define ROOM_SMALL_H 15
#define ROOM_LARGE_H 28
#define ROOM_X0      (TOWER_X0 + 3)
#define ROOM_X1      (TOWER_X1 - 3)
#define ROOM_BASE_Y  (ROOM_TOP_Y + 3 * ROOM_BAND_H + 2 * ROOM_SMALL_H + ROOM_LARGE_H)

/* What a storey shows is a room: one of the eight districts, numbered as
 * duel_host.h numbers them, or one of the two ends of the tower that are not
 * districts at all. The four floors are the first four districts, so a
 * neighbour storey named by its floor is already a room. */
#define ROOM_LOFT   DUEL_DISTRICT_COUNT
#define ROOM_CELLAR (DUEL_DISTRICT_COUNT + 1)

/*
 * Drawing inside a room, lit or not.
 *
 * Every room is dark, as the rest of the town is. A lit one has its furniture
 * drawn solid by its own light; an unlit one has the same furniture dimly
 * picked out. One set of shapes at two strengths, so a storey does not have
 * to be drawn twice and the two cannot drift apart. A lit room used to be a
 * white block with its furniture in silhouette, the one inverted patch in the
 * picture.
 */
static void room_px(town_fb_t *fb, int x, int y, bool lit) {
    if (lit || shade_on(x, y, 6))
        px(fb, x, y, true);
}

/* A detail cut back out of furniture already drawn: a hoop on a cask, a
 * star on a chart. */
static void room_cut_hline(town_fb_t *fb, int x0, int x1, int y) {
    for (int x = x0; x <= x1; x++)
        px(fb, x, y, false);
}

static void room_hline(town_fb_t *fb, int x0, int x1, int y, bool lit) {
    for (int x = x0; x <= x1; x++)
        room_px(fb, x, y, lit);
}

static void room_vline(town_fb_t *fb, int x, int y0, int y1, bool lit) {
    for (int y = y0; y <= y1; y++)
        room_px(fb, x, y, lit);
}

static void room_rect(town_fb_t *fb, int x0, int y0, int x1, int y1, bool lit) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            room_px(fb, x, y, lit);
}

static void room_line(town_fb_t *fb, int x0, int y0, int x1, int y1, bool lit) {
    int dx = x1 - x0 < 0 ? x0 - x1 : x1 - x0;
    int dy = y1 - y0 < 0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        room_px(fb, x0, y0, lit);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void room_box(town_fb_t *fb, int x0, int y0, int x1, int y1, bool lit) {
    room_hline(fb, x0, x1, y0, lit);
    room_hline(fb, x0, x1, y1, lit);
    room_vline(fb, x0, y0, y1, lit);
    room_vline(fb, x1, y0, y1, lit);
}

static void room_ring(town_fb_t *fb, int cx, int cy, int radius, bool lit) {
    int rr = radius * radius;
    int inner = (radius - 1) * (radius - 1);
    for (int y = -radius; y <= radius; y++)
        for (int x = -radius; x <= radius; x++) {
            int d = x * x + y * y;
            if (d <= rr && d > inner)
                room_px(fb, cx + x, cy + y, lit);
        }
}

static void room_arch(town_fb_t *fb, int cx, int y, int radius, bool lit) {
    for (int x = -radius; x <= radius; x++) {
        int rise = 0;
        while ((rise + 1) * (rise + 1) + x * x <= radius * radius)
            rise++;
        room_px(fb, cx + x, y - rise, lit);
    }
}

/* A course of brick between two storeys: two courses of stretchers with the
 * perpends staggered, which is what a wall does and what a blank spacer
 * never said. */
static void draw_brick_band(town_fb_t *fb, int y) {
    for (int course = 0; course < 2; course++) {
        int cy = y + course * 3;
        for (int x = TOWER_X0 + 1; x < TOWER_X1; x++)
            if (((x + cy) % 9) != 0)
                px(fb, x, cy, true);
        for (int x = TOWER_X0 + 2 + course * 4; x < TOWER_X1; x += 8)
            vline(fb, x, cy + 1, cy + 2);
        shade_rect(fb, TOWER_X0 + 1, cy + 1, TOWER_X1 - 1, cy + 2, 2);
    }
}

/* Where each room's own light is: the fire, a lamp on the specimen cabinet,
 * the still's burner, the orb, a candle on the lectern, the glow over the
 * prism, a lantern over the ring, the furnace under the pipes, and a lantern
 * in the loft and the cellar. */
static void room_light(int room, int x0, int y0, int floor_y, int *lx, int *ly) {
    switch (room) {
        case DUEL_DISTRICT_COMMONS:
            *lx = x0 + 6;
            *ly = floor_y - 3;
            break;
        case DUEL_DISTRICT_RESEARCH:
            *lx = x0 + 26;
            *ly = floor_y - 13;
            break;
        case DUEL_DISTRICT_WORKSHOP:
        case DUEL_DISTRICT_OBSERVATORY:
            *lx = x0 + 9;
            *ly = floor_y - 9;
            break;
        case DUEL_DISTRICT_SCRIPTORIUM:
            *lx = x0 + 24;
            *ly = floor_y - 10;
            break;
        case DUEL_DISTRICT_STUDIO:
            *lx = x0 + 26;
            *ly = floor_y - 11;
            break;
        case DUEL_DISTRICT_ARENA:
            *lx = x0 + 10;
            *ly = floor_y - 12;
            break;
        case DUEL_DISTRICT_UNDERCROFT:
            *lx = x0 + 17;
            *ly = floor_y - 3;
            break;
        case ROOM_LOFT:
            *lx = x0 + 16;
            *ly = y0 + 6;
            break;
        default:
            *lx = x0 + 16;
            *ly = floor_y - 6;
            break;
    }
}

/*
 * The light in a lit room: a flame that flickers a pixel, and rings of dots
 * round it that thin out as they spread, so the light reads as radiating
 * rather than as a lit box. A dot lands only where nothing is lit next to it,
 * so the light never fills a piece of furniture in.
 */
static void draw_room_light(town_fb_t *fb, int room, int y0, int height, uint32_t frame) {
    int lx;
    int ly;
    room_light(room, ROOM_X0, y0, y0 + height - 1, &lx, &ly);
    int flick = (int)((frame >> 3) & 1u);
    fill_rect(fb, lx - 1, ly - 1 - flick, lx + 1, ly, true);
    px(fb, lx, ly - 2 - flick, true);

    int reach = (height >= ROOM_LARGE_H ? 16 : 11) + flick;
    int y1 = y0 + height - 1;
    for (int ring = 0; ring < 4; ring++) {
        int radius = 4 + ring * (reach - 4) / 3;
        int spacing = 2 + ring;
        int dots = (radius * 6) / spacing;
        for (int i = 0; i < dots; i++) {
            uint32_t a = (uint32_t)(i * 256 / dots) + (uint32_t)ring * 9u + (frame >> 4);
            int x = lx + isin(a + 64u) * radius / 127;
            int y = ly + isin(a) * radius / 127;
            if (x < ROOM_X0 || x > ROOM_X1 || y < y0 || y > y1)
                continue;
            bool clear = true;
            for (int ny = -1; ny <= 1 && clear; ny++)
                for (int nx = -1; nx <= 1 && clear; nx++)
                    if (town_fb_get(fb, x + nx, y + ny))
                        clear = false;
            if (clear)
                px(fb, x, y, true);
        }
    }
}

/*
 * What is in each room, by the district it is.
 *
 * The eight districts are eight different rooms rather than four floors with
 * a scene nobody can see: a hearth, an observing instrument, a workshop, the
 * top of the tower, a scriptorium, a music studio, a sparring ring, and the
 * undercroft the rest of the tower runs on. Each is the panels' district
 * (duel_environment_draw.c) carried into the town's wider room: the same two
 * silhouettes that tell it apart on the keyboard -- the rising telescope and
 * the specimen cabinet, the lectern and the scroll rack, the harp and the
 * prism stage, the railed ring and the stepped stand, the pipe run high
 * across the room over the lever bank and the valve -- so the desktop and
 * the keyboard agree about where the host is. Switching applications changes
 * the picture and not only which rectangle is bright.
 *
 * The storey you are on is the district; the storeys either side are the
 * floors above and below, read as plainly as the keyboard reads a bare floor.
 *
 * Everything on the floor is measured up from the floor and everything hung
 * from the ceiling is measured down from it, because the same room is drawn
 * at two heights and only the wall between them changes length. `roomy` is
 * the extra furniture the tall middle storey has space for: the room you are
 * on is not merely bigger, it has more in it.
 */
static void draw_room_contents(town_fb_t *fb, int x0, int y0, int height, int room, bool lit,
                               uint8_t intensity, uint32_t frame, uint8_t phase) {
    int floor_y = y0 + height - 1;
    bool roomy = height >= ROOM_LARGE_H;

    switch (room) {
        case DUEL_DISTRICT_COMMONS: {
            /* A hearth, alight, and a table laid under the window. */
            room_rect(fb, x0 + 1, floor_y - 8, x0 + 10, floor_y, lit);
            room_hline(fb, x0, x0 + 11, floor_y - 9, lit); /* the mantel */
            for (int i = 0; i < 8; i++)                    /* the fire, licking */
                room_px(fb, x0 + 2 + i, floor_y - 2 - (int)(((frame >> 2) + (uint32_t)i) % 2u),
                        lit);
            room_hline(fb, x0 + 15, x0 + 28, floor_y - 6, lit);
            room_hline(fb, x0 + 15, x0 + 28, floor_y - 5, lit);
            room_vline(fb, x0 + 17, floor_y - 4, floor_y, lit);
            room_vline(fb, x0 + 26, floor_y - 4, floor_y, lit);
            room_rect(fb, x0 + 12, floor_y - 3, x0 + 13, floor_y, lit);
            room_rect(fb, x0 + 30, floor_y - 3, x0 + 31, floor_y, lit);
            if (roomy) {
                /* Pots over the fire, a chair with a back, and a long bench. */
                room_hline(fb, x0 + 1, x0 + 10, y0 + 2, lit);
                room_vline(fb, x0 + 4, y0 + 3, y0 + 5, lit);
                room_rect(fb, x0 + 3, y0 + 6, x0 + 6, y0 + 8, lit);
                room_vline(fb, x0 + 8, y0 + 3, y0 + 4, lit);
                room_rect(fb, x0 + 20, floor_y - 14, x0 + 21, floor_y - 7, lit);
                room_hline(fb, x0 + 16, x0 + 27, floor_y + 1, lit);
            }
            break;
        }
        case DUEL_DISTRICT_RESEARCH: {
            /* A telescope on its tripod, rising across the room, and the
             * specimen cabinet that supports it. */
            room_line(fb, x0 + 4, floor_y - 5, x0 + 14, floor_y - 12, lit);
            room_line(fb, x0 + 4, floor_y - 4, x0 + 14, floor_y - 11, lit);
            room_rect(fb, x0 + 13, floor_y - 14, x0 + 16, floor_y - 11, lit);
            room_vline(fb, x0 + 8, floor_y - 7, floor_y, lit);
            room_line(fb, x0 + 8, floor_y - 3, x0 + 4, floor_y, lit);
            room_line(fb, x0 + 8, floor_y - 3, x0 + 12, floor_y, lit);
            room_box(fb, x0 + 21, floor_y - 11, x0 + 31, floor_y, lit);
            room_hline(fb, x0 + 21, x0 + 31, floor_y - 6, lit);
            for (int j = 0; j < 3; j++) {
                room_rect(fb, x0 + 23 + j * 3, floor_y - 9, x0 + 24 + j * 3, floor_y - 7, lit);
                room_rect(fb, x0 + 23 + j * 3, floor_y - 3, x0 + 24 + j * 3, floor_y - 1, lit);
            }
            if (roomy) {
                /* An orrery hung from the ceiling, its planets on their
                 * arcs, and a probe left on the boards. */
                room_vline(fb, x0 + 6, y0, y0 + 3, lit);
                room_arch(fb, x0 + 6, y0 + 9, 5, lit);
                room_px(fb, x0 + 6, y0 + 6, lit);
                room_px(fb, x0 + 2, y0 + 7, lit);
                room_px(fb, x0 + 10, y0 + 8, lit);
                room_ring(fb, x0 + 26, y0 + 4, 3, lit);
                room_px(fb, x0 + 26, y0 + 4, lit);
                room_rect(fb, x0 + 16, floor_y - 2, x0 + 18, floor_y, lit);
            }
            break;
        }
        case DUEL_DISTRICT_SCRIPTORIUM: {
            /* Shelves of books, and a lectern with a quill to copy one at. */
            int shelves = roomy ? 5 : 3;
            room_vline(fb, x0 + 1, y0 + 1, floor_y, lit);
            room_vline(fb, x0 + 13, y0 + 1, floor_y, lit);
            for (int shelf = 0; shelf < shelves; shelf++) {
                int sy = y0 + 3 + shelf * 4;
                room_hline(fb, x0 + 1, x0 + 13, sy, lit);
                for (int b = 0; b < 11; b += 2)
                    room_vline(fb, x0 + 2 + b, sy - 2, sy - 1, lit);
            }
            room_hline(fb, x0 + 19, x0 + 27, floor_y - 7, lit);
            room_hline(fb, x0 + 20, x0 + 28, floor_y - 6, lit);
            room_vline(fb, x0 + 24, floor_y - 5, floor_y, lit);
            room_hline(fb, x0 + 21, x0 + 27, floor_y, lit);
            room_line(fb, x0 + 27, floor_y - 8, x0 + 30, floor_y - 12, lit); /* the quill */
            if (roomy) {
                /* A scroll rack on the far wall and a stack of books left on
                 * the boards. */
                for (int i = 0; i < 4; i++)
                    room_rect(fb, x0 + 20 + i * 3, y0 + 2, x0 + 21 + i * 3, y0 + 7, lit);
                room_hline(fb, x0 + 19, x0 + 31, y0 + 8, lit);
                room_rect(fb, x0 + 29, floor_y - 4, x0 + 31, floor_y, lit);
            }
            break;
        }
        case DUEL_DISTRICT_WORKSHOP: {
            /* A bench with an alembic on it, tools on a rail, a barrel. */
            room_hline(fb, x0 + 2, x0 + 19, floor_y - 6, lit);
            room_vline(fb, x0 + 3, floor_y - 5, floor_y, lit);
            room_vline(fb, x0 + 18, floor_y - 5, floor_y, lit);
            room_rect(fb, x0 + 7, floor_y - 10, x0 + 12, floor_y - 7, lit);
            room_vline(fb, x0 + 9, floor_y - 13, floor_y - 11, lit);
            room_px(fb, x0 + 10, floor_y - 13, lit);
            room_hline(fb, x0 + 21, x0 + 30, y0 + 1, lit);
            for (int t = 0; t < 3; t++)
                room_vline(fb, x0 + 22 + t * 4, y0 + 2, y0 + 4 + t, lit);
            room_rect(fb, x0 + 23, floor_y - 5, x0 + 30, floor_y, lit);
            room_cut_hline(fb, x0 + 23, x0 + 30, floor_y - 3);
            if (roomy) {
                /* A second still, a bellows on the wall, and sacks under the
                 * bench. */
                room_rect(fb, x0 + 14, floor_y - 11, x0 + 17, floor_y - 7, lit);
                room_vline(fb, x0 + 15, floor_y - 14, floor_y - 12, lit);
                room_rect(fb, x0 + 25, y0 + 7, x0 + 29, y0 + 11, lit);
                room_hline(fb, x0 + 22, x0 + 25, y0 + 9, lit);
                room_rect(fb, x0 + 5, floor_y - 4, x0 + 8, floor_y, lit);
                room_rect(fb, x0 + 10, floor_y - 3, x0 + 13, floor_y, lit);
            }
            break;
        }
        case DUEL_DISTRICT_OBSERVATORY: {
            /* The top of a wizard's tower: an orb on its tripod and a glass
             * pointed at the sky it has all this height for.
             *
             * The glass is the same four-stage instrument the panels draw
             * from the civic intensity (duel_environment_draw.c): a calm
             * host leaves it nearly level over an empty sky, and each stage
             * up tilts it higher and finds one more star. On the keyboard
             * the stage is the host's workload; on the watch it is the body
             * bucket (R1), which is how the day shows in the tower. */
            int stage = intensity & 3;
            static const int glass_run[4] = {10, 10, 10, 7};
            static const int glass_rise[4] = {2, 5, 9, 9};
            room_rect(fb, x0 + 6, floor_y - 11, x0 + 12, floor_y - 6, lit);
            room_vline(fb, x0 + 6, floor_y - 5, floor_y, lit);
            room_vline(fb, x0 + 12, floor_y - 5, floor_y, lit);
            room_vline(fb, x0 + 9, floor_y - 5, floor_y, lit);
            for (int i = 0; i < glass_run[stage]; i++) {
                int y =
                    floor_y - 4 - (i * glass_rise[stage] + glass_run[stage] / 2) / glass_run[stage];
                room_px(fb, x0 + 20 + i, y, lit);
                room_px(fb, x0 + 20 + i, y + 1, lit);
            }
            room_vline(fb, x0 + 24, floor_y - 6, floor_y, lit);
            room_hline(fb, x0 + 22, x0 + 26, floor_y, lit);
            for (int star = 0; star < stage; star++)
                room_px(fb, x0 + 14 + star * 2, y0 + 1 + ((star + stage) & 1), lit);
            if (roomy) {
                /* A chart of the sky pinned to the wall, and a lamp on a
                 * chain over the orb. */
                room_rect(fb, x0 + 1, y0 + 2, x0 + 11, y0 + 9, lit);
                for (int i = 0; i < 5; i++) {
                    uint32_t h = town_hash((uint32_t)i, 3u);
                    px(fb, x0 + 2 + (int)(h % 9u), y0 + 3 + (int)((h >> 5) % 6u), false);
                }
                room_vline(fb, x0 + 22, y0 + 1, y0 + 4, lit);
                room_rect(fb, x0 + 20, y0 + 5, x0 + 24, y0 + 7, lit);
            }
            break;
        }
        case DUEL_DISTRICT_STUDIO: {
            /* A harp on the stage, its strings fanned to the crown, and a
             * prism on its plinth that the light is played through. */
            room_hline(fb, x0 + 1, x0 + 19, floor_y, lit);
            room_hline(fb, x0 + 3, x0 + 17, floor_y - 1, lit);
            room_line(fb, x0 + 4, floor_y - 2, x0 + 10, floor_y - 13, lit);
            room_line(fb, x0 + 16, floor_y - 2, x0 + 10, floor_y - 13, lit);
            for (int sx = x0 + 7; sx <= x0 + 13; sx += 3)
                room_line(fb, sx, floor_y - 2, x0 + 10, floor_y - 11, lit);
            room_rect(fb, x0 + 23, floor_y - 3, x0 + 29, floor_y, lit);
            for (int i = 0; i <= 4; i++)
                room_hline(fb, x0 + 26 - i, x0 + 26 + i, floor_y - 8 + i, lit);
            if (roomy) {
                /* What the prism throws up the wall, and a reel of the
                 * evening's music. */
                for (int k = 0; k < 3; k++)
                    for (int d = 2; d < 9; d += 2)
                        room_px(fb, x0 + 25 - k * 2 - d / 2, floor_y - 10 - d - k, lit);
                room_ring(fb, x0 + 24, y0 + 5, 4, lit);
                room_px(fb, x0 + 24, y0 + 5, lit);
                room_hline(fb, x0 + 28, x0 + 31, y0 + 5, lit);
            }
            break;
        }
        case DUEL_DISTRICT_ARENA: {
            /* The sparring ring is the one hollow mass -- posts and ropes,
             * not a block -- and the stand beside it the one staircase. */
            room_hline(fb, x0 + 1, x0 + 18, floor_y, lit);
            room_vline(fb, x0 + 2, floor_y - 9, floor_y, lit);
            room_vline(fb, x0 + 17, floor_y - 9, floor_y, lit);
            room_hline(fb, x0 + 2, x0 + 17, floor_y - 7, lit);
            room_hline(fb, x0 + 2, x0 + 17, floor_y - 4, lit);
            for (int tier = 0; tier < 3; tier++) {
                int top = floor_y - 3 - tier * 3;
                int tx = x0 + 21 + tier * 3;
                room_hline(fb, tx, x0 + 31, top, lit);
                room_vline(fb, tx, top, top + 3 > floor_y ? floor_y : top + 3, lit);
            }
            room_vline(fb, x0 + 31, floor_y - 9, floor_y, lit);
            if (roomy) {
                /* A canopy over the stand on its legs, and the tally orbs
                 * on their rising arc over the ring. */
                room_hline(fb, x0 + 20, x0 + 31, floor_y - 17, lit);
                room_arch(fb, x0 + 26, floor_y - 17, 5, lit);
                room_vline(fb, x0 + 21, floor_y - 16, floor_y - 10, lit);
                room_vline(fb, x0 + 30, floor_y - 16, floor_y - 10, lit);
                for (int i = 0; i < 5; i++)
                    room_rect(fb, x0 + 3 + i * 3, floor_y - 14 - (i * (4 - i)) / 2, x0 + 4 + i * 3,
                              floor_y - 13 - (i * (4 - i)) / 2, lit);
            }
            break;
        }
        case DUEL_DISTRICT_UNDERCROFT: {
            /* Two pipes across the whole room high up, which nothing else
             * has; the lever bank under them; and the riser broken round a
             * stop valve. */
            room_hline(fb, x0, x0 + 31, y0 + 2, lit);
            room_hline(fb, x0, x0 + 31, y0 + 5, lit);
            for (int x = x0 + 3; x <= x0 + 29; x += 6)
                room_vline(fb, x, y0, y0 + 1, lit);
            room_box(fb, x0 + 2, floor_y - 5, x0 + 14, floor_y, lit);
            for (int x = x0 + 4; x <= x0 + 12; x += 2)
                room_vline(fb, x, floor_y - 8, floor_y - 6, lit);
            int housing = floor_y - 9 < y0 + 7 ? y0 + 7 : floor_y - 9;
            for (int y = y0 + 6; y <= floor_y; y++)
                if (y < housing || y > floor_y - 3) {
                    room_px(fb, x0 + 23, y, lit);
                    room_px(fb, x0 + 29, y, lit);
                }
            room_box(fb, x0 + 21, housing, x0 + 31, floor_y - 3, lit);
            room_ring(fb, x0 + 26, (housing + floor_y - 3) / 2, 2, lit);
            if (roomy) {
                /* A junction plate on the pipes and vapour off the bank. */
                room_rect(fb, x0 + 22, y0 + 1, x0 + 30, y0 + 6, lit);
                room_cut_hline(fb, x0 + 23, x0 + 29, y0 + 3);
                for (int x = x0 + 5; x <= x0 + 13; x += 4)
                    room_px(fb, x, floor_y - 11 - ((x >> 2) & 1), lit);
            }
            break;
        }
        case ROOM_LOFT: {
            /* Above the top floor: rafters, and what gets put up here. */
            for (int i = 0; i <= 10; i++) {
                room_px(fb, x0 + 2 + i, y0 + 1 + i, lit);
                room_px(fb, x0 + 30 - i, y0 + 1 + i, lit);
            }
            room_rect(fb, x0 + 6, floor_y - 5, x0 + 12, floor_y, lit);
            room_rect(fb, x0 + 14, floor_y - 3, x0 + 18, floor_y, lit);
            room_rect(fb, x0 + 22, floor_y - 6, x0 + 27, floor_y, lit);
            room_cut_hline(fb, x0 + 22, x0 + 27, floor_y - 3);
            break;
        }
        default: {
            /* Below the ground floor: two barrel vaults and the casks under
             * them. */
            room_arch(fb, x0 + 8, floor_y, 7, lit);
            room_arch(fb, x0 + 24, floor_y, 7, lit);
            room_vline(fb, x0 + 1, floor_y - 7, floor_y, lit);
            room_vline(fb, x0 + 16, floor_y - 7, floor_y, lit);
            room_vline(fb, x0 + 31, floor_y - 7, floor_y, lit);
            room_rect(fb, x0 + 4, floor_y - 4, x0 + 11, floor_y, lit);
            room_rect(fb, x0 + 20, floor_y - 4, x0 + 27, floor_y, lit);
            break;
        }
    }

    /* Whoever is up there, crossing their room on the civic clock. The town
     * is occupied at street level and the tower never was. */
    if (lit && room < DUEL_DISTRICT_COUNT) {
        int span = ROOM_X1 - ROOM_X0 - 8;
        int floor = (int)duel_district_floor((uint8_t)room);
        int step = (int)((phase + (uint8_t)(floor * 37)) % (uint8_t)(2 * span));
        int wx = x0 + 4 + (step < span ? step : 2 * span - step);
        room_rect(fb, wx - 1, floor_y - 6, wx + 1, floor_y, lit);
        room_rect(fb, wx - 1, floor_y - 9, wx + 1, floor_y - 7, lit);
    }
}

/*
 * The ward, as a dome over the tower.
 *
 * ward_strength is up for about a third of every run and the town has never
 * shown it. It is drawn as an arc of dashes whose gaps close as the ward
 * thickens, which is legible at a glance and cannot be mistaken for
 * architecture: nothing else in the town is a curve that large.
 */
static void draw_ward(town_fb_t *fb, const duel_render_t *r, uint32_t frame) {
    duel_view_wizard_t wz = duel_view_wizard(&r->view, SIM_SIDE_L);
    if (!wz.ward_strength)
        return;
    /*
     * Two nested arcs over the top of the tower, and nothing below the
     * shoulders of the circle. Taken round to its widest points the dome
     * closed into a lens with a flat underside -- a hoop the tower was
     * wearing rather than a shield standing over it.
     */
    int period = 7 - (int)wz.ward_strength;
    if (period < 2)
        period = 2;
    int spin = (int)((frame >> 2) % (uint32_t)period);
    int shells = wz.ward_strength >= 4 ? 2 : 1;
    for (int shell = 0; shell < shells; shell++) {
        int radius = 64 + shell * 7;
        for (int a = 0; a < 256; a++) {
            int dy = isin((uint32_t)a);
            if (dy > -52) /* the crown of the circle only */
                continue;
            int dx = isin((uint32_t)a + 64u);
            if (((a + spin + shell * 2) % period) != 0)
                continue;
            px(fb, TOWER_CX + dx * radius / 127, WARD_CY + dy * radius / 127, true);
        }
    }
    /* Where the ward is focused reads as a thickening on that side. */
    int focus = wz.ward_focus == 1 ? -1 : wz.ward_focus == 2 ? 1 : 0;
    if (focus) {
        for (int a = 0; a < 256; a++) {
            int dy = isin((uint32_t)a);
            int dx = isin((uint32_t)a + 64u);
            if (dy > -52 || dx * focus < 30)
                continue;
            px(fb, TOWER_CX + dx * 60 / 127, WARD_CY + dy * 60 / 127, true);
        }
    }
}

/* ---- the typing summary --------------------------------------------------
 * Four bounded enums from the opt-in typing helper (docs/typing-summary.md).
 * Each one changes one thing in the town, and none of them says good or bad:
 * a fast typist gets a stiff wind, not a reward. Every function here is only
 * reached for a value other than none, so a town without the helper is drawn
 * by exactly the code that drew it before. */

/* Tempo is the wind: the faster the typing, the further and harder the
 * pennant streams, and the faster it ripples. */
static void draw_pennant_typed(town_fb_t *fb, int flag_y, uint8_t tempo, uint32_t frame) {
    if (tempo == DUEL_CITY_TEMPO_DELIBERATE) {
        /* Limp: it hangs down the pole and only sways at the hem. */
        int sway = isin(frame << 1) > 40 ? 1 : 0;
        for (int t = 0; t < 10; t++) {
            int width = 4 - t * 4 / 10;
            int lean = t >= 6 ? sway : 0;
            for (int x = 0; x <= width; x++)
                px(fb, TOWER_CX + 1 + x + lean, flag_y + t, true);
        }
        return;
    }
    static const struct {
        uint8_t length, amplitude, speed_shift_up, depth;
    } wind[DUEL_CITY_TEMPO_COUNT] = {
        [DUEL_CITY_TEMPO_FLOWING] = {14, 1, 1, 4},
        [DUEL_CITY_TEMPO_RAPID] = {18, 1, 3, 3},
        [DUEL_CITY_TEMPO_FRANTIC] = {22, 2, 4, 3},
    };
    int length = wind[tempo].length;
    uint32_t phase = frame << wind[tempo].speed_shift_up;
    for (int i = 0; i < length; i++) {
        int wave = isin((uint32_t)(i * 20) + phase) * wind[tempo].amplitude / 127;
        int depth = wind[tempo].depth - i * wind[tempo].depth / length;
        /* A gale splits the tail into a swallowtail. */
        bool forked = tempo == DUEL_CITY_TEMPO_FRANTIC && i >= length - 6;
        for (int t = 0; t <= depth; t++) {
            if (forked && t == depth / 2 + 1)
                continue;
            px(fb, TOWER_CX + 1 + i, flag_y + wave + t, true);
        }
        if (forked)
            px(fb, TOWER_CX + 1 + i, flag_y + wave + depth + 1, true);
    }
    if (tempo == DUEL_CITY_TEMPO_FRANTIC) {
        /* Wind streaks past the flag, so the gale reads when it holds still. */
        for (int s = 0; s < 3; s++) {
            int sx = TOWER_CX + 8 + (int)((frame * 3u + (uint32_t)s * 11u) % 24u);
            int sy = flag_y - 4 + s * 6;
            hline(fb, sx, sx + 5, sy);
        }
    }
}

/* Spread is the rhythm of the smoke: an even typist's chimneys puff in a
 * straight, evenly spaced column; an uneven one's come out in ragged bursts. */
static void draw_smoke_typed(town_fb_t *fb, int x, int base_y, uint32_t frame, uint32_t salt,
                             uint8_t spread) {
    static const uint8_t even_ages[4] = {0, 4, 8, 12};
    static const uint8_t ragged_ages[4] = {0, 2, 7, 13};
    static const uint8_t ragged_sizes[4] = {2, 1, 3, 1};
    for (int puff = 0; puff < 4; puff++) {
        const uint8_t *ages = spread == DUEL_CITY_SPREAD_IRREGULAR ? ragged_ages : even_ages;
        uint32_t age = ((frame >> 3) + ages[puff] + salt) % 16u;
        int y = base_y - 3 - (int)age;
        int dx = 0;
        int radius = 1;
        if (spread == DUEL_CITY_SPREAD_VARIED) {
            /* A lazy S: it sways rather than drifting off. */
            dx = isin(age * 24u + salt * 8u) * 2 / 127;
            radius = age < 7u ? 1 : 2;
        } else if (spread == DUEL_CITY_SPREAD_IRREGULAR) {
            dx = (int)(town_hash((uint32_t)puff + salt, frame >> 4) % 5u) - 2;
            radius = ragged_sizes[(puff + (int)(frame >> 5)) & 3];
        }
        if (age < 6u)
            disc(fb, x + dx, y, radius, true);
        else
            shade_disc(fb, x + dx, y, radius, age < 11u ? 9 : 5);
    }
}

/* Row is height: the busiest keyboard row lights the lantern at the matching
 * storey of the tower, from the eaves (top row) down to the doorstep (thumbs).
 * Row spread is how many lanterns hang: one alone with a glow when the typing
 * is focused on that row, a neighbour when it is mixed, all four when it is
 * even. A row spread with no row anchors on HOME, as a tie does. */
static int lantern_y(const town_fb_t *fb, uint8_t row) {
    switch (row) {
        case DUEL_CITY_ROW_TOP:
            return TOWER_TOP_Y + 6;
        case DUEL_CITY_ROW_HOME:
            return BALCONY_Y + 8;
        case DUEL_CITY_ROW_BOTTOM:
            return 152;
        default:
            return GROUND_Y - 16;
    }
}

static void draw_lantern(town_fb_t *fb, uint8_t row, bool lit, bool glow) {
    int y = lantern_y(fb, row);
    /* Off the right wall, clear of the buttress flare at the foot. */
    int wall = TOWER_X1 + (y > 150 ? (y - 150) * 6 / (GROUND_Y - 150) : 0);
    int x = wall + 6;
    /* A focused lantern clears a pool of dark around itself first, so its
     * halo reads against the hills' dither rather than vanishing into it.
     * The pool stops short of the wall it hangs from. */
    if (glow)
        fill_rect(fb, wall + 1, y - 3, x + 10, y + 15, false);
    hline(fb, wall + 1, x, y);
    px(fb, x, y + 1, true);
    fill_rect(fb, x - 3, y + 2, x + 3, y + 10, false);
    hline(fb, x - 2, x + 2, y + 2);
    frame_rect(fb, x - 2, y + 3, x + 2, y + 9);
    if (lit)
        fill_rect(fb, x - 1, y + 4, x + 1, y + 8, true);
    if (glow) {
        /* A dashed halo on the open side only. */
        for (uint32_t a = 0; a < 256u; a += 8u) {
            int dx = isin(a + 64u) * 9 / 127;
            int dy = isin(a) * 9 / 127;
            if (dx >= -2 && ((a >> 3) & 1u) == 0u)
                px(fb, x + dx, y + 6 + dy, true);
        }
    }
}

static void draw_lanterns(town_fb_t *fb, const town_typing_t *typing) {
    if (typing->row == DUEL_CITY_ROW_NONE && typing->row_spread == DUEL_CITY_ROW_SPREAD_NONE)
        return;
    uint8_t busiest = typing->row != DUEL_CITY_ROW_NONE ? typing->row : DUEL_CITY_ROW_HOME;
    if (typing->row_spread == DUEL_CITY_ROW_SPREAD_EVEN) {
        for (uint8_t row = DUEL_CITY_ROW_TOP; row < DUEL_CITY_ROW_COUNT; row++)
            if (row != busiest)
                draw_lantern(fb, row, false, false);
    } else if (typing->row_spread == DUEL_CITY_ROW_SPREAD_MIXED) {
        uint8_t neighbour = busiest == DUEL_CITY_ROW_THUMB ? busiest - 1u : busiest + 1u;
        draw_lantern(fb, neighbour, false, false);
    }
    draw_lantern(fb, busiest, true, typing->row_spread == DUEL_CITY_ROW_SPREAD_FOCUSED);
}

static void draw_tower(town_fb_t *fb, const duel_render_t *r, const town_typing_t *typing,
                       uint32_t frame) {
    uint8_t mode = DUEL_CIVIC_MODE(r->civic);

    fill_rect(fb, TOWER_X0 - 6, TOWER_TOP_Y, TOWER_X1 + 6, GROUND_Y, false);

    /* Buttresses first, so the shaft outline closes over them: the tower is
     * heavy and a 38-pixel column standing on a line does not look it. */
    for (int y = 150; y <= GROUND_Y; y++) {
        int flare = (y - 150) * 6 / (GROUND_Y - 150);
        px(fb, TOWER_X0 - flare, y, true);
        px(fb, TOWER_X1 + flare, y, true);
        shade_rect(fb, TOWER_X0 - flare, y, TOWER_X0, y, 3);
        shade_rect(fb, TOWER_X1, y, TOWER_X1 + flare, y, 3);
    }

    frame_rect(fb, TOWER_X0, TOWER_TOP_Y, TOWER_X1, GROUND_Y);
    /* Coursed stone on the upper shaft only. Below the balcony the wall is
     * the brick band between one room and the next, so running a course over
     * the whole shaft would draw the same stone twice. */
    for (int y = TOWER_TOP_Y + 6; y < ROOM_TOP_Y; y += 6) {
        for (int x = TOWER_X0 + 1; x < TOWER_X1; x++)
            if (((x + y) % 7) != 0)
                px(fb, x, y, true);
        for (int x = TOWER_X0 + 1 + ((y / 6) & 1) * 6; x < TOWER_X1; x += 12)
            vline(fb, x, y, y + 5);
    }

    /* Conical roof, then the spire and its finial. */
    for (int y = TOWER_TOP_Y; y >= ROOF_APEX_Y; y--) {
        int span = (y - ROOF_APEX_Y) * (TOWER_HALF + 4) / (TOWER_TOP_Y - ROOF_APEX_Y);
        px(fb, TOWER_CX - span, y, true);
        px(fb, TOWER_CX + span, y, true);
        /* Tiled, in courses that follow the cone. */
        if (((TOWER_TOP_Y - y) % 5) == 0)
            hline(fb, TOWER_CX - span, TOWER_CX + span, y);
    }
    shade_rect(fb, TOWER_CX - TOWER_HALF - 4, ROOF_APEX_Y, TOWER_CX + TOWER_HALF + 4, TOWER_TOP_Y,
               1);
    hline(fb, TOWER_CX - TOWER_HALF - 4, TOWER_CX + TOWER_HALF + 4, TOWER_TOP_Y);
    vline(fb, TOWER_CX, SPIRE_TIP_Y, ROOF_APEX_Y);
    hline(fb, TOWER_CX - 3, TOWER_CX + 3, SPIRE_TIP_Y + 4);
    px(fb, TOWER_CX, SPIRE_TIP_Y - 2, true);

    /* A pennant, rippling on the frame count. The only thing in the town that
     * says which way the wind is going. */
    int flag_y = SPIRE_TIP_Y + 6;
    if (typing->tempo != DUEL_CITY_TEMPO_NONE) {
        draw_pennant_typed(fb, flag_y, typing->tempo, frame);
    } else {
        for (int i = 0; i < 12; i++) {
            int wave = isin((uint32_t)(i * 18) + (frame >> 1)) * 2 / 127;
            int depth = 4 - i / 4;
            for (int t = 0; t <= depth; t++)
                px(fb, TOWER_CX + 1 + i, flag_y + wave + t, true);
        }
    }

    /* An urgent town lights its beacon; the pulse is the only thing on the
     * tower that moves without the world moving. */
    if (mode == DUEL_CIVIC_MODE_URGENT && ((frame >> 3) & 1u) == 0u) {
        disc(fb, TOWER_CX, SPIRE_TIP_Y - 6, 3, true);
        ring(fb, TOWER_CX, SPIRE_TIP_Y - 6, 5 + (int)((frame >> 2) & 3u), true);
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

    /* Balcony: a slab wider than the shaft, with a rail and corbels under it.
     * It is where the wizard stands and where a spell leaves from. */
    hline(fb, TOWER_CX - BALCONY_HALF, TOWER_CX + BALCONY_HALF, BALCONY_Y);
    hline(fb, TOWER_CX - BALCONY_HALF, TOWER_CX + BALCONY_HALF, BALCONY_Y + 1);
    hline(fb, TOWER_CX - BALCONY_HALF, TOWER_CX + BALCONY_HALF, BALCONY_Y - 5);
    for (int x = TOWER_CX - BALCONY_HALF; x <= TOWER_CX + BALCONY_HALF; x += 5)
        vline(fb, x, BALCONY_Y - 4, BALCONY_Y - 1);
    for (int i = 0; i < 4; i++) {
        int cx = TOWER_CX - 21 + i * 14;
        for (int s = 0; s < 3; s++)
            hline(fb, cx - 2 + s, cx + 2 - s, BALCONY_Y + 2 + s);
    }

    /*
     * The storeys. A course of brick, then a room seen through one wide
     * opening. The middle slot is the floor the host is on, drawn tall; the
     * slots either side are its neighbours, drawn short. Above the top floor
     * and below the ground floor those neighbours are the loft and the
     * cellar, so the shaft is always three storeys deep whichever floor is
     * active and the composition never changes height.
     */
    int active = (int)DUEL_CIVIC_FLOOR(r->civic);
    uint8_t intensity = DUEL_CIVIC_INTENSITY(r->civic);
    /* Slot 0 is the upper neighbour, 1 the active floor, 2 the lower one.
     * The active storey is the district the host scene makes of its floor;
     * the neighbours are floors, which are the first four rooms. */
    const int slot_room[TOWER_SLOTS] = {
        active + 1 > DUEL_CIVIC_FLOOR_SPECIAL ? ROOM_LOFT : active + 1,
        (int)duel_civic_district(r->civic, r->external),
        active - 1 < DUEL_CIVIC_FLOOR_COMMONS ? ROOM_CELLAR : active - 1,
    };
    const int slot_height[TOWER_SLOTS] = {ROOM_SMALL_H, ROOM_LARGE_H, ROOM_SMALL_H};

    int band_y = ROOM_TOP_Y;
    for (int slot = 0; slot < TOWER_SLOTS; slot++) {
        int height = slot_height[slot];
        int y0 = band_y + ROOM_BAND_H;
        /* The active storey is lit; a busy host lights the landings either
         * side of it as well, which is the same widening the intensity used
         * to do to a band of windows. */
        bool lit = slot == 1 || intensity >= DUEL_CIVIC_INTENSITY_BUSY;

        draw_brick_band(fb, band_y);

        fill_rect(fb, ROOM_X0 - 1, y0 - 1, ROOM_X1 + 1, y0 + height, false);
        frame_rect(fb, ROOM_X0 - 1, y0 - 1, ROOM_X1 + 1, y0 + height);
        draw_room_contents(fb, ROOM_X0, y0, height, slot_room[slot], lit, intensity, frame,
                           r->civic_phase);
        if (lit)
            draw_room_light(fb, slot_room[slot], y0, height, frame);
        /* Mullions, over the top of whatever is behind them. Without them a
         * lit storey is an opening in the wall rather than a window. */
        for (int m = 1; m < 3; m++) {
            int mx = ROOM_X0 + m * (ROOM_X1 - ROOM_X0) / 3;
            vline(fb, mx, y0 - 1, y0 + height);
        }
        /* The tall storey gets a transom, which is what a taller window has
         * and what keeps it from reading as a doorway. */
        if (height >= ROOM_LARGE_H)
            hline(fb, ROOM_X0 - 1, ROOM_X1 + 1, y0 + height / 3);

        band_y = y0 + height;
    }

    /*
     * The base: brick all the way to the ground, with the doorway cut out of
     * it. Left dark, the wall either side of the door was exactly as dark as
     * the door, and the entrance read as one of four identical panels rather
     * than as the way in.
     */
    for (int y = ROOM_BASE_Y; y < GROUND_Y; y += 3) {
        for (int x = TOWER_X0 + 1; x < TOWER_X1; x++)
            if (((x + y) % 9) != 0)
                px(fb, x, y, true);
        for (int x = TOWER_X0 + 2 + ((y / 3) & 1) * 4; x < TOWER_X1; x += 8)
            vline(fb, x, y + 1, y + 2);
        shade_rect(fb, TOWER_X0 + 1, y + 1, TOWER_X1 - 1, y + 2, 2);
    }

    /*
     * An arched opening with its head inside its own height, so the arch does
     * not rise into the room above it -- the doorway is cut through the base
     * course, not stacked on top of it.
     */
    int door_top = GROUND_Y - 18;
    fill_rect(fb, TOWER_CX - DOOR_W, door_top, TOWER_CX + DOOR_W, GROUND_Y, false);
    vline(fb, TOWER_CX - DOOR_W, door_top + DOOR_W, GROUND_Y);
    vline(fb, TOWER_CX + DOOR_W, door_top + DOOR_W, GROUND_Y);
    arch(fb, TOWER_CX, door_top + DOOR_W, DOOR_W);
    hline(fb, TOWER_CX - DOOR_W, TOWER_CX + DOOR_W, GROUND_Y);
    vline(fb, TOWER_CX, door_top + 3, GROUND_Y - 1); /* where the two leaves meet */
    px(fb, TOWER_CX - 2, GROUND_Y - 7, true);        /* and the ring to pull on */
    px(fb, TOWER_CX + 2, GROUND_Y - 7, true);
    /* A lamp over the door, lit after dusk like the ones on the square. */
    if (sky_is_night(DUEL_SECONDARY_SKY_PHASE(r->secondary))) {
        disc(fb, TOWER_CX, door_top - 3, 2, true);
        shade_disc(fb, TOWER_CX, door_top - 1, 8, 4);
    }
    /* Steps up to it, which is what makes the door a way in. */
    for (int s = 0; s < 3; s++)
        hline(fb, TOWER_CX - DOOR_W - 2 - s * 2, TOWER_CX + DOOR_W + 2 + s * 2, GROUND_Y + 1 + s);
}

/* ---- the health buckets -------------------------------------------------- */

/*
 * Health is mood, never a reading: nothing here flashes, nothing is drawn as
 * a warning, and no value looks like a worse version of another. Each bucket
 * has its own object in the town, clear of the typing art, so that body,
 * heart and sleep can be told apart at a glance on a watch.
 */

/* A kite: a solid diamond with its spars left dark, a tail of bows streaming
 * the way the pennant does, and a margin cleared round it so it reads over
 * cloud and sun alike. */
static void draw_kite(town_fb_t *fb, int cx, int cy, int half_w, int half_h, uint32_t frame,
                      uint32_t salt) {
    for (int dy = -half_h - 2; dy <= half_h + 2; dy++) {
        int ady = dy < 0 ? -dy : dy;
        int span = (half_w + 2) * (half_h + 2 - ady) / (half_h + 2);
        fill_rect(fb, cx - span, cy + dy, cx + span, cy + dy, false);
    }
    for (int dy = -half_h; dy <= half_h; dy++) {
        int ady = dy < 0 ? -dy : dy;
        int span = half_w * (half_h - ady) / half_h;
        hline(fb, cx - span, cx + span, cy + dy);
    }
    for (int dy = -half_h + 1; dy < half_h; dy++)
        px(fb, cx, cy + dy, false);
    for (int x = cx - half_w + 1; x < cx + half_w; x++)
        px(fb, x, cy - half_h / 3, false);
    for (int b = 1; b <= 4; b++) {
        int bx = cx + b * 2 + isin(frame * 6u + (uint32_t)b * 48u + salt * 70u) * 2 / 127;
        int by = cy + half_h + b * 4;
        hline(fb, bx - 1, bx + 1, by);
        px(fb, bx, by - 1, true);
        px(fb, bx, by + 1, true);
    }
}

/* Body is kites over the town: one up for each activity ring closed, the
 * first and third flown from the street right of the tower and the second
 * from the street left of it. The landscape has the width to spread them
 * further apart. At rest the one kite leans against a wall. */
static void draw_kites(town_fb_t *fb, uint8_t body, uint32_t frame) {
    static const struct {
        int16_t town_x, landscape_x, y, anchor;
        uint8_t half_w, half_h;
    } kites[3] = {
        {186, 252, 58, 216, 6, 9},
        {64, 146, 106, 30, 5, 8},
        {204, 312, 100, 216, 4, 6},
    };
    if (body == DUEL_CITY_BODY_NONE)
        return;
    int flying = (int)body - (int)DUEL_CITY_BODY_RESTING;
    if (flying == 0) {
        /* Grounded: stood on its tail against the house right of the tower,
         * the string wound on its stick at its foot. */
        int x = TOWN_X(160);
        draw_kite(fb, x, GROUND_Y - 26, 5, 8, 0u, 3u);
        fill_rect(fb, x + 5, GROUND_Y - 3, x + 7, GROUND_Y - 1, true);
        return;
    }
    for (int k = 0; k < flying; k++) {
        int bob = isin((frame << 2) + (uint32_t)k * 85u) * 2 / 127;
        int x = CANVAS_W == LANDSCAPE_W ? kites[k].landscape_x : kites[k].town_x;
        int y = kites[k].y + bob;
        line_step(fb, x, y + kites[k].half_h, TOWN_X(kites[k].anchor), GROUND_Y - 44, 2, k);
        draw_kite(fb, x, y, kites[k].half_w, kites[k].half_h, frame, (uint32_t)k);
    }
}

/* Heart is the windmill on the hill: sails furled to the bare lattice when
 * the heart is still, cloth out and turning when it is lively. Never a pulse
 * and never a number. */
static void draw_windmill(town_fb_t *fb, const duel_render_t *r, uint8_t heart, uint32_t frame) {
    if (heart == DUEL_CITY_HEART_NONE)
        return;
    uint32_t seed = r->seed;
    int mx = TOWN_X(92);
    int base = HILL_BASE_Y - 22 - (isin((uint32_t)mx * 3u + seed * 11u + 90u) * 11) / 127 -
               (isin((uint32_t)mx * 7u + seed) * 4) / 127;
    int top = base - 20;
    for (int y = top; y <= base; y++) {
        int half = 3 + (y - top) * 3 / 20;
        fill_rect(fb, mx - half, y, mx + half, y, false);
        px(fb, mx - half, y, true);
        px(fb, mx + half, y, true);
    }
    for (int s = 0; s <= 4; s++)
        hline(fb, mx - 4 + s, mx + 4 - s, top - s);     /* the cap */
    frame_rect(fb, mx - 1, base - 6, mx + 1, base - 1); /* the door */
    int hub_y = top + 1;
    bool lively = heart == DUEL_CITY_HEART_LIVELY;
    uint32_t turn = lively ? frame * 5u : 0u;
    for (uint32_t arm = 0; arm < 4u; arm++) {
        uint32_t a = 32u + arm * 64u + turn;
        int cx = isin(a + 64u);
        int cy = isin(a);
        int perp_x = -cy * 3 / 127;
        int perp_y = cx * 3 / 127;
        for (int t = 2; t <= 14; t++)
            px(fb, mx + t * cx / 127, hub_y + t * cy / 127, true);
        if (lively) {
            /* Cloth on the trailing side, and the sweep it has just made. */
            for (int t = 5; t <= 14; t++)
                for (int w = 1; w <= 3; w++)
                    px(fb, mx + t * cx / 127 + perp_x * w / 3,
                       hub_y + t * cy / 127 + perp_y * w / 3, true);
            for (uint32_t s = 8u; s <= 24u; s += 4u) {
                int sx = isin(a - s + 64u);
                int sy = isin(a - s);
                px(fb, mx + 17 * sx / 127, hub_y + 17 * sy / 127, true);
            }
        } else {
            /* Furled: only the lattice, a rung every third pixel and the
             * hill showing through between them. */
            for (int t = 5; t <= 14; t++)
                px(fb, mx + t * cx / 127 + perp_x, hub_y + t * cy / 127 + perp_y, (t % 3) == 2);
        }
    }
    disc(fb, mx, hub_y, 1, true);
}

/* Sleep is who is on the ridge of the house right of the tower the morning
 * after: a cockerel up and crowing after a rested night, a cat curled up
 * asleep after a tired one. */
static void draw_ridge_sleeper(town_fb_t *fb, uint8_t sleep, uint32_t frame) {
    if (sleep == DUEL_CITY_SLEEP_NONE)
        return;
    int x = TOWN_X(182);
    int y = GROUND_Y - 33 - 8; /* the ridge of the pitched roof */
    if (sleep == DUEL_CITY_SLEEP_RESTED) {
        fill_rect(fb, x - 6, y - 15, x + 6, y - 1, false);
        fill_rect(fb, x - 3, y - 6, x + 2, y - 3, true); /* the body */
        for (int t = 0; t < 4; t++)                      /* the tail, arched */
            vline(fb, x - 4 - t / 2, y - 8 + t, y - 4);
        px(fb, x - 6, y - 7, true);
        vline(fb, x + 2, y - 10, y - 6); /* the neck */
        fill_rect(fb, x + 2, y - 12, x + 3, y - 10, true);
        px(fb, x + 3, y - 13, true); /* the comb */
        px(fb, x + 2, y - 14, true);
        bool crowing = ((frame >> 4) & 1u) == 0u;
        px(fb, x + 4, y - 11 - (crowing ? 1 : 0), true); /* the beak */
        vline(fb, x - 1, y - 2, y);
        vline(fb, x + 1, y - 2, y);
    } else {
        /* Curled nose to tail, ears up, the tail wrapped round the front. */
        static const char *const cat[9] = {
            "..........X.X..", "..........XXX..", "...XXXXX..XXXX.",
            "..XXXXXXXXXXXXX", ".XXXXXXXXXX..XX", ".XXXXXXXXXXXXXX",
            "..XXXXXXXXXXXX.", "XX.............", ".XXXXXXXXXX....",
        };
        fill_rect(fb, x - 8, y - 9, x + 8, y + 1, false);
        for (int row = 0; row < 9; row++)
            for (int col = 0; cat[row][col]; col++)
                if (cat[row][col] == 'X')
                    px(fb, x - 7 + col, y - 8 + row, true);
        /* Breathing: the back rises a pixel now and then. */
        if (((frame >> 5) & 1u) == 0u)
            hline(fb, x - 3, x + 1, y - 7);
    }
}

/* ---- the wizard on the balcony ------------------------------------------- */

/*
 * The champion's status, on the balcony beside the figure, as the panels hang
 * it beside the tower: burning flames, a frost star, disruption's zigzags, the
 * mark's chevrons and scald's rising wisps. Each grows with the level, as the
 * panel icons do -- more flames, frost diagonals and then crossbars, a second
 * and third zigzag or chevron. The ground is cleared first so the mark owns
 * its pixels against the lit study behind it.
 */
static void draw_status_mark(town_fb_t *fb, const duel_view_wizard_t *wz, int cx, int feet,
                             uint32_t frame) {
    if (!wz->status || !wz->status_intensity)
        return;
    int level = wz->status_intensity;
    int sx = cx - 13, sy = feet - 14; /* clear of the tower wall at cx - 19 */
    fill_rect(fb, sx - 5, sy - 8, sx + 5, sy + 6, false);
    switch (wz->status) {
        case STATUS_BURNING:
            for (int i = 0; i < level; i++) {
                int fx = sx - 3 + i * 3;
                int h = 5 + (int)((frame + (uint32_t)i * 3u) % 3u);
                vline(fb, fx, sy + 5 - h, sy + 5);
                px(fb, fx + ((int)(frame >> 1) + i) % 2, sy + 4 - h, true);
            }
            break;
        case STATUS_FROZEN:
            hline(fb, sx - 4, sx + 4, sy);
            vline(fb, sx, sy - 4, sy + 4);
            if (level >= 2)
                for (int d = 1; d <= 3; d++)
                    for (int q = 0; q < 4; q++)
                        px(fb, sx + (q & 1 ? d : -d), sy + (q & 2 ? d : -d), true);
            if (level >= 3) {
                hline(fb, sx - 1, sx + 1, sy - 4);
                hline(fb, sx - 1, sx + 1, sy + 4);
                vline(fb, sx - 4, sy - 1, sy + 1);
                vline(fb, sx + 4, sy - 1, sy + 1);
            }
            break;
        case STATUS_DISRUPTED:
            for (int k = 0; k < level; k++) {
                int zy = sy + 3 - k * 4;
                for (int x = -5; x <= 5; x++)
                    px(fb, sx + x, zy - ((x + 5) / 2 % 2), true);
            }
            break;
        case STATUS_MARKED:
            for (int k = 0; k < level; k++) {
                int vy = sy + 4 - k * 4;
                for (int d = 0; d <= 3; d++) {
                    px(fb, sx - d, vy - d, true);
                    px(fb, sx + d, vy - d, true);
                }
            }
            break;
        default: /* scalded: three wisps sway as they rise, the last out of step */
            for (int w = 0; w < 3; w++)
                for (int k = 0; k < 7; k++)
                    px(fb, sx - 4 + w * 4 + (((int)(frame >> 1) + k / 2 + (w == 2)) & 1),
                       sy + 4 - k, true);
            break;
    }
}

/*
 * The spell flavor of the city's aftermath, as a sigil hung in the sky beside
 * the spire while the aftermath lasts: the panels' marks, doubled. Rune and
 * bloom share a diamond, familiar and echo a cup, a wall stands a bar, a
 * vortex hooks round, and a combination crosses.
 */
static void draw_flavor_sigil(town_fb_t *fb, const duel_render_t *r) {
    if (!(r->revision & INCANTATION_AFTERMATH_WIRE) ||
        INCANTATION_AFTER_KIND(r->shared_pres, SIM_SIDE_L) == AFTER_NONE)
        return;
    uint8_t flavor = INCANTATION_AFTERMATH_FLAVOR(r->revision);
    if (flavor == AFTER_FLAVOR_BASE)
        return;
    int mx = TOWER_CX - 32, my = 26;
    disc(fb, mx, my, 8, false);
    switch (flavor) {
        case AFTER_FLAVOR_RUNE:
        case AFTER_FLAVOR_BLOOM:
            for (int d = 0; d <= 5; d++) {
                px(fb, mx - 5 + d, my - d, true);
                px(fb, mx + 5 - d, my - d, true);
                px(fb, mx - 5 + d, my + d, true);
                px(fb, mx + 5 - d, my + d, true);
            }
            px(fb, mx, my, true);
            break;
        case AFTER_FLAVOR_FAMILIAR:
        case AFTER_FLAVOR_ECHO:
            for (int d = 0; d <= 5; d++)
                for (int t = 0; t < 2; t++) {
                    px(fb, mx - 5 + d, my - 2 + t + d * 4 / 5, true);
                    px(fb, mx + 5 - d, my - 2 + t + d * 4 / 5, true);
                }
            break;
        case AFTER_FLAVOR_WALL:
            fill_rect(fb, mx - 1, my - 6, mx + 1, my + 6, true);
            hline(fb, mx + 2, mx + 3, my);
            break;
        case AFTER_FLAVOR_VORTEX:
            hline(fb, mx - 5, mx + 5, my - 5);
            vline(fb, mx + 5, my - 5, my + 5);
            hline(fb, mx - 2, mx + 5, my + 5);
            vline(fb, mx - 2, my, my + 5);
            break;
        default: /* combo */
            for (int d = -5; d <= 5; d++) {
                px(fb, mx + d, my + d, true);
                px(fb, mx + d, my - d, true);
            }
            break;
    }
}

static void draw_wizard(town_fb_t *fb, const duel_render_t *r, uint32_t frame) {
    duel_view_wizard_t wz = duel_view_wizard(&r->view, SIM_SIDE_L);
    int feet = BALCONY_Y - 1;

    /*
     * The champion is felled for about three seconds in every run, and the
     * balcony used to simply empty. The arc is drawn instead: a figure going
     * down, a shape on the boards, and a replacement walking out. Something
     * happened up there, which is the whole reading.
     */
    if (wz.life != LIFE_ACTIVE) {
        int cx = TOWER_CX;
        if (wz.life == LIFE_COLLAPSE || wz.life == LIFE_DOWNED || wz.life == LIFE_MEDIC) {
            /* Clear the rail as well as the boards. A prone figure the width
             * of the balcony, drawn straight over the railing it is lying
             * behind, is the same white as the railing and disappears into
             * it -- the balcony simply looked empty. */
            fill_rect(fb, cx - 12, feet - 8, cx + 12, feet + 1, false);
            hline(fb, cx - 12, cx + 12, BALCONY_Y);
            fill_rect(fb, cx - 8, feet - 2, cx + 4, feet, true); /* the body */
            disc(fb, cx - 10, feet - 3, 2, true);                /* the head */
            px(fb, cx + 6, feet - 4, true);                      /* the hat, fallen */
            px(fb, cx + 7, feet - 3, true);
            px(fb, cx + 5, feet - 3, true);
            if (wz.life == LIFE_MEDIC) { /* someone stooping over them */
                fill_rect(fb, cx + 6, feet - 11, cx + 9, feet - 1, true);
                px(fb, cx + 5, feet - 7, true);
                px(fb, cx + 4, feet - 6, true);
            }
        } else { /* REPLACE: the next of the roster walks out of the study */
            int walk = (int)((frame >> 2) & 7u);
            fill_rect(fb, cx - 2 + walk / 2, feet - 11, cx + 2 + walk / 2, feet - 1, true);
            fill_rect(fb, cx - 1 + walk / 2, feet - 15, cx + 1 + walk / 2, feet - 12, true);
            hline(fb, cx - 4 + walk / 2, cx + 4 + walk / 2, feet - 16);
        }
        return;
    }

    bool casting = wz.pose == POSE_CAST;
    /* A slow shuffle along the balcony while nothing is brewing. */
    int sway = wz.inc_state == INC_IDLE && !casting ? (int)((frame >> 5) & 3u) - 1 : 0;
    int cx = TOWER_CX + sway;

    /* Stand clear of the lit study behind and the rail in front: the figure
     * owns its own column of pixels or it is a smudge on the balcony. */
    fill_rect(fb, cx - 9, feet - 26, cx + 10, feet, false);

    /* The non-casting stances are simulation state the town has never read.
     * FORTIFY is held for a sixth of a run and MEDITATE and STUDY between
     * them cover most of the rest of the idle time, so most of the frames in
     * which nothing is being thrown now show what is being done instead. */
    bool seated = wz.stance == DUEL_STANCE_MEDITATE;
    int body_top = seated ? feet - 8 : feet - 11;

    fill_rect(fb, cx - 3, body_top, cx + 3, feet - 1, true); /* robe */
    /* The hem flares, and it flares wider when the robe is settled. */
    px(fb, cx - 4, feet - 2, true);
    px(fb, cx + 4, feet - 2, true);
    px(fb, cx - 4, feet - 1, true);
    px(fb, cx + 4, feet - 1, true);
    if (seated) {
        px(fb, cx - 5, feet - 1, true);
        px(fb, cx + 5, feet - 1, true);
    }
    fill_rect(fb, cx - 2, body_top - 4, cx + 2, body_top - 1, true); /* head */
    hline(fb, cx - 5, cx + 5, body_top - 5);                         /* hat brim */
    for (int step = 0; step <= 4; step++)
        hline(fb, cx - 3 + step, cx + 3 - step, body_top - 6 - step);

    int staff_x = cx + 6;
    if (casting) {
        /* Staff up, arm out, and a head on the orb: a cast is the loudest
         * thing the figure does and it should be the loudest silhouette. */
        vline(fb, staff_x, feet - 24, feet - 6);
        hline(fb, cx + 3, staff_x, body_top + 1);
        disc(fb, staff_x, feet - 26, 3, true);
        ring(fb, staff_x, feet - 26, 5 + (int)((frame >> 1) & 1u), true);
    } else if (wz.stance == DUEL_STANCE_FORTIFY) {
        /* Staff held across the body, both hands on it. */
        for (int i = -6; i <= 6; i++)
            px(fb, cx + i, feet - 13 + i / 3, true);
        px(fb, cx - 4, feet - 12, true);
        px(fb, cx + 4, feet - 10, true);
    } else if (wz.stance == DUEL_STANCE_STUDY) {
        /* A book, held open. */
        vline(fb, staff_x + 1, feet - 17, feet - 1);
        frame_rect(fb, cx - 6, feet - 13, cx + 1, feet - 9);
        vline(fb, cx - 2, feet - 13, feet - 9);
    } else {
        vline(fb, staff_x, feet - 17, feet - 1);
        px(fb, staff_x, feet - 19, true);
    }
    draw_status_mark(fb, &wz, cx, feet, frame);

    /* Charging a big cast lights the shaft below the balcony. */
    if (wz.rearm_lock && (wz.inc_state == INC_WINDUP || wz.inc_state == INC_PREPARED)) {
        for (int i = 0; i < 6; i++) {
            int my = BALCONY_Y + 10 + (int)((frame * 2u + (uint32_t)i * 11u) % 52u);
            px(fb, TOWER_X0 - 3, my, true);
            px(fb, TOWER_X1 + 3, my, true);
            px(fb, TOWER_X0 - 4, my + 1, true);
            px(fb, TOWER_X1 + 4, my + 1, true);
        }
    }
}

/* ---- spells over the town ------------------------------------------------ */

/*
 * A carrier leaves the balcony and arcs out over the roofs. The panels send it
 * along a desk between two towers; here there is one tower, so the two slots
 * throw in opposite directions and the town is what they fly over.
 *
 * The path is the descriptor's, not a constant. The compiled trajectory names
 * how high the throw goes and whether it comes back, and the world produces
 * LOW, MID, HIGH and RETURNING in roughly equal numbers -- so four visibly
 * different flights were already being simulated and drawn as one. RETURNING
 * is the one worth having: it climbs out over the far roofs, turns, and comes
 * home, which is a shape no other thing in the town makes.
 */
static int traj_apex(uint8_t traj) {
    switch (traj) {
        case TRAJ_GROUND:
            return 6;
        case TRAJ_LOW:
            return 26;
        case TRAJ_MID:
            return 58;
        case TRAJ_HIGH:
            return 84;
        case TRAJ_ROOF:
            return 72;
        case TRAJ_RETURNING:
            return 48;
        case TRAJ_AREA:
            return 40;
        default:
            return 66; /* HOMING */
    }
}

/* Where the flight starts and where it ends, so both the carrier and the
 * burst that follows it can be put in the same place. The throw leaves the
 * balcony and comes down over the far end of the town: an arc that returned
 * to its launch height left the spell hanging in mid-air at the end of every
 * flight, with nothing under it and nothing to hit. */
#define SPELL_Y0 (BALCONY_Y - 10)
#define SPELL_Y1 (GROUND_Y - 58)

static int spell_reach(const town_fb_t *fb) { return fb->width == LANDSCAPE_W ? 160 : 96; }

static void spell_point(town_fb_t *fb, uint8_t side, uint8_t traj, int travel, int *out_x,
                        int *out_y) {
    if (travel < 0)
        travel = 0;
    if (travel > 255)
        travel = 255;
    int along = travel;
    if (traj == TRAJ_RETURNING) {
        /* Out to the turn at the halfway mark, then home again -- so a
         * returning spell lands back on the balcony it left, which is the one
         * flight in the town that draws a shape nothing else draws. */
        along = travel <= 127 ? travel * 2 : (255 - travel) * 2;
    }
    int reach = along * spell_reach(fb) / 255;
    int arc = 4 * along * (255 - along) / 255;
    *out_x = TOWER_CX + (side == SIM_SIDE_L ? reach : -reach);
    /* The ballistic baseline falls as it goes; the arc rides on top of it. */
    *out_y = SPELL_Y0 + (SPELL_Y1 - SPELL_Y0) * along / 255 - arc * traj_apex(traj) / 255;
    if (traj == TRAJ_HOMING) /* it wanders on the way */
        *out_y += isin((uint32_t)travel * 4u) * 5 / 127;
}

/*
 * The body of a spell, by element.
 *
 * The old carrier was a two-by-two square with a three-pixel tail, which at
 * this size is a speck: the thing the whole world is about was the least
 * visible object on the canvas. Each element now has a silhouette of its own
 * at a radius the magnitude sets, because "which spell is that" should be
 * answerable from across the room and without reading the descriptor.
 */
static void draw_spell_body(town_fb_t *fb, int x, int y, uint8_t element, int radius, int lead,
                            uint32_t frame, uint32_t salt) {
    switch (element) {
        case ELEM_EMBER: {
            /* A flame: a core, with tongues streaming back off it whose
             * lengths flicker frame to frame. */
            disc(fb, x, y, radius, true);
            for (int i = 0; i < 5; i++) {
                uint32_t h = town_hash(salt + (uint32_t)i, frame >> 1);
                int len = radius + 2 + (int)(h % 5u);
                int spread = (i - 2) * 2;
                for (int d = radius; d <= len; d++)
                    px(fb, x - lead * d, y + spread * d / (len ? len : 1), true);
            }
            for (int i = 0; i < 3; i++) { /* embers falling out of it */
                uint32_t h = town_hash(salt, (uint32_t)i + (frame >> 2));
                px(fb, x - lead * (int)(2u + h % 9u), y + 2 + (int)((h >> 4) % 6u), true);
            }
            break;
        }
        case ELEM_FROST: {
            /* A shard: three axes crossing, a small solid core, and motes
             * drifting off it downwards. */
            disc(fb, x, y, radius - 1 > 0 ? radius - 1 : 1, true);
            for (int i = 0; i < 3; i++) {
                uint32_t a = (uint32_t)i * 256u / 6u + ((frame >> 3) & 7u);
                int dx = isin(a + 64u);
                int dy = isin(a);
                int len = radius + 4;
                for (int d = -len; d <= len; d++)
                    px(fb, x + dx * d / 127, y + dy * d / 127, true);
            }
            for (int i = 0; i < 4; i++) {
                uint32_t h = town_hash(salt, (uint32_t)i + (frame >> 3));
                px(fb, x - lead * (int)(3u + h % 11u), y + 3 + (int)((h >> 5) % 8u), true);
            }
            break;
        }
        case ELEM_VOID: {
            /* A hole, drawn as a hole: the interior is cleared, so whatever
             * was behind it -- cloud, star, roofline -- is eaten. A broken
             * halo turns outside it. */
            disc(fb, x, y, radius + 1, false);
            ring(fb, x, y, radius + 1, true);
            for (int a = 0; a < 256; a += 8) {
                if (((a >> 3) + (int)(frame >> 2)) % 3 == 0)
                    continue;
                int rr = radius + 4;
                px(fb, x + isin((uint32_t)a + 64u) * rr / 127, y + isin((uint32_t)a) * rr / 127,
                   true);
            }
            break;
        }
        default: { /* ELEM_FORCE: a dart, with a shock front ahead of it */
            disc(fb, x, y, radius, true);
            for (int i = 1; i <= 3; i++) {
                px(fb, x + lead * (radius + i), y - i, true);
                px(fb, x + lead * (radius + i), y + i, true);
            }
            for (int i = 0; i < 3; i++)
                px(fb, x - lead * (radius + 2 + i * 2), y, true);
            break;
        }
    }
}

/*
 * What the panels add to a carrier, at the town's size.
 *
 * Tempo: a swift spell streaks three speed lines behind it, a heavy one wears
 * a casing of four corner brackets. Signature: a rune hangs a diamond over
 * the carrier with a blinking core, a wall raises a crenellated slab behind
 * it, a vortex turns four pinwheel arms round it, and a bloom opens and closes
 * petals on its diagonals. A combining spell blinks a tall bar either side.
 * The panels draw the same marks; these are the same words, larger.
 */
static void draw_spell_marks(town_fb_t *fb, const duel_view_spell_t *spell, int x, int y,
                             int radius, int lead, uint32_t frame) {
    int reach = radius + 3;
    switch (DUEL_KIND_MODIFIER(spell->kind)) {
        case MOD_SWIFT:
            /* Under the body: the trail comes down from above and behind,
             * and would swallow a line drawn through it. */
            for (int i = 0; i < 2; i++) {
                int near = x - lead * (radius - 2 + i * 3);
                int far = x - lead * (radius + 12 + i * 3);
                hline(fb, near < far ? near : far, near < far ? far : near, y + reach + i * 3);
            }
            break;
        case MOD_HEAVY:
            for (int q = 0; q < 4; q++) {
                int sx = q & 1 ? 1 : -1, sy = q & 2 ? 1 : -1;
                int cx = x + sx * (reach + 2), cy = y + sy * (reach + 2);
                for (int d = 0; d < 4; d++) {
                    px(fb, cx - sx * d, cy, true);
                    px(fb, cx, cy - sy * d, true);
                }
            }
            break;
        default:
            break;
    }
    switch (incantation_signature(spell->descriptor)) {
        case SPELL_SIGNATURE_RUNE: {
            int ry = y - radius - 9;
            for (int d = 0; d <= 3; d++) {
                px(fb, x - 3 + d, ry - d, true);
                px(fb, x + 3 - d, ry - d, true);
                px(fb, x - 3 + d, ry + d, true);
                px(fb, x + 3 - d, ry + d, true);
            }
            if (frame & 4u)
                disc(fb, x, ry, 1, true);
            break;
        }
        case SPELL_SIGNATURE_WALL: {
            /* An outlined slab cut out of the trail, with a crenellated top. */
            int wx = x - lead * (reach + 4);
            int top = y - radius - 7;
            fill_rect(fb, wx - 3, top, wx + 3, y + radius, false);
            frame_rect(fb, wx - 3, top, wx + 3, y + radius);
            for (int c = -3; c <= 3; c += 2)
                px(fb, wx + c, top - 1, true);
            break;
        }
        case SPELL_SIGNATURE_VORTEX:
            /* Outside void's own broken ring, and swept hard so they read
             * as turning. */
            for (int arm = 0; arm < 4; arm++)
                for (int d = 0; d < 7; d++) {
                    uint32_t a = (uint32_t)arm * 64u + (uint32_t)d * 10u + (frame << 2);
                    int rr = radius + 6 + d;
                    px(fb, x + isin(a + 64u) * rr / 127, y + isin(a) * rr / 127, true);
                }
            break;
        case SPELL_SIGNATURE_BLOOM: {
            int d = reach + ((frame & 4u) ? 1 : 3);
            for (int q = 0; q < 4; q++)
                disc(fb, x + (q & 1 ? d : -d), y + (q & 2 ? d : -d), 2, true);
            break;
        }
        default:
            break;
    }
    /* Only the view's combine flag yields INTERACT_COMBINE, never on void. */
    if (SPELL_DESC_INTERACTION(spell->descriptor) == INTERACT_COMBINE && (frame & 4u)) {
        int bx = reach + 5;
        for (int s = -1; s <= 1; s += 2) {
            vline(fb, x + s * bx, y - reach, y + reach);
            px(fb, x + s * (bx - 1), y - reach, true);
            px(fb, x + s * (bx - 1), y + reach, true);
        }
    }
}

static void draw_spells(town_fb_t *fb, const duel_render_t *r, uint32_t frame) {
    for (uint8_t side = 0; side < 2u; side++) {
        duel_view_spell_t spell = duel_view_spell(&r->view, side, r->seed);
        if (!spell.active)
            continue;
        int travel = side == SIM_SIDE_L ? spell.pos : 255 - spell.pos;
        uint8_t traj = SPELL_DESC_TRAJECTORY(spell.descriptor);
        uint8_t element = SPELL_DESC_ELEMENT(spell.descriptor);
        uint8_t magnitude = SPELL_DESC_MAGNITUDE(spell.descriptor);
        int lead = side == SIM_SIDE_L ? 1 : -1;
        int radius = 2 + (int)magnitude + (int)DUEL_KIND_TIER(spell.kind);

        int x, y;
        spell_point(fb, side, traj, travel, &x, &y);

        /*
         * The whole flight, not the head and a smear behind it.
         *
         * Eight samples of trail read as a smudge while the frame is moving
         * and as nothing much at all when it stops -- and stopped is how an
         * ambient surface shows this world, one still every minute or every
         * quarter hour. So the path already travelled is drawn end to end,
         * thinning back toward the balcony it left, and the arc still to come
         * is dotted in ahead of the head. One frame then says thrown along
         * this trajectory, this far along, which is a sentence the old
         * carrier could only say in motion.
         *
         * spell_point() already answers for any point on the flight, so this
         * is a loop bound and a shade level rather than new geometry.
         */
        for (int t = travel + 6; t <= 255; t += 6) {
            int ax, ay;
            spell_point(fb, side, traj, t, &ax, &ay);
            if (shade_on(ax, ay, 8))
                px(fb, ax, ay, true);
        }
        for (int t = 0; t < travel; t += 3) {
            int tx, ty;
            spell_point(fb, side, traj, t, &tx, &ty);
            /* How far back down the arc this sample is, which is the whole of
             * what decides how solid it still looks. */
            int behind = travel - t;
            int tr = radius - behind / 20;
            if (tr < 1)
                tr = 1;
            int level = 16 - behind / 6;
            if (level < 5)
                level = 5;
            shade_disc(fb, tx, ty, tr, level);
        }

        draw_spell_body(fb, x, y, element, radius, lead, frame, (uint32_t)side * 977u + r->seed);
        draw_spell_marks(fb, &spell, x, y, radius, lead, frame);

        /* Leaving and arriving are the two moments worth marking: a muzzle
         * flash off the balcony, and a bow wave as it runs out of the town. */
        if (travel < 18) {
            int fx = TOWER_CX + lead * 8;
            for (int i = 0; i < 6; i++) {
                uint32_t a = (uint32_t)i * 256u / 6u;
                int d = 6 + (18 - travel) / 3;
                px(fb, fx + isin(a + 64u) * d / 127, BALCONY_Y - 12 + isin(a) * d / 127, true);
            }
        }

        /* A status rider on the carrier, when the descriptor carries one. */
        uint8_t status = SPELL_DESC_STATUS(spell.descriptor);
        if (status == STATUS_MARKED)
            ring(fb, x, y, radius + 6, true);
        else if (status == STATUS_BURNING)
            for (int i = 0; i < 4; i++)
                px(fb, x + (int)((frame + (uint32_t)i * 3u) % 7u) - 3, y - radius - 2 - i, true);
    }
}

/*
 * What happened, drawn where it happened.
 *
 * The one-shot outcomes are already armed for the panels -- impact, deflect,
 * ward shatter, heal, residue -- and the town has been ignoring every one of
 * them, so a spell simply stopped existing at the end of its flight. The
 * flash counts down twelve frames for an impact and eight for anything else,
 * which is exactly the ring radius an expanding burst wants.
 */
static void draw_outcome(town_fb_t *fb, const duel_render_t *r) {
    if (!r->flash_frames)
        return;
    uint8_t kind = r->flash_kind;
    /*
     * The suffix names the DEFENDER, and the town throws the left champion's
     * spells to the right -- so a hit on the left is a hit by the flight that
     * went left, and the burst belongs on that side. Reading the suffix as
     * the direction of travel put every outcome on the wrong half of the
     * canvas from the flight that caused it.
     */
    bool left = kind == FX_IMPACT_L || kind == FX_DEFLECT_L || kind == FX_FIZZLE_L ||
                kind == FX_HEAL_L || kind == FX_WARD_SHATTER_L || kind == FX_SHATTER_L;
    int lead = left ? -1 : 1;
    int age = 12 - (int)r->flash_frames;
    if (age < 0)
        age = 0;
    /* Where the flight ends, so the burst is where the spell was. */
    int x = TOWER_CX + lead * spell_reach(fb);
    int y = SPELL_Y1;

    switch (kind) {
        case FX_IMPACT_L:
        case FX_IMPACT_R:
        case FX_SHATTER_L:
        case FX_SHATTER_R: {
            /*
             * The loudest thing that happens in a run should be the loudest
             * thing on the canvas -- but eight even spokes around two even
             * rings drew a second sun. It is struck instead: a solid core
             * that collapses as the shell expands, and a dozen shards thrown
             * to unequal distances off a hash, so no two impacts are the
             * same shape and none of them is a symmetrical star.
             */
            disc(fb, x, y, 9 - age > 1 ? 9 - age : 1, true);
            ring(fb, x, y, 5 + age * 3, true);
            if (age > 1)
                ring(fb, x, y, 2 + age * 4, true);
            for (int i = 0; i < 12; i++) {
                uint32_t h = town_hash((uint32_t)i, 91u);
                uint32_t a = (uint32_t)i * 21u + (h & 15u);
                int d0 = 6 + age * 3;
                int d1 = d0 + 5 + (int)(h % 11u);
                for (int d = d0; d <= d1; d++)
                    px(fb, x + isin(a + 64u) * d / 127, y + isin(a) * d / 127, true);
            }
            /* Dust knocked off the roofs underneath it. */
            for (int i = 0; i < 6; i++) {
                uint32_t h = town_hash((uint32_t)i, 17u);
                px(fb, x + (int)(h % 40u) - 20, y + 12 + age + (int)((h >> 6) % 8u), true);
            }
            /* A shatter is the same hit through a frozen champion: the frost
             * goes with it, as six small ice crosses thrown clear of the
             * burst. */
            if (kind == FX_SHATTER_L || kind == FX_SHATTER_R)
                for (int i = 0; i < 6; i++) {
                    uint32_t a = (uint32_t)i * 43u + 10u;
                    int d = 14 + age * 4 + (int)(town_hash((uint32_t)i, 23u) % 6u);
                    int sx = x + isin(a + 64u) * d / 127, sy = y + isin(a) * d / 127;
                    hline(fb, sx - 2, sx + 2, sy);
                    vline(fb, sx, sy - 2, sy + 2);
                }
            break;
        }
        case FX_DEFLECT_L:
        case FX_DEFLECT_R: {
            /* A ripple off a ward: arcs, not a burst, and turned to face the
             * thing that was stopped. */
            for (int k = 0; k < 3; k++)
                for (int a = 0; a < 256; a++) {
                    int dx = isin((uint32_t)a + 64u);
                    if (dx * lead < 60)
                        continue;
                    int rr = 8 + age * 2 + k * 4;
                    px(fb, x + dx * rr / 127, y + isin((uint32_t)a) * rr / 127, true);
                }
            break;
        }
        case FX_WARD_SHATTER_L:
        case FX_WARD_SHATTER_R: {
            /* Fracture lines flying apart from where the ward stood. */
            for (int i = 0; i < 7; i++) {
                uint32_t a = town_hash((uint32_t)kind, (uint32_t)i) % 256u;
                int d0 = 4 + age * 2;
                int d1 = d0 + 9;
                line_step(fb, x + isin(a + 64u) * d0 / 127, y + isin(a) * d0 / 127,
                          x + isin(a + 64u) * d1 / 127, y + isin(a) * d1 / 127, 1, 0);
            }
            break;
        }
        case FX_HEAL_L:
        case FX_HEAL_R: {
            /* Motes rising over the balcony rather than a burst out in the
             * air: a heal happens to the champion, not to the sky. */
            for (int i = 0; i < 7; i++) {
                int mx = TOWER_CX - 12 + (int)(town_hash((uint32_t)i, 5u) % 25u);
                px(fb, mx, BALCONY_Y - 6 - age * 2 - i * 2, true);
                px(fb, mx + 1, BALCONY_Y - 6 - age * 2 - i * 2, true);
            }
            break;
        }
        case FX_THAW: {
            /* Frost met ember: steam puffs rise off the roofs over the town
             * and thin as they go. */
            for (int i = 0; i < 7; i++) {
                uint32_t h = town_hash((uint32_t)i, 31u);
                int sx = TOWER_CX - 36 + i * 12 + (int)(h % 5u);
                int sy = BALCONY_Y - 14 - age * 3 - (int)((h >> 4) % 8u);
                shade_disc(fb, sx, sy, 3 + age / 3, 12 - age);
            }
            break;
        }
        case FX_FIELD_CLASH: {
            /* Two fields met: a bracket from each side closes over the town
             * and strikes a spark where they meet. The tower behind is cut
             * dark first, or its walls swallow the brackets. */
            int gap = 34 - age * 3;
            int cy = BALCONY_Y - 32; /* clear of the champion's head */
            fill_rect(fb, TOWER_CX - 26, cy - 10, TOWER_CX + 26, cy + 10, false);
            for (int s = -1; s <= 1; s += 2) {
                int bx = TOWER_CX + s * gap;
                vline(fb, bx, cy - 8, cy + 8);
                hline(fb, s < 0 ? bx : bx - 3, s < 0 ? bx + 3 : bx, cy - 8);
                hline(fb, s < 0 ? bx : bx - 3, s < 0 ? bx + 3 : bx, cy + 8);
            }
            if (gap < 12)
                for (int i = 0; i < 8; i++) {
                    uint32_t a = (uint32_t)i * 32u;
                    int d = 3 + age;
                    px(fb, TOWER_CX + isin(a + 64u) * d / 127, cy + isin(a) * d / 127, true);
                }
            break;
        }
        case FX_RESIDUE:
        case FX_DETONATE:
        case FX_COMBINE: {
            /* Side-neutral aftermaths happen over the town, so they are drawn
             * over the town: a low, wide bloom above the roofs. */
            for (int a = 0; a < 256; a += 3) {
                int rr = 10 + age * 3;
                px(fb, TOWER_CX + isin((uint32_t)a + 64u) * rr * 2 / 127,
                   BALCONY_Y - 20 + isin((uint32_t)a) * rr / 127, true);
            }
            break;
        }
        default:
            break;
    }
}

/*
 * Persistent fields: the world keeps two slots and the only kind the
 * self-playing caster ever raises is steam, which hangs over the plaza for
 * three seconds at a time. It is worth drawing because it is the one thing in
 * the world that lingers, and because a town with weather in it is a town.
 */
static void draw_fields(town_fb_t *fb, const duel_render_t *r, uint32_t frame) {
    for (unsigned slot = 0; slot < SIM_FIELD_SLOTS; slot++) {
        uint8_t kind = (uint8_t)(r->field[slot] & 7u);
        if (kind == FIELD_NONE)
            continue;
        int cx = TOWER_CX + (slot ? 62 : -62);
        int cy = GROUND_Y - 16;
        switch (kind) {
            case FIELD_STEAM:
                for (int i = 0; i < 10; i++) {
                    uint32_t h = town_hash((uint32_t)slot, (uint32_t)i);
                    int age = (int)(((frame >> 2) + (h >> 3)) % 30u);
                    shade_disc(fb, cx + (int)(h % 34u) - 17 + age / 4, cy - age, 2 + age / 12,
                               10 - age / 4);
                }
                break;
            case FIELD_WALL:
                for (int y = GROUND_Y - 26; y <= GROUND_Y; y++)
                    if (((y + (int)(frame >> 3)) & 3) != 0)
                        shade_rect(fb, cx - 10, y, cx + 10, y, 7);
                break;
            case FIELD_VORTEX:
                for (int a = 0; a < 256; a += 4) {
                    int rr = 6 + ((a + (int)(frame >> 1)) & 15);
                    px(fb, cx + isin((uint32_t)a + (frame >> 1) + 64u) * rr / 127,
                       cy + isin((uint32_t)a + (frame >> 1)) * rr / 127, true);
                }
                break;
            default: /* trap, rune, familiar, singularity: a marked ground glyph */
                ring(fb, cx, cy + 12, 7, true);
                for (int i = 0; i < 4; i++) {
                    uint32_t a = (uint32_t)i * 64u + (frame >> 2);
                    px(fb, cx + isin(a + 64u) * 10 / 127, cy + 12 + isin(a) * 10 / 127, true);
                }
                break;
        }
    }
}

/* ---- what the duel leaves behind ------------------------------------------
 *
 * Residue is the world's long memory: four zones along the battlefield axis,
 * each holding an element and an intensity that saturates at three and decays
 * over about forty-five seconds. The town has been drawing none of it.
 *
 * That matters more here than it does on the panels, because the town is the
 * layer an ambient surface shows, and an ambient surface samples this world
 * discontinuously -- one still every minute or every quarter hour. Sampled
 * that way the world has residue standing 94% of the time and a spell in the
 * air 29%, so these marks are the likeliest thing a single frame has with
 * which to say that a duel is going on at all.
 *
 * They go where the panels put theirs: on the roofline directly under the
 * spell lanes, which here is the near row's own silhouette. That is the one
 * surface at these two positions that is neither already built on nor down in
 * the plaza's furniture, it is where the flights terminate and where
 * draw_outcome bursts, and it is against the sky -- which is what a still
 * frame needs more than anything else. The element vocabulary is the panels'
 * too, mark for mark, so the two drawings stay opinions about one world.
 * Void is the same exception it is there: it takes pixels away instead of
 * adding them, which only works because the roof it bites into is real.
 */

/*
 * The four zones on the town's axis. The doorsteps are where the flights
 * terminate -- TOWER_CX +/- SPELL_REACH, the points draw_outcome bursts over
 * -- and the middle pair is the panels' own anchor spacing (13/48/207/242 in
 * battlefield u) carried across onto those two fixed points. Zones 0 and 3
 * are the only two the self-playing world ever fills; the middle pair is
 * drawn because the world model has it, not because this caster reaches it.
 */
/*
 * The top of the near row's silhouette over one column, so a mark can be put
 * on the roof rather than at a height guessed near it. Every branch is the
 * matching branch of draw_near_row read back: a pitch rises half a pixel per
 * column from the eave, a stepped gable stands three rows per step, and a
 * parapet is flat six above the eave. Nothing standing there is the ground.
 */
static int near_row_roof_y(town_fb_t *fb, int x) {
    for (size_t i = 0; i < sizeof near_row / sizeof near_row[0]; i++) {
        town_building_t placed = near_row[i];
        placed.x0 = (int16_t)TOWN_X(placed.x0);
        const town_building_t *b = &placed;
        int x1 = b->x0 + b->width;
        if (x < b->x0 || x > x1)
            continue;
        int top = GROUND_Y - b->height;
        int in = x - b->x0 < x1 - x ? x - b->x0 : x1 - x;
        if (b->roof == 1) {
            int y = top - 3;
            for (int s = 1; s < 4; s++)
                if (s * b->width / 8 <= in)
                    y = top - 3 - s * 3;
            return y;
        }
        if (b->roof == 2)
            return top - 6;
        return top - in / 2;
    }
    return GROUND_Y;
}

/* The half-width of an ellipse of radii (rw, rh) at one offset along the
 * other axis. The scorch and the pit that eats it are both drawn out of it. */
static int ellipse_span(int rw, int rh, int off) {
    int span = 0;
    while ((span + 1) * (span + 1) * rh * rh + off * off * rw * rw <= rw * rw * rh * rh)
        span++;
    return span;
}

static void draw_residue(town_fb_t *fb, const duel_render_t *r, uint32_t frame) {
    for (uint8_t zone = 0; zone < SIM_RESIDUE_ZONES; zone++) {
        int intensity = (int)DUEL_RENDER_RESIDUE_INTENSITY(r, zone);
        if (!intensity)
            continue;
        const int x_by_zone[SIM_RESIDUE_ZONES] = {
            TOWER_CX - spell_reach(fb),
            TOWN_X(61),
            TOWN_X(195),
            TOWER_CX + spell_reach(fb),
        };
        int x = x_by_zone[zone];
        int base = near_row_roof_y(fb, x);
        /* Intensity is the whole of the size: one mark that grows is easier
         * to read across a room than three marks that differ in kind. */
        int half = 3 + intensity * 3;
        uint32_t salt = (uint32_t)zone * 631u + r->seed;

        /* Scorched roof under the mark, drawn first and drawn downwards into
         * the tiles. Three pixels of shard on a roof that already has courses
         * and a dormer in it is not a mark; a burnt patch with something
         * standing in it is -- and it is what void has to bite into. */
        int rw = half + 2;
        int rh = 2 + intensity;
        for (int dy = 0; dy <= rh; dy++) {
            int span = ellipse_span(rw, rh, dy);
            for (int dx = -span; dx <= span; dx++)
                if (shade_on(x + dx, base + dy, 3 + intensity * 2))
                    px(fb, x + dx, base + dy, true);
        }

        switch (DUEL_RENDER_RESIDUE_ELEMENT(r, zone)) {
            case ELEM_FORCE: {
                /* A rubble mound: broken blocks heaped into a shallow arc,
                 * highest over the middle, and slates thrown off the pitch. */
                for (int i = 0; i < 4 + intensity * 5; i++) {
                    uint32_t h = town_hash(salt, (uint32_t)i);
                    int dx = (int)(h % (uint32_t)(2 * half + 1)) - half;
                    int lift = (half - (dx < 0 ? -dx : dx)) / 2;
                    int by = base - (int)((h >> 7) % (uint32_t)(lift + 1));
                    int size = 1 + (int)((h >> 11) & 1u);
                    fill_rect(fb, x + dx, by - size, x + dx + size, by, true);
                }
                for (int i = 0; i < intensity; i++) {
                    uint32_t h = town_hash(salt, (uint32_t)i + 90u);
                    int dx = (int)(h % (uint32_t)(2 * half + 1)) - half;
                    line_step(fb, x + dx, base + 1, x + dx + (int)(h >> 8) % 9 - 4, base + rh + 3,
                              2, 0);
                }
                break;
            }
            case ELEM_EMBER: {
                /* A bed of coals with tongues off it. The bed is the whole of
                 * what a still frame needs; the tongues are what it gains
                 * when somebody is watching it move. */
                fill_rect(fb, x - half, base - 1, x + half, base, true);
                for (int i = 0; i < intensity * 3; i++) {
                    uint32_t h = town_hash(salt, (uint32_t)i + (frame >> 2));
                    int dx = (int)(h % (uint32_t)(2 * half + 1)) - half;
                    int len = 3 + (int)((h >> 6) % (uint32_t)(2 + intensity * 3));
                    int lean = ((h >> 3) & 1u) ? 1 : -1;
                    for (int d = 0; d < len; d++)
                        px(fb, x + dx + (d > len / 2 ? lean : 0), base - 2 - d, true);
                }
                for (int i = 0; i < intensity; i++) { /* sparks off the bed */
                    uint32_t h = town_hash(salt + 7u, (uint32_t)i + (frame >> 3));
                    px(fb, x + (int)(h % (uint32_t)(2 * half + 1)) - half,
                       base - 7 - (int)((h >> 9) % 7u), true);
                }
                break;
            }
            case ELEM_FROST: {
                /* Shards standing out of a rime crust, and a spire between
                 * them once the roof has taken enough of it. */
                for (int s = -1; s <= 1; s += 2) {
                    int sx = x + s * (half - 2);
                    int tall = 5 + intensity * 3;
                    for (int d = 0; d <= tall; d++)
                        hline(fb, sx - (tall - d) / 3, sx + (tall - d) / 3, base - d);
                }
                if (intensity >= 3) {
                    int tall = 18;
                    for (int d = 0; d <= tall; d++)
                        hline(fb, x - (tall - d) / 5, x + (tall - d) / 5, base - d);
                }
                for (int dx = -half; dx <= half; dx++) /* the crust */
                    if (((dx + (int)salt) & 1) == 0)
                        px(fb, x + dx, base + 1, true);
                break;
            }
            default: {
                /* ELEM_VOID: a pit. The roof inside it is taken away rather
                 * than drawn over, so the hole is a hole in the town and not
                 * a black shape laid on top of one -- which is why the scorch
                 * above had to be drawn first, and why the rim is the only
                 * part of this that adds anything. */
                int pw = half;
                int ph = 1 + intensity;
                for (int dy = 0; dy <= ph; dy++) {
                    int span = ellipse_span(pw, ph, dy);
                    for (int dx = -span; dx <= span; dx++)
                        px(fb, x + dx, base + dy, false);
                    px(fb, x - span, base + dy, true);
                    px(fb, x + span, base + dy, true);
                }
                for (int dx = -pw; dx <= pw; dx++)
                    px(fb, x + dx, base, true); /* the rim it left */
                if (intensity >= 2)             /* and what is still going up out of it */
                    for (int i = 0; i < 4; i++) {
                        uint32_t h = town_hash(salt, (uint32_t)i + (frame >> 3));
                        px(fb, x + (int)(h % (uint32_t)(2 * pw + 1)) - pw,
                           base - 3 - (int)((h >> 8) % 7u), true);
                    }
                break;
            }
        }
    }
}

/* ---- the civic street -----------------------------------------------------
 *
 * The panels draw three civic things the town did not: the courier a
 * notification sends, the rare event the deck deals, and the resident an
 * aftermath sets to work. All three come out of the two shared bytes the
 * panels read, shared_pres and revision, so the town stays an opinion about
 * one state. While an aftermath lasts it owns both bytes and the courier and
 * the event stand down, as they do on the keyboard; the aftermath's spell
 * flavor keeps its sigil by the spire.
 *
 * The town has one tower, so the two cities are its two sides. A left-city
 * courier walks in from the left edge, a right-city event happens to the
 * right-hand houses, and each champion's aftermath resident stands on its
 * own side of the door. Nothing here flashes or is drawn as a warning, and a
 * quiet town drops the motion marks but keeps every figure.
 */

/* A figure is drawn into its own small bitmap first and stamped with a
 * one-pixel dark margin, so it reads against a facade without a box cut out
 * of the street round it. */
#define FIG_W 32
#define FIG_H 40

typedef struct {
    uint32_t row[FIG_H];
    int x0;
    int y0;
} town_fig_t;

static void fig_begin(town_fig_t *f, int cx, int feet) {
    memset(f, 0, sizeof *f);
    f->x0 = cx - FIG_W / 2;
    f->y0 = feet - FIG_H + 4;
}

static void fig_px(town_fig_t *f, int x, int y) {
    x -= f->x0;
    y -= f->y0;
    if (x < 0 || x >= FIG_W || y < 0 || y >= FIG_H)
        return;
    f->row[y] |= 1u << x;
}

/* Filled, corners in either order: props are authored facing right and
 * mirrored by multiplying their x offsets by the facing. */
static void fig_rect(town_fig_t *f, int x0, int y0, int x1, int y1) {
    if (x0 > x1) {
        int t = x0;
        x0 = x1;
        x1 = t;
    }
    if (y0 > y1) {
        int t = y0;
        y0 = y1;
        y1 = t;
    }
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            fig_px(f, x, y);
}

static void fig_box(town_fig_t *f, int x0, int y0, int x1, int y1) {
    fig_rect(f, x0, y0, x1, y0);
    fig_rect(f, x0, y1, x1, y1);
    fig_rect(f, x0, y0, x0, y1);
    fig_rect(f, x1, y0, x1, y1);
}

static void fig_stamp(town_fb_t *fb, const town_fig_t *f) {
    for (int y = 0; y < FIG_H; y++) {
        uint32_t m = f->row[y];
        if (y > 0)
            m |= f->row[y - 1];
        if (y + 1 < FIG_H)
            m |= f->row[y + 1];
        m |= (m << 1) | (m >> 1);
        for (int x = 0; x < FIG_W; x++)
            if ((m >> x) & 1u)
                px(fb, f->x0 + x, f->y0 + y, false);
    }
    for (int y = 0; y < FIG_H; y++)
        for (int x = 0; x < FIG_W; x++)
            if ((f->row[y] >> x) & 1u)
                px(fb, f->x0 + x, f->y0 + y, true);
}

/* A street figure, a size up from the residents on the square because it
 * stands on the street line in front of the houses: head, cloak, and legs
 * that stride when it walks. A pixel of nose says which way it faces. */
static void fig_person(town_fig_t *f, int x, int feet, int face, bool walking) {
    fig_rect(f, x - 1, feet - 13, x + 1, feet - 11);
    fig_px(f, x + 2 * face, feet - 12);
    fig_px(f, x, feet - 10);
    fig_rect(f, x - 2, feet - 9, x + 2, feet - 3);
    fig_px(f, x - 3, feet - 3);
    fig_px(f, x + 3, feet - 3);
    if (walking) {
        for (int s = -1; s <= 1; s += 2) {
            fig_px(f, x + s, feet - 2);
            fig_px(f, x + 2 * s, feet - 1);
            fig_px(f, x + 3 * s, feet);
        }
    } else {
        fig_rect(f, x - 1, feet - 2, x - 1, feet);
        fig_rect(f, x + 1, feet - 2, x + 1, feet);
    }
}

/* Where the street is for one city: its edge of the town, and the spot by
 * the tower door its couriers walk to. */
static int street_edge(const town_fb_t *fb, bool left) { return left ? 14 : CANVAS_W - 15; }
static int street_door(const town_fb_t *fb, bool left) {
    return left ? TOWER_X0 - 16 : TOWER_X1 + 16;
}

/*
 * The courier: one visitor, walking the street of its own city. Arriving it
 * has just come in at the edge of the town; waiting it is most of the way to
 * the door; an old notice has it standing at the door; and resolving it is
 * walking back out with its hands empty. The kind is the hat and what it
 * carries -- a messenger's winged cap and letter, a carter's handcart, a
 * lamp-bearer's lantern on a pole, a sentinel's crested helm and halberd --
 * and the count is how much of it: letters in the satchel, crates on the
 * cart, rays off the lamp, pennants on the halberd. A messenger on the way
 * out has handed its letter over; the others take their load back with them.
 */
static void draw_courier_figure(town_fb_t *fb, const duel_render_t *r) {
    uint8_t sp = r->shared_pres;
    uint8_t kind = DUEL_VISITOR_KIND(sp);
    if (kind == DUEL_CIVIC_COURIER_NONE || kind >= DUEL_CIVIC_COURIER_COUNT)
        return;
    bool left = DUEL_VISITOR_CITY(sp) == 0u;
    uint8_t life = DUEL_VISITOR_LIFECYCLE(sp);
    int load = (int)DUEL_VISITOR_DENSITY(sp) + 1; /* one, a few, many */
    if (load > 3)
        load = 3;
    bool quiet = DUEL_CIVIC_MODE(r->civic) == DUEL_CIVIC_MODE_QUIET;
    bool leaving = life == DUEL_CIVIC_VISIT_RESOLVING;
    bool walking = life == DUEL_CIVIC_VISIT_ARRIVING || leaving;

    static const int route_third[4] = {0, 2, 3, 1};
    int edge = street_edge(fb, left), door = street_door(fb, left);
    int x = edge + (door - edge) * route_third[life & 3u] / 3;
    int inward = left ? 1 : -1;
    int f = leaving ? -inward : inward;
    int feet = GROUND_Y - 1;

    town_fig_t fig;
    fig_begin(&fig, x, feet);
    fig_person(&fig, x, feet, f, walking);
    switch (kind) {
        case DUEL_CIVIC_COURIER_MESSENGER:
            /* Winged cap; satchel on the back with the letters showing. */
            fig_rect(&fig, x - 1, feet - 14, x + 1, feet - 14);
            fig_px(&fig, x - 2 * f, feet - 14);
            fig_px(&fig, x - 3 * f, feet - 15);
            fig_px(&fig, x - 4 * f, feet - 16);
            fig_rect(&fig, x - 4 * f, feet - 8, x - 3 * f, feet - 5);
            for (int k = 0; k < load; k++)
                fig_px(&fig, x - 3 * f - (k & 1) * f, feet - 9 - k);
            if (!leaving) {
                fig_px(&fig, x + 3 * f, feet - 8);
                fig_px(&fig, x + 4 * f, feet - 9);
                fig_box(&fig, x + 5 * f, feet - 12, x + 8 * f, feet - 9);
                fig_px(&fig, x + 6 * f, feet - 11);
                fig_px(&fig, x + 7 * f, feet - 11);
            }
            break;
        case DUEL_CIVIC_COURIER_PARCEL:
            /* Brimmed cap; a handcart pushed ahead, crates stacked on it. */
            fig_rect(&fig, x - 1, feet - 14, x + 1, feet - 14);
            fig_px(&fig, x + 2 * f, feet - 14);
            fig_px(&fig, x + 3 * f, feet - 7);
            fig_rect(&fig, x + 4 * f, feet - 7, x + 4 * f, feet - 3);
            fig_rect(&fig, x + 4 * f, feet - 3, x + 11 * f, feet - 3);
            fig_px(&fig, x + 8 * f, feet - 2);
            fig_px(&fig, x + 7 * f, feet - 1);
            fig_px(&fig, x + 9 * f, feet - 1);
            fig_px(&fig, x + 8 * f, feet);
            for (int k = 0; k < load; k++) {
                int bottom = feet - 4 - 4 * k;
                fig_box(&fig, x + 6 * f, bottom - 3, x + 10 * f, bottom);
                fig_px(&fig, x + 8 * f, bottom - 2);
            }
            break;
        case DUEL_CIVIC_COURIER_BEACON: {
            /* Pointed hood; a lantern carried high on a pole. */
            fig_rect(&fig, x - 1, feet - 14, x + 1, feet - 14);
            fig_px(&fig, x, feet - 15);
            fig_rect(&fig, x - 2 * f, feet - 12, x - 2 * f, feet - 11);
            int pole = x + 4 * f;
            fig_rect(&fig, pole, feet - 19, pole, feet - 4);
            fig_px(&fig, x + 3 * f, feet - 8);
            fig_px(&fig, pole, feet - 23);
            fig_rect(&fig, pole - 1, feet - 22, pole + 1, feet - 20);
            static const int8_t ray[3][2][2] = {
                {{-3, -21}, {3, -21}},
                {{-3, -24}, {3, -24}},
                {{0, -26}, {0, -27}},
            };
            for (int k = 0; k < load; k++)
                for (int e = 0; e < 2; e++)
                    fig_px(&fig, pole + ray[k][e][0], feet + ray[k][e][1]);
            break;
        }
        default: {
            /* Sentinel: crested helm, shield on the back, halberd carried
             * upright with a pennant for each notice it stands for. */
            fig_rect(&fig, x - 2, feet - 14, x + 2, feet - 14);
            fig_rect(&fig, x, feet - 17, x, feet - 15);
            int haft = x + 3 * f;
            fig_rect(&fig, haft, feet - 20, haft, feet);
            fig_px(&fig, haft, feet - 21);
            fig_rect(&fig, haft + f, feet - 19, haft + 2 * f, feet - 17);
            for (int k = 0; k < load; k++)
                fig_rect(&fig, haft + f, feet - 15 + 2 * k, haft + (3 - k) * f, feet - 15 + 2 * k);
            fig_box(&fig, x - 5 * f, feet - 10, x - 3 * f, feet - 4);
            break;
        }
    }
    if (walking && !quiet) {
        /* Dust kicked up behind a figure on the move. */
        fig_px(&fig, x - 5 * f, feet);
        fig_px(&fig, x - 7 * f, feet - 1);
    }
    fig_stamp(fb, &fig);
}

/*
 * The rare events, on the side of the town their city is. The four local
 * families happen to the near row's houses: a scroll that has got loose
 * streams off a roof, the gable clock's gear jams, a bench comes out for a
 * break, and a crack opens down a wall. Their phase is how far along it is
 * -- about to happen, happening, being put right, and the trace it leaves.
 * The diplomatic courier hangs banners off the balcony on the side it
 * favours, or both, and the civic sky is an aurora behind everything.
 */
static void draw_event_scroll(town_fb_t *fb, bool left, uint8_t phase, bool quiet, uint32_t frame) {
    int ax = left ? TOWN_X(46) : TOWN_X(210);
    int ay = near_row_roof_y(fb, ax) - 2;
    int dir = left ? 1 : -1;
    static const int length[4] = {8, 36, 20, 0};
    int len = length[phase & 3u];
    uint32_t wave = quiet ? 0u : frame;
    for (int t = 1; t <= len; t++) {
        int x = ax + dir * t;
        int y = ay - 2 - t * 2 / 5 + isin((uint32_t)t * 14u + wave * 3u) * 2 / 127;
        px(fb, x, y, true);
        if (t % 5 != 0) /* the paper, with a gap between its lines */
            px(fb, x, y - 1, true);
    }
    disc(fb, ax, ay - 2, 2, false);
    ring(fb, ax, ay - 2, 2, true); /* the roll it came off */
    if (phase == DUEL_CIVIC_EVENT_PHASE_COOLDOWN) {
        hline(fb, ax - 3, ax + 3, ay - 2); /* tied up again */
    } else if (phase == DUEL_CIVIC_EVENT_PHASE_ACTIVE && !quiet) {
        int x = ax + dir * (len + 2);
        int y = ay - 4 - len * 2 / 5;
        px(fb, x, y, true);
        px(fb, x + dir, y - 1, true);
    }
}

static void draw_event_gear(town_fb_t *fb, bool left, uint8_t phase, bool quiet) {
    /* The gable clock: in the stepped gable of the nearer gabled house. */
    int cx = left ? TOWN_X(72) : TOWN_X(223);
    int cy = left ? GROUND_Y - 32 : GROUND_Y - 51;
    disc(fb, cx, cy, 6, false);
    ring(fb, cx, cy, 4, true);
    px(fb, cx, cy, true);
    /* Eight teeth; a gear running again has turned half a tooth. */
    uint32_t turn = phase == DUEL_CIVIC_EVENT_PHASE_COOLDOWN ? 16u : 0u;
    for (uint32_t i = 0; i < 8u; i++) {
        uint32_t a = i * 32u + turn;
        px(fb, cx + isin(a + 64u) * 6 / 127, cy + isin(a) * 6 / 127, true);
    }
    switch (phase) {
        case DUEL_CIVIC_EVENT_PHASE_ARMED:
            px(fb, cx + 2, cy - 2, true); /* the hand, stopped */
            break;
        case DUEL_CIVIC_EVENT_PHASE_ACTIVE:
            /* Seized: a bent spoke and a puff of something from the works. */
            px(fb, cx + 1, cy - 1, true);
            px(fb, cx + 2, cy - 1, true);
            px(fb, cx + 2, cy - 2, true);
            if (!quiet)
                shade_disc(fb, cx + 6, cy - 8, 2, 8);
            break;
        case DUEL_CIVIC_EVENT_PHASE_RESOLVING:
            /* A spanner on it. */
            line_step(fb, cx + 2, cy + 2, cx + 8, cy + 8, 1, 0);
            px(fb, cx + 1, cy + 3, true);
            px(fb, cx + 3, cy + 1, true);
            break;
        default:
            px(fb, cx - 2, cy - 2, true); /* the hand, moving again */
            break;
    }
}

static void draw_event_break(town_fb_t *fb, bool left, uint8_t phase, bool quiet) {
    /* A bench and a kettle out on the street. */
    int x = left ? TOWN_X(47) : TOWN_X(238);
    int feet = GROUND_Y - 1;
    town_fig_t fig;
    fig_begin(&fig, x, feet);
    fig_rect(&fig, x - 6, feet - 4, x + 6, feet - 4);
    fig_rect(&fig, x - 5, feet - 3, x - 5, feet);
    fig_rect(&fig, x + 5, feet - 3, x + 5, feet);
    if (phase != DUEL_CIVIC_EVENT_PHASE_COOLDOWN) {
        /* The kettle on the bench, a cup either side once it has poured. */
        fig_rect(&fig, x - 1, feet - 7, x + 1, feet - 5);
        fig_px(&fig, x + 2, feet - 7);
        fig_px(&fig, x, feet - 8);
        if (phase >= DUEL_CIVIC_EVENT_PHASE_ACTIVE) {
            fig_rect(&fig, x - 5, feet - 6, x - 4, feet - 5);
            fig_rect(&fig, x + 4, feet - 6, x + 5, feet - 5);
        }
    }
    fig_stamp(fb, &fig);
    /* Steam off the kettle while it is hot. */
    if (!quiet && phase < DUEL_CIVIC_EVENT_PHASE_RESOLVING) {
        int wisps = phase == DUEL_CIVIC_EVENT_PHASE_ACTIVE ? 3 : 1;
        for (int k = 0; k < wisps; k++) {
            px(fb, x + (k & 1), feet - 10 - 2 * k, true);
            px(fb, x + 1 - (k & 1), feet - 11 - 2 * k, true);
        }
    }
}

static void draw_event_crack(town_fb_t *fb, bool left, uint8_t phase, bool quiet) {
    /* Down the wall of the tall house at the end of the row. */
    int x = left ? TOWN_X(26) : TOWN_X(230);
    int top = left ? GROUND_Y - 33 : GROUND_Y - 39;
    static const int length[4] = {7, 22, 22, 9};
    int len = length[phase & 3u];
    for (int t = 0; t < len; t++)
        px(fb, x + ((t >> 2) & 1), top + t, true);
    switch (phase) {
        case DUEL_CIVIC_EVENT_PHASE_ACTIVE:
            /* What came down, on the street at its foot. */
            if (!quiet)
                for (int k = 0; k < 3; k++)
                    fill_rect(fb, x - 3 + k * 3, GROUND_Y - 2, x - 2 + k * 3, GROUND_Y - 1, true);
            break;
        case DUEL_CIVIC_EVENT_PHASE_RESOLVING:
            /* A ladder up against it. */
            for (int y = top + 2; y < GROUND_Y; y++) {
                int lean = (y - top) / 8;
                px(fb, x + 3 + lean, y, true);
                px(fb, x + 6 + lean, y, true);
                if ((y - top) % 4 == 0)
                    hline(fb, x + 3 + lean, x + 6 + lean, y);
            }
            break;
        case DUEL_CIVIC_EVENT_PHASE_COOLDOWN:
            /* Patched: a square of new plaster where it opened. */
            frame_rect(fb, x - 2, top + 1, x + 3, top + 7);
            break;
        default:
            break;
    }
}

static void draw_event_banners(town_fb_t *fb, uint8_t target, uint8_t phase) {
    /* Furled, unfurled with the visiting crest, hanging while it is seen
     * off, and furled short again. */
    static const int length[4] = {8, 16, 13, 4};
    int len = length[phase & 3u];
    bool open = phase == DUEL_CIVIC_EVENT_PHASE_ACTIVE || phase == DUEL_CIVIC_EVENT_PHASE_RESOLVING;
    for (int s = -1; s <= 1; s += 2) {
        if ((s < 0 && target == DUEL_CIVIC_EVENT_TARGET_RIGHT) ||
            (s > 0 && target == DUEL_CIVIC_EVENT_TARGET_LEFT))
            continue;
        int bx = TOWER_CX + s * (BALCONY_HALF - 3);
        int y0 = BALCONY_Y + 2;
        int half = open ? 2 : 0;
        fill_rect(fb, bx - half - 1, y0, bx + half + 1, y0 + len + 2, false);
        hline(fb, bx - half - 1, bx + half + 1, y0); /* the rod */
        fill_rect(fb, bx - half, y0 + 1, bx + half, y0 + len, true);
        if (open) {
            /* A swallowtail, and the crest left dark on the cloth. */
            px(fb, bx, y0 + len, false);
            px(fb, bx, y0 + len - 1, false);
            if (phase == DUEL_CIVIC_EVENT_PHASE_ACTIVE) {
                px(fb, bx, y0 + 4, false);
                px(fb, bx - 1, y0 + 5, false);
                px(fb, bx + 1, y0 + 5, false);
                px(fb, bx, y0 + 6, false);
            }
        }
    }
}

static void draw_event_aurora(town_fb_t *fb, const duel_render_t *r, uint8_t phase) {
    /* A curtain across the sky, passing behind the spire and the roof. Its
     * line is dotted while it gathers and fades, solid while it burns, and
     * it hangs streamers while it is at its height. */
    int base = 46;
    static const int every[4] = {2, 1, 1, 4};
    static const int streamer[4] = {0, 3, 6, 0};
    for (int x = 0; x < CANVAS_W; x++) {
        int y = base + isin((uint32_t)x * 3u + r->seed) * 4 / 127;
        int off = x - TOWER_CX < 0 ? TOWER_CX - x : x - TOWER_CX;
        int span = y >= ROOF_APEX_Y && y <= TOWER_TOP_Y
                       ? (y - ROOF_APEX_Y) * (TOWER_HALF + 4) / (TOWER_TOP_Y - ROOF_APEX_Y)
                       : 0;
        if (off <= span + 2)
            continue;
        if (x % every[phase & 3u] == 0)
            px(fb, x, y, true);
        int step = streamer[phase & 3u];
        if (step && x % step == 0)
            vline(fb, x, y + 2, y + 2 + (step == 3 ? 3 + (x / 3) % 2 : 1));
    }
}

/* The local families and the aurora; the banners go on with the tower. */
static bool civic_event(const duel_render_t *r, uint8_t *id, uint8_t *phase, uint8_t *target) {
    if (r->revision & INCANTATION_AFTERMATH_WIRE)
        return false;
    *id = DUEL_EVENT_ID(r->revision);
    *phase = DUEL_EVENT_PHASE(r->revision);
    *target = DUEL_EVENT_TARGET(r->revision);
    return *id != DUEL_CIVIC_EVENT_NONE && *id < DUEL_CIVIC_EVENT_COUNT;
}

static void draw_sky_event(town_fb_t *fb, const duel_render_t *r) {
    uint8_t id, phase, target;
    if (civic_event(r, &id, &phase, &target) && id == DUEL_CIVIC_EVENT_CIVIC_SKY)
        draw_event_aurora(fb, r, phase);
}

static void draw_street_event(town_fb_t *fb, const duel_render_t *r, uint32_t frame) {
    uint8_t id, phase, target;
    if (!civic_event(r, &id, &phase, &target))
        return;
    bool quiet = DUEL_CIVIC_MODE(r->civic) == DUEL_CIVIC_MODE_QUIET;
    bool left = target != DUEL_CIVIC_EVENT_TARGET_RIGHT;
    switch (id) {
        case DUEL_CIVIC_EVENT_RUNAWAY_SCROLL:
            draw_event_scroll(fb, left, phase, quiet, frame);
            break;
        case DUEL_CIVIC_EVENT_JAMMED_GEAR:
            draw_event_gear(fb, left, phase, quiet);
            break;
        case DUEL_CIVIC_EVENT_WORK_BREAK:
            draw_event_break(fb, left, phase, quiet);
            break;
        case DUEL_CIVIC_EVENT_DAMAGE_COMPLAINT:
            draw_event_crack(fb, left, phase, quiet);
            break;
        default:
            break;
    }
}

static void draw_tower_event(town_fb_t *fb, const duel_render_t *r) {
    uint8_t id, phase, target;
    if (civic_event(r, &id, &phase, &target) && id == DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER)
        draw_event_banners(fb, target, phase);
}

/*
 * The aftermath residents: one each side of the tower door, at what the
 * champion on that side left them to do. The panels set the room's resident
 * to the same task; here it comes down to the street. A cheer has both arms
 * up under a little confetti, a complaint holds a placard at the tower, panic
 * runs to and fro, a fire is fought with a bucket and then hammered right,
 * an inspector stoops with a glass over the marks, a repair has a ladder and
 * a hammer, and a big cast has someone pointing up at the spire before they
 * cheer. The phase is how far through it they are.
 */
static void draw_aftermath_resident(town_fb_t *fb, uint8_t kind, uint8_t phase, bool left,
                                    bool quiet) {
    if (kind == AFTER_NONE)
        return;
    int f = left ? 1 : -1; /* facing the tower */
    int x = left ? TOWER_X0 - 12 : TOWER_X1 + 12;
    int feet = GROUND_Y - 1;
    if (kind == AFTER_PANIC && (phase & 1u))
        x -= 4 * f; /* running back from it */
    bool cheer = kind == AFTER_CHEER || (kind == AFTER_MAX_CAST && phase >= 2u);
    bool panic = kind == AFTER_PANIC || (kind == AFTER_FIRE && phase == 0u);

    town_fig_t fig;
    fig_begin(&fig, x, feet);
    if (kind == AFTER_INSPECT) {
        /* Stooped: the head dropped and forward, the glass held low. */
        fig_rect(&fig, x - 1 + f, feet - 11, x + 1 + f, feet - 9);
        fig_rect(&fig, x - 2, feet - 8, x + 2, feet - 3);
        fig_px(&fig, x - 3, feet - 3);
        fig_px(&fig, x + 3, feet - 3);
        fig_rect(&fig, x - 1, feet - 2, x - 1, feet);
        fig_rect(&fig, x + 1, feet - 2, x + 1, feet);
        fig_px(&fig, x + 3 * f, feet - 7);
        fig_px(&fig, x + 4 * f, feet - 6);
        fig_box(&fig, x + 5 * f, feet - 7, x + 7 * f, feet - 5);
        for (int k = 0; k < 4 - (int)phase; k++)
            fig_px(&fig, x + (6 + 2 * k) * f, feet);
    } else {
        fig_person(&fig, x, feet, f, panic);
    }
    if (cheer) {
        fig_px(&fig, x - 3, feet - 10);
        fig_px(&fig, x - 4, feet - 11);
        fig_px(&fig, x + 3, feet - 10);
        fig_px(&fig, x + 4, feet - 11);
        int pips = kind == AFTER_CHEER ? 4 - (int)phase : 1;
        for (int k = 0; k < pips; k++)
            fig_px(&fig, x - 4 + k * 3, feet - 16 - (k & 1) * 2);
    } else if (panic) {
        /* Arms flung up and out. */
        fig_px(&fig, x - 3, feet - 9);
        fig_px(&fig, x - 4, feet - 10);
        fig_px(&fig, x - 5, feet - 12);
        fig_px(&fig, x + 3, feet - 9);
        fig_px(&fig, x + 4, feet - 10);
        fig_px(&fig, x + 5, feet - 12);
    }
    switch (kind) {
        case AFTER_COMPLAINT: {
            /* A placard on a stick, held up at the tower, lowered later. */
            int lift = phase >= 2u ? 3 : 0;
            fig_px(&fig, x + 3 * f, feet - 8);
            fig_rect(&fig, x + 4 * f, feet - 16 + lift, x + 4 * f, feet - 7);
            fig_box(&fig, x + 2 * f, feet - 21 + lift, x + 7 * f, feet - 17 + lift);
            fig_rect(&fig, x + 3 * f, feet - 19 + lift, x + 6 * f, feet - 19 + lift);
            break;
        }
        case AFTER_FIRE:
            if (phase < 3u) {
                /* Flames on the doorstep, lower each phase. */
                for (int k = 0; k < 3; k++) {
                    int fx = x + (7 + 2 * k) * f;
                    int h = 5 - (int)phase - (k == 1 ? 0 : 1);
                    fig_rect(&fig, fx, feet - h, fx, feet);
                }
            } else {
                /* Out: the scorch marked, and a hammer on it. */
                for (int d = -2; d <= 2; d++) {
                    fig_px(&fig, x + 9 * f + d, feet - 2 + d);
                    fig_px(&fig, x + 9 * f + d, feet - 2 - d);
                }
                fig_px(&fig, x + 3 * f, feet - 8);
                fig_rect(&fig, x + 4 * f, feet - 12, x + 4 * f, feet - 8);
                fig_rect(&fig, x + 3 * f, feet - 13, x + 5 * f, feet - 13);
            }
            if (phase == 1u || phase == 2u) {
                /* A bucket, and the water going on. */
                fig_box(&fig, x + 3 * f, feet - 9, x + 5 * f, feet - 7);
                if (!quiet)
                    for (int k = 0; k < 3; k++)
                        fig_px(&fig, x + (6 + k) * f, feet - 9 + k * k / 2);
            }
            break;
        case AFTER_REPAIR: {
            /* A ladder against the tower and a hammer, up and then down. */
            for (int y = feet - 18; y <= feet; y++) {
                fig_px(&fig, x + 6 * f, y);
                fig_px(&fig, x + 9 * f, y);
                if ((feet - y) % 4 == 2)
                    fig_rect(&fig, x + 6 * f, y, x + 9 * f, y);
            }
            int raised = phase < 3u && !(phase & 1u);
            fig_px(&fig, x + 3 * f, feet - 8);
            if (raised) {
                fig_rect(&fig, x + 4 * f, feet - 13, x + 4 * f, feet - 9);
                fig_rect(&fig, x + 3 * f, feet - 14, x + 5 * f, feet - 14);
            } else {
                fig_rect(&fig, x + 4 * f, feet - 8, x + 4 * f, feet - 5);
                fig_rect(&fig, x + 3 * f, feet - 4, x + 5 * f, feet - 4);
            }
            break;
        }
        case AFTER_MAX_CAST:
            if (phase < 2u) {
                /* Pointing up at the spire, with what is left of the cast
                 * drifting down. */
                for (int k = 1; k <= 4; k++)
                    fig_px(&fig, x + (2 + k) * f, feet - 9 - k);
                for (int k = 0; k < 3 - (int)phase; k++)
                    fig_px(&fig, x + (2 + 3 * k) * f, feet - 20 + k);
            }
            break;
        default:
            break;
    }
    if (panic && !quiet) {
        /* Hurry lines behind. */
        fig_rect(&fig, x - 6 * f, feet - 7, x - 8 * f, feet - 7);
        fig_rect(&fig, x - 6 * f, feet - 4, x - 7 * f, feet - 4);
    }
    fig_stamp(fb, &fig);
}

static void draw_street(town_fb_t *fb, const duel_render_t *r) {
    bool quiet = DUEL_CIVIC_MODE(r->civic) == DUEL_CIVIC_MODE_QUIET;
    if (r->revision & INCANTATION_AFTERMATH_WIRE) {
        for (uint8_t side = 0; side < 2u; side++)
            draw_aftermath_resident(fb, INCANTATION_AFTER_KIND(r->shared_pres, side),
                                    INCANTATION_AFTER_PHASE(r->revision, side), side == SIM_SIDE_L,
                                    quiet);
        return;
    }
    draw_courier_figure(fb, r);
}

/* ---- the plaza ----------------------------------------------------------- */

/*
 * The bottom fifth of the square is the nearest thing to the viewer and had
 * the least in it: five rows of dashes standing in for cobbles. It is drawn
 * as a paved square now -- stones that grow as they approach, a well, stalls
 * and lamps -- because the foreground is where detail is cheapest to read and
 * most expensive to omit.
 */
static void draw_plaza(town_fb_t *fb, const duel_render_t *r, uint32_t frame) {
    hline(fb, 0, CANVAS_W - 1, GROUND_Y);
    hline(fb, 0, CANVAS_W - 1, GROUND_Y + 1);

    /*
     * Paving as joints, not as stones. Drawing each cobble as a filled bar
     * put forty percent ink into the nearest fifth of the square and the
     * plaza came out a brick wall standing between the viewer and the town;
     * the mortar between the stones is the whole of what needs to be there.
     * Courses deepen toward the bottom edge, which is the recession.
     */
    int y = GROUND_Y + 7;
    for (int row = 0; row < 6 && y < CANVAS_H; row++) {
        int depth = 5 + row * 2;
        int gap = 14 + row * 5;
        int offset = (row & 1) ? gap / 2 : 0;
        for (int x = 0; x < CANVAS_W; x += 4)
            px(fb, x + (row & 1), y, true); /* the course, broken */
        /* The cross joints are ticks off the course, not full-depth rules.
         * Drawn the full depth they line up row over row into a picket fence,
         * which is a thing standing in the plaza rather than the plaza. */
        for (int x = offset; x < CANVAS_W; x += gap)
            vline(fb, x + ((y >> 1) & 3), y + 1, y + 1 + depth / 3);
        y += depth;
    }

    /* A well at the centre of the square, on the tower's axis. */
    int wx = TOWER_CX;
    int wy = GROUND_Y + 26;
    fill_rect(fb, wx - 11, wy - 4, wx + 11, wy + 7, false);
    frame_rect(fb, wx - 11, wy - 4, wx + 11, wy + 7);
    shade_rect(fb, wx - 10, wy - 3, wx + 10, wy + 6, 3);
    for (int x = wx - 11; x <= wx + 11; x += 4)
        vline(fb, x, wy - 4, wy + 7);
    vline(fb, wx - 8, wy - 16, wy - 5);
    vline(fb, wx + 8, wy - 16, wy - 5);
    for (int s = 0; s <= 8; s++)
        hline(fb, wx - 8 + s, wx + 8 - s, wy - 16 - s / 2); /* a little roof */
    vline(fb, wx, wy - 14, wy - 8);
    frame_rect(fb, wx - 2, wy - 8, wx + 2, wy - 5); /* the bucket */

    /* Market stalls: a striped awning on posts, one either side. */
    for (int i = 0; i < 2; i++) {
        int sx = CANVAS_W == LANDSCAPE_W ? (i ? 304 : 96) : (i ? 200 : 46);
        int sy = GROUND_Y + 22;
        vline(fb, sx - 13, sy - 10, sy + 6);
        vline(fb, sx + 13, sy - 10, sy + 6);
        hline(fb, sx - 14, sx + 14, sy - 10);
        for (int x = sx - 14; x <= sx + 14; x++)
            if (((x - sx + 60) / 3) & 1)
                vline(fb, x, sy - 9, sy - 6); /* the stripes */
        hline(fb, sx - 12, sx + 12, sy - 1);
        shade_rect(fb, sx - 12, sy, sx + 12, sy + 4, 5); /* what is on the table */
        for (int c = 0; c < 3; c++)
            disc(fb, sx - 8 + c * 8, sy - 3, 2, true);
    }

    /* Lamps down the front of the square, lit after dusk. */
    bool night = sky_is_night(DUEL_SECONDARY_SKY_PHASE(r->secondary));
    int lamps = CANVAS_W == LANDSCAPE_W ? 6 : 4;
    for (int i = 0; i < lamps; i++) {
        int lx = CANVAS_W == LANDSCAPE_W ? 24 + i * 70 : 20 + i * 72;
        int ly = GROUND_Y + 44;
        vline(fb, lx, ly - 22, ly);
        hline(fb, lx - 2, lx + 2, ly);
        frame_rect(fb, lx - 3, ly - 27, lx + 3, ly - 22);
        px(fb, lx, ly - 29, true);
        if (night) {
            fill_rect(fb, lx - 2, ly - 26, lx + 2, ly - 23, true);
            /* A pool of light on the stones under it, flickering slowly. */
            int reach = 7 + (int)(((frame >> 4) + (uint32_t)i) & 1u);
            shade_disc(fb, lx, ly + 2, reach, 4);
        }
    }
}

/*
 * The almanac: a notice board at the back of the square that keeps the day.
 * The shell counts the day's casts, impacts and knockdowns and passes them
 * back in; the library remembers none of it. Each tally is a row of strokes
 * under its own mark -- a spark, a heart, a fallen figure -- and the strokes
 * are steps, not counts: one at the first, then at 4, 16 and 64, and the
 * fifth crosses the gate when the shell's byte is full. The world casts
 * hundreds of spells an hour, so a stroke per spell would read full by mid
 * morning; a step per fourfold fills the board over the first hours instead.
 * Like the other off-keyboard signals it is a level, never a reading, and a
 * day with nothing in it draws no board at all.
 */
static int almanac_strokes(uint8_t count) {
    static const uint8_t steps[] = {1u, 4u, 16u, 64u, 255u};
    int strokes = 0;
    for (size_t i = 0; i < sizeof steps; i++)
        strokes += count >= steps[i];
    return strokes;
}

static void draw_almanac(town_fb_t *fb, const town_day_t *day) {
    if (day->casts == 0u && day->impacts == 0u && day->knockdowns == 0u)
        return;
    /* Between the left-hand stall and the next lamp, standing back with the
     * stalls so the residents cross in front of it. */
    int cx = CANVAS_W == LANDSCAPE_W ? 137 : 76;
    int x0 = cx - 10, x1 = cx + 10;
    int y0 = GROUND_Y + 5, y1 = y0 + 20;
    fill_rect(fb, x0, y0, x1, y1, false);
    frame_rect(fb, x0, y0, x1, y1);
    hline(fb, x0 - 2, x1 + 2, y0 - 2); /* the rail the notices hang from */
    px(fb, x0 - 2, y0 - 1, true);
    px(fb, x1 + 2, y0 - 1, true);
    vline(fb, x0 + 2, y1 + 1, y1 + 5); /* two legs on the stones */
    vline(fb, x1 - 2, y1 + 1, y1 + 5);

    const uint8_t counts[3] = {day->casts, day->impacts, day->knockdowns};
    for (int row = 0; row < 3; row++) {
        int ry = y0 + 2 + row * 6; /* each row is five pixels tall */
        int ix = x0 + 4;           /* the centre of its mark */
        if (row == 0) {
            /* A spark: the spells cast. */
            vline(fb, ix, ry, ry + 4);
            hline(fb, ix - 2, ix + 2, ry + 2);
            px(fb, ix - 1, ry + 1, true);
            px(fb, ix + 1, ry + 3, true);
        } else if (row == 1) {
            /* A heart: the health it cost. */
            px(fb, ix - 1, ry, true);
            px(fb, ix + 1, ry, true);
            hline(fb, ix - 2, ix + 2, ry + 1);
            hline(fb, ix - 2, ix + 2, ry + 2);
            hline(fb, ix - 1, ix + 1, ry + 3);
            px(fb, ix, ry + 4, true);
        } else {
            /* A figure laid flat, head to the left, one foot up: the
             * champions felled. */
            fill_rect(fb, ix - 2, ry + 3, ix - 1, ry + 4, true);
            hline(fb, ix, ix + 2, ry + 4);
            px(fb, ix + 2, ry + 3, true);
        }

        int strokes = almanac_strokes(counts[row]);
        int mx = x0 + 9;
        for (int i = 0; i < strokes && i < 4; i++)
            vline(fb, mx + i * 2, ry, ry + 4);
        if (strokes == 5) {
            /* The gate closed: the day's byte is full. */
            for (int k = 0; k <= 8; k++)
                px(fb, mx - 1 + k, ry + 4 - (k * 4 + 4) / 8, true);
        }
    }
}

/*
 * Residents cross the plaza on the civic clock, the same clock that paces the
 * occupation on the panels. There are more of them than there were, they walk
 * at their own depths, and the ones nearest the front are drawn a little
 * larger -- the plaza has forty-eight rows to cross and a single size read as
 * a row of identical tokens.
 *
 * The desktop's own signals reach the square as well as the objects above it.
 * Body activity says how many residents are out, typing tempo how fast they
 * walk, and last night's sleep whether they step briskly or some of them sit
 * down on the stones. Every one of them is a level, never a reading, and a
 * shell that sends none of them gets the square exactly as it was.
 */
/* Sat down where the paving is, cloak pooled round them, feet out in front. */
static void draw_square_sitter(town_fb_t *fb, int x, int y, int big) {
    fill_rect(fb, x - 2 - big, y - 5, x + 2 + big, y - 2, true);
    fill_rect(fb, x - 1, y - 8 - big, x + 1, y - 6 - big, true);
    px(fb, x - 3 - big, y - 1, true);
    px(fb, x + 3 + big, y - 1, true);
    px(fb, x - 4 - big, y, true);
    px(fb, x + 4 + big, y, true);
}

/* Cloak flaring to the hem, a head above it, and legs that alternate. Four
 * pixels of shoulder is what makes it a person and not a post. Watching is
 * the head tipped back and one arm up: the town notices the duel. Standing
 * is feet together, for someone who has got where they were going. */
static void draw_square_walker(town_fb_t *fb, int x, int y, int big, bool stepping, bool watching,
                               bool standing) {
    fill_rect(fb, x - 1 - big, y - 8 - big * 2, x + 1 + big, y - 6, true);
    fill_rect(fb, x - 2 - big, y - 5, x + 2 + big, y - 3, true);
    px(fb, x - 3 - big, y - 3, true);
    px(fb, x + 3 + big, y - 3, true);
    fill_rect(fb, x - 1, y - 11 - big * 2, x + 1, y - 9 - big * 2, true);
    if (watching) {
        px(fb, x + 2, y - 12 - big * 2, true);
        px(fb, x + 3, y - 13 - big * 2, true);
        px(fb, x - 2, y - 10 - big * 2, true);
    } else if (!standing) {
        px(fb, x + (stepping ? 1 : -1), y - 2, true);
    }
    px(fb, x - 2 - big, y - 1, true);
    px(fb, x + 2 + big, y - 1, true);
    if (standing) {
        px(fb, x - 1, y, true);
        px(fb, x + 1, y, true);
    } else {
        px(fb, stepping ? x - 3 - big : x - 2, y, true);
        px(fb, stepping ? x + 2 : x + 3 + big, y, true);
    }
}

/* A lantern carried after dark, which is the cheapest way to say the hour
 * down at street level. */
static void draw_hand_lantern(town_fb_t *fb, int x, int y, int big) {
    px(fb, x + 4 + big, y - 5, true);
    disc(fb, x + 5 + big, y - 4, 1, true);
    shade_disc(fb, x + 5 + big, y - 4, 5, 3);
}

static void draw_residents(town_fb_t *fb, const duel_render_t *r, const town_typing_t *typing,
                           const town_health_t *health, uint32_t frame) {
    bool quiet = DUEL_CIVIC_MODE(r->civic) == DUEL_CIVIC_MODE_QUIET;
    bool night = sky_is_night(DUEL_SECONDARY_SKY_PHASE(r->secondary));
    /* Two out in a quiet town and six otherwise, until a watch says how much
     * of a day it has been: three on a resting day up to nine on a full one,
     * and a quiet town keeps to a third of that. */
    int walkers = quiet ? 2 : 6;
    if (health->body != DUEL_CITY_BODY_NONE)
        walkers = quiet ? 1 + (int)health->body / 2 : 1 + (int)health->body * 2;
    /* A short night sits every third resident down; a full one puts a spring
     * in everyone's step. */
    bool tired = health->sleep == DUEL_CITY_SLEEP_TIRED;
    unsigned step_bit = health->sleep == DUEL_CITY_SLEEP_RESTED ? 1u : 2u;
    /* Somebody is watching the sky whenever there is something in it. */
    bool spell_up = duel_view_spell(&r->view, SIM_SIDE_L, r->seed).active ||
                    duel_view_spell(&r->view, SIM_SIDE_R, r->seed).active;

    for (int i = 0; i < walkers; i++) {
        uint32_t h = town_hash(r->seed, (uint32_t)i + 40u);
        int span = CANVAS_W + 40;
        /* Each walks at a pace of their own until the typing summary names
         * one: a deliberate typist's town strolls, a frantic one's hurries. */
        int speed = typing->tempo != DUEL_CITY_TEMPO_NONE ? (int)typing->tempo : 1 + (int)(h & 1u);
        int phase = (int)(((uint32_t)r->civic_phase * (uint32_t)speed + (h >> 4)) % (uint32_t)span);
        int x = (h & 2u) ? phase - 20 : span - phase - 20;
        int y = GROUND_Y + 14 + (int)((h >> 6) % 34u);
        bool stepping = ((r->civic_phase + (uint8_t)i) & step_bit) == 0u;
        bool watching = spell_up && ((h >> 11) & 3u) == 0u;
        /* Nearer the bottom of the square is nearer the viewer. */
        int big = y > GROUND_Y + 32 ? 1 : 0;

        if (tired && (i % 3) == 1) {
            /* The one figure on the square that does not cross it. */
            x = 20 + (int)((h >> 4) % (uint32_t)(CANVAS_W - 40));
            draw_square_sitter(fb, x, y, big);
            continue;
        }
        draw_square_walker(fb, x, y, big, stepping, watching, false);
        /* A lantern for one of them after dark. */
        if (night && ((h >> 13) & 3u) == 0u)
            draw_hand_lantern(fb, x, y, big);
    }
    (void)frame;
}

/*
 * The residents of the town life (DC8), where duel_city_life_advance left
 * them: walking between the places on the square, standing at the well or
 * the market stall, sat on the bench, stopped to watch a spell, or out of
 * sight indoors or past a gate. Nobody here is drawn on a hash: each figure
 * is somewhere because it is going somewhere.
 *
 * The square's x is the 256-column town's, mapped onto either composition;
 * past either end of it the road runs on to a gate just off the canvas, so a
 * resident heading out walks off the edge rather than vanishing in the
 * landscape's wings. Several stood at one place stand a little apart.
 *
 * A watch's body and sleep buckets keep the meaning they have for the hashed
 * walkers (DC4): body is how many of the residents are out on the square,
 * and a short night sits every third one that has stopped. Typing tempo
 * paces only the hashed walkers; residents walk at their own pace.
 */
static int life_column(const town_fb_t *fb, int x) {
    if (x < 0)
        return TOWN_X(0) + x * (TOWN_X(0) + 20) / 16;
    if (x > TOWN_W - 1)
        return TOWN_X(TOWN_W - 1) + (x - (TOWN_W - 1)) * (CANVAS_W - TOWN_X(TOWN_W - 1) + 20) / 16;
    return TOWN_X(x);
}

static void draw_town_life(town_fb_t *fb, const duel_render_t *r, const town_health_t *health,
                           const duel_town_life_t *life) {
    bool quiet = DUEL_CIVIC_MODE(r->civic) == DUEL_CIVIC_MODE_QUIET;
    bool night = sky_is_night(DUEL_SECONDARY_SKY_PHASE(r->secondary));
    int out = DUEL_TOWN_LIFE_RESIDENTS;
    if (health->body != DUEL_CITY_BODY_NONE)
        out = quiet ? 1 + (int)health->body / 2 : 1 + (int)health->body * 2;
    bool tired = health->sleep == DUEL_CITY_SLEEP_TIRED;
    uint32_t tick = duel_town_life_ticks(life);

    /* The bench the residents sit on for company: a seat on two legs. */
    int16_t bx, by;
    if (duel_town_life_place(DUEL_TOWN_PLACE_BENCH, &bx, &by, NULL, NULL)) {
        int x = life_column(fb, bx), y = GROUND_Y + by;
        fill_rect(fb, x - 9, y - 4, x + 9, y, false);
        hline(fb, x - 8, x + 8, y - 3);
        vline(fb, x - 7, y - 2, y);
        vline(fb, x + 7, y - 2, y);
    }

    int drawn = 0;
    for (uint8_t i = 0; i < DUEL_TOWN_LIFE_RESIDENTS && drawn < out; i++) {
        duel_town_life_view_t v;
        if (!duel_town_life_resident(life, i, &v) || !v.visible)
            continue;
        drawn++;
        bool walking = v.state == DUEL_TOWN_RES_WALK;
        int x = life_column(fb, v.x);
        int y = GROUND_Y + v.y;
        if (!walking && v.state != DUEL_TOWN_RES_WATCH)
            x += ((int)(i % 3u) - 1) * 5;
        int big = y > GROUND_Y + 32 ? 1 : 0;
        bool stopped = v.state == DUEL_TOWN_RES_STAY || v.state == DUEL_TOWN_RES_IDLE;
        if (stopped && (v.place == DUEL_TOWN_PLACE_BENCH || (tired && (i % 3u) == 1u))) {
            draw_square_sitter(fb, x, y, big);
            continue;
        }
        bool stepping = ((tick + i) & 1u) == 0u;
        draw_square_walker(fb, x, y, big, stepping, v.state == DUEL_TOWN_RES_WATCH, !walking);
        if (night && (i & 3u) == 0u)
            draw_hand_lantern(fb, x, y, big);
    }
}

void duel_town_draw(town_fb_t *fb, const duel_render_t *r, const town_typing_t *typing,
                    const town_health_t *health, const town_day_t *day,
                    const duel_town_life_t *life, uint32_t frame) {
    static const town_typing_t no_typing;
    static const town_health_t no_health;
    static const town_day_t no_day;
    if (!typing)
        typing = &no_typing;
    if (!health)
        health = &no_health;
    if (!day)
        day = &no_day;
    uint8_t phase = DUEL_SECONDARY_SKY_PHASE(r->secondary);
    uint8_t sub = DUEL_SECONDARY_SKY_SUBPHASE(r->secondary);

    draw_stars(fb, r, phase, frame);
    draw_sky_event(fb, r);
    draw_celestial(fb, phase, sub, frame);
    draw_clouds(fb, r, phase, frame);
    draw_birds(fb, r, phase, frame);
    draw_hills(fb, r);
    draw_windmill(fb, r, health->heart, frame);
    draw_far_row(fb);
    draw_near_row(fb, r, typing->spread, frame);
    draw_ridge_sleeper(fb, health->sleep, frame);
    draw_kites(fb, health->body, frame);
    draw_residue(fb, r, frame);
    draw_street_event(fb, r, frame);
    draw_tower(fb, r, typing, frame);
    draw_tower_event(fb, r);
    draw_lanterns(fb, typing);
    draw_street(fb, r);
    draw_flavor_sigil(fb, r);
    draw_wizard(fb, r, frame);
    draw_ward(fb, r, frame);
    draw_fields(fb, r, frame);
    draw_spells(fb, r, frame);
    draw_outcome(fb, r);
    draw_plaza(fb, r, frame);
    draw_almanac(fb, day);
    if (life)
        draw_town_life(fb, r, health, life);
    else
        draw_residents(fb, r, typing, health, frame);
}
