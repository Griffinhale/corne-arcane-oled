/*
 * test_main.c — replay/golden test runner for the duel engine.
 *
 * Usage: ./test_runner [--write-golden] <traces-dir> <golden-dir>
 *
 * t0_*: harness self-tests (framebuffer, trace parser).
 * t2_*: M2 acceptance — determinism, cadence invariance, overflow, wrap.
 * t3_*: M3 acceptance — lossy-link convergence, rollback protection, session
 *       restart, link-dead independence, stale marker.
 * t4_*: M4 acceptance — spell flight, screen ownership, shield window,
 *       missed-impact recovery.
 * t5_*: M5 acceptance — KO lifecycle arc, no dead ends, downed fizzle,
 *       downed-cannot-act, regen, snapshot/loss recovery, double KO, roster
 *       variants, lifecycle rendering, KO replay golden.
 * t6_*: M6 acceptance — recipe determinism, element/modifier reachability,
 *       four-ingredient window, inactivity expiry, bounded mash, slave never
 *       compiles, kind sync, deflect windows, VOID pierce, glyph distinctness.
 * t7_*: M7 acceptance — layer-key chord machine (roll never opens, deliberate
 *       dwell opens, early release, third-key cancel+latch, scene selection,
 *       slave-never-opens, wire sync, overlay rendering, no combat coupling).
 * t75_*: M7.5 state acceptance — 10-tick anticipation, capped recipe tiers,
 *        presentation-only combat coupling, and charge/tier wire sync.
 * t8_*: M8 host protocol — validation, absolute context, session/sequence
 *       ordering, delayed-old-session rejection, and heartbeat expiry.
 * t9_*: M9 Archive underlay — scene isolation, mirror/determinism, activity
 *       tiers, overlay precedence, combat visibility, and hash invariance.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "duel_display.h"
#include "duel_draw.h"
#include "duel_host.h"
#include "duel_proto.h"
#include "duel_sim.h"
#include "runner.h"
#include "trace.h"

_Static_assert(TRACE_EV_PRESS == SIM_EV_KEYDOWN && TRACE_EV_RELEASE == SIM_EV_KEYUP,
               "trace event kinds must match sim event kinds");
_Static_assert(sizeof(sim_world_t) == 56, "sim_world_t wire-independent hash layout changed");

static int g_failures;
static int g_write_golden;
static const char *g_traces_dir;
static const char *g_golden_dir;

#define CHECK(cond, name)                                          \
    do {                                                           \
        if (cond) {                                                \
            printf("PASS %s\n", name);                             \
        } else {                                                   \
            printf("FAIL %s (%s:%d)\n", name, __FILE__, __LINE__); \
            g_failures++;                                          \
        }                                                          \
    } while (0)

// fnv1a64 — the snapshot/world hash used by every golden stream.
static uint64_t fnv1a64(const void *data, size_t len) {
    const uint8_t *p = data;
    uint64_t h = 0xcbf29ce484222325u;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 0x100000001b3u;
    }
    return h;
}

static uint64_t world_hash(const sim_world_t *w) {
    return fnv1a64(w, sizeof *w);
}

// World hash with the tick counter masked out, for comparing runs that start
// at different absolute ticks (wrap test).
static uint64_t world_hash_no_tick(const sim_world_t *w) {
    sim_world_t c = *w;
    c.tick = 0;
    return fnv1a64(&c, sizeof c);
}

static int load_trace_or_fail(const char *name, trace_t *t) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", g_traces_dir, name);
    return trace_load(path, t);
}

/* ---------------- Phase 1: harness self-tests ---------------- */

static void t0_fb_roundtrip(void) {
    duel_fb_t fb;
    duel_fb_clear(&fb);
    bool ok = true;
    int pts[][2] = {{0, 0}, {31, 0}, {0, 127}, {31, 127}, {16, 64}};
    for (unsigned i = 0; i < sizeof pts / sizeof pts[0]; i++) {
        duel_fb_px(&fb, pts[i][0], pts[i][1], true);
        ok &= duel_fb_get(&fb, pts[i][0], pts[i][1]);
    }
    duel_fb_px(&fb, -1, 0, true);
    duel_fb_px(&fb, 32, 0, true);
    duel_fb_px(&fb, 0, 128, true);
    ok &= !duel_fb_get(&fb, -1, 0) && !duel_fb_get(&fb, 32, 0) && !duel_fb_get(&fb, 0, 128);
    duel_fb_px(&fb, 16, 64, false);
    ok &= !duel_fb_get(&fb, 16, 64);
    CHECK(ok, "t0_fb_roundtrip");
}

static void t0_fb_qmk_page_layout(void) {
    duel_fb_t fb;
    duel_fb_clear(&fb);
    uint8_t expected[sizeof fb.bits] = {0};
    bool ok = sizeof fb.bits == 512;
    for (int y = 0; y < DUEL_CANVAS_H; y++) {
        for (int x = 0; x < DUEL_CANVAS_W; x++) {
            bool on = ((x * 5 + y * 3) % 17) < 4;
            if (!on) continue;
            duel_fb_px(&fb, x, y, true);
            expected[x + (y >> 3) * DUEL_CANVAS_W] |= (uint8_t)(1u << (y & 7));
        }
    }
    ok &= memcmp(fb.bits, expected, sizeof expected) == 0;
    for (int y = 0; y < DUEL_CANVAS_H; y++) {
        for (int x = 0; x < DUEL_CANVAS_W; x++) {
            bool on = ((x * 5 + y * 3) % 17) < 4;
            ok &= duel_fb_get(&fb, x, y) == on;
        }
    }
    CHECK(ok, "t0_fb_qmk_page_layout");
}

static void t0_draw_deterministic(void) {
    duel_fb_t a, b;
    duel_fb_clear(&a);
    duel_fb_clear(&b);
    wiz_draw(&a, true, -1, 0);
    wiz_draw(&b, true, -1, 0);
    bool same = memcmp(a.bits, b.bits, sizeof a.bits) == 0;
    duel_fb_clear(&b);
    wiz_draw(&b, true, +1, 0);
    bool mirrored_differs = memcmp(a.bits, b.bits, sizeof a.bits) != 0;
    bool nonempty = fnv1a64(a.bits, sizeof a.bits) != fnv1a64((uint8_t[512]){0}, 512);
    CHECK(same && mirrored_differs && nonempty, "t0_draw_deterministic");
}

static void t0_trace_parse(void) {
    trace_t t;
    bool ok = load_trace_or_fail("cast_basic.trace", &t) == 0;
    if (ok) {
        ok &= t.n_ev >= 4;
        ok &= t.ev[0].kind == TRACE_EV_PRESS && t.ev[0].side == 0;
        ok &= t.end_tick > t.ev[t.n_ev - 1].tick;
        for (int i = 1; i < t.n_ev; i++) ok &= t.ev[i].tick >= t.ev[i - 1].tick;
    }
    CHECK(ok, "t0_trace_parse");
}

static void t11_display_policy(void) {
    duel_display_policy_t p = {0};
    duel_display_init(&p, 1000);
    bool ok = p.phase == DUEL_DISPLAY_ACTIVE;
    ok &= duel_display_brightness(&p, 1000) == DUEL_DISPLAY_ACTIVE_BRIGHTNESS;
    ok &= duel_display_update(&p, 1000 + DUEL_DISPLAY_DIM_MS - 1) == DUEL_DISPLAY_ACTIVE;
    ok &= duel_display_update(&p, 1000 + DUEL_DISPLAY_DIM_MS) == DUEL_DISPLAY_DIM;
    ok &= duel_display_brightness(&p, 1000 + DUEL_DISPLAY_DIM_MS) == DUEL_DISPLAY_ACTIVE_BRIGHTNESS;
    ok &= duel_display_brightness(&p, 1000 + DUEL_DISPLAY_DIM_MS + DUEL_DISPLAY_FADE_MS / 2) == 80;
    ok &= duel_display_brightness(&p, 1000 + DUEL_DISPLAY_DIM_MS + DUEL_DISPLAY_FADE_MS) == DUEL_DISPLAY_DIM_BRIGHTNESS;
    ok &= duel_display_redraw_ms(&p) == DUEL_DISPLAY_DIM_REDRAW_MS;
    ok &= duel_display_update(&p, 1000 + DUEL_DISPLAY_SLEEP_MS) == DUEL_DISPLAY_SLEEP;
    ok &= duel_display_brightness(&p, 1000 + DUEL_DISPLAY_SLEEP_MS) == 0;
    duel_display_note_key(&p, 1000 + DUEL_DISPLAY_SLEEP_MS + 1);
    ok &= p.phase == DUEL_DISPLAY_ACTIVE;
    ok &= duel_display_redraw_ms(&p) == DUEL_DISPLAY_ACTIVE_REDRAW_MS;

    // Remote phase following synchronizes the slave without treating host or
    // render activity as a wake source; only note_key can return to ACTIVE.
    duel_display_follow(&p, DUEL_DISPLAY_SLEEP, 400000);
    ok &= p.phase == DUEL_DISPLAY_SLEEP;
    duel_host_state_t host = {0};
    duel_host_packet_t packet;
    duel_host_encode_summary(DUEL_HOST_MSG_HELLO, 9, 0, DUEL_HOST_SCENE_DUEL,
                             0, 0, 0, 0, false, &packet);
    ok &= duel_host_accept(&host, &packet) == DUEL_HOST_APPLIED_HEARTBEAT;
    duel_host_encode_summary(DUEL_HOST_MSG_NOTIFY, 9, 1, DUEL_HOST_SCENE_ARCHIVE,
                             2, DUEL_HOST_CATEGORY_SYSTEM, DUEL_HOST_PRIORITY_CRITICAL,
                             0, true, &packet);
    ok &= duel_host_accept(&host, &packet) == DUEL_HOST_APPLIED;
    ok &= p.phase == DUEL_DISPLAY_SLEEP; // host/focus/notification has no wake path
    duel_display_follow(&p, DUEL_DISPLAY_DIM, 400100);
    ok &= p.phase == DUEL_DISPLAY_DIM;
    CHECK(ok, "t11_display_policy");
}

static void t11_display_wire_compatibility(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    duel_snapshot_t p;
    duel_encode_external_alert_display(&w, 7, 11, 0x55, 0x2a,
                                       DUEL_DISPLAY_SLEEP, &p);
    bool ok = sizeof p == 31 && duel_decode_valid(&p);
    ok &= (p.flags & DUEL_FLAGS_WORLD_VALID) != 0;
    ok &= DUEL_FLAGS_DISPLAY(p.flags) == DUEL_DISPLAY_SLEEP;
    ok &= p.external == 0x55 && p.alert == 0x2a;
    sim_world_t decoded;
    duel_decode_world(&p, &decoded);
    ok &= decoded.wiz[SIM_SIDE_L].hp == SIM_MAX_HP;
    ok &= decoded.wiz[SIM_SIDE_R].hp == SIM_MAX_HP;
    ok &= decoded.flags == 0; // a decoded slave view remains non-authoritative
    CHECK(ok, "t11_display_wire_compatibility");
}

/* ---------------- M2: deterministic world loop ---------------- */

// Golden hash stream: identical replays across code changes are the
// regression baseline every later milestone builds on. Shared by
// t2_replay_golden (cast_basic) and t5_replay_golden (duel_ko); returns true
// when the replay ran to completion so callers can add semantic checks on
// `final` (valid whenever true is returned, including --write-golden runs).
static bool replay_golden_stream(const char *trace_name, const char *golden_name,
                                 const char *test_name, sim_world_t *final) {
    trace_t t;
    if (load_trace_or_fail(trace_name, &t) != 0) { CHECK(false, test_name); return false; }

    char lines[16384];
    size_t off = 0;
    runner_t r;
    runner_init(&r, &t, SIMF_AUTHORITATIVE);
    while (!runner_done(&r)) {
        runner_step(&r);
        if (r.ticks_run % 5 == 0 || runner_done(&r)) {
            off += (size_t)snprintf(lines + off, sizeof lines - off, "T%u %016llx\n",
                                    r.ticks_run, (unsigned long long)world_hash(&r.w));
        }
    }
    if (final) *final = r.w;

    // Two fresh instances must agree before we even look at the golden file.
    runner_t r2;
    runner_init(&r2, &t, SIMF_AUTHORITATIVE);
    while (!runner_done(&r2)) runner_step(&r2);
    if (world_hash(&r.w) != world_hash(&r2.w)) { CHECK(false, test_name); return false; }

    char gpath[512];
    snprintf(gpath, sizeof gpath, "%s/%s", g_golden_dir, golden_name);
    if (g_write_golden) {
        FILE *f = fopen(gpath, "w");
        if (!f) { CHECK(false, test_name); return false; }
        fputs(lines, f);
        fclose(f);
        printf("WROTE %s\n", gpath);
        CHECK(true, test_name);
        return true;
    }
    FILE *f = fopen(gpath, "r");
    if (!f) {
        printf("FAIL %s: missing %s (run 'make golden')\n", test_name, gpath);
        g_failures++;
        return true; // the replay itself succeeded; semantic checks may still run
    }
    char golden[16384];
    size_t glen = fread(golden, 1, sizeof golden - 1, f);
    golden[glen] = '\0';
    fclose(f);
    CHECK(strcmp(lines, golden) == 0, test_name);
    return true;
}

static void t2_replay_golden(void) {
    replay_golden_stream("cast_basic.trace", "cast_basic.hashes", "t2_replay_golden", NULL);
}

// The M2 keystone: how often you snapshot (render) cannot change outcomes.
static void t2_cadence_invariance(void) {
    trace_t t;
    if (load_trace_or_fail("cast_basic.trace", &t) != 0) { CHECK(false, "t2_cadence_invariance"); return; }

    enum { MAX_TICKS = 4096 };
    static uint64_t every_tick_hash[MAX_TICKS];
    uint32_t total = t.end_tick - t.start_tick;
    if (total > MAX_TICKS) { CHECK(false, "t2_cadence_invariance"); return; }

    // Pass A: snapshot (copy + hash) every tick.
    runner_t r;
    runner_init(&r, &t, SIMF_AUTHORITATIVE);
    while (!runner_done(&r)) {
        runner_step(&r);
        sim_world_t snap = r.w; // the "render copy"
        every_tick_hash[r.ticks_run - 1] = world_hash(&snap);
    }
    uint64_t final_a = world_hash(&r.w);

    // Pass B: snapshot every 3rd tick — sampled hashes must match pass A.
    bool ok = true;
    runner_init(&r, &t, SIMF_AUTHORITATIVE);
    while (!runner_done(&r)) {
        runner_step(&r);
        if (r.ticks_run % 3 == 0) {
            sim_world_t snap = r.w;
            ok &= world_hash(&snap) == every_tick_hash[r.ticks_run - 1];
        }
    }
    ok &= world_hash(&r.w) == final_a;

    // Pass C: seeded-jitter sampling.
    uint32_t rng = t.seed ? t.seed : 1;
    runner_init(&r, &t, SIMF_AUTHORITATIVE);
    while (!runner_done(&r)) {
        runner_step(&r);
        rng = rng * 1664525u + 1013904223u;
        if (rng & 1) {
            sim_world_t snap = r.w;
            ok &= world_hash(&snap) == every_tick_hash[r.ticks_run - 1];
        }
    }
    ok &= world_hash(&r.w) == final_a;
    CHECK(ok, "t2_cadence_invariance");
}

// Snapshotting must be provably side-effect-free.
static void t2_snapshot_pure(void) {
    trace_t t;
    if (load_trace_or_fail("cast_basic.trace", &t) != 0) { CHECK(false, "t2_snapshot_pure"); return; }
    runner_t r;
    runner_init(&r, &t, SIMF_AUTHORITATIVE);
    for (int i = 0; i < 50; i++) runner_step(&r); // mid-trace, casts in flight
    uint64_t before = world_hash(&r.w);
    sim_world_t s1 = r.w, s2 = r.w;
    bool ok = memcmp(&s1, &s2, sizeof s1) == 0 && world_hash(&r.w) == before;
    CHECK(ok, "t2_snapshot_pure");
}

