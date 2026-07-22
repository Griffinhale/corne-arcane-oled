/*
 * duel_proto.h — split-link snapshot protocol.
 *
 * The master encodes its authoritative world into a compact packet every few
 * ticks; the slave renders from the last accepted packet and can never roll
 * state backward. Hardware-agnostic (no QMK includes) so the host harness
 * replays loss/duplication/reordering with exactly the firmware's code.
 *
 * Wire format: v12 uses the complete 32-byte RPC_M2S_BUFFER_SIZE. Both halves
 * (and the test hosts we
 * care about) are little-endian, so the struct ships as raw bytes. The serial
 * protocol only checksums its own framing, hence our CRC over the payload.
 *
 * v12 combines the old magic/version bytes as 0xAC and compresses the two
 * active spell projections to seven bytes. The recovered bytes carry the two
 * global field projections while every trailing host/civic offset stays fixed.
 * Full byte/bit map: docs/protocol-ledger.md.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "duel_view.h"

#define DUEL_MAGIC             0xA7 /* diagnostics-only identity remains stable */
#define DUEL_VER               12
#define DUEL_SIGNATURE_VERSION 0xAC

// Snapshot flags: bit0 world valid; bits1-2 synchronized display phase;
// bits3-4 residue zone2 element; bits5-6 residue zone2 intensity; bit7
// residue zone3 intensity low bit. Each receiver rejects every other version.
#define DUEL_FLAGS_WORLD_VALID         0x01u
#define DUEL_FLAGS_DISPLAY_PACK(phase) ((uint8_t)(((phase) & 3u) << 1))
#define DUEL_FLAGS_DISPLAY(flags)      ((uint8_t)(((flags) >> 1) & 3u))

/* Battlefield residue zones on the duel u-axis, encoded in the snapshot (the
 * encoder fills them from sim_world_t.residue). Zones 0-1 pack into the
 * dedicated residue byte, zone 2 into flags, zone 3 across the
 * civic/flags/secondary spare bits. Mirrors the SIM_RESIDUE_* enum
 * (static-asserted in duel_proto.c). */
enum {
    DUEL_RESIDUE_DOORSTEP_L = 0,
    DUEL_RESIDUE_MID_L,
    DUEL_RESIDUE_MID_R,
    DUEL_RESIDUE_DOORSTEP_R,
    DUEL_RESIDUE_ZONES,
};

/* Residue's borrowed bits inside the shared bytes: zone 2 plus zone 3's
 * intensity low bit in flags, zone 3's element in civic, zone 3's intensity
 * high bit in secondary. The change detector in the master glue masks these
 * out when comparing civic semantics, and duel_snapshot_set_civic preserves
 * them across a civic rewrite. */
#define DUEL_FLAGS_RESIDUE_BITS     0xF8u
#define DUEL_CIVIC_RESIDUE_BITS     0xC0u
#define DUEL_SECONDARY_RESIDUE_BITS 0x80u

typedef struct __attribute__((packed)) {
    uint8_t signature_version;      /* high nibble 0xA, low nibble DUEL_VER */
    uint8_t session;                /* master boot nonce */
    uint8_t flags;                  /* valid/display + residue bits, see above */
    uint8_t seq;                    /* per-session, wrapping byte */
    uint8_t residue;                /* zone0: elem[0:1] int[2:3]; zone1: elem[4:5] int[6:7] */
    duel_view_t view;               /* canonical transport/render projection */
    uint8_t field[SIM_FIELD_SLOTS]; /* kind[0:2], zone[3:4], age[5:6], owner[7] */
    uint8_t external;               /* host: absolute disposable host context; see duel_host.h */
    uint8_t alert;                  /* packed category, priority, and age */
    /* Absolute civic presentation relayed master->slave. The current engine
     * temporarily reuses shared_pres/revision for bounded authoritative
     * aftermath while its marker bit is set. All
     * four bytes are CRC-covered (the checksum spans offsetof(crc)), so a
     * corrupted civic byte is caught exactly like the combat view. See
     * duel_host.h for the DUEL_CIVIC / DUEL_SECONDARY bit layouts; the master
     * writes them via duel_snapshot_set_civic. */
    uint8_t civic;       /* DUEL_CIVIC_* : floor, mode, intensity; bits6-7 residue zone3 element */
    uint8_t secondary;   /* activity 0-2, sky phase 3-4, sky sub-phase 5-6, bit7 residue zone3
                            intensity high */
    uint8_t shared_pres; /* visitor, or aftermath payload when revision.7=1 */
    uint8_t revision;    /* rare event, or marked aftermath phases */
    uint8_t crc;         /* duel_crc8 over the preceding bytes (offsetof(crc)) */
} duel_snapshot_t;

