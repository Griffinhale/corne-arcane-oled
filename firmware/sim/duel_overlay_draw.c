#include "duel_draw_internal.h"
#include "duel_host.h"
#include "duel_proto.h"
#include "duel_resident.h"
#include "duel_runtime.h"

void duel_overlay_draw_box3(duel_fb_t *fb, int x, int y) {
    for (int i = 0; i < 3; i++) {
        duel_fb_px(fb, x + i, y, true);
        duel_fb_px(fb, x + i, y + 2, true);
    }
    duel_fb_px(fb, x, y + 1, true);
    duel_fb_px(fb, x + 2, y + 1, true);
}

// Normalized alert glyphs. Each row is five bits wide; category identity
// is deterministic and independent of application/source text.
static const uint8_t alert_glyphs[DUEL_HOST_CATEGORY_COUNT][7] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // none
    {0x1F, 0x10, 0x10, 0x16, 0x10, 0x10, 0x1F}, // terminal prompt
    {0x0E, 0x11, 0x11, 0x15, 0x0E, 0x04, 0x08}, // communication bubble
    {0x04, 0x0E, 0x15, 0x04, 0x15, 0x0E, 0x04}, // transfer arrows
    {0x04, 0x15, 0x0E, 0x1B, 0x0E, 0x15, 0x04}, // system cog
    {0x0A, 0x1F, 0x11, 0x17, 0x15, 0x11, 0x1F}, // calendar
    {0x0E, 0x11, 0x11, 0x1F, 0x0E, 0x04, 0x04}, // security shield
    {0x04, 0x0E, 0x1F, 0x1B, 0x1F, 0x0E, 0x04}, // other diamond
};

static void draw_alert_bitmap(duel_fb_t *fb, uint8_t category, int ox, int oy, bool mirror) {
    if (category >= DUEL_HOST_CATEGORY_COUNT)
        return;
    for (int y = 0; y < 7; y++) {
        for (int x = 0; x < 5; x++) {
            if (alert_glyphs[category][y] & (1u << (4 - x))) {
                duel_fb_px(fb, mirror ? ox - x : ox + x, oy + y, true);
            }
        }
    }
}

/* The alert hangs as a banner on the wizard tower's shaft (between the
 * upper window and the balcony) instead of owning a reserved top corner.
 * Canonical coordinates describe the left shaft; the right half is its exact
 * desk mirror, so the pair still reads as one desk-space instrument. */
void duel_overlay_draw_alert(duel_fb_t *fb, const duel_render_t *r, bool is_left) {
    uint8_t category = DUEL_HOST_ALERT_CATEGORY(r->alert);
    uint8_t priority = DUEL_HOST_ALERT_PRIORITY(r->alert);
    uint8_t age = DUEL_HOST_ALERT_AGE(r->alert);
    if (!duel_render_host_online(r) || !duel_render_notification_count(r) ||
        category == DUEL_HOST_CATEGORY_NONE || priority == DUEL_HOST_PRIORITY_NONE)
        return;
    // Clear the banner field on the shaft face so the glyph stays legible
    // over a lit window edge or a dragged body crossing the doorway.
    for (int y = 24; y <= 35; y++)
        for (int x = 2; x <= 10; x++)
            duel_fb_px(fb, is_left ? x : 31 - x, y, false);
    draw_alert_bitmap(fb, category, is_left ? 3 : 28, 26, !is_left);
    if (priority >= DUEL_HOST_PRIORITY_NORMAL) {
        for (int x = 2; x <= 8; x++) {
            duel_fb_px(fb, is_left ? x : 31 - x, 25, true);
            duel_fb_px(fb, is_left ? x : 31 - x, 33, true);
        }
    }
    if (priority == DUEL_HOST_PRIORITY_CRITICAL) {
        for (int y = 26; y <= 32; y++) {
            duel_fb_px(fb, is_left ? 2 : 29, y, true);
            duel_fb_px(fb, is_left ? 8 : 23, y, true);
        }
        duel_fb_px(fb, is_left ? 10 : 21, 25, true);
        duel_fb_px(fb, is_left ? 10 : 21, 33, true);
    }
    int accents = 3 - (age > 5 ? 3 : age / 2);
    for (int i = 0; i < accents; i++)
        duel_fb_px(fb, is_left ? 2 + i * 3 : 29 - i * 3, 24, true);
    int pips = duel_render_notification_count(r) > 4 ? 4 : duel_render_notification_count(r);
    for (int i = 0; i < pips; i++)
        duel_fb_px(fb, is_left ? 2 + i * 2 : 29 - i * 2, 35, true);
    if (DUEL_HOST_CONTEXT_PERSISTENT(r->external)) {
        int ax = is_left ? 6 : 25;
        duel_fb_px(fb, ax, 36, true);
        duel_fb_px(fb, ax, 37, true);
        duel_fb_px(fb, ax + (is_left ? -1 : 1), 38, true);
        duel_fb_px(fb, ax + (is_left ? 1 : -1), 38, true);
    }
}