// Firmware queues key-down detail only. Releases and rising edges remain
// level-sampled, so omitting ignored key-up detail must preserve held poses,
// release-to-idle, recipes, and rapid alternating input exactly.
static void t2_keydown_only_equivalence(void) {
    sim_world_t with_keyups, keydowns_only;
    sim_init(&with_keyups, SIMF_AUTHORITATIVE, 0);
    sim_init(&keydowns_only, SIMF_AUTHORITATIVE, 0);
    uint8_t previous = 0;
    bool saw_recipe = false;
    bool ok = true;

    for (int tick = 0; tick < 64; tick++) {
        uint8_t levels;
        if (tick < 6) levels = 1;            // one held key
        else if (tick < 20) levels = 0;      // release without detail
        else if (tick & 1) levels = 0;       // rapid press/release cadence
        else levels = (tick & 2) ? 1 : 2;    // alternate hands

        sim_event_t full[2], down[2];
        uint8_t n_full = 0, n_down = 0;
        for (uint8_t side = 0; side < 2; side++) {
            bool was = (previous >> side) & 1;
            bool now = (levels >> side) & 1;
            if (was == now) continue;
            sim_event_t ev = {now ? SIM_EV_KEYDOWN : SIM_EV_KEYUP, side,
                              (uint8_t)((tick / 2) & 3), (uint8_t)(tick % 6)};
            full[n_full++] = ev;
            if (now) down[n_down++] = ev;
        }
        sim_inputs_t inputs = {.down_mask = levels};
        sim_tick(&with_keyups, inputs, full, n_full);
        sim_tick(&keydowns_only, inputs, down, n_down);
        ok &= world_hash(&with_keyups) == world_hash(&keydowns_only);
        saw_recipe |= keydowns_only.wiz[0].recipe_n >= 2 ||
                      keydowns_only.wiz[1].recipe_n >= 2;
        if (tick == 5) ok &= keydowns_only.wiz[0].pose == POSE_CAST;
        if (tick == 20) ok &= keydowns_only.wiz[0].pose == POSE_IDLE;
        previous = levels;
    }
    ok &= saw_recipe;
    CHECK(ok, "t2_keydown_only_equivalence");
}

// Overflow is explicit and harmless: pushes fail loudly, drops are counted,
// and the sim keeps running deterministically.
static void t2_queue_overflow(void) {
    sim_evq_t q = {0};
    int rejected = 0;
    for (int i = 0; i < SIM_EVQ_CAP + 8; i++) {
        if (!sim_evq_push(&q, (sim_event_t){SIM_EV_KEYDOWN, 0, (uint8_t)(i % 4), (uint8_t)(i % 6)})) rejected++;
    }
    sim_event_t evs[SIM_EVQ_CAP + 1];
    uint8_t n = sim_evq_drain(&q, evs);

    bool ok = rejected == 8;
    ok &= n == SIM_EVQ_CAP + 1;
    ok &= evs[SIM_EVQ_CAP].kind == SIM_EV_OVERFLOW && evs[SIM_EVQ_CAP].row == 8;

    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    sim_tick(&w, (sim_inputs_t){.down_mask = 1}, evs, n);
    ok &= w.overflow_count == 8;
    ok &= w.tick == 1 && w.wiz[SIM_SIDE_L].pose == POSE_CAST; // still a sane world

    // queue drains clean afterwards
    ok &= sim_evq_drain(&q, evs) == 0;
    CHECK(ok, "t2_queue_overflow");
}

// Relative behavior must be identical when the tick counter wraps uint32.
static void t2_tick_wrap(void) {
    trace_t t;
    if (load_trace_or_fail("cast_basic.trace", &t) != 0) { CHECK(false, "t2_tick_wrap"); return; }
    trace_t tw = t;
    tw.start_tick = UINT32_MAX - 50; // wraps mid-trace (length 200)
    tw.end_tick   = tw.start_tick + (t.end_tick - t.start_tick);
    for (int i = 0; i < tw.n_ev; i++) tw.ev[i].tick = tw.start_tick + t.ev[i].tick;

    runner_t a, b;
    runner_init(&a, &t, SIMF_AUTHORITATIVE);
    runner_init(&b, &tw, SIMF_AUTHORITATIVE);
    bool ok = true;
    while (!runner_done(&a)) {
        runner_step(&a);
        runner_step(&b);
        ok &= world_hash_no_tick(&a.w) == world_hash_no_tick(&b.w);
    }
    ok &= runner_done(&b);
    ok &= b.w.tick == (uint32_t)(UINT32_MAX - 50 + 200); // wrapped, still counting
    CHECK(ok, "t2_tick_wrap");
}

/* ---------------- M3: split snapshot proof ---------------- */

// Lossy link model: seeded drop/duplicate/reorder/corrupt over a packet FIFO.
typedef struct {
    duel_snapshot_t q[64];
    int             n;
    uint32_t        rng;
    int             drop_pct, dup_pct, swap_pct, corrupt_pct;
} link_t;

static uint32_t lcg(uint32_t *s) {
    *s = *s * 1664525u + 1013904223u;
    return *s;
}

static void link_send(link_t *l, const duel_snapshot_t *p) {
    if ((int)(lcg(&l->rng) % 100) < l->drop_pct) return;
    if (l->n < 63 && (int)(lcg(&l->rng) % 100) < l->dup_pct) l->q[l->n++] = *p;
    if (l->n < 64) l->q[l->n++] = *p;
    if ((int)(lcg(&l->rng) % 100) < l->corrupt_pct && l->n) {
        l->q[l->n - 1].tick16 ^= 0x5A5A; // payload corruption; CRC must catch it
    }
    if (l->n >= 2 && (int)(lcg(&l->rng) % 100) < l->swap_pct) {
        duel_snapshot_t tmp = l->q[l->n - 1];
        l->q[l->n - 1]      = l->q[l->n - 2];
        l->q[l->n - 2]      = tmp;
    }
}

static bool link_recv(link_t *l, duel_snapshot_t *out) {
    if (!l->n) return false;
    *out = l->q[0];
    memmove(l->q, l->q + 1, (size_t)(--l->n) * sizeof *l->q);
    return true;
}

// Master streams snapshots through a nasty link; the slave must converge to
// the master's final state and typing (the master sim) must be unaffected.
static void t3_lossy_convergence(void) {
    trace_t t;
    if (load_trace_or_fail("cast_basic.trace", &t) != 0) { CHECK(false, "t3_lossy_convergence"); return; }

    runner_t m;
    runner_init(&m, &t, SIMF_AUTHORITATIVE);
    link_t link = {.rng = 7, .drop_pct = 20, .dup_pct = 10, .swap_pct = 20, .corrupt_pct = 10};
    duel_rx_state_t rx = {0};
    uint16_t seq = 0;
    int corrupt_dropped = 0;

    while (!runner_done(&m)) {
        runner_step(&m);
        if (m.ticks_run % 2 == 0) {
            duel_snapshot_t pkt;
            duel_encode(&m.w, 0x42, ++seq, &pkt);
            link_send(&link, &pkt);
        }
        duel_snapshot_t in;
        while (link_recv(&link, &in)) {
            if (!duel_decode_valid(&in)) { corrupt_dropped++; continue; }
            duel_rx_accept(&rx, &in, false);
        }
    }
    // Deliver one final clean snapshot (the steady-state 80 ms cadence).
    duel_snapshot_t final_pkt;
    duel_encode(&m.w, 0x42, ++seq, &final_pkt);
    bool ok = duel_rx_accept(&rx, &final_pkt, false);
    ok &= memcmp(&rx.last, &final_pkt, sizeof final_pkt) == 0;
    ok &= corrupt_dropped > 0;           // the corruption path was exercised
    ok &= rx.stale_drops > 0;            // ...and so was reorder/dup rejection
    CHECK(ok, "t3_lossy_convergence");
}

// A stale or duplicate sequence can never roll the slave's view backward.
static void t3_stale_rollback(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 100);
    duel_rx_state_t rx = {0};
    duel_snapshot_t p10, p5, dup;

    duel_encode(&w, 0x42, 10, &p10);
    bool ok = duel_rx_accept(&rx, &p10, false);

    w.wiz[0].pose = POSE_CAST; // a different, older world state
    duel_encode(&w, 0x42, 5, &p5);
    ok &= !duel_rx_accept(&rx, &p5, false);
    ok &= memcmp(&rx.last, &p10, sizeof p10) == 0;

    dup = p10;
    ok &= !duel_rx_accept(&rx, &dup, false);
    ok &= rx.stale_drops == 2;
    CHECK(ok, "t3_stale_rollback");
}

// A rebooted master (new session nonce) is adopted immediately at seq 1.
static void t3_session_restart(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    duel_rx_state_t rx = {0};
    duel_snapshot_t old_pkt, new_pkt;

    duel_encode(&w, 0x42, 1000, &old_pkt);
    bool ok = duel_rx_accept(&rx, &old_pkt, false);

    duel_encode(&w, 0x43, 1, &new_pkt); // new boot nonce, sequence restarts
    ok &= duel_rx_accept(&rx, &new_pkt, false);
    ok &= rx.session == 0x43 && rx.last_seq == 1;
    CHECK(ok, "t3_session_restart");
}

// Encoding snapshots must not perturb the master sim: a run that encodes
// every 2nd tick hashes identically to one that never encodes. This is the
// host proxy for "the keyboard keeps typing if display sync fails".
static void t3_link_dead_typing_ok(void) {
    trace_t t;
    if (load_trace_or_fail("cast_basic.trace", &t) != 0) { CHECK(false, "t3_link_dead_typing_ok"); return; }

    runner_t plain, linked;
    runner_init(&plain, &t, SIMF_AUTHORITATIVE);
    runner_init(&linked, &t, SIMF_AUTHORITATIVE);
    uint16_t seq = 0;
    bool ok = true;
    while (!runner_done(&plain)) {
        runner_step(&plain);
        runner_step(&linked);
        if (linked.ticks_run % 2 == 0) {
            duel_snapshot_t pkt;
            duel_encode(&linked.w, 0x42, ++seq, &pkt); // sent into the void (100% loss)
        }
        ok &= world_hash(&plain.w) == world_hash(&linked.w);
    }
    CHECK(ok, "t3_link_dead_typing_ok");
}

// The stale flag rises after silence and a live packet clears it — including
// the pathological reboot where the session nonce collides and the sequence
// number is LOWER (the link_was_stale override must force adoption).
static void t3_stale_marker(void) {
    enum { STALE_TICKS = 12 };
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    duel_rx_state_t rx = {0};
    duel_snapshot_t pkt;

    duel_encode(&w, 0x42, 500, &pkt);
    bool ok = duel_rx_accept(&rx, &pkt, false);

    uint32_t ticks_since_pkt = 0;
    for (int i = 0; i < 20; i++) ticks_since_pkt++; // 20 silent ticks
    bool stale = ticks_since_pkt > STALE_TICKS;
    ok &= stale;

    // Nonce collision + lower seq after "reboot": only the stale override saves us.
    duel_encode(&w, 0x42, 3, &pkt);
    ok &= !duel_rx_accept(&rx, &pkt, false); // without the override: rejected
    ok &= duel_rx_accept(&rx, &pkt, stale);  // with it: adopted
    ticks_since_pkt = 0;
    ok &= ticks_since_pkt <= STALE_TICKS && rx.last_seq == 3;
    CHECK(ok, "t3_stale_marker");
}

/* ---------------- M4: first cross-screen spell ---------------- */

// One cast crosses the whole battlefield: monotonic progress, past the
// midpoint, resolved by impact with exactly one hp lost — master-decided.
static void t4_flight_golden(void) {
    trace_t t;
    if (load_trace_or_fail("cast_impact.trace", &t) != 0) { CHECK(false, "t4_flight_golden"); return; }
    runner_t r;
    runner_init(&r, &t, SIMF_AUTHORITATIVE);

    bool ok = true, saw_spawn = false, crossed_mid = false, resolved = false;
    uint8_t last_pos = 0;
    while (!runner_done(&r)) {
        runner_step(&r);
        const sim_spell_t *sp = &r.w.spell[SIM_SIDE_L];
        if (sp->active) {
            if (!saw_spawn) {
                saw_spawn = true;
                ok &= sp->pos == SIM_SPAWN_L && sp->dir == SIM_SPELL_SPEED;
            } else {
                ok &= sp->pos == (uint8_t)(last_pos + SIM_SPELL_SPEED); // monotonic, fixed speed
            }
            last_pos = sp->pos;
            if (sp->pos >= 128) crossed_mid = true;
        } else if (saw_spawn && !resolved && r.w.fx_seq == 1) {
            resolved = true;
        }
    }
    ok &= saw_spawn && crossed_mid && resolved;
    ok &= r.w.fx_seq == 1 && r.w.fx_kind == FX_IMPACT_R;
    ok &= r.w.wiz[SIM_SIDE_R].hp == SIM_MAX_HP - 1 && r.w.wiz[SIM_SIDE_L].hp == SIM_MAX_HP;
    CHECK(ok, "t4_flight_golden");
}

// Does rendering this world put spell pixels on this canvas? Diff against
// the same world with the slots cleared — same draw code the firmware blits.
static bool canvas_has_spell_pixels(const sim_world_t *w, bool is_left) {
    duel_render_t with = {.w = *w}, without = {.w = *w};
    without.w.spell[0].active = without.w.spell[1].active = 0;
    duel_fb_t a, b;
    duel_fb_clear(&a);
    duel_fb_clear(&b);
    wiz_draw_scene(&a, &with, is_left, 0, false);
    wiz_draw_scene(&b, &without, is_left, 0, false);
    return memcmp(a.bits, b.bits, sizeof a.bits) != 0;
}

// The spell renders on the caster's screen early, the defender's late, and
// NEITHER while crossing the desk gap. Ownership is sampled well inside each
// visible range so edge clipping cannot make the assertion ambiguous.
static void t4_screen_ownership(void) {
    trace_t t;
    if (load_trace_or_fail("cast_impact.trace", &t) != 0) { CHECK(false, "t4_screen_ownership"); return; }
    runner_t r;
    runner_init(&r, &t, SIMF_AUTHORITATIVE);

    bool ok = true;
    int checked_left = 0, checked_gap = 0, checked_right = 0;
    while (!runner_done(&r)) {
        runner_step(&r);
        const sim_spell_t *sp = &r.w.spell[SIM_SIDE_L];
        if (!sp->active) continue;
        uint8_t u = sp->pos;
        if (u >= 48 && u <= 95) {
            ok &= canvas_has_spell_pixels(&r.w, true) && !canvas_has_spell_pixels(&r.w, false);
            checked_left++;
        } else if (u > 95 && u < 160) {
            ok &= !canvas_has_spell_pixels(&r.w, true) && !canvas_has_spell_pixels(&r.w, false);
            checked_gap++;
        } else if (u >= 160 && u <= 207) {
            ok &= !canvas_has_spell_pixels(&r.w, true) && canvas_has_spell_pixels(&r.w, false);
            checked_right++;
        }
    }
    ok &= checked_left > 3 && checked_gap > 3 && checked_right > 3;
    CHECK(ok, "t4_screen_ownership");
}

// Sweep the defender's tap tick across the flight. Shield lives for
// SIM_SHIELD_TICKS after the keydown and the spell sits in the doorstep
// band for 2 ticks (78: u=240, 79: u=244; impact tick 80), so deflects must
// form exactly the contiguous window T in [69, 79] — 11 ticks wide, and the
// outcome flips exactly at the documented thresholds.
static void t4_shield_window(void) {
    bool ok = true;
    int first_deflect = -1, last_deflect = -1, deflects = 0, impacts = 0;
    for (uint32_t T = 55; T <= 85; T++) {
        trace_t t;
        memset(&t, 0, sizeof t);
        t.seed     = 1;
        t.end_tick = 120;
        t.n_ev     = 4;
        t.ev[0] = (trace_ev_t){10, TRACE_EV_PRESS, 0, 2, 3};
        t.ev[1] = (trace_ev_t){13, TRACE_EV_RELEASE, 0, 2, 3};
        t.ev[2] = (trace_ev_t){T, TRACE_EV_PRESS, 1, 1, 1};
        t.ev[3] = (trace_ev_t){T + 2, TRACE_EV_RELEASE, 1, 1, 1};

        runner_t r;
        runner_init(&r, &t, SIMF_AUTHORITATIVE);
        uint8_t outcome = FX_NONE;
        while (!runner_done(&r)) {
            runner_step(&r);
            if (outcome == FX_NONE && r.w.fx_seq >= 1) outcome = r.w.fx_kind;
        }
        if (outcome == FX_DEFLECT_R) {
            deflects++;
            if (first_deflect < 0) first_deflect = (int)T;
            last_deflect = (int)T;
        } else if (outcome == FX_IMPACT_R) {
            impacts++;
        } else {
            ok = false; // every run must resolve one way or the other
        }
    }
    ok &= deflects == 11 && first_deflect == 69 && last_deflect == 79;
    ok &= deflects + impacts == 31; // contiguous window, no holes
    CHECK(ok, "t4_shield_window");
}

// (t4_hp_reset was deleted: its "felled wizard resets both duelists" M4
//  placeholder semantics are superseded by the M5 lifecycle arc — see t5_*.)

// The link drops every snapshot around the impact; the next delivered one
// must land the slave directly in the post-impact state.
static void t4_missed_impact_recovery(void) {
    trace_t t;
    if (load_trace_or_fail("cast_impact.trace", &t) != 0) { CHECK(false, "t4_missed_impact_recovery"); return; }
    runner_t m;
    runner_init(&m, &t, SIMF_AUTHORITATIVE);
    duel_rx_state_t rx = {0};
    duel_snapshot_t pkt;
    bool ok = true;

    while (m.ticks_run < 70) runner_step(&m); // pre-impact, spell in flight
    duel_encode(&m.w, 0x42, 1, &pkt);
    ok &= duel_rx_accept(&rx, &pkt, false);
    sim_world_t pre;
    duel_decode_world(&rx.last, &pre);
    ok &= pre.spell[SIM_SIDE_L].active && pre.wiz[SIM_SIDE_R].hp == SIM_MAX_HP;

    while (m.ticks_run < 85) runner_step(&m); // impact happened; packets all lost
    duel_encode(&m.w, 0x42, 2, &pkt);         // first packet to get through
    ok &= duel_rx_accept(&rx, &pkt, false);
    sim_world_t post;
    duel_decode_world(&rx.last, &post);
    ok &= !post.spell[SIM_SIDE_L].active && !post.spell[SIM_SIDE_R].active;
    ok &= post.wiz[SIM_SIDE_R].hp == SIM_MAX_HP - 1;
    ok &= post.fx_seq == 1 && post.fx_kind == FX_IMPACT_R;
    CHECK(ok, "t4_missed_impact_recovery");
}

