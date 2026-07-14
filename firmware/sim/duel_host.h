/*
 * duel_host.h — versioned semantic host protocol and disposable external
 * context.
 *
 * Hardware-agnostic: QMK only transports the fixed 32-byte reports and owns
 * the heartbeat clock. This module validates packets, orders daemon sessions,
 * and stores the small context that may disappear without touching the duel.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DUEL_HOST_REPORT_SIZE 32
#define DUEL_HOST_MAGIC0      0xCA
#define DUEL_HOST_MAGIC1      0x8E
#define DUEL_HOST_VERSION     2
#define DUEL_HOST_VERSION_V1  1
#define DUEL_HOST_PAYLOAD_SIZE 20

enum {
    DUEL_HOST_MSG_HELLO     = 1,
    DUEL_HOST_MSG_HEARTBEAT = 2,
    DUEL_HOST_MSG_NOTIFY    = 3,
};

/*
 * Diagnostics use the same physical Raw HID endpoint but a deliberately
 * separate, diagnostics-build-only protocol.  The request/response envelope
 * stays fixed at one QMK report; two response pages expose the complete
 * master and reverse-RPC measurements without allocating release traffic.
 */
#define DUEL_HOST_DIAG_VERSION 1
#define DUEL_HOST_DIAG_PAGES   2

enum {
    DUEL_HOST_MSG_DIAG_REQUEST  = 0x70,
    DUEL_HOST_MSG_DIAG_RESPONSE = 0x71,
};

enum {
    DUEL_HOST_DIAG_FLAG_FIXED_SPLIT_CADENCE = 0x01,
};

typedef struct __attribute__((packed)) {
    uint8_t  magic0;
    uint8_t  magic1;
    uint8_t  version;
    uint8_t  type;
    uint8_t  page;
    uint8_t  page_count;
    uint16_t nonce;
    uint8_t  payload[23];
    uint8_t  crc;
} duel_host_diag_packet_t;

_Static_assert(sizeof(duel_host_diag_packet_t) == DUEL_HOST_REPORT_SIZE,
               "diagnostic report must match QMK RAW_EPSIZE");

enum {
    DUEL_HOST_CATEGORY_NONE = 0,
    DUEL_HOST_CATEGORY_TERMINAL,
    DUEL_HOST_CATEGORY_COMMUNICATION,
    DUEL_HOST_CATEGORY_TRANSFER,
    DUEL_HOST_CATEGORY_SYSTEM,
    DUEL_HOST_CATEGORY_CALENDAR,
    DUEL_HOST_CATEGORY_SECURITY,
    DUEL_HOST_CATEGORY_OTHER,
    DUEL_HOST_CATEGORY_COUNT,
};

enum {
    DUEL_HOST_PRIORITY_NONE = 0,
    DUEL_HOST_PRIORITY_LOW,
    DUEL_HOST_PRIORITY_NORMAL,
    DUEL_HOST_PRIORITY_CRITICAL,
    DUEL_HOST_PRIORITY_COUNT,
};

enum {
    DUEL_HOST_SCENE_DUEL    = 0,
    DUEL_HOST_SCENE_ARCHIVE = 1,
    DUEL_HOST_SCENE_FOCUS   = 2,
    DUEL_HOST_SCENE_COUNT   = 3,
};

/*
 * Every report carries an absolute scene/count summary. A dropped semantic
 * event is therefore repaired by the next heartbeat instead of becoming
 * permanent state drift. Integers ship little-endian (RP2040 + Linux host).
 */
typedef struct __attribute__((packed)) {
    uint8_t  magic0;
    uint8_t  magic1;
    uint8_t  version;
    uint8_t  type;
    uint32_t session;
    uint16_t seq;
    uint8_t  payload_len;
    /* v2: scene/count/category/priority/age/persistent */
    uint8_t  payload[DUEL_HOST_PAYLOAD_SIZE];
    uint8_t  crc;
} duel_host_packet_t;

_Static_assert(sizeof(duel_host_packet_t) == DUEL_HOST_REPORT_SIZE,
               "host report must match QMK RAW_EPSIZE");