/* Each OLED owns one vertical almanac scroll. The paper is an outlined black
 * reading field rather than an illuminated slab: its rollers and labelled
 * rows establish hierarchy while the living room remains visible around the
 * narrow margins. Seven bounded extents unroll from the centre; content then
 * advances upward one pixel at a time and freezes while the scroll rerolls. */
#define SCRY_SCROLL_TOP      5
#define SCRY_SCROLL_BOTTOM   123
#define SCRY_CONTENT_TOP     10
#define SCRY_CONTENT_X0      4
#define SCRY_CONTENT_X1      27
#define SCRY_VALUE_ROW_FIRST 13
#define SCRY_VALUE_ROW_STEP  17

typedef struct {
    duel_fb_t *fb;
    int clip_top;
    int clip_bottom;
    uint8_t scroll;
} scry_scroll_t;

#define SCRY_GLYPH(r0, r1, r2, r3, r4)                                                             \
    ((uint16_t)((r0) | ((r1) << 3) | ((r2) << 6) | ((r3) << 9) | ((r4) << 12)))

static uint16_t scry_glyph(char c) {
    switch (c) {
        case 'A':
            return SCRY_GLYPH(2, 5, 7, 5, 5);
        case 'B':
            return SCRY_GLYPH(3, 5, 3, 5, 3);
        case 'C':
            return SCRY_GLYPH(6, 1, 1, 1, 6);
        case 'D':
            return SCRY_GLYPH(3, 5, 5, 5, 3);
        case 'E':
            return SCRY_GLYPH(7, 1, 3, 1, 7);
        case 'F':
            return SCRY_GLYPH(7, 1, 3, 1, 1);
        case 'G':
            return SCRY_GLYPH(6, 1, 5, 5, 6);
        case 'H':
            return SCRY_GLYPH(5, 5, 7, 5, 5);
        case 'I':
            return SCRY_GLYPH(7, 2, 2, 2, 7);
        case 'J':
            return SCRY_GLYPH(4, 4, 4, 5, 2);
        case 'K':
            return SCRY_GLYPH(5, 5, 3, 5, 5);
        case 'L':
            return SCRY_GLYPH(1, 1, 1, 1, 7);
        case 'M':
            return SCRY_GLYPH(5, 7, 7, 5, 5);
        case 'N':
            return SCRY_GLYPH(5, 7, 7, 7, 5);
        case 'O':
            return SCRY_GLYPH(2, 5, 5, 5, 2);
        case 'P':
            return SCRY_GLYPH(3, 5, 3, 1, 1);
        case 'Q':
            return SCRY_GLYPH(2, 5, 5, 7, 6);
        case 'R':
            return SCRY_GLYPH(3, 5, 3, 5, 5);
        case 'S':
            return SCRY_GLYPH(6, 1, 2, 4, 3);
        case 'T':
            return SCRY_GLYPH(7, 2, 2, 2, 2);
        case 'U':
            return SCRY_GLYPH(5, 5, 5, 5, 7);
        case 'V':
            return SCRY_GLYPH(5, 5, 5, 5, 2);
        case 'W':
            return SCRY_GLYPH(5, 5, 7, 7, 5);
        case 'X':
            return SCRY_GLYPH(5, 5, 2, 5, 5);
        case 'Y':
            return SCRY_GLYPH(5, 5, 2, 2, 2);
        case 'Z':
            return SCRY_GLYPH(7, 4, 2, 1, 7);
        case '0':
            return SCRY_GLYPH(2, 5, 5, 5, 2);
        case '1':
            return SCRY_GLYPH(2, 3, 2, 2, 7);
        case '2':
            return SCRY_GLYPH(3, 4, 2, 1, 7);
        case '3':
            return SCRY_GLYPH(3, 4, 2, 4, 3);
        case '4':
            return SCRY_GLYPH(5, 5, 7, 4, 4);
        case '5':
            return SCRY_GLYPH(7, 1, 3, 4, 3);
        case '6':
            return SCRY_GLYPH(6, 1, 7, 5, 7);
        case '7':
            return SCRY_GLYPH(7, 4, 2, 2, 2);
        case '8':
            return SCRY_GLYPH(7, 5, 7, 5, 7);
        case '9':
            return SCRY_GLYPH(7, 5, 7, 4, 3);
        case '/':
            return SCRY_GLYPH(4, 4, 2, 1, 1);
        default:
            return 0u;
    }
}