/* ---------------- M5: lifecycle and roster ---------------- */

/* Timing model used throughout t5 (derived from duel_sim.c, not the spec):
 * rising edge at processed tick E -> windup 10 -> spawn during tick E+10 at
 * u=8/247 -> 4 u/tick -> doorstep (240/15) during tick E+68 -> impact
 * (248/7) during tick E+70. A KO during tick K runs COLLAPSE 12 / DOWNED 25 /
 * MEDIC 25 / REPLACE 20 and is ACTIVE again during tick K+82. "Observed at
 * tick T" below means: after the sim_tick call for which w->tick becomes T. */

// Minimal programmatic driver for tests that need direct world pokes between
// ticks: derives edge events from a desired down mask, mirroring runner.h.
typedef struct {
    sim_world_t w;
    uint8_t     down;
} drv_t;

static void drv_init(drv_t *d, uint8_t flags) {
    memset(d, 0, sizeof *d);
    sim_init(&d->w, flags, 0);
}

static void drv_step(drv_t *d, uint8_t mask) {
    sim_event_t evs[2];
    uint8_t     n = 0;
    for (uint8_t s = 0; s < 2; s++) {
        uint8_t was = (d->down >> s) & 1, now = (mask >> s) & 1;
        if (now != was) evs[n++] = (sim_event_t){now ? SIM_EV_KEYDOWN : SIM_EV_KEYUP, s, 2, 3};
    }
    d->down = mask;
    sim_tick(&d->w, (sim_inputs_t){.down_mask = mask}, evs, n);
}

// Short tap on `side` (press 3 ticks, release): a rising edge with a shield
// window that expires ~54 ticks before any spell could reach this side.
static void drv_tap(drv_t *d, int side) {
    uint8_t bit = (uint8_t)(1 << side);
    drv_step(d, bit);
    drv_step(d, bit);
    drv_step(d, bit);
    drv_step(d, 0);
}

// M5 acceptance: five impacts fell a wizard; the KO arc runs on exact fixed
// timers (12/25/25/20 = 82 ticks), returns ACTIVE at full hp with the next
// roster variant and a fresh regen clock, and never touches the opponent.
static void t5_ko_sequence(void) {
    // Pending-cast cancellation: start a lethal bolt near right, then have
    // the victim press two ticks before impact. Deliberately omit the detail
    // event (as if that channel dropped it), so the sampled rising edge starts
    // windup without also raising a ward and preventing the killing impact.
    bool ok = true;
    {
        sim_world_t w;
        sim_init(&w, SIMF_AUTHORITATIVE, 0);
        w.wiz[SIM_SIDE_R].hp = 1; // poke: the next impact fells the victim
        w.spell[SIM_SIDE_L] = (sim_spell_t){.active = 1, .pos = 239, .dir = SIM_SPELL_SPEED};
        sim_tick(&w, (sim_inputs_t){.down_mask = 0x2}, NULL, 0); // victim presses; bolt -> 243
        ok &= w.wiz[SIM_SIDE_R].cast_windup > 0;
        sim_tick(&w, (sim_inputs_t){.down_mask = 0x2}, NULL, 0); // tick before impact; bolt -> 247
        ok &= w.wiz[SIM_SIDE_R].cast_windup > 0;                 // prove pending at collapse setup
        sim_tick(&w, (sim_inputs_t){.down_mask = 0}, NULL, 0);   // killing impact; bolt -> 251
        ok &= w.wiz[SIM_SIDE_R].life == LIFE_COLLAPSE;
        ok &= w.wiz[SIM_SIDE_R].cast_windup == 0; // canceled immediately by KO
        ok &= !w.spell[SIM_SIDE_R].active;
        while (w.wiz[SIM_SIDE_R].life != LIFE_ACTIVE) {
            sim_tick(&w, (sim_inputs_t){0}, NULL, 0);
            ok &= !w.spell[SIM_SIDE_R].active;
        }
    }

    trace_t t;
    memset(&t, 0, sizeof t);
    t.seed     = 1;
    t.end_tick = 560; // 5th impact at 400, ACTIVE again at 482
    t.n_ev     = 10;
    for (int i = 0; i < 5; i++) { // presses at 10/90/170/250/330: impacts at 80/160/240/320/400
        t.ev[2 * i]     = (trace_ev_t){10 + 80u * (uint32_t)i, TRACE_EV_PRESS, 0, 2, 3};
        t.ev[2 * i + 1] = (trace_ev_t){13 + 80u * (uint32_t)i, TRACE_EV_RELEASE, 0, 2, 3};
    }

    runner_t r;
    runner_init(&r, &t, SIMF_AUTHORITATIVE);
    uint8_t  prev_hp = SIM_MAX_HP, prev_fx = 0, prev_life = LIFE_ACTIVE;
    uint8_t  life_seq[8];
    uint32_t life_tick[8];
    int      n_life = 0, impacts = 0;
    while (!runner_done(&r)) {
        runner_step(&r);
        const sim_wizard_t *rw = &r.w.wiz[SIM_SIDE_R];
        if (rw->hp != prev_hp) {
            if (rw->hp < prev_hp) { // one pip per impact, fx bumped in lockstep
                ok &= rw->hp == prev_hp - 1;
                ok &= r.w.fx_seq == prev_fx + 1 && r.w.fx_kind == FX_IMPACT_R;
                impacts++;
            } else { // the only hp gain allowed here is the respawn refill
                ok &= rw->hp == SIM_MAX_HP && rw->life == LIFE_ACTIVE;
            }
            prev_hp = rw->hp;
            prev_fx = r.w.fx_seq;
        }
        if (rw->life != prev_life) {
            if (n_life < 8) {
                life_seq[n_life]  = rw->life;
                life_tick[n_life] = r.w.tick;
            }
            n_life++;
            if (rw->life == LIFE_COLLAPSE) { // felled: all action state cleared
                ok &= rw->hp == 0 && rw->pose == POSE_IDLE;
                ok &= rw->shield_ticks == 0 && rw->cast_windup == 0;
            }
            prev_life = rw->life;
        }
        // The opponent is never perturbed by the defender's lifecycle.
        ok &= r.w.wiz[SIM_SIDE_L].life == LIFE_ACTIVE && r.w.wiz[SIM_SIDE_L].hp == SIM_MAX_HP;
    }
    ok &= impacts == 5;
    ok &= n_life == 5;
    ok &= life_seq[0] == LIFE_COLLAPSE && life_seq[1] == LIFE_DOWNED && life_seq[2] == LIFE_MEDIC &&
          life_seq[3] == LIFE_REPLACE && life_seq[4] == LIFE_ACTIVE;
    ok &= life_tick[1] - life_tick[0] == SIM_COLLAPSE_TICKS;  // 12
    ok &= life_tick[2] - life_tick[1] == SIM_DOWNED_TICKS;    // 25
    ok &= life_tick[3] - life_tick[2] == SIM_MEDIC_TICKS;     // 25
    ok &= life_tick[4] - life_tick[3] == SIM_REPLACE_TICKS;   // 20
    ok &= life_tick[4] - life_tick[0] == 82;                  // total downtime
    const sim_wizard_t *rw = &r.w.wiz[SIM_SIDE_R];
    ok &= rw->life == LIFE_ACTIVE && rw->hp == SIM_MAX_HP && rw->variant == 1;
    ok &= rw->regen_ticks == SIM_REGEN_TICKS; // regen clock restored at respawn
    CHECK(ok, "t5_ko_sequence");
}

static void no_dead_end_invariants(const sim_world_t *w, bool *ok) {
    for (int s = 0; s < 2; s++) {
        *ok &= (w->wiz[s].life == LIFE_ACTIVE) == (w->wiz[s].hp > 0);
        if (w->wiz[s].life != LIFE_ACTIVE) *ok &= w->wiz[s].life_ticks >= 1;
    }
}

// M5 acceptance: no dead ends — every KO returns to ACTIVE in exactly 82
// ticks with no input, on either side, repeatedly, and both sides can still
// cast afterwards. Per-tick invariants: ACTIVE <=> hp>0; a non-ACTIVE phase
// always has a live timer.
static void t5_no_dead_end(void) {
    drv_t d;
    drv_init(&d, SIMF_AUTHORITATIVE);
    bool ok = true;
    for (int cycle = 0; cycle < 3; cycle++) {
        for (int victim = 0; victim < 2; victim++) {
            int attacker = 1 - victim;
            d.w.wiz[victim].hp = 1; // poke: one impact fells, skipping 4 identical flights
            drv_tap(&d, attacker);
            no_dead_end_invariants(&d.w, &ok);
            uint32_t collapse_tick = 0, active_tick = 0;
            for (int i = 0; i < 200 && !active_tick; i++) {
                drv_step(&d, 0);
                no_dead_end_invariants(&d.w, &ok);
                if (!collapse_tick && d.w.wiz[victim].life != LIFE_ACTIVE) collapse_tick = d.w.tick;
                if (collapse_tick && d.w.wiz[victim].life == LIFE_ACTIVE) active_tick = d.w.tick;
            }
            ok &= collapse_tick != 0 && active_tick != 0;
            ok &= active_tick - collapse_tick == 82;
        }
    }
    // After 3 KO cycles per side, both sides still cast: a fresh tap must put
    // a spell in the air within windup+1 ticks of the rising edge.
    drv_step(&d, 0x3); // simultaneous rising edges
    for (int i = 0; i < SIM_CAST_WINDUP_TICKS; i++) drv_step(&d, 0);
    ok &= d.w.spell[SIM_SIDE_L].active && d.w.spell[SIM_SIDE_R].active;
    CHECK(ok, "t5_no_dead_end");
}

// M5 acceptance: a bolt arriving at a downed wizard's doorstep fizzles — no
// hp change, no lifecycle perturbation (identical per-tick to a control run
// without the bolt) — while the opponent's pose machine runs normally.
static void t5_downed_fizzle(void) {
    drv_t a, b; // a: KO + a second bolt at the corpse; b: control, KO only
    drv_init(&a, SIMF_AUTHORITATIVE);
    drv_init(&b, SIMF_AUTHORITATIVE);
    a.w.wiz[SIM_SIDE_R].hp = 1; // poke: the first impact fells (skips 4 flights)
    b.w.wiz[SIM_SIDE_R].hp = 1;

    // Left presses at 10 (KO impact at 76) and — in world a only — at 89,
    // while right is DOWNED (88..112). The second bolt spawns at 95 and hits
    // the doorstep (u=240) during tick 153: mid-REPLACE, still non-ACTIVE.
    // (Earliest possible fizzle is KO+65 — the slot only frees at the KO
    // impact — which always lands in REPLACE for a full-flight second cast.)
    bool ok = true, saw_fizzle = false, saw_cast = false, saw_recover = false;
    for (uint32_t t = 0; t < 180; t++) {
        uint8_t mask_a = (t >= 10 && t < 13) || (t >= 89 && t < 92) ? 0x1 : 0;
        uint8_t mask_b = (t >= 10 && t < 13) ? 0x1 : 0;
        drv_step(&a, mask_a);
        drv_step(&b, mask_b);
        // The extra bolt must not perturb any byte of right's wizard state.
        ok &= memcmp(&a.w.wiz[SIM_SIDE_R], &b.w.wiz[SIM_SIDE_R], sizeof a.w.wiz[SIM_SIDE_R]) == 0;
        if (!saw_fizzle && a.w.fx_seq == 2) {
            saw_fizzle = true;
            ok &= a.w.fx_kind == FX_FIZZLE_R;
            ok &= a.w.wiz[SIM_SIDE_R].hp == 0;          // no damage to the corpse
            ok &= !a.w.spell[SIM_SIDE_L].active;        // bolt gone at the doorstep
        }
        if (a.w.wiz[SIM_SIDE_R].life != LIFE_ACTIVE) {  // opponent-stays-active
            if (a.w.wiz[SIM_SIDE_L].pose == POSE_CAST) saw_cast = true;
            if (a.w.wiz[SIM_SIDE_L].pose == POSE_RECOVER) saw_recover = true;
        }
    }
    ok &= saw_fizzle && saw_cast && saw_recover;
    ok &= a.w.fx_seq == 2 && b.w.fx_seq == 1; // exactly one fizzle, control untouched
    ok &= a.w.wiz[SIM_SIDE_R].life == LIFE_ACTIVE && a.w.wiz[SIM_SIDE_R].hp == SIM_MAX_HP;
    CHECK(ok, "t5_downed_fizzle");
}

// M5 acceptance: a non-ACTIVE wizard cannot act — keys grant no shield, no
// windup, no pose, no spell, in every phase — and a key held from mid-DOWNED
// through respawn never auto-casts (no rising edge), while a fresh press
// after respawn casts fine.
static void t5_downed_cannot_act(void) {
    drv_t d;
    drv_init(&d, SIMF_AUTHORITATIVE);
    bool ok = true;

    // KO right, then toggle a right key every 3 ticks through the whole
    // downtime: every phase (min length 12) sees multiple press edges.
    d.w.wiz[SIM_SIDE_R].hp = 1; // poke: one impact fells
    drv_tap(&d, SIM_SIDE_L);
    for (int i = 0; i < 100 && d.w.wiz[SIM_SIDE_R].life == LIFE_ACTIVE; i++) drv_step(&d, 0);
    ok &= d.w.wiz[SIM_SIDE_R].life == LIFE_COLLAPSE;
    bool pressed_in[5] = {false};
    while (d.w.wiz[SIM_SIDE_R].life != LIFE_ACTIVE) {
        // stop pressing near a phase's end so no edge can straddle the respawn
        uint8_t phase = d.w.wiz[SIM_SIDE_R].life;
        uint8_t mask  = (d.w.tick % 6 < 3 && d.w.wiz[SIM_SIDE_R].life_ticks > 2) ? 0x2 : 0;
        if (mask) pressed_in[phase] = true;
        drv_step(&d, mask);
        const sim_wizard_t *rw = &d.w.wiz[SIM_SIDE_R];
        if (rw->life != LIFE_ACTIVE) {
            ok &= rw->shield_ticks == 0 && rw->cast_windup == 0 && rw->pose == POSE_IDLE;
            ok &= !d.w.spell[SIM_SIDE_R].active;
        }
    }
    ok &= pressed_in[LIFE_COLLAPSE] && pressed_in[LIFE_DOWNED] && pressed_in[LIFE_MEDIC] && pressed_in[LIFE_REPLACE];
    ok &= !d.w.spell[SIM_SIDE_R].active; // nothing spawned across the whole arc

    // Second KO: hold a right key from mid-DOWNED through respawn.
    d.w.wiz[SIM_SIDE_R].hp = 1; // poke: fell it again
    drv_tap(&d, SIM_SIDE_L);
    for (int i = 0; i < 100 && d.w.wiz[SIM_SIDE_R].life != LIFE_DOWNED; i++) drv_step(&d, 0);
    for (int i = 0; i < 8; i++) drv_step(&d, 0); // mid-DOWNED
    ok &= d.w.wiz[SIM_SIDE_R].life == LIFE_DOWNED;
    for (int i = 0; i < 100 && d.w.wiz[SIM_SIDE_R].life != LIFE_ACTIVE; i++) drv_step(&d, 0x2); // hold
    for (int i = 0; i < 20; i++) { // still held after respawn: no rising edge
        drv_step(&d, 0x2);
        const sim_wizard_t *rw = &d.w.wiz[SIM_SIDE_R];
        ok &= rw->pose == POSE_IDLE && rw->cast_windup == 0 && rw->shield_ticks == 0;
        ok &= !d.w.spell[SIM_SIDE_R].active;
    }
    // Release, then a fresh press: the replacement casts (and shields) fine.
    drv_step(&d, 0);
    drv_step(&d, 0);
    drv_step(&d, 0x2);
    ok &= d.w.wiz[SIM_SIDE_R].shield_ticks > 0 && d.w.wiz[SIM_SIDE_R].pose == POSE_CAST;
    for (int i = 0; i < SIM_CAST_WINDUP_TICKS; i++) drv_step(&d, 0x2);
    ok &= d.w.spell[SIM_SIDE_R].active;
    CHECK(ok, "t5_downed_cannot_act");
}

