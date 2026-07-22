#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "duel_draw.h"
#include "duel_courier.h"
#include "duel_event.h"
#include "duel_host.h"
#include "duel_proto.h"
#include "duel_resident.h"
#include "duel_runtime.h"
#include "duel_rgb.h"
#include "duel_sim.h"
#include "duel_view.h"
#include "test_support.h"

static int failures;
static const char *expect_expr; /* first failing EXPECT of the current test */
static int expect_line;

#define CHECK(condition, name) do { \
    if (condition) printf("PASS %s\n", name); \
    else { \
        printf("FAIL %s (%s:%d)\n", name, __FILE__, __LINE__); \
        if (expect_expr) \
            printf("     first failing sub-assertion (line %d): %s\n", \
                   expect_line, expect_expr); \
        failures++; \
    } \
    expect_expr = NULL; \
} while (0)

/* Folds a sub-assertion into the enclosing test's `ok` accumulator (single
 * evaluation — conditions may have side effects) and records the first
 * failure so CHECK can print WHICH condition sank the test. */
#define EXPECT(cond) do { \
    bool expect_ok_ = (cond); \
    if (!expect_ok_ && !expect_expr) { expect_expr = #cond; expect_line = __LINE__; } \
    ok &= expect_ok_; \
} while (0)

static void install_spell(sim_world_t *w, uint8_t side, uint32_t desc, uint8_t progress);
static void land_spell(sim_world_t *w, uint8_t side, uint32_t desc);
static void idle_step(sim_world_t *w);
static uint64_t incantation_bytes_hash(const void *data, size_t size);

static uint32_t desc_set_magnitude_for_test(uint32_t desc, uint8_t magnitude) {
    return (desc & ~(3u << 10)) | ((uint32_t)(magnitude - 1u) << 10);
}

static void step(sim_world_t *w, uint32_t left, uint32_t right,
                 uint8_t llayer, uint8_t rlayer,
                 const sim_event_t *events, uint8_t n) {
    sim_inputs_t in = {0};
    in.held_pos[0] = left;
    in.held_pos[1] = right;
    in.layer[0] = llayer;
    in.layer[1] = rlayer;
    if (left) in.down_mask |= 1u;
    if (right) in.down_mask |= 2u;
    sim_tick(w, in, events, n, 0);
}

static void tap(sim_world_t *w, uint8_t side, uint8_t row, uint8_t col, uint8_t layer) {
    sim_event_t event = SIM_EV_PACK(SIM_EV_KEYDOWN, side, row, col);
    uint32_t held = 1u << (row * 6u + col);
    step(w, side ? 0 : held, side ? held : 0, side ? 0 : layer, side ? layer : 0, &event, 1);
    step(w, 0, 0, 0, 0, NULL, 0);
}

static void wait_ticks(sim_world_t *w, unsigned ticks) {
    while (ticks--) step(w, 0, 0, 0, 0, NULL, 0);
}

static void release_recipe(sim_world_t *w, uint8_t side, uint8_t row) {
    tap(w, side, row, 1, 0);
    wait_ticks(w, INCANTATION_IDLE_COMMIT_TICKS - 1u);
    for (unsigned guard = 0; guard < INCANTATION_WINDUP_MAX_TICKS + 2u && !w->spell[side].active; guard++)
        step(w, 0, 0, 0, 0, NULL, 0);
}

static void test_layout_and_protocol(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    duel_snapshot_t packet;
    duel_encode_external_alert_display(&w, 9, 0x1234, 0x55, 0x2a, 2, &packet);
    duel_snapshot_set_civic(&packet, 1, 2, 3, 4);
    bool ok = true;
    EXPECT(sizeof(duel_view_t) == 19 && sizeof(duel_snapshot_t) == 32 &&
              DUEL_VER == 11 && sizeof(duel_render_t) <= 40u &&
              sizeof(sim_world_t) <= 56u + 1024u &&
              duel_decode_valid(&packet));
    duel_snapshot_t bad = packet;
    for (size_t i = 0; i < offsetof(duel_snapshot_t, crc); i++) {
        ((uint8_t *)&bad)[i] ^= 0x40u;
        EXPECT(!duel_decode_valid(&bad));
        bad = packet;
    }
    bad.ver = 9;
    bad.crc = duel_crc8(&bad, offsetof(duel_snapshot_t, crc));
    EXPECT(!duel_decode_valid(&bad));
    /* Non-canonical residue (element set on an empty zone) and an
     * out-of-range display phase are the v11 range checks with teeth. */
    bad = packet; bad.residue = 0x01u;
    bad.crc = duel_crc8(&bad, offsetof(duel_snapshot_t, crc));
    EXPECT(!duel_decode_valid(&bad));
    bad = packet; bad.flags |= DUEL_FLAGS_DISPLAY_PACK(3u);
    bad.crc = duel_crc8(&bad, offsetof(duel_snapshot_t, crc));
    EXPECT(!duel_decode_valid(&bad));
    bad = packet; bad.shared_pres = DUEL_VISITOR_PACK(7u, 0u, 0u); bad.revision = 0u;
    bad.crc = duel_crc8(&bad, offsetof(duel_snapshot_t, crc));
    EXPECT(!duel_decode_valid(&bad));
    bad = packet; bad.revision = DUEL_EVENT_PACK(DUEL_CIVIC_EVENT_WORK_BREAK,
        DUEL_CIVIC_EVENT_PHASE_ACTIVE, 3u);
    bad.crc = duel_crc8(&bad, offsetof(duel_snapshot_t, crc));
    EXPECT(!duel_decode_valid(&bad));
    bad = packet; bad.revision = INCANTATION_AFTERMATH_WIRE | 0x10u;
    bad.crc = duel_crc8(&bad, offsetof(duel_snapshot_t, crc));
    EXPECT(!duel_decode_valid(&bad));
    CHECK(ok, "incantation_v11_exact_size_crc_and_version_rejection");
}

static void test_v11_repack_and_sky_subphase(void) {
    bool ok = true;
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    duel_snapshot_t p;
    w.fx_seq = 0x37u; /* only the low nibble reaches the wire */
    test_encode_snapshot(&w, 5, 258u, &p); /* wide caller counter truncates to a byte */
    EXPECT(p.seq == 2u && VIEW_FX_SEQ(p.view.fx_stance) == 7u &&
           VIEW_FX_STANCE(p.view.fx_stance, SIM_SIDE_L) == DUEL_STANCE_NONE &&
           VIEW_FX_STANCE(p.view.fx_stance, SIM_SIDE_R) == DUEL_STANCE_NONE &&
           duel_decode_valid(&p));

    /* Residue round-trip across every zone and value through the production
     * world encoder, including zone 3's byte-straddling intensity. */
    uint8_t civic = DUEL_CIVIC_PACK(2u, 1u, 3u);
    uint8_t secondary = DUEL_SECONDARY_SKY_SUB_PACK(
        DUEL_SECONDARY_SKY_PACK(DUEL_CIVIC_SECONDARY_MEDIA, DUEL_SKY_DUSK), 3u);
    for (uint8_t zone = 0; zone < DUEL_RESIDUE_ZONES; zone++)
        for (uint8_t elem = 0; elem < 4u; elem++)
            for (uint8_t inten = 1; inten < 4u; inten++) {
                w.residue[zone] = (sim_residue_t){elem, inten, 0u};
                test_encode_snapshot(&w, 5u, 258u, &p);
                duel_snapshot_set_civic(&p, civic, secondary, 0u, 0u);
                EXPECT(duel_snapshot_residue_element(&p, zone) == elem &&
                       duel_snapshot_residue_intensity(&p, zone) == inten &&
                       duel_decode_valid(&p));
                w.residue[zone] = (sim_residue_t){0};
                test_encode_snapshot(&w, 5u, 258u, &p);
                duel_snapshot_set_civic(&p, civic, secondary, 0u, 0u);
                EXPECT(duel_decode_valid(&p));
            }
    /* All four zones loaded at once: the scattered fields never alias. */
    for (uint8_t zone = 0; zone < DUEL_RESIDUE_ZONES; zone++)
        w.residue[zone] = (sim_residue_t){zone, (uint8_t)(3u - (zone & 1u)), 0u};
    test_encode_snapshot(&w, 5u, 258u, &p);
    duel_snapshot_set_civic(&p, civic, secondary, 0u, 0u);
    for (uint8_t zone = 0; zone < DUEL_RESIDUE_ZONES; zone++)
        EXPECT(duel_snapshot_residue_element(&p, zone) == zone &&
               duel_snapshot_residue_intensity(&p, zone) == 3u - (zone & 1u));
    /* The exact straddle boundary: zone 3 intensity 2 sets only the high
     * bit (secondary.7), intensity 1 only the low bit (flags.7). */
    w.residue[DUEL_RESIDUE_DOORSTEP_R] = (sim_residue_t){ELEM_EMBER, 2u, 0u};
    test_encode_snapshot(&w, 5u, 258u, &p);
    duel_snapshot_set_civic(&p, civic, secondary, 0u, 0u);
    EXPECT((p.secondary & 0x80u) != 0u && (p.flags & 0x80u) == 0u);
    w.residue[DUEL_RESIDUE_DOORSTEP_R].intensity = 1u;
    test_encode_snapshot(&w, 5u, 258u, &p);
    duel_snapshot_set_civic(&p, civic, secondary, 0u, 0u);
    EXPECT((p.secondary & 0x80u) == 0u && (p.flags & 0x80u) != 0u);
    /* Through all of it the neighboring fields kept their values. */
    EXPECT(DUEL_CIVIC_FLOOR(p.civic) == 2u && DUEL_CIVIC_MODE(p.civic) == 1u &&
           DUEL_CIVIC_INTENSITY(p.civic) == 3u &&
           DUEL_SECONDARY_ACTIVITY(p.secondary) == DUEL_CIVIC_SECONDARY_MEDIA &&
           DUEL_SECONDARY_SKY_PHASE(p.secondary) == DUEL_SKY_DUSK &&
           DUEL_SECONDARY_SKY_SUBPHASE(p.secondary) == 3u &&
           (p.flags & DUEL_FLAGS_WORLD_VALID) != 0u &&
           duel_decode_valid(&p));

    /* Stance round-trip through the view's shared fx byte. */
    duel_view_t v = p.view;
    v.fx_stance = VIEW_FX_PACK(9u, DUEL_STANCE_STUDY, DUEL_STANCE_FORTIFY);
    EXPECT(VIEW_FX_SEQ(v.fx_stance) == 9u &&
           duel_view_wizard(&v, SIM_SIDE_L).stance == DUEL_STANCE_STUDY &&
           duel_view_wizard(&v, SIM_SIDE_R).stance == DUEL_STANCE_FORTIFY);

    /* The fx nibble wrap still re-arms the flash policy (equality compare),
     * and a later stance change alone must NOT re-arm it. */
    duel_flash_policy_t flash = {0};
    uint8_t last_kind[2] = {0, 0};
    w.fx_kind = FX_IMPACT_R;
    w.fx_seq = 15u;
    duel_view_t fv;
    duel_view_from_world(&w, &fv);
    EXPECT(duel_flash_observe_view(&flash, last_kind, &fv, 100u));
    w.fx_seq = 16u; /* nibble wraps to 0 */
    duel_view_from_world(&w, &fv);
    EXPECT(duel_flash_observe_view(&flash, last_kind, &fv, 200u));
    fv.fx_stance = VIEW_FX_PACK(VIEW_FX_SEQ(fv.fx_stance),
                                DUEL_STANCE_MEDITATE, DUEL_STANCE_NONE);
    EXPECT(!duel_flash_observe_view(&flash, last_kind, &fv, 300u));

    /* v10 (and future v12) frames are rejected outright: a mixed-revision
     * pair takes the established stale-link presentation. */
    duel_snapshot_t old = p;
    old.ver = 10u;
    old.crc = duel_crc8(&old, offsetof(duel_snapshot_t, crc));
    EXPECT(!duel_decode_valid(&old));
    old.ver = 12u;
    old.crc = duel_crc8(&old, offsetof(duel_snapshot_t, crc));
    EXPECT(!duel_decode_valid(&old));

    /* Sky sub-phase: exact quarter boundaries inside every phase, wrap-safe
     * across the cycle. Quarters are 37.5 s in dawn/dusk, 300 s in day,
     * 75 s at night. */
    EXPECT(duel_sky_subphase(0u) == 0u &&
           duel_sky_subphase(37499u) == 0u &&
           duel_sky_subphase(37500u) == 1u &&
           duel_sky_subphase(149999u) == 3u &&
           duel_sky_subphase(150000u) == 0u &&   /* day begins */
           duel_sky_subphase(449999u) == 0u &&
           duel_sky_subphase(450000u) == 1u &&
           duel_sky_subphase(1349999u) == 3u &&
           duel_sky_subphase(1350000u) == 0u &&  /* dusk begins */
           duel_sky_subphase(1387500u) == 1u &&
           duel_sky_subphase(1499999u) == 3u &&
           duel_sky_subphase(1500000u) == 0u &&  /* night begins */
           duel_sky_subphase(1575000u) == 1u &&
           duel_sky_subphase(1799999u) == 3u &&
           duel_sky_subphase(1800000u) == 0u);   /* wraps to dawn */
    CHECK(ok, "v11_repack_residue_stance_fx_nibble_subphase_boundaries_and_version_gate");
}

/* M15 Track A: battlefield residue. Deposits (impact, fizzle, ember/frost
 * clash, singularity collapse, fire/repair aftermath hooks), the ~45 s decay
 * clock, every transmutation row with its once-per-spell flag, ward
 * non-interaction, and the wire path (encode fill, set_civic preservation,
 * decode round-trip, render pack). */
static void test_residue_deposits_decay_and_transmutation(void) {
    bool ok = true;
    sim_world_t w;

    /* Impact deposit: a landed hit stains the defender's doorstep with the
     * spell's element; a fully deflected hit leaves no stain. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t frost_mid = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FROST, PAY_DAMAGE,
                                         TRAJ_MID, 1u, STATUS_NONE, INTERACT_SOLID,
                                         TEMPO_FLOWING, TREND_STEADY, 0u);
    land_spell(&w, SIM_SIDE_L, frost_mid);
    EXPECT(w.wiz[1].hp == SIM_MAX_HP - 1u &&
           w.residue[SIM_RESIDUE_DOORSTEP_R].element == ELEM_FROST &&
           w.residue[SIM_RESIDUE_DOORSTEP_R].intensity == 1u &&
           w.residue[SIM_RESIDUE_DOORSTEP_L].intensity == 0u);
    w.wiz[1].ward_strength = 4u;
    w.wiz[1].ward_capacity = 4u;
    land_spell(&w, SIM_SIDE_L, frost_mid);
    EXPECT(w.wiz[1].hp == SIM_MAX_HP - 1u && /* deflected: no new stain */
           w.residue[SIM_RESIDUE_DOORSTEP_R].intensity == 1u);

    /* Fizzle deposit at a downed wizard's doorstep. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[1].life = LIFE_DOWNED;
    w.wiz[1].life_ticks = SIM_DOWNED_TICKS;
    land_spell(&w, SIM_SIDE_L, frost_mid);
    EXPECT(w.fx_kind == FX_FIZZLE_R &&
           w.residue[SIM_RESIDUE_DOORSTEP_R].element == ELEM_FROST &&
           w.residue[SIM_RESIDUE_DOORSTEP_R].intensity == 1u);

    /* Deposit cap: a non-reacting element deposited onto a full zone
     * overwrites the element and saturates at 3. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.residue[SIM_RESIDUE_DOORSTEP_R] =
        (sim_residue_t){ ELEM_FORCE, 3u, SIM_RESIDUE_DECAY_UNITS };
    land_spell(&w, SIM_SIDE_L, frost_mid); /* frost x force: no reaction */
    EXPECT(w.residue[SIM_RESIDUE_DOORSTEP_R].element == ELEM_FROST &&
           w.residue[SIM_RESIDUE_DOORSTEP_R].intensity == 3u);

    /* Ember x frost clash: both mid zones take their caster's element, and
     * the fire aftermath hook then scorches (and claims) both doorsteps on
     * top of the 1-damage pulse stains. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t ember_mid = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_EMBER, PAY_DAMAGE,
                                         TRAJ_MID, 2u, STATUS_NONE, INTERACT_SOLID,
                                         TEMPO_FLOWING, TREND_STEADY, 0u);
    uint32_t frost_clash = desc_set_magnitude_for_test(frost_mid, 2u);
    install_spell(&w, SIM_SIDE_L, ember_mid, 120u);
    install_spell(&w, SIM_SIDE_R, frost_clash, 130u); /* u = 125, in contact */
    idle_step(&w);
    EXPECT(!w.spell[0].active && !w.spell[1].active &&
           w.residue[SIM_RESIDUE_MID_L].element == ELEM_EMBER &&
           w.residue[SIM_RESIDUE_MID_L].intensity == 1u &&
           w.residue[SIM_RESIDUE_MID_R].element == ELEM_FROST &&
           w.residue[SIM_RESIDUE_MID_R].intensity == 1u &&
           w.residue[SIM_RESIDUE_DOORSTEP_L].element == ELEM_EMBER &&
           w.residue[SIM_RESIDUE_DOORSTEP_L].intensity == 2u &&
           w.residue[SIM_RESIDUE_DOORSTEP_R].element == ELEM_EMBER &&
           w.residue[SIM_RESIDUE_DOORSTEP_R].intensity == 2u &&
           w.wiz[0].hp == SIM_MAX_HP - 1u && w.wiz[1].hp == SIM_MAX_HP - 1u);

    /* Repair aftermath hook: a same-element combine clash starts AFTER_REPAIR
     * on both sides, sweeping one step off each doorstep. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.residue[SIM_RESIDUE_DOORSTEP_L] =
        (sim_residue_t){ ELEM_FORCE, 2u, SIM_RESIDUE_DECAY_UNITS };
    w.residue[SIM_RESIDUE_DOORSTEP_R] =
        (sim_residue_t){ ELEM_FROST, 1u, SIM_RESIDUE_DECAY_UNITS };
    install_spell(&w, SIM_SIDE_L, ember_mid, 120u);
    install_spell(&w, SIM_SIDE_R, desc_set_magnitude_for_test(ember_mid, 1u), 130u);
    idle_step(&w);
    EXPECT(w.fx_kind == FX_COMBINE &&
           w.residue[SIM_RESIDUE_DOORSTEP_L].element == ELEM_FORCE &&
           w.residue[SIM_RESIDUE_DOORSTEP_L].intensity == 1u &&
           w.residue[SIM_RESIDUE_DOORSTEP_R].intensity == 0u &&
           w.residue[SIM_RESIDUE_DOORSTEP_R].element == 0u); /* canonical */

    /* Uncharged singularity collapse scars its own mid zone with void +2. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t sing = SPELL_DESC_PACK(SPELL_SINGULARITY, ELEM_VOID, PAY_DAMAGE,
                                    TRAJ_MID, 2u, STATUS_NONE, INTERACT_ABSORB,
                                    TEMPO_FLOWING, TREND_STEADY, 0u);
    install_spell(&w, SIM_SIDE_L, sing, 0u);
    wait_ticks(&w, 30u); /* > the 28-tick uncharged collapse timeline */
    EXPECT(!w.spell[0].active && w.fx_kind == FX_COLLAPSE &&
           w.residue[SIM_RESIDUE_MID_L].element == ELEM_VOID &&
           w.residue[SIM_RESIDUE_MID_L].intensity == 2u);

    /* Decay boundary: one intensity step per 225 prescaled units (1125
     * ticks); the final step clears the zone to canonical empty. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.residue[SIM_RESIDUE_MID_R] =
        (sim_residue_t){ ELEM_EMBER, 2u, SIM_RESIDUE_DECAY_UNITS };
    wait_ticks(&w, 1115u);
    EXPECT(w.residue[SIM_RESIDUE_MID_R].intensity == 2u);
    wait_ticks(&w, 10u);
    EXPECT(w.residue[SIM_RESIDUE_MID_R].intensity == 1u &&
           w.residue[SIM_RESIDUE_MID_R].element == ELEM_EMBER &&
           w.residue[SIM_RESIDUE_MID_R].decay == SIM_RESIDUE_DECAY_UNITS);
    w.residue[SIM_RESIDUE_MID_R].decay = 1u;
    wait_ticks(&w, SIM_RESIDUE_DECAY_PRESCALE);
    EXPECT(w.residue[SIM_RESIDUE_MID_R].intensity == 0u &&
           w.residue[SIM_RESIDUE_MID_R].element == 0u &&
           w.residue[SIM_RESIDUE_MID_R].decay == 0u);
    CHECK(ok, "residue_deposit_table_aftermath_hooks_cap_and_decay_boundary");
}

static void test_residue_transmutation_rows_and_wire(void) {
    bool ok = true;
    sim_world_t w;

    /* Steam burst: ember spell over a frost zone clears the zone and lands a
     * 1-damage area pulse through the ordinary path (which itself stains the
     * defender's doorstep). The spell flies on, reaction spent. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t ember_mid = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_EMBER, PAY_DAMAGE,
                                         TRAJ_MID, 1u, STATUS_NONE, INTERACT_SOLID,
                                         TEMPO_FLOWING, TREND_STEADY, 0u);
    w.residue[SIM_RESIDUE_MID_R] =
        (sim_residue_t){ ELEM_FROST, 2u, SIM_RESIDUE_DECAY_UNITS };
    install_spell(&w, SIM_SIDE_L, ember_mid, 170u);
    idle_step(&w);
    EXPECT(w.fx_kind == FX_DETONATE && w.spell[0].active &&
           (w.spell[0].resolved & SPELL_RESOLVED_REACTED) != 0u &&
           w.residue[SIM_RESIDUE_MID_R].intensity == 0u &&
           w.residue[SIM_RESIDUE_MID_R].element == 0u &&
           w.wiz[1].hp == SIM_MAX_HP - 1u &&
           w.residue[SIM_RESIDUE_DOORSTEP_R].element == ELEM_EMBER);

    /* Void absorb: zone -1, spell magnitude +1. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t void_low = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_VOID, PAY_DAMAGE,
                                        TRAJ_LOW, 2u, STATUS_NONE, INTERACT_SOLID,
                                        TEMPO_FLOWING, TREND_STEADY, 0u);
    w.residue[SIM_RESIDUE_MID_L] =
        (sim_residue_t){ ELEM_EMBER, 2u, SIM_RESIDUE_DECAY_UNITS };
    install_spell(&w, SIM_SIDE_L, void_low, 60u);
    idle_step(&w);
    EXPECT(SPELL_DESC_MAGNITUDE(w.spell[0].descriptor) == 3u &&
           w.residue[SIM_RESIDUE_MID_L].intensity == 1u &&
           w.residue[SIM_RESIDUE_MID_L].element == ELEM_EMBER &&
           (w.spell[0].resolved & SPELL_RESOLVED_REACTED) != 0u);
    /* Once per lifetime: recharging the zone provokes nothing further. */
    w.residue[SIM_RESIDUE_MID_L] =
        (sim_residue_t){ ELEM_EMBER, 2u, SIM_RESIDUE_DECAY_UNITS };
    idle_step(&w);
    EXPECT(SPELL_DESC_MAGNITUDE(w.spell[0].descriptor) == 3u &&
           w.residue[SIM_RESIDUE_MID_L].intensity == 2u);

    /* Force x force rubble: zone -1, trajectory bumps one lane. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t force_low = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE,
                                         TRAJ_LOW, 2u, STATUS_NONE, INTERACT_SOLID,
                                         TEMPO_FLOWING, TREND_STEADY, 0u);
    w.residue[SIM_RESIDUE_MID_L] =
        (sim_residue_t){ ELEM_FORCE, 3u, SIM_RESIDUE_DECAY_UNITS };
    install_spell(&w, SIM_SIDE_L, force_low, 60u);
    idle_step(&w);
    EXPECT(SPELL_DESC_TRAJECTORY(w.spell[0].descriptor) == TRAJ_MID &&
           SPELL_DESC_MAGNITUDE(w.spell[0].descriptor) == 2u &&
           w.residue[SIM_RESIDUE_MID_L].intensity == 2u);

    /* Same-element feed: zone -1, magnitude +1 — and an unmatched pair
     * (frost over force) neither reacts nor spends the flag. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t frost_low = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FROST, PAY_DAMAGE,
                                         TRAJ_LOW, 1u, STATUS_NONE, INTERACT_SOLID,
                                         TEMPO_FLOWING, TREND_STEADY, 0u);
    w.residue[SIM_RESIDUE_MID_L] =
        (sim_residue_t){ ELEM_FORCE, 2u, SIM_RESIDUE_DECAY_UNITS };
    install_spell(&w, SIM_SIDE_L, frost_low, 55u);
    idle_step(&w);
    EXPECT((w.spell[0].resolved & SPELL_RESOLVED_REACTED) == 0u &&
           w.residue[SIM_RESIDUE_MID_L].intensity == 2u);
    w.residue[SIM_RESIDUE_MID_L].element = ELEM_FROST;
    idle_step(&w);
    EXPECT((w.spell[0].resolved & SPELL_RESOLVED_REACTED) != 0u &&
           SPELL_DESC_MAGNITUDE(w.spell[0].descriptor) == 2u &&
           w.residue[SIM_RESIDUE_MID_L].intensity == 1u);

    /* Wire: the encoder fills all four zones from the world, set_civic
     * preserves the borrowed zone-3 bits under a full civic rewrite, decode
     * round-trips them, and the render pack agrees on both fill paths. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.residue[SIM_RESIDUE_DOORSTEP_L] = (sim_residue_t){ ELEM_EMBER, 1u, 10u };
    w.residue[SIM_RESIDUE_MID_L]      = (sim_residue_t){ ELEM_FROST, 2u, 10u };
    w.residue[SIM_RESIDUE_MID_R]      = (sim_residue_t){ ELEM_FORCE, 3u, 10u };
    w.residue[SIM_RESIDUE_DOORSTEP_R] = (sim_residue_t){ ELEM_VOID,  3u, 10u };
    duel_snapshot_t p;
    test_encode_snapshot(&w, 5u, 7u, &p);
    EXPECT(duel_decode_valid(&p) &&
           duel_snapshot_residue_element(&p, DUEL_RESIDUE_DOORSTEP_L) == ELEM_EMBER &&
           duel_snapshot_residue_intensity(&p, DUEL_RESIDUE_DOORSTEP_L) == 1u &&
           duel_snapshot_residue_element(&p, DUEL_RESIDUE_MID_L) == ELEM_FROST &&
           duel_snapshot_residue_intensity(&p, DUEL_RESIDUE_MID_L) == 2u &&
           duel_snapshot_residue_element(&p, DUEL_RESIDUE_MID_R) == ELEM_FORCE &&
           duel_snapshot_residue_intensity(&p, DUEL_RESIDUE_MID_R) == 3u &&
           duel_snapshot_residue_element(&p, DUEL_RESIDUE_DOORSTEP_R) == ELEM_VOID &&
           duel_snapshot_residue_intensity(&p, DUEL_RESIDUE_DOORSTEP_R) == 3u);
    uint8_t sec = DUEL_SECONDARY_SKY_SUB_PACK(
        DUEL_SECONDARY_SKY_PACK(DUEL_CIVIC_SECONDARY_MEDIA, DUEL_SKY_NIGHT), 3u);
    /* Deliberately dirty the incoming residue-owned bits: set_civic must
     * mask them and keep the encoder's zone-3 value. */
    duel_snapshot_set_civic(&p, DUEL_CIVIC_PACK(2u, 1u, 3u) | 0xC0u,
                            (uint8_t)(sec | 0x80u), 0u, 0u);
    EXPECT(duel_decode_valid(&p) &&
           duel_snapshot_residue_element(&p, DUEL_RESIDUE_DOORSTEP_R) == ELEM_VOID &&
           duel_snapshot_residue_intensity(&p, DUEL_RESIDUE_DOORSTEP_R) == 3u &&
           DUEL_CIVIC_FLOOR(p.civic) == 2u &&
           (p.secondary & 0x7Fu) == sec); /* semantics kept, straddle intact */
    /* Direct accessors are the packet contract; no reconstructed world API is
     * needed by production or by this assertion. */
    for (uint8_t zone = 0; zone < SIM_RESIDUE_ZONES; zone++)
        EXPECT(duel_snapshot_residue_element(&p, zone) == w.residue[zone].element &&
               duel_snapshot_residue_intensity(&p, zone) == w.residue[zone].intensity);
    uint8_t from_world[2], from_snapshot[2];
    duel_residue_pack(&w, from_world);
    duel_snapshot_residue_render(&p, from_snapshot);
    duel_render_t render = {0};
    duel_render_from_world(&render, &w);
    EXPECT(from_world[0] == from_snapshot[0] && from_world[1] == from_snapshot[1] &&
           render.residue[0] == from_world[0] && render.residue[1] == from_world[1] &&
           DUEL_RENDER_RESIDUE_ELEMENT(&render, SIM_RESIDUE_MID_R) == ELEM_FORCE &&
           DUEL_RENDER_RESIDUE_INTENSITY(&render, SIM_RESIDUE_MID_R) == 3u &&
           DUEL_RENDER_RESIDUE_ELEMENT(&render, SIM_RESIDUE_DOORSTEP_R) == ELEM_VOID &&
           DUEL_RENDER_RESIDUE_INTENSITY(&render, SIM_RESIDUE_DOORSTEP_R) == 3u);
    CHECK(ok, "residue_transmutation_rows_once_per_spell_and_v11_wire_paths");
}