typedef struct {
    uint32_t session;
    uint32_t previous_session;
    uint16_t last_seq;
    uint8_t  state_flags;
    uint8_t  external;
    uint8_t  alert;
#ifdef ARCANE_M12
    uint8_t  civic;      /* last accepted DUEL_CIVIC_* byte (payload[6]) */
    uint8_t  secondary;  /* last accepted DUEL_SECONDARY_* byte (payload[7]) */
#endif
#ifdef ARCANE_DIAGNOSTICS
    uint16_t malformed_packets;
    uint16_t stale_packets;
#endif
} duel_host_state_t;

#define DUEL_HOST_STATE_HAVE_SESSION  0x01u
#define DUEL_HOST_STATE_HAVE_PREVIOUS 0x02u

typedef enum {
    DUEL_HOST_DROP_MALFORMED = 0,
    DUEL_HOST_DROP_STALE,
    DUEL_HOST_APPLIED,
    DUEL_HOST_APPLIED_HEARTBEAT,
} duel_host_result_t;

// Canonical encoder used by host tests; the daemon has an independent Python
// implementation checked against the same known vector.
void duel_host_encode(uint8_t type, uint32_t session, uint16_t seq,
                      uint8_t scene, uint8_t notification_count,
                      duel_host_packet_t *out);
void duel_host_encode_summary(uint8_t type, uint32_t session, uint16_t seq,
                      uint8_t scene, uint8_t notification_count,
                      uint8_t category, uint8_t priority, uint8_t age,
                      bool persistent,
                      duel_host_packet_t *out);
void duel_host_encode_v1(uint8_t type, uint32_t session, uint16_t seq,
                         uint8_t scene, uint8_t notification_count,
                         duel_host_packet_t *out);

#ifdef ARCANE_M12
// M12 v2 civic encoder: a full v2 summary plus payload[6]=civic/[7]=secondary
// with payload_len promoted to DUEL_HOST_PAYLOAD_LEN_M12 (8). The 32-byte report
// size is unchanged; only two previously-zero payload bytes now carry meaning.
void duel_host_encode_civic(uint8_t type, uint32_t session, uint16_t seq,
                            uint8_t scene, uint8_t notification_count,
                            uint8_t category, uint8_t priority, uint8_t age,
                            bool persistent, uint8_t civic, uint8_t secondary,
                            duel_host_packet_t *out);
#endif

bool duel_host_packet_valid(const duel_host_packet_t *packet);
duel_host_result_t duel_host_accept(duel_host_state_t *state,
                                    const duel_host_packet_t *packet);

// Called by the QMK glue after its monotonic heartbeat deadline. Session
// ordering is retained, but all disposable context is cleared immediately.
void duel_host_expire(duel_host_state_t *state);

// Compact absolute context for the master->slave snapshot: bit0 online,
// bits1-2 scene, bits3-6 notification count, bit7 persistent.
#define DUEL_HOST_CONTEXT_PACK(online, scene, notif, persistent) \
    ((uint8_t)(((online) ? 1u : 0u) | (((scene) & 3u) << 1) | \
               (((notif) & 15u) << 3) | ((persistent) ? 0x80u : 0u)))
#define DUEL_HOST_CONTEXT_ONLINE(value) ((uint8_t)((value) & 1u))
#define DUEL_HOST_CONTEXT_SCENE(value)  ((uint8_t)(((value) >> 1) & 3u))
#define DUEL_HOST_CONTEXT_NOTIF(value)  ((uint8_t)(((value) >> 3) & 15u))
#define DUEL_HOST_CONTEXT_PERSISTENT(value) ((uint8_t)(((value) >> 7) & 1u))

// Canonical split alert byte: bits0-2 category, bits3-4 priority, bits5-7 age.
#define DUEL_HOST_ALERT_PACK(category, priority, age) \
    ((uint8_t)(((category) & 7u) | (((priority) & 3u) << 3) | (((age) & 7u) << 5)))
