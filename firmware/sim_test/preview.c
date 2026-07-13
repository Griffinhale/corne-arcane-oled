/*
 * preview.c — terminal previewer for the two portrait OLED canvases.
 *
 * Renders both 32x128 framebuffers side by side with a marked centre gap,
 * using half-block characters (2 vertical pixels per char -> ~72 cols x 64
 * rows). Uses exactly the drawing code the firmware blits.
 *
 *   ./preview                          both wizards idle
 *   ./preview --cast L|R|both          force cast pose(s)
 *   ./preview --variant N              roster cosmetic 0..3 on both wizards
 *   ./preview --scry [scene]           M7 scrying overlay open (scene 0..2)
 *   ./preview --host-scene <name>      online host scene: duel|archive|focus
 *   ./preview --scenario <name>        M7.5 impact/deflect/fizzle/VOID/charge tableau
 *   ./preview --life <phase> [--ticks N]  right wizard mid-lifecycle (M5):
 *                                      phase = collapse|downed|medic|replace
 *   ./preview <file.trace> --tick N    run the sim, show the frame at tick N
 *   ./preview <file.trace> --play      animate the whole trace (~25 fps)
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "duel_draw.h"
#include "duel_host.h"
#include "duel_sim.h"
#include "runner.h"
#include "scenarios.h"
#include "trace.h"

#define GAP_COLS 8

typedef enum { SHOW_BOTH, SHOW_LEFT, SHOW_RIGHT } show_side_t;
static show_side_t output_side = SHOW_BOTH;
static int output_gap = GAP_COLS;
static uint32_t output_frame;

static void emit_row_pair(const duel_fb_t *fb, int y) {
    for (int x = 0; x < DUEL_CANVAS_W; x++) {
        int top = duel_fb_get(fb, x, y);
        int bot = duel_fb_get(fb, x, y + 1);
        fputs(top ? (bot ? "█" : "▀") : (bot ? "▄" : " "), stdout);
    }
}

static void show(const duel_fb_t *left, const duel_fb_t *right) {
    for (int y = 0; y < DUEL_CANVAS_H; y += 2) {
        if (output_side != SHOW_RIGHT) emit_row_pair(left, y);
        if (output_side == SHOW_BOTH) {
            if (output_gap >= 2) {
                fputs("│", stdout);
                for (int i = 0; i < output_gap - 2; i++) fputc(' ', stdout);
                fputs("│", stdout);
            }
        }
        if (output_side != SHOW_LEFT) emit_row_pair(right, y);
        fputc('\n', stdout);
    }
}

static void show_render(const duel_render_t *r) {
    duel_fb_t left, right;
    duel_fb_clear(&left);
    duel_fb_clear(&right);
    wiz_draw_scene(&left, r, true, output_frame, false);
    wiz_draw_scene(&right, r, false, output_frame, false);
    show(&left, &right);
}

static int pbm_write(const char *path, const duel_fb_t *left, const duel_fb_t *right) {
    enum { SCALE = 4, GAP = 8 };
    const int source_w = DUEL_CANVAS_W * 2 + GAP;
    const int width = source_w * SCALE;
    const int height = DUEL_CANVAS_H * SCALE;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P4\n%d %d\n", width, height);
    for (int sy = 0; sy < DUEL_CANVAS_H; sy++) {
        for (int yy = 0; yy < SCALE; yy++) {
            uint8_t byte = 0;
            int bit = 7;
            for (int sx = 0; sx < source_w; sx++) {
                bool on = sx < DUEL_CANVAS_W ? duel_fb_get(left, sx, sy)
                        : sx >= DUEL_CANVAS_W + GAP ? duel_fb_get(right, sx - DUEL_CANVAS_W - GAP, sy)
                                                    : false;
                for (int xx = 0; xx < SCALE; xx++) {
                    if (on) byte |= (uint8_t)(1u << bit);
                    if (--bit < 0) { fputc(byte, f); byte = 0; bit = 7; }
                }
            }
            if (bit != 7) fputc(byte, f);
        }
    }
    return fclose(f);
}

static int write_gallery(const char *dir) {
    if (mkdir(dir, 0775) != 0 && errno != EEXIST) {
        perror(dir);
        return 1;
    }
    char html_path[512];
    if (snprintf(html_path, sizeof html_path, "%s/index.html", dir) >= (int)sizeof html_path) return 1;
    FILE *html = fopen(html_path, "w");
    if (!html) { perror(html_path); return 1; }
    fputs("<!doctype html><meta charset=\"utf-8\"><title>Corne Arcane M11 gallery</title>"
          "<style>body{margin:24px;background:#171411;color:#eee7d8;font:14px system-ui}"
          "h1{font:600 28px Georgia,serif}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(310px,1fr));gap:18px}"
          "figure{margin:0;padding:14px;background:#24201b;border:1px solid #514839}"
          "img{width:288px;max-width:100%;height:auto;image-rendering:pixelated;background:#090807;filter:invert(1)}"
          "figcaption{margin-top:8px}.group{color:#bda77e;font-size:12px;text-transform:uppercase}</style>"
          "<h1>M11 Living Grimoire — canonical frames</h1><div class=\"grid\">", html);
    for (size_t i = 0; i < duel_scenario_count(); i++) {
        const duel_scenario_t *s = duel_scenario_at(i);
        duel_fb_t left, right;
        duel_scenario_render(s, s->frame, &left, &right);
        char path[512];
        if (snprintf(path, sizeof path, "%s/%s.pbm", dir, s->name) >= (int)sizeof path ||
            pbm_write(path, &left, &right) != 0) {
            fclose(html); perror(path); return 1;
        }
        fprintf(html, "<figure><img src=\"%s.pbm\" alt=\"%s\"><figcaption><span class=\"group\">%s</span><br><b>%s</b> — %s</figcaption></figure>",
                s->name, s->name, s->group, s->name, s->description);
    }
    fputs("</div>", html);
    if (fclose(html) != 0) return 1;
    printf("wrote %zu canonical frames and %s\n", duel_scenario_count(), html_path);
    return 0;
}

typedef struct {
    uint8_t seen_fx_seq;
    uint8_t flash_frames;
    uint8_t flash_kind;
    uint8_t flash_spell_kind;
    uint8_t last_spell_kind[2];
} preview_fx_t;

// Mirror keymap.c's render-only outcome latch so trace playback shows the same
// revised effects as hardware. One trace tick is presented as one render frame.
static void show_presented_world(preview_fx_t *p, const sim_world_t *w) {
    for (int s = 0; s < 2; s++) {
        if (w->spell[s].active) p->last_spell_kind[s] = w->spell[s].kind;
    }
    if (w->fx_seq != p->seen_fx_seq) {
        p->seen_fx_seq = w->fx_seq;
        p->flash_kind  = w->fx_kind;
        bool defender_left = p->flash_kind == FX_IMPACT_L || p->flash_kind == FX_DEFLECT_L ||
                             p->flash_kind == FX_FIZZLE_L;
        p->flash_spell_kind = p->last_spell_kind[defender_left ? SIM_SIDE_R : SIM_SIDE_L];
        bool impact = p->flash_kind == FX_IMPACT_L || p->flash_kind == FX_IMPACT_R;
        p->flash_frames = impact ? 12 : 8;
    } else if (p->flash_frames) {
        p->flash_frames--;
    }
    duel_render_t r = {
        .w = *w,
        .flash_frames = p->flash_frames,
        .flash_kind = p->flash_kind,
        .flash_spell_kind = p->flash_spell_kind,
    };
    show_render(&r);
}

static int usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s [--cast L|R|both] [--variant N]\n"
            "       %s --life collapse|downed|medic|replace [--ticks N]\n"
            "       %s --spell-kind <force|ember|frost|void>/<none|swift|heavy>\n"
            "                         [/short|medium|long|saturated]\n"
            "       %s --scenario impact|deflect|fizzle|void-pierce|short-cast|long-cast\n"
            "                     |archive-idle|archive-pulse|archive-cast|archive-impact\n"
            "                     |archive-ko|archive-scry|terminal-completion\n"
            "                     |aggregated-normal|persistent-critical|aged-alert\n"
            "                     |alert-under-scry\n"
            "       %s --host-scene duel|archive|focus\n"
            "       %s --scry [scene]\n"
            "       %s --scenario NAME [--frame N] [--side left|right|both] [--gap N]\n"
            "       %s --list-scenarios\n"
            "       %s --gallery DIR\n"
            "       %s <file.trace> --tick N\n"
            "       %s <file.trace> --play\n",
            argv0, argv0, argv0, argv0, argv0, argv0, argv0, argv0, argv0, argv0, argv0);
    return 2;
}

int main(int argc, char **argv) {
    // Trace-driven modes.
    if (argc >= 3 && argv[1][0] != '-') {
        trace_t t;
        if (trace_load(argv[1], &t) != 0) return 1;
        runner_t r;
        runner_init(&r, &t, SIMF_AUTHORITATIVE);
        preview_fx_t present = {0};

        if (strcmp(argv[2], "--play") == 0) {
            const struct timespec frame_time = {0, SIM_TICK_MS * 1000000L};
            while (!runner_done(&r)) {
                runner_step(&r);
                fputs("\033[H\033[2J", stdout);
                printf("tick %u/%u\n", r.ticks_run, t.end_tick - t.start_tick);
                show_presented_world(&present, &r.w);
                fflush(stdout);
                nanosleep(&frame_time, NULL);
            }
            return 0;
        }
        if (strcmp(argv[2], "--tick") == 0 && argc == 4) {
            uint32_t target;
            if (sscanf(argv[3], "%u", &target) != 1) return usage(argv[0]);
            while (!runner_done(&r) && r.ticks_run < target) {
                runner_step(&r);
                // Advance the render-only latch at the same nominal cadence;
                // only the target frame is emitted below.
                for (int s = 0; s < 2; s++) {
                    if (r.w.spell[s].active) present.last_spell_kind[s] = r.w.spell[s].kind;
                }
                if (r.w.fx_seq != present.seen_fx_seq) {
                    present.seen_fx_seq = r.w.fx_seq;
                    present.flash_kind = r.w.fx_kind;
                    bool dl = present.flash_kind == FX_IMPACT_L || present.flash_kind == FX_DEFLECT_L ||
                              present.flash_kind == FX_FIZZLE_L;
                    present.flash_spell_kind = present.last_spell_kind[dl ? SIM_SIDE_R : SIM_SIDE_L];
                    present.flash_frames = (present.flash_kind == FX_IMPACT_L || present.flash_kind == FX_IMPACT_R) ? 12 : 8;
                } else if (present.flash_frames) {
                    present.flash_frames--;
                }
            }
            printf("tick %u\n", r.ticks_run);
            show_render(&(duel_render_t){.w = r.w, .flash_frames = present.flash_frames,
                                         .flash_kind = present.flash_kind,
                                         .flash_spell_kind = present.flash_spell_kind});
            return 0;
        }
        return usage(argv[0]);
    }

    // Static mode.
    bool cast_l = false, cast_r = false;
    unsigned variant = 0;
    int      life    = -1;   // LIFE_* when --life given
    int      ticks   = -1;   // --ticks override, else the phase midpoint
    int      spell_kind = -1;
    int      scry_scene = -1; // >= 0 when --scry given
    int      host_scene = -1; // >= 0 means online external context
    const char *scenario = NULL;
    const char *gallery = NULL;
    bool list_scenarios = false;
    bool frame_set = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            scenario = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--frame") == 0 && i + 1 < argc) {
            if (sscanf(argv[++i], "%u", &output_frame) != 1) return usage(argv[0]);
            frame_set = true;
            continue;
        }
        if (strcmp(argv[i], "--side") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "left") == 0) output_side = SHOW_LEFT;
            else if (strcmp(argv[i], "right") == 0) output_side = SHOW_RIGHT;
            else if (strcmp(argv[i], "both") == 0) output_side = SHOW_BOTH;
            else return usage(argv[0]);
            continue;
        }
        if (strcmp(argv[i], "--gap") == 0 && i + 1 < argc) {
            if (sscanf(argv[++i], "%d", &output_gap) != 1 || output_gap < 0 || output_gap > 80) return usage(argv[0]);
            continue;
        }
        if (strcmp(argv[i], "--gallery") == 0 && i + 1 < argc) {
            gallery = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--list-scenarios") == 0) {
            list_scenarios = true;
            continue;
        }
        if (strcmp(argv[i], "--host-scene") == 0 && i + 1 < argc) {
            i++;
            if      (strcmp(argv[i], "duel") == 0)    host_scene = DUEL_HOST_SCENE_DUEL;
            else if (strcmp(argv[i], "archive") == 0) host_scene = DUEL_HOST_SCENE_ARCHIVE;
            else if (strcmp(argv[i], "focus") == 0)   host_scene = DUEL_HOST_SCENE_FOCUS;
            else return usage(argv[0]);
            continue;
        }
        if (strcmp(argv[i], "--scry") == 0) {
            scry_scene = 0;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                i++;
                if (sscanf(argv[i], "%d", &scry_scene) != 1 || scry_scene < 0 || scry_scene >= SCRY_SCENES) return usage(argv[0]);
            }
            continue;
        }
        if (strcmp(argv[i], "--cast") == 0 && i + 1 < argc) {
            i++;
            if      (strcmp(argv[i], "L") == 0) cast_l = true;
            else if (strcmp(argv[i], "R") == 0) cast_r = true;
            else if (strcmp(argv[i], "both") == 0) cast_l = cast_r = true;
            else return usage(argv[0]);
        } else if (strcmp(argv[i], "--variant") == 0 && i + 1 < argc) {
            i++;
            if (sscanf(argv[i], "%u", &variant) != 1 || variant >= SIM_ROSTER_N) return usage(argv[0]);
        } else if (strcmp(argv[i], "--life") == 0 && i + 1 < argc) {
            i++;
            if      (strcmp(argv[i], "collapse") == 0) life = LIFE_COLLAPSE;
            else if (strcmp(argv[i], "downed") == 0)   life = LIFE_DOWNED;
            else if (strcmp(argv[i], "medic") == 0)    life = LIFE_MEDIC;
            else if (strcmp(argv[i], "replace") == 0)  life = LIFE_REPLACE;
            else return usage(argv[0]);
        } else if (strcmp(argv[i], "--ticks") == 0 && i + 1 < argc) {
            i++;
            if (sscanf(argv[i], "%d", &ticks) != 1 || ticks < 1) return usage(argv[0]);
        } else if (strcmp(argv[i], "--spell-kind") == 0 && i + 1 < argc) {
            char element[16], modifier[16], tier_name[16] = "medium";
            int elem, mod, tier;
            i++;
            int fields = sscanf(argv[i], "%15[^/]/%15[^/]/%15s", element, modifier, tier_name);
            if (fields < 2) return usage(argv[0]);
            if      (strcmp(element, "force") == 0) elem = ELEM_FORCE;
            else if (strcmp(element, "ember") == 0) elem = ELEM_EMBER;
            else if (strcmp(element, "frost") == 0) elem = ELEM_FROST;
            else if (strcmp(element, "void") == 0)  elem = ELEM_VOID;
            else return usage(argv[0]);
            if      (strcmp(modifier, "none") == 0)  mod = MOD_NONE;
            else if (strcmp(modifier, "swift") == 0) mod = MOD_SWIFT;
            else if (strcmp(modifier, "heavy") == 0) mod = MOD_HEAVY;
            else return usage(argv[0]);
            if      (strcmp(tier_name, "short") == 0)     tier = SPELL_TIER_SHORT;
            else if (strcmp(tier_name, "medium") == 0)    tier = SPELL_TIER_MEDIUM;
            else if (strcmp(tier_name, "long") == 0)      tier = SPELL_TIER_LONG;
            else if (strcmp(tier_name, "saturated") == 0) tier = SPELL_TIER_SATURATED;
            else return usage(argv[0]);
            spell_kind = DUEL_KIND_WITH_TIER(DUEL_KIND_PACK(elem, mod, PAY_IMPACT), tier);
        } else {
            return usage(argv[0]);
        }
    }

    if (list_scenarios) {
        for (size_t i = 0; i < duel_scenario_count(); i++) {
            const duel_scenario_t *s = duel_scenario_at(i);
            printf("%-24s %-12s %s\n", s->name, s->group, s->description);
        }
        return 0;
    }
    if (gallery) return write_gallery(gallery);

    if (scenario) {
        const duel_scenario_t *canonical = duel_scenario_find(scenario);
        if (!canonical) return usage(argv[0]);
        if (!frame_set) output_frame = canonical->frame;
        duel_fb_t left, right;
        duel_scenario_render(canonical, output_frame, &left, &right);
        show(&left, &right);
        return 0;
    }

    if (scry_scene >= 0) {
        // Overlay open above an ordinary duel: left casting, right warding, so
        // the still-running world is visible under the panel. Host offline and
        // no notifications match the M7 (pre-M8) stubs; layer 3 is the chord.
        sim_world_t w;
        sim_init(&w, SIMF_AUTHORITATIVE, 0);
        w.scry.state           = SCRY_ACTIVE;
        w.scry.scene           = (uint8_t)scry_scene;
        w.wiz[SIM_SIDE_L].pose  = POSE_CAST;
        w.wiz[SIM_SIDE_R].shield_ticks = SIM_SHIELD_TICKS;
        duel_render_t r = {.w = w, .overlay_layer = 3,
                           .overlay_host = host_scene >= 0,
                           .overlay_scene = host_scene >= 0 ? (uint8_t)host_scene : 0,
                           .overlay_notif = 0};
        show_render(&r);
        return 0;
    }

    if (spell_kind >= 0) {
        sim_world_t w;
        sim_init(&w, SIMF_AUTHORITATIVE, 0);
        w.spell[SIM_SIDE_L] = (sim_spell_t){.active = 1, .pos = 40, .dir = +4, .kind = (uint8_t)spell_kind};
        w.spell[SIM_SIDE_R] = (sim_spell_t){.active = 1, .pos = 210, .dir = -4, .kind = (uint8_t)spell_kind};
        duel_render_t r = {.w = w, .overlay_host = host_scene >= 0,
                           .overlay_scene = host_scene >= 0 ? (uint8_t)host_scene : 0};
        show_render(&r);
        return 0;
    }

    if (life >= 0) {
        // Hand-built lifecycle tableau: right wizard mid-phase, left active.
        int midpoint = life == LIFE_COLLAPSE ? SIM_COLLAPSE_TICKS / 2
                     : life == LIFE_DOWNED   ? SIM_DOWNED_TICKS / 2
                     : life == LIFE_MEDIC    ? SIM_MEDIC_TICKS / 2
                                             : SIM_REPLACE_TICKS / 2;
        sim_world_t w;
        sim_init(&w, SIMF_AUTHORITATIVE, 0);
        w.wiz[SIM_SIDE_R].life       = (uint8_t)life;
        w.wiz[SIM_SIDE_R].life_ticks = (uint8_t)(ticks >= 0 ? ticks : midpoint);
        w.wiz[SIM_SIDE_R].hp         = 0;
        // The sim bumps the variant on entering REPLACE; mimic that here.
        if (life == LIFE_REPLACE) w.wiz[SIM_SIDE_R].variant = 1;
        duel_render_t r = {.w = w, .overlay_host = host_scene >= 0,
                           .overlay_scene = host_scene >= 0 ? (uint8_t)host_scene : 0};
        show_render(&r);
        return 0;
    }

    if (host_scene >= 0) {
        sim_world_t w;
        sim_init(&w, SIMF_AUTHORITATIVE, 0);
        w.wiz[SIM_SIDE_L].pose = cast_l ? POSE_CAST : POSE_IDLE;
        w.wiz[SIM_SIDE_R].pose = cast_r ? POSE_CAST : POSE_IDLE;
        w.wiz[SIM_SIDE_L].variant = (uint8_t)variant;
        w.wiz[SIM_SIDE_R].variant = (uint8_t)variant;
        duel_render_t r = {.w = w, .overlay_host = 1, .overlay_scene = (uint8_t)host_scene};
        show_render(&r);
        return 0;
    }

    duel_fb_t left, right;
    duel_fb_clear(&left);
    duel_fb_clear(&right);
    wiz_draw(&left, cast_l, +1, (uint8_t)variant); // facing the gap: +1 on the left half
    wiz_draw(&right, cast_r, -1, (uint8_t)variant);
    show(&left, &right);
    return 0;
}
