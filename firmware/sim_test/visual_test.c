#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "duel_courier.h" // Wave 6 couriers
#include "duel_event.h"   // Wave 7 rare events
#include "duel_host.h"
#include "duel_sim.h"
#include "scenarios.h"

static int failures;

#define VCHECK(cond, name) do { \
    if (cond) printf("PASS %s\n", name); \
    else { printf("FAIL %s (%s:%d)\n", name, __FILE__, __LINE__); failures++; } \
} while (0)

static uint64_t hash_fb(const duel_fb_t *fb) {
    // Hash canonical logical rows, not duel_fb_t storage. Packing each logical
    // group of eight x pixels reproduces the historical golden byte stream,
    // so a storage-layout refactor is not mistaken for an artwork change.
    uint64_t h = UINT64_C(0xcbf29ce484222325);
    for (int y = 0; y < DUEL_CANVAS_H; y++) {
        for (int x0 = 0; x0 < DUEL_CANVAS_W; x0 += 8) {
            uint8_t logical = 0;
            for (int bit = 0; bit < 8; bit++) {
                if (duel_fb_get(fb, x0 + bit, y)) logical |= (uint8_t)(1u << bit);
            }
            h ^= logical;
            h *= UINT64_C(0x100000001b3);
        }
    }
    return h;
}

static int pixel_count(const duel_fb_t *fb, int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) n += duel_fb_get(fb, x, y);
    return n;
}

static bool exact_hashes(const char *path, bool write) {
    char actual[16384];
    size_t off = 0;
    for (size_t i = 0; i < duel_scenario_count(); i++) {
        const duel_scenario_t *s = duel_scenario_at(i);
        duel_fb_t left, right;
        duel_scenario_render(s, s->frame, &left, &right);
        off += (size_t)snprintf(actual + off, sizeof actual - off,
                               "%-24s %016llx %016llx\n", s->name,
                               (unsigned long long)hash_fb(&left),
                               (unsigned long long)hash_fb(&right));
    }
    if (write) {
        FILE *f = fopen(path, "w");
        if (!f) return false;
        bool ok = fwrite(actual, 1, off, f) == off && fclose(f) == 0;
        if (ok) printf("WROTE %s\n", path);
        return ok;
    }
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char expected[sizeof actual];
    size_t n = fread(expected, 1, sizeof expected - 1, f);
    expected[n] = '\0';
    fclose(f);
    return n == off && memcmp(expected, actual, off) == 0;
}

static void test_catalog(void) {
    bool ok = duel_scenario_count() >= 35;
    for (size_t i = 0; i < duel_scenario_count(); i++) {
        const duel_scenario_t *a = duel_scenario_at(i);
        duel_render_t r;
        ok &= a && duel_scenario_build(a, &r);
        for (size_t j = 0; j < i; j++) ok &= strcmp(a->name, duel_scenario_at(j)->name) != 0;
    }
    const char *required[] = {"pose-cast", "recipe-force-short", "recipe-void-saturated",
        "impact", "deflect", "fizzle", "void-pierce", "life-collapse", "life-downed",
        "life-medic", "life-replace", "archive-idle", "archive-pulse", "scry",
        "stale-link", "diagnostics", "alert-impact", "scry-stale-diagnostics"};
    for (size_t i = 0; i < sizeof required / sizeof required[0]; i++)
        ok &= duel_scenario_find(required[i]) != NULL;
    VCHECK(ok, "visual_catalog_complete_unique");
}

static void test_density_and_distinction(void) {
    bool density = true, distinct = true;
    uint64_t hashes[128];
    size_t hn = 0;
    for (size_t i = 0; i < duel_scenario_count(); i++) {
        duel_fb_t left, right;
        const duel_scenario_t *s = duel_scenario_at(i);
        duel_scenario_render(s, s->frame, &left, &right);
        int lp = pixel_count(&left, 0, 0, 31, 127);
        int rp = pixel_count(&right, 0, 0, 31, 127);
        bool bounded = lp >= 25 && lp <= 1400 && rp >= 25 && rp <= 1400;
        if (!bounded) printf("density %s: left=%d right=%d\n", s->name, lp, rp);
        density &= bounded;
        uint64_t pair = hash_fb(&left) ^ (hash_fb(&right) * UINT64_C(0x9e3779b97f4a7c15));
        for (size_t j = 0; j < hn; j++) {
            if (hashes[j] == pair) {
                printf("pair collision: %s == %s\n", s->name, duel_scenario_at(j)->name);
                distinct = false;
            }
        }
        hashes[hn++] = pair;
    }
    VCHECK(density, "visual_density_bounds");
    VCHECK(distinct, "visual_canonical_frames_distinct");
}