static void test_host_protocol_current_payload_and_ordering(void) {
    duel_host_packet_t hello;
    test_build_host_packet(DUEL_HOST_MSG_HELLO, 0x11223344u, 0,
                     DUEL_HOST_SCENE_ARCHIVE, 2,
                     DUEL_HOST_CATEGORY_COMMUNICATION,
                     DUEL_HOST_PRIORITY_NORMAL, 3, false,
                     DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_RESEARCH,
                                     DUEL_CIVIC_MODE_NORMAL,
                                     DUEL_CIVIC_INTENSITY_ACTIVE),
                     DUEL_SECONDARY_PACK(DUEL_CIVIC_SECONDARY_MEDIA),
                     &hello);
    duel_host_state_t state = {0};
    bool ok = true;
    EXPECT(sizeof hello == DUEL_HOST_REPORT_SIZE &&
              hello.version == DUEL_HOST_VERSION &&
              hello.payload_len == DUEL_HOST_PAYLOAD_LEN &&
              duel_host_packet_valid(&hello) &&
              duel_host_accept(&state, &hello) &&
              DUEL_HOST_CONTEXT_SCENE(duel_host_context(&state)) == DUEL_HOST_SCENE_ARCHIVE &&
              DUEL_CIVIC_FLOOR(duel_host_civic(&state)) == DUEL_CIVIC_FLOOR_RESEARCH);

    duel_host_packet_t heartbeat;
    test_build_host_packet(DUEL_HOST_MSG_HEARTBEAT, 0x11223344u, 1,
                     DUEL_HOST_SCENE_FOCUS, 0,
                     DUEL_HOST_CATEGORY_NONE, DUEL_HOST_PRIORITY_NONE, 0, false,
                     DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_WORKSHOP,
                                     DUEL_CIVIC_MODE_QUIET,
                                     DUEL_CIVIC_INTENSITY_CALM),
                     DUEL_SECONDARY_PACK(DUEL_CIVIC_SECONDARY_NONE),
                     &heartbeat);
    EXPECT(duel_host_accept(&state, &heartbeat));
    EXPECT(!duel_host_accept(&state, &heartbeat));

    duel_host_packet_t bad = heartbeat;
    bad.payload_len = 6;
    bad.crc = duel_crc8(&bad, offsetof(duel_host_packet_t, crc));
    EXPECT(!duel_host_packet_valid(&bad));
    bad = heartbeat;
    bad.version = 1;
    bad.crc = duel_crc8(&bad, offsetof(duel_host_packet_t, crc));
    EXPECT(!duel_host_packet_valid(&bad));
    bad = heartbeat;
    bad.payload[DUEL_HOST_PAYLOAD_SECONDARY] = 0x80u;
    bad.crc = duel_crc8(&bad, offsetof(duel_host_packet_t, crc));
    EXPECT(!duel_host_packet_valid(&bad));
    /* Reserved tail bytes beyond payload_len must ship zero: CRC-covered
     * garbage there is rejected so the space stays usable for future
     * versions. */
    bad = heartbeat;
    bad.payload[DUEL_HOST_PAYLOAD_LEN] = 1u;
    bad.crc = duel_crc8(&bad, offsetof(duel_host_packet_t, crc));
    EXPECT(!duel_host_packet_valid(&bad));
    bad = heartbeat;
    bad.payload[DUEL_HOST_PAYLOAD_SIZE - 1u] = 0xffu;
    bad.crc = duel_crc8(&bad, offsetof(duel_host_packet_t, crc));
    EXPECT(!duel_host_packet_valid(&bad));
    CHECK(ok, "host_v2_eight_byte_payload_malformed_stale_ordering_and_reserved_tail");
}

static void test_view_validation(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[0].ward_strength = 4;
    w.wiz[0].status = STATUS_FROZEN;
    w.wiz[0].status_intensity = 3;
    w.wiz[0].status_ticks = 125;
    w.spell[0].active = 1;
    w.spell[0].descriptor = SPELL_DESC_PACK(SPELL_BEAM, ELEM_FROST, PAY_HYBRID,
                                             TRAJ_AREA, 4, STATUS_FROZEN,
                                             INTERACT_SOLID, TEMPO_RAPID,
                                             TREND_ACCELERATING, 2);
    w.spell[0].progress = 91;
    duel_view_t view;
    duel_view_from_world(&w, &view);
    bool ok = true;
    EXPECT(duel_view_valid(&view));
    duel_view_spell_t spell = duel_view_spell(&view, 0);
    duel_view_wizard_t wizard = duel_view_wizard(&view, 0);
    EXPECT(spell.active && spell.descriptor == w.spell[0].descriptor && spell.progress == 91);
    EXPECT(wizard.hp == SIM_MAX_HP && wizard.ward_strength == 4 && wizard.status == STATUS_FROZEN);
    duel_view_t bad = view;
    bad.wizard[0][0] = (uint8_t)((bad.wizard[0][0] & 0xf0u) | 13u);
    EXPECT(!duel_view_valid(&bad));
    bad = view; bad.wizard[0][0] = (uint8_t)((bad.wizard[0][0] & 0x8fu) | (5u << 4));
    EXPECT(!duel_view_valid(&bad));
    bad = view; bad.outcome_overlay |= 0x80u;
    EXPECT(!duel_view_valid(&bad));
    bad = view; memset(bad.spell[1], 0, 3); bad.spell[1][3] = 1;
    EXPECT(!duel_view_valid(&bad));
    bad = view; bad.spell[0][1] |= 0x70u; /* reserved status encoding */
    EXPECT(!duel_view_valid(&bad));
    CHECK(ok, "incantation_view_roundtrip_and_reserved_validation");
}

static void test_complexity_formula(void) {
    sim_incantation_t inc = {0};
    inc.key_count = 70;
    inc.seen_pos = 0xffffu;
    inc.turns = 20;
    inc.layer_transitions = 9;
    inc.overlap_peak = 5;
    inc.rhythm_changes = 9;
    bool ok = true;
    EXPECT(incantation_complexity(&inc) == 255);
    memset(&inc, 0, sizeof inc);
    inc.key_count = 3;             /* 6 */
    inc.seen_pos = 7;              /* 9 */
    inc.turns = 2;                 /* 4 */
    inc.layer_transitions = 1;     /* 4 */
    inc.overlap_peak = 2;          /* 8 */
    inc.rhythm_changes = 1;        /* 3 = 34 */
    EXPECT(incantation_complexity(&inc) == 34);
    CHECK(ok, "incantation_complexity_exact_and_clamped");
}

static sim_incantation_t incantation_at_complexity(uint8_t target) {
    sim_incantation_t inc;
    memset(&inc, 0, sizeof inc);
    inc.hash = 0x6d31332eu;
    inc.gap_min = 1u;
    for (uint8_t keys = 0; keys <= 64u; keys++) {
        for (uint8_t unique = 0; unique <= 16u; unique++) {
            for (uint8_t turns = 0; turns <= 16u; turns++) {
                inc.key_count = keys;
                inc.seen_pos = unique == 0u ? 0u : (1u << unique) - 1u;
                inc.turns = turns;
                if (incantation_complexity(&inc) == target) {
                    inc.row_hist[1] = keys ? keys : 1u;
                    inc.row_recent[1] = 1u;
                    return inc;
                }
            }
        }
    }
    inc.key_count = 0xffu; /* impossible sentinel: the test will fail */
    return inc;
}

static void test_magnitude_thresholds(void) {
    static const uint8_t complexity[] = {47u, 48u, 111u, 112u, 191u, 192u};
    static const uint8_t magnitude[] = {1u, 2u, 2u, 3u, 3u, 4u};
    bool ok = true;
    for (size_t i = 0; i < sizeof complexity; i++) {
        sim_incantation_t inc = incantation_at_complexity(complexity[i]);
        EXPECT(inc.key_count != 0xffu && incantation_complexity(&inc) == complexity[i] &&
              SPELL_DESC_MAGNITUDE(incantation_compile(&inc, 0, SIM_TEMPER_NEUTRAL)) == magnitude[i]);
    }
    sim_incantation_t saturated = {0};
    saturated.hash = 1u; saturated.key_count = 64u; saturated.seen_pos = 0xffffu;
    saturated.turns = 16u; saturated.layer_transitions = 8u;
    saturated.overlap_peak = 5u; saturated.rhythm_changes = 8u;
    saturated.row_hist[1] = 64u;
    EXPECT(SPELL_DESC_MAGNITUDE(incantation_compile(&saturated, 0, SIM_TEMPER_NEUTRAL)) == 4u);
    CHECK(ok, "incantation_magnitude_thresholds_48_112_192");
}

static void test_compiler_determinism_and_gates(void) {
    sim_incantation_t inc = {0};
    inc.hash = 0x12345678u;
    inc.key_count = 1;
    inc.seen_pos = 1;
    inc.row_hist[0] = 1;
    inc.row_recent[0] = 1;
    inc.gap_min = 1;
    uint32_t a = incantation_compile(&inc, 0, SIM_TEMPER_NEUTRAL), b = incantation_compile(&inc, 0, SIM_TEMPER_NEUTRAL);
    bool ok = true;
    EXPECT(a == b && SPELL_DESC_FORM(a) == SPELL_PROJECTILE &&
              SPELL_DESC_ELEMENT(a) == ELEM_FROST && (a & 0xff000000u) == 0 &&
              SPELL_DESC_VALID(a));

    /* Track T ladder: complexity 64 opens the first four forms (ground wave
     * joined at the new 48 gate) but never beam/singularity. */
    memset(&inc, 0, sizeof inc);
    inc.key_count = 8;       /* 16 */
    inc.seen_pos = 0xffffu;  /* 48 => 64 */
    inc.row_hist[1] = 8; inc.row_recent[1] = 1; inc.gap_min = 0;
    bool saw_non_projectile = false;
    for (uint32_t h = 1; h < 500; h++) {
        inc.hash = h * 2654435761u;
        uint8_t form = SPELL_DESC_FORM(incantation_compile(&inc, 1, SIM_TEMPER_NEUTRAL));
        EXPECT(form == SPELL_PROJECTILE || form == SPELL_FIREBALL ||
               form == SPELL_SWARM || form == SPELL_GROUND_WAVE);
        saw_non_projectile |= form != SPELL_PROJECTILE;
    }
    EXPECT(saw_non_projectile);

    /* Track T's headline promise: every form is reachable once complexity
     * hits 160 (the old ladder held the full roster hostage above 224). */
    sim_incantation_t open = incantation_at_complexity(160u);
    uint32_t seen_forms = 0;
    for (uint32_t h = 1; h < 2000; h++) {
        open.hash = h * 2654435761u;
        seen_forms |= 1u << SPELL_DESC_FORM(incantation_compile(&open, 0, SIM_TEMPER_NEUTRAL));
    }
    EXPECT(open.key_count != 0xffu && seen_forms == 0xffu);
    CHECK(ok, "incantation_compiler_determinism_privacy_and_complexity_gate");
}

static void test_compiler_reachability(void) {
    uint32_t forms = 0, elements = 0, payloads = 0, trajectories = 0;
    uint32_t magnitudes = 0, statuses = 0, interactions = 0, tempos = 0, trends = 0;
    for (uint32_t i = 0; i < 8192u; i++) {
        for (uint8_t row = 0; row < 4; row++) {
            sim_incantation_t inc = {0};
            inc.hash = i * 2654435761u + row * 0x9e37u;
            inc.key_count = (uint8_t)(1u + i % 80u);
            uint8_t unique = (uint8_t)(1u + (i / 3u) % 24u);
            inc.seen_pos = unique == 24u ? 0x00ffffffu : ((1u << unique) - 1u);
            inc.turns = (uint8_t)((i / 5u) % 20u);
            inc.layer_transitions = (uint8_t)((i / 7u) % 10u);
            inc.overlap_peak = (uint8_t)(1u + (i / 11u) % 6u);
            inc.rhythm_changes = (uint8_t)((i / 13u) % 10u);
            inc.row_hist[row] = inc.key_count;
            inc.row_recent[row] = 4;
            inc.held_ticks = (i & 8u) ? (uint16_t)inc.key_count * 4u : 0u;
            inc.column_drift = (i & 16u) ? 6 : -2;
            uint8_t avg = (uint8_t)(1u + (i / 17u) % 6u);
            inc.gap_count = 3; inc.gap_sum = (uint16_t)avg * 3u;
            inc.gap_min = avg; inc.gap_max = (i & 32u) ? (uint8_t)(avg + 4u) : avg;
            inc.first_gap = (uint8_t)(avg + ((i >> 6) & 1u));
            inc.last_gap = (uint8_t)(avg + ((i >> 7) & 1u));
            uint32_t desc = incantation_compile(&inc, (uint8_t)(i & 3u), SIM_TEMPER_NEUTRAL);
            forms |= 1u << SPELL_DESC_FORM(desc);
            elements |= 1u << SPELL_DESC_ELEMENT(desc);
            payloads |= 1u << SPELL_DESC_PAYLOAD(desc);
            trajectories |= 1u << SPELL_DESC_TRAJECTORY(desc);
            magnitudes |= 1u << (SPELL_DESC_MAGNITUDE(desc) - 1u);
            statuses |= 1u << SPELL_DESC_STATUS(desc);
            interactions |= 1u << SPELL_DESC_INTERACTION(desc);
            tempos |= 1u << SPELL_DESC_TEMPO(desc);
            trends |= 1u << SPELL_DESC_TREND(desc);
        }
    }
    bool ok = true;
    EXPECT(forms == 0xffu && elements == 0x0fu && payloads == 0x0fu &&
              trajectories == 0xffu && magnitudes == 0x0fu &&
              (statuses & 0x1fu) == 0x1fu && interactions == 0x0fu &&
              tempos == 0x0fu && trends == 0x0fu);
    CHECK(ok, "incantation_initial_descriptor_attribute_reachability");
}

static void test_independent_accumulators_and_commit(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    sim_event_t events[2] = {
        SIM_EV_PACK(SIM_EV_KEYDOWN, 0, 0, 1),
        SIM_EV_PACK(SIM_EV_KEYDOWN, 1, 2, 4),
    };
    step(&w, 1u << 1, 1u << 16, 0, 2, events, 2);
    step(&w, 0, 0, 0, 0, NULL, 0);
    bool ok = true;
    EXPECT(w.wiz[0].inc.key_count == 1 && w.wiz[1].inc.key_count == 1 &&
              w.wiz[0].inc.seen_pos == (1u << 1) && w.wiz[1].inc.seen_pos == (1u << 16) &&
              w.wiz[0].inc.hash != w.wiz[1].inc.hash);
    wait_ticks(&w, INCANTATION_IDLE_COMMIT_TICKS - 1u);
    EXPECT(w.wiz[0].inc_state == INC_WINDUP && w.wiz[1].inc_state == INC_WINDUP);
    uint32_t left = w.wiz[0].pending_desc, right = w.wiz[1].pending_desc;
    EXPECT(SPELL_DESC_ELEMENT(left) == ELEM_FROST && SPELL_DESC_ELEMENT(right) == ELEM_EMBER);
    CHECK(ok, "incantation_simultaneous_per_half_idle_commit");
}

