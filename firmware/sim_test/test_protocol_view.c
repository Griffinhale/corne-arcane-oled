#include "test_harness.h"

static void test_layout_and_protocol(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    duel_snapshot_t packet;
    duel_encode_external_alert_display(&w, 9, 0x1234, 0x55, 0x2a, 2, &packet);
    duel_snapshot_set_civic(&packet, 1, 2, 3, 4);
    bool ok = true;
    EXPECT(sizeof(duel_view_t) == 18 && sizeof(duel_snapshot_t) == 32 && DUEL_VER == 12 &&
           sizeof(duel_render_t) <= 48u && sizeof(sim_world_t) <= 56u + 1024u &&
           duel_decode_valid(&packet));
    duel_snapshot_t bad = packet;
    for (size_t i = 0; i < offsetof(duel_snapshot_t, crc); i++) {
        ((uint8_t *)&bad)[i] ^= 0x40u;
        EXPECT(!duel_decode_valid(&bad));
        bad = packet;
    }
    bad.signature_version = 0xABu;
    bad.crc = duel_crc8(&bad, offsetof(duel_snapshot_t, crc));
    EXPECT(!duel_decode_valid(&bad));
    /* Non-canonical residue (element set on an empty zone) and an
     * out-of-range display phase are split range checks with teeth. */
    bad = packet;
    bad.residue = 0x01u;
    bad.crc = duel_crc8(&bad, offsetof(duel_snapshot_t, crc));
    EXPECT(!duel_decode_valid(&bad));
    bad = packet;
    bad.flags |= DUEL_FLAGS_DISPLAY_PACK(3u);
    bad.crc = duel_crc8(&bad, offsetof(duel_snapshot_t, crc));
    EXPECT(!duel_decode_valid(&bad));
    bad = packet;
    bad.shared_pres = DUEL_VISITOR_PACK(7u, 0u, 0u);
    bad.revision = 0u;
    bad.crc = duel_crc8(&bad, offsetof(duel_snapshot_t, crc));
    EXPECT(!duel_decode_valid(&bad));
    bad = packet;
    bad.revision = DUEL_EVENT_PACK(DUEL_CIVIC_EVENT_WORK_BREAK, DUEL_CIVIC_EVENT_PHASE_ACTIVE, 3u);
    bad.crc = duel_crc8(&bad, offsetof(duel_snapshot_t, crc));
    EXPECT(!duel_decode_valid(&bad));
    bad = packet;
    bad.revision = INCANTATION_AFTERMATH_WIRE | 0x70u;
    bad.crc = duel_crc8(&bad, offsetof(duel_snapshot_t, crc));
    EXPECT(!duel_decode_valid(&bad));
    CHECK(ok, "incantation_v12_exact_size_crc_and_version_rejection");
}

static void test_v12_repack_and_sky_subphase(void) {
    bool ok = true;
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    duel_snapshot_t p;
    w.fx_seq = 0x37u;                      /* only the low nibble reaches the wire */
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
                       duel_snapshot_residue_intensity(&p, zone) == inten && duel_decode_valid(&p));
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
           (p.flags & DUEL_FLAGS_WORLD_VALID) != 0u && duel_decode_valid(&p));

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
    EXPECT(duel_flash_observe_view(&flash, last_kind, &fv, 0u, 100u));
    w.fx_seq = 16u; /* nibble wraps to 0 */
    duel_view_from_world(&w, &fv);
    EXPECT(duel_flash_observe_view(&flash, last_kind, &fv, 0u, 200u));
    fv.fx_stance = VIEW_FX_PACK(VIEW_FX_SEQ(fv.fx_stance), DUEL_STANCE_MEDITATE, DUEL_STANCE_NONE);
    EXPECT(!duel_flash_observe_view(&flash, last_kind, &fv, 0u, 300u));

    /* v11 (and a future v13) signature are rejected outright: a mixed-revision
     * pair takes the established stale-link presentation. */
    duel_snapshot_t old = p;
    old.signature_version = 0xABu;
    old.crc = duel_crc8(&old, offsetof(duel_snapshot_t, crc));
    EXPECT(!duel_decode_valid(&old));
    old.signature_version = 0xADu;
    old.crc = duel_crc8(&old, offsetof(duel_snapshot_t, crc));
    EXPECT(!duel_decode_valid(&old));

    /* Sky sub-phase: exact quarter boundaries inside every phase, wrap-safe
     * across the cycle. Quarters are 37.5 s in dawn/dusk, 300 s in day,
     * 75 s at night. */
    EXPECT(duel_sky_subphase(0u) == 0u && duel_sky_subphase(37499u) == 0u &&
           duel_sky_subphase(37500u) == 1u && duel_sky_subphase(149999u) == 3u &&
           duel_sky_subphase(150000u) == 0u && /* day begins */
           duel_sky_subphase(449999u) == 0u && duel_sky_subphase(450000u) == 1u &&
           duel_sky_subphase(1349999u) == 3u &&
           duel_sky_subphase(1350000u) == 0u && /* dusk begins */
           duel_sky_subphase(1387500u) == 1u && duel_sky_subphase(1499999u) == 3u &&
           duel_sky_subphase(1500000u) == 0u && /* night begins */
           duel_sky_subphase(1575000u) == 1u && duel_sky_subphase(1799999u) == 3u &&
           duel_sky_subphase(1800000u) == 0u); /* wraps to dawn */
    CHECK(ok, "v12_repack_residue_stance_fx_nibble_subphase_boundaries_and_version_gate");
}

