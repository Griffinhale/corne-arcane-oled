/*
 * duel_proto.h — split-link snapshot protocol (M3).
 *
 * The master encodes its authoritative world into a compact packet every few
 * ticks; the slave renders from the last accepted packet and can never roll
 * state backward. Hardware-agnostic (no QMK includes) so the host harness
 * replays loss/duplication/reordering with exactly the firmware's code.
 *
 * Wire format notes: 27 bytes packed, under QMK's 32-byte
 * RPC_M2S_BUFFER_SIZE. Both halves (and the test hosts we care about) are
 * little-endian, so the struct ships as raw bytes. The serial protocol only
 * checksums its own framing, hence our CRC over the payload.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "duel_view.h"

#define DUEL_MAGIC 0xA7
#define DUEL_VER   8

// Snapshot flags: bit0 world valid; bits1-2 synchronized display phase.
// The reserved v7 bits carry presentation policy without changing the packet
// size, version, simulation world, or compatibility with older v7 receivers.
#define DUEL_FLAGS_WORLD_VALID 0x01u
#define DUEL_FLAGS_DISPLAY_PACK(phase) ((uint8_t)(((phase) & 3u) << 1))
#define DUEL_FLAGS_DISPLAY(flags)      ((uint8_t)(((flags) >> 1) & 3u))

typedef struct __attribute__((packed)) {
    uint8_t  magic;        /* DUEL_MAGIC */
    uint8_t  ver;          /* DUEL_VER */
    uint8_t  session;      /* master boot nonce */
    uint8_t  flags;        /* bit0: world valid; rest reserved */
    uint16_t seq;          /* per-session, wraps */
    duel_view_t view;      /* canonical transport/render projection */
    uint8_t  external;     /* M8: absolute disposable host context; see duel_host.h */
    uint8_t  alert;        /* M10: packed category, priority, and age */
    uint8_t  crc;          /* duel_crc8 over the 26 preceding bytes */
} duel_snapshot_t;

_Static_assert(sizeof(duel_snapshot_t) == 27, "v8 snapshot must leave five RPC bytes free");

typedef struct __attribute__((packed)) {
    uint8_t  magic;
    uint8_t  version;
    uint16_t accepted_seq;
    uint16_t snapshot_age_ms;
    uint16_t peak_housekeeping_us;
    uint16_t peak_render_us;
    uint16_t queue_overflow;
    uint16_t missed_tick_resyncs;
    uint16_t stale_events;
} duel_split_diag_reply_t;

_Static_assert(sizeof(duel_split_diag_reply_t) == 16,
               "diagnostic split response must remain fixed at 16 bytes");

uint8_t duel_crc8(const void *data, size_t len);

// Encode the world into a wire packet (computes the CRC).
void duel_encode(const sim_world_t *w, uint8_t session, uint16_t seq, duel_snapshot_t *out);

// M8 host branch variant: `external` is a packed, disposable presentation
// summary. The ordinary encoder above always writes zero and remains the
// firmware-only/Vial path.
void duel_encode_external(const sim_world_t *w, uint8_t session, uint16_t seq,
                          uint8_t external, duel_snapshot_t *out);
void duel_encode_external_alert(const sim_world_t *w, uint8_t session, uint16_t seq,
                                uint8_t external, uint8_t alert,
                                duel_snapshot_t *out);
void duel_encode_external_alert_display(const sim_world_t *w, uint8_t session,
                                        uint16_t seq, uint8_t external,
                                        uint8_t alert, uint8_t display_phase,
                                        duel_snapshot_t *out);

// Magic/version/CRC check. A false result means: drop silently, the next
// packet lands within a couple of ticks.
bool duel_decode_valid(const duel_snapshot_t *p);

// Compatibility helper for simulator assertions. Runtime rendering consumes
// the embedded canonical view directly.
void duel_decode_world(const duel_snapshot_t *p, sim_world_t *out);

/* ---- slave-side acceptance ---------------------------------------------- */
typedef struct {
    bool            have_any;
    duel_snapshot_t last;        /* last accepted packet */
#ifdef ARCANE_DIAGNOSTICS
    uint16_t        stale_drops; /* rejected as stale/duplicate, saturating */
#endif
} duel_rx_state_t;

// Applies `p` (assumed already validated) if it should replace the current
// view; returns whether it did. `link_was_stale` is the caller's link-timeout
// judgement and forces adoption, guaranteeing convergence even if a rebooted
// master collides on the session nonce with a lower sequence number.
bool duel_rx_accept(duel_rx_state_t *rx, const duel_snapshot_t *p, bool link_was_stale);
