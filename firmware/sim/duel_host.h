/*
 * duel_host.h — M8 semantic host protocol and disposable external context.
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
#define DUEL_HOST_VERSION     1
#define DUEL_HOST_PAYLOAD_SIZE 20

enum {
    DUEL_HOST_MSG_HELLO     = 1,
    DUEL_HOST_MSG_HEARTBEAT = 2,
    DUEL_HOST_MSG_NOTIFY    = 3,
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
    uint8_t  payload[DUEL_HOST_PAYLOAD_SIZE]; /* [0] scene, [1] notification count */
    uint8_t  crc;
} duel_host_packet_t;

_Static_assert(sizeof(duel_host_packet_t) == DUEL_HOST_REPORT_SIZE,
               "host report must match QMK RAW_EPSIZE");

typedef struct {
    bool     have_session;
    bool     have_previous;
    bool     online;
    uint32_t session;
    uint32_t previous_session;
    uint16_t last_seq;
    uint8_t  scene;
    uint8_t  notification_count;
    uint16_t malformed_packets;
    uint16_t stale_packets;
} duel_host_state_t;

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

bool duel_host_packet_valid(const duel_host_packet_t *packet);
duel_host_result_t duel_host_accept(duel_host_state_t *state,
                                    const duel_host_packet_t *packet);

// Called by the QMK glue after its monotonic heartbeat deadline. Session
// ordering is retained, but all disposable context is cleared immediately.
void duel_host_expire(duel_host_state_t *state);

// Compact absolute context for the master->slave snapshot: bit0 online,
// bits1-2 scene, bits3-6 notification count, bit7 reserved.
#define DUEL_HOST_CONTEXT_PACK(online, scene, notif) \
    ((uint8_t)(((online) ? 1u : 0u) | (((scene) & 3u) << 1) | (((notif) & 15u) << 3)))
#define DUEL_HOST_CONTEXT_ONLINE(value) ((uint8_t)((value) & 1u))
#define DUEL_HOST_CONTEXT_SCENE(value)  ((uint8_t)(((value) >> 1) & 3u))
#define DUEL_HOST_CONTEXT_NOTIF(value)  ((uint8_t)(((value) >> 3) & 15u))

uint8_t duel_host_context(const duel_host_state_t *state);