static void test_forced_cap_and_rearm(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    sim_event_t event = SIM_EV_PACK(SIM_EV_KEYDOWN, 0, 1, 2);
    uint32_t held = 1u << 8;
    step(&w, held, 0, 0, 0, &event, 1);
    for (unsigned i = 1; i < INCANTATION_FORCE_COMMIT_TICKS; i++) step(&w, held, 0, 0, 0, NULL, 0);
    bool ok = true;
    EXPECT(w.wiz[0].rearm_lock && w.wiz[0].inc_state == INC_WINDUP);
    uint8_t count = w.wiz[0].inc.key_count;
    sim_event_t ignored = SIM_EV_PACK(SIM_EV_KEYDOWN, 0, 0, 0);
    step(&w, held | 1u, 0, 0, 0, &ignored, 1);
    EXPECT(w.wiz[0].inc.key_count == count);
    step(&w, 0, 0, 0, 0, NULL, 0);
    EXPECT(!w.wiz[0].rearm_lock);
    CHECK(ok, "incantation_ten_second_force_commit_full_release_rearm");
}

static void test_release_and_prepared(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    release_recipe(&w, 0, 1);
    bool ok = true;
    EXPECT(w.spell[0].active && SPELL_DESC_VALID(w.spell[0].descriptor));
    tap(&w, 0, 2, 2, 0);
    wait_ticks(&w, INCANTATION_IDLE_COMMIT_TICKS - 1u);
    unsigned guard = 0;
    while (w.wiz[0].inc_state == INC_WINDUP && guard++ < 60) step(&w, 0, 0, 0, 0, NULL, 0);
    EXPECT(w.wiz[0].inc_state == INC_PREPARED && w.wiz[0].prepared && w.wiz[0].ward_strength);
    while (w.wiz[0].prepared && guard++ < 120) step(&w, 0, 0, 0, 0, NULL, 0);
    EXPECT(w.spell[0].active && !w.wiz[0].prepared);
    CHECK(ok, "incantation_one_active_one_prepared_auto_release");
}

static void test_windup_ignored_input_and_interruption(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    tap(&w, 0, 1, 1, 0);
    wait_ticks(&w, INCANTATION_IDLE_COMMIT_TICKS - 1u);
    bool ok = true;
    EXPECT(w.wiz[0].inc_state == INC_WINDUP && w.wiz[0].ward_strength == 1);
    uint8_t count = w.wiz[0].inc.key_count;
    sim_event_t ignored = SIM_EV_PACK(SIM_EV_KEYDOWN, 0, 2, 5);
    step(&w, 1u << 17, 0, 3, 0, &ignored, 1);
    EXPECT(w.wiz[0].inc.key_count == count);
    step(&w, 0, 0, 0, 0, NULL, 0);

    uint32_t hostile = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_VOID, PAY_DAMAGE,
                                       TRAJ_LOW, 2, STATUS_NONE, INTERACT_PHASE,
                                       TEMPO_RAPID, TREND_STEADY, 0);
    land_spell(&w, 1, hostile);
    EXPECT(w.wiz[0].inc_state == INC_IDLE && !w.wiz[0].pending_desc &&
          !w.wiz[0].prepared && !w.wiz[0].ward_strength &&
          !w.wiz[0].ward_capacity);
    CHECK(ok, "incantation_windup_input_ignored_and_unblocked_contact_interrupts");
}

static void install_spell(sim_world_t *w, uint8_t side, uint32_t desc, uint8_t progress) {
    sim_spell_t *sp = &w->spell[side];
    memset(sp, 0, sizeof *sp);
    sp->active = 1;
    sp->descriptor = desc;
    sp->progress = progress;
    sp->dir = side ? -1 : 1;
}

/* Installed here, one idle tick carries a ballistic spell across the
 * defender's doorstep and resolves contact. */
#define SIM_CONTACT_PROGRESS 239u

static void idle_step(sim_world_t *w) { step(w, 0, 0, 0, 0, NULL, 0); }

static void land_spell(sim_world_t *w, uint8_t side, uint32_t desc) {
    install_spell(w, side, desc, SIM_CONTACT_PROGRESS);
    idle_step(w);
}

static void test_ward_capacity_semantics(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    tap(&w, 0, 1, 1, 0);
    bool ok = true;
    EXPECT(w.wiz[0].inc_state == INC_COLLECTING &&
              w.wiz[0].ward_capacity == 1u && w.wiz[0].ward_strength == 1u);

    uint32_t chip = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE,
                                    TRAJ_MID, 1, STATUS_NONE, INTERACT_SOLID,
                                    TEMPO_FLOWING, TREND_STEADY, 0);
    land_spell(&w, 1, chip);
    EXPECT(w.wiz[0].hp == SIM_MAX_HP && w.wiz[0].ward_capacity == 1u &&
          w.wiz[0].ward_strength == 0u && w.wiz[0].inc_state == INC_COLLECTING);

    bool stayed_spent_below_threshold = true;
    unsigned guard = 0;
    while (w.wiz[0].ward_capacity < 2u && guard++ < 64u) {
        uint8_t pos = (uint8_t)((guard * 7u) % 24u);
        tap(&w, 0, pos / 6u, pos % 6u, (uint8_t)(guard & 3u));
        if (w.wiz[0].ward_capacity == 1u) stayed_spent_below_threshold &=
            w.wiz[0].ward_strength == 0u;
    }
    EXPECT(stayed_spent_below_threshold && w.wiz[0].ward_capacity == 2u &&
          w.wiz[0].ward_strength == 1u); /* only the newly crossed tier returns */

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    tap(&w, 0, 1, 1, 0);
    uint32_t two = desc_set_magnitude_for_test(chip, 2u);
    land_spell(&w, 1, two);
    EXPECT(w.wiz[0].hp == SIM_MAX_HP - 1u && !w.wiz[0].ward_strength &&
          !w.wiz[0].ward_capacity && w.wiz[0].inc_state == INC_IDLE);

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[0].ward_capacity = 2u; w.wiz[0].ward_strength = 2u;
    w.wiz[0].ward_focus = 2u;
    uint32_t high = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FROST, PAY_DAMAGE,
                                    TRAJ_HIGH, 1, STATUS_NONE, INTERACT_SOLID,
                                    TEMPO_FLOWING, TREND_STEADY, 0);
    land_spell(&w, 1, high);
    EXPECT(w.wiz[0].hp == SIM_MAX_HP && w.wiz[0].ward_strength == 1u);

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[0].ward_capacity = 1u; w.wiz[0].ward_strength = 1u;
    w.wiz[0].ward_focus = 2u;
    land_spell(&w, 1, high);
    EXPECT(w.wiz[0].hp == SIM_MAX_HP - 1u && !w.wiz[0].ward_capacity);

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    release_recipe(&w, 0, 1);
    EXPECT(w.spell[0].active && !w.wiz[0].ward_strength &&
          !w.wiz[0].ward_capacity);
    CHECK(ok, "incantation_ward_capacity_spend_growth_leakage_coverage_and_launch_clear");
}

static void test_regeneration_boundary_and_hit_reset(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[1].hp = SIM_MAX_HP - 2u;
    wait_ticks(&w, SIM_REGEN_TICKS - 1u);
    bool ok = true;
    EXPECT(w.wiz[1].hp == SIM_MAX_HP - 2u && w.wiz[1].regen_ticks == 1u);
    wait_ticks(&w, 1u);
    EXPECT(w.wiz[1].hp == SIM_MAX_HP - 1u && w.wiz[1].regen_ticks == SIM_REGEN_TICKS);

    wait_ticks(&w, SIM_REGEN_TICKS - 2u);
    uint32_t chip = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE,
                                    TRAJ_MID, 1, STATUS_NONE, INTERACT_PHASE,
                                    TEMPO_FLOWING, TREND_STEADY, 0);
    land_spell(&w, 0, chip);
    EXPECT(w.wiz[1].hp == SIM_MAX_HP - 2u && w.wiz[1].regen_ticks == SIM_REGEN_TICKS);
    wait_ticks(&w, SIM_REGEN_TICKS - 1u);
    EXPECT(w.wiz[1].hp == SIM_MAX_HP - 2u);
    wait_ticks(&w, 1u);
    EXPECT(w.wiz[1].hp == SIM_MAX_HP - 1u);
    CHECK(ok, "incantation_regeneration_exact_20_seconds_and_damage_reset");
}

/* M15 Track B: stance entry rules, exact timing, the STUDY buff's two arms,
 * MEDITATE's regen/ward gates, FORTIFY's held grant and windup trigger, and
 * the stance wire path through the view's fx_stance nibble. */
static void test_stance_entry_mechanics_and_exit(void) {
    sim_world_t w;
    duel_view_t v;
    bool ok = true;

    /* STUDY: unhurt + neutral temper. Entry lands exactly at
     * SIM_STANCE_ENTRY_TICKS of INC_IDLE and rides the wire. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    wait_ticks(&w, SIM_STANCE_ENTRY_TICKS - 1u);
    EXPECT(w.wiz[0].stance == DUEL_STANCE_NONE && w.wiz[1].stance == DUEL_STANCE_NONE);
    wait_ticks(&w, 1u);
    EXPECT(w.wiz[0].stance == DUEL_STANCE_STUDY && w.wiz[0].studied == 1u &&
           w.wiz[1].stance == DUEL_STANCE_STUDY);
    duel_view_from_world(&w, &v);
    EXPECT(VIEW_FX_STANCE(v.fx_stance, SIM_SIDE_L) == DUEL_STANCE_STUDY &&
           duel_view_wizard(&v, SIM_SIDE_R).stance == DUEL_STANCE_STUDY &&
           duel_view_valid(&v));

    /* Any own keydown exits instantly; the pending buff survives into the
     * commit and shifts a frost recipe to the variant-0 force affinity. */
    release_recipe(&w, 0, 0);
    EXPECT(w.wiz[0].stance == DUEL_STANCE_NONE && w.spell[0].active &&
           SPELL_DESC_ELEMENT(w.spell[0].descriptor) == ELEM_FORCE &&
           w.wiz[0].studied == 0u);

    /* Already-aligned STUDY deepens instead: a force recipe gains +1
     * magnitude over the single-key baseline of 1. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    wait_ticks(&w, SIM_STANCE_ENTRY_TICKS);
    EXPECT(w.wiz[1].stance == DUEL_STANCE_STUDY);
    release_recipe(&w, 1, 1);
    EXPECT(w.spell[1].active &&
           SPELL_DESC_ELEMENT(w.spell[1].descriptor) == ELEM_FORCE &&
           SPELL_DESC_MAGNITUDE(w.spell[1].descriptor) == 2u);

    /* MEDITATE: hurt + cool. Regen burns double while held; the ward is
     * suppressed on the wire but the stored strength survives. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[0].hp = 3u; w.wiz[0].temper = 1u;
    w.wiz[0].ward_strength = 2u; w.wiz[0].ward_capacity = 2u; w.wiz[0].ward_focus = 2u;
    wait_ticks(&w, SIM_STANCE_ENTRY_TICKS);
    EXPECT(w.wiz[0].stance == DUEL_STANCE_MEDITATE);
    uint16_t regen = w.wiz[0].regen_ticks;
    wait_ticks(&w, 10u);
    EXPECT(w.wiz[0].regen_ticks == (uint16_t)(regen - 20u));
    duel_view_from_world(&w, &v);
    EXPECT(duel_view_wizard(&v, SIM_SIDE_L).ward_strength == 0u &&
           w.wiz[0].ward_strength == 2u && duel_view_valid(&v));
    /* A keydown restores the presented ward instantly. */
    tap(&w, 0, 1, 2, 0);
    duel_view_from_world(&w, &v);
    EXPECT(w.wiz[0].stance == DUEL_STANCE_NONE &&
           duel_view_wizard(&v, SIM_SIDE_L).ward_strength == 2u);

    /* While meditating, ward_covers is gated: a coverable chip punches
     * through, and the interruption ends the stance. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[0].hp = 3u; w.wiz[0].temper = 1u;
    w.wiz[0].ward_strength = 2u; w.wiz[0].ward_capacity = 2u; w.wiz[0].ward_focus = 2u;
    wait_ticks(&w, SIM_STANCE_ENTRY_TICKS);
    uint32_t chip = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE,
                                    TRAJ_MID, 1, STATUS_NONE, INTERACT_SOLID,
                                    TEMPO_FLOWING, TREND_STEADY, 0);
    land_spell(&w, 1, chip);
    EXPECT(w.wiz[0].hp == 2u && w.wiz[0].stance == DUEL_STANCE_NONE &&
           w.wiz[0].temper == 2u);

    /* FORTIFY by hot temper: one ward pip exactly at the 50-tick hold. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[0].temper = 7u;
    wait_ticks(&w, SIM_STANCE_ENTRY_TICKS);
    EXPECT(w.wiz[0].stance == DUEL_STANCE_FORTIFY && w.wiz[0].ward_strength == 0u);
    wait_ticks(&w, SIM_STANCE_FORTIFY_HOLD_TICKS - 1u);
    EXPECT(w.wiz[0].ward_strength == 0u);
    wait_ticks(&w, 1u);
    EXPECT(w.wiz[0].ward_strength == 1u);
    wait_ticks(&w, 100u);
    EXPECT(w.wiz[0].ward_strength == 1u); /* granted exactly once */

    /* FORTIFY by visible opponent windup: a hurt neutral wizard paces until
     * the other side starts winding up. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[0].hp = 4u;
    wait_ticks(&w, SIM_STANCE_ENTRY_TICKS + 10u);
    EXPECT(w.wiz[0].stance == DUEL_STANCE_NONE);
    tap(&w, 1, 1, 1, 0);
    wait_ticks(&w, INCANTATION_IDLE_COMMIT_TICKS);
    EXPECT(w.wiz[1].inc_state == INC_WINDUP &&
           w.wiz[0].stance == DUEL_STANCE_FORTIFY);
    CHECK(ok, "incantation_stance_entry_rules_buffs_gates_and_wire_nibble");
}

/* M15 Track B: temperament drift at resolve time, its windup and KO
 * consequences, all clamped and deterministic. */
static void test_temper_drift_windup_and_ko_step(void) {
    sim_world_t w;
    bool ok = true;
    uint32_t chip = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE,
                                    TRAJ_MID, 1, STATUS_NONE, INTERACT_SOLID,
                                    TEMPO_FLOWING, TREND_STEADY, 0);

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    land_spell(&w, 0, chip);
    EXPECT(w.wiz[1].temper == 5u && w.wiz[0].temper == SIM_TEMPER_NEUTRAL);
    w.wiz[1].ward_strength = 4u; w.wiz[1].ward_focus = 2u;
    land_spell(&w, 0, chip);
    EXPECT(w.wiz[0].temper == 3u && w.wiz[1].temper == 5u); /* full stop cools */

    /* Windup: hot -2 / cool +2 around the neutral value, same recipe. */
    uint8_t wind[3];
    static const uint8_t tempers[3] = { SIM_TEMPER_NEUTRAL, 7u, 1u };
    for (uint8_t i = 0; i < 3u; i++) {
        sim_init(&w, SIMF_AUTHORITATIVE, 0);
        w.wiz[0].temper = tempers[i];
        tap(&w, 0, 0, 1, 0);
        tap(&w, 0, 1, 3, 0);
        tap(&w, 0, 2, 2, 0);
        wait_ticks(&w, INCANTATION_IDLE_COMMIT_TICKS - 1u);
        EXPECT(w.wiz[0].inc_state == INC_WINDUP);
        wind[i] = w.wiz[0].windup_total;
    }
    EXPECT(wind[0] > INCANTATION_WINDUP_MIN_TICKS + 2u &&
           wind[1] == (uint8_t)(wind[0] - 2u) &&
           wind[2] == (uint8_t)(wind[0] + 2u));

    /* KO steps temper one back toward neutral (after the final hit's +1). */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[1].temper = 7u; w.wiz[1].hp = 1u;
    land_spell(&w, 0, chip);
    EXPECT(w.wiz[1].life == LIFE_COLLAPSE && w.wiz[1].temper == 6u);
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[1].temper = 0u; w.wiz[1].hp = 1u;
    land_spell(&w, 0, chip);
    EXPECT(w.wiz[1].life == LIFE_COLLAPSE && w.wiz[1].temper == 2u);
    CHECK(ok, "incantation_temper_drift_windup_shift_and_ko_recentering");
}

static void test_damage_heal_ward_and_status(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t damage = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE,
                                      TRAJ_MID, 4, STATUS_NONE, INTERACT_SOLID,
                                      TEMPO_FLOWING, TREND_STEADY, 0);
    w.wiz[1].ward_strength = 2; w.wiz[1].ward_focus = 2;
    land_spell(&w, 0, damage);
    bool ok = true;
    EXPECT(w.wiz[1].hp == SIM_MAX_HP - 2u && w.wiz[1].ward_strength == 0);

    uint32_t heal = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_HEAL,
                                    TRAJ_RETURNING, 4, STATUS_NONE, INTERACT_SOLID,
                                    TEMPO_FLOWING, TREND_STEADY, 0);
    w.wiz[0].hp = 5;
    land_spell(&w, 0, heal);
    EXPECT(w.wiz[0].hp == SIM_MAX_HP); /* 5 + 4 clamps at the retuned max */

    uint32_t burn = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_EMBER, PAY_STATUS,
                                    TRAJ_MID, 3, STATUS_BURNING, INTERACT_SOLID,
                                    TEMPO_RAPID, TREND_STEADY, 0);
    land_spell(&w, 0, burn);
    uint8_t hp = w.wiz[1].hp;
    EXPECT(w.wiz[1].status == STATUS_BURNING && hp == SIM_MAX_HP - 2u);
    while (!w.wiz[1].status_burned) step(&w, 0, 0, 0, 0, NULL, 0);
    EXPECT(w.wiz[1].hp == (uint8_t)(hp - 1u) &&
          w.wiz[1].regen_ticks == SIM_REGEN_TICKS);
    CHECK(ok, "incantation_damage_residual_heal_clamp_and_delayed_burn");
}

static void test_status_dominance_and_effects(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t frozen = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FROST, PAY_STATUS,
                                      TRAJ_LOW, 3, STATUS_FROZEN, INTERACT_PHASE,
                                      TEMPO_FLOWING, TREND_STEADY, 0);
    land_spell(&w, 0, frozen);
    bool ok = true;
    EXPECT(w.wiz[1].status == STATUS_FROZEN && w.wiz[1].status_intensity == 3);
    uint8_t duration = w.wiz[1].status_ticks;
    uint32_t weak_burn = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_EMBER, PAY_STATUS,
                                         TRAJ_LOW, 1, STATUS_BURNING, INTERACT_PHASE,
                                         TEMPO_FLOWING, TREND_STEADY, 0);
    land_spell(&w, 0, weak_burn);
    EXPECT(w.wiz[1].status == STATUS_FROZEN && w.wiz[1].status_intensity == 3 &&
          w.wiz[1].status_ticks < duration);

    sim_world_t normal, slowed;
    sim_init(&normal, SIMF_AUTHORITATIVE, 0); sim_init(&slowed, SIMF_AUTHORITATIVE, 0);
    uint32_t bolt = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE,
                                    TRAJ_MID, 1, STATUS_NONE, INTERACT_SOLID,
                                    TEMPO_RAPID, TREND_STEADY, 0);
    install_spell(&normal, 0, bolt, 0); install_spell(&slowed, 0, bolt, 0);
    slowed.wiz[0].status = STATUS_FROZEN; slowed.wiz[0].status_intensity = 2; slowed.wiz[0].status_ticks = 100;
    step(&normal, 0, 0, 0, 0, NULL, 0); step(&normal, 0, 0, 0, 0, NULL, 0);
    step(&slowed, 0, 0, 0, 0, NULL, 0); step(&slowed, 0, 0, 0, 0, NULL, 0);
    EXPECT(normal.spell[0].progress == 22 && slowed.spell[0].progress == 11);

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[0].inc_state = INC_PREPARED; w.wiz[0].prepared = 1;
    w.wiz[0].prepared_desc = desc_set_magnitude_for_test(bolt, 3);
    w.wiz[0].status = STATUS_DISRUPTED; w.wiz[0].status_intensity = 2; w.wiz[0].status_ticks = 100;
    step(&w, 0, 0, 0, 0, NULL, 0);
    EXPECT(w.spell[0].active && SPELL_DESC_MAGNITUDE(w.spell[0].descriptor) == 2 &&
          w.wiz[0].status == STATUS_NONE);

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[1].ward_strength = 4; w.wiz[1].ward_focus = 2;
    w.wiz[1].status = STATUS_MARKED; w.wiz[1].status_intensity = 2; w.wiz[1].status_ticks = 100;
    uint32_t area = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE,
                                    TRAJ_AREA, 2, STATUS_NONE, INTERACT_SOLID,
                                    TEMPO_RAPID, TREND_STEADY, 0);
    land_spell(&w, 0, area);
    EXPECT(w.wiz[1].hp == SIM_MAX_HP - 2u && w.wiz[1].ward_strength == 0);
    CHECK(ok, "incantation_status_strength_frozen_disrupted_and_marked_effects");
}

static void test_form_lifecycles(void) {
    sim_world_t w;
    bool ok = true;
    uint32_t beam = SPELL_DESC_PACK(SPELL_BEAM, ELEM_FORCE, PAY_DAMAGE,
                                    TRAJ_MID, 2, STATUS_NONE, INTERACT_SOLID,
                                    TEMPO_RAPID, TREND_STEADY, 0);
    sim_init(&w, SIMF_AUTHORITATIVE, 0); install_spell(&w, 0, beam, 0);
    wait_ticks(&w, 5); uint8_t hp = w.wiz[1].hp;
    EXPECT(hp == SIM_MAX_HP - 2u && w.spell[0].progress >= 64);
    wait_ticks(&w, 32);
    EXPECT(w.wiz[1].hp == hp && !w.spell[0].active);

    uint32_t singularity = SPELL_DESC_PACK(SPELL_SINGULARITY, ELEM_VOID, PAY_DAMAGE,
                                           TRAJ_AREA, 2, STATUS_NONE, INTERACT_ABSORB,
                                           TEMPO_DELIBERATE, TREND_STEADY, 0);
    sim_init(&w, SIMF_AUTHORITATIVE, 0); install_spell(&w, 0, singularity, 0);
    wait_ticks(&w, 28);
    EXPECT(!w.spell[0].active && w.wiz[1].hp == SIM_MAX_HP);

    uint32_t swarm = SPELL_DESC_PACK(SPELL_SWARM, ELEM_FORCE, PAY_DAMAGE,
                                     TRAJ_MID, 4, STATUS_NONE, INTERACT_SOLID,
                                     TEMPO_FRANTIC, TREND_STEADY, 0);
    sim_init(&w, SIMF_AUTHORITATIVE, 0); install_spell(&w, 0, swarm, 0); w.spell[0].aux = 6;
    wait_ticks(&w, 36);
    EXPECT(!w.spell[0].active && w.wiz[1].hp == SIM_MAX_HP - 6u);
    CHECK(ok, "incantation_beam_once_singularity_empty_and_six_orb_lifecycles");
}

