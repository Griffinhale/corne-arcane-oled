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
 * The square is 256x256 and the panels' 32x128 habits do not scale into it.
 * Two rules keep it from reading as a small drawing on a large canvas:
 *
 *   - The sky is not background. Roughly the top half of the square is air,
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
#include "duel_host.h"
#include "duel_incantation.h"
#include "duel_runtime.h"
#include "duel_view.h"

/* ---- the town's geometry, stated once -----------------------------------
 * The near ground is where the town stands; the far ground sits higher and
 * carries the smaller roofs behind it, and the hills sit higher again. The
 * tower is centred because it is the reason the town is here. */
#define GROUND_Y     208
#define FAR_GROUND_Y 196
#define HILL_BASE_Y  190
#define TOWER_CX     128
#define TOWER_HALF   19
#define TOWER_X0     (TOWER_CX - TOWER_HALF)
#define TOWER_X1     (TOWER_CX + TOWER_HALF)
#define TOWER_TOP_Y  64
#define ROOF_APEX_Y  30
#define SPIRE_TIP_Y  13
#define BALCONY_Y    102
#define BALCONY_HALF 27
/* The ward dome hangs over the tower's upper half rather than round the
 * balcony: centred on the balcony it apexed inside the roof cone, and the
 * tower -- drawn afterwards -- erased the top of its own shield. */