static void test_archive_continuity_and_variation(void) {
#ifdef ARCANE_M12
    // M12 retires the upper archive underlay: "archive-idle" now renders the
    // Research tower FLOOR (y61-110). The two city-states are architecturally
    // DISTINCT (astral left vs mechanical right), so the floor is deliberately
    // NOT gap-mirrored; it is present, and fully static (no frame variation),
    // unlike the old animated archive rune.
    const duel_scenario_t *s = duel_scenario_find("archive-idle");
    duel_fb_t l0, r0, l1, r1;
    duel_scenario_render(s, 0, &l0, &r0);
    duel_scenario_render(s, 32, &l1, &r1);
    bool cities_differ = false, has_floor = false, floor_static = true;
    for (int y = 61; y <= 110; y++)
        for (int x = 0; x < 32; x++) {
            if (duel_fb_get(&l0, x, y) != duel_fb_get(&r0, 31 - x, y)) cities_differ = true;
            has_floor |= duel_fb_get(&l0, x, y);
            floor_static &= duel_fb_get(&l0, x, y) == duel_fb_get(&l1, x, y);
        }
    VCHECK(cities_differ && has_floor, "visual_archive_gap_continuity_asymmetry");
    VCHECK(floor_static, "visual_archive_sparse_static_variation");
#else
    const duel_scenario_t *s = duel_scenario_find("archive-idle");
    duel_fb_t l0, r0, l1, r1;
    duel_scenario_render(s, 0, &l0, &r0);
    duel_scenario_render(s, 32, &l1, &r1);
    bool mirror = true, asymmetric = false, lower_static = true;
    for (int y = 3; y <= 44; y++)
        for (int x = 0; x < 32; x++) {
            mirror &= duel_fb_get(&l0, x, y) == duel_fb_get(&r0, 31 - x, y);
            if (duel_fb_get(&l0, x, y) != duel_fb_get(&l0, 31 - x, y)) asymmetric = true;
        }
    for (int y = 45; y < 128; y++)
        for (int x = 0; x < 32; x++) lower_static &= duel_fb_get(&l0, x, y) == duel_fb_get(&l1, x, y);
    bool upper_varies = memcmp(l0.bits, l1.bits, sizeof l0.bits) != 0;
    VCHECK(mirror && asymmetric, "visual_archive_gap_continuity_asymmetry");
    VCHECK(upper_varies && lower_static, "visual_archive_sparse_static_variation");
#endif
}

static void render_direct(duel_fb_t *fb, duel_render_t *r, bool left, uint32_t frame, bool diag) {
    duel_fb_clear(fb);
    wiz_draw_scene(fb, r, left, frame, diag);
}

static void test_isolation_and_restoration(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    duel_render_t r = {0};
    duel_render_from_world(&r, &w);
    duel_fb_t duel, focus, offline_archive, archive, restored;
    render_direct(&duel, &r, true, 0, false);
    r.external = DUEL_HOST_CONTEXT_PACK(1, DUEL_HOST_SCENE_FOCUS, 0, false);
    render_direct(&focus, &r, true, 0, false);
    r.external = DUEL_HOST_CONTEXT_PACK(0, DUEL_HOST_SCENE_ARCHIVE, 0, false);
    render_direct(&offline_archive, &r, true, 0, false);
    bool isolated = memcmp(duel.bits, focus.bits, sizeof duel.bits) == 0 &&
                    memcmp(duel.bits, offline_archive.bits, sizeof duel.bits) == 0;

    r.external = DUEL_HOST_CONTEXT_PACK(1, DUEL_HOST_SCENE_ARCHIVE, 0, false);
    render_direct(&archive, &r, true, 0, false);
    r.view.scry = DUEL_SCRY_PACK(true, 0);
    render_direct(&restored, &r, true, 0, false);
    bool scry_differs = memcmp(archive.bits, restored.bits, sizeof archive.bits) != 0;
    r.view.scry = DUEL_SCRY_PACK(false, 0);
    render_direct(&restored, &r, true, 0, false);
    bool restores = memcmp(archive.bits, restored.bits, sizeof archive.bits) == 0;
    VCHECK(isolated, "visual_scene_isolation");
    VCHECK(scry_differs && restores, "visual_scry_restoration");
}