static void test_collision_precedence(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t phase = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_VOID, PAY_DAMAGE,
                                     TRAJ_MID, 2, STATUS_NONE, INTERACT_PHASE,
                                     TEMPO_RAPID, TREND_STEADY, 0);
    uint32_t solid = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE,
                                     TRAJ_MID, 2, STATUS_NONE, INTERACT_SOLID,
                                     TEMPO_RAPID, TREND_STEADY, 0);
    install_spell(&w, 0, phase, 120); install_spell(&w, 1, solid, 120);
    step(&w, 0, 0, 0, 0, NULL, 0);
    bool ok = true;
    EXPECT(w.spell[0].active && w.spell[1].active);

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t sing = SPELL_DESC_PACK(SPELL_SINGULARITY, ELEM_FORCE, PAY_DAMAGE,
                                    TRAJ_AREA, 2, STATUS_NONE, INTERACT_ABSORB,
                                    TEMPO_DELIBERATE, TREND_STEADY, 0);
    install_spell(&w, 0, sing, 48); w.spell[0].age = 10;
    install_spell(&w, 1, solid, 207);
    step(&w, 0, 0, 0, 0, NULL, 0);
    EXPECT(w.spell[0].active && w.spell[0].aux == 4 && !w.spell[1].active);

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t ember = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_EMBER, PAY_DAMAGE,
                                     TRAJ_MID, 2, STATUS_NONE, INTERACT_SOLID,
                                     TEMPO_RAPID, TREND_STEADY, 0);
    uint32_t frost = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FROST, PAY_DAMAGE,
                                     TRAJ_MID, 2, STATUS_NONE, INTERACT_SOLID,
                                     TEMPO_RAPID, TREND_STEADY, 0);
    install_spell(&w, 0, ember, 120); install_spell(&w, 1, frost, 120);
    step(&w, 0, 0, 0, 0, NULL, 0);
    EXPECT(!w.spell[0].active && !w.spell[1].active);
    CHECK(ok, "incantation_collision_phase_singularity_and_ember_frost_precedence");
}

static uint32_t clash_desc(uint8_t element, uint8_t magnitude, uint8_t tempo,
                           uint8_t trend) {
    return SPELL_DESC_PACK(SPELL_PROJECTILE, element, PAY_DAMAGE, TRAJ_MID,
                           magnitude, STATUS_NONE, INTERACT_SOLID, tempo, trend, 0);
}

static uint32_t form_desc(uint8_t form, uint8_t trajectory, uint8_t magnitude,
                          uint8_t tempo, uint8_t trend) {
    return SPELL_DESC_PACK(form, ELEM_FORCE, PAY_DAMAGE, trajectory, magnitude,
                           STATUS_NONE, INTERACT_SOLID, tempo, trend, 0);
}

// Mirror-form duels resolve symmetrically (magnitude, then the tempo/trend
// tiebreak, dead tie annihilating both) instead of silently favouring the
// left slot.
static void test_mirror_form_collisions(void) {
    sim_world_t w;
    bool ok = true;

    /* Beams: dead tie burns both out. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t beam = form_desc(SPELL_BEAM, TRAJ_MID, 2, TEMPO_FLOWING, TREND_STEADY);
    install_spell(&w, 0, beam, 120); install_spell(&w, 1, beam, 120);
    idle_step(&w);
    EXPECT(!w.spell[0].active && !w.spell[1].active);

    /* Beams: the better-paced RIGHT beam survives (the old code would have
     * kept the left one regardless). */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    install_spell(&w, 0, beam, 120);
    install_spell(&w, 1, form_desc(SPELL_BEAM, TRAJ_MID, 2, TEMPO_FRANTIC,
                                   TREND_STEADY), 120);
    idle_step(&w);
    EXPECT(!w.spell[0].active && w.spell[1].active);

    /* Chains: the stronger side survives, and equal-magnitude survivors pay
     * the ordinary one-step chain toll; a dead tie consumes both. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t chain3 = form_desc(SPELL_CHAIN, TRAJ_HOMING, 3, TEMPO_RAPID, TREND_STEADY);
    install_spell(&w, 0, form_desc(SPELL_CHAIN, TRAJ_HOMING, 2, TEMPO_RAPID,
                                   TREND_STEADY), 120);
    install_spell(&w, 1, chain3, 120);
    idle_step(&w);
    EXPECT(!w.spell[0].active && w.spell[1].active &&
           SPELL_DESC_MAGNITUDE(w.spell[1].descriptor) == 3u &&
           w.fx_kind == FX_RESIDUE);
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    install_spell(&w, 0, chain3, 120); install_spell(&w, 1, chain3, 120);
    idle_step(&w);
    EXPECT(!w.spell[0].active && !w.spell[1].active && w.fx_kind == FX_RESIDUE);
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    install_spell(&w, 0, chain3, 120);
    install_spell(&w, 1, form_desc(SPELL_CHAIN, TRAJ_HOMING, 3, TEMPO_FRANTIC,
                                   TREND_STEADY), 120);
    idle_step(&w);
    EXPECT(!w.spell[0].active && w.spell[1].active &&
           SPELL_DESC_MAGNITUDE(w.spell[1].descriptor) == 2u);

    /* Swarms trade one mote each per contact tick (the old code bled only
     * the left swarm). progress 49 puts both swarms mid-gap in contact. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t swarm = form_desc(SPELL_SWARM, TRAJ_MID, 2, TEMPO_DELIBERATE,
                               TREND_STEADY);
    install_spell(&w, 0, swarm, 49); w.spell[0].aux = 2;
    install_spell(&w, 1, swarm, 49); w.spell[1].aux = 1;
    idle_step(&w);
    EXPECT(w.spell[0].active && w.spell[0].aux == 1u && !w.spell[1].active);
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    install_spell(&w, 0, swarm, 49); w.spell[0].aux = 1;
    install_spell(&w, 1, swarm, 49); w.spell[1].aux = 1;
    idle_step(&w);
    EXPECT(!w.spell[0].active && !w.spell[1].active);
    CHECK(ok, "incantation_mirror_beam_chain_swarm_symmetric_resolution");
}

static void collide(sim_world_t *w, uint32_t left, uint32_t right) {
    install_spell(w, 0, left, 120u);
    install_spell(w, 1, right, 120u);
    step(w, 0, 0, 0, 0, NULL, 0);
}

static void test_productive_clashes(void) {
    sim_world_t w;
    bool ok = true;

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    collide(&w, clash_desc(ELEM_FORCE, 3, TEMPO_FLOWING, TREND_STEADY),
            clash_desc(ELEM_FORCE, 2, TEMPO_FRANTIC, TREND_IRREGULAR));
    EXPECT(w.spell[0].active && !w.spell[1].active &&
          SPELL_DESC_MAGNITUDE(w.spell[0].descriptor) == 4u);

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    collide(&w, clash_desc(ELEM_FROST, 2, TEMPO_RAPID, TREND_STEADY),
            clash_desc(ELEM_FROST, 2, TEMPO_FLOWING, TREND_IRREGULAR));
    EXPECT(w.spell[0].active && !w.spell[1].active &&
          SPELL_DESC_MAGNITUDE(w.spell[0].descriptor) == 4u);

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    collide(&w, clash_desc(ELEM_FORCE, 2, TEMPO_RAPID, TREND_ACCELERATING),
            clash_desc(ELEM_FORCE, 2, TEMPO_RAPID, TREND_STEADY));
    EXPECT(w.spell[0].active && !w.spell[1].active);

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t exact = clash_desc(ELEM_FORCE, 2, TEMPO_FLOWING, TREND_STEADY);
    collide(&w, exact, exact);
    EXPECT(!w.spell[0].active && !w.spell[1].active &&
          w.wiz[0].hp == SIM_MAX_HP - 1u && w.wiz[1].hp == SIM_MAX_HP - 1u);

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[0].ward_capacity = 4u; w.wiz[0].ward_strength = 4u;
    w.wiz[1].ward_capacity = 4u; w.wiz[1].ward_strength = 4u;
    collide(&w, exact, exact);
    EXPECT(w.wiz[0].hp == SIM_MAX_HP && w.wiz[1].hp == SIM_MAX_HP &&
          w.wiz[0].ward_strength == 3u && w.wiz[1].ward_strength == 3u);

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[0].ward_capacity = 4u; w.wiz[0].ward_strength = 4u;
    collide(&w, clash_desc(ELEM_EMBER, 4, TEMPO_FRANTIC, TREND_IRREGULAR),
            clash_desc(ELEM_FROST, 4, TEMPO_FRANTIC, TREND_IRREGULAR));
    EXPECT(w.wiz[0].hp == SIM_MAX_HP && w.wiz[0].ward_strength == 3u &&
          w.wiz[1].hp == SIM_MAX_HP - 1u);

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    collide(&w, clash_desc(ELEM_FORCE, 2, TEMPO_FLOWING, TREND_STEADY),
            clash_desc(ELEM_VOID, 2, TEMPO_FLOWING, TREND_STEADY));
    EXPECT(!w.spell[0].active && !w.spell[1].active &&
          w.wiz[0].hp == SIM_MAX_HP && w.wiz[1].hp == SIM_MAX_HP);

    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t maximum = clash_desc(ELEM_FORCE, 4, TEMPO_FLOWING, TREND_STEADY);
    land_spell(&w, 0, maximum);
    EXPECT(w.wiz[1].hp == SIM_MAX_HP - 4u);
    CHECK(ok, "incantation_productive_clash_cap_tiebreak_pulses_wards_and_damage_cap");
}

static void test_incantation_link_ordering(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    duel_snapshot_t a, b, old_session;
    test_encode_snapshot(&w, 7, 0xffffu, &a);
    test_encode_snapshot(&w, 7, 0u, &b);
    test_encode_snapshot(&w, 6, 100u, &old_session);
    duel_rx_state_t rx = {0};
    bool ok = true;
    EXPECT(duel_rx_accept(&rx, &a, false) && duel_rx_accept(&rx, &b, false) &&
              !duel_rx_accept(&rx, &a, false) && duel_rx_accept(&rx, &old_session, false));
    duel_snapshot_t corrupt = b; corrupt.view.phase[0] ^= 0x40u;
    EXPECT(!duel_decode_valid(&corrupt));
    CHECK(ok, "incantation_sequence_wrap_session_restart_and_corruption");
}

static void test_render_purity(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    release_recipe(&w, 0, 0);
    sim_world_t before = w;
    duel_render_t render = {0};
    duel_render_from_world(&render, &w);
    duel_fb_t fb;
    duel_fb_clear(&fb);
    wiz_draw_scene(&fb, &render, true, 7, false);
    bool nonempty = false;
    for (size_t i = 0; i < sizeof fb.bits; i++) nonempty |= fb.bits[i] != 0;
    CHECK(nonempty && memcmp(&w, &before, sizeof w) == 0,
          "incantation_render_nonempty_and_authoritative_pure");
}

static uint32_t compile_actual_pattern(uint32_t seed, uint8_t extra_gap) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    static const uint8_t pos[4] = {0, 7, 14, 21};
    for (uint8_t i = 0; i < 4u; i++) {
        tap(&w, 0, pos[i] / 6u, pos[i] % 6u, (uint8_t)((seed + i) & 3u));
        if (i != 3u) wait_ticks(&w, extra_gap);
    }
    wait_ticks(&w, INCANTATION_IDLE_COMMIT_TICKS + 1u);
    return w.wiz[0].pending_desc;
}

static void test_real_input_reachability_and_timing_buckets(void) {
    uint32_t bucket_a = compile_actual_pattern(3u, 1u);
    uint32_t bucket_b = compile_actual_pattern(3u, 2u);
    bool ok = true;
    EXPECT(bucket_a == bucket_b);
    uint32_t forms = 0;
    uint8_t max_complexity = 0;
    for (uint32_t seed = 0; seed < 2048u && forms != 0xffu; seed++) {
        sim_world_t w;
        sim_init(&w, SIMF_AUTHORITATIVE, 0);
        sim_event_t chord[5];
        uint32_t chord_mask = 0;
        for (uint8_t j = 0; j < 5u; j++) {
            uint8_t p = (uint8_t)((seed + j * 5u) % 24u);
            chord[j] = SIM_EV_PACK(SIM_EV_KEYDOWN, 0, p / 6u, p % 6u);
            chord_mask |= 1u << p;
        }
        step(&w, chord_mask, 0, (uint8_t)(seed & 3u), 0, chord, 5);
        step(&w, 0, 0, 0, 0, NULL, 0);
        for (uint8_t i = 0; i < 70u; i++) {
            uint8_t pos = (uint8_t)((seed * 7u + i * 5u + (uint16_t)i * i) % 24u);
            uint8_t layer = (uint8_t)((seed + i * 3u + (i >> 2)) & 3u);
            tap(&w, 0, pos / 6u, pos % 6u, layer);
        }
        uint8_t complexity = incantation_complexity(&w.wiz[0].inc);
        if (complexity > max_complexity) max_complexity = complexity;
        wait_ticks(&w, INCANTATION_IDLE_COMMIT_TICKS + 1u);
        if (SPELL_DESC_VALID(w.wiz[0].pending_desc))
            forms |= 1u << SPELL_DESC_FORM(w.wiz[0].pending_desc);
    }
    if (!ok || forms != 0xffu)
        printf("DIAG actual forms=%02x complexity=%u bucket_a=%06x bucket_b=%06x\n",
               (unsigned)forms, max_complexity, (unsigned)bucket_a, (unsigned)bucket_b);
    EXPECT(forms == 0xffu);
    CHECK(ok, "incantation_real_input_all_forms_and_bucket_repeatability");
}

static uint32_t prose_workload_first_ko(uint8_t profile) {
    static const uint16_t period[3][2] = {
        {165u, 183u}, /* steady phrases */
        {175u, 191u}, /* dense bursts with longer thought pauses */
        {180u, 200u}, /* mixed-layer editing prose */
    };
    static const uint8_t keys[3][2] = {{8u, 7u}, {14u, 12u}, {10u, 9u}};
    static const uint8_t offset[2] = {0u, 37u};
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    for (uint32_t tick = 0; tick < 4500u; tick++) {
        sim_inputs_t in = {0};
        sim_event_t event[2];
        uint8_t n = 0;
        for (uint8_t side = 0; side < 2u; side++) {
            uint16_t phase = (uint16_t)((tick + offset[side]) % period[profile][side]);
            if (phase < (uint16_t)keys[profile][side] * 2u && !(phase & 1u)) {
                uint8_t rank = (uint8_t)(phase / 2u);
                uint8_t pos = (uint8_t)((rank * (side ? 7u : 5u) +
                                         profile * 3u + side * 11u +
                                         tick / period[profile][side]) % 24u);
                uint8_t layer = profile == 2u ? (uint8_t)((rank + side) & 3u) :
                                profile == 1u && rank >= 8u ? (uint8_t)(1u + side) : 0u;
                in.held_pos[side] = 1u << pos;
                in.down_mask |= (uint8_t)(1u << side);
                in.layer[side] = layer;
                event[n++] = SIM_EV_PACK(SIM_EV_KEYDOWN, side, pos / 6u, pos % 6u);
            }
        }
        sim_tick(&w, in, event, n, 0);
        if (w.wiz[0].life != LIFE_ACTIVE || w.wiz[1].life != LIFE_ACTIVE)
            return tick + 1u;
    }
    return 0u;
}

static void test_prose_typing_ko_window(void) {
    /* Re-measured after Track T (HP 12->8, regen 20 s) AND Track B: first
     * KOs land at 398/1367/1044 ticks (~16/55/42 s). Profiles 1-2 sit at
     * pre-B pacing (FORTIFY wards absorb what STUDY adds), but profile 0's
     * steady phrases open with a STUDY-buffed magnitude-3 swarm — five
     * 1-hp pulses — whose per-pulse temper drift then doubles the fireball
     * weight: a deliberate escalation spiral, measured here so a future
     * change that tightens it further trips the bound. Whether ~16 s to
     * first blood feels restless on the desk is backlog Q4 (hardware). */
    bool ok = true;
    for (uint8_t profile = 0; profile < 3u; profile++) {
        uint32_t ko = prose_workload_first_ko(profile);
        if (ko < 350u || ko > 3750u)
            printf("DIAG prose profile=%u first_ko_ticks=%lu\n", profile,
                   (unsigned long)ko);
        EXPECT(ko >= 350u && ko <= 3750u);
    }
    CHECK(ok, "incantation_steady_burst_mixed_prose_first_ko_14_to_150_seconds");
}

static void test_max_cast_aftermath_and_wire(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    sim_event_t event = SIM_EV_PACK(SIM_EV_KEYDOWN, 0, 1, 2);
    uint32_t held = 1u << 8;
    step(&w, held, 0, 0, 0, &event, 1);
    for (unsigned i = 1; i < INCANTATION_FORCE_COMMIT_TICKS; i++)
        step(&w, held, 0, 0, 0, NULL, 0);
    uint8_t shared = incantation_aftermath_shared(&w);
    uint8_t revision = incantation_aftermath_revision(&w);
    bool ok = true;
    EXPECT(w.aftermath[0].kind == AFTER_MAX_CAST &&
              w.aftermath[1].kind == AFTER_MAX_CAST &&
              w.aftermath[0].resident_state == RESIDENT_WATCH_CAST &&
              w.world_state == WORLD_WONDER &&
              INCANTATION_AFTER_KIND(shared, 0) == AFTER_MAX_CAST &&
              INCANTATION_AFTER_KIND(shared, 1) == AFTER_MAX_CAST &&
              (revision & INCANTATION_AFTERMATH_WIRE));
    duel_snapshot_t packet;
    test_encode_snapshot(&w, 4, 9, &packet);
    EXPECT(packet.shared_pres == shared && packet.revision == revision && duel_decode_valid(&packet));
    duel_render_t render = {0};
    duel_render_from_world(&render, &w);
    EXPECT(render.shared_pres == shared && render.revision == revision);
    step(&w, 0, 0, 0, 0, NULL, 0);
    /* Wait to one tick shy of the halfway boundary (phase 2 = cheer), then
     * cross it. The arc started during the casts above, so derive the elapsed
     * ticks from the countdown rather than assuming a fresh start. */
    unsigned elapsed = SIM_AFTER_MAX_CAST_TICKS - w.aftermath[0].ticks;
    wait_ticks(&w, SIM_AFTER_MAX_CAST_TICKS / 2u - 1u - elapsed);
    EXPECT(w.aftermath[0].kind == AFTER_MAX_CAST &&
          w.aftermath[0].resident_state == RESIDENT_WATCH_CAST);
    wait_ticks(&w, 2u);
    EXPECT(w.aftermath[0].resident_state == RESIDENT_CHEER);
    CHECK(ok, "incantation_max_cast_coordinated_authoritative_wire_aftermath");
}

static void test_fireball_room_resident_object_arc(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t fireball = SPELL_DESC_PACK(SPELL_FIREBALL, ELEM_EMBER, PAY_DAMAGE,
                                        TRAJ_ROOF, 3, STATUS_NONE, INTERACT_SOLID,
                                        TEMPO_FLOWING, TREND_STEADY, 0);
    land_spell(&w, 0, fireball);
    bool ok = true;
    EXPECT(!w.spell[0].active && w.wiz[1].hp == SIM_MAX_HP - 3u &&
              w.fx_kind == FX_DETONATE && w.aftermath[1].kind == AFTER_FIRE &&
              w.aftermath[1].resident_state == RESIDENT_PANIC &&
              w.aftermath[1].room_state == ROOM_DISRUPTED &&
              w.aftermath[1].object_state == OBJECT_FIRE &&
              w.world_state == WORLD_CRISIS);
    /* Quarter-phase boundaries of the fire arc: response, recovery, expiry. */
    wait_ticks(&w, SIM_AFTER_FIRE_TICKS / 4u + 1u);
    EXPECT(w.aftermath[1].resident_state == RESIDENT_FIGHT_FIRE &&
          w.aftermath[1].object_state == OBJECT_FIRE);
    wait_ticks(&w, SIM_AFTER_FIRE_TICKS / 2u + 1u);
    EXPECT(w.aftermath[1].resident_state == RESIDENT_REPAIR &&
          w.aftermath[1].room_state == ROOM_RECOVERY &&
          w.aftermath[1].object_state == OBJECT_DAMAGED);
    wait_ticks(&w, SIM_AFTER_FIRE_TICKS / 4u + 2u);
    EXPECT(w.aftermath[1].kind == AFTER_NONE && w.world_state == WORLD_CALM);
    CHECK(ok, "incantation_fireball_roof_resident_room_object_recovery_arc");
}

static void test_reachable_complaint_and_ward_shatter(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t chip = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE,
                                    TRAJ_MID, 1, STATUS_NONE, INTERACT_SOLID,
                                    TEMPO_FLOWING, TREND_STEADY, 0);
    w.wiz[1].ward_strength = 1; w.wiz[1].ward_focus = 2;
    land_spell(&w, 0, chip);
    bool ok = true;
    EXPECT(w.fx_kind == FX_WARD_SHATTER_R && w.wiz[1].hp == SIM_MAX_HP &&
              w.wiz[1].ward_strength == 0);
    land_spell(&w, 0, chip);
    EXPECT(w.fx_kind == FX_COMPLAINT && w.wiz[1].hp == SIM_MAX_HP - 1u &&
          w.aftermath[1].kind == AFTER_COMPLAINT &&
          w.aftermath[1].resident_state == RESIDENT_COMPLAIN);
    CHECK(ok, "incantation_ward_shatter_and_complaint_reachable");
}