/* Battlefield residue deposits (impact, fizzle, ember/frost
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
    uint32_t frost_mid =
        SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FROST, PAY_DAMAGE, TRAJ_MID, 1u, STATUS_NONE,
                        INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, 0u);
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
    EXPECT(w.fx_kind == FX_FIZZLE_R && w.residue[SIM_RESIDUE_DOORSTEP_R].element == ELEM_FROST &&
           w.residue[SIM_RESIDUE_DOORSTEP_R].intensity == 1u);

    /* Deposit cap: a non-reacting element deposited onto a full zone
     * overwrites the element and saturates at 3. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.residue[SIM_RESIDUE_DOORSTEP_R] = (sim_residue_t){ELEM_FORCE, 3u, SIM_RESIDUE_DECAY_UNITS};
    land_spell(&w, SIM_SIDE_L, frost_mid); /* frost x force: no reaction */
    EXPECT(w.residue[SIM_RESIDUE_DOORSTEP_R].element == ELEM_FROST &&
           w.residue[SIM_RESIDUE_DOORSTEP_R].intensity == 3u);

    /* Ember x frost clash: both mid zones take their caster's element, and
     * the fire aftermath hook then scorches (and claims) both doorsteps on
     * top of the 1-damage pulse stains. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t ember_mid =
        SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_EMBER, PAY_DAMAGE, TRAJ_MID, 2u, STATUS_NONE,
                        INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, 0u);
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
           w.residue[SIM_RESIDUE_DOORSTEP_R].intensity == 2u && w.wiz[0].hp == SIM_MAX_HP - 1u &&
           w.wiz[1].hp == SIM_MAX_HP - 1u);

    /* Repair aftermath hook: a same-element combine clash starts AFTER_REPAIR
     * on both sides, sweeping one step off each doorstep. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.residue[SIM_RESIDUE_DOORSTEP_L] = (sim_residue_t){ELEM_FORCE, 2u, SIM_RESIDUE_DECAY_UNITS};
    w.residue[SIM_RESIDUE_DOORSTEP_R] = (sim_residue_t){ELEM_FROST, 1u, SIM_RESIDUE_DECAY_UNITS};
    install_spell(&w, SIM_SIDE_L, ember_mid, 120u);
    install_spell(&w, SIM_SIDE_R, desc_set_magnitude_for_test(ember_mid, 1u), 130u);
    idle_step(&w);
    EXPECT(w.fx_kind == FX_COMBINE && w.residue[SIM_RESIDUE_DOORSTEP_L].element == ELEM_FORCE &&
           w.residue[SIM_RESIDUE_DOORSTEP_L].intensity == 1u &&
           w.residue[SIM_RESIDUE_DOORSTEP_R].intensity == 0u &&
           w.residue[SIM_RESIDUE_DOORSTEP_R].element == 0u); /* canonical */

    /* A matured singularity first leaves the in-flight slot, then its bounded
     * field lifetime ends in the same uncharged void scar. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t sing = SPELL_DESC_PACK(SPELL_SINGULARITY, ELEM_VOID, PAY_DAMAGE, TRAJ_MID, 2u,
                                    STATUS_NONE, INTERACT_ABSORB, TEMPO_FLOWING, TREND_STEADY, 0u);
    install_spell(&w, SIM_SIDE_L, sing, 0u);
    wait_ticks(&w, 30u);
    EXPECT(!w.spell[0].active && w.field[0].kind == FIELD_SINGULARITY && w.field[0].timer == 36u);
    wait_ticks(&w, 36u);
    EXPECT(w.field[0].kind == FIELD_NONE && w.fx_kind == FX_COLLAPSE &&
           w.residue[SIM_RESIDUE_MID_L].element == ELEM_VOID &&
           w.residue[SIM_RESIDUE_MID_L].intensity == 2u);

    /* Decay boundary: one intensity step per 225 prescaled units (1125
     * ticks); the final step clears the zone to canonical empty. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.residue[SIM_RESIDUE_MID_R] = (sim_residue_t){ELEM_EMBER, 2u, SIM_RESIDUE_DECAY_UNITS};
    wait_ticks(&w, 1115u);
    EXPECT(w.residue[SIM_RESIDUE_MID_R].intensity == 2u);
    wait_ticks(&w, 10u);
    EXPECT(w.residue[SIM_RESIDUE_MID_R].intensity == 1u &&
           w.residue[SIM_RESIDUE_MID_R].element == ELEM_EMBER &&
           w.residue[SIM_RESIDUE_MID_R].decay == SIM_RESIDUE_DECAY_UNITS);
    w.residue[SIM_RESIDUE_MID_R].decay = 1u;
    wait_ticks(&w, SIM_RESIDUE_DECAY_PRESCALE);
    EXPECT(w.residue[SIM_RESIDUE_MID_R].intensity == 0u &&
           w.residue[SIM_RESIDUE_MID_R].element == 0u && w.residue[SIM_RESIDUE_MID_R].decay == 0u);
    CHECK(ok, "residue_deposit_table_aftermath_hooks_cap_and_decay_boundary");
}

static void test_residue_transmutation_rows_and_wire(void) {
    bool ok = true;
    sim_world_t w;

    /* Steam transmutation now creates a persistent cloud directly. The
     * original carrier flies on, reaction spent, without an immediate pulse. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t ember_mid =
        SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_EMBER, PAY_DAMAGE, TRAJ_MID, 1u, STATUS_NONE,
                        INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, 0u);
    w.residue[SIM_RESIDUE_MID_R] = (sim_residue_t){ELEM_FROST, 2u, SIM_RESIDUE_DECAY_UNITS};
    install_spell(&w, SIM_SIDE_L, ember_mid, 170u);
    idle_step(&w);
    EXPECT(w.fx_kind == FX_RESIDUE && w.spell[0].active &&
           (w.spell[0].resolved & SPELL_RESOLVED_REACTED) != 0u &&
           w.residue[SIM_RESIDUE_MID_R].intensity == 0u &&
           w.residue[SIM_RESIDUE_MID_R].element == 0u && w.wiz[1].hp == SIM_MAX_HP &&
           w.field[0].kind == FIELD_STEAM && w.field[0].zone == SIM_RESIDUE_MID_R);

    /* Void absorb: zone -1, spell magnitude +1. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t void_low =
        SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_VOID, PAY_DAMAGE, TRAJ_LOW, 2u, STATUS_NONE,
                        INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, 0u);
    w.residue[SIM_RESIDUE_MID_L] = (sim_residue_t){ELEM_EMBER, 2u, SIM_RESIDUE_DECAY_UNITS};
    install_spell(&w, SIM_SIDE_L, void_low, 60u);
    idle_step(&w);
    EXPECT(SPELL_DESC_MAGNITUDE(w.spell[0].descriptor) == 3u &&
           w.residue[SIM_RESIDUE_MID_L].intensity == 1u &&
           w.residue[SIM_RESIDUE_MID_L].element == ELEM_EMBER &&
           (w.spell[0].resolved & SPELL_RESOLVED_REACTED) != 0u);
    /* Once per lifetime: recharging the zone provokes nothing further. */
    w.residue[SIM_RESIDUE_MID_L] = (sim_residue_t){ELEM_EMBER, 2u, SIM_RESIDUE_DECAY_UNITS};
    idle_step(&w);
    EXPECT(SPELL_DESC_MAGNITUDE(w.spell[0].descriptor) == 3u &&
           w.residue[SIM_RESIDUE_MID_L].intensity == 2u);

    /* Force x force rubble: zone -1, trajectory bumps one lane. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t force_low =
        SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE, TRAJ_LOW, 2u, STATUS_NONE,
                        INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, 0u);
    w.residue[SIM_RESIDUE_MID_L] = (sim_residue_t){ELEM_FORCE, 3u, SIM_RESIDUE_DECAY_UNITS};
    install_spell(&w, SIM_SIDE_L, force_low, 60u);
    idle_step(&w);
    EXPECT(SPELL_DESC_TRAJECTORY(w.spell[0].descriptor) == TRAJ_MID &&
           SPELL_DESC_MAGNITUDE(w.spell[0].descriptor) == 2u &&
           w.residue[SIM_RESIDUE_MID_L].intensity == 2u);

    /* Same-element feed: zone -1, magnitude +1 — and an unmatched pair
     * (frost over force) neither reacts nor spends the flag. */
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    uint32_t frost_low =
        SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FROST, PAY_DAMAGE, TRAJ_LOW, 1u, STATUS_NONE,
                        INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, 0u);
    w.residue[SIM_RESIDUE_MID_L] = (sim_residue_t){ELEM_FORCE, 2u, SIM_RESIDUE_DECAY_UNITS};
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
    w.residue[SIM_RESIDUE_DOORSTEP_L] = (sim_residue_t){ELEM_EMBER, 1u, 10u};
    w.residue[SIM_RESIDUE_MID_L] = (sim_residue_t){ELEM_FROST, 2u, 10u};
    w.residue[SIM_RESIDUE_MID_R] = (sim_residue_t){ELEM_FORCE, 3u, 10u};
    w.residue[SIM_RESIDUE_DOORSTEP_R] = (sim_residue_t){ELEM_VOID, 3u, 10u};
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
    duel_snapshot_set_civic(&p, DUEL_CIVIC_PACK(2u, 1u, 3u) | 0xC0u, (uint8_t)(sec | 0x80u), 0u,
                            0u);
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
    CHECK(ok, "residue_transmutation_rows_once_per_spell_and_v12_wire_paths");
}