static void test_m12_floor_and_resident(void) {
#ifdef ARCANE_M12
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    duel_render_t r = {0};
    duel_render_from_world(&r, &w);
    r.seed = 21;
    r.civic = DUEL_CIVIC_PACK(DUEL_M12_FLOOR_WORKSHOP, DUEL_M12_MODE_NORMAL,
                              DUEL_M12_INTENSITY_CALM);

    // 1) The floor room occupies its band (y61-110) and stays clear of the
    //    raised rooftop: rows y59-60 (the gap between the lifted cluster and the
    //    ceiling beam) carry no floor pixels with an idle champion.
    r.civic_phase = 0;
    duel_fb_t fb0;
    render_direct(&fb0, &r, true, 0, false);
    bool floor_present = pixel_count(&fb0, 0, 61, 31, 110) > 0;
    bool separated = pixel_count(&fb0, 0, 59, 31, 60) == 0;
    VCHECK(floor_present && separated, "visual_m12_floor_within_band");

    // 2) Sweeping the civic phase advances ONLY the resident (nothing else keys
    //    off phase here). Every pixel that changes must fall inside y61-110, so
    //    the resident stays in its room; that it changes at all proves it is a
    //    distinct actor, separate from the static occupation anchors.
    bool moved = false, in_band = true;
    duel_fb_t prev = fb0;
    for (int ph = 16; ph <= 208; ph += 16) {
        r.civic_phase = (uint8_t)ph;
        duel_fb_t cur;
        render_direct(&cur, &r, true, 0, false);
        for (int y = 0; y < DUEL_CANVAS_H; y++)
            for (int x = 0; x < DUEL_CANVAS_W; x++)
                if (duel_fb_get(&cur, x, y) != duel_fb_get(&prev, x, y)) {
                    moved = true;
                    if (y < 61 || y > 110) in_band = false;
                }
        prev = cur;
    }
    VCHECK(in_band, "visual_m12_resident_within_floor_band");
    VCHECK(moved, "visual_m12_resident_distinct_from_anchors");
#endif
}