static int scry_stream_y(const scry_scroll_t *scroll, int virtual_y) {
    int y = (virtual_y - scroll->scroll) % DUEL_SCRY_STREAM_PIXELS;
    if (y < 0)
        y += DUEL_SCRY_STREAM_PIXELS;
    return SCRY_CONTENT_TOP + y;
}

static void scry_stream_px(const scry_scroll_t *scroll, int x, int virtual_y) {
    int y = scry_stream_y(scroll, virtual_y);
    if (x >= SCRY_CONTENT_X0 && x <= SCRY_CONTENT_X1 && y >= scroll->clip_top &&
        y <= scroll->clip_bottom)
        duel_fb_px(scroll->fb, x, y, true);
}

static int scry_text_width(const char *text) {
    int count = 0;
    while (text[count])
        count++;
    return count ? count * 4 - 1 : 0;
}

static void scry_text(const scry_scroll_t *scroll, int virtual_y, const char *text) {
    int x0 = (DUEL_CANVAS_W - scry_text_width(text)) / 2;
    for (int i = 0; text[i]; i++) {
        uint16_t glyph = scry_glyph(text[i]);
        for (int y = 0; y < 5; y++)
            for (int x = 0; x < 3; x++)
                if (glyph & ((uint16_t)1u << (y * 3 + x)))
                    scry_stream_px(scroll, x0 + i * 4 + x, virtual_y + y);
    }
}

static void scry_number(const scry_scroll_t *scroll, int virtual_y, uint8_t value) {
    char number[3] = {0};
    if (value >= 10u) {
        number[0] = (char)('0' + value / 10u);
        number[1] = (char)('0' + value % 10u);
    } else {
        number[0] = (char)('0' + value);
    }
    scry_text(scroll, virtual_y, number);
}

static void scry_fraction(const scry_scroll_t *scroll, int virtual_y, uint8_t value,
                          uint8_t total) {
    char fraction[4] = {(char)('0' + value), '/', (char)('0' + total), 0};
    scry_text(scroll, virtual_y, fraction);
}

static void scry_hp_fraction(const scry_scroll_t *scroll, int virtual_y, uint8_t hp) {
    char fraction[6] = {0};
    int cursor = 0;
    if (hp >= 10u) {
        fraction[cursor++] = '1';
        fraction[cursor++] = '0';
    } else {
        fraction[cursor++] = (char)('0' + hp);
    }
    fraction[cursor++] = '/';
    if (SIM_MAX_HP >= 10) {
        fraction[cursor++] = '1';
        fraction[cursor++] = '0';
    } else {
        fraction[cursor++] = (char)('0' + SIM_MAX_HP);
    }
    scry_text(scroll, virtual_y, fraction);
}

static void scry_pair(const scry_scroll_t *scroll, uint8_t row, const char *label,
                      const char *value) {
    int y = SCRY_VALUE_ROW_FIRST + row * SCRY_VALUE_ROW_STEP;
    scry_text(scroll, y, label);
    scry_text(scroll, y + 6, value);
}

static void scry_city_content(const scry_scroll_t *scroll, const duel_render_t *r, bool is_left) {
    static const char *const districts[DUEL_DISTRICT_COUNT] = {
        "COMMON", "ARCHIV", "WORK", "OBSERV", "SCRIPT", "STUDIO", "ARENA", "UNDER",
    };
    static const char *const modes[] = {"NORMAL", "QUIET", "URGENT", "QUIET"};
    static const char *const levels[] = {"CALM", "ACTIVE", "BUSY", "SAT"};
    static const char *const actions[] = {"WORK",  "WALK",  "CHECK", "REST",
                                          "WATCH", "DELIV", "REACT"};
    static const char *const couriers[] = {"NONE", "MSG", "PARCEL", "BEACON", "GUARD"};
    static const char *const events[] = {"NONE",   "SCROLL", "GEAR", "BREAK",
                                         "DAMAGE", "DIPLO",  "SKY"};
    uint8_t district = duel_render_district(r);
    uint8_t mode = DUEL_CIVIC_MODE(r->civic);
    civic_resident_t resident =
        civic_resident_derive(r->seed, is_left, district, mode, r->civic_phase);
    uint8_t courier = DUEL_VISITOR_KIND(r->shared_pres);
    uint8_t event = (r->revision & INCANTATION_AFTERMATH_WIRE) ? 0u : DUEL_EVENT_ID(r->revision);
    scry_pair(scroll, 0u, "DIST", districts[district]);
    scry_pair(scroll, 1u, "MODE", modes[mode]);
    scry_pair(scroll, 2u, "LEVEL", levels[DUEL_CIVIC_INTENSITY(r->civic)]);
    scry_pair(scroll, 3u, "TASK", actions[resident.action]);
    scry_pair(scroll, 4u, "VISIT", couriers[courier]);
    scry_pair(scroll, 5u, "EVENT", events[event]);
}