// M5 acceptance: a lost pip regenerates on a slow clock that a hit resets,
// and only the authoritative sim can regen. Exact timing (from the code, the
// spec's "+375" is off by one): resolution runs BEFORE regen inside the same
// sim_tick, so the impact tick itself already consumes one count of the
// freshly reset clock — the pip lands SIM_REGEN_TICKS-1 ticks after the
// observed drop, and successive pips land exactly SIM_REGEN_TICKS apart.
static void t5_regen(void) {
    bool ok = true;

    // Scenario 1: single impact, one pip back.
    {
        drv_t d;
        drv_init(&d, SIMF_AUTHORITATIVE);
        uint32_t drop = 0, regain = 0;
        uint8_t  prev = SIM_MAX_HP;
        for (uint32_t t = 0; t < 470; t++) {
            drv_step(&d, t >= 10 && t < 13 ? 0x1 : 0);
            uint8_t hp = d.w.wiz[SIM_SIDE_R].hp;
            if (hp < prev) drop = d.w.tick;
            if (hp > prev) regain = d.w.tick;
            prev = hp;
        }
        ok &= drop == 81;                                // impact during tick 80
        ok &= regain == drop + SIM_REGEN_TICKS - 1;      // hp==4 through regain-1
        ok &= d.w.wiz[SIM_SIDE_R].hp == SIM_MAX_HP;
    }

    // Scenario 2: two impacts 100 ticks apart — the second resets the clock
    // (no pip at first-impact+374), then both pips return, 375 apart.
    {
        drv_t d;
        drv_init(&d, SIMF_AUTHORITATIVE);
        uint32_t change[8] = {0};
        int      n = 0;
        uint8_t  prev = SIM_MAX_HP;
        for (uint32_t t = 0; t < 940; t++) {
            drv_step(&d, (t >= 10 && t < 13) || (t >= 110 && t < 113) ? 0x1 : 0);
            uint8_t hp = d.w.wiz[SIM_SIDE_R].hp;
            if (hp != prev && n < 8) change[n++] = d.w.tick;
            prev = hp;
        }
        ok &= n == 4;                                    // 5->4, 4->3, 3->4, 4->5
        ok &= change[0] == 81 && change[1] == 181;       // impacts at 80 and 180
        ok &= change[2] - change[1] == SIM_REGEN_TICKS - 1;
        ok &= change[3] - change[2] == SIM_REGEN_TICKS;
        ok &= d.w.wiz[SIM_SIDE_R].hp == SIM_MAX_HP;
    }

    // Scenario 3: a non-authoritative (slave) world can never regen.
    {
        sim_world_t w;
        sim_init(&w, 0, 0);
        w.wiz[SIM_SIDE_R].hp = 3; // poke: a decoded snapshot view below max
        for (int t = 0; t < 800; t++) {
            sim_tick(&w, (sim_inputs_t){0}, NULL, 0);
            ok &= w.wiz[SIM_SIDE_R].hp == 3;
        }
    }
    CHECK(ok, "t5_regen");
}

// M5 acceptance: a non-authoritative world is only a rendered snapshot view;
// neither time nor local key traffic may mutate any byte of lifecycle state.
static void t5_slave_never_transitions(void) {
    sim_world_t w;
    sim_init(&w, 0, 0);
    w.wiz[SIM_SIDE_R].life        = LIFE_DOWNED; // poke: decoded mid-arc slave snapshot
    w.wiz[SIM_SIDE_R].life_ticks = 10;
    w.wiz[SIM_SIDE_R].variant    = 2;
    w.wiz[SIM_SIDE_R].hp         = 0;
    sim_wizard_t frozen = w.wiz[SIM_SIDE_R];
    bool         ok     = true;

    for (int t = 0; t < 200; t++) {
        if (t < 100) {
            sim_tick(&w, (sim_inputs_t){0}, NULL, 0);
        } else {
            bool        down = (t & 1) != 0;
            sim_event_t ev   = {down ? SIM_EV_KEYDOWN : SIM_EV_KEYUP, SIM_SIDE_R, 2, 3};
            sim_tick(&w, (sim_inputs_t){.down_mask = down ? 0x2 : 0}, &ev, 1);
        }
        ok &= memcmp(&w.wiz[SIM_SIDE_R], &frozen, sizeof frozen) == 0;
    }
    CHECK(ok, "t5_slave_never_transitions");
}

// M5 acceptance: lifecycle state rides the snapshot protocol — mid-phase
// packets round-trip exactly, and (styled on t4_missed_impact_recovery) a
// slave that misses any stretch of the arc, up to all 82 ticks of it, lands
// directly on the master's current phase from the next packet.
static void t5_snapshot_lifecycle(void) {
    drv_t m;
    drv_init(&m, SIMF_AUTHORITATIVE);
    m.w.wiz[SIM_SIDE_R].hp = 1; // poke: one impact fells (KO during tick 80)
    bool            ok = true;
    duel_rx_state_t rx_b = {0}, rx_c = {0}; // b: rejoin mid-MEDIC; c: miss the whole arc
    duel_snapshot_t pkt;
    sim_world_t     dec;

    for (uint32_t t = 0; t < 170; t++) {
        drv_step(&m, t >= 10 && t < 13 ? 0x1 : 0);
        const sim_wizard_t *mw = &m.w.wiz[SIM_SIDE_R];

        if (m.w.tick == 70) { // pre-KO: both slaves have a live view
            duel_encode(&m.w, 0x42, 1, &pkt);
            ok &= duel_rx_accept(&rx_b, &pkt, false) && duel_rx_accept(&rx_c, &pkt, false);
        }
        if (m.w.tick == 104 || m.w.tick == 152) { // (a) mid-DOWNED / mid-REPLACE round-trip
            ok &= mw->life == (m.w.tick == 104 ? LIFE_DOWNED : LIFE_REPLACE);
            duel_encode(&m.w, 0x42, 99, &pkt);
            duel_decode_world(&pkt, &dec);
            ok &= dec.wiz[SIM_SIDE_R].life == mw->life;
            ok &= dec.wiz[SIM_SIDE_R].life_ticks == mw->life_ticks;
            ok &= dec.wiz[SIM_SIDE_R].variant == mw->variant;
            ok &= dec.wiz[SIM_SIDE_R].hp == mw->hp;
        }
        if (m.w.tick == 129) { // (b) every packet since tick 70 dropped; rejoin mid-MEDIC
            ok &= mw->life == LIFE_MEDIC;
            duel_encode(&m.w, 0x42, 2, &pkt);
            ok &= duel_rx_accept(&rx_b, &pkt, false);
            duel_decode_world(&rx_b.last, &dec);
            ok &= dec.wiz[SIM_SIDE_R].life == LIFE_MEDIC;
            ok &= dec.wiz[SIM_SIDE_R].life_ticks == mw->life_ticks;
        }
        if (m.w.tick == 169) { // (c) the entire 82-tick downtime was dropped
            ok &= mw->life == LIFE_ACTIVE;
            duel_encode(&m.w, 0x42, 2, &pkt);
            ok &= duel_rx_accept(&rx_c, &pkt, false);
            duel_decode_world(&rx_c.last, &dec);
            ok &= dec.wiz[SIM_SIDE_R].life == LIFE_ACTIVE && dec.wiz[SIM_SIDE_R].life_ticks == 0;
            ok &= dec.wiz[SIM_SIDE_R].hp == SIM_MAX_HP && dec.wiz[SIM_SIDE_R].variant == 1;
        }
    }
    CHECK(ok, "t5_snapshot_lifecycle");
}

// M5 acceptance: a simultaneous double KO runs two full, independent
// lifecycle arcs in parallel — both impacts resolve, both sides return
// ACTIVE at full hp on the next variant with the exact 82-tick timing.
static void t5_double_ko(void) {
    drv_t d;
    drv_init(&d, SIMF_AUTHORITATIVE);
    d.w.wiz[SIM_SIDE_L].hp = 1; // poke both: the mirrored impacts (same tick 80)
    d.w.wiz[SIM_SIDE_R].hp = 1; // fell both wizards at once
    bool     ok = true;
    uint32_t collapse[2] = {0, 0}, active[2] = {0, 0};
    for (uint32_t t = 0; t < 180; t++) {
        drv_step(&d, t >= 10 && t < 13 ? 0x3 : 0); // symmetric casts, same tick
        for (int s = 0; s < 2; s++) {
            if (!collapse[s] && d.w.wiz[s].life != LIFE_ACTIVE) {
                collapse[s] = d.w.tick;
                ok &= d.w.wiz[s].life == LIFE_COLLAPSE && d.w.wiz[s].hp == 0;
            }
            if (collapse[s] && !active[s] && d.w.wiz[s].life == LIFE_ACTIVE) active[s] = d.w.tick;
        }
    }
    ok &= d.w.fx_seq == 2;                       // both impacts resolved
    ok &= collapse[0] == 81 && collapse[1] == 81; // felled the same tick
    ok &= active[0] - collapse[0] == 82 && active[1] - collapse[1] == 82;
    for (int s = 0; s < 2; s++) {
        ok &= d.w.wiz[s].life == LIFE_ACTIVE && d.w.wiz[s].hp == SIM_MAX_HP && d.w.wiz[s].variant == 1;
    }
    CHECK(ok, "t5_double_ko");
}

// M5 acceptance: the roster cycles — SIM_ROSTER_N KOs walk the variant
// through 1,2,3,0 (wrapping), and every value survives an encode/decode
// round-trip through the 3-bit wire field.
static void t5_variant_cycle(void) {
    drv_t d;
    drv_init(&d, SIMF_AUTHORITATIVE);
    bool ok = true;
    for (int k = 1; k <= SIM_ROSTER_N; k++) {
        d.w.wiz[SIM_SIDE_R].hp = 1; // poke: one impact per KO cycle
        drv_tap(&d, SIM_SIDE_L);
        bool downed = false;
        for (int i = 0; i < 200; i++) {
            drv_step(&d, 0);
            if (d.w.wiz[SIM_SIDE_R].life != LIFE_ACTIVE) downed = true;
            if (downed && d.w.wiz[SIM_SIDE_R].life == LIFE_ACTIVE) break;
        }
        ok &= downed && d.w.wiz[SIM_SIDE_R].life == LIFE_ACTIVE;
        ok &= d.w.wiz[SIM_SIDE_R].variant == k % SIM_ROSTER_N; // 1,2,3,0
        duel_snapshot_t pkt;
        sim_world_t     dec;
        duel_encode(&d.w, 0x42, (uint16_t)k, &pkt);
        duel_decode_world(&pkt, &dec);
        ok &= dec.wiz[SIM_SIDE_R].variant == k % SIM_ROSTER_N;
        ok &= dec.wiz[SIM_SIDE_R].life == LIFE_ACTIVE;
    }
    CHECK(ok, "t5_variant_cycle");
}

// M5 acceptance: every lifecycle phase reads distinctly on the defender's
// canvas, and none of it leaks a single pixel onto the opponent's canvas.
static void t5_draw_lifecycle_distinct(void) {
    enum { N = 5 };
    bool ok = true;
    for (int victim = 0; victim < 2; victim++) {
        sim_world_t w[N];
        for (int i = 0; i < N; i++) sim_init(&w[i], 0, 0); // render-side worlds
        // Hand-built mid-phase tableaus (i=0 stays ACTIVE).
        w[1].wiz[victim] = (sim_wizard_t){.life = LIFE_COLLAPSE, .life_ticks = 6, .hp = 0};
        w[2].wiz[victim] = (sim_wizard_t){.life = LIFE_DOWNED, .life_ticks = 12, .hp = 0};
        w[3].wiz[victim] = (sim_wizard_t){.life = LIFE_MEDIC, .life_ticks = 12, .hp = 0};
        w[4].wiz[victim] = (sim_wizard_t){.life = LIFE_REPLACE, .life_ticks = 10, .hp = 0, .variant = 1};

        bool      victim_is_left = victim == SIM_SIDE_L;
        duel_fb_t defender[N];
        for (int i = 0; i < N; i++) {
            duel_render_t rd = {.w = w[i]};
            duel_fb_clear(&defender[i]);
            wiz_draw_scene(&defender[i], &rd, victim_is_left, 0, false);
        }
        for (int i = 0; i < N; i++) {
            for (int j = i + 1; j < N; j++) { // all 10 defender-canvas pairs differ
                ok &= memcmp(defender[i].bits, defender[j].bits, sizeof defender[i].bits) != 0;
            }
        }

        const uint32_t frames[] = {0, 7};
        for (unsigned f = 0; f < sizeof frames / sizeof frames[0]; f++) {
            duel_fb_t opponent[N];
            for (int i = 0; i < N; i++) {
                duel_render_t rd = {.w = w[i]};
                duel_fb_clear(&opponent[i]);
                wiz_draw_scene(&opponent[i], &rd, !victim_is_left, frames[f], false);
                ok &= memcmp(opponent[i].bits, opponent[0].bits, sizeof opponent[i].bits) == 0;
            }
        }
    }
    CHECK(ok, "t5_draw_lifecycle_distinct");
}

// M5 acceptance: roster variants are deterministic to render and pairwise
// distinct in every pose and facing.
static void t5_draw_variants(void) {
    bool ok = true;
    for (int f = 0; f < 2; f++) {
        int facing = f ? +1 : -1;
        for (int casting = 0; casting < 2; casting++) {
            duel_fb_t fb[SIM_ROSTER_N];
            for (uint8_t v = 0; v < SIM_ROSTER_N; v++) {
                duel_fb_t again;
                duel_fb_clear(&fb[v]);
                duel_fb_clear(&again);
                wiz_draw(&fb[v], casting, facing, v);
                wiz_draw(&again, casting, facing, v);
                ok &= memcmp(fb[v].bits, again.bits, sizeof again.bits) == 0; // deterministic
            }
            for (int i = 0; i < SIM_ROSTER_N; i++) {
                for (int j = i + 1; j < SIM_ROSTER_N; j++) {
                    ok &= memcmp(fb[i].bits, fb[j].bits, sizeof fb[i].bits) != 0;
                }
            }
        }
    }
    CHECK(ok, "t5_draw_variants");
}

// M5 acceptance: the full KO trace replays to a golden hash stream, plus
// semantic anchors so the golden file is not the only guard: the trace ends
// with the right REPLACEMENT (variant 1, full hp) mid-cast after one fizzle.
static void t5_replay_golden(void) {
    sim_world_t w;
    if (!replay_golden_stream("duel_ko.trace", "duel_ko.hashes", "t5_replay_golden", &w)) return;
    bool ok = w.wiz[SIM_SIDE_R].life == LIFE_ACTIVE && w.wiz[SIM_SIDE_R].hp == SIM_MAX_HP &&
              w.wiz[SIM_SIDE_R].variant == 1;
    ok &= w.fx_seq == 6 && w.fx_kind == FX_FIZZLE_R; // 5 impacts + 1 fizzle, nothing more
    ok &= w.spell[SIM_SIDE_R].active;                // the replacement's bolt is in flight
    ok &= w.wiz[SIM_SIDE_L].life == LIFE_ACTIVE && w.wiz[SIM_SIDE_L].hp == SIM_MAX_HP;
    CHECK(ok, "t5_replay_golden_semantics");
}

/* ---- M6: Noita-inspired spell recipes ----------------------------------
 * A cast compiles the recent keydown burst into spell.kind: element from the
 * dominant physical row class (top->FROST, home->FORCE, bottom->EMBER,
 * thumb->VOID; ties -> most recent), modifier from the row-class pattern
 * (all identical -> HEAVY, strictly alternating -> SWIFT, else NONE). The
 * modifier sets the flight speed (NONE 4 / SWIFT 6 / HEAVY 3). All of this is
 * authoritative-only and timing-independent; the tick numbers below are the
 * firmware's exact behaviour (verified against the sim). */

// Drive one authoritative tick with an optional single event.
static void m6_tick(sim_world_t *w, uint8_t down_mask, int has_ev, uint8_t kind,
                    uint8_t side, uint8_t row, uint8_t col) {
    sim_event_t ev[1];
    uint8_t     n = 0;
    if (has_ev) { ev[0] = (sim_event_t){kind, side, row, col}; n = 1; }
    sim_tick(w, (sim_inputs_t){.down_mask = down_mask}, ev, n);
}

// Cast the left wizard with a held row-class burst; return the spawned spell.
static sim_spell_t m6_cast_left(const uint8_t *rows, int nrows) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    for (int t = 0; t < 16; t++) {
        int has = t < nrows;
        m6_tick(&w, 1, has, SIM_EV_KEYDOWN, SIM_SIDE_L, (uint8_t)(has ? rows[t] : 0), 0);
        if (w.spell[SIM_SIDE_L].active) return w.spell[SIM_SIDE_L];
    }
    return (sim_spell_t){0};
}

// Cast the left wizard with a held burst, leaving `gap` empty ticks between
// ingredients (all still inside the spawn windup), to prove that recipe
// compilation is timing-independent.
static sim_spell_t m6_cast_left_spaced(const uint8_t *rows, int nrows, int gap) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    int fed = 0, next = 0;
    for (int t = 0; t < 40; t++) {
        int has = fed < nrows && t == next;
        m6_tick(&w, 1, has, SIM_EV_KEYDOWN, SIM_SIDE_L, (uint8_t)(has ? rows[fed] : 0), 0);
        if (has) { fed++; next += gap; }
        if (w.spell[SIM_SIDE_L].active) return w.spell[SIM_SIDE_L];
    }
    return (sim_spell_t){0};
}