// --- Wave 6 couriers ---
static void test_m12_courier_band_and_city(void) {
#ifdef ARCANE_M12
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    duel_render_t r = {0};
    duel_render_from_world(&r, &w);
    r.seed = 42;
    r.civic = DUEL_CIVIC_PACK(DUEL_M12_FLOOR_COMMONS, DUEL_M12_MODE_NORMAL,
                              DUEL_M12_INTENSITY_CALM);
    r.civic_phase = 48;

    // Baseline: no courier assigned (COURIER_NONE), both halves are floor-only.
    r.shared_pres = 0;
    duel_fb_t base_l, base_r;
    render_direct(&base_l, &r, true, 0, false);
    render_direct(&base_r, &r, false, 0, false);

    // A courier assigned to one city must appear ONLY on that half and ONLY
    // inside the floor band (y61-110); the other half stays byte-identical to
    // the floor-only baseline. Sweep every lifecycle and each courier form.
    static const uint8_t right_kind[4] = {
        DUEL_M12_COURIER_PARCEL, DUEL_M12_COURIER_BEACON,
        DUEL_M12_COURIER_SENTINEL, DUEL_M12_COURIER_PARCEL };
    bool city_isolated = true, in_band = true, drawn = true;
    for (uint8_t life = 0; life < 4; life++) {
        duel_fb_t cl, cr;

        // Left city: the messenger draws on the left only.
        r.shared_pres = DUEL_VISITOR_PACK(DUEL_M12_COURIER_MESSENGER, 0u, life);
        render_direct(&cl, &r, true, 0, false);
        render_direct(&cr, &r, false, 0, false);
        city_isolated &= memcmp(cr.bits, base_r.bits, sizeof cr.bits) == 0;
        bool diff = false;
        for (int y = 0; y < DUEL_CANVAS_H; y++)
            for (int x = 0; x < DUEL_CANVAS_W; x++)
                if (duel_fb_get(&cl, x, y) != duel_fb_get(&base_l, x, y)) {
                    diff = true;
                    if (y < 61 || y > 110) in_band = false;
                }
        drawn &= diff;

        // Right city: parcel / beacon / sentinel draw on the right only.
        r.shared_pres = DUEL_VISITOR_PACK(right_kind[life], 1u, life);
        render_direct(&cl, &r, true, 0, false);
        render_direct(&cr, &r, false, 0, false);
        city_isolated &= memcmp(cl.bits, base_l.bits, sizeof cl.bits) == 0;
        diff = false;
        for (int y = 0; y < DUEL_CANVAS_H; y++)
            for (int x = 0; x < DUEL_CANVAS_W; x++)
                if (duel_fb_get(&cr, x, y) != duel_fb_get(&base_r, x, y)) {
                    diff = true;
                    if (y < 61 || y > 110) in_band = false;
                }
        drawn &= diff;
    }
    VCHECK(city_isolated, "visual_m12_courier_assigned_city_only");
    VCHECK(in_band && drawn, "visual_m12_courier_within_floor_band");

    // Routing sanity: derivation lands each category on the intended form/city
    // (§11.3), with persistent/security overriding to a stationed sentinel.
    m12_visitor_state_t comm = m12_visitor_derive(42, 48, DUEL_HOST_CATEGORY_COMMUNICATION, 1, 1, false);
    m12_visitor_state_t xfer = m12_visitor_derive(42, 48, DUEL_HOST_CATEGORY_TRANSFER, 1, 1, false);
    m12_visitor_state_t sys  = m12_visitor_derive(42, 48, DUEL_HOST_CATEGORY_SYSTEM, 1, 1, false);
    m12_visitor_state_t sec  = m12_visitor_derive(42, 48, DUEL_HOST_CATEGORY_SECURITY, 1, 1, false);
    m12_visitor_state_t pers = m12_visitor_derive(42, 48, DUEL_HOST_CATEGORY_TRANSFER, 1, 1, true);
    m12_visitor_state_t none = m12_visitor_derive(42, 48, DUEL_HOST_CATEGORY_COMMUNICATION, 0, 1, false);
    bool routing =
        DUEL_VISITOR_STATE_KIND(comm) == DUEL_M12_COURIER_MESSENGER && DUEL_VISITOR_STATE_CITY(comm) == 0u &&
        DUEL_VISITOR_STATE_KIND(xfer) == DUEL_M12_COURIER_PARCEL    && DUEL_VISITOR_STATE_CITY(xfer) == 1u &&
        DUEL_VISITOR_STATE_KIND(sys)  == DUEL_M12_COURIER_BEACON    && DUEL_VISITOR_STATE_CITY(sys)  == 1u &&
        DUEL_VISITOR_STATE_KIND(sec)  == DUEL_M12_COURIER_SENTINEL  && DUEL_VISITOR_STATE_CITY(sec)  == 0u &&
        DUEL_VISITOR_STATE_KIND(pers) == DUEL_M12_COURIER_SENTINEL  && DUEL_VISITOR_STATE_CITY(pers) == 1u &&
        DUEL_VISITOR_STATE_KIND(none) == DUEL_M12_COURIER_NONE;
    VCHECK(routing, "visual_m12_courier_category_routing");
#endif
}