static void test_ground_chain_summon_and_trap(void) {
    sim_world_t w;
    uint32_t ground = SPELL_DESC_PACK(SPELL_GROUND_WAVE, ELEM_FORCE, PAY_DAMAGE,
                                      TRAJ_GROUND, 2, STATUS_NONE, INTERACT_SOLID,
                                      TEMPO_FLOWING, TREND_STEADY, 0);
    uint32_t high = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FROST, PAY_DAMAGE,
                                    TRAJ_HIGH, 2, STATUS_NONE, INTERACT_SOLID,
                                    TEMPO_FLOWING, TREND_STEADY, 0);
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    install_spell(&w, 0, ground, 120); install_spell(&w, 1, high, 120);
    step(&w, 0, 0, 0, 0, NULL, 0);
    bool ok = true;
    EXPECT(w.spell[0].active && w.spell[1].active);

    uint32_t chain = SPELL_DESC_PACK(SPELL_CHAIN, ELEM_FORCE, PAY_DAMAGE,
                                     TRAJ_HOMING, 2, STATUS_NONE, INTERACT_SOLID,
                                     TEMPO_RAPID, TREND_ACCELERATING, 0);
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    install_spell(&w, 0, chain, 120); install_spell(&w, 1, high, 120);
    step(&w, 0, 0, 0, 0, NULL, 0);
    EXPECT(w.spell[0].active && !w.spell[1].active && w.fx_kind == FX_RESIDUE);

    uint32_t trap = SPELL_DESC_PACK(SPELL_CONJURE, ELEM_EMBER, PAY_DAMAGE,
                                    TRAJ_GROUND, 2, STATUS_NONE, INTERACT_SOLID,
                                    TEMPO_DELIBERATE, TREND_STEADY, 0);
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    install_spell(&w, 0, trap, 16); w.spell[0].aux = 3;
    install_spell(&w, 1, high, 175);
    step(&w, 0, 0, 0, 0, NULL, 0);
    EXPECT(!w.spell[0].active && !w.spell[1].active &&
          w.wiz[1].hp == SIM_MAX_HP - 2u && w.fx_kind == FX_DETONATE);

    uint32_t summon = SPELL_DESC_PACK(SPELL_CONJURE, ELEM_FORCE, PAY_DAMAGE,
                                      TRAJ_RETURNING, 2, STATUS_NONE, INTERACT_SOLID,
                                      TEMPO_FRANTIC, TREND_STEADY, 0);
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    install_spell(&w, 0, summon, 0); w.spell[0].aux = 2;
    wait_ticks(&w, 22);
    EXPECT(!w.spell[0].active && w.wiz[1].hp == SIM_MAX_HP - 2u);
    CHECK(ok, "incantation_ground_chain_summon_and_trap_lifecycles");
}

static void test_swarm_gather_launch_and_tempo_motion(void) {
    sim_world_t w;
    uint32_t swarm = SPELL_DESC_PACK(SPELL_SWARM, ELEM_FORCE, PAY_DAMAGE,
                                     TRAJ_MID, 4, STATUS_NONE, INTERACT_SOLID,
                                     TEMPO_FRANTIC, TREND_ACCELERATING, 0);
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    install_spell(&w, 0, swarm, 0); w.spell[0].aux = 6;
    wait_ticks(&w, 11);
    bool ok = true;
    EXPECT(w.wiz[1].hp == SIM_MAX_HP && (w.spell[0].progress >> 5) == 6u &&
              (w.spell[0].progress & 31u) < 12u);
    wait_ticks(&w, 4);
    EXPECT(w.wiz[1].hp == SIM_MAX_HP && (w.spell[0].progress & 31u) >= 12u);
    wait_ticks(&w, 1);
    EXPECT(w.wiz[1].hp == SIM_MAX_HP - 1u && (w.spell[0].progress >> 5) == 5u);
    wait_ticks(&w, 20);
    EXPECT(!w.spell[0].active && w.wiz[1].hp == SIM_MAX_HP - 6u);

    uint32_t slow = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE,
                                    TRAJ_LOW, 1, STATUS_NONE, INTERACT_SOLID,
                                    TEMPO_DELIBERATE, TREND_DECELERATING, 0);
    uint32_t fast = SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE,
                                    TRAJ_LOW, 1, STATUS_NONE, INTERACT_SOLID,
                                    TEMPO_FRANTIC, TREND_ACCELERATING, 0);
    sim_world_t a, b;
    sim_init(&a, SIMF_AUTHORITATIVE, 0); sim_init(&b, SIMF_AUTHORITATIVE, 0);
    install_spell(&a, 0, slow, 0); install_spell(&b, 0, fast, 0);
    wait_ticks(&a, 8); wait_ticks(&b, 8);
    EXPECT(b.spell[0].progress > a.spell[0].progress);
    CHECK(ok, "incantation_swarm_gather_serial_launch_and_tempo_trend_motion");
}

static void test_bilateral_beam_and_aftermath_split_render(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t beam = SPELL_DESC_PACK(SPELL_BEAM, ELEM_FORCE, PAY_DAMAGE,
                                    TRAJ_MID, 3, STATUS_NONE, INTERACT_SOLID,
                                    TEMPO_FLOWING, TREND_STEADY, 0);
    install_spell(&w, 1, beam, 128);
    w.aftermath[0] = (sim_aftermath_t){AFTER_INSPECT, 80, 2, RESIDENT_INSPECT,
                                       ROOM_ALERT, OBJECT_RESIDUE};
    w.world_state = WORLD_RECOVERY;
    duel_render_t master = {0};
    duel_render_from_world(&master, &w); master.seed = 9; master.civic_phase = 12;
    duel_fb_t ml, mr;
    duel_fb_clear(&ml); duel_fb_clear(&mr);
    wiz_draw_scene(&ml, &master, true, 0, false);
    wiz_draw_scene(&mr, &master, false, 0, false);
    int beam_y = 63 + DUEL_ROOF_DY;
    bool ok = true;
    EXPECT(duel_fb_get(&ml, 21, beam_y) && duel_fb_get(&ml, 31, beam_y) &&
              duel_fb_get(&mr, 0, beam_y) && duel_fb_get(&mr, 10, beam_y) &&
              !duel_fb_get(&ml, 0, beam_y) && !duel_fb_get(&mr, 31, beam_y));

    duel_snapshot_t packet;
    test_encode_snapshot(&w, 8, 20, &packet);
    duel_rx_state_t rx = {0};
    EXPECT(duel_rx_accept(&rx, &packet, false) && duel_decode_valid(&rx.last));
    duel_render_t slave = master;
    slave.view = rx.last.view; slave.shared_pres = rx.last.shared_pres;
    slave.revision = rx.last.revision;
    duel_fb_t sl, sr;
    duel_fb_clear(&sl); duel_fb_clear(&sr);
    wiz_draw_scene(&sl, &slave, true, 0, false);
    wiz_draw_scene(&sr, &slave, false, 0, false);
    EXPECT(memcmp(&ml, &sl, sizeof ml) == 0 && memcmp(&mr, &sr, sizeof mr) == 0);
    CHECK(ok, "incantation_bilateral_beam_and_aftermath_split_render_convergence");
}

static bool exact_mirror(const duel_fb_t *a, const duel_fb_t *b) {
    for (int y = 0; y < DUEL_CANVAS_H; y++)
        for (int x = 0; x < DUEL_CANVAS_W; x++) {
            if (duel_fb_get(a, x, y) !=
                duel_fb_get(b, DUEL_CANVAS_W - 1 - x, y)) return false;
        }
    return true;
}

static unsigned band_difference(const duel_fb_t *a, const duel_fb_t *b,
                                int y0, int y1) {
    unsigned n = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = 0; x < DUEL_CANVAS_W; x++)
            n += duel_fb_get(a, x, y) != duel_fb_get(b, x, y);
    return n;
}

static void render_floor_scene(uint8_t floor, bool is_left, uint8_t transition,
                               duel_fb_t *fb) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    duel_render_t r = {0};
    duel_render_from_world(&r, &w);
    r.civic = DUEL_CIVIC_PACK(floor, DUEL_CIVIC_MODE_NORMAL, 0);
    r.seed = 0x42u; r.civic_phase = 19u; r.floor_transition = transition;
    duel_fb_clear(fb);
    wiz_draw_scene(fb, &r, is_left, 7u, false);
}

static void test_floor_occupations_and_transitions(void) {
    duel_fb_t floor[2][3];
    bool ok = true;
    for (uint8_t city = 0; city < 2u; city++)
        for (uint8_t occupation = 0; occupation < 3u; occupation++)
            render_floor_scene(occupation, city == 0u, 0u,
                               &floor[city][occupation]);
    for (uint8_t city = 0; city < 2u; city++)
        for (uint8_t a = 0; a < 3u; a++)
            for (uint8_t b = (uint8_t)(a + 1u); b < 3u; b++) {
                unsigned diff = band_difference(&floor[city][a], &floor[city][b], DUEL_FLOOR_Y0, DUEL_FLOOR_Y1);
                if (diff < 40u) printf("DIAG floor city=%u pair=%u/%u diff=%u\n",
                                       city, a, b, diff);
                EXPECT(diff >= 40u);
            }

    for (uint8_t phase = 0; phase < 4u; phase++) {
        duel_fb_t transitioned;
        uint8_t byte = INCANTATION_FLOOR_TRANSITION_PACK(DUEL_CIVIC_FLOOR_COMMONS,
                                                  phase, true);
        render_floor_scene(DUEL_CIVIC_FLOOR_WORKSHOP, true, byte, &transitioned);
        const duel_fb_t *reference = phase < 2u ?
            &floor[0][DUEL_CIVIC_FLOOR_COMMONS] : &floor[0][DUEL_CIVIC_FLOOR_WORKSHOP];
        EXPECT(band_difference(&transitioned, reference, DUEL_FLOOR_Y0, DUEL_FLOOR_Y1) > 0u);
        /* Protection now includes the beam row itself (previously a one-row hole). */
        EXPECT(band_difference(&transitioned, reference, 0, DUEL_FLOOR_BEAM_Y) == 0u);
        EXPECT(band_difference(&transitioned, reference, DUEL_FLOOR_Y1 + 1, DUEL_CANVAS_H - 1) == 0u);
        EXPECT(INCANTATION_FLOOR_TRANSITION_SOURCE(byte) == DUEL_CIVIC_FLOOR_COMMONS &&
              INCANTATION_FLOOR_TRANSITION_PHASE(byte) == phase &&
              INCANTATION_FLOOR_TRANSITION_ACTIVE(byte) && !(byte & 0xe0u));
    }
    CHECK(ok, "incantation_six_occupation_scenes_40px_and_four_protected_transition_phases");
}

static unsigned framebuffer_pixels(const duel_fb_t *fb) {
    unsigned n = 0;
    for (int y = 0; y < DUEL_CANVAS_H; y++)
        for (int x = 0; x < DUEL_CANVAS_W; x++) n += duel_fb_get(fb, x, y);
    return n;
}

static void incantation_render(duel_fb_t *fb, const duel_render_t *r, bool is_left,
                       bool diagnostics);

static bool pixels_within(const duel_fb_t *fb, int y0, int y1) {
    for (int y = 0; y < DUEL_CANVAS_H; y++)
        for (int x = 0; x < DUEL_CANVAS_W; x++)
            if (duel_fb_get(fb, x, y) && (y < y0 || y > y1)) return false;
    return true;
}

static void test_civic_anchor_and_courier_matrix(void) {
    bool ok = true;
    for (uint8_t action = 0; action < DUEL_CIVIC_ACTION_COUNT; action++) {
        incantation_point_t fallback = incantation_occupation_anchor(INCANTATION_OCCUPATION_FLOORS, action);
        incantation_point_t commons = incantation_occupation_anchor(DUEL_CIVIC_FLOOR_COMMONS, action);
        EXPECT(fallback.x == commons.x && fallback.y == commons.y);
        for (uint8_t floor = 0; floor < INCANTATION_OCCUPATION_FLOORS; floor++) {
            incantation_point_t point = incantation_occupation_anchor(floor, action);
            EXPECT(point.x >= 0 && point.x < DUEL_CANVAS_W && point.y >= DUEL_FLOOR_BEAM_Y && point.y <= DUEL_FLOOR_Y1);
        }
    }

    sim_world_t world; sim_init(&world, SIMF_AUTHORITATIVE, 0);
    for (uint8_t floor = 0; floor < INCANTATION_OCCUPATION_FLOORS; floor++)
        for (uint8_t kind = DUEL_CIVIC_COURIER_MESSENGER;
             kind < DUEL_CIVIC_COURIER_COUNT; kind++)
            for (uint8_t life = DUEL_CIVIC_VISIT_ARRIVING;
                 life <= DUEL_CIVIC_VISIT_RESOLVING; life++)
                for (uint8_t density = DUEL_CIVIC_DENSITY_SINGLE;
                     density <= DUEL_CIVIC_DENSITY_MANY; density++)
                    for (uint8_t mode = DUEL_CIVIC_MODE_NORMAL;
                         mode <= DUEL_CIVIC_MODE_QUIET; mode++)
                        for (uint8_t city = 0; city < 2u; city++) {
                            duel_render_t r = {0}; duel_render_from_world(&r, &world);
                            r.civic = DUEL_CIVIC_PACK(floor, mode, 0);
                            r.shared_pres = (uint8_t)(DUEL_VISITOR_PACK(kind, city, life) |
                                DUEL_VISITOR_DENSITY_PACK(density));
                            duel_fb_t assigned, repeat, opposite;
                            duel_fb_clear(&assigned); duel_fb_clear(&repeat); duel_fb_clear(&opposite);
                            draw_courier(&assigned, &r, city == 0u);
                            draw_courier(&repeat, &r, city == 0u);
                            draw_courier(&opposite, &r, city != 0u);
                            EXPECT(memcmp(&assigned, &repeat, sizeof assigned) == 0 &&
                                  framebuffer_pixels(&opposite) == 0u &&
                                  pixels_within(&assigned, DUEL_FLOOR_BEAM_Y, DUEL_FLOOR_Y1));
                            if (floor == DUEL_CIVIC_FLOOR_SPECIAL)
                                EXPECT(framebuffer_pixels(&assigned) == 0u);
                            else
                                EXPECT(framebuffer_pixels(&assigned) >= 6u);
                        }

    /* City assignment is a pure mirror, and transition routing uses the visible
     * source room until the target reveal begins. */
    duel_render_t route = {0}; duel_render_from_world(&route, &world);
    route.civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_WORKSHOP, DUEL_CIVIC_MODE_NORMAL, 0);
    route.shared_pres = DUEL_VISITOR_PACK(DUEL_CIVIC_COURIER_PARCEL, 0,
                                           DUEL_CIVIC_VISIT_WAITING);
    route.floor_transition = INCANTATION_FLOOR_TRANSITION_PACK(DUEL_CIVIC_FLOOR_COMMONS, 1, true);
    duel_fb_t source, expected, mirror;
    duel_fb_clear(&source); duel_fb_clear(&expected); duel_fb_clear(&mirror);
    draw_courier(&source, &route, true);
    route.floor_transition = 0;
    route.civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_COMMONS, DUEL_CIVIC_MODE_NORMAL, 0);
    draw_courier(&expected, &route, true);
    route.shared_pres = DUEL_VISITOR_PACK(DUEL_CIVIC_COURIER_PARCEL, 1,
                                           DUEL_CIVIC_VISIT_WAITING);
    draw_courier(&mirror, &route, false);
    EXPECT(memcmp(&source, &expected, sizeof source) == 0 && exact_mirror(&expected, &mirror));
    for (uint8_t kind = DUEL_CIVIC_COURIER_MESSENGER;
         kind < DUEL_CIVIC_COURIER_COUNT; kind++) {
        duel_fb_t variant[INCANTATION_OCCUPATION_FLOORS];
        for (uint8_t floor = 0; floor < INCANTATION_OCCUPATION_FLOORS; floor++) {
            route.civic = DUEL_CIVIC_PACK(floor, DUEL_CIVIC_MODE_NORMAL, 0);
            route.shared_pres = DUEL_VISITOR_PACK(kind, 0, DUEL_CIVIC_VISIT_AGING);
            duel_fb_clear(&variant[floor]); draw_courier(&variant[floor], &route, true);
        }
        EXPECT(memcmp(&variant[0], &variant[1], sizeof variant[0]) != 0 &&
              memcmp(&variant[0], &variant[2], sizeof variant[0]) != 0 &&
              memcmp(&variant[1], &variant[2], sizeof variant[0]) != 0);
    }
    CHECK(ok, "incantation_canonical_anchors_and_courier_floor_lifecycle_density_mode_city_matrix");
}

static void test_rare_event_floor_phase_mode_target_matrix(void) {
    bool ok = true;
    for (uint8_t floor = 0; floor < INCANTATION_OCCUPATION_FLOORS; floor++)
        for (uint8_t id = DUEL_CIVIC_EVENT_RUNAWAY_SCROLL;
             id < DUEL_CIVIC_EVENT_COUNT; id++)
            for (uint8_t phase = DUEL_CIVIC_EVENT_PHASE_ARMED;
                 phase <= DUEL_CIVIC_EVENT_PHASE_COOLDOWN; phase++)
                for (uint8_t mode = DUEL_CIVIC_MODE_NORMAL;
                     mode <= DUEL_CIVIC_MODE_QUIET; mode++) {
                    bool shared = id >= DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER;
                    for (uint8_t target_case = 0; target_case < (shared ? 1u : 2u); target_case++) {
                        uint8_t target = shared ? DUEL_CIVIC_EVENT_TARGET_SHARED : target_case;
                        duel_render_t r = {0};
                        r.civic = DUEL_CIVIC_PACK(floor, mode, 0);
                        r.revision = DUEL_EVENT_PACK(id, phase, target);
                        duel_fb_t left, right, repeat;
                        duel_fb_clear(&left); duel_fb_clear(&right); duel_fb_clear(&repeat);
                        draw_rare_event(&left, &r, true);
                        draw_rare_event(&right, &r, false);
                        draw_rare_event(&repeat, &r, true);
                        EXPECT(memcmp(&left, &repeat, sizeof left) == 0);
                        if (floor == DUEL_CIVIC_FLOOR_SPECIAL) {
                            EXPECT(framebuffer_pixels(&left) == 0u &&
                                  framebuffer_pixels(&right) == 0u);
                        } else if (shared) {
                            EXPECT(framebuffer_pixels(&left) >= 4u && framebuffer_pixels(&right) >= 4u);
                        } else if (target == DUEL_CIVIC_EVENT_TARGET_LEFT) {
                            EXPECT(framebuffer_pixels(&left) >= 4u && framebuffer_pixels(&right) == 0u);
                        } else {
                            EXPECT(framebuffer_pixels(&right) >= 4u && framebuffer_pixels(&left) == 0u);
                        }
                        if (id == DUEL_CIVIC_EVENT_CIVIC_SKY)
                            EXPECT(pixels_within(&left, 16, 26) && pixels_within(&right, 16, 26));
                        else
                            EXPECT(pixels_within(&left, DUEL_FLOOR_BEAM_Y, DUEL_FLOOR_Y1) &&
                                  pixels_within(&right, DUEL_FLOOR_BEAM_Y, DUEL_FLOOR_Y1));
                    }
                }

    duel_render_t none = {0}; duel_fb_t empty;
    none.revision = DUEL_EVENT_PACK(DUEL_CIVIC_EVENT_NONE, 0, 0);
    duel_fb_clear(&empty); draw_rare_event(&empty, &none, true);
    EXPECT(framebuffer_pixels(&empty) == 0u);
    for (uint8_t id = DUEL_CIVIC_EVENT_RUNAWAY_SCROLL;
         id < DUEL_CIVIC_EVENT_COUNT; id++) {
        duel_fb_t variant[INCANTATION_OCCUPATION_FLOORS];
        for (uint8_t floor = 0; floor < INCANTATION_OCCUPATION_FLOORS; floor++) {
            none.civic = DUEL_CIVIC_PACK(floor, DUEL_CIVIC_MODE_NORMAL, 0);
            none.revision = DUEL_EVENT_PACK(id, DUEL_CIVIC_EVENT_PHASE_ACTIVE,
                id >= DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER ?
                    DUEL_CIVIC_EVENT_TARGET_SHARED : DUEL_CIVIC_EVENT_TARGET_LEFT);
            duel_fb_clear(&variant[floor]); draw_rare_event(&variant[floor], &none, true);
        }
        EXPECT(memcmp(&variant[0], &variant[1], sizeof variant[0]) != 0 &&
              memcmp(&variant[0], &variant[2], sizeof variant[0]) != 0 &&
              memcmp(&variant[1], &variant[2], sizeof variant[0]) != 0);
    }
    none.civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_WORKSHOP, DUEL_CIVIC_MODE_NORMAL, 0);
    none.revision = DUEL_EVENT_PACK(DUEL_CIVIC_EVENT_RUNAWAY_SCROLL,
                                    DUEL_CIVIC_EVENT_PHASE_ACTIVE,
                                    DUEL_CIVIC_EVENT_TARGET_LEFT);
    none.floor_transition = INCANTATION_FLOOR_TRANSITION_PACK(DUEL_CIVIC_FLOOR_COMMONS, 1, true);
    duel_fb_t transition, commons;
    duel_fb_clear(&transition); draw_rare_event(&transition, &none, true);
    none.civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_COMMONS, DUEL_CIVIC_MODE_NORMAL, 0);
    none.floor_transition = 0;
    duel_fb_clear(&commons); draw_rare_event(&commons, &none, true);
    EXPECT(memcmp(&transition, &commons, sizeof transition) == 0);
    CHECK(ok, "incantation_rare_event_floor_family_phase_mode_target_routing_and_safety_matrix");
}

static void test_aftermath_floor_kind_phase_half_matrix(void) {
    bool ok = true;
    sim_world_t world; sim_init(&world, SIMF_AUTHORITATIVE, 0);
    for (uint8_t floor = 0; floor < INCANTATION_OCCUPATION_FLOORS; floor++)
        for (uint8_t kind = AFTER_CHEER; kind <= AFTER_MAX_CAST; kind++)
            for (uint8_t phase = 0; phase < 4u; phase++)
                for (uint8_t side = 0; side < 2u; side++) {
                    duel_render_t base = {0}; duel_render_from_world(&base, &world);
                    base.seed = 0x51u; base.civic_phase = 23u;
                    base.civic = DUEL_CIVIC_PACK(floor, DUEL_CIVIC_MODE_NORMAL, 0);
                    duel_render_t after = base;
                    after.shared_pres = (uint8_t)((kind << (side * 3u)) |
                                                   (WORLD_RECOVERY << 6));
                    after.revision = (uint8_t)(INCANTATION_AFTERMATH_WIRE |
                                               (phase << (side * 2u)));
                    duel_fb_t before, first, second;
                    incantation_render(&before, &base, side == 0u, false);
                    incantation_render(&first, &after, side == 0u, false);
                    incantation_render(&second, &after, side == 0u, false);
                    EXPECT(memcmp(&first, &second, sizeof first) == 0 &&
                          memcmp(&before, &first, sizeof first) != 0 &&
                          band_difference(&before, &first, 0, DUEL_FLOOR_BEAM_Y) == 0u &&
                          band_difference(&before, &first, DUEL_FLOOR_Y1 + 1, DUEL_CANVAS_H - 1) == 0u);
                }
    CHECK(ok, "incantation_aftermath_floor_kind_phase_half_anchor_priority_and_protected_regions");
}