static void scry_duel_content(const scry_scroll_t *scroll, const duel_render_t *r, bool is_left) {
    static const char *const statuses[] = {"NONE", "BURN", "FROZEN", "DISRUP", "MARKED"};
    static const char *const elements[] = {"FORCE", "EMBER", "FROST", "VOID"};
    static const char *const fields[] = {"NONE", "TRAP",  "SING", "STEAM",
                                         "RUNE", "FAMIL", "WALL", "VORTEX"};
    uint8_t side = is_left ? SIM_SIDE_L : SIM_SIDE_R;
    duel_view_wizard_t wizard = duel_view_wizard(&r->view, side);
    scry_text(scroll, SCRY_VALUE_ROW_FIRST, "HP");
    scry_hp_fraction(scroll, SCRY_VALUE_ROW_FIRST + 6, wizard.hp);
    scry_text(scroll, SCRY_VALUE_ROW_FIRST + SCRY_VALUE_ROW_STEP, "WARD");
    scry_number(scroll, SCRY_VALUE_ROW_FIRST + SCRY_VALUE_ROW_STEP + 6, wizard.ward_strength);
    scry_pair(scroll, 2u, "STATUS", statuses[wizard.status]);
    scry_pair(scroll, 3u, "SPELL",
              wizard.prepared ? elements[VIEW_PHASE_ELEMENT(r->view.phase[side])] : "NONE");
    scry_text(scroll, SCRY_VALUE_ROW_FIRST + 4 * SCRY_VALUE_ROW_STEP, "RESID");
    char residue[5] = {0};
    for (uint8_t zone = 0; zone < SIM_RESIDUE_ZONES; zone++)
        residue[zone] = (char)('0' + DUEL_RENDER_RESIDUE_INTENSITY(r, zone));
    scry_text(scroll, SCRY_VALUE_ROW_FIRST + 4 * SCRY_VALUE_ROW_STEP + 6, residue);
    scry_text(scroll, SCRY_VALUE_ROW_FIRST + 5 * SCRY_VALUE_ROW_STEP, "FIELD");
    scry_text(scroll, SCRY_VALUE_ROW_FIRST + 5 * SCRY_VALUE_ROW_STEP + 6,
              fields[DUEL_FIELD_KIND(r->field[0])]);
    scry_text(scroll, SCRY_VALUE_ROW_FIRST + 5 * SCRY_VALUE_ROW_STEP + 12,
              fields[DUEL_FIELD_KIND(r->field[1])]);
}

static void scry_host_content(const scry_scroll_t *scroll, const duel_render_t *r) {
    static const char *const districts[DUEL_DISTRICT_COUNT] = {
        "COMMON", "ARCHIV", "WORK", "OBSERV", "SCRIPT", "STUDIO", "ARENA", "UNDER",
    };
    static const char *const alerts[] = {"NONE",   "TERM", "COMMS",  "XFER",
                                         "SYSTEM", "CAL",  "SECURE", "OTHER"};
    static const char *const activities[] = {"NONE", "MEDIA",  "XFER", "SYSTEM",
                                             "CAL",  "SCROLL", "TAB",  "PAGE"};
    static const char *const priorities[] = {"NONE", "LOW", "NORMAL", "CRIT"};
    scry_pair(scroll, 0u, "LINK", duel_render_host_online(r) ? "ONLINE" : "OFF");
    scry_pair(scroll, 1u, "FOCUS", districts[duel_render_district(r)]);
    scry_text(scroll, SCRY_VALUE_ROW_FIRST + 2 * SCRY_VALUE_ROW_STEP, "NOTICE");
    scry_number(scroll, SCRY_VALUE_ROW_FIRST + 2 * SCRY_VALUE_ROW_STEP + 6,
                duel_render_notification_count(r));
    scry_text(scroll, SCRY_VALUE_ROW_FIRST + 3 * SCRY_VALUE_ROW_STEP, "ALERT");
    scry_text(scroll, SCRY_VALUE_ROW_FIRST + 3 * SCRY_VALUE_ROW_STEP + 6,
              alerts[DUEL_HOST_ALERT_CATEGORY(r->alert)]);
    char priority[7] = {0};
    int cursor = 0;
    if (DUEL_HOST_CONTEXT_PERSISTENT(r->external)) {
        priority[cursor++] = 'P';
        priority[cursor++] = ' ';
    }
    const char *priority_name = priorities[DUEL_HOST_ALERT_PRIORITY(r->alert)];
    for (int i = 0; priority_name[i] && cursor < 6; i++)
        priority[cursor++] = priority_name[i];
    scry_text(scroll, SCRY_VALUE_ROW_FIRST + 3 * SCRY_VALUE_ROW_STEP + 12, priority);
    scry_text(scroll, SCRY_VALUE_ROW_FIRST + 4 * SCRY_VALUE_ROW_STEP, "TIMER");
    if (DUEL_CIVIC_FLOOR(r->civic) == DUEL_CIVIC_FLOOR_SPECIAL) {
        char quarter[3] = {'Q', (char)('1' + DUEL_CIVIC_INTENSITY(r->civic)), 0};
        scry_text(scroll, SCRY_VALUE_ROW_FIRST + 4 * SCRY_VALUE_ROW_STEP + 6, quarter);
    } else {
        scry_text(scroll, SCRY_VALUE_ROW_FIRST + 4 * SCRY_VALUE_ROW_STEP + 6, "IDLE");
    }
    scry_pair(scroll, 5u, "ACT", activities[DUEL_SECONDARY_ACTIVITY(r->secondary)]);
}