static void test_host_protocol_current_payload_and_ordering(void) {
    duel_host_packet_t hello;
    test_build_host_packet(DUEL_HOST_MSG_HELLO, 0x11223344u, 0, DUEL_HOST_SCENE_ARCHIVE, 2,
                           DUEL_HOST_CATEGORY_COMMUNICATION, DUEL_HOST_PRIORITY_NORMAL, 3, false,
                           DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_RESEARCH, DUEL_CIVIC_MODE_NORMAL,
                                           DUEL_CIVIC_INTENSITY_ACTIVE),
                           DUEL_SECONDARY_PACK(DUEL_CIVIC_SECONDARY_MEDIA), &hello);
    duel_host_state_t state = {0};
    bool ok = true;
    EXPECT(sizeof hello == DUEL_HOST_REPORT_SIZE && hello.version == DUEL_HOST_VERSION &&
           hello.payload_len == DUEL_HOST_PAYLOAD_LEN && duel_host_packet_valid(&hello) &&
           duel_host_accept(&state, &hello) &&
           DUEL_HOST_CONTEXT_SCENE(duel_host_context(&state)) == DUEL_HOST_SCENE_ARCHIVE &&
           DUEL_CIVIC_FLOOR(duel_host_civic(&state)) == DUEL_CIVIC_FLOOR_RESEARCH);

    duel_host_packet_t heartbeat;
    test_build_host_packet(DUEL_HOST_MSG_HEARTBEAT, 0x11223344u, 1, DUEL_HOST_SCENE_FOCUS, 0,
                           DUEL_HOST_CATEGORY_NONE, DUEL_HOST_PRIORITY_NONE, 0, false,
                           DUEL_CIVIC_PACK(DUEL_CIVIC_FLOOR_WORKSHOP, DUEL_CIVIC_MODE_QUIET,
                                           DUEL_CIVIC_INTENSITY_CALM),
                           DUEL_SECONDARY_PACK(DUEL_CIVIC_SECONDARY_NONE), &heartbeat);
    EXPECT(duel_host_accept(&state, &heartbeat));
    EXPECT(!duel_host_accept(&state, &heartbeat));

    duel_host_packet_t bad = heartbeat;
    bad.payload_len = 6;
    bad.crc = duel_crc8(&bad, offsetof(duel_host_packet_t, crc));
    EXPECT(!duel_host_packet_valid(&bad));
    bad = heartbeat;
    bad.version = 2; /* production Raw HID v2 is a strict reject */
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
    CHECK(ok, "host_v3_eight_byte_payload_v2_reject_malformed_stale_ordering_and_reserved_tail");
}

