#include <string.h>

#include "duel_proto.h"

// CRC-8, polynomial 0x07, no table — 27 bytes per packet doesn't warrant one.
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
    duel_view_from_world(w, &out->view);
    out->external = external;
    out->alert    = alert;
    out->crc     = duel_crc8(out, offsetof(duel_snapshot_t, crc));
}

bool duel_decode_valid(const duel_snapshot_t *p) {
    return p->magic == DUEL_MAGIC && p->ver == DUEL_VER &&
           (p->flags & 0xF8u) == 0 && duel_view_valid(&p->view) &&
           p->crc == duel_crc8(p, offsetof(duel_snapshot_t, crc));
}

void duel_decode_world(const duel_snapshot_t *p, sim_world_t *out) {
    duel_view_to_render_world(&p->view, out);
}

bool duel_rx_accept(duel_rx_state_t *rx, const duel_snapshot_t *p, bool link_was_stale) {
    bool accept;
    if (!rx->have_any || link_was_stale) {
        accept = true; // fresh boot, or the link was dead: adopt whatever is live
    } else if (p->session != rx->last.session) {
        accept = true; // new master session (serial can't reorder across a reboot)
    } else {
        accept = (int16_t)(p->seq - rx->last.seq) > 0; // wrap-safe; stale/dup never win
    }

    if (accept) {
        rx->have_any = true;
        rx->last     = *p;
    } else {
#ifdef ARCANE_DIAGNOSTICS
        if (rx->stale_drops < 0xFFFF) rx->stale_drops++;
#endif
    }
    return accept;
}
