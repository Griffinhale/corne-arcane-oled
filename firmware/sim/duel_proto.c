#include <string.h>

#include "duel_proto.h"
#include "duel_civic.h"
#include "duel_display.h"
#include "duel_host.h"

// CRC-8, polynomial 0x07, no table — 31 bytes per 32-byte report doesn't
// warrant one.
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

/* Scattered-bit writer shared by the encoder (no CRC yet) and the public
 * setter (which recomputes it). */
static void snapshot_write_residue(duel_snapshot_t *p, uint8_t zone,
                                   uint8_t element, uint8_t intensity) {
    element &= 3u;
    intensity &= 3u;
    switch (zone & 3u) {
        case DUEL_RESIDUE_DOORSTEP_L:
            p->residue = (uint8_t)((p->residue & 0xF0u) | element | (intensity << 2));
            break;
        case DUEL_RESIDUE_MID_L:
            p->residue = (uint8_t)((p->residue & 0x0Fu) | (element << 4) | (intensity << 6));
            break;
        case DUEL_RESIDUE_MID_R:
            p->flags = (uint8_t)((p->flags & 0x87u) | (element << 3) | (intensity << 5));
            break;
        default:
            p->civic     = (uint8_t)((p->civic & 0x3Fu) | (element << 6));
            p->flags     = (uint8_t)((p->flags & 0x7Fu) | ((intensity & 1u) << 7));
            p->secondary = (uint8_t)((p->secondary & 0x7Fu) | ((intensity >> 1) << 7));
            break;
    }
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
    /* v11 seq is a wrapping byte (ample for stale detection at snapshot
     * cadence); callers keep their wider counters and we truncate. The
     * memset above is the v11 stance prefill (Track B); residue is live
     * (Track A) and filled from the world below. */
    out->seq     = (uint8_t)seq;
    duel_view_from_world(w, &out->view);
    out->external = external;
    out->alert    = alert;
    uint8_t packed[2];
    duel_residue_pack(w, packed);
    out->residue = packed[0];
    snapshot_write_residue(out, DUEL_RESIDUE_MID_R,
                           packed[1] & 3u, (packed[1] >> 2) & 3u);
    snapshot_write_residue(out, DUEL_RESIDUE_DOORSTEP_R,
                           (packed[1] >> 4) & 3u, (packed[1] >> 6) & 3u);
    /* Prefill so offline/test packets carry the world's aftermath; the master
     * glue overwrites these (plus civic/secondary) via duel_snapshot_set_civic. */
    out->shared_pres = incantation_aftermath_shared(w);
    out->revision = incantation_aftermath_revision(w);
    out->crc     = duel_crc8(out, offsetof(duel_snapshot_t, crc));
}

void duel_snapshot_set_civic(duel_snapshot_t *p, uint8_t civic, uint8_t secondary,
                             uint8_t shared_pres, uint8_t revision) {
    /* civic bits 6-7 and secondary bit 7 belong to residue zone 3 (Track A,
     * written by the encoder): mask them out of the incoming semantics and
     * preserve what the encoder wrote, so callers need no ordering dance. */
    p->civic       = (uint8_t)((civic & (uint8_t)~DUEL_CIVIC_RESIDUE_BITS) |
                               (p->civic & DUEL_CIVIC_RESIDUE_BITS));
    p->secondary   = (uint8_t)((secondary & (uint8_t)~DUEL_SECONDARY_RESIDUE_BITS) |
                               (p->secondary & DUEL_SECONDARY_RESIDUE_BITS));
    p->shared_pres = shared_pres;
    p->revision    = revision;
    p->crc         = duel_crc8(p, offsetof(duel_snapshot_t, crc));
}

uint8_t duel_snapshot_residue_element(const duel_snapshot_t *p, uint8_t zone) {
    switch (zone & 3u) {
        case DUEL_RESIDUE_DOORSTEP_L: return (uint8_t)(p->residue & 3u);
        case DUEL_RESIDUE_MID_L:      return (uint8_t)((p->residue >> 4) & 3u);
        case DUEL_RESIDUE_MID_R:      return (uint8_t)((p->flags >> 3) & 3u);
        default:                      return (uint8_t)((p->civic >> 6) & 3u);
    }
}