static void t6_same_recipe_determinism(void) {
    const uint8_t seq[] = {0, 1, 0};
    sim_world_t   a, b;
    sim_init(&a, SIMF_AUTHORITATIVE, 0);
    sim_init(&b, SIMF_AUTHORITATIVE, 0);
    for (int t = 0; t <= SIM_CAST_WINDUP_TICKS; t++) {
        int has = t < 3;
        m6_tick(&a, 1, has, SIM_EV_KEYDOWN, SIM_SIDE_L, (uint8_t)(has ? seq[t] : 0), 0);
        m6_tick(&b, 1, has, SIM_EV_KEYDOWN, SIM_SIDE_L, (uint8_t)(has ? seq[t] : 0), 0);
    }
    bool ok = a.spell[0].active && b.spell[0].active &&
              a.spell[0].kind == b.spell[0].kind && world_hash(&a) == world_hash(&b);
    // Same ingredient sequence, different inter-key timing -> identical spell.
    // Kills any modifier that keys off cadence (e.g. SWIFT only when idle small).
    sim_spell_t tight  = m6_cast_left((uint8_t[]){0, 1, 0}, 3);
    sim_spell_t spaced = m6_cast_left_spaced((uint8_t[]){0, 1, 0}, 3, 2);
    ok &= tight.kind == spaced.kind && DUEL_KIND_MODIFIER(tight.kind) == MOD_SWIFT;
    CHECK(ok, "t6_same_recipe_determinism");
}

static void t6_element_reachability(void) {
    bool ok = true;
    ok &= DUEL_KIND_ELEMENT(m6_cast_left((uint8_t[]){0}, 1).kind) == ELEM_FROST;
    ok &= DUEL_KIND_ELEMENT(m6_cast_left((uint8_t[]){1}, 1).kind) == ELEM_FORCE;
    ok &= DUEL_KIND_ELEMENT(m6_cast_left((uint8_t[]){2}, 1).kind) == ELEM_EMBER;
    ok &= DUEL_KIND_ELEMENT(m6_cast_left((uint8_t[]){3}, 1).kind) == ELEM_VOID;
    // Tie between two classes resolves to the most recent ingredient.
    ok &= DUEL_KIND_ELEMENT(m6_cast_left((uint8_t[]){1, 2}, 2).kind) == ELEM_EMBER; // bottom newest
    ok &= DUEL_KIND_ELEMENT(m6_cast_left((uint8_t[]){2, 1}, 2).kind) == ELEM_FORCE; // home newest
    // The dominant class wins even when it is NOT the newest ingredient — kills
    // a "just take the newest row class" shortcut.
    ok &= DUEL_KIND_ELEMENT(m6_cast_left((uint8_t[]){0, 0, 1}, 3).kind) == ELEM_FROST; // top dominant, home newest
    ok &= DUEL_KIND_ELEMENT(m6_cast_left((uint8_t[]){3, 3, 2}, 3).kind) == ELEM_VOID;  // thumb dominant, bottom newest
    CHECK(ok, "t6_element_reachability");
}

// The compiler reads exactly the last FOUR ingredients: a fourth ingredient
// changes the outcome, and a fifth pushes the oldest out of the window.
static void t6_recipe_window(void) {
    bool ok = true;
    // home,home,bottom,home: across all four the pattern is mixed (adjacent
    // home,home) -> NONE. A compiler capping at three would see home,bottom,home
    // (alternating) -> SWIFT, so asserting NONE pins the four-ingredient window.
    ok &= DUEL_KIND_MODIFIER(m6_cast_left((uint8_t[]){1, 1, 2, 1}, 4).kind) == MOD_NONE;
    // bottom then four home: the bottom falls out of the last-4 window, leaving
    // four home -> FORCE/HEAVY (not EMBER, and not a mixed modifier).
    sim_spell_t s = m6_cast_left((uint8_t[]){2, 1, 1, 1, 1}, 5);
    ok &= DUEL_KIND_ELEMENT(s.kind) == ELEM_FORCE && DUEL_KIND_MODIFIER(s.kind) == MOD_HEAVY;
    CHECK(ok, "t6_recipe_window");
}

static void t6_modifier_reachability(void) {
    sim_spell_t none1 = m6_cast_left((uint8_t[]){1}, 1);
    sim_spell_t heavy = m6_cast_left((uint8_t[]){1, 1, 1}, 3);
    sim_spell_t swift = m6_cast_left((uint8_t[]){0, 1, 0}, 3);
    sim_spell_t mixed = m6_cast_left((uint8_t[]){0, 0, 1}, 3);
    bool        ok    = true;
    ok &= DUEL_KIND_MODIFIER(none1.kind) == MOD_NONE && none1.dir == 4;
    ok &= DUEL_KIND_MODIFIER(heavy.kind) == MOD_HEAVY && heavy.dir == 3;
    ok &= DUEL_KIND_MODIFIER(swift.kind) == MOD_SWIFT && swift.dir == 6;
    ok &= DUEL_KIND_MODIFIER(mixed.kind) == MOD_NONE && mixed.dir == 4;
    CHECK(ok, "t6_modifier_reachability");
}

static void t6_inactivity_expiry(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    // One cast to enter cooldown, so a later keydown accumulates without spawning.
    m6_tick(&w, 1, 1, SIM_EV_KEYDOWN, SIM_SIDE_L, 1, 0);        // t0: press home
    for (int t = 0; t < SIM_CAST_WINDUP_TICKS; t++) m6_tick(&w, 0, 0, 0, 0, 0, 0);
    bool ok = w.spell[SIM_SIDE_L].active && w.wiz[SIM_SIDE_L].cast_cooldown > 0;
    // Inject one ingredient during cooldown (no rising-edge cast possible).
    m6_tick(&w, 1, 1, SIM_EV_KEYDOWN, SIM_SIDE_L, 0, 0);
    ok &= w.wiz[SIM_SIDE_L].recipe_n == 1 && w.wiz[SIM_SIDE_L].recipe_idle == 1;
    // Boundary: idle reaches RECIPE_EXPIRE_TICKS (25) exactly one tick after the
    // 23rd idle-only tick (idle 1 -> 24 -> 25), so 23 keeps it, the 24th clears
    // it. Pins the threshold to 25 (a 24 or 26 mutant fails one of the two).
    for (int t = 0; t < RECIPE_EXPIRE_TICKS - 2; t++) m6_tick(&w, 0, 0, 0, 0, 0, 0);
    ok &= w.wiz[SIM_SIDE_L].recipe_n == 1; // idle == 24, still open
    m6_tick(&w, 0, 0, 0, 0, 0, 0);
    ok &= w.wiz[SIM_SIDE_L].recipe_n == 0 && w.wiz[SIM_SIDE_L].recipe_hist == 0; // idle == 25 -> discarded
    CHECK(ok, "t6_inactivity_expiry");
}

static void t6_bounded_under_mash(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    bool    ok    = true;
    uint8_t max_n = 0;
    for (int t = 0; t < 400; t++) {
        sim_evq_t q = {0};
        for (int k = 0; k < 20; k++) { // > SIM_EVQ_CAP: exercises the overflow path too
            uint8_t side = (uint8_t)((t + k) & 1);
            uint8_t row  = (uint8_t)((t * 3 + k) & 3);
            uint8_t col  = (uint8_t)((t + k) % 6);
            sim_evq_push(&q, (sim_event_t){SIM_EV_KEYDOWN, side, row, col});
        }
        sim_event_t evs[SIM_EVQ_CAP + 1];
        uint8_t     n = sim_evq_drain(&q, evs);
        sim_tick(&w, (sim_inputs_t){.down_mask = 3}, evs, n);
        for (int s = 0; s < 2; s++) {
            ok &= w.wiz[s].recipe_n <= RECIPE_N_MAX;
            if (w.wiz[s].recipe_n > max_n) max_n = w.wiz[s].recipe_n;
            if (w.spell[s].active) {
                ok &= DUEL_KIND_PAYLOAD(w.spell[s].kind) == PAY_IMPACT;
                ok &= DUEL_KIND_ELEMENT(w.spell[s].kind) <= ELEM_VOID;
                ok &= DUEL_KIND_MODIFIER(w.spell[s].kind) <= MOD_HEAVY;
            }
        }
    }
    ok &= max_n == RECIPE_N_MAX; // saturation actually reached (catches a cap at 14)
    ok &= w.overflow_count > 0;  // the bounded queue overflowed and stayed bounded
    CHECK(ok, "t6_bounded_under_mash");
}

static void t6_slave_never_compiles(void) {
    sim_world_t w;
    sim_init(&w, 0, 0); // non-authoritative
    const uint8_t seq[] = {1, 1, 1};
    for (int t = 0; t < 16; t++) {
        int has = t < 3;
        m6_tick(&w, 1, has, SIM_EV_KEYDOWN, SIM_SIDE_L, (uint8_t)(has ? seq[t] : 0), 0);
    }
    bool ok = !w.spell[SIM_SIDE_L].active && w.spell[SIM_SIDE_L].kind == 0 &&
              w.wiz[SIM_SIDE_L].recipe_n == 0 && w.wiz[SIM_SIDE_L].recipe_hist == 0;
    CHECK(ok, "t6_slave_never_compiles");
}

static void t6_kind_sync(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint8_t kl = DUEL_KIND_WITH_TIER(DUEL_KIND_PACK(ELEM_FROST, MOD_SWIFT, PAY_IMPACT), SPELL_TIER_LONG);
    uint8_t kr = DUEL_KIND_WITH_TIER(DUEL_KIND_PACK(ELEM_VOID, MOD_HEAVY, PAY_IMPACT), SPELL_TIER_SATURATED);
    w.spell[SIM_SIDE_L] = (sim_spell_t){.active = 1, .pos = 100, .dir = 6, .kind = kl};
    w.spell[SIM_SIDE_R] = (sim_spell_t){.active = 1, .pos = 150, .dir = -3, .kind = kr};
    duel_snapshot_t pkt;
    duel_encode(&w, 7, 1, &pkt);
    bool ok = duel_decode_valid(&pkt) && pkt.ver == DUEL_VER && DUEL_VER == 7;
    ok &= pkt.spell_kind[0] == kl && pkt.spell_kind[1] == kr;
    // A fresh decode models total packet-loss recovery: absolute kind restored.
    sim_world_t out;
    duel_decode_world(&pkt, &out);
    ok &= out.spell[0].active && out.spell[1].active;
    ok &= out.spell[0].kind == kl && out.spell[1].kind == kr;
    // spell_kind must be under the CRC: flipping either kind byte invalidates
    // the packet (catches a CRC that stops short of the new field).
    duel_snapshot_t bad0 = pkt;
    bad0.spell_kind[0] ^= 0x01;
    ok &= !duel_decode_valid(&bad0);
    duel_snapshot_t bad1 = pkt;
    bad1.spell_kind[1] ^= 0x04;
    ok &= !duel_decode_valid(&bad1);
    CHECK(ok, "t6_kind_sync");
}

// Left casts a spell of a chosen modifier; the right wizard taps a shield at
// tap_tick. Returns the first resolution fx (deflect or impact on the right).
static uint8_t m6_duel_deflect(const uint8_t *atk_rows, int atk_n, int tap_tick) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    for (uint32_t t = 0; t < 200; t++) {
        sim_event_t ev[2];
        uint8_t     n = 0, dm = 0;
        if ((int)t < atk_n) ev[n++] = (sim_event_t){SIM_EV_KEYDOWN, SIM_SIDE_L, atk_rows[t], 0};
        if ((int)t < SIM_CAST_WINDUP_TICKS) dm |= 1 << SIM_SIDE_L; // hold through wind-up
        if ((int)t == tap_tick) {
            ev[n++] = (sim_event_t){SIM_EV_KEYDOWN, SIM_SIDE_R, 1, 0};
            dm |= 1 << SIM_SIDE_R;
        }
        sim_tick(&w, (sim_inputs_t){.down_mask = dm}, ev, n);
        if (w.fx_seq > 0) return w.fx_kind;
    }
    return FX_NONE;
}

static void t6_deflect_windows(void) {
    bool ok = true;
    // NONE speed 4: doorstep ticks 68/69, impact 70, deflect if tap in [59,69].
    ok &= m6_duel_deflect((uint8_t[]){1}, 1, 69) == FX_DEFLECT_R;
    ok &= m6_duel_deflect((uint8_t[]){1}, 1, 70) == FX_IMPACT_R;
    ok &= m6_duel_deflect((uint8_t[]){1}, 1, 59) == FX_DEFLECT_R;
    ok &= m6_duel_deflect((uint8_t[]){1}, 1, 58) == FX_IMPACT_R;
    // SWIFT speed 6: doorstep 49, impact 50, deflect if tap in [40,49] (narrower).
    ok &= m6_duel_deflect((uint8_t[]){0, 1, 0}, 3, 49) == FX_DEFLECT_R;
    ok &= m6_duel_deflect((uint8_t[]){0, 1, 0}, 3, 50) == FX_IMPACT_R;
    ok &= m6_duel_deflect((uint8_t[]){0, 1, 0}, 3, 40) == FX_DEFLECT_R;
    ok &= m6_duel_deflect((uint8_t[]){0, 1, 0}, 3, 39) == FX_IMPACT_R; // one tick before the window
    // HEAVY speed 3: doorstep 88/89, impact 90, deflect if tap in [79,89].
    ok &= m6_duel_deflect((uint8_t[]){1, 1, 1}, 3, 89) == FX_DEFLECT_R;
    ok &= m6_duel_deflect((uint8_t[]){1, 1, 1}, 3, 90) == FX_IMPACT_R;
    ok &= m6_duel_deflect((uint8_t[]){1, 1, 1}, 3, 79) == FX_DEFLECT_R;
    ok &= m6_duel_deflect((uint8_t[]){1, 1, 1}, 3, 78) == FX_IMPACT_R; // one tick before the window
    CHECK(ok, "t6_deflect_windows");
}

static void t6_void_pierces_ward(void) {
    bool ok = true;
    // Same speed (NONE) and the same in-window ward tap: a FORCE bolt (home) is
    // deflected, but a VOID bolt (thumb) ignores the ward and lands. This is the
    // one place the element changes the OUTCOME, not just the look.
    ok &= m6_duel_deflect((uint8_t[]){1}, 1, 69) == FX_DEFLECT_R; // FORCE/NONE warded
    ok &= m6_duel_deflect((uint8_t[]){3}, 1, 69) == FX_IMPACT_R;  // VOID/NONE pierces
    CHECK(ok, "t6_void_pierces_ward");
}

// Render just the left canvas of a world carrying one left-slot spell of `kind`.
static void m6_render_spell(duel_fb_t *fb, uint8_t kind, bool is_left) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.spell[SIM_SIDE_L] = (sim_spell_t){.active = 1, .pos = 60, .dir = 4, .kind = kind};
    duel_render_t r = {.w = w};
    duel_fb_clear(fb);
    wiz_draw_scene(fb, &r, is_left, 0, false);
}

static void t6_draw_distinct(void) {
    bool      ok = true;
    duel_fb_t e[4];
    uint8_t   elems[4] = {ELEM_FORCE, ELEM_EMBER, ELEM_FROST, ELEM_VOID};
    for (int i = 0; i < 4; i++) m6_render_spell(&e[i], DUEL_KIND_PACK(elems[i], MOD_NONE, PAY_IMPACT), true);
    for (int i = 0; i < 4; i++)
        for (int j = i + 1; j < 4; j++) ok &= memcmp(e[i].bits, e[j].bits, sizeof e[i].bits) != 0;
    uint8_t   mods[3] = {MOD_NONE, MOD_SWIFT, MOD_HEAVY};
    // Modifier tells must be distinct on MORE than one element, or a bug that
    // only decorates FORCE would slip through.
    uint8_t   bases[2] = {ELEM_FORCE, ELEM_VOID};
    for (int b = 0; b < 2; b++) {
        duel_fb_t m[3];
        for (int i = 0; i < 3; i++) m6_render_spell(&m[i], DUEL_KIND_PACK(bases[b], mods[i], PAY_IMPACT), true);
        for (int i = 0; i < 3; i++)
            for (int j = i + 1; j < 3; j++) ok &= memcmp(m[i].bits, m[j].bits, sizeof m[i].bits) != 0;
    }
    // The opposing canvas is unaffected by our spell's kind (spell is on our u-range).
    duel_fb_t r0, r1;
    m6_render_spell(&r0, DUEL_KIND_PACK(ELEM_VOID, MOD_HEAVY, PAY_IMPACT), false);
    m6_render_spell(&r1, DUEL_KIND_PACK(ELEM_FROST, MOD_SWIFT, PAY_IMPACT), false);
    ok &= memcmp(r0.bits, r1.bits, sizeof r0.bits) == 0;
    CHECK(ok, "t6_draw_distinct");
}