static void test_view_validation(void) {
    sim_world_t w;
    sim_init(&w, SIMF_AUTHORITATIVE, 0);
    w.wiz[0].ward_strength = 4;
    w.wiz[0].status = STATUS_FROZEN;
    w.wiz[0].status_intensity = 3;
    w.wiz[0].status_ticks = 125;
    w.spell[0].active = 1;
    w.spell[0].descriptor =
        SPELL_DESC_PACK(SPELL_BEAM, ELEM_FROST, PAY_HYBRID, TRAJ_AREA, 4, STATUS_FROZEN,
                        INTERACT_SOLID, TEMPO_RAPID, TREND_ACCELERATING, 2);
    w.spell[0].progress = 91;
    duel_view_t view;
    duel_view_from_world(&w, &view);
    bool ok = true;
    EXPECT(duel_view_valid(&view));
    duel_view_spell_t spell = duel_view_spell(&view, 0, 9u);
    duel_view_wizard_t wizard = duel_view_wizard(&view, 0);
    EXPECT(spell.active &&
           spell.descriptor == duel_spell_descriptor_expand(
                                   duel_spell_descriptor_compress(w.spell[0].descriptor), 9u, 0u) &&
           spell.progress == 91);
    EXPECT(wizard.hp == SIM_MAX_HP && wizard.ward_strength == 4 && wizard.status == STATUS_FROZEN);
    duel_view_t bad = view;
    bad.wizard[0][0] = (uint8_t)((bad.wizard[0][0] & 0xf0u) | 13u);
    EXPECT(!duel_view_valid(&bad));
    bad = view;
    bad.wizard[0][0] = (uint8_t)((bad.wizard[0][0] & 0x8fu) | (5u << 4));
    EXPECT(!duel_view_valid(&bad));
    bad = view;
    bad.outcome_overlay |= 0x80u;
    EXPECT(!duel_view_valid(&bad));
    bad = view;
    bad.spell[3] &= 0x0fu;
    bad.spell[4] = bad.spell[5] = 0u;
    bad.spell[6] = 1u;
    EXPECT(!duel_view_valid(&bad));
    bad = view;
    bad.spell[1] |= 0x70u; /* out-of-range compressed status encoding */
    EXPECT(!duel_view_valid(&bad));
    CHECK(ok, "incantation_view_roundtrip_and_reserved_validation");
}

