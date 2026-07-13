#include <string.h>

#include "duel_proto.h"

// CRC-8, polynomial 0x07, no table — 29 bytes per packet doesn't warrant one.
uint8_t duel_crc8(const void *data, size_t len) {
    const uint8_t *p = data;
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

void duel_encode(const sim_world_t *w, uint8_t session, uint16_t seq, duel_snapshot_t *out) {
    duel_encode_external(w, session, seq, 0, out);
}

void duel_encode_external(const sim_world_t *w, uint8_t session, uint16_t seq,
                          uint8_t external, duel_snapshot_t *out) {
    duel_encode_external_alert(w, session, seq, external, 0, out);
}

void duel_encode_external_alert(const sim_world_t *w, uint8_t session, uint16_t seq,
                                uint8_t external, uint8_t alert,
                                duel_snapshot_t *out) {
    duel_encode_external_alert_display(w, session, seq, external, alert, 0, out);
}

void duel_encode_external_alert_display(const sim_world_t *w, uint8_t session,
                                        uint16_t seq, uint8_t external,
                                        uint8_t alert, uint8_t display_phase,
                                        duel_snapshot_t *out) {
    memset(out, 0, sizeof *out);
    out->magic   = DUEL_MAGIC;
    out->ver     = DUEL_VER;
    out->session = session;
    out->flags   = DUEL_FLAGS_WORLD_VALID | DUEL_FLAGS_DISPLAY_PACK(display_phase);
    out->seq     = seq;
    out->tick16  = (uint16_t)w->tick;
    for (int s = 0; s < 2; s++) {
        out->pose[s]      = (uint8_t)((w->wiz[s].pose & 0x03) | ((w->wiz[s].pose_ticks & 0x3F) << 2));
        out->hp[s]        = w->wiz[s].hp;
        out->shield[s]    = w->wiz[s].shield_ticks;
        out->spell_pos[s] = w->spell[s].pos;
        out->spell_kind[s] = w->spell[s].kind;
        if (w->spell[s].active) out->spell_state |= DUEL_SPELLSTATE_ACTIVE(s);
        if (w->spell[s].dir < 0) out->spell_state |= DUEL_SPELLSTATE_NEG(s);
        out->life[s]       = DUEL_LIFE_PACK(w->wiz[s].life, w->wiz[s].variant);
        out->life_ticks[s] = w->wiz[s].life_ticks;
        out->charge[s]     = DUEL_CHARGE_PACK(w->wiz[s].cast_windup, w->wiz[s].cast_tier);
    }
    out->fx_seq  = w->fx_seq;
    out->fx_kind = w->fx_kind;
    out->scry    = DUEL_SCRY_PACK(scry_is_open(w), w->scry.scene);
    out->external = external;
    out->alert    = alert;
    out->crc     = duel_crc8(out, offsetof(duel_snapshot_t, crc));
}

bool duel_decode_valid(const duel_snapshot_t *p) {
    return p->magic == DUEL_MAGIC && p->ver == DUEL_VER &&
           p->crc == duel_crc8(p, offsetof(duel_snapshot_t, crc));
}

void duel_decode_world(const duel_snapshot_t *p, sim_world_t *out) {
    memset(out, 0, sizeof *out);
    out->tick = p->tick16;
    for (int s = 0; s < 2; s++) {
        out->wiz[s].pose         = p->pose[s] & 0x03;
        out->wiz[s].pose_ticks   = (uint8_t)(p->pose[s] >> 2);
        out->wiz[s].hp           = p->hp[s];
        out->wiz[s].shield_ticks = p->shield[s];
        out->spell[s].pos        = p->spell_pos[s];
        out->spell[s].active     = (p->spell_state & DUEL_SPELLSTATE_ACTIVE(s)) != 0;
        out->spell[s].dir        = (p->spell_state & DUEL_SPELLSTATE_NEG(s)) ? -4 : 4;
        out->spell[s].kind       = p->spell_kind[s];
        out->wiz[s].life         = DUEL_LIFE_STATE(p->life[s]);
        out->wiz[s].variant      = DUEL_LIFE_VARIANT(p->life[s]);
        out->wiz[s].life_ticks   = p->life_ticks[s];
        out->wiz[s].cast_windup  = DUEL_CHARGE_WINDUP(p->charge[s]);
        out->wiz[s].cast_tier    = DUEL_CHARGE_TIER(p->charge[s]);
        // regen_ticks stays 0: decoded worlds are render-only and never tick
        // lifecycle/regen (no authority flag).
    }
    out->fx_seq  = p->fx_seq;
    out->fx_kind = p->fx_kind;
    // Overlay is a render-only outcome on the slave: land in ACTIVE/IDLE so
    // scry_is_open() matches the master's authoritative view. The PENDING dwell
    // never crosses the wire (the overlay isn't open yet), so timer stays 0.
    out->scry.state = DUEL_SCRY_OPEN(p->scry) ? SCRY_ACTIVE : SCRY_IDLE;
    out->scry.scene = DUEL_SCRY_SCENE(p->scry);
}

bool duel_rx_accept(duel_rx_state_t *rx, const duel_snapshot_t *p, bool link_was_stale) {
    bool accept;
    if (!rx->have_any || link_was_stale) {
        accept = true; // fresh boot, or the link was dead: adopt whatever is live
    } else if (p->session != rx->session) {
        accept = true; // new master session (serial can't reorder across a reboot)
    } else {
        accept = (int16_t)(p->seq - rx->last_seq) > 0; // wrap-safe; stale/dup never win
    }

    if (accept) {
        rx->have_any = true;
        rx->session  = p->session;
        rx->last_seq = p->seq;
        rx->last     = *p;
    } else {
        if (rx->stale_drops < 0xFFFF) rx->stale_drops++;
    }
    return accept;
}