#define DUEL_HOST_ALERT_CATEGORY(value) ((uint8_t)((value) & 7u))
#define DUEL_HOST_ALERT_PRIORITY(value) ((uint8_t)(((value) >> 3) & 3u))
#define DUEL_HOST_ALERT_AGE(value)      ((uint8_t)(((value) >> 5) & 7u))

/* ------- M12 Twin Cities civic semantics (shared cross-track contract) -------
 * The stable interface between the host daemon (produces civic bytes), the split
 * protocol (relays them), and the renderer (derives floors/residents). Pure
 * declarations with zero release footprint. Track P wires the encode/decode;
 * Track H produces them; Track R consumes them. Do not renumber these values. */

// Active tower-floor occupation (civic byte bits 0-1). WORKSHOP arrives with the
// M12.1 terminal/build semantics; SPECIAL stays reserved for a later world.
enum {
    DUEL_M12_FLOOR_COMMONS  = 0,
    DUEL_M12_FLOOR_RESEARCH = 1,
    DUEL_M12_FLOOR_WORKSHOP = 2,
    DUEL_M12_FLOOR_SPECIAL  = 3,
};
// Civic mode (civic byte bits 2-3): quiets or emphasises the current floor
// without changing which floor is shown.
enum {
    DUEL_M12_MODE_NORMAL   = 0,
    DUEL_M12_MODE_QUIET    = 1,
    DUEL_M12_MODE_URGENT   = 2,
    DUEL_M12_MODE_RESERVED = 3,
};
// Secondary host-activity intensity (civic byte bits 4-5): background host
// workload; local typing intensity stays firmware-derived.
enum {
    DUEL_M12_INTENSITY_CALM      = 0,
    DUEL_M12_INTENSITY_ACTIVE    = 1,
    DUEL_M12_INTENSITY_BUSY      = 2,
    DUEL_M12_INTENSITY_SATURATED = 3,
};
// Secondary activity channel (secondary byte bits 0-2): activates one bounded
// supporting object or ambience. TRANSFER/SYSTEM arrive with M12.1.
enum {
    DUEL_M12_SECONDARY_NONE     = 0,
    DUEL_M12_SECONDARY_MEDIA    = 1,
    DUEL_M12_SECONDARY_TRANSFER = 2,
    DUEL_M12_SECONDARY_SYSTEM   = 3,
    DUEL_M12_SECONDARY_CALENDAR = 4,
};

// Civic byte: bits0-1 floor, bits2-3 mode, bits4-5 host intensity, 6-7 reserved.
#define DUEL_CIVIC_PACK(floor, mode, intensity) \
    ((uint8_t)(((floor) & 3u) | (((mode) & 3u) << 2) | (((intensity) & 3u) << 4)))
#define DUEL_CIVIC_FLOOR(value)     ((uint8_t)((value) & 3u))
#define DUEL_CIVIC_MODE(value)      ((uint8_t)(((value) >> 2) & 3u))
#define DUEL_CIVIC_INTENSITY(value) ((uint8_t)(((value) >> 4) & 3u))

// Secondary byte: bits0-2 secondary activity, bits3-7 reserved for later civic
// semantics (visitor/rare-event spill under M12.1/M12.2).
#define DUEL_SECONDARY_PACK(activity)  ((uint8_t)((activity) & 7u))
#define DUEL_SECONDARY_ACTIVITY(value) ((uint8_t)((value) & 7u))

// Raw HID v2 payload positions for the civic bytes. payload_len becomes 8 under
// M12; Track P owns the encode/validate wiring and the split-packet relay.
#define DUEL_HOST_PAYLOAD_CIVIC     6
#define DUEL_HOST_PAYLOAD_SECONDARY 7
#define DUEL_HOST_PAYLOAD_LEN_M12   8

uint8_t duel_host_context(const duel_host_state_t *state);
uint8_t duel_host_alert(const duel_host_state_t *state);

#ifdef ARCANE_M12
// Disposable civic context: mirrors duel_host_context/alert gating — both
// collapse to zero while the daemon is offline (expiry clears them too).
uint8_t duel_host_civic(const duel_host_state_t *state);
uint8_t duel_host_secondary(const duel_host_state_t *state);
#endif
