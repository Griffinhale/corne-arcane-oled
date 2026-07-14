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

uint8_t duel_host_context(const duel_host_state_t *state);
uint8_t duel_host_alert(const duel_host_state_t *state);