// --- Wave 7 rare events ---
// Structural invariants for the rare-event slot: an active LOCAL event only adds
// pixels inside the floor band (y61-110); a suppressed (ineligible) event adds
// nothing; and the deck is deterministic, honours the back-to-back cooldown, and
// keeps the ~75/25 local/shared weighting.
static void test_m12_rare_event(void) {
#ifdef ARCANE_M12
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    duel_render_t base = {0};
    duel_render_from_world(&base, &w);
    base.seed = 33;
    base.civic = DUEL_CIVIC_PACK(DUEL_M12_FLOOR_COMMONS, DUEL_M12_MODE_NORMAL,
                                 DUEL_M12_INTENSITY_CALM);
    base.civic_phase = 8;

    // Baseline floor with an empty rare-event slot (revision == 0 -> NONE).
    duel_fb_t noev;
    render_direct(&noev, &base, true, 0, false);

    // An ACTIVE local event targeted at this (left) city must add pixels, and
    // every added pixel must lie inside the floor band y61-110.
    duel_render_t ev = base;
    ev.revision = DUEL_EVENT_PACK(DUEL_M12_EVENT_RUNAWAY_SCROLL,
                                  DUEL_M12_EVENT_PHASE_ACTIVE, DUEL_M12_EVENT_TARGET_LEFT);
    duel_fb_t withev;
    render_direct(&withev, &ev, true, 0, false);
    bool added = false, in_band = true;
    for (int y = 0; y < DUEL_CANVAS_H; y++)
        for (int x = 0; x < DUEL_CANVAS_W; x++)
            if (duel_fb_get(&withev, x, y) != duel_fb_get(&noev, x, y)) {
                added = true;
                if (y < 61 || y > 110) in_band = false;
            }
    VCHECK(added && in_band, "visual_m12_rare_event_local_within_band");

    // A LOCAL event targeted at the OTHER city draws nothing on this half.
    duel_render_t other = base;
    other.revision = DUEL_EVENT_PACK(DUEL_M12_EVENT_JAMMED_GEAR,
                                     DUEL_M12_EVENT_PHASE_ACTIVE, DUEL_M12_EVENT_TARGET_RIGHT);
    duel_fb_t otherfb;
    render_direct(&otherfb, &other, true, 0, false);
    bool other_city_quiet = memcmp(otherfb.bits, noev.bits, sizeof noev.bits) == 0;

    // Suppressed (ineligible) slot: the derived state is NONE, so nothing extra.
    duel_render_t sup = base;
    m12_event_state_t s = m12_event_derive(base.seed, base.civic_phase, false);
    sup.revision = m12_event_revision(s);
    duel_fb_t supfb;
    render_direct(&supfb, &sup, true, 0, false);
    bool suppressed_noop = memcmp(supfb.bits, noev.bits, sizeof noev.bits) == 0;
    VCHECK(other_city_quiet && suppressed_noop, "visual_m12_rare_event_suppressed_noop");

    // Deck determinism + back-to-back cooldown across the whole phase range for a
    // sweep of seeds; and the family weighting stays ~75/25 local/shared.
    bool deterministic = true, cooldown = true;
    long local_hits = 0, shared_hits = 0;
    for (int seed = 0; seed < 256; seed++) {
        for (int phase = 0; phase < 256; phase++) {
            m12_event_state_t a = m12_event_derive((uint8_t)seed, (uint8_t)phase, true);
            m12_event_state_t b = m12_event_derive((uint8_t)seed, (uint8_t)phase, true);
            if (a.id_target != b.id_target || a.phase != b.phase) deterministic = false;
            uint8_t family = DUEL_EVENT_ID(a.id_target);
            if (family >= DUEL_M12_EVENT_DIPLOMATIC_COURIER) shared_hits++; else local_hits++;
        }
    }
    // Adjacent-cycle inequality (the back-to-back cooldown), checked directly on
    // the per-cycle family (sampled at the ACTIVE sub-phase of each cycle).
    for (int seed = 0; seed < 256 && cooldown; seed++) {
        uint8_t prev = DUEL_M12_EVENT_NONE;
        for (int cyc = 0; cyc < 32; cyc++) {
            m12_event_state_t st = m12_event_derive((uint8_t)seed, (uint8_t)(cyc * 8 + 2), true);
            uint8_t family = DUEL_EVENT_ID(st.id_target);
            if (cyc > 0 && family == prev) cooldown = false;
            prev = family;
        }
    }
    long total = local_hits + shared_hits;
    double shared_frac = total ? (double)shared_hits / (double)total : 0.0;
    bool weighting = shared_frac > 0.15 && shared_frac < 0.35;
    VCHECK(deterministic, "visual_m12_rare_event_deterministic");
    VCHECK(cooldown, "visual_m12_rare_event_family_cooldown");
    VCHECK(weighting, "visual_m12_rare_event_local_shared_weighting");
#endif
}