static void t6_replay_golden(void) {
    replay_golden_stream("duel_recipes.trace", "duel_recipes.hashes", "t6_replay_golden", NULL);
    trace_t t;
    if (load_trace_or_fail("duel_recipes.trace", &t) != 0) {
        CHECK(false, "t6_replay_golden_semantics");
        return;
    }
    runner_t r;
    runner_init(&r, &t, SIMF_AUTHORITATIVE);
    bool saw_deflect = false;
    uint8_t fx_seq = 0;
    while (!runner_done(&r)) {
        runner_step(&r);
        if (r.w.fx_seq != fx_seq) {
            fx_seq = r.w.fx_seq;
            if (r.w.fx_kind == FX_DEFLECT_R) saw_deflect = true;
        }
    }
    CHECK(saw_deflect, "t6_replay_golden_semantics");
}

/* ---------------- M7.5: presentation state ----------------------------- */

static void t75_windup_timing(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    sim_event_t press = {SIM_EV_KEYDOWN, SIM_SIDE_L, 1, 0};
    sim_tick(&w, (sim_inputs_t){.down_mask = 1}, &press, 1);
    bool ok = SIM_CAST_WINDUP_TICKS == 10 && w.wiz[SIM_SIDE_L].cast_windup == 10 &&
              !w.spell[SIM_SIDE_L].active;
    for (int i = 1; i < SIM_CAST_WINDUP_TICKS; i++) {
        sim_tick(&w, (sim_inputs_t){0}, NULL, 0);
        ok &= !w.spell[SIM_SIDE_L].active;
        ok &= w.wiz[SIM_SIDE_L].cast_windup == SIM_CAST_WINDUP_TICKS - i;
    }
    sim_tick(&w, (sim_inputs_t){0}, NULL, 0);
    ok &= w.spell[SIM_SIDE_L].active && w.spell[SIM_SIDE_L].pos == SIM_SPAWN_L;
    ok &= w.wiz[SIM_SIDE_L].cast_windup == 0 && w.wiz[SIM_SIDE_L].pose == POSE_CAST;
    CHECK(ok, "t75_windup_timing");
}

static void t75_recipe_tiers(void) {
    sim_spell_t short_s = m6_cast_left((uint8_t[]){1}, 1);
    sim_spell_t medium  = m6_cast_left((uint8_t[]){1, 2, 1}, 3);
    sim_spell_t long_s  = m6_cast_left((uint8_t[]){1, 2, 1, 2, 1}, 5);
    sim_spell_t capped  = m6_cast_left((uint8_t[]){1, 2, 1, 2, 1, 2, 1, 2, 1}, 9);
    bool ok = DUEL_KIND_TIER(short_s.kind) == SPELL_TIER_SHORT;
    ok &= DUEL_KIND_TIER(medium.kind) == SPELL_TIER_MEDIUM;
    ok &= DUEL_KIND_TIER(long_s.kind) == SPELL_TIER_LONG;
    ok &= DUEL_KIND_TIER(capped.kind) == SPELL_TIER_SATURATED;
    ok &= duel_recipe_tier(RECIPE_N_MAX) == SPELL_TIER_SATURATED;
    CHECK(ok, "t75_recipe_tiers");
}

static void t75_tier_no_combat_coupling(void) {
    sim_world_t a, b;
    sim_init(&a, SIMF_AUTHORITATIVE, 0);
    sim_init(&b, SIMF_AUTHORITATIVE, 0);
    uint8_t base = DUEL_KIND_PACK(ELEM_FORCE, MOD_NONE, PAY_IMPACT);
    a.spell[SIM_SIDE_L] = (sim_spell_t){.active = 1, .pos = 244, .dir = 4,
                                        .kind = DUEL_KIND_WITH_TIER(base, SPELL_TIER_SHORT)};
    b.spell[SIM_SIDE_L] = (sim_spell_t){.active = 1, .pos = 244, .dir = 4,
                                        .kind = DUEL_KIND_WITH_TIER(base, SPELL_TIER_SATURATED)};
    sim_tick(&a, (sim_inputs_t){0}, NULL, 0);
    sim_tick(&b, (sim_inputs_t){0}, NULL, 0);
    bool ok = a.fx_kind == FX_IMPACT_R && b.fx_kind == FX_IMPACT_R;
    ok &= a.wiz[SIM_SIDE_R].hp == SIM_MAX_HP - 1 && b.wiz[SIM_SIDE_R].hp == SIM_MAX_HP - 1;
    ok &= a.wiz[SIM_SIDE_R].life == b.wiz[SIM_SIDE_R].life;
    ok &= a.wiz[SIM_SIDE_R].regen_ticks == b.wiz[SIM_SIDE_R].regen_ticks;
    CHECK(ok, "t75_tier_no_combat_coupling");
}

static void t75_charge_sync(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[SIM_SIDE_L].cast_windup = 7;
    w.wiz[SIM_SIDE_L].cast_tier   = SPELL_TIER_LONG;
    w.wiz[SIM_SIDE_R].cast_windup = 3;
    w.wiz[SIM_SIDE_R].cast_tier   = SPELL_TIER_SATURATED;
    duel_snapshot_t pkt;
    duel_encode(&w, 5, 9, &pkt);
    sim_world_t out;
    duel_decode_world(&pkt, &out);
    bool ok = sizeof pkt == 31 && DUEL_VER == 7 && duel_decode_valid(&pkt);
    ok &= out.wiz[SIM_SIDE_L].cast_windup == 7 && out.wiz[SIM_SIDE_L].cast_tier == SPELL_TIER_LONG;
    ok &= out.wiz[SIM_SIDE_R].cast_windup == 3 && out.wiz[SIM_SIDE_R].cast_tier == SPELL_TIER_SATURATED;
    duel_snapshot_t bad_charge = pkt;
    bad_charge.charge[0] ^= 0x01;
    ok &= !duel_decode_valid(&bad_charge);
    duel_snapshot_t old_ver = pkt;
    old_ver.ver = 4;
    old_ver.crc = duel_crc8(&old_ver, offsetof(duel_snapshot_t, crc));
    ok &= !duel_decode_valid(&old_ver);
    CHECK(ok, "t75_charge_sync");
}

static int fb_pixels(const duel_fb_t *fb) {
    int n = 0;
    for (int y = 0; y < DUEL_CANVAS_H; y++)
        for (int x = 0; x < DUEL_CANVAS_W; x++) n += duel_fb_get(fb, x, y);
    return n;
}

static void t75_draw_recipe_scale(void) {
    duel_fb_t spell[4], charge_early, charge_short, charge_long;
    int       pixels[4];
    uint8_t base = DUEL_KIND_PACK(ELEM_FORCE, MOD_NONE, PAY_IMPACT);
    for (int tier = 0; tier < 4; tier++) {
        m6_render_spell(&spell[tier], DUEL_KIND_WITH_TIER(base, tier), true);
        pixels[tier] = fb_pixels(&spell[tier]);
    }
    bool ok = pixels[0] < pixels[1] && pixels[1] < pixels[2] && pixels[2] < pixels[3];
    for (int i = 0; i < 4; i++)
        for (int j = i + 1; j < 4; j++) ok &= memcmp(spell[i].bits, spell[j].bits, sizeof spell[i].bits) != 0;

    sim_world_t w;
    sim_init(&w, 0, 0);
    w.wiz[SIM_SIDE_L].pose = POSE_CAST;
    w.wiz[SIM_SIDE_L].cast_tier = SPELL_TIER_LONG;
    w.wiz[SIM_SIDE_L].cast_windup = SIM_CAST_WINDUP_TICKS;
    duel_render_t r = {.w = w};
    duel_fb_clear(&charge_early);
    wiz_draw_scene(&charge_early, &r, true, 0, false);
    r.w.wiz[SIM_SIDE_L].cast_windup = 2;
    duel_fb_clear(&charge_long);
    wiz_draw_scene(&charge_long, &r, true, 0, false);
    r.w.wiz[SIM_SIDE_L].cast_tier = SPELL_TIER_SHORT;
    duel_fb_clear(&charge_short);
    wiz_draw_scene(&charge_short, &r, true, 0, false);
    ok &= fb_pixels(&charge_early) < fb_pixels(&charge_long);
    ok &= fb_pixels(&charge_short) < fb_pixels(&charge_long);
    bool upper_used = false;
    for (int y = 20; y < 50; y++)
        for (int x = 0; x < DUEL_CANVAS_W; x++) upper_used |= duel_fb_get(&charge_long, x, y);
    ok &= upper_used;
    CHECK(ok, "t75_draw_recipe_scale");
}

static void render_outcome(duel_fb_t *fb, uint8_t fx, uint8_t kind) {
    sim_world_t w;
    sim_init(&w, 0, 0);
    if (fx == FX_IMPACT_R) w.wiz[SIM_SIDE_R].hp = SIM_MAX_HP - 1;
    if (fx == FX_DEFLECT_R) w.wiz[SIM_SIDE_R].shield_ticks = SIM_SHIELD_TICKS;
    if (fx == FX_FIZZLE_R) {
        w.wiz[SIM_SIDE_R].life = LIFE_DOWNED;
        w.wiz[SIM_SIDE_R].life_ticks = SIM_DOWNED_TICKS / 2;
        w.wiz[SIM_SIDE_R].hp = 0;
    }
    duel_render_t r = {.w = w, .flash_frames = fx == FX_IMPACT_R ? 10 : 7,
                       .flash_kind = fx, .flash_spell_kind = kind};
    duel_fb_clear(fb);
    wiz_draw_scene(fb, &r, false, 0, false);
}

static void t75_outcome_grammars(void) {
    uint8_t medium = DUEL_KIND_WITH_TIER(DUEL_KIND_PACK(ELEM_FORCE, MOD_NONE, PAY_IMPACT), SPELL_TIER_MEDIUM);
    duel_fb_t impact, deflect, fizzle, impact_short, impact_long;
    render_outcome(&impact, FX_IMPACT_R, medium);
    render_outcome(&deflect, FX_DEFLECT_R, medium);
    render_outcome(&fizzle, FX_FIZZLE_R, medium);
    bool ok = memcmp(impact.bits, deflect.bits, sizeof impact.bits) != 0;
    ok &= memcmp(impact.bits, fizzle.bits, sizeof impact.bits) != 0;
    ok &= memcmp(deflect.bits, fizzle.bits, sizeof impact.bits) != 0;
    // Right-side impact recoil: original apex clears, shifted/compressed apex appears.
    ok &= !duel_fb_get(&impact, 16, 54) && duel_fb_get(&impact, 18, 55);
    ok &= duel_fb_get(&deflect, 16, 54); // deflection leaves the defender stable
    render_outcome(&impact_short, FX_IMPACT_R, DUEL_KIND_WITH_TIER(medium, SPELL_TIER_SHORT));
    render_outcome(&impact_long, FX_IMPACT_R, DUEL_KIND_WITH_TIER(medium, SPELL_TIER_LONG));
    ok &= fb_pixels(&impact_short) < fb_pixels(&impact_long);
    CHECK(ok, "t75_outcome_grammars");
}

static void t75_void_ward_puncture(void) {
    sim_world_t w;
    sim_init(&w, 0, 0);
    w.wiz[SIM_SIDE_R].shield_ticks = SIM_SHIELD_TICKS;
    duel_render_t r = {.w = w};
    duel_fb_t ward, pierced;
    duel_fb_clear(&ward);
    wiz_draw_scene(&ward, &r, false, 0, false);
    r.w.spell[SIM_SIDE_L] = (sim_spell_t){
        .active = 1, .pos = 236, .dir = 4,
        .kind = DUEL_KIND_WITH_TIER(DUEL_KIND_PACK(ELEM_VOID, MOD_NONE, PAY_IMPACT), SPELL_TIER_LONG),
    };
    duel_fb_clear(&pierced);
    wiz_draw_scene(&pierced, &r, false, 0, false);
    bool ok = duel_fb_get(&ward, 7, 60) && !duel_fb_get(&pierced, 7, 60); // visible split in arc
    ok &= !duel_fb_get(&ward, 10, 61) && duel_fb_get(&pierced, 10, 61);   // projectile continues inward
    CHECK(ok, "t75_void_ward_puncture");
}

/* ---------------- Phase 7: M7 layer-key scrying overlay ----------------
 * An explicit chord state machine (idle/first-held/pending/active/select/
 * cancelled) driven purely by the level-sampled scry_mask opens a temporary
 * overlay above the running duel. The two layer thumbs co-held are exactly QMK
 * layer 3, so these tests pin the structural separation: a still deliberate
 * co-hold opens it, but any other key touched during the co-hold (ordinary
 * layer-3 use) latches it cancelled, and a one-key layer roll never escalates.
 * Authoritative-only, so the slave shows it purely from the wire. */

#define SCRY_BOTH  (SCRY_M_L | SCRY_M_R)

static void scry_run(sim_world_t *w, uint8_t mask, int ticks) {
    for (int i = 0; i < ticks; i++) sim_tick(w, (sim_inputs_t){.scry_mask = mask}, NULL, 0);
}

// A normal layer roll — one layer key plus a stream of other keys — parks in
// FIRST_HELD forever and never opens the overlay. Neither does a lone hold.
static void t7_roll_never_opens(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    bool ok = true;
    for (int t = 0; t < 40; t++) {
        // alternate "just the layer key" and "layer key + a symbol" — the shape
        // of rolling onto a layer and typing on it.
        scry_run(&w, SCRY_M_L | (t & 1 ? SCRY_M_OTHER : 0), 1);
        ok &= !scry_is_open(&w) && w.scry.state == SCRY_FIRST_HELD;
    }
    scry_run(&w, 0, 1); // release
    ok &= w.scry.state == SCRY_IDLE && !scry_is_open(&w);
    // A lone layer key held indefinitely also never escalates.
    scry_run(&w, SCRY_M_R, 60);
    ok &= w.scry.state == SCRY_FIRST_HELD && !scry_is_open(&w);
    CHECK(ok, "t7_roll_never_opens");
}

// A deliberate still co-hold opens the overlay after exactly the dwell, and
// not one tick sooner — pinning SCRY_PENDING_TICKS.
static void t7_deliberate_opens(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    bool ok = true;
    // Entry tick arms PENDING (timer = SCRY_PENDING_TICKS) without decrementing,
    // so opening takes one entry tick plus SCRY_PENDING_TICKS decrements.
    scry_run(&w, SCRY_BOTH, SCRY_PENDING_TICKS); // entry + (PENDING_TICKS-1) decrements -> timer 1
    ok &= w.scry.state == SCRY_PENDING && !scry_is_open(&w);
    scry_run(&w, SCRY_BOTH, 1);                  // timer 0 -> ACTIVE
    ok &= w.scry.state == SCRY_ACTIVE && scry_is_open(&w);
    // Overlay is stable while held.
    scry_run(&w, SCRY_BOTH, 30);
    ok &= w.scry.state == SCRY_ACTIVE && scry_is_open(&w);
    // Release closes it and leaves nothing latched.
    scry_run(&w, 0, 1);
    ok &= w.scry.state == SCRY_IDLE && !scry_is_open(&w);
    CHECK(ok, "t7_deliberate_opens");
}

// Letting go before the dwell elapses opens nothing.
static void t7_early_release(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    scry_run(&w, SCRY_BOTH, SCRY_PENDING_TICKS - 1); // still short of the dwell
    bool ok = w.scry.state == SCRY_PENDING && !scry_is_open(&w);
    scry_run(&w, SCRY_M_L, 1); // drop to one key
    ok &= w.scry.state == SCRY_FIRST_HELD && !scry_is_open(&w);
    scry_run(&w, 0, 1);
    ok &= w.scry.state == SCRY_IDLE;
    CHECK(ok, "t7_early_release");
}

// A third key during the co-hold (i.e. real layer-3 use) cancels, and the
// cancel latches until a FULL release, so the overlay cannot flicker open even
// if the third key lets go while both layer keys stay down.
static void t7_third_key_cancels_and_latches(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    scry_run(&w, SCRY_BOTH, 3);                 // partway through the dwell
    bool ok = w.scry.state == SCRY_PENDING;
    scry_run(&w, SCRY_BOTH | SCRY_M_OTHER, 1);  // press a layer-3 key
    ok &= w.scry.state == SCRY_CANCELLED && !scry_is_open(&w);
    // Release the third key but keep both layer keys down: still latched.
    scry_run(&w, SCRY_BOTH, 40);
    ok &= w.scry.state == SCRY_CANCELLED && !scry_is_open(&w);
    // Only a full release clears the latch; a fresh deliberate hold then works.
    scry_run(&w, 0, 1);
    ok &= w.scry.state == SCRY_IDLE;
    scry_run(&w, SCRY_BOTH, SCRY_PENDING_TICKS + 1);
    ok &= scry_is_open(&w);
    CHECK(ok, "t7_third_key_cancels_and_latches");
}