static void draw_scry_roller(duel_fb_t *fb, int y, bool top) {
    int inner = top ? 1 : -1;
    duel_fb_hline(fb, 4, 27, y);
    duel_fb_hline(fb, 2, 29, y + inner);
    duel_fb_hline(fb, 4, 27, y + inner * 2);
    duel_fb_px(fb, 1, y + inner, true);
    duel_fb_px(fb, 30, y + inner, true);
}

void duel_overlay_draw_scry(duel_fb_t *fb, const duel_render_t *r, bool is_left) {
    static const uint8_t half_height[] = {0u, 3u, 9u, 17u, 27u, 39u, 51u, 59u};
    uint8_t extent = DUEL_SCRY_MOTION_EXTENT(r->scry_motion);
    if (!extent && duel_view_scry_open(&r->view))
        extent = DUEL_SCRY_EXTENT_FULL;
    if (!extent)
        return;

    int top = 64 - half_height[extent];
    int bottom = 64 + half_height[extent];
    if (extent == DUEL_SCRY_EXTENT_FULL) {
        top = SCRY_SCROLL_TOP;
        bottom = SCRY_SCROLL_BOTTOM;
    }
    for (int y = top; y <= bottom; y++)
        for (int x = 2; x <= 29; x++)
            duel_fb_px(fb, x, y, false);
    draw_scry_roller(fb, top, true);
    draw_scry_roller(fb, bottom, false);
    for (int y = top + 3; y <= bottom - 3; y++) {
        duel_fb_px(fb, 3, y, true);
        duel_fb_px(fb, 28, y, true);
    }

    scry_scroll_t scroll = {
        .fb = fb,
        .clip_top = top + 4,
        .clip_bottom = bottom - 4,
        .scroll = (uint8_t)(r->scry_scroll % DUEL_SCRY_STREAM_PIXELS),
    };
    uint8_t page = VIEW_OVERLAY_SCENE(r->view.outcome_overlay);
    static const char *const page_name[SCRY_SCENES] = {"CITY", "DUEL", "HOST"};
    scry_text(&scroll, 0, page_name[page]);
    scry_fraction(&scroll, 6, (uint8_t)(page + 1u), SCRY_SCENES);
    if (page == 0u)
        scry_city_content(&scroll, r, is_left);
    else if (page == 1u)
        scry_duel_content(&scroll, r, is_left);
    else
        scry_host_content(&scroll, r);
}

#undef SCRY_GLYPH

void duel_overlay_draw_attunement(duel_fb_t *fb, const duel_render_t *r,
                                  const duel_view_wizard_t *wz, bool is_left) {
    uint8_t local = DUEL_RENDER_LOCAL_LAYER(r->layer);
    if ((is_left && local != DUEL_RENDER_LOCAL_LEFT) ||
        (!is_left && local != DUEL_RENDER_LOCAL_RIGHT))
        return;
    int facing = is_left ? 1 : -1;
    bool casting = wz->life == LIFE_ACTIVE && wz->pose == POSE_CAST;
    int tip_x = 16 + facing * (casting ? 5 : 6);
    int tip_y = (casting ? 55 : 62) + DUEL_ROOF_DY;
    /* A stable three-pixel arc outside the staff/casting hand. */
    duel_fb_px(fb, tip_x + facing * 2, tip_y - 1, true);
    duel_fb_px(fb, tip_x + facing * 3, tip_y, true);
    duel_fb_px(fb, tip_x + facing * 2, tip_y + 1, true);
    int notches = local == DUEL_RENDER_LOCAL_RIGHT ? 2 : 1;
    for (int i = 0; i < notches; i++)
        duel_fb_px(fb, tip_x + facing * (1 + i), tip_y - 3, true);
    incantation_resident_draw_attunement(fb, is_left, DUEL_CIVIC_FLOOR(r->civic));
}