static void test_v12_descriptor_compression_domain(void) {
    bool ok = true;
    bool domain_ok = true;
    for (uint8_t form = 0; form <= SPELL_CONJURE && domain_ok; form++)
        for (uint8_t element = 0; element < 4u && domain_ok; element++)
            for (uint8_t payload = 0; payload < 4u && domain_ok; payload++)
                for (uint8_t trajectory = 0; trajectory < 8u && domain_ok; trajectory++)
                    for (uint8_t magnitude = 1u; magnitude <= 4u && domain_ok; magnitude++)
                        for (uint8_t status = 0; status <= STATUS_MARKED && domain_ok; status++)
                            for (uint8_t tempo = 0; tempo < 4u && domain_ok; tempo++)
                                for (uint8_t trend = 0; trend < 4u && domain_ok; trend++) {
                                    uint32_t reference = SPELL_DESC_PACK(
                                        form, element, payload, trajectory, magnitude, status,
                                        INTERACT_SOLID, tempo, trend, 0u);
                                    uint32_t compressed = duel_spell_descriptor_compress(reference);
                                    for (uint8_t interaction = 0; interaction < 4u; interaction++)
                                        for (uint8_t variance = 0; variance < 4u; variance++) {
                                            uint32_t descriptor = SPELL_DESC_PACK(
                                                form, element, payload, trajectory, magnitude,
                                                status, interaction, tempo, trend, variance);
                                            if (duel_spell_descriptor_compress(descriptor) !=
                                                compressed)
                                                domain_ok = false;
                                        }
                                    for (uint8_t side = 0; side < 2u; side++) {
                                        uint8_t session = (uint8_t)(0x5au + side);
                                        uint32_t expanded =
                                            duel_spell_descriptor_expand(compressed, session, side);
                                        uint8_t expected_interaction =
                                            form == SPELL_SINGULARITY ? INTERACT_ABSORB
                                            : element == ELEM_VOID    ? INTERACT_PHASE
                                                                      : INTERACT_SOLID;
                                        uint8_t bytes[5] = {
                                            session,
                                            side,
                                            (uint8_t)compressed,
                                            (uint8_t)(compressed >> 8),
                                            (uint8_t)(compressed >> 16),
                                        };
                                        if (!SPELL_DESC_VALID(expanded) ||
                                            SPELL_DESC_FORM(expanded) != form ||
                                            SPELL_DESC_ELEMENT(expanded) != element ||
                                            SPELL_DESC_PAYLOAD(expanded) != payload ||
                                            SPELL_DESC_TRAJECTORY(expanded) != trajectory ||
                                            SPELL_DESC_MAGNITUDE(expanded) != magnitude ||
                                            SPELL_DESC_STATUS(expanded) != status ||
                                            SPELL_DESC_TEMPO(expanded) != tempo ||
                                            SPELL_DESC_TREND(expanded) != trend ||
                                            SPELL_DESC_INTERACTION(expanded) !=
                                                expected_interaction ||
                                            SPELL_DESC_VARIANCE(expanded) !=
                                                (duel_crc8(bytes, sizeof bytes) & 3u) ||
                                            duel_spell_descriptor_expand(compressed, session,
                                                                         side) != expanded)
                                            domain_ok = false;
                                    }
                                }
    EXPECT(domain_ok);
    EXPECT(duel_spell_descriptor_compress(0u) == 0u &&
           duel_spell_descriptor_expand(0u, 9u, SIM_SIDE_L) == 0u);
    CHECK(ok,
          "v12_exhaustive_descriptor_domain_preserves_observables_and_reconstructs_crc_variance");
}