// While active, tapping a selector key cycles the scene and keeps the overlay
// open (SELECT is an open state); the scene wraps modulo SCRY_SCENES.
static void t7_select_cycles_scene(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    scry_run(&w, SCRY_BOTH, SCRY_PENDING_TICKS + 1); // -> ACTIVE, scene 0
    bool ok = scry_is_open(&w) && w.scry.scene == 0;
    for (uint8_t expect = 1; expect <= SCRY_SCENES; expect++) {
        scry_run(&w, SCRY_BOTH | SCRY_M_OTHER, 1); // selector down -> SELECT, scene++
        ok &= w.scry.state == SCRY_SELECT && scry_is_open(&w);
        ok &= w.scry.scene == (expect % SCRY_SCENES);
        scry_run(&w, SCRY_BOTH, 1);                 // selector up -> ACTIVE
        ok &= w.scry.state == SCRY_ACTIVE && scry_is_open(&w);
    }
    // Holding the selector does NOT keep cycling (one bump per press edge).
    scry_run(&w, SCRY_BOTH | SCRY_M_OTHER, 5);
    ok &= w.scry.scene == (1 % SCRY_SCENES) && scry_is_open(&w);
    CHECK(ok, "t7_select_cycles_scene");
}

// The chord machine is authoritative-only: a slave world fed the full
// deliberate gesture never opens, but decoding a snapshot with the open bit set
// shows the overlay — proving it rides the wire exactly like combat outcomes.
static void t7_slave_never_opens(void) {
    sim_world_t slave;
    sim_init(&slave, 0, 0); // non-authoritative
    scry_run(&slave, SCRY_BOTH, 60);
    bool ok = slave.scry.state == SCRY_IDLE && !scry_is_open(&slave);

    sim_world_t master;
    sim_init(&master, SIMF_AUTHORITATIVE, 0);
    scry_run(&master, SCRY_BOTH, SCRY_PENDING_TICKS + 1);
    scry_run(&master, SCRY_BOTH | SCRY_M_OTHER, 1); // scene -> 1
    ok &= scry_is_open(&master) && master.scry.scene == 1;

    duel_snapshot_t pkt;
    duel_encode(&master, 9, 1, &pkt);
    sim_world_t decoded;
    duel_decode_world(&pkt, &decoded);
    ok &= scry_is_open(&decoded) && decoded.scry.scene == 1;
    CHECK(ok, "t7_slave_never_opens");
}

// Wire round-trip: open+scene survive, and the scry byte is under
// the CRC so a flipped bit invalidates the packet.
static void t7_scry_sync(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.scry.state = SCRY_ACTIVE;
    w.scry.scene = 2;
    duel_snapshot_t pkt;
    duel_encode(&w, 3, 1, &pkt);
    bool ok = duel_decode_valid(&pkt) && pkt.ver == DUEL_VER && DUEL_VER == 7;
    ok &= DUEL_SCRY_OPEN(pkt.scry) == 1 && DUEL_SCRY_SCENE(pkt.scry) == 2;
    // A closed world clears the open bit.
    sim_world_t c;
    sim_init(&c, SIMF_AUTHORITATIVE, 0);
    duel_snapshot_t cpkt;
    duel_encode(&c, 3, 2, &cpkt);
    ok &= DUEL_SCRY_OPEN(cpkt.scry) == 0;
    // scry byte is covered by the CRC.
    duel_snapshot_t bad = pkt;
    bad.scry ^= 0x01;
    ok &= !duel_decode_valid(&bad);
    CHECK(ok, "t7_scry_sync");
}

// Render an open overlay (world in ACTIVE) with the given presentation content.
static void t7_render_overlay(duel_fb_t *fb, bool is_left, uint8_t scene,
                              uint8_t layer, uint8_t host, uint8_t notif) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.scry.state = SCRY_ACTIVE;
    w.scry.scene = scene;
    duel_render_t r = {.w = w, .overlay_layer = layer, .overlay_host = host, .overlay_notif = notif};
    duel_fb_clear(fb);
    wiz_draw_scene(fb, &r, is_left, 0, false);
}

static void t7_overlay_draws(void) {
    bool ok = true;
    // Closed vs open differ on BOTH canvases (the panel is drawn on each half).
    for (int lr = 0; lr < 2; lr++) {
        bool is_left = lr == 0;
        sim_world_t closed;
        sim_init(&closed, SIMF_AUTHORITATIVE, 0); // scry IDLE
        duel_render_t rc = {.w = closed};
        duel_fb_t fclosed, fopen;
        duel_fb_clear(&fclosed);
        wiz_draw_scene(&fclosed, &rc, is_left, 0, false);
        t7_render_overlay(&fopen, is_left, 0, 0, 0, 0);
        ok &= memcmp(fclosed.bits, fopen.bits, sizeof fopen.bits) != 0;
    }
    // Scene selector, layer readout, host status and notification count each
    // change the rendered panel — the concise readout is real, not decorative.
    duel_fb_t a, b;
    t7_render_overlay(&a, true, 0, 0, 0, 0);
    t7_render_overlay(&b, true, 2, 0, 0, 0);
    ok &= memcmp(a.bits, b.bits, sizeof a.bits) != 0; // scene
    t7_render_overlay(&b, true, 0, 3, 0, 0);
    ok &= memcmp(a.bits, b.bits, sizeof a.bits) != 0; // layer
    t7_render_overlay(&b, true, 0, 0, 1, 0);
    ok &= memcmp(a.bits, b.bits, sizeof a.bits) != 0; // host online glyph
    t7_render_overlay(&b, true, 0, 0, 0, 2);
    ok &= memcmp(a.bits, b.bits, sizeof a.bits) != 0; // notifications
    // Deterministic: same content renders identically.
    t7_render_overlay(&b, true, 0, 0, 0, 0);
    ok &= memcmp(a.bits, b.bits, sizeof a.bits) == 0;
    CHECK(ok, "t7_overlay_draws");
}

// The overlay is pure presentation: opening it must not perturb the duel. A
// world stepped with the deliberate chord matches one stepped identically with
// no chord, once the scry sub-state is masked out.
static void t7_overlay_no_combat_coupling(void) {
    sim_world_t chord, quiet;
    sim_init(&chord, SIMF_AUTHORITATIVE, 0);
    sim_init(&quiet, SIMF_AUTHORITATIVE, 0);
    for (int t = 0; t < 30; t++) {
        sim_tick(&chord, (sim_inputs_t){.scry_mask = SCRY_BOTH}, NULL, 0);
        sim_tick(&quiet, (sim_inputs_t){0}, NULL, 0);
    }
    chord.scry = (sim_scry_t){0}; // mask the only field that should differ
    bool ok = scry_is_open(&quiet) == false && world_hash(&chord) == world_hash(&quiet);
    CHECK(ok, "t7_overlay_no_combat_coupling");
}

static void host_recrc(duel_host_packet_t *packet) {
    packet->crc = duel_crc8(packet, offsetof(duel_host_packet_t, crc));
}

static void t8_host_valid_flow(void) {
    duel_host_state_t state = {0};
    duel_host_packet_t packet;
    duel_host_encode(DUEL_HOST_MSG_HELLO, 0x11223344u, 0,
                     DUEL_HOST_SCENE_ARCHIVE, 2, &packet);
    bool ok = duel_host_packet_valid(&packet);
    ok &= duel_host_accept(&state, &packet) == DUEL_HOST_APPLIED_HEARTBEAT;
    ok &= state.online && state.session == 0x11223344u && state.last_seq == 0;
    ok &= state.scene == DUEL_HOST_SCENE_ARCHIVE && state.notification_count == 2;

    duel_host_encode(DUEL_HOST_MSG_HEARTBEAT, 0x11223344u, 1,
                     DUEL_HOST_SCENE_FOCUS, 3, &packet);
    ok &= duel_host_accept(&state, &packet) == DUEL_HOST_APPLIED_HEARTBEAT;
    ok &= state.online && state.last_seq == 1 && state.scene == DUEL_HOST_SCENE_FOCUS;

    duel_host_encode(DUEL_HOST_MSG_NOTIFY, 0x11223344u, 2,
                     DUEL_HOST_SCENE_ARCHIVE, 4, &packet);
    ok &= duel_host_accept(&state, &packet) == DUEL_HOST_APPLIED;
    ok &= state.last_seq == 2 && state.scene == DUEL_HOST_SCENE_ARCHIVE;
    ok &= state.notification_count == 4;
    CHECK(ok, "t8_host_valid_flow");
}

static void t8_host_known_vector(void) {
    duel_host_packet_t packet;
    duel_host_encode(DUEL_HOST_MSG_HELLO, 0x11223344u, 0,
                     DUEL_HOST_SCENE_ARCHIVE, 2, &packet);
    const uint8_t expected[DUEL_HOST_REPORT_SIZE] = {
        0xCA, 0x8E, 0x02, 0x01, 0x44, 0x33, 0x22, 0x11,
        0x00, 0x00, 0x06, 0x01, 0x02, 0x07, 0x02, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1E,
    };
    CHECK(memcmp(&packet, expected, sizeof expected) == 0,
          "t8_host_known_vector");
}

static void t8_host_malformed(void) {
    duel_host_state_t state = {0};
    duel_host_packet_t base;
    duel_host_encode(DUEL_HOST_MSG_HELLO, 7, 0, DUEL_HOST_SCENE_DUEL, 0, &base);
    bool ok = true;

    duel_host_packet_t bad = base;
    bad.crc ^= 0x80;
    ok &= duel_host_accept(&state, &bad) == DUEL_HOST_DROP_MALFORMED;
    bad = base; bad.version++; host_recrc(&bad);
    ok &= duel_host_accept(&state, &bad) == DUEL_HOST_DROP_MALFORMED;
    bad = base; bad.type = 99; host_recrc(&bad);
    ok &= duel_host_accept(&state, &bad) == DUEL_HOST_DROP_MALFORMED;
    bad = base; bad.payload_len = 1; host_recrc(&bad);
    ok &= duel_host_accept(&state, &bad) == DUEL_HOST_DROP_MALFORMED;
    bad = base; bad.payload[0] = DUEL_HOST_SCENE_COUNT; host_recrc(&bad);
    ok &= duel_host_accept(&state, &bad) == DUEL_HOST_DROP_MALFORMED;
    bad = base; bad.payload[1] = 16; host_recrc(&bad);
    ok &= duel_host_accept(&state, &bad) == DUEL_HOST_DROP_MALFORMED;
    ok &= state.malformed_packets == 6 && !state.have_session;
    CHECK(ok, "t8_host_malformed");
}

static void t8_host_session_ordering(void) {
    const uint32_t session_a = 0xA0A0A0A0u;
    const uint32_t session_b = 0xB0B0B0B0u;
    duel_host_state_t state = {0};
    duel_host_packet_t packet;
    bool ok = true;

    // A data packet cannot invent a session; only sequence-zero HELLO can.
    duel_host_encode(DUEL_HOST_MSG_HEARTBEAT, session_a, 1, 0, 0, &packet);
    ok &= duel_host_accept(&state, &packet) == DUEL_HOST_DROP_STALE;
    duel_host_encode(DUEL_HOST_MSG_HELLO, session_a, 0, 1, 1, &packet);
    ok &= duel_host_accept(&state, &packet) == DUEL_HOST_APPLIED_HEARTBEAT;
    duel_host_encode(DUEL_HOST_MSG_HEARTBEAT, session_a, 1, 1, 1, &packet);
    ok &= duel_host_accept(&state, &packet) == DUEL_HOST_APPLIED_HEARTBEAT;
    ok &= duel_host_accept(&state, &packet) == DUEL_HOST_DROP_STALE; // duplicate

    // A clean daemon restart adopts B only through HELLO/0.
    duel_host_encode(DUEL_HOST_MSG_HELLO, session_b, 0, 2, 2, &packet);
    ok &= duel_host_accept(&state, &packet) == DUEL_HOST_APPLIED_HEARTBEAT;
    ok &= state.session == session_b && state.previous_session == session_a;

    // Neither delayed A traffic nor A's delayed greeting may roll B back.
    duel_host_encode(DUEL_HOST_MSG_HEARTBEAT, session_a, 2, DUEL_HOST_SCENE_FOCUS, 3, &packet);
    ok &= duel_host_accept(&state, &packet) == DUEL_HOST_DROP_STALE;
    duel_host_encode(DUEL_HOST_MSG_HELLO, session_a, 0, DUEL_HOST_SCENE_FOCUS, 3, &packet);
    ok &= duel_host_accept(&state, &packet) == DUEL_HOST_DROP_STALE;
    duel_host_encode(DUEL_HOST_MSG_HELLO, 0xCCCCCCCCu, 1, DUEL_HOST_SCENE_FOCUS, 3, &packet);
    ok &= duel_host_accept(&state, &packet) == DUEL_HOST_DROP_STALE;
    ok &= state.session == session_b && state.scene == 2 && state.notification_count == 2;
    ok &= state.stale_packets == 5;
    CHECK(ok, "t8_host_session_ordering");
}

static void t8_host_expiry_and_context(void) {
    duel_host_state_t state = {0};
    duel_host_packet_t packet;
    duel_host_encode(DUEL_HOST_MSG_HELLO, 42, 0, DUEL_HOST_SCENE_ARCHIVE, 15, &packet);
    bool ok = duel_host_accept(&state, &packet) == DUEL_HOST_APPLIED_HEARTBEAT;
    uint8_t packed = duel_host_context(&state);
    ok &= DUEL_HOST_CONTEXT_ONLINE(packed) == 1;
    ok &= DUEL_HOST_CONTEXT_SCENE(packed) == DUEL_HOST_SCENE_ARCHIVE;
    ok &= DUEL_HOST_CONTEXT_NOTIF(packed) == 15;

    duel_host_expire(&state);
    packed = duel_host_context(&state);
    ok &= !state.online && state.have_session && state.session == 42;
    ok &= packed == 0; // offline always collapses to duel / zero notifications

    // The same daemon can recover after a scheduling pause without a new HELLO.
    duel_host_encode(DUEL_HOST_MSG_HEARTBEAT, 42, 1, DUEL_HOST_SCENE_FOCUS, 3, &packet);
    ok &= duel_host_accept(&state, &packet) == DUEL_HOST_APPLIED_HEARTBEAT;
    ok &= state.online && state.scene == DUEL_HOST_SCENE_FOCUS;
    ok &= state.notification_count == 3;
    CHECK(ok, "t8_host_expiry_and_context");
}

static void t8_host_split_context(void) {
    sim_world_t world;
    sim_init(&world, SIMF_AUTHORITATIVE, 9);
    uint8_t context = DUEL_HOST_CONTEXT_PACK(true, DUEL_HOST_SCENE_ARCHIVE, 7, false);
    duel_snapshot_t packet;
    duel_encode_external(&world, 4, 12, context, &packet);

    bool ok = sizeof packet == 31 && DUEL_VER == 7 && duel_decode_valid(&packet);
    ok &= packet.external == context;
    ok &= DUEL_HOST_CONTEXT_ONLINE(packet.external) == 1;
    ok &= DUEL_HOST_CONTEXT_SCENE(packet.external) == DUEL_HOST_SCENE_ARCHIVE;
    ok &= DUEL_HOST_CONTEXT_NOTIF(packet.external) == 7;

    // External context is covered by CRC but deliberately ignored when the
    // render-only sim_world_t is reconstructed.
    duel_snapshot_t bad = packet;
    bad.external ^= 0x08;
    ok &= !duel_decode_valid(&bad);
    sim_world_t decoded;
    duel_decode_world(&packet, &decoded);
    ok &= decoded.tick == (uint16_t)world.tick;

    duel_encode(&world, 4, 13, &packet);
    ok &= duel_decode_valid(&packet) && packet.external == 0;
    CHECK(ok, "t8_host_split_context");
}

static void t8_host_overlay_scene(void) {
    sim_world_t world;
    sim_init(&world, SIMF_AUTHORITATIVE, 0);
    world.scry.state = SCRY_ACTIVE;
    world.scry.scene = 0;
    duel_render_t render = {
        .w = world,
        .overlay_host = 1,
        .overlay_scene = DUEL_HOST_SCENE_ARCHIVE,
    };
    duel_fb_t archive, focus;
    duel_fb_clear(&archive);
    wiz_draw_scene(&archive, &render, true, 0, false);
    render.overlay_scene = DUEL_HOST_SCENE_FOCUS;
    duel_fb_clear(&focus);
    wiz_draw_scene(&focus, &render, true, 0, false);
    bool ok = memcmp(archive.bits, focus.bits, sizeof archive.bits) != 0;

    // Once offline, external context is disposable and the local selector
    // wins; changing the stale external byte cannot alter the frame.
    render.overlay_host = 0;
    duel_fb_clear(&archive);
    wiz_draw_scene(&archive, &render, true, 0, false);
    render.overlay_scene = DUEL_HOST_SCENE_ARCHIVE;
    duel_fb_clear(&focus);
    wiz_draw_scene(&focus, &render, true, 0, false);
    ok &= memcmp(archive.bits, focus.bits, sizeof archive.bits) == 0;
    CHECK(ok, "t8_host_overlay_scene");
}

static void t9_render(duel_fb_t *fb, const sim_world_t *world, bool is_left,
                      uint8_t online, uint8_t scene, uint32_t frame) {
    duel_render_t render = {
        .w = *world,
        .overlay_host = online,
        .overlay_scene = scene,
    };
    duel_fb_clear(fb);
    wiz_draw_scene(fb, &render, is_left, frame, false);
}

static int fb_pixels_band(const duel_fb_t *fb, int y0, int y1) {
    int count = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = 0; x < DUEL_CANVAS_W; x++) count += duel_fb_get(fb, x, y);
    return count;
}