/* Diegetic HP: 2x2 lit windows stacked as the shaft's lower tier, two columns
 * (gapward x7-8, outer x3-4). The candidate constant selects four rows for
 * 8 HP or five rows for 10 HP. Each row fills gapward then outward,
 * bottom-up, so damage darkens the shaft from the top. Single source for the
 * clear, fill, and lost-window flash sites. */
static void hp_window_xy(int i, bool is_left, int *px, int *py) {
    int canonical_x = (i & 1) ? 3 : 7;
    *px = is_left ? canonical_x : DUEL_CANVAS_W - 2 - canonical_x;
    *py = 56 - (i / 2) * 4;
}

void duel_overlay_draw_health(duel_fb_t *fb, const duel_view_wizard_t *wz, bool is_left) {
    /* The health instrument owns its exact cells even while medics and
     * replacement silhouettes cross the away-side rooftop entrance. */
    int px, py;
    for (int i = 0; i < SIM_MAX_HP; i++) {
        hp_window_xy(i, is_left, &px, &py);
        for (int dy = 0; dy < 2; dy++) {
            duel_fb_px(fb, px, py + dy, false);
            duel_fb_px(fb, px + 1, py + dy, false);
        }
    }
    int hp = wz->hp > SIM_MAX_HP ? SIM_MAX_HP : wz->hp;
    for (int i = 0; i < hp; i++) {
        hp_window_xy(i, is_left, &px, &py);
        for (int dy = 0; dy < 2; dy++) {
            duel_fb_px(fb, px, py + dy, true);
            duel_fb_px(fb, px + 1, py + dy, true);
        }
    }
}

// One-shot local outcome flourishes (impact/fizzle/heal/shatter/deflect).
// All render-frame state: losing it costs only the flourish, never health or
// split convergence.
void duel_overlay_draw_local_fx(duel_fb_t *fb, const duel_render_t *r, const duel_view_wizard_t *wz,
                                int facing, bool is_left) {
    bool is_impact = r->flash_kind == FX_IMPACT_L || r->flash_kind == FX_IMPACT_R;
    bool is_fizzle = r->flash_kind == FX_FIZZLE_L || r->flash_kind == FX_FIZZLE_R;
    int tier = DUEL_KIND_TIER(r->flash_spell_kind);
    int fy = duel_combat_spell_lane_y(r->flash_spell_kind);

    if (is_impact) {
        // Force enters from the gap: contact burst, inward shock line,
        // local debris, recoil above, and a flashing frame at the shaft
        // window that just went dark. Only the defender's border corners twitch.
        // Presentation weight: the flourish scales one presentation tier up.
        if (tier < SPELL_TIER_SATURATED)
            tier++;
        int hx = 16 + facing * 5;
        int reach = 2 + tier + (r->flash_frames >= 8);
        for (int d = 0; d <= reach; d++)
            duel_fb_px(fb, hx + facing * d, fy, true);
        for (int d = 1; d <= reach; d++) {
            duel_fb_px(fb, hx, fy - d, true);
            duel_fb_px(fb, hx, fy + d, true);
        }
        duel_fb_line(fb, hx, fy, hx - facing * (3 + tier), fy - 3 - tier);
        duel_fb_line(fb, hx, fy, hx - facing * (2 + tier), fy + 4 + tier);
        duel_fb_px(fb, hx - facing * 6, fy - 8 - tier, true);
        duel_fb_px(fb, hx - facing * 4, fy + 9 + tier, true);
        if (tier >= SPELL_TIER_LONG) {
            duel_fb_px(fb, hx + facing * 2, fy - 7, true);
            duel_fb_px(fb, hx + facing * 3, fy + 7, true);
        }
        if (r->flash_frames >= 7) {
            for (int d = 0; d < 4; d++) {
                duel_fb_px(fb, d, 0, true);
                duel_fb_px(fb, DUEL_CANVAS_W - 1 - d, 0, true);
                duel_fb_px(fb, d, DUEL_CANVAS_H - 1, true);
                duel_fb_px(fb, DUEL_CANVAS_W - 1 - d, DUEL_CANVAS_H - 1, true);
            }
        }
        if (wz->hp < SIM_MAX_HP) {
            int px, py;
            hp_window_xy(wz->hp, is_left, &px, &py);
            duel_fb_px(fb, px - 1, py - 1, true);
            duel_fb_px(fb, px + 2, py - 1, true);
            duel_fb_px(fb, px - 1, py + 2, true);
            duel_fb_px(fb, px + 2, py + 2, true);
        }
    } else if (is_fizzle) {
        // Harmless dissipation stays away from the body and contracts from
        // a sparse outer shell into a tiny core. No border and no recoil.
        int fx = 16 + facing * 8;
        if (r->flash_frames >= 5) {
            int radius = 2 + (tier >= SPELL_TIER_LONG);
            duel_fb_px(fb, fx - radius, fy - radius, true);
            duel_fb_px(fb, fx + radius, fy - radius, true);
            duel_fb_px(fb, fx - radius, fy + radius, true);
            duel_fb_px(fb, fx + radius, fy + radius, true);
            duel_fb_px(fb, fx + facing * (radius + 1), fy, true);
        } else {
            duel_fb_px(fb, fx, fy, true);
            if (r->flash_frames >= 3) {
                duel_fb_px(fb, fx - 1, fy, true);
                duel_fb_px(fb, fx + 1, fy, true);
            }
        }
    } else if (r->flash_kind == FX_HEAL_L || r->flash_kind == FX_HEAL_R) {
        int hx = 16 - facing * 5;
        int radius = 2 + (r->flash_frames > 4u);
        duel_fb_px(fb, hx - radius, fy, true);
        duel_fb_px(fb, hx + radius, fy, true);
        duel_fb_px(fb, hx, fy - radius, true);
        duel_fb_px(fb, hx, fy + radius, true);
        duel_fb_line(fb, hx - 1, fy, hx + 1, fy);
        duel_fb_line(fb, hx, fy - 1, hx, fy + 1);
    } else if (r->flash_kind == FX_WARD_SHATTER_L || r->flash_kind == FX_WARD_SHATTER_R) {
        int ax = 16 + facing * 9;
        for (int i = 0; i < 4; i++) {
            int scatter = 2 + i * 2 + (8 - r->flash_frames) / 2;
            duel_fb_px(fb, ax + facing * scatter, fy - 6 + i * 4, true);
            duel_fb_px(fb, ax - facing * (scatter / 2), fy - 4 + i * 3, true);
        }
        duel_fb_line(fb, ax, fy - 7, ax - facing * 2, fy - 2);
        duel_fb_line(fb, ax - facing * 2, fy - 2, ax + facing, fy + 6);
    } else {
        // Redirection: the ward is the dominant thick shape while the
        // carrier breaks into two streaks thrown back toward the gap.
        int ax = 16 + facing * 9;
        int dist = 2 + (8 - r->flash_frames) / 2;
        duel_combat_draw_ward(fb, facing, 2, 2, false, fy);
        duel_fb_line(fb, ax + facing, fy, ax + facing * (dist + 2), fy - dist - tier);
        duel_fb_line(fb, ax + facing, fy, ax + facing * (dist + 1), fy + dist + tier);
        duel_fb_px(fb, ax - facing, fy - 5, true);
        duel_fb_px(fb, ax - facing, fy + 5, true);
        if (tier >= SPELL_TIER_LONG) {
            duel_fb_px(fb, ax + facing * (dist + 3), fy - 2, true);
            duel_fb_px(fb, ax + facing * (dist + 2), fy + 3, true);
        }
    }
}