#define WARD_CY 124
#define DOOR_W  7

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
    int cx = 26 + step * 13; /* inset, so the corona is not clipped at dawn */
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
        int x = (int)(h % 250u) + 3;
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
        int ox = (int)(h % 170u) + 20;
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
            (int)((((frame * (uint32_t)speed) >> 6) + (h & 511u)) % (uint32_t)(TOWN_W + 120)) - 60;

        int top[TOWN_W];
        for (int i = 0; i < TOWN_W; i++)
            top[i] = -1;
        for (int lobe = 0; lobe < lobes; lobe++) {
            uint32_t g = town_hash(h, (uint32_t)lobe);
            int cx = x0 + 9 + lobe * 13;
            int radius = 6 + (int)(g % 4u);
            int cy = base - (int)((g >> 5) % 4u);
            for (int x = cx - radius; x <= cx + radius; x++) {
                if (x < 0 || x >= TOWN_W)
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
            if (x < 0 || x >= TOWN_W || top[x] < 0)
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
        int x = (int)((((frame >> 3) + (h >> 6)) % (uint32_t)(TOWN_W + 40))) - 20;
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
    for (int x = 0; x < TOWN_W; x++) {
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
    int sx = (r->seed & 1u) ? 224 : 70;
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
static const town_building_t near_row[] = {
    {12, 42, 40, 9, 0, 1},   {58, 28, 27, 0, 1, 0},  {88, 18, 20, 0, 2, 0},
    {166, 33, 33, 24, 0, 1}, {203, 41, 46, 8, 1, 0},
};

static void draw_far_row(town_fb_t *fb) {
    for (size_t i = 0; i < sizeof far_row / sizeof far_row[0]; i++) {
        const town_building_t *b = &far_row[i];
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
    for (int x = 0; x < TOWN_W; x += 3)
        px(fb, x, FAR_GROUND_Y, true);
}

/* Puffs, not a plume: three of them, well separated, leaning further as they
 * rise and thinning out at the top. A continuous column reads as a mast. */
static void draw_smoke(town_fb_t *fb, int x, int base_y, uint32_t frame, uint32_t salt) {
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

static void draw_near_row(town_fb_t *fb, const duel_render_t *r, uint32_t frame) {
    uint8_t mode = DUEL_CIVIC_MODE(r->civic);
    bool night = sky_is_night(DUEL_SECONDARY_SKY_PHASE(r->secondary));
    for (size_t i = 0; i < sizeof near_row / sizeof near_row[0]; i++) {
        const town_building_t *b = &near_row[i];
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
            draw_smoke(fb, cx + 2, ctop, frame, (uint32_t)i * 13u);
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

/* Two storeys that are not civic floors, for the ends of the tower. */
#define ROOM_LOFT   4
#define ROOM_CELLAR 5

/*
 * Drawing inside a room, in whichever sense the light is going.
 *
 * A lit room is a bright rectangle with its furniture in silhouette; an unlit
 * one is dark with the same furniture dimly picked out. One set of shapes,
 * two polarities, so a storey does not have to be drawn twice and the two
 * cannot drift apart.
 */
static void room_px(town_fb_t *fb, int x, int y, bool lit) {
    if (lit)
        px(fb, x, y, false);
    else if (shade_on(x, y, 6))
        px(fb, x, y, true);
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

/*
 * What is in each room, by the floor it is.
 *
 * The four civic floors are four different rooms rather than four heights of
 * the same one -- a hearth, a library, a workshop, and whatever the top of a
 * wizard's tower is for -- so that switching applications changes the picture
 * and not only which rectangle is bright.
 *
 * Everything on the floor is measured up from the floor and everything hung
 * from the ceiling is measured down from it, because the same room is drawn
 * at two heights and only the wall between them changes length. `roomy` is
 * the extra furniture the tall middle storey has space for: the room you are
 * on is not merely bigger, it has more in it.
 */
static void draw_room_contents(town_fb_t *fb, int x0, int y0, int height, int floor, bool lit,
                               uint32_t frame, uint8_t phase) {
    int floor_y = y0 + height - 1;
    bool roomy = height >= ROOM_LARGE_H;

    switch (floor) {
        case DUEL_CIVIC_FLOOR_COMMONS: {
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
        case DUEL_CIVIC_FLOOR_RESEARCH: {
            /* Shelves of books, and a lectern to read one at. */
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
        case DUEL_CIVIC_FLOOR_WORKSHOP: {
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
            room_hline(fb, x0 + 23, x0 + 30, floor_y - 3, !lit);
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
        case DUEL_CIVIC_FLOOR_SPECIAL: {
            /* The top of a wizard's tower: an orb on its tripod and a glass
             * pointed at the sky it has all this height for. */
            room_rect(fb, x0 + 6, floor_y - 11, x0 + 12, floor_y - 6, lit);
            room_vline(fb, x0 + 6, floor_y - 5, floor_y, lit);
            room_vline(fb, x0 + 12, floor_y - 5, floor_y, lit);
            room_vline(fb, x0 + 9, floor_y - 5, floor_y, lit);
            for (int i = 0; i <= 9; i++) {
                room_px(fb, x0 + 20 + i, floor_y - 4 - i, lit);
                room_px(fb, x0 + 20 + i, floor_y - 3 - i, lit);
            }
            room_vline(fb, x0 + 24, floor_y - 6, floor_y, lit);
            room_hline(fb, x0 + 22, x0 + 26, floor_y, lit);
            if (roomy) {
                /* A chart of the sky pinned to the wall, and a lamp on a
                 * chain over the orb. */
                room_rect(fb, x0 + 1, y0 + 2, x0 + 11, y0 + 9, lit);
                for (int i = 0; i < 5; i++) {
                    uint32_t h = town_hash((uint32_t)i, 3u);
                    room_px(fb, x0 + 2 + (int)(h % 9u), y0 + 3 + (int)((h >> 5) % 6u), !lit);
                }
                room_vline(fb, x0 + 22, y0 + 1, y0 + 4, lit);
                room_rect(fb, x0 + 20, y0 + 5, x0 + 24, y0 + 7, lit);
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
            room_hline(fb, x0 + 22, x0 + 27, floor_y - 3, !lit);
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
    if (lit && floor <= DUEL_CIVIC_FLOOR_SPECIAL) {
        int span = ROOM_X1 - ROOM_X0 - 8;
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

static void draw_tower(town_fb_t *fb, const duel_render_t *r, uint32_t frame) {
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
    for (int i = 0; i < 12; i++) {
        int wave = isin((uint32_t)(i * 18) + (frame >> 1)) * 2 / 127;
        int depth = 4 - i / 4;
        for (int t = 0; t <= depth; t++)
            px(fb, TOWER_CX + 1 + i, flag_y + wave + t, true);
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
    /* Slot 0 is the upper neighbour, 1 the active floor, 2 the lower one. */
    const int slot_floor[TOWER_SLOTS] = {
        active + 1 > DUEL_CIVIC_FLOOR_SPECIAL ? ROOM_LOFT : active + 1,
        active,
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
        if (lit)
            fill_rect(fb, ROOM_X0, y0, ROOM_X1, y0 + height - 1, true);
        draw_room_contents(fb, ROOM_X0, y0, height, slot_floor[slot], lit, frame, r->civic_phase);
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

/* ---- the wizard on the balcony ------------------------------------------- */

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
#define SPELL_REACH 96
#define SPELL_Y0    (BALCONY_Y - 10)
#define SPELL_Y1    (GROUND_Y - 58)

static void spell_point(uint8_t side, uint8_t traj, int travel, int *out_x, int *out_y) {
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
    int reach = along * SPELL_REACH / 255;
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
        spell_point(side, traj, travel, &x, &y);

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
            spell_point(side, traj, t, &ax, &ay);
            if (shade_on(ax, ay, 8))
                px(fb, ax, ay, true);
        }
        for (int t = 0; t < travel; t += 3) {
            int tx, ty;
            spell_point(side, traj, t, &tx, &ty);
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
                kind == FX_HEAL_L || kind == FX_WARD_SHATTER_L;
    int lead = left ? -1 : 1;
    int age = 12 - (int)r->flash_frames;
    if (age < 0)
        age = 0;
    /* Where the flight ends, so the burst is where the spell was. */
    int x = TOWER_CX + lead * SPELL_REACH;
    int y = SPELL_Y1;

    switch (kind) {
        case FX_IMPACT_L:
        case FX_IMPACT_R: {
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
        int cx = slot ? 190 : 66;
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
static const int residue_x[SIM_RESIDUE_ZONES] = {
    TOWER_CX - SPELL_REACH,
    61,
    195,
    TOWER_CX + SPELL_REACH,
};

/*
 * The top of the near row's silhouette over one column, so a mark can be put
 * on the roof rather than at a height guessed near it. Every branch is the
 * matching branch of draw_near_row read back: a pitch rises half a pixel per
 * column from the eave, a stepped gable stands three rows per step, and a
 * parapet is flat six above the eave. Nothing standing there is the ground.
 */
static int near_row_roof_y(int x) {
    for (size_t i = 0; i < sizeof near_row / sizeof near_row[0]; i++) {
        const town_building_t *b = &near_row[i];
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
        int x = residue_x[zone];
        int base = near_row_roof_y(x);
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

/* ---- the plaza ----------------------------------------------------------- */

/*
 * The bottom fifth of the square is the nearest thing to the viewer and had
 * the least in it: five rows of dashes standing in for cobbles. It is drawn
 * as a paved square now -- stones that grow as they approach, a well, stalls
 * and lamps -- because the foreground is where detail is cheapest to read and
 * most expensive to omit.
 */
static void draw_plaza(town_fb_t *fb, const duel_render_t *r, uint32_t frame) {
    hline(fb, 0, TOWN_W - 1, GROUND_Y);
    hline(fb, 0, TOWN_W - 1, GROUND_Y + 1);

    /*
     * Paving as joints, not as stones. Drawing each cobble as a filled bar
     * put forty percent ink into the nearest fifth of the square and the
     * plaza came out a brick wall standing between the viewer and the town;
     * the mortar between the stones is the whole of what needs to be there.
     * Courses deepen toward the bottom edge, which is the recession.
     */
    int y = GROUND_Y + 7;
    for (int row = 0; row < 6 && y < TOWN_H; row++) {
        int depth = 5 + row * 2;
        int gap = 14 + row * 5;
        int offset = (row & 1) ? gap / 2 : 0;
        for (int x = 0; x < TOWN_W; x += 4)
            px(fb, x + (row & 1), y, true); /* the course, broken */
        /* The cross joints are ticks off the course, not full-depth rules.
         * Drawn the full depth they line up row over row into a picket fence,
         * which is a thing standing in the plaza rather than the plaza. */
        for (int x = offset; x < TOWN_W; x += gap)
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
        int sx = i ? 200 : 46;
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
    for (int i = 0; i < 4; i++) {
        int lx = 20 + i * 72;
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
 * Residents cross the plaza on the civic clock, the same clock that paces the
 * occupation on the panels. There are more of them than there were, they walk
 * at their own depths, and the ones nearest the front are drawn a little
 * larger -- the plaza has forty-eight rows to cross and a single size read as
 * a row of identical tokens.
 */
static void draw_residents(town_fb_t *fb, const duel_render_t *r, uint32_t frame) {
    uint8_t mode = DUEL_CIVIC_MODE(r->civic);
    bool night = sky_is_night(DUEL_SECONDARY_SKY_PHASE(r->secondary));
    int walkers = mode == DUEL_CIVIC_MODE_QUIET ? 2 : 6;
    /* Somebody is watching the sky whenever there is something in it. */
    bool spell_up = duel_view_spell(&r->view, SIM_SIDE_L, r->seed).active ||
                    duel_view_spell(&r->view, SIM_SIDE_R, r->seed).active;

    for (int i = 0; i < walkers; i++) {
        uint32_t h = town_hash(r->seed, (uint32_t)i + 40u);
        int span = TOWN_W + 40;
        int speed = 1 + (int)(h & 1u);
        int phase = (int)(((uint32_t)r->civic_phase * (uint32_t)speed + (h >> 4)) % (uint32_t)span);
        int x = (h & 2u) ? phase - 20 : span - phase - 20;
        int y = GROUND_Y + 14 + (int)((h >> 6) % 34u);
        bool stepping = ((r->civic_phase + (uint8_t)i) & 2u) == 0u;
        bool watching = spell_up && ((h >> 11) & 3u) == 0u;
        /* Nearer the bottom of the square is nearer the viewer. */
        int big = y > GROUND_Y + 32 ? 1 : 0;

        /* Cloak flaring to the hem, a head above it, and legs that alternate.
         * Four pixels of shoulder is what makes it a person and not a post. */
        fill_rect(fb, x - 1 - big, y - 8 - big * 2, x + 1 + big, y - 6, true);
        fill_rect(fb, x - 2 - big, y - 5, x + 2 + big, y - 3, true);
        px(fb, x - 3 - big, y - 3, true);
        px(fb, x + 3 + big, y - 3, true);
        fill_rect(fb, x - 1, y - 11 - big * 2, x + 1, y - 9 - big * 2, true);
        if (watching) {
            /* Head tipped back, one arm up: the town notices the duel. */
            px(fb, x + 2, y - 12 - big * 2, true);
            px(fb, x + 3, y - 13 - big * 2, true);
            px(fb, x - 2, y - 10 - big * 2, true);
        } else {
            px(fb, x + (stepping ? 1 : -1), y - 2, true);
        }
        px(fb, x - 2 - big, y - 1, true);
        px(fb, x + 2 + big, y - 1, true);
        px(fb, stepping ? x - 3 - big : x - 2, y, true);
        px(fb, stepping ? x + 2 : x + 3 + big, y, true);

        /* A lantern for one of them after dark, which is the cheapest way to
         * say the hour down at street level. */
        if (night && ((h >> 13) & 3u) == 0u) {
            px(fb, x + 4 + big, y - 5, true);
            disc(fb, x + 5 + big, y - 4, 1, true);
            shade_disc(fb, x + 5 + big, y - 4, 5, 3);
        }
    }
    (void)frame;
}

void duel_town_draw(town_fb_t *fb, const duel_render_t *r, uint32_t frame) {
    uint8_t phase = DUEL_SECONDARY_SKY_PHASE(r->secondary);
    uint8_t sub = DUEL_SECONDARY_SKY_SUBPHASE(r->secondary);

    draw_stars(fb, r, phase, frame);
    draw_celestial(fb, phase, sub, frame);
    draw_clouds(fb, r, phase, frame);
    draw_birds(fb, r, phase, frame);
    draw_hills(fb, r);
    draw_far_row(fb);
    draw_near_row(fb, r, frame);
    draw_residue(fb, r, frame);
    draw_tower(fb, r, frame);
    draw_wizard(fb, r, frame);
    draw_ward(fb, r, frame);
    draw_fields(fb, r, frame);
    draw_spells(fb, r, frame);
    draw_outcome(fb, r);
    draw_plaza(fb, r, frame);
    draw_residents(fb, r, frame);
}