_Static_assert(sizeof(duel_snapshot_t) == 32,
               "v12 snapshot must consume exactly one 32-byte RPC packet");
_Static_assert(offsetof(duel_snapshot_t, external) == 25 &&
                   offsetof(duel_snapshot_t, alert) == 26 &&
                   offsetof(duel_snapshot_t, civic) == 27 &&
                   offsetof(duel_snapshot_t, secondary) == 28 &&
                   offsetof(duel_snapshot_t, shared_pres) == 29 &&
                   offsetof(duel_snapshot_t, revision) == 30,
               "v12 must preserve all trailing split offsets");

#define DUEL_FIELD_KIND(value)  ((uint8_t)((value) & 7u))
#define DUEL_FIELD_ZONE(value)  ((uint8_t)(((value) >> 3) & 3u))
#define DUEL_FIELD_AGE(value)   ((uint8_t)(((value) >> 5) & 3u))
#define DUEL_FIELD_OWNER(value) ((uint8_t)(((value) >> 7) & 1u))

uint8_t duel_crc8(const void *data, size_t len);
uint8_t duel_field_projection(const sim_field_t *field);

// `external` is a packed, disposable presentation summary. The ordinary
// runtime encoder writes the complete production packet and computes the CRC.
void duel_encode_external_alert_display(const sim_world_t *w, uint8_t session, uint16_t seq,
                                        uint8_t external, uint8_t alert, uint8_t display_phase,
                                        duel_snapshot_t *out);

// Convergence setter: overwrite the four civic bytes on an already
// encoded snapshot and recompute the CRC. The encoders prefill shared_pres
// and revision from the world's aftermath state (offline simulation and the
// tests rely on that prefill — do not "simplify" it away); the master glue
// (keymap.c) then overwrites all four bytes via this call to relay the
// current civic state, including its own aftermath override.
// Civic bits6-7 and secondary bit7 belong to residue zone 3, which the
// encoder fills from the world. This call masks them out of the
// incoming semantics and preserves the encoder's bits, so callers need no
// ordering dance.
void duel_snapshot_set_civic(duel_snapshot_t *p, uint8_t civic, uint8_t secondary,
                             uint8_t shared_pres, uint8_t revision);

/* v12 residue accessors — the only sanctioned door to the scattered zone
 * bits (zone 3 straddles civic/flags/secondary). The setter recomputes the
 * CRC. Elements reuse ELEM_*; intensity 0 means empty and its canonical
 * form requires element 0 (the validator rejects non-canonical zones).
 * duel_snapshot_residue_render repacks all four zones into the two-byte
 * duel_render_t grammar (see duel_residue_pack) for the slave's render fill. */
uint8_t duel_snapshot_residue_element(const duel_snapshot_t *p, uint8_t zone);
uint8_t duel_snapshot_residue_intensity(const duel_snapshot_t *p, uint8_t zone);
void duel_snapshot_residue_render(const duel_snapshot_t *p, uint8_t out[2]);

// Magic/version/CRC check. A false result means: drop silently, the next
// packet lands within a couple of ticks.
bool duel_decode_valid(const duel_snapshot_t *p);

/* ---- slave-side acceptance ---------------------------------------------- */
typedef struct {
    bool have_any;
    duel_snapshot_t last; /* last accepted packet */
#ifdef ARCANE_DIAGNOSTICS
    uint16_t stale_drops; /* rejected as stale/duplicate, saturating */
#endif
} duel_rx_state_t;

// Applies `p` (assumed already validated) if it should replace the current
// view; returns whether it did. `link_was_stale` is the caller's link-timeout
// judgement and forces adoption, guaranteeing convergence even if a rebooted
// master collides on the session nonce with a lower sequence number.
bool duel_rx_accept(duel_rx_state_t *rx, const duel_snapshot_t *p, bool link_was_stale);