/* Battlefield residue marks: the duel's session-scale history
 * sits on the rooftop deck directly under the spell lanes, at each zone's
 * battlefield position. Each canvas shows its own two zones (the other two
 * live across the gap). Element picks the mark's shape, intensity its
 * density; every pattern is horizontally symmetric, so the desk-mirror
 * contract holds without per-side flips. Void residue is the exception that
 * proves the deck is real: it eats a hole in the deck rows instead of adding
 * pixels. Anchor u values keep the marks clear of the crenellation teeth
 * (x28-29 / x2-3) and mirror exactly (13<->242, 48<->207). Wards and local
 * fx draw later and may transiently overlap — combat happens on top of its
 * own history. */
void duel_overlay_draw_residue(duel_fb_t *fb, const duel_render_t *r, bool is_left) {
    static const uint8_t zone_anchor_u[SIM_RESIDUE_ZONES] = {13u, 48u, 207u, 242u};
    for (uint8_t zone = 0; zone < SIM_RESIDUE_ZONES; zone++) {
        uint8_t intensity = DUEL_RENDER_RESIDUE_INTENSITY(r, zone);
        if (!intensity)
            continue;
        int x;
        if (!duel_combat_battlefield_to_x(zone_anchor_u[zone], is_left, &x))
            continue;
        int base = DUEL_DECK_Y0 - 1;
        switch (DUEL_RENDER_RESIDUE_ELEMENT(r, zone)) {
            case ELEM_FORCE: /* rubble mound spreading, then heaping */
                duel_fb_px(fb, x, base, true);
                if (intensity >= 2) {
                    duel_fb_px(fb, x - 1, base, true);
                    duel_fb_px(fb, x + 1, base, true);
                }
                if (intensity >= 3)
                    duel_fb_px(fb, x, base - 1, true);
                break;
            case ELEM_EMBER: /* flame column rising, then a glowing bed */
                duel_fb_px(fb, x, base, true);
                duel_fb_px(fb, x, base - 1, true);
                if (intensity >= 2)
                    duel_fb_px(fb, x, base - 2, true);
                if (intensity >= 3) {
                    duel_fb_px(fb, x - 1, base, true);
                    duel_fb_px(fb, x + 1, base, true);
                }
                break;
            case ELEM_FROST: /* twin shards growing, then a centre spire */
                duel_fb_px(fb, x - 1, base, true);
                duel_fb_px(fb, x + 1, base, true);
                if (intensity >= 2) {
                    duel_fb_px(fb, x - 1, base - 1, true);
                    duel_fb_px(fb, x + 1, base - 1, true);
                }
                if (intensity >= 3)
                    duel_fb_px(fb, x, base - 2, true);
                break;
            default: /* ELEM_VOID: a pit widening, then biting the beam */
                duel_fb_px(fb, x, DUEL_DECK_Y0, false);
                if (intensity >= 2) {
                    duel_fb_px(fb, x - 1, DUEL_DECK_Y0, false);
                    duel_fb_px(fb, x + 1, DUEL_DECK_Y0, false);
                }
                if (intensity >= 3)
                    duel_fb_px(fb, x, DUEL_FLOOR_BEAM_Y, false);
                break;
        }
    }
}