static void incantation_render(duel_fb_t *fb, const duel_render_t *r, bool is_left,
                       bool diagnostics) {
    duel_fb_clear(fb);
    wiz_draw_scene(fb, r, is_left, 7u, diagnostics);
}

static uint64_t incantation_bytes_hash(const void *data, size_t size) {
    const uint8_t *p = data;
    uint64_t h = UINT64_C(1469598103934665603);
    while (size--) { h ^= *p++; h *= UINT64_C(1099511628211); }
    return h;
}

static uint8_t quiet_action(uint8_t action) {
    if (action == DUEL_CIVIC_ACTION_WALK) return DUEL_CIVIC_ACTION_REST;
    if (action == DUEL_CIVIC_ACTION_REACT) return DUEL_CIVIC_ACTION_INSPECT;
    if (action == DUEL_CIVIC_ACTION_WATCH_ROOF) return DUEL_CIVIC_ACTION_WORK;
    return action;
}

static void test_resident_occupation_derivation(void) {
    bool seen[2][INCANTATION_OCCUPATION_FLOORS][DUEL_CIVIC_ACTION_COUNT] = {{{false}}};
    bool personalities[2][DUEL_CIVIC_PERSONALITY_COUNT] = {{false}};
    bool ok = true;
    for (uint16_t seed = 0; seed < 256u; seed++) {
        for (uint8_t side = 0; side < 2u; side++) {
            personalities[side][civic_resident_personality((uint8_t)seed, side == 0u)] = true;
            for (uint8_t slot = 0; slot < 16u; slot++) {
                uint8_t phase = (uint8_t)(slot * DUEL_CIVIC_ACTION_SLOT);
                civic_resident_t common = civic_resident_derive((uint8_t)seed, side == 0u,
                    DUEL_CIVIC_FLOOR_COMMONS, DUEL_CIVIC_MODE_NORMAL, phase);
                for (uint8_t floor = 0; floor < INCANTATION_OCCUPATION_FLOORS; floor++) {
                    civic_resident_t a = civic_resident_derive((uint8_t)seed, side == 0u,
                        floor, DUEL_CIVIC_MODE_NORMAL, phase);
                    civic_resident_t b = civic_resident_derive((uint8_t)seed, side == 0u,
                        floor, DUEL_CIVIC_MODE_NORMAL, phase);
                    EXPECT(memcmp(&a, &b, sizeof a) == 0 && a.action == common.action);
                    EXPECT(a.station == INCANTATION_OCCUPATION_KEY(floor, a.action));
                    seen[side][floor][a.action] = true;

                    civic_resident_t quiet = civic_resident_derive((uint8_t)seed, side == 0u,
                        floor, DUEL_CIVIC_MODE_QUIET, phase);
                    EXPECT(quiet.action == quiet_action(common.action) &&
                          quiet.station == INCANTATION_OCCUPATION_KEY(floor, quiet.action));
                }
                civic_resident_t special = civic_resident_derive((uint8_t)seed, side == 0u,
                    DUEL_CIVIC_FLOOR_SPECIAL, DUEL_CIVIC_MODE_NORMAL, phase);
                EXPECT(special.action == common.action &&
                      special.station == INCANTATION_OCCUPATION_KEY(DUEL_CIVIC_FLOOR_SPECIAL,
                                                            special.action));
            }
        }
    }
    for (uint8_t side = 0; side < 2u; side++) {
        for (uint8_t p = 0; p < DUEL_CIVIC_PERSONALITY_COUNT; p++) EXPECT(personalities[side][p]);
        for (uint8_t floor = 0; floor < INCANTATION_OCCUPATION_FLOORS; floor++)
            for (uint8_t action = 0; action < DUEL_CIVIC_ACTION_COUNT; action++)
                EXPECT(seen[side][floor][action]);
    }

    /* Authoritative aftermath suppresses personality, progress, carry, and
     * object-reaction modifiers while retaining the same resident body/task. */
    civic_resident_t after = {DUEL_CIVIC_PERSONALITY_DILIGENT, DUEL_CIVIC_ACTION_REACT,
        INCANTATION_OCCUPATION_KEY(DUEL_CIVIC_FLOOR_RESEARCH, DUEL_CIVIC_ACTION_REACT), 0,
        RESIDENT_CHEER};
    duel_fb_t first, second;
    duel_fb_clear(&first); duel_fb_clear(&second);
    civic_resident_draw(&first, &after, true, DUEL_CIVIC_MODE_NORMAL, 0);
    after.personality = DUEL_CIVIC_PERSONALITY_DISTRACTED; after.progress = 15;
    civic_resident_draw(&second, &after, true, DUEL_CIVIC_MODE_NORMAL, 99);
    EXPECT(memcmp(&first, &second, sizeof first) == 0);
    CHECK(ok, "incantation_resident_42_keys_personalities_quiet_fallback_deterministic_aftermath");
}

static void test_resident_geometry_and_object_separation(void) {
    bool ok = true;
    for (uint8_t side = 0; side < 2u; side++) {
        civic_resident_t core = {DUEL_CIVIC_PERSONALITY_DILIGENT, DUEL_CIVIC_ACTION_WORK,
            INCANTATION_OCCUPATION_KEY(DUEL_CIVIC_FLOOR_COMMONS, DUEL_CIVIC_ACTION_WORK), 0, 0xffu};
        duel_fb_t body;
        duel_fb_clear(&body);
        civic_resident_draw(&body, &core, side == 0u, DUEL_CIVIC_MODE_NORMAL, 0);
        int x0 = 32, x1 = -1, y0 = 128, y1 = -1;
        for (int y = 0; y < DUEL_CANVAS_H; y++)
            for (int x = 0; x < DUEL_CANVAS_W; x++)
                if (duel_fb_get(&body, x, y)) {
                    if (x < x0) x0 = x;
                    if (x > x1) x1 = x;
                    if (y < y0) y0 = y;
                    if (y > y1) y1 = y;
                }
        EXPECT(x1 - x0 + 1 == 5 && y1 - y0 + 1 == 14 && y0 >= 61 && y1 <= 110);

        for (uint8_t floor = 0; floor < INCANTATION_OCCUPATION_FLOORS; floor++) {
            core.station = INCANTATION_OCCUPATION_KEY(floor, DUEL_CIVIC_ACTION_WORK);
            duel_fb_clear(&body);
            civic_resident_draw(&body, &core, side == 0u, DUEL_CIVIC_MODE_NORMAL, 0);
            duel_fb_t object;
            duel_fb_clear(&object);
            incantation_resident_draw_attunement(&object, side == 0u, floor);
            for (int y = 0; y < DUEL_CANVAS_H; y++)
                for (int x = 0; x < DUEL_CANVAS_W; x++)
                    EXPECT(!(duel_fb_get(&body, x, y) && duel_fb_get(&object, x, y)));

            duel_fb_t room;
            render_floor_scene(floor, side == 0u, 0u, &room);
            EXPECT(framebuffer_pixels(&room) > framebuffer_pixels(&body) * 2u);
        }
    }
    CHECK(ok, "incantation_resident_5x14_core_bounds_negative_space_and_object_mass");
}

/* v12 wire-compression canary: the planned descriptor repack drops the
 * interaction bits by substituting SOLID for COMBINE on the slave, which is
 * sound only while no renderer path draws COMBINE differently (the sole
 * non-authoritative interaction read is the INTERACT_PHASE portal). This
 * pins that spike result: it fails the moment anyone adds a COMBINE visual. */
static void test_render_interaction_combine_solid_parity(void) {
    bool ok = true;
    static const uint8_t progresses[] = {60u, 200u};
    for (uint8_t elem = 0; elem < 4u; elem++)
        for (uint8_t form = 0; form < 8u; form++) {
            bool spell_drawn = false;
            for (size_t p = 0; p < sizeof progresses; p++)
                for (uint8_t caster = 0; caster < 2u; caster++) {
                    sim_world_t w;
                    sim_init(&w, SIMF_AUTHORITATIVE, 0);
                    install_spell(&w, caster,
                        SPELL_DESC_PACK(form, elem, PAY_DAMAGE, TRAJ_MID, 2,
                                        STATUS_NONE, INTERACT_COMBINE,
                                        TEMPO_RAPID, TREND_STEADY, 0),
                        progresses[p]);
                    duel_render_t combine = {0};
                    duel_render_from_world(&combine, &w);
                    w.spell[caster].descriptor =
                        SPELL_DESC_PACK(form, elem, PAY_DAMAGE, TRAJ_MID, 2,
                                        STATUS_NONE, INTERACT_SOLID,
                                        TEMPO_RAPID, TREND_STEADY, 0);
                    duel_render_t solid = {0};
                    duel_render_from_world(&solid, &w);
                    w.spell[caster].active = 0;
                    duel_render_t none = {0};
                    duel_render_from_world(&none, &w);
                    for (uint8_t half = 0; half < 2u; half++) {
                        duel_fb_t fc, fs, fn;
                        incantation_render(&fc, &combine, half == 0u, false);
                        incantation_render(&fs, &solid, half == 0u, false);
                        incantation_render(&fn, &none, half == 0u, false);
                        EXPECT(memcmp(&fc, &fs, sizeof fc) == 0);
                        spell_drawn |= memcmp(&fc, &fn, sizeof fc) != 0;
                    }
                }
            /* Guard against a vacuous pass: every combo must actually put
             * carrier pixels on at least one canvas. */
            EXPECT(spell_drawn);
        }
    CHECK(ok, "incantation_render_combine_solid_parity_all_elements_forms");
}

/* Mirrors hp_window_xy: 2x2 lit windows, gapward column x7-8, outer x3-4,
 * rows bottom-up at y56/52/48/44 (each window owns py and py+1). */
static bool health_pixel(bool is_left, int hp_index, int x, int y) {
    int canonical_x = (hp_index & 1) ? 3 : 7;
    int px = is_left ? canonical_x : DUEL_CANVAS_W - 2 - canonical_x;
    int py = 56 - (hp_index / 2) * 4;
    return (y == py || y == py + 1) && (x == px || x == px + 1);
}

static void test_health_grid_geometry_and_lifecycles(void) {
    bool ok = true;
    for (uint8_t side = 0; side < 2u; side++) {
        for (uint8_t hp = 0; hp <= SIM_MAX_HP; hp++) {
            sim_world_t w;
            sim_init(&w, SIMF_AUTHORITATIVE, 0);
            w.wiz[side].hp = hp;
            duel_render_t r = {0}; duel_render_from_world(&r, &w);
            duel_fb_t fb; incantation_render(&fb, &r, side == 0u, false);
            unsigned lit = 0;
            for (int y = 44; y <= 57; y++) {
                for (int x = 3; x <= 8; x++) {
                    int sx = side == 0u ? x : DUEL_CANVAS_W - 1 - x;
                    bool expected = false;
                    for (int i = 0; i < hp; i++) expected |= health_pixel(side == 0u, i, sx, y);
                    bool actual = duel_fb_get(&fb, sx, y);
                    if (actual != expected)
                        printf("DIAG health side=%u hp=%u x=%d y=%d actual=%u expected=%u\n",
                               side, hp, sx, y, actual, expected);
                    EXPECT(actual == expected);
                    lit += actual;
                }
            }
            EXPECT(lit == 4u * hp);
        }
    }

    /* The fixed grid remains unobscured for every wizard tableau. */
    static const uint8_t life[] = {LIFE_ACTIVE, LIFE_ACTIVE, LIFE_ACTIVE,
        LIFE_COLLAPSE, LIFE_DOWNED, LIFE_MEDIC, LIFE_REPLACE};
    static const uint8_t pose[] = {POSE_IDLE, POSE_CAST, POSE_RECOVER,
        POSE_IDLE, POSE_IDLE, POSE_IDLE, POSE_IDLE};
    static const uint8_t ticks[] = {0, 0, 0, SIM_COLLAPSE_TICKS,
        SIM_DOWNED_TICKS, SIM_MEDIC_TICKS, SIM_REPLACE_TICKS};
    for (uint8_t side = 0; side < 2u; side++)
        for (size_t state = 0; state < sizeof life; state++) {
            sim_world_t w;
            sim_init(&w, SIMF_AUTHORITATIVE, 0);
            w.wiz[side].life = life[state]; w.wiz[side].life_ticks = ticks[state];
            w.wiz[side].pose = pose[state]; w.wiz[side].hp = 0;
            duel_render_t empty = {0}; duel_render_from_world(&empty, &w);
            duel_fb_t zero; incantation_render(&zero, &empty, side == 0u, false);
            w.wiz[side].hp = SIM_MAX_HP;
            duel_render_t full = {0}; duel_render_from_world(&full, &w);
            duel_fb_t grid; incantation_render(&grid, &full, side == 0u, false);
            for (int i = 0; i < SIM_MAX_HP; i++) {
                for (int x = 0; x < DUEL_CANVAS_W; x++)
                    for (int y = 44; y <= 57; y++)
                        if (health_pixel(side == 0u, i, x, y))
                            if (duel_fb_get(&zero, x, y) || !duel_fb_get(&grid, x, y)) {
                                printf("DIAG health-pose side=%u state=%zu x=%d y=%d zero=%u full=%u\n",
                                       side, state, x, y, duel_fb_get(&zero, x, y),
                                       duel_fb_get(&grid, x, y));
                                ok = false;
                            }
            }
        }
    CHECK(ok, "incantation_health_0_8_window_2x2_bottom_up_mirror_6x14_pose_clearance");
}

static void test_local_layer_attunement(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint64_t before = incantation_bytes_hash(&w, sizeof w);
    duel_render_t base = {0}; duel_render_from_world(&base, &w);
    base.civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_RESEARCH, DUEL_CIVIC_MODE_NORMAL, 0);
    base.seed = 7; base.civic_phase = 32;
    base.layer = DUEL_RENDER_LAYER_PACK(0, DUEL_RENDER_LOCAL_NONE);
    duel_fb_t bl, br, ll, lr, rl, rr;
    incantation_render(&bl, &base, true, false); incantation_render(&br, &base, false, false);

    duel_render_t local = base;
    local.layer = DUEL_RENDER_LAYER_PACK(1, DUEL_RENDER_LOCAL_LEFT);
    incantation_render(&ll, &local, true, false); incantation_render(&lr, &local, false, false);
    bool left_changes = memcmp(&bl, &ll, sizeof bl) != 0;
    bool left_spares_right = memcmp(&br, &lr, sizeof br) == 0;
    bool ok = true;
    EXPECT(left_changes && left_spares_right);
    local.layer = DUEL_RENDER_LAYER_PACK(2, DUEL_RENDER_LOCAL_RIGHT);
    incantation_render(&rl, &local, true, false); incantation_render(&rr, &local, false, false);
    bool right_spares_left = memcmp(&bl, &rl, sizeof bl) == 0;
    bool right_changes = memcmp(&br, &rr, sizeof br) != 0;
    EXPECT(right_spares_left && right_changes);

    /* Release and ordinary global-layer typing reproduce the exact baseline. */
    local.layer = DUEL_RENDER_LAYER_PACK(3, DUEL_RENDER_LOCAL_NONE);
    incantation_render(&rl, &local, true, false); incantation_render(&rr, &local, false, false);
    bool global_left_same = memcmp(&bl, &rl, sizeof bl) == 0;
    bool global_right_same = memcmp(&br, &rr, sizeof br) == 0;
    EXPECT(global_left_same && global_right_same);

    /* During the bilateral dwell each OLED can show its own mark. Once scry is
     * authoritative, the instruments replace both without retaining either. */
    duel_render_t open_none = base;
    open_none.view.outcome_overlay |= 0x10u;
    open_none.layer = DUEL_RENDER_LAYER_PACK(3, DUEL_RENDER_LOCAL_NONE);
    duel_fb_t ol0, or0, ol1, or1;
    incantation_render(&ol0, &open_none, true, false); incantation_render(&or0, &open_none, false, false);
    duel_render_t open_local = open_none;
    open_local.layer = DUEL_RENDER_LAYER_PACK(3, DUEL_RENDER_LOCAL_LEFT);
    incantation_render(&ol1, &open_local, true, false);
    open_local.layer = DUEL_RENDER_LAYER_PACK(3, DUEL_RENDER_LOCAL_RIGHT);
    incantation_render(&or1, &open_local, false, false);
    bool open_left_same = memcmp(&ol0, &ol1, sizeof ol0) == 0;
    bool open_right_same = memcmp(&or0, &or1, sizeof or0) == 0;
    EXPECT(open_left_same && open_right_same);

    sim_world_t typed;
    sim_init(&typed, SIMF_AUTHORITATIVE, 0);
    for (int i = 0; i < SCRY_PENDING_TICKS * 3; i++)
        sim_tick(&typed, (sim_inputs_t){.scry_mask = SCRY_M_L | SCRY_M_OTHER,
                 .layer = {1, 0}, .held_pos = {1u, 0}}, NULL, 0, 0);
    bool typing_closed = !scry_is_open(&typed) && typed.scry.state == SCRY_FIRST_HELD;
    bool world_same = incantation_bytes_hash(&w, sizeof w) == before;
    EXPECT(typing_closed && world_same);
    if (!ok) printf("DIAG local lc=%u lsr=%u rsl=%u rc=%u gl=%u gr=%u ol=%u or=%u typing=%u world=%u state=%u\n",
                    left_changes, left_spares_right, right_spares_left, right_changes,
                    global_left_same, global_right_same, open_left_same, open_right_same,
                    typing_closed, world_same, typed.scry.state);
    CHECK(ok, "incantation_local_attunement_physical_half_release_typing_pending_and_scry_suppression");
}

static bool framebuffer_subset(const duel_fb_t *small, const duel_fb_t *large) {
    for (int y = 0; y < DUEL_CANVAS_H; y++)
        for (int x = 0; x < DUEL_CANVAS_W; x++)
            if (duel_fb_get(small, x, y) && !duel_fb_get(large, x, y)) return false;
    return true;
}

static void test_diegetic_scry_instruments(void) {
    bool ok = true;
    for (uint8_t floor = 0; floor < INCANTATION_OCCUPATION_FLOORS; floor++)
        for (uint8_t scene = 0; scene < SCRY_SCENES; scene++)
            for (uint8_t online = 0; online < 2u; online++)
                for (uint8_t notif_case = 0; notif_case < 2u; notif_case++)
                    for (uint8_t side = 0; side < 2u; side++) {
                    sim_world_t w;
                    sim_init(&w, SIMF_AUTHORITATIVE, 0);
                    duel_render_t r = {0}; duel_render_from_world(&r, &w);
                    r.civic = DUEL_CIVIC_PACK(floor, DUEL_CIVIC_MODE_NORMAL, 0);
                    r.seed = 9; r.civic_phase = 48;
                    uint8_t notif = notif_case ? 4u : 0u;
                    r.external = DUEL_HOST_CONTEXT_PACK(online, scene, notif, false);
                    r.layer = DUEL_RENDER_LAYER_PACK(scene, DUEL_RENDER_LOCAL_NONE);
                    r.view.outcome_overlay = (uint8_t)((r.view.outcome_overlay & 0x1fu) |
                                                       (scene << 5));
                    duel_fb_t base, open;
                    incantation_render(&base, &r, side == 0u, false);
                    r.view.outcome_overlay |= 0x10u;
                    incantation_render(&open, &r, side == 0u, false);
                    bool subset = framebuffer_subset(&base, &open);
                    bool changed = memcmp(&base, &open, sizeof base) != 0;
                    if (!subset || !changed)
                        printf("DIAG scry case floor=%u scene=%u online=%u notif=%u side=%u subset=%u changed=%u\n",
                               floor, scene, online, notif, side, subset, changed);
                    EXPECT(subset && changed);
                    for (int y = 0; y < DUEL_CANVAS_H; y++)
                        for (int x = 0; x < DUEL_CANVAS_W; x++)
                            if (duel_fb_get(&base, x, y) != duel_fb_get(&open, x, y))
                                if (!(y <= 35 || (y >= 59 && y <= 60))) {
                                    printf("DIAG scry exclusion floor=%u scene=%u online=%u notif=%u side=%u x=%d y=%d\n",
                                           floor, scene, online, notif, side, x, y);
                                    ok = false;
                                }
                }

    /* Compare only added instruments: all architectural positions mirror while
     * selector indices retain their semantic order. */
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    duel_render_t r = {0}; duel_render_from_world(&r, &w);
    r.civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_COMMONS, DUEL_CIVIC_MODE_NORMAL, 0);
    r.external = DUEL_HOST_CONTEXT_PACK(true, DUEL_HOST_SCENE_FOCUS, 3, true);
    r.alert = DUEL_HOST_ALERT_PACK(DUEL_HOST_CATEGORY_SECURITY,
                                   DUEL_HOST_PRIORITY_CRITICAL, 7);
    r.layer = DUEL_RENDER_LAYER_PACK(3, DUEL_RENDER_LOCAL_NONE);
    duel_fb_t lb, rb, lo, ro;
    incantation_render(&lb, &r, true, false); incantation_render(&rb, &r, false, false);
    r.view.outcome_overlay = (uint8_t)((r.view.outcome_overlay & 0x0fu) | 0x10u |
                                       (DUEL_HOST_SCENE_FOCUS << 5));
    incantation_render(&lo, &r, true, false); incantation_render(&ro, &r, false, false);
    for (int y = 0; y < DUEL_CANVAS_H; y++)
        for (int x = 0; x < DUEL_CANVAS_W; x++) {
            bool ld = duel_fb_get(&lo, x, y) != duel_fb_get(&lb, x, y);
            bool rd = duel_fb_get(&ro, 31 - x, y) != duel_fb_get(&rb, 31 - x, y);
            if (ld != rd) {
                printf("DIAG scry mirror x=%d y=%d ld=%u rd=%u\n", x, y, ld, rd);
                ok = false;
            }
        }

    /* Every normalized alert remains visible in the outer corner; stale-link
     * and diagnostics retain their later-layer priority. */
    duel_render_t empty_alert = r;
    empty_alert.alert = 0;
    empty_alert.external = DUEL_HOST_CONTEXT_PACK(true, DUEL_HOST_SCENE_FOCUS, 4, false);
    duel_fb_t empty_alert_fb;
    incantation_render(&empty_alert_fb, &empty_alert, true, false);
    for (uint8_t category = 1; category < DUEL_HOST_CATEGORY_COUNT; category++)
        for (uint8_t priority_level = DUEL_HOST_PRIORITY_LOW;
             priority_level <= DUEL_HOST_PRIORITY_CRITICAL; priority_level++)
            for (uint8_t persistent = 0; persistent < 2u; persistent++) {
                duel_render_t alert = r;
                alert.external = DUEL_HOST_CONTEXT_PACK(true, DUEL_HOST_SCENE_FOCUS,
                                                         4, persistent);
                alert.alert = DUEL_HOST_ALERT_PACK(category, priority_level, 3);
                duel_fb_t category_fb;
                incantation_render(&category_fb, &alert, true, false);
                EXPECT(memcmp(&empty_alert_fb, &category_fb, sizeof category_fb) != 0);
            }
    r.flags |= DUEL_RENDER_STALE;
    r.diag_tick = 7;
    duel_fb_t priority;
    incantation_render(&priority, &r, true, true);
    /* Stale-link box in the corner, and the diagnostics build's 1 Hz sync
     * heartbeat on the left tower-top tip (diag_tick 7 < 13 -> lit at x6 y0),
     * drawn last so it keeps its later-layer priority over the scene. */
    EXPECT(duel_fb_get(&priority, 23, 2) && duel_fb_get(&priority, 6, 0));
    CHECK(ok, "incantation_diegetic_scry_all_scenes_floors_host_alert_subset_mirror_exclusions_priority");
}