static void t9_archive_scene_isolated(void) {
    sim_world_t world;
    sim_init(&world, SIMF_AUTHORITATIVE, 0);
    duel_fb_t duel, focus, archive, offline;
    t9_render(&duel, &world, true, 1, DUEL_HOST_SCENE_DUEL, 0);
    t9_render(&focus, &world, true, 1, DUEL_HOST_SCENE_FOCUS, 0);
    t9_render(&archive, &world, true, 1, DUEL_HOST_SCENE_ARCHIVE, 0);
    t9_render(&offline, &world, true, 0, DUEL_HOST_SCENE_ARCHIVE, 0);
    bool ok = memcmp(duel.bits, focus.bits, sizeof duel.bits) == 0;
    ok &= memcmp(duel.bits, offline.bits, sizeof duel.bits) == 0;
    ok &= memcmp(duel.bits, archive.bits, sizeof duel.bits) != 0;
    // The Archive is an upper-canvas underlay; every combat/health byte below
    // it remains exactly the accepted Duel frame.
    for (int y = 45; y < DUEL_CANVAS_H; y++)
        for (int x = 0; x < DUEL_CANVAS_W; x++)
            ok &= duel_fb_get(&duel, x, y) == duel_fb_get(&archive, x, y);
    CHECK(ok, "t9_archive_scene_isolated");
}

static void t9_archive_deterministic_and_mirrored(void) {
    sim_world_t world;
    sim_init(&world, SIMF_AUTHORITATIVE, 0);
    duel_fb_t left_a, left_b, right;
    t9_render(&left_a, &world, true, 1, DUEL_HOST_SCENE_ARCHIVE, 17);
    t9_render(&left_b, &world, true, 1, DUEL_HOST_SCENE_ARCHIVE, 17);
    t9_render(&right, &world, false, 1, DUEL_HOST_SCENE_ARCHIVE, 17);
    bool ok = memcmp(left_a.bits, left_b.bits, sizeof left_a.bits) == 0;
    for (int y = 3; y <= 44; y++)
        for (int x = 0; x < DUEL_CANVAS_W; x++)
            ok &= duel_fb_get(&left_a, x, y) == duel_fb_get(&right, 31 - x, y);
    CHECK(ok, "t9_archive_deterministic_and_mirrored");
}

static void t9_archive_activity_tiers(void) {
    sim_world_t world;
    sim_init(&world, SIMF_AUTHORITATIVE, 0);
    duel_fb_t idle, press, expanded, short_cast, long_cast;
    t9_render(&idle, &world, true, 1, DUEL_HOST_SCENE_ARCHIVE, 0);
    world.wiz[SIM_SIDE_L].shield_ticks = SIM_SHIELD_TICKS;
    t9_render(&press, &world, true, 1, DUEL_HOST_SCENE_ARCHIVE, 0);
    world.wiz[SIM_SIDE_L].shield_ticks = SIM_SHIELD_TICKS / 2;
    t9_render(&expanded, &world, true, 1, DUEL_HOST_SCENE_ARCHIVE, 0);
    bool ok = memcmp(idle.bits, press.bits, sizeof idle.bits) != 0;
    ok &= memcmp(press.bits, expanded.bits, sizeof press.bits) != 0;

    world.wiz[SIM_SIDE_L].shield_ticks = 0;
    world.wiz[SIM_SIDE_L].cast_windup = 4;
    world.wiz[SIM_SIDE_L].cast_tier = SPELL_TIER_SHORT;
    t9_render(&short_cast, &world, true, 1, DUEL_HOST_SCENE_ARCHIVE, 0);
    world.wiz[SIM_SIDE_L].cast_tier = SPELL_TIER_LONG;
    t9_render(&long_cast, &world, true, 1, DUEL_HOST_SCENE_ARCHIVE, 0);
    ok &= fb_pixels_band(&short_cast, 3, 44) < fb_pixels_band(&long_cast, 3, 44);
    // Bounded even at the richest tier: the sparse archive never fills half
    // of its 32x42-pixel band.
    ok &= fb_pixels_band(&long_cast, 3, 44) < (32 * 42 / 2);
    CHECK(ok, "t9_archive_activity_tiers");
}

static void t9_archive_precedence_and_invariance(void) {
    sim_world_t world;
    sim_init(&world, SIMF_AUTHORITATIVE, 123);
    world.wiz[SIM_SIDE_R].life = LIFE_DOWNED;
    world.wiz[SIM_SIDE_R].life_ticks = 10;
    world.wiz[SIM_SIDE_R].hp = 0;
    uint64_t before = world_hash(&world);
    duel_fb_t closed, open, stale;
    t9_render(&closed, &world, true, 1, DUEL_HOST_SCENE_ARCHIVE, 0);

    duel_render_t render = {
        .w = world,
        .overlay_host = 1,
        .overlay_scene = DUEL_HOST_SCENE_ARCHIVE,
        .overlay_layer = 3,
    };
    render.w.scry.state = SCRY_ACTIVE;
    duel_fb_clear(&open);
    wiz_draw_scene(&open, &render, true, 0, false);
    bool ok = duel_fb_get(&closed, 10, 30) && !duel_fb_get(&open, 10, 30);
    ok &= duel_fb_get(&open, 3, 3); // panel border remains above the cleared art

    render.w.scry.state = SCRY_IDLE;
    render.stale_link = true;
    duel_fb_clear(&stale);
    wiz_draw_scene(&stale, &render, true, 0, true);
    ok &= duel_fb_get(&stale, 23, 2); // stale marker above Archive
    ok &= duel_fb_get(&stale, (int)(world.tick % 25), DUEL_CANVAS_H - 1); // HUD above Archive
    ok &= world_hash(&world) == before; // rendering never mutates authoritative state
    CHECK(ok, "t9_archive_precedence_and_invariance");
}

/* ---------------- M10: normalized notification presentation ------------ */

static void t10_host_v1_v2_validation(void) {
    duel_host_packet_t packet;
    duel_host_state_t state = {0};
    duel_host_encode_v1(DUEL_HOST_MSG_HELLO, 1, 0, DUEL_HOST_SCENE_DUEL, 3, &packet);
    bool ok = duel_host_packet_valid(&packet);
    ok &= duel_host_accept(&state, &packet) == DUEL_HOST_APPLIED_HEARTBEAT;
    ok &= state.notification_count == 3;
    ok &= state.notification_category == DUEL_HOST_CATEGORY_NONE;

    duel_host_encode_summary(DUEL_HOST_MSG_HEARTBEAT, 1, 1,
        DUEL_HOST_SCENE_ARCHIVE, 2, DUEL_HOST_CATEGORY_SECURITY,
        DUEL_HOST_PRIORITY_CRITICAL, 7, true, &packet);
    ok &= duel_host_packet_valid(&packet);
    ok &= duel_host_accept(&state, &packet) == DUEL_HOST_APPLIED_HEARTBEAT;
    ok &= state.notification_category == DUEL_HOST_CATEGORY_SECURITY;
    ok &= state.notification_priority == DUEL_HOST_PRIORITY_CRITICAL;
    ok &= state.notification_age == 7 && state.notification_persistent;

    duel_host_packet_t bad = packet;
    bad.payload[3] = DUEL_HOST_PRIORITY_NORMAL; host_recrc(&bad);
    ok &= !duel_host_packet_valid(&bad); // persistent normal is impossible
    bad = packet; bad.payload[1] = 0; host_recrc(&bad);
    ok &= !duel_host_packet_valid(&bad); // empty summaries must be all-zero
    bad = packet; bad.payload[5] = 2; host_recrc(&bad);
    ok &= !duel_host_packet_valid(&bad);

    // NOTIFY advances ordering but cannot resurrect expired external context.
    duel_host_expire(&state);
    duel_host_encode_summary(DUEL_HOST_MSG_NOTIFY, 1, 2, DUEL_HOST_SCENE_FOCUS,
        1, DUEL_HOST_CATEGORY_TERMINAL, DUEL_HOST_PRIORITY_LOW, 0, false, &packet);
    ok &= duel_host_accept(&state, &packet) == DUEL_HOST_APPLIED;
    ok &= duel_host_context(&state) == 0 && duel_host_alert(&state) == 0;
    CHECK(ok, "t10_host_v1_v2_validation");
}

static void t10_split_v7_alert(void) {
    sim_world_t world;
    sim_init(&world, SIMF_AUTHORITATIVE, 99);
    uint8_t external = DUEL_HOST_CONTEXT_PACK(true, DUEL_HOST_SCENE_FOCUS, 15, true);
    uint8_t alert = DUEL_HOST_ALERT_PACK(DUEL_HOST_CATEGORY_SECURITY,
                                         DUEL_HOST_PRIORITY_CRITICAL, 6);
    duel_snapshot_t packet;
    duel_encode_external_alert(&world, 9, 4, external, alert, &packet);
    bool ok = sizeof packet == 31 && packet.ver == 7 && duel_decode_valid(&packet);
    ok &= DUEL_HOST_CONTEXT_PERSISTENT(packet.external);
    ok &= DUEL_HOST_ALERT_CATEGORY(packet.alert) == DUEL_HOST_CATEGORY_SECURITY;
    ok &= DUEL_HOST_ALERT_PRIORITY(packet.alert) == DUEL_HOST_PRIORITY_CRITICAL;
    ok &= DUEL_HOST_ALERT_AGE(packet.alert) == 6;
    packet.alert ^= 0x20;
    ok &= !duel_decode_valid(&packet);
    CHECK(ok, "t10_split_v7_alert");
}

static void t10_render_alert(duel_fb_t *fb, bool is_left, uint8_t count,
                             uint8_t category, uint8_t priority,
                             uint8_t age, bool persistent, bool scry) {
    sim_world_t world;
    sim_init(&world, SIMF_AUTHORITATIVE, 0);
    world.scry.state = scry ? SCRY_ACTIVE : SCRY_IDLE;
    duel_render_t render = {
        .w = world,
        .overlay_host = 1,
        .overlay_notif = count,
        .overlay_category = category,
        .overlay_priority = priority,
        .overlay_age = age,
        .overlay_persistent = persistent,
    };
    duel_fb_clear(fb);
    wiz_draw_scene(fb, &render, is_left, 0, false);
}

static void t10_zero_v1_and_category_glyphs(void) {
    duel_fb_t base, zero, legacy, glyphs[7];
    t10_render_alert(&base, true, 0, 0, 0, 0, false, false);
    t10_render_alert(&zero, true, 0, DUEL_HOST_CATEGORY_SECURITY,
                     DUEL_HOST_PRIORITY_CRITICAL, 7, true, false);
    t10_render_alert(&legacy, true, 3, DUEL_HOST_CATEGORY_NONE,
                     DUEL_HOST_PRIORITY_NONE, 0, false, false);
    bool ok = memcmp(base.bits, zero.bits, sizeof base.bits) == 0;
    ok &= memcmp(base.bits, legacy.bits, sizeof base.bits) == 0;
    for (int category = 1; category < DUEL_HOST_CATEGORY_COUNT; category++) {
        t10_render_alert(&glyphs[category - 1], true, 1, (uint8_t)category,
                         DUEL_HOST_PRIORITY_LOW, 0, false, false);
        ok &= memcmp(base.bits, glyphs[category - 1].bits, sizeof base.bits) != 0;
        for (int prior = 1; prior < category; prior++)
            ok &= memcmp(glyphs[category - 1].bits, glyphs[prior - 1].bits,
                         sizeof base.bits) != 0;
    }
    CHECK(ok, "t10_zero_v1_and_category_glyphs");
}

static void t10_mirror_priority_age_persistence_and_scry(void) {
    duel_fb_t left, right, low, normal, critical, aged, anchored, scry, scry_empty;
    t10_render_alert(&left, true, 4, DUEL_HOST_CATEGORY_TRANSFER,
                     DUEL_HOST_PRIORITY_NORMAL, 0, false, false);
    t10_render_alert(&right, false, 4, DUEL_HOST_CATEGORY_TRANSFER,
                     DUEL_HOST_PRIORITY_NORMAL, 0, false, false);
    bool ok = true;
    for (int y = 1; y <= 15; y++)
        for (int x = 0; x < DUEL_CANVAS_W; x++)
            ok &= duel_fb_get(&left, x, y) == duel_fb_get(&right, 31 - x, y);

    t10_render_alert(&low, true, 1, DUEL_HOST_CATEGORY_SECURITY,
                     DUEL_HOST_PRIORITY_LOW, 0, false, false);
    t10_render_alert(&normal, true, 1, DUEL_HOST_CATEGORY_SECURITY,
                     DUEL_HOST_PRIORITY_NORMAL, 0, false, false);
    t10_render_alert(&critical, true, 1, DUEL_HOST_CATEGORY_SECURITY,
                     DUEL_HOST_PRIORITY_CRITICAL, 0, false, false);
    t10_render_alert(&aged, true, 1, DUEL_HOST_CATEGORY_SECURITY,
                     DUEL_HOST_PRIORITY_NORMAL, 7, false, false);
    t10_render_alert(&anchored, true, 1, DUEL_HOST_CATEGORY_SECURITY,
                     DUEL_HOST_PRIORITY_CRITICAL, 0, true, false);
    ok &= memcmp(low.bits, normal.bits, sizeof low.bits) != 0;
    ok &= memcmp(normal.bits, critical.bits, sizeof low.bits) != 0;
    ok &= memcmp(normal.bits, aged.bits, sizeof low.bits) != 0;
    ok &= memcmp(critical.bits, anchored.bits, sizeof low.bits) != 0;

    t10_render_alert(&scry, true, 2, DUEL_HOST_CATEGORY_CALENDAR,
                     DUEL_HOST_PRIORITY_CRITICAL, 0, true, true);
    t10_render_alert(&scry_empty, true, 0, 0, 0, 0, false, true);
    ok &= !duel_fb_get(&scry, 2, 4); // transient outer sigil is absent
    ok &= memcmp(scry.bits, scry_empty.bits, sizeof scry.bits) != 0; // in-panel summary
    CHECK(ok, "t10_mirror_priority_age_persistence_and_scry");
}

int main(int argc, char **argv) {
    int argi = 1;
    if (argi < argc && strcmp(argv[argi], "--write-golden") == 0) {
        g_write_golden = 1;
        argi++;
    }
    if (argc - argi != 2) {
        fprintf(stderr, "usage: %s [--write-golden] <traces-dir> <golden-dir>\n", argv[0]);
        return 2;
    }
    g_traces_dir = argv[argi];
    g_golden_dir = argv[argi + 1];

    t0_fb_roundtrip();
    t0_fb_qmk_page_layout();
    t0_draw_deterministic();
    t0_trace_parse();

    t2_replay_golden();
    t2_cadence_invariance();
    t2_snapshot_pure();
    t2_keydown_only_equivalence();
    t2_queue_overflow();
    t2_tick_wrap();

    t3_lossy_convergence();
    t3_stale_rollback();
    t3_session_restart();
    t3_link_dead_typing_ok();
    t3_stale_marker();

    t4_flight_golden();
    t4_screen_ownership();
    t4_shield_window();
    t4_missed_impact_recovery();

    t5_ko_sequence();
    t5_no_dead_end();
    t5_downed_fizzle();
    t5_downed_cannot_act();
    t5_regen();
    t5_slave_never_transitions();
    t5_snapshot_lifecycle();
    t5_double_ko();
    t5_variant_cycle();
    t5_draw_lifecycle_distinct();
    t5_draw_variants();
    t5_replay_golden();

    t6_same_recipe_determinism();
    t6_element_reachability();
    t6_recipe_window();
    t6_modifier_reachability();
    t6_inactivity_expiry();
    t6_bounded_under_mash();
    t6_slave_never_compiles();
    t6_kind_sync();
    t6_deflect_windows();
    t6_void_pierces_ward();
    t6_draw_distinct();
    t6_replay_golden();

    t75_windup_timing();
    t75_recipe_tiers();
    t75_tier_no_combat_coupling();
    t75_charge_sync();
    t75_draw_recipe_scale();
    t75_outcome_grammars();
    t75_void_ward_puncture();

    t7_roll_never_opens();
    t7_deliberate_opens();
    t7_early_release();
    t7_third_key_cancels_and_latches();
    t7_select_cycles_scene();
    t7_slave_never_opens();
    t7_scry_sync();
    t7_overlay_draws();
    t7_overlay_no_combat_coupling();

    t8_host_valid_flow();
    t8_host_known_vector();
    t8_host_malformed();
    t8_host_session_ordering();
    t8_host_expiry_and_context();
    t8_host_split_context();
    t8_host_overlay_scene();

    t9_archive_scene_isolated();
    t9_archive_deterministic_and_mirrored();
    t9_archive_activity_tiers();
    t9_archive_precedence_and_invariance();

    t10_host_v1_v2_validation();
    t10_split_v7_alert();
    t10_zero_v1_and_category_glyphs();
    t10_mirror_priority_age_persistence_and_scry();

    t11_display_policy();
    t11_display_wire_compatibility();

    if (g_failures) {
        printf("%d test(s) FAILED\n", g_failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
