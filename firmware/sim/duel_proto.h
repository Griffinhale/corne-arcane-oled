/*
 * duel_proto.h — split-link snapshot protocol (M3).
 *
 * The master encodes its authoritative world into a compact packet every few
 * ticks; the slave renders from the last accepted packet and can never roll
 * state backward. Hardware-agnostic (no QMK includes) so the host harness
 * replays loss/duplication/reordering with exactly the firmware's code.
 *
 * Wire format notes: 31 bytes packed, under QMK's 32-byte
 * RPC_M2S_BUFFER_SIZE. Both halves (and the test hosts we care about) are
 * little-endian, so the struct ships as raw bytes. The serial protocol only
 * checksums its own framing, hence our CRC over the payload.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "duel_sim.h"

#define DUEL_MAGIC 0xA7
#define DUEL_VER   7

// Snapshot flags: bit0 world valid; bits1-2 synchronized display phase.
// The reserved v7 bits carry presentation policy without changing the packet
// size, version, simulation world, or compatibility with older v7 receivers.
#define DUEL_FLAGS_WORLD_VALID 0x01u
#define DUEL_FLAGS_DISPLAY_PACK(phase) ((uint8_t)(((phase) & 3u) << 1))
#define DUEL_FLAGS_DISPLAY(flags)      ((uint8_t)(((flags) >> 1) & 3u))

// M7.5 charge byte per wizard: bits0-3 wind-up countdown, bits4-5 recipe
// presentation tier, bits6-7 reserved. This is absolute render state; the
// slave never advances it independently.
#define DUEL_CHARGE_PACK(windup, tier) ((uint8_t)(((windup) & 0x0F) | (((tier) & 3) << 4)))
#define DUEL_CHARGE_WINDUP(b) ((b) & 0x0F)
#define DUEL_CHARGE_TIER(b)   (((b) >> 4) & 3)
_Static_assert(SIM_CAST_WINDUP_TICKS <= 15, "wind-up must fit the 4-bit wire field");

// scry byte (M7): bit0 overlay open, bits1-2 scene selector, bits3-7 reserved
#define DUEL_SCRY_PACK(open, scene) ((uint8_t)(((open) ? 1 : 0) | (((scene) & 3) << 1)))
#define DUEL_SCRY_OPEN(b)  ((b) & 1)
#define DUEL_SCRY_SCENE(b) (((b) >> 1) & 3)
_Static_assert(SCRY_SCENES <= 4, "scene selector must fit the 2-bit wire field");

// bit0/1: spell slot active; bit2/3: dir sign (1 = negative / leftward)
#define DUEL_SPELLSTATE_ACTIVE(slot) (1u << (slot))
#define DUEL_SPELLSTATE_NEG(slot)    (1u << (2 + (slot)))

// life byte: bits0-2 LIFE_* state, bits3-5 roster variant, bits6-7 reserved
#define DUEL_LIFE_PACK(life, variant) ((uint8_t)(((life) & 0x07) | (((variant) & 0x07) << 3)))
#define DUEL_LIFE_STATE(b)   ((b) & 0x07)
#define DUEL_LIFE_VARIANT(b) (((b) >> 3) & 0x07)
_Static_assert(SIM_ROSTER_N <= 8, "roster variant must fit the 3-bit wire field");

typedef struct __attribute__((packed)) {
    uint8_t  magic;        /* DUEL_MAGIC */
    uint8_t  ver;          /* DUEL_VER */
    uint8_t  session;      /* master boot nonce */
    uint8_t  flags;        /* bit0: world valid; rest reserved */
    uint16_t seq;          /* per-session, wraps */
    uint16_t tick16;       /* low 16 bits of world tick (debug HUD) */
    uint8_t  pose[2];      /* pose (2 bits) | pose_ticks (6 bits) per wizard */
    uint8_t  hp[2];
    uint8_t  shield[2];
    uint8_t  spell_pos[2]; /* battlefield u per slot */
    uint8_t  spell_state;  /* DUEL_SPELLSTATE_* bits */
    uint8_t  fx_seq;       /* one-shot outcome counter */
    uint8_t  fx_kind;
    uint8_t  life[2];      /* bits0-2 LIFE_*, bits3-5 roster variant, bits6-7 reserved */
    uint8_t  life_ticks[2]; /* remaining phase ticks; 0 while ACTIVE */
    uint8_t  spell_kind[2];
    uint8_t  charge[2];     /* M7.5: wind-up countdown + recipe presentation tier */
    uint8_t  scry;         /* M7: bit0 overlay open, bits1-2 scene */
    uint8_t  external;     /* M8: absolute disposable host context; see duel_host.h */
    uint8_t  alert;        /* M10: packed category, priority, and age */
    uint8_t  crc;          /* duel_crc8 over the 30 preceding bytes */
} duel_snapshot_t;

_Static_assert(sizeof(duel_snapshot_t) == 31, "snapshot must stay under the 32-byte RPC limit");

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

// Rebuild a render-ready world from a packet (the slave's view). Fields the
// packet doesn't carry stay zero; SIMF_AUTHORITATIVE is never set here.
void duel_decode_world(const duel_snapshot_t *p, sim_world_t *out);

/* ---- slave-side acceptance ---------------------------------------------- */
typedef struct {
    bool            have_any;
    uint8_t         session;
    uint16_t        last_seq;
    uint16_t        stale_drops; /* rejected as stale/duplicate, saturating */
    duel_snapshot_t last;        /* last accepted packet */
} duel_rx_state_t;

// Applies `p` (assumed already validated) if it should replace the current
// view; returns whether it did. `link_was_stale` is the caller's link-timeout
// judgement and forces adoption, guaranteeing convergence even if a rebooted
// master collides on the session nonce with a lower sequence number.
bool duel_rx_accept(duel_rx_state_t *rx, const duel_snapshot_t *p, bool link_was_stale);