void duel_overlay_draw_fields(duel_fb_t *fb, const duel_render_t *r, bool is_left) {
    static const uint8_t zone_anchor_u[SIM_RESIDUE_ZONES] = {24u, 88u, 167u, 231u};
    for (uint8_t slot = 0; slot < SIM_FIELD_SLOTS; slot++) {
        uint8_t projection = r->field[slot];
        uint8_t kind = DUEL_FIELD_KIND(projection);
        if (kind == FIELD_NONE)
            continue;
        int x;
        if (!duel_combat_battlefield_to_x(zone_anchor_u[DUEL_FIELD_ZONE(projection)], is_left, &x))
            continue;
        uint8_t age = DUEL_FIELD_AGE(projection);
        switch (kind) {
            case FIELD_TRAP: /* toothed ground triangle */
                duel_fb_line(fb, x - 3, 59, x, 54);
                duel_fb_line(fb, x, 54, x + 3, 59);
                duel_fb_px(fb, x - 1, 58, true);
                duel_fb_px(fb, x + 1, 58, true);
                break;
            case FIELD_SINGULARITY: /* hollow gravity eye */
                duel_fb_px(fb, x - 3, 46, true);
                duel_fb_px(fb, x + 3, 46, true);
                duel_fb_hline(fb, x - 1, x + 1, 43);
                duel_fb_hline(fb, x - 1, x + 1, 49);
                if (age >= 2u)
                    duel_fb_px(fb, x, 46, true);
                break;
            case FIELD_STEAM: /* broad soft cloud */
                duel_fb_hline(fb, x - 3, x + 3, 48);
                duel_fb_px(fb, x - 2, 46, true);
                duel_fb_px(fb, x, 45, true);
                duel_fb_px(fb, x + 2, 46, true);
                break;
            case FIELD_RUNE: /* grounded diamond and stem */
                duel_fb_px(fb, x, 51, true);
                duel_fb_px(fb, x - 2, 54, true);
                duel_fb_px(fb, x + 2, 54, true);
                duel_fb_px(fb, x, 57, true);
                duel_fb_line(fb, x, 57, x, 60);
                break;
            case FIELD_FAMILIAR: /* winged carrier */
                duel_fb_line(fb, x - 4, 45, x, 48);
                duel_fb_line(fb, x, 48, x + 4, 45);
                duel_fb_px(fb, x, 46, true);
                duel_fb_px(fb, x + (DUEL_FIELD_OWNER(projection) ? -1 : 1), 49, true);
                break;
            case FIELD_WALL: /* unmistakable vertical barrier */
                for (int y = 42; y <= 57; y++)
                    duel_fb_px(fb, x, y, true);
                for (int y = 43; y <= 55; y += 4)
                    duel_fb_px(fb, x + ((y / 4) & 1 ? -1 : 1), y, true);
                break;
            case FIELD_VORTEX: /* rotating open spiral */
                duel_fb_line(fb, x - 3, 43, x + 3, 43);
                duel_fb_line(fb, x + 3, 43, x + 3, 49);
                duel_fb_line(fb, x + 3, 49, x - 2, 49);
                duel_fb_line(fb, x - 2, 49, x - 2, 46);
                duel_fb_px(fb, x, 46, true);
                break;
        }
        if (age == 3u)
            duel_fb_px(fb, x + (slot ? 4 : -4), 52, true);
    }
}
