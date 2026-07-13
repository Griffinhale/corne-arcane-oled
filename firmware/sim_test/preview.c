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
 *   ./preview --scenario <name>        M7.5 impact/deflect/fizzle/VOID/charge tableau
 *   ./preview --life <phase> [--ticks N]  right wizard mid-lifecycle (M5):
 *                                      phase = collapse|downed|medic|replace
 *   ./preview <file.trace> --tick N    run the sim, show the frame at tick N
 *   ./preview <file.trace> --play      animate the whole trace (~25 fps)
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "duel_draw.h"
#include "duel_sim.h"
#include "runner.h"
#include "trace.h"

#define GAP_COLS 8

static void emit_row_pair(const duel_fb_t *fb, int y) {
    for (int x = 0; x < DUEL_CANVAS_W; x++) {
        int top = duel_fb_get(fb, x, y);
        int bot = duel_fb_get(fb, x, y + 1);
        fputs(top ? (bot ? "█" : "▀") : (bot ? "▄" : " "), stdout);
    }
}

static void show(const duel_fb_t *left, const duel_fb_t *right) {
    for (int y = 0; y < DUEL_CANVAS_H; y += 2) {
        emit_row_pair(left, y);
        fputs("│", stdout);
        for (int i = 0; i < GAP_COLS - 2; i++) fputc(' ', stdout);
        fputs("│", stdout);
        emit_row_pair(right, y);
        fputc('\n', stdout);
    }
}

static void show_render(const duel_render_t *r) {
    duel_fb_t left, right;
    duel_fb_clear(&left);
    duel_fb_clear(&right);
    wiz_draw_scene(&left, r, true, 0, false);
    wiz_draw_scene(&right, r, false, 0, false);
    show(&left, &right);
}

static void show_world(const sim_world_t *w) {
    duel_render_t r = {.w = *w, .stale_link = false};
    show_render(&r);
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
            "       %s --scry [scene]\n"
            "       %s <file.trace> --tick N\n"
            "       %s <file.trace> --play\n",
            argv0, argv0, argv0, argv0, argv0, argv0, argv0);
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
    const char *scenario = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            scenario = argv[++i];
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

    if (scenario) {
        sim_world_t w;
        sim_init(&w, SIMF_AUTHORITATIVE, 0);
        duel_render_t r = {.w = w};
        uint8_t medium_force = DUEL_KIND_WITH_TIER(DUEL_KIND_PACK(ELEM_FORCE, MOD_NONE, PAY_IMPACT), SPELL_TIER_MEDIUM);
        if (strcmp(scenario, "impact") == 0) {
            r.w.wiz[SIM_SIDE_R].hp = SIM_MAX_HP - 1;
            r.flash_frames = 10; r.flash_kind = FX_IMPACT_R; r.flash_spell_kind = medium_force;
        } else if (strcmp(scenario, "deflect") == 0) {
            r.w.wiz[SIM_SIDE_R].shield_ticks = SIM_SHIELD_TICKS;
            r.flash_frames = 7; r.flash_kind = FX_DEFLECT_R; r.flash_spell_kind = medium_force;
        } else if (strcmp(scenario, "fizzle") == 0) {
            r.w.wiz[SIM_SIDE_R].life = LIFE_DOWNED;
            r.w.wiz[SIM_SIDE_R].life_ticks = SIM_DOWNED_TICKS / 2;
            r.w.wiz[SIM_SIDE_R].hp = 0;
            r.flash_frames = 7; r.flash_kind = FX_FIZZLE_R; r.flash_spell_kind = medium_force;
        } else if (strcmp(scenario, "void-pierce") == 0) {
            r.w.wiz[SIM_SIDE_R].shield_ticks = SIM_SHIELD_TICKS;
            r.w.spell[SIM_SIDE_L] = (sim_spell_t){
                .active = 1, .pos = 236, .dir = 4,
                .kind = DUEL_KIND_WITH_TIER(DUEL_KIND_PACK(ELEM_VOID, MOD_NONE, PAY_IMPACT), SPELL_TIER_LONG),
            };
        } else if (strcmp(scenario, "short-cast") == 0 || strcmp(scenario, "long-cast") == 0) {
            r.w.wiz[SIM_SIDE_L].pose = POSE_CAST;
            r.w.wiz[SIM_SIDE_L].cast_windup = 2;
            r.w.wiz[SIM_SIDE_L].cast_tier = strcmp(scenario, "short-cast") == 0 ?
                                                   SPELL_TIER_SHORT : SPELL_TIER_LONG;
        } else {
            return usage(argv[0]);
        }
        show_render(&r);
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
        duel_render_t r = {.w = w, .overlay_layer = 3, .overlay_host = 0, .overlay_notif = 0};
        show_render(&r);
        return 0;
    }

    if (spell_kind >= 0) {
        sim_world_t w;
        sim_init(&w, SIMF_AUTHORITATIVE, 0);
        w.spell[SIM_SIDE_L] = (sim_spell_t){.active = 1, .pos = 40, .dir = +4, .kind = (uint8_t)spell_kind};
        w.spell[SIM_SIDE_R] = (sim_spell_t){.active = 1, .pos = 210, .dir = -4, .kind = (uint8_t)spell_kind};
        show_world(&w);
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
        show_world(&w);
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