static void test_gap_cue_families_temporal_mirrors(void) {
    static const uint32_t desc[] = {
        SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE, TRAJ_MID, 2,
                        STATUS_NONE, INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, 1),
        SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FROST, PAY_DAMAGE, TRAJ_HOMING, 2,
                        STATUS_NONE, INTERACT_SOLID, TEMPO_RAPID, TREND_ACCELERATING, 2),
        SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_VOID, PAY_DAMAGE, TRAJ_MID, 2,
                        STATUS_NONE, INTERACT_PHASE, TEMPO_FLOWING, TREND_STEADY, 3),
        SPELL_DESC_PACK(SPELL_BEAM, ELEM_EMBER, PAY_DAMAGE, TRAJ_MID, 3,
                        STATUS_NONE, INTERACT_SOLID, TEMPO_RAPID, TREND_STEADY, 0),
        SPELL_DESC_PACK(SPELL_CHAIN, ELEM_FORCE, PAY_DAMAGE, TRAJ_HOMING, 3,
                        STATUS_NONE, INTERACT_SOLID, TEMPO_RAPID, TREND_IRREGULAR, 1),
    };
    static const uint8_t progress[] = {105u, 130u, 155u};
    bool ok = true;
    for (size_t family = 0; family < sizeof desc / sizeof desc[0]; family++) {
        for (size_t stage = 0; stage < sizeof progress; stage++) {
            duel_view_spell_t left = {.active = 1u, .descriptor = desc[family],
                .progress = progress[stage],
                .kind = DUEL_KIND_WITH_TIER(DUEL_KIND_PACK(SPELL_DESC_ELEMENT(desc[family]),
                                                           MOD_NONE, PAY_IMPACT), 1u)};
            duel_view_spell_t right = left;
            duel_fb_t ll, lr, rl, rr;
            duel_fb_clear(&ll); duel_fb_clear(&lr); duel_fb_clear(&rl); duel_fb_clear(&rr);
            incantation_draw_spell(&ll, &left, 0, 0, true, 9u);
            incantation_draw_spell(&lr, &left, 0, 0, false, 9u);
            incantation_draw_spell(&rl, &right, 1, 0, true, 9u);
            incantation_draw_spell(&rr, &right, 1, 0, false, 9u);
            EXPECT(exact_mirror(&ll, &rr) && exact_mirror(&lr, &rl) &&
                  framebuffer_pixels(&ll) + framebuffer_pixels(&lr) > 0u);
        }
    }

    /* Non-continuous families have no edge handoff before departure or after
     * arrival. Beam/chain remain bilateral by design and are tested above. */
    for (size_t family = 0; family < 3u; family++) {
        for (uint8_t p = 32u; p <= 224u; p = (uint8_t)(p + 192u)) {
            duel_view_spell_t sp = {.active = 1u, .descriptor = desc[family],
                .progress = p,
                .kind = DUEL_KIND_WITH_TIER(DUEL_KIND_PACK(SPELL_DESC_ELEMENT(desc[family]),
                                                           MOD_NONE, PAY_IMPACT), 1u)};
            duel_fb_t left, right;
            duel_fb_clear(&left); duel_fb_clear(&right);
            incantation_draw_spell(&left, &sp, 0, 0, true, 9u);
            incantation_draw_spell(&right, &sp, 0, 0, false, 9u);
            for (int y = 0; y < DUEL_CANVAS_H; y++)
                EXPECT(!duel_fb_get(&left, 31, y) && !duel_fb_get(&right, 0, y));
            if (p == 224u) break;
        }
    }
    CHECK(ok, "incantation_gap_cue_departure_midpoint_arrival_mirrors_and_bounds");
}

static void test_all_forms_bilateral_mirror(void) {
    bool ok = true;
    for (uint8_t form = SPELL_PROJECTILE; form <= SPELL_CONJURE; form++) {
        uint8_t trajectory = form == SPELL_FIREBALL ? TRAJ_ROOF :
                             form == SPELL_GROUND_WAVE ? TRAJ_GROUND :
                             form == SPELL_CHAIN ? TRAJ_HOMING :
                             form == SPELL_CONJURE ? TRAJ_RETURNING : TRAJ_MID;
        uint32_t desc = SPELL_DESC_PACK(form, ELEM_FORCE, PAY_DAMAGE, trajectory,
                                        3, STATUS_NONE,
                                        form == SPELL_SINGULARITY ? INTERACT_ABSORB : INTERACT_SOLID,
                                        TEMPO_RAPID, TREND_ACCELERATING, 2);
        uint8_t progress = form == SPELL_BEAM ? 128u :
                           form == SPELL_SINGULARITY ? 144u :
                           form == SPELL_SWARM ? (uint8_t)((5u << 5) | 14u) :
                           form == SPELL_CHAIN ? 176u :
                           form == SPELL_CONJURE ? (uint8_t)((3u << 5) | 14u) : 72u;
        sim_world_t left_world, right_world;
        sim_init(&left_world, SIMF_AUTHORITATIVE, 0);
        sim_init(&right_world, SIMF_AUTHORITATIVE, 0);
        install_spell(&left_world, 0, desc, progress);
        install_spell(&right_world, 1, desc, progress);
        duel_view_t lv, rv;
        duel_view_from_world(&left_world, &lv); duel_view_from_world(&right_world, &rv);
        duel_view_spell_t ls = duel_view_spell(&lv, 0), rs = duel_view_spell(&rv, 1);
        duel_fb_t ll, lr, rl, rr;
        duel_fb_clear(&ll); duel_fb_clear(&lr); duel_fb_clear(&rl); duel_fb_clear(&rr);
        incantation_draw_spell(&ll, &ls, 0, 0, true, 5);
        incantation_draw_spell(&lr, &ls, 0, 0, false, 5);
        incantation_draw_spell(&rl, &rs, 1, 0, true, 5);
        incantation_draw_spell(&rr, &rs, 1, 0, false, 5);
        bool mirrored = exact_mirror(&ll, &rr) && exact_mirror(&lr, &rl);
        if (!mirrored) printf("DIAG bilateral form=%u\n", form);
        EXPECT(mirrored);
    }
    CHECK(ok, "incantation_all_forms_bilateral_pixel_mirror");
}

static void test_aftermath_split_loss_and_reconnect(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.aftermath[0] = (sim_aftermath_t){AFTER_FIRE, 175, 4, RESIDENT_PANIC,
                                       ROOM_DISRUPTED, OBJECT_FIRE};
    w.world_state = WORLD_CRISIS;
    duel_snapshot_t first, later, corrupt, restarted;
    test_encode_snapshot(&w, 3, 10, &first);
    duel_rx_state_t rx = {0};
    bool ok = true;
    EXPECT(duel_decode_valid(&first) && duel_rx_accept(&rx, &first, false));
    uint8_t old_revision = rx.last.revision;
    wait_ticks(&w, 50);
    test_encode_snapshot(&w, 3, 11, &later);
    EXPECT(later.revision != old_revision && rx.last.revision == old_revision); /* dropped update */
    corrupt = later; corrupt.shared_pres ^= 0x04u;
    EXPECT(!duel_decode_valid(&corrupt) && rx.last.revision == old_revision);
    EXPECT(duel_rx_accept(&rx, &later, false) && rx.last.revision == later.revision);
    wait_ticks(&w, 50);
    test_encode_snapshot(&w, 4, 1, &restarted);
    EXPECT(duel_rx_accept(&rx, &restarted, true) &&
          rx.last.session == 4u && rx.last.revision == restarted.revision &&
          INCANTATION_AFTER_KIND(rx.last.shared_pres, 0) == AFTER_FIRE);
    CHECK(ok, "incantation_aftermath_split_loss_corruption_and_reconnect");
}

static void test_runtime_mailbox_policy(void) {
    duel_mailbox_t box = {0};
    uint8_t seen = 0, out[8] = {0};
    const uint8_t first[8] = {1,2,3,4,5,6,7,8};
    const uint8_t latest[8] = {8,7,6,5,4,3,2,1};
    duel_mailbox_publish(&box, first, sizeof first);
    bool ok = true;
    EXPECT(duel_mailbox_consume(&box, &seen, out, sizeof out) &&
              memcmp(out, first, sizeof out) == 0 &&
              !duel_mailbox_consume(&box, &seen, out, sizeof out));
    box.version++; /* writer in progress: a partial/torn value is never read */
    box.data[0] = 0xffu;
    EXPECT(!duel_mailbox_consume(&box, &seen, out, sizeof out));
    box.version++;
    EXPECT(duel_mailbox_consume(&box, &seen, out, sizeof out) && out[0] == 0xffu);
    duel_mailbox_publish(&box, first, sizeof first);
    duel_mailbox_publish(&box, latest, sizeof latest);
    EXPECT(duel_mailbox_consume(&box, &seen, out, sizeof out) &&
          memcmp(out, latest, sizeof out) == 0);
    box.version = 254u;
    seen = 254u;
    duel_mailbox_publish(&box, first, sizeof first); /* version wraps to zero */
    EXPECT(box.version == 0u && duel_mailbox_consume(&box, &seen, out, sizeof out) &&
          seen == 0u && memcmp(out, first, sizeof out) == 0);
    CHECK(ok, "runtime_mailbox_stable_odd_torn_retry_latest_wins_and_wrap");
}

static void test_runtime_tx_policy(void) {
    duel_tx_policy_t tx = {0};
    bool ok = true;
    EXPECT(duel_tx_attempt(&tx, 1000u, false, false, true) && tx.sequence == 1u);
    duel_tx_commit(&tx, 1000u);
    EXPECT(!duel_tx_attempt(&tx, 1079u, false, false, true) && tx.sequence == 2u);
    EXPECT(duel_tx_attempt(&tx, 1080u, false, false, true));
    duel_tx_commit(&tx, 1080u);
    EXPECT(!duel_tx_attempt(&tx, 1329u, false, false, false));
    EXPECT(duel_tx_attempt(&tx, 1330u, false, false, false));
    duel_tx_commit(&tx, 1330u);
    EXPECT(!duel_tx_attempt(&tx, 1331u, true, false, false));
    EXPECT(!duel_tx_attempt(&tx, 1332u, false, true, false));
    EXPECT(duel_tx_attempt(&tx, 1410u, true, false, false));
    EXPECT(duel_tx_attempt(&tx, 1411u, false, true, false));
    tx.have_sent = true;
    tx.last_sent_ms = UINT32_MAX - 39u;
    EXPECT(!duel_tx_attempt(&tx, 39u, false, false, true));
    EXPECT(duel_tx_attempt(&tx, 40u, false, false, true));
    tx.sequence = UINT16_MAX;
    (void)duel_tx_attempt(&tx, 41u, true, false, false);
    EXPECT(tx.sequence == 0u);
    CHECK(ok, "runtime_tx_urgent_semantic_repair_boundaries_sequence_and_timer_wrap");
}

static void test_runtime_presentation_policy(void) {
    duel_floor_policy_t floor = {0};
    bool ok = true;
    EXPECT(duel_floor_note_target(&floor,
        DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_COMMONS, 0, 0), 100u, DUEL_DISPLAY_ACTIVE));
    EXPECT(duel_floor_note_target(&floor,
        DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_RESEARCH, 0, 0), 200u, DUEL_DISPLAY_ACTIVE));
    EXPECT(INCANTATION_FLOOR_TRANSITION_ACTIVE(duel_floor_presentation(&floor, 349u)) &&
          INCANTATION_FLOOR_TRANSITION_PHASE(duel_floor_presentation(&floor, 350u)) == 1u);
    EXPECT(duel_floor_note_target(&floor,
        DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_SPECIAL, 0, 0), 400u, DUEL_DISPLAY_ACTIVE) &&
          floor.source == DUEL_CIVIC_FLOOR_RESEARCH);
    EXPECT(!INCANTATION_FLOOR_TRANSITION_ACTIVE(duel_floor_presentation(&floor, 1000u)));
    EXPECT(duel_floor_note_target(&floor,
        DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_WORKSHOP, 0, 0), 1100u, DUEL_DISPLAY_SLEEP) &&
          !floor.active);

    duel_flash_policy_t flash = {0};
    EXPECT(duel_flash_note(&flash, 1u, FX_IMPACT_L, ELEM_EMBER, UINT32_MAX - 99u) &&
          duel_flash_remaining(&flash, 400u) == 2u &&
          duel_flash_remaining(&flash, 500u) == 0u);
    EXPECT(duel_flash_note(&flash, 2u, FX_WARD_SHATTER_R, ELEM_FORCE, 1000u) &&
          duel_flash_remaining(&flash, 1399u) == 1u &&
          duel_flash_remaining(&flash, 1400u) == 0u);

    uint32_t grace = 19u;
    EXPECT(duel_wake_grace_active(&grace, UINT32_MAX - 20u) &&
          duel_wake_grace_active(&grace, 0u) &&
          !duel_display_should_follow(DUEL_DISPLAY_SLEEP, &grace, 0u) &&
          duel_display_should_follow(DUEL_DISPLAY_SLEEP, &grace, 20u) && grace == 0u);
    CHECK(ok, "runtime_floor_restart_sleep_snap_flash_deadlines_wake_grace_and_follow");
}

// OLED power policy: the only sim module previously without a direct test.
// Timer arithmetic is wrap-safe unsigned age, so a wrap boundary case is
// included alongside the DIM/SLEEP thresholds, key wake, follow, and fade.
static void test_display_power_policy(void) {
    bool ok = true;
    duel_display_policy_t d;
    duel_display_init(&d, 1000u);
    EXPECT(d.phase == DUEL_DISPLAY_ACTIVE && d.initialized);

    /* Threshold ticks: one ms early stays, the boundary transitions. */
    EXPECT(duel_display_update(&d, 1000u + DUEL_DISPLAY_DIM_MS - 1u) == DUEL_DISPLAY_ACTIVE);
    EXPECT(duel_display_update(&d, 1000u + DUEL_DISPLAY_DIM_MS) == DUEL_DISPLAY_DIM);
    EXPECT(duel_display_update(&d, 1000u + DUEL_DISPLAY_SLEEP_MS - 1u) == DUEL_DISPLAY_DIM);
    EXPECT(duel_display_update(&d, 1000u + DUEL_DISPLAY_SLEEP_MS) == DUEL_DISPLAY_SLEEP);

    /* A physical key is the sole wake source. */
    duel_display_note_key(&d, 2000000u);
    EXPECT(d.phase == DUEL_DISPLAY_ACTIVE &&
           duel_display_update(&d, 2000001u) == DUEL_DISPLAY_ACTIVE);

    /* Brightness: full while active, ramp during the dim fade, floor after,
     * dark asleep. */
    EXPECT(duel_display_brightness(&d, 2000001u) == DUEL_DISPLAY_ACTIVE_BRIGHTNESS);
    uint32_t dim_at = 2000000u + DUEL_DISPLAY_DIM_MS;
    duel_display_update(&d, dim_at);
    EXPECT(d.phase == DUEL_DISPLAY_DIM &&
           duel_display_brightness(&d, dim_at) == DUEL_DISPLAY_ACTIVE_BRIGHTNESS);
    uint8_t mid_fade = duel_display_brightness(&d, dim_at + DUEL_DISPLAY_FADE_MS / 2u);
    EXPECT(mid_fade < DUEL_DISPLAY_ACTIVE_BRIGHTNESS && mid_fade > DUEL_DISPLAY_DIM_BRIGHTNESS);
    EXPECT(duel_display_brightness(&d, dim_at + DUEL_DISPLAY_FADE_MS) ==
           DUEL_DISPLAY_DIM_BRIGHTNESS);
    duel_display_update(&d, 2000000u + DUEL_DISPLAY_SLEEP_MS);
    EXPECT(duel_display_brightness(&d, 2000000u + DUEL_DISPLAY_SLEEP_MS + 5u) == 0u);

    /* Follow adopts the master's phase; ACTIVE also refreshes the local key
     * clock; out-of-range phases clamp to ACTIVE. */
    duel_display_follow(&d, DUEL_DISPLAY_ACTIVE, 3000000u);
    EXPECT(d.phase == DUEL_DISPLAY_ACTIVE && d.last_key_ms == 3000000u);
    duel_display_follow(&d, (duel_display_phase_t)3, 3000001u);
    EXPECT(d.phase == DUEL_DISPLAY_ACTIVE);
    duel_display_follow(&d, DUEL_DISPLAY_SLEEP, 3000002u);
    EXPECT(d.phase == DUEL_DISPLAY_SLEEP);

    /* ms-clock wrap: a key just before wrap keeps the panel awake across 0. */
    duel_display_note_key(&d, UINT32_MAX - 10u);
    EXPECT(duel_display_update(&d, 5u) == DUEL_DISPLAY_ACTIVE);
    EXPECT(duel_display_update(&d, DUEL_DISPLAY_DIM_MS - 11u) == DUEL_DISPLAY_DIM);

    /* Presentation deadlines share the same wrap-safe idiom. */
    EXPECT(duel_presentation_remaining(UINT32_MAX - 99u, DUEL_PRESENTATION_IMPACT_MS, 400u) == 2u);
    EXPECT(duel_presentation_remaining(0u, DUEL_PRESENTATION_OTHER_MS, DUEL_PRESENTATION_OTHER_MS) == 0u);
    EXPECT(duel_presentation_remaining(0u, DUEL_PRESENTATION_OTHER_MS, 1u) == 8u);
    CHECK(ok, "display_power_policy_thresholds_wake_follow_fade_and_wrap");
}

// Matrix -> sim_inputs_t sampling (moved out of keymap.c): the scry chord,
// per-side held positions, and the physical spell-layer policy.
static void test_runtime_input_sampling(void) {
    bool ok = true;
    uint16_t rows[DUEL_INPUT_ROWS] = {0};
    sim_inputs_t in = duel_inputs_from_rows(rows);
    EXPECT(in.down_mask == 0 && in.scry_mask == 0 &&
           in.layer[0] == 0 && in.layer[1] == 0 &&
           !in.held_pos[0] && !in.held_pos[1]);

    /* An ordinary key: left down mask, position bit, OTHER (kills the chord). */
    rows[1] = 1u << 2;
    in = duel_inputs_from_rows(rows);
    EXPECT(in.down_mask == 1u && in.held_pos[0] == (1u << (1u * 6u + 2u)) &&
           in.held_pos[1] == 0 && in.scry_mask == SCRY_M_OTHER &&
           in.layer[0] == 0);

    /* Lone left layer thumb: spell layer 1 on the left wizard only. */
    memset(rows, 0, sizeof rows);
    rows[SCRY_KEY_L_ROW] = 1u << SCRY_KEY_L_COL;
    in = duel_inputs_from_rows(rows);
    EXPECT(in.scry_mask == SCRY_M_L && in.layer[0] == 1u && in.layer[1] == 0u &&
           in.down_mask == 1u);

    /* Lone right layer thumb: spell layer 2 on the right wizard only. */
    memset(rows, 0, sizeof rows);
    rows[SCRY_KEY_R_ROW] = 1u << SCRY_KEY_R_COL;
    in = duel_inputs_from_rows(rows);
    EXPECT(in.scry_mask == SCRY_M_R && in.layer[0] == 0u && in.layer[1] == 2u &&
           in.down_mask == 2u && in.held_pos[1] == (1u << (3u * 6u + 4u)));

    /* The deliberate two-thumb chord: layer 3 for both, no OTHER. */
    rows[SCRY_KEY_L_ROW] = 1u << SCRY_KEY_L_COL;
    in = duel_inputs_from_rows(rows);
    EXPECT(in.scry_mask == (SCRY_M_L | SCRY_M_R) &&
           in.layer[0] == 3u && in.layer[1] == 3u && in.down_mask == 3u);

    /* Chord plus any other key = ordinary layer-3 use: OTHER set. */
    rows[0] = 1u;
    in = duel_inputs_from_rows(rows);
    EXPECT(in.scry_mask == (SCRY_M_L | SCRY_M_R | SCRY_M_OTHER));
    CHECK(ok, "runtime_input_sampling_scry_chord_positions_and_spell_layers");
}

static void test_runtime_tick_budget(void) {
    bool ok = true;
    uint32_t next = 1000u;
    bool resynced = true;
    EXPECT(duel_tick_budget(&next, 999u, &resynced) == 0u && !resynced && next == 1000u);
    EXPECT(duel_tick_budget(&next, 1000u, &resynced) == 1u && !resynced &&
           next == 1000u + SIM_TICK_MS);
    /* Missed two deadlines: catch up by replaying, deadline stays on grid. */
    EXPECT(duel_tick_budget(&next, 1000u + 3u * SIM_TICK_MS, &resynced) == 3u &&
           !resynced && next == 1000u + 4u * SIM_TICK_MS);
    /* Long stall (USB suspend): capped at DUEL_TICK_CATCHUP_MAX and resynced
     * to now + one tick instead of replaying history. */
    next = 2000u;
    EXPECT(duel_tick_budget(&next, 2000u + 10u * SIM_TICK_MS, &resynced) ==
               DUEL_TICK_CATCHUP_MAX && resynced &&
           next == 2000u + 11u * SIM_TICK_MS);
    /* 49.7-day wrap boundary: a deadline just before wrap still fires. */
    next = UINT32_MAX - 10u;
    EXPECT(duel_tick_budget(&next, 5u, &resynced) == 1u && !resynced &&
           next == (uint32_t)(UINT32_MAX - 10u + SIM_TICK_MS));
    CHECK(ok, "runtime_tick_budget_catchup_stall_resync_and_wrap");
}