uint8_t duel_snapshot_residue_intensity(const duel_snapshot_t *p, uint8_t zone) {
    switch (zone & 3u) {
        case DUEL_RESIDUE_DOORSTEP_L: return (uint8_t)((p->residue >> 2) & 3u);
        case DUEL_RESIDUE_MID_L:      return (uint8_t)((p->residue >> 6) & 3u);
        case DUEL_RESIDUE_MID_R:      return (uint8_t)((p->flags >> 5) & 3u);
        default: /* straddles: flags.7 is the low bit, secondary.7 the high */
            return (uint8_t)(((p->flags >> 7) & 1u) | (((p->secondary >> 7) & 1u) << 1));
    }
}

void duel_snapshot_residue_render(const duel_snapshot_t *p, uint8_t out[2]) {
    out[0] = p->residue;
    out[1] = (uint8_t)(duel_snapshot_residue_element(p, DUEL_RESIDUE_MID_R) |
                       (duel_snapshot_residue_intensity(p, DUEL_RESIDUE_MID_R) << 2) |
                       (duel_snapshot_residue_element(p, DUEL_RESIDUE_DOORSTEP_R) << 4) |
                       (duel_snapshot_residue_intensity(p, DUEL_RESIDUE_DOORSTEP_R) << 6));
}

bool duel_decode_valid(const duel_snapshot_t *p) {
    bool shared_valid;
    if (p->revision & INCANTATION_AFTERMATH_WIRE) {
        shared_valid = (p->revision & INCANTATION_AFTERMATH_REV_RESERVED) == 0u;
    } else {
        shared_valid = DUEL_VISITOR_KIND(p->shared_pres) < DUEL_CIVIC_COURIER_COUNT &&
                       DUEL_EVENT_ID(p->revision) < DUEL_CIVIC_EVENT_COUNT &&
                       DUEL_EVENT_TARGET(p->revision) <= DUEL_CIVIC_EVENT_TARGET_SHARED;
    }
    /* v11 has no reserved bits left; the range checks with teeth are the
     * display-phase bound, the activity enum, and the residue canonical
     * form (an empty zone must carry element 0). A v10 half fails the ver
     * check and takes the established stale-link presentation. */
    bool residue_canonical = true;
    for (uint8_t zone = 0; zone < DUEL_RESIDUE_ZONES; zone++)
        if (duel_snapshot_residue_intensity(p, zone) == 0u &&
            duel_snapshot_residue_element(p, zone) != 0u)
            residue_canonical = false;
    return p->magic == DUEL_MAGIC && p->ver == DUEL_VER &&
           DUEL_FLAGS_DISPLAY(p->flags) <= DUEL_DISPLAY_SLEEP &&
           DUEL_SECONDARY_ACTIVITY(p->secondary) <= DUEL_CIVIC_SECONDARY_CALENDAR &&
           residue_canonical &&
           shared_valid &&
           duel_view_valid(&p->view) &&
           p->crc == duel_crc8(p, offsetof(duel_snapshot_t, crc));
}

_Static_assert((int)DUEL_RESIDUE_ZONES == (int)SIM_RESIDUE_ZONES &&
               (int)DUEL_RESIDUE_DOORSTEP_L == (int)SIM_RESIDUE_DOORSTEP_L &&
               (int)DUEL_RESIDUE_MID_L == (int)SIM_RESIDUE_MID_L &&
               (int)DUEL_RESIDUE_MID_R == (int)SIM_RESIDUE_MID_R &&
               (int)DUEL_RESIDUE_DOORSTEP_R == (int)SIM_RESIDUE_DOORSTEP_R,
               "wire and sim residue zone enums must agree");

bool duel_rx_accept(duel_rx_state_t *rx, const duel_snapshot_t *p, bool link_was_stale) {
    bool accept;
    if (!rx->have_any || link_was_stale) {
        accept = true; // fresh boot, or the link was dead: adopt whatever is live
    } else if (p->session != rx->last.session) {
        accept = true; // new master session (serial can't reorder across a reboot)
    } else {
        accept = (int8_t)(p->seq - rx->last.seq) > 0; // wrap-safe; stale/dup never win
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