static void test_v12_field_projection_and_reconnect(void) {
    static const uint16_t duration[] = {
        1u,
        SIM_FIELD_TRAP_TICKS,
        SIM_FIELD_SINGULARITY_TICKS,
        SIM_FIELD_STEAM_TICKS,
        SIM_FIELD_RUNE_TICKS,
        SIM_FIELD_FAMILIAR_TICKS,
        SIM_FIELD_WALL_TICKS,
        SIM_FIELD_VORTEX_TICKS,
    };
    bool ok = true;
    sim_world_t world;
    sim_init(&world, SIMF_AUTHORITATIVE, 0u);
    uint32_t descriptor =
        SPELL_DESC_PACK(SPELL_PROJECTILE, ELEM_FORCE, PAY_DAMAGE, TRAJ_MID, 2u, STATUS_NONE,
                        INTERACT_SOLID, TEMPO_FLOWING, TREND_STEADY, 0u);
    for (uint8_t kind = FIELD_TRAP; kind < FIELD_KIND_COUNT; kind++) {
        sim_field_t field = {
            .descriptor = descriptor,
            .timer = duration[kind],
            .kind = kind,
            .zone = (uint8_t)(kind & 3u),
            .owner = (uint8_t)(kind & 1u),
        };
        uint8_t projection = duel_field_projection(&field);
        EXPECT(DUEL_FIELD_KIND(projection) == kind && DUEL_FIELD_ZONE(projection) == field.zone &&
               DUEL_FIELD_AGE(projection) == 0u && DUEL_FIELD_OWNER(projection) == field.owner);
        field.timer = (uint16_t)(duration[kind] - (uint16_t)((duration[kind] + 3u) / 4u));
        EXPECT(DUEL_FIELD_AGE(duel_field_projection(&field)) == 1u);
        field.timer = 0u;
        EXPECT(DUEL_FIELD_AGE(duel_field_projection(&field)) == 3u);
    }
    sim_field_t inactive = {.descriptor = descriptor, .zone = 3u, .owner = 1u};
    EXPECT(duel_field_projection(&inactive) == 0u);

    world.field[0] = (sim_field_t){
        .descriptor = descriptor,
        .timer = SIM_FIELD_TRAP_TICKS,
        .kind = FIELD_TRAP,
        .zone = SIM_RESIDUE_MID_L,
        .owner = SIM_SIDE_L,
    };
    world.field[1] = (sim_field_t){
        .descriptor = descriptor,
        .timer = SIM_FIELD_VORTEX_TICKS / 2u,
        .kind = FIELD_VORTEX,
        .zone = SIM_RESIDUE_MID_R,
        .owner = SIM_SIDE_R,
    };
    duel_snapshot_t first;
    test_encode_snapshot(&world, 7u, 1u, &first);
    EXPECT(duel_decode_valid(&first) && DUEL_FIELD_KIND(first.field[0]) == FIELD_TRAP &&
           DUEL_FIELD_OWNER(first.field[0]) == SIM_SIDE_L &&
           DUEL_FIELD_KIND(first.field[1]) == FIELD_VORTEX &&
           DUEL_FIELD_OWNER(first.field[1]) == SIM_SIDE_R && DUEL_FIELD_AGE(first.field[1]) == 2u);
    duel_snapshot_t malformed = first;
    malformed.field[0] = 0x08u; /* kind zero with nonzero zone is non-canonical */
    malformed.crc = duel_crc8(&malformed, offsetof(duel_snapshot_t, crc));
    EXPECT(!duel_decode_valid(&malformed));

    world.field[0].kind = FIELD_RUNE;
    world.field[0].owner = SIM_SIDE_R;
    world.field[0].timer = SIM_FIELD_RUNE_TICKS;
    duel_snapshot_t later;
    test_encode_snapshot(&world, 7u, 2u, &later);
    duel_rx_state_t receiver = {0};
    EXPECT(duel_rx_accept(&receiver, &first, false) && duel_rx_accept(&receiver, &later, false) &&
           DUEL_FIELD_KIND(receiver.last.field[0]) == FIELD_RUNE &&
           DUEL_FIELD_OWNER(receiver.last.field[0]) == SIM_SIDE_R);

    world.aftermath[0].kind = AFTER_INSPECT;
    world.aftermath[0].ticks = SIM_AFTER_DEFAULT_TICKS;
    for (uint8_t flavor = AFTER_FLAVOR_BASE; flavor < AFTER_FLAVOR_COUNT; flavor++) {
        world.aftermath_flavor = flavor;
        test_encode_snapshot(&world, 7u, (uint16_t)(10u + flavor), &later);
        EXPECT(duel_decode_valid(&later) && INCANTATION_AFTERMATH_FLAVOR(later.revision) == flavor);
    }
    later.revision = (uint8_t)(INCANTATION_AFTERMATH_WIRE | (7u << 4));
    later.crc = duel_crc8(&later, offsetof(duel_snapshot_t, crc));
    EXPECT(!duel_decode_valid(&later));
    CHECK(ok, "v12_two_field_projection_age_owner_canonical_zero_flavor_and_reconnect_convergence");
}

void run_protocol_view_tests(void) {
    test_layout_and_protocol();
    test_v12_repack_and_sky_subphase();
    test_residue_deposits_decay_and_transmutation();
    test_residue_transmutation_rows_and_wire();
    test_host_protocol_current_payload_and_ordering();
    test_view_validation();
    test_v12_descriptor_compression_domain();
    test_v12_field_projection_and_reconnect();
}