static void test_runtime_slave_presenter(void) {
    bool ok = true;
    duel_slave_presenter_t pres = {0};
    /* Nothing ever received: local fallback, quiet while idle. */
    duel_slave_decision_t d = duel_slave_present(&pres, false, false, false,
                                                 false, false, false);
    EXPECT(!d.use_remote && !d.base_refresh && !d.set_stale);
    /* Local ticks re-render the fallback. */
    d = duel_slave_present(&pres, false, false, false, true, false, false);
    EXPECT(!d.use_remote && d.base_refresh && !d.set_stale);
    /* First accepted snapshot: adopt remote, follow the master's phase. */
    d = duel_slave_present(&pres, true, true, false, false, false, false);
    EXPECT(d.use_remote && d.consider_follow && d.base_refresh);
    /* Steady remote with no new packet: nothing to redo. */
    d = duel_slave_present(&pres, false, true, false, false, false, false);
    EXPECT(d.use_remote && !d.consider_follow && !d.base_refresh);
    /* Link goes stale: fall back once, marking the render stale. */
    d = duel_slave_present(&pres, false, true, true, false, false, false);
    EXPECT(!d.use_remote && d.base_refresh && d.set_stale);
    /* Still stale and already presented as such: quiet until a tick. */
    d = duel_slave_present(&pres, false, true, true, false, false, true);
    EXPECT(!d.use_remote && !d.base_refresh);
    d = duel_slave_present(&pres, false, true, true, true, false, true);
    EXPECT(!d.use_remote && d.base_refresh && d.set_stale);
    /* Re-acquire: fresh acceptance clears stale, re-follows, re-presents. */
    d = duel_slave_present(&pres, true, true, false, false, false, true);
    EXPECT(d.use_remote && d.consider_follow && d.base_refresh);
    /* A render invalidation alone (local keypress) also re-presents remote. */
    d = duel_slave_present(&pres, false, true, false, false, true, false);
    EXPECT(d.use_remote && !d.consider_follow && d.base_refresh);
    CHECK(ok, "runtime_slave_presenter_fallback_stale_edge_and_reacquire");
}

static void test_runtime_civic_shared_derive(void) {
    bool ok = true;
    sim_world_t world;
    sim_init(&world, SIMF_AUTHORITATIVE, 0);
    duel_host_state_t host = {0};
    /* Offline host: no visitor is derived; the rare-event deck still runs
     * (both champions standing => eligible). */
    duel_civic_shared_t calm = duel_civic_shared_derive(0x5au, 1900u, &host,
                                                        &world, 0);
    EXPECT(DUEL_VISITOR_KIND(calm.shared_pres) == DUEL_CIVIC_COURIER_NONE &&
           !(calm.revision & INCANTATION_AFTERMATH_WIRE));
    /* Safety gate (spec §14.1): a downed champion empties the event slot at
     * every civic phase. */
    world.wiz[SIM_SIDE_L].life = LIFE_DOWNED;
    for (uint32_t phase = 0; phase < 256u; phase++) {
        duel_civic_shared_t gated = duel_civic_shared_derive(0x5au,
            phase * DUEL_CIVIC_TICK_MS, &host, &world, 0);
        EXPECT(DUEL_EVENT_ID(gated.revision) == DUEL_CIVIC_EVENT_NONE);
    }
    world.wiz[SIM_SIDE_L].life = LIFE_ACTIVE;
    /* A live aftermath owns both coordination bytes (bit7 discriminator). */
    world.aftermath[0].kind = AFTER_CHEER;
    world.aftermath[0].ticks = 50u;
    world.aftermath[0].intensity = 2u;
    duel_civic_shared_t owned = duel_civic_shared_derive(0x5au, 1900u, &host,
                                                         &world, 0);
    EXPECT((owned.revision & INCANTATION_AFTERMATH_WIRE) &&
           owned.shared_pres == incantation_aftermath_shared(&world) &&
           owned.revision == incantation_aftermath_revision(&world));
    CHECK(ok, "runtime_civic_shared_offline_safety_gate_and_aftermath_override");
}

static void test_runtime_flash_observe(void) {
    bool ok = true;
    sim_world_t world;
    sim_init(&world, SIMF_AUTHORITATIVE, 0);
    /* Left slot carries a live spell; the world then reports a right-side
     * impact: the flash must scale from the LEFT slot's cached style. */
    world.spell[SIM_SIDE_L].active = 1;
    world.spell[SIM_SIDE_L].descriptor = SPELL_DESC_PACK(SPELL_PROJECTILE,
        ELEM_EMBER, PAY_DAMAGE, TRAJ_MID, 3, STATUS_NONE, INTERACT_SOLID,
        TEMPO_FLOWING, TREND_STEADY, 2);
    world.spell[SIM_SIDE_L].progress = 100u;
    duel_view_t view;
    duel_view_from_world(&world, &view);
    duel_flash_policy_t flash = {0};
    uint8_t last_kind[2] = {0, 0};
    EXPECT(!duel_flash_observe_view(&flash, last_kind, &view, 100u) &&
           DUEL_KIND_ELEMENT(last_kind[SIM_SIDE_L]) == ELEM_EMBER);
    world.spell[SIM_SIDE_L].active = 0;
    world.spell[SIM_SIDE_L].descriptor = 0;
    world.fx_seq = 1u;
    world.fx_kind = FX_IMPACT_R;
    duel_view_from_world(&world, &view);
    EXPECT(duel_flash_observe_view(&flash, last_kind, &view, 200u) &&
           flash.kind == FX_IMPACT_R &&
           flash.spell_kind == last_kind[SIM_SIDE_L] &&
           flash.duration_ms == DUEL_PRESENTATION_IMPACT_MS);
    /* The same fx_seq never re-arms. */
    EXPECT(!duel_flash_observe_view(&flash, last_kind, &view, 300u));
    CHECK(ok, "runtime_flash_observe_caches_style_and_arms_defender_flash");
}

static void test_runtime_sky_and_diplomacy(void) {
    bool ok = true;
    EXPECT(duel_sky_phase(0u) == DUEL_SKY_DAWN &&
              duel_sky_phase(149999u) == DUEL_SKY_DAWN &&
              duel_sky_phase(150000u) == DUEL_SKY_DAY &&
              duel_sky_phase(1349999u) == DUEL_SKY_DAY &&
              duel_sky_phase(1350000u) == DUEL_SKY_DUSK &&
              duel_sky_phase(1499999u) == DUEL_SKY_DUSK &&
              duel_sky_phase(1500000u) == DUEL_SKY_NIGHT &&
              duel_sky_phase(1799999u) == DUEL_SKY_NIGHT &&
              duel_sky_phase(1800000u) == DUEL_SKY_DAWN);
    sim_world_t world;
    sim_init(&world, SIMF_AUTHORITATIVE, 0);
    duel_snapshot_t master;
    test_encode_snapshot(&world, 7u, 500u, &master);
    duel_snapshot_set_civic(&master, 0u,
        DUEL_SECONDARY_SKY_PACK(0u, DUEL_SKY_NIGHT), 0u, 0u);
    duel_rx_state_t rx = {0};
    EXPECT(duel_decode_valid(&master) && duel_rx_accept(&rx, &master, false) &&
          DUEL_SECONDARY_SKY_PHASE(rx.last.secondary) == DUEL_SKY_NIGHT);
    /* A stale half may be in its own dawn; stale adoption takes the live
     * master's current phase even when a collided session has a lower seq. */
    duel_snapshot_set_civic(&master, 0u,
        DUEL_SECONDARY_SKY_PACK(0u, DUEL_SKY_DAY), 0u, 0u);
    master.seq = 1u;
    master.crc = duel_crc8(&master, offsetof(duel_snapshot_t, crc));
    EXPECT(duel_sky_phase(0u) == DUEL_SKY_DAWN &&
          duel_rx_accept(&rx, &master, true) &&
          DUEL_SECONDARY_SKY_PHASE(rx.last.secondary) == DUEL_SKY_DAY);
    duel_diplomacy_t dip;
    duel_diplomacy_init(&dip);
    EXPECT(!duel_diplomacy_update(&dip, LIFE_ACTIVE, LIFE_ACTIVE));
    EXPECT(duel_diplomacy_update(&dip, LIFE_ACTIVE, LIFE_COLLAPSE) && dip.balance == 1);
    EXPECT(!duel_diplomacy_update(&dip, LIFE_ACTIVE, LIFE_DOWNED) && dip.balance == 1);
    duel_diplomacy_update(&dip, LIFE_ACTIVE, LIFE_ACTIVE);
    for (int i = 0; i < 5; i++) {
        duel_diplomacy_update(&dip, LIFE_ACTIVE, LIFE_COLLAPSE);
        duel_diplomacy_update(&dip, LIFE_ACTIVE, LIFE_ACTIVE);
    }
    EXPECT(dip.balance == 3 && duel_diplomacy_target(&dip) == DUEL_DIPLOMACY_LEFT_ADVANTAGE);
    duel_diplomacy_init(&dip);
    EXPECT(dip.balance == 0 && duel_diplomacy_target(&dip) == DUEL_DIPLOMACY_BALANCED);
    CHECK(ok, "runtime_sky_boundaries_wrap_and_diplomacy_ko_edge_saturation_reset");
}

static void test_observatory_sky_and_suppression(void) {
    sim_world_t world;
    sim_init(&world, SIMF_AUTHORITATIVE, 0);
    duel_render_t base = {0};
    duel_render_from_world(&base, &world);
    base.seed = 0x5au;
    base.civic_phase = 19u;
    base.civic = DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_SPECIAL,
                                 DUEL_CIVIC_MODE_QUIET, 0);
    base.alert = DUEL_HOST_ALERT_PACK(DUEL_HOST_CATEGORY_COMMUNICATION,
                                      DUEL_HOST_PRIORITY_CRITICAL, 2);

    duel_fb_t clean, disposable;
    duel_fb_clear(&clean);
    wiz_draw_scene(&clean, &base, true, 7u, false);
    duel_render_t noisy = base;
    noisy.shared_pres = (uint8_t)(DUEL_VISITOR_PACK(DUEL_CIVIC_COURIER_BEACON, 0,
        DUEL_CIVIC_VISIT_WAITING) | DUEL_VISITOR_DENSITY_PACK(DUEL_CIVIC_DENSITY_MANY));
    noisy.revision = DUEL_EVENT_PACK(DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER,
        DUEL_CIVIC_EVENT_PHASE_ACTIVE, DUEL_CIVIC_EVENT_TARGET_LEFT);
    noisy.local_ambience = INCANTATION_AMBIENCE_PACK(true, TEMPO_FRANTIC,
                                                     TREND_IRREGULAR);
    duel_fb_clear(&disposable);
    wiz_draw_scene(&disposable, &noisy, true, 7u, false);
    bool ok = true;
    EXPECT(memcmp(clean.bits, disposable.bits, sizeof clean.bits) == 0);

    duel_fb_t phase_fb[4];
    for (uint8_t phase = DUEL_SKY_DAWN; phase <= DUEL_SKY_NIGHT; phase++) {
        duel_render_t sky = base;
        sky.secondary = DUEL_SECONDARY_SKY_PACK(0, phase);
        duel_fb_clear(&phase_fb[phase]);
        wiz_draw_scene(&phase_fb[phase], &sky, true, 7u, false);
        if (phase)
            EXPECT(memcmp(phase_fb[phase - 1].bits, phase_fb[phase].bits,
                         sizeof phase_fb[phase].bits) != 0);
    }
    /* M15 contract: the sky may repaint the upper band (celestial arc, tower
     * window lighting), but the deck, the room interior, and everything
     * below — through the stone course to the canvas bottom — must be
     * bit-identical across phases. */
    for (int y = DUEL_DECK_Y0; y < DUEL_CANVAS_H; y++) {
        for (int x = 0; x < DUEL_CANVAS_W; x++)
            EXPECT(duel_fb_get(&phase_fb[DUEL_SKY_DAWN], x, y) ==
                  duel_fb_get(&phase_fb[DUEL_SKY_NIGHT], x, y));
    }
    CHECK(ok, "observatory_distinct_sky_protected_regions_and_disposable_ambience_suppression");
}

static bool rgb_is(duel_rgb_t color, uint8_t r, uint8_t g, uint8_t b) {
    return color.r == r && color.g == g && color.b == b;
}

static void test_rgb_world_surface_policy(void) {
    duel_rgb_world_t world = {.display_phase = DUEL_DISPLAY_ACTIVE};
    bool ok = true;
    EXPECT(rgb_is(duel_rgb_policy(&world, DUEL_RGB_LED_UNDERGLOW, true), 6, 0, 18) &&
              rgb_is(duel_rgb_policy(&world, DUEL_RGB_LED_UNDERGLOW, false), 18, 6, 0) &&
              rgb_is(duel_rgb_policy(&world, DUEL_RGB_LED_KEYLIGHT, true), 0, 0, 0));
    world.observatory = true;
    EXPECT(rgb_is(duel_rgb_policy(&world, DUEL_RGB_LED_UNDERGLOW, true), 4, 0, 20));
    world.prepared[SIM_SIDE_L] = true;
    for (uint8_t element = ELEM_FORCE; element <= ELEM_VOID; element++) {
        static const duel_rgb_t expected[4] = {
            {12,12,16}, {24,3,0}, {0,10,24}, {8,0,20},
        };
        world.prepared_element[SIM_SIDE_L] = element;
        duel_rgb_t color = duel_rgb_policy(&world, DUEL_RGB_LED_KEYLIGHT, true);
        EXPECT(rgb_is(color, expected[element].r, expected[element].g, expected[element].b));
        EXPECT(rgb_is(duel_rgb_policy(&world, DUEL_RGB_LED_KEYLIGHT, false), 0, 0, 0));
    }
    world.flash_active = true; world.flash_kind = FX_WARD_SHATTER_L;
    EXPECT(rgb_is(duel_rgb_policy(&world, DUEL_RGB_LED_KEYLIGHT, true), 20, 20, 32) &&
          !rgb_is(duel_rgb_policy(&world, DUEL_RGB_LED_KEYLIGHT, false), 20, 20, 32));
    world.flash_kind = FX_IMPACT_R;
    EXPECT(rgb_is(duel_rgb_policy(&world, DUEL_RGB_LED_UNDERGLOW, false), 32, 0, 0));
    world.stale = true;
    EXPECT(rgb_is(duel_rgb_policy(&world, DUEL_RGB_LED_UNDERGLOW, true), 0, 4, 10) &&
          rgb_is(duel_rgb_policy(&world, DUEL_RGB_LED_KEYLIGHT, true), 0, 0, 0));
    world.display_phase = DUEL_DISPLAY_DIM;
    EXPECT(rgb_is(duel_rgb_policy(&world, DUEL_RGB_LED_UNDERGLOW, true), 0, 1, 2));
    world.display_phase = DUEL_DISPLAY_SLEEP;
    EXPECT(rgb_is(duel_rgb_policy(&world, DUEL_RGB_LED_UNDERGLOW, true), 0, 0, 0));
    CHECK(ok, "rgb_world_surface_all_priorities_sides_elements_dim_sleep_and_stale");
}

static void test_live_ambience_classifier(void) {
    bool ok = true;
    static const struct { uint16_t sum; uint8_t count, first, last, min, max; } cases[] = {
        {12, 2, 6, 6, 6, 6}, {8, 2, 5, 3, 3, 5},
        {4, 2, 1, 3, 1, 3}, {2, 2, 1, 1, 1, 1},
        {8, 3, 2, 2, 1, 6},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        sim_incantation_t inc = {0};
        inc.key_count = 3; inc.row_hist[1] = 3; inc.hash = (uint32_t)(0x1234u + i);
        inc.gap_sum = cases[i].sum; inc.gap_count = cases[i].count;
        inc.first_gap = cases[i].first; inc.last_gap = cases[i].last;
        inc.gap_min = cases[i].min; inc.gap_max = cases[i].max;
        uint8_t live = incantation_tempo_trend(&inc);
        uint32_t desc = incantation_compile(&inc, 0, SIM_TEMPER_NEUTRAL);
        EXPECT(INCANTATION_AMBIENCE_TEMPO(live) == SPELL_DESC_TEMPO(desc) &&
              INCANTATION_AMBIENCE_TREND(live) == SPELL_DESC_TREND(desc));
        sim_wizard_t wizard = {.inc = inc, .inc_state = INC_COLLECTING};
        EXPECT(incantation_local_ambience(&wizard) == live);
        wizard.inc_state = INC_WINDUP; wizard.pending_desc = desc;
        EXPECT(INCANTATION_AMBIENCE_TEMPO(incantation_local_ambience(&wizard)) ==
              SPELL_DESC_TEMPO(desc));
        wizard.inc_state = INC_IDLE;
        EXPECT(incantation_local_ambience(&wizard) == 0u);
    }
    sim_wizard_t halves[2] = {{0}};
    halves[0].inc_state = halves[1].inc_state = INC_COLLECTING;
    halves[0].inc.gap_count = halves[1].inc.gap_count = 2;
    halves[0].inc.gap_sum = 2; halves[1].inc.gap_sum = 12;
    EXPECT(INCANTATION_AMBIENCE_TEMPO(incantation_local_ambience(&halves[0])) == TEMPO_FRANTIC &&
          INCANTATION_AMBIENCE_TEMPO(incantation_local_ambience(&halves[1])) == TEMPO_DELIBERATE);
    CHECK(ok, "ambience_live_compiler_equivalence_launch_calm_and_per_half_independence");
}

static void test_diplomatic_weight_target_and_combat_independence(void) {
    unsigned balanced = 0, imbalanced = 0;
    bool ok = true, saw_left = false, saw_right = false, saw_balanced = false;
    sim_world_t world; sim_init(&world, SIMF_AUTHORITATIVE, 0);
    uint64_t before = incantation_bytes_hash(&world, sizeof world);
    for (unsigned seed = 0; seed < 256u; seed++) {
        for (uint8_t cycle = 0; cycle < 16u; cycle++) {
            uint8_t phase = (uint8_t)(cycle * 8u + 2u);
            civic_event_state_t zero = civic_event_derive((uint8_t)seed, phase, true, 0);
            civic_event_state_t left = civic_event_derive((uint8_t)seed, phase, true, 3);
            civic_event_state_t right = civic_event_derive((uint8_t)seed, phase, true, -3);
            balanced += (zero.id_target & 7u) == DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER;
            imbalanced += (left.id_target & 7u) == DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER;
            if ((zero.id_target & 7u) == DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER)
                saw_balanced |= ((zero.id_target >> 5) & 3u) == DUEL_CIVIC_EVENT_TARGET_SHARED;
            if ((left.id_target & 7u) == DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER)
                saw_left |= ((left.id_target >> 5) & 3u) == DUEL_CIVIC_EVENT_TARGET_LEFT;
            if ((right.id_target & 7u) == DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER)
                saw_right |= ((right.id_target >> 5) & 3u) == DUEL_CIVIC_EVENT_TARGET_RIGHT;
        }
    }
    EXPECT(imbalanced > balanced && saw_left && saw_right && saw_balanced &&
          incantation_bytes_hash(&world, sizeof world) == before);
    CHECK(ok, "diplomacy_weight_4_plus_balance_targeting_and_zero_combat_influence");
}

int main(void) {
    test_runtime_mailbox_policy();
    test_runtime_tx_policy();
    test_display_power_policy();
    test_runtime_input_sampling();
    test_runtime_tick_budget();
    test_runtime_slave_presenter();
    test_runtime_civic_shared_derive();
    test_runtime_flash_observe();
    test_runtime_presentation_policy();
    test_runtime_sky_and_diplomacy();
    test_observatory_sky_and_suppression();
    test_rgb_world_surface_policy();
    test_live_ambience_classifier();
    test_diplomatic_weight_target_and_combat_independence();
    test_layout_and_protocol();
    test_v11_repack_and_sky_subphase();
    test_residue_deposits_decay_and_transmutation();
    test_residue_transmutation_rows_and_wire();
    test_host_protocol_current_payload_and_ordering();
    test_view_validation();
    test_complexity_formula();
    test_magnitude_thresholds();
    test_compiler_determinism_and_gates();
    test_compiler_reachability();
    test_independent_accumulators_and_commit();
    test_forced_cap_and_rearm();
    test_release_and_prepared();
    test_windup_ignored_input_and_interruption();
    test_ward_capacity_semantics();
    test_regeneration_boundary_and_hit_reset();
    test_stance_entry_mechanics_and_exit();
    test_temper_drift_windup_and_ko_step();
    test_damage_heal_ward_and_status();
    test_status_dominance_and_effects();
    test_form_lifecycles();
    test_collision_precedence();
    test_mirror_form_collisions();
    test_productive_clashes();
    test_incantation_link_ordering();
    test_render_purity();
    test_render_interaction_combine_solid_parity();
    test_real_input_reachability_and_timing_buckets();
    test_prose_typing_ko_window();
    test_max_cast_aftermath_and_wire();
    test_fireball_room_resident_object_arc();
    test_reachable_complaint_and_ward_shatter();
    test_ground_chain_summon_and_trap();
    test_swarm_gather_launch_and_tempo_motion();
    test_bilateral_beam_and_aftermath_split_render();
    test_resident_occupation_derivation();
    test_resident_geometry_and_object_separation();
    test_civic_anchor_and_courier_matrix();
    test_rare_event_floor_phase_mode_target_matrix();
    test_aftermath_floor_kind_phase_half_matrix();
    test_health_grid_geometry_and_lifecycles();
    test_local_layer_attunement();
    test_diegetic_scry_instruments();
    test_floor_occupations_and_transitions();
    test_gap_cue_families_temporal_mirrors();
    test_all_forms_bilateral_mirror();
    test_aftermath_split_loss_and_reconnect();
    if (failures) { printf("%d mechanics test(s) failed\n", failures); return 1; }
    printf("all mechanics tests passed\n");
    return 0;
}