static void test_precedence_and_alert_grammar(void) {
    duel_fb_t alert_impact_l, alert_impact_r, stack_l, stack_r;
    const duel_scenario_t *impact = duel_scenario_find("alert-impact");
    const duel_scenario_t *stack = duel_scenario_find("scry-stale-diagnostics");
    duel_scenario_render(impact, impact->frame, &alert_impact_l, &alert_impact_r);
    duel_scenario_render(stack, stack->frame, &stack_l, &stack_r);
    bool protected_regions = pixel_count(&alert_impact_l, 0, DUEL_ALERT_Y0, 9, DUEL_ALERT_Y1) > 15;
    protected_regions &= pixel_count(&alert_impact_r, 22, DUEL_ALERT_Y0, 31, DUEL_ALERT_Y1) > 15;
    protected_regions &= pixel_count(&alert_impact_l, 0, DUEL_HEALTH_Y0, 31, DUEL_HEALTH_Y1) > 0;
    protected_regions &= duel_fb_get(&stack_l, 23, 2) && duel_fb_get(&stack_l, 25, 4);
    protected_regions &= duel_fb_get(&stack_l, 17, DUEL_DIAG_BOTTOM_Y);
    protected_regions &= duel_fb_get(&stack_l, DUEL_SCRY_X0, DUEL_SCRY_Y0);

    const char *alerts[] = {"terminal-completion", "aggregated-normal", "persistent-critical",
                            "aged-alert", "alert-system", "alert-calendar", "alert-other"};
    uint64_t hashes[sizeof alerts / sizeof alerts[0]];
    bool grammar = true;
    for (size_t i = 0; i < sizeof alerts / sizeof alerts[0]; i++) {
        duel_fb_t l, rr;
        const duel_scenario_t *s = duel_scenario_find(alerts[i]);
        duel_scenario_render(s, s->frame, &l, &rr);
        hashes[i] = hash_fb(&l);
        for (size_t j = 0; j < i; j++) grammar &= hashes[i] != hashes[j];
    }
    VCHECK(protected_regions, "visual_protected_regions_precedence");
    VCHECK(grammar, "visual_alert_category_priority_age_persistence");
}

static uint64_t monotonic_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static int benchmark(void) {
    uint64_t worst = 0, total = 0, samples = 0;
    volatile uint64_t sink = 0;
    for (int pass = 0; pass < 200; pass++) {
        for (size_t i = 0; i < duel_scenario_count(); i++) {
            duel_fb_t l, r;
            const duel_scenario_t *s = duel_scenario_at(i);
            uint64_t start = monotonic_ns();
            duel_scenario_render(s, s->frame + (uint32_t)pass, &l, &r);
            uint64_t elapsed = monotonic_ns() - start;
            if (elapsed > worst) worst = elapsed;
            total += elapsed; samples++;
            sink ^= hash_fb(&l) ^ hash_fb(&r);
        }
    }
    printf("desktop framebuffer composition: mean %.3f ms, peak %.3f ms (%llu samples, sink %llx)\n",
           (double)total / (double)samples / 1000000.0, (double)worst / 1000000.0,
           (unsigned long long)samples, (unsigned long long)sink);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--benchmark") == 0) return benchmark();
    bool write = argc == 3 && strcmp(argv[1], "--write-golden") == 0;
    const char *path = write ? argv[2] : (argc == 2 ? argv[1] : NULL);
    if (!path) {
        fprintf(stderr, "usage: %s [--write-golden] <visual.hashes> | --benchmark\n", argv[0]);
        return 2;
    }
    test_catalog();
    test_density_and_distinction();
    test_archive_continuity_and_variation();
    test_isolation_and_restoration();
    test_m12_floor_and_resident();
    test_m12_courier_band_and_city(); // Wave 6
    test_m12_rare_event();            // Wave 7
    test_precedence_and_alert_grammar();
    VCHECK(exact_hashes(path, write), "visual_exact_framebuffer_hashes");
    if (failures) { printf("%d visual test(s) FAILED\n", failures); return 1; }
    printf("all visual tests passed\n");
    return 0;
}
