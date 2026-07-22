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

#define DUEL_HOST_REPORT_SIZE  32
#define DUEL_HOST_MAGIC0       0xCA
#define DUEL_HOST_MAGIC1       0x8E
#define DUEL_HOST_VERSION      3
#define DUEL_HOST_PAYLOAD_SIZE 20

enum {
    DUEL_HOST_MSG_HELLO = 1,
    DUEL_HOST_MSG_HEARTBEAT = 2,
    DUEL_HOST_MSG_NOTIFY = 3,
};

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
    DUEL_HOST_SCENE_DUEL = 0,
    DUEL_HOST_SCENE_ARCHIVE = 1,
    DUEL_HOST_SCENE_FOCUS = 2,
    DUEL_HOST_SCENE_COUNT = 3,
};

/*
 * Every report carries an absolute scene/count summary. A dropped semantic
 * event is therefore repaired by the next heartbeat instead of becoming
 * permanent state drift. Integers ship little-endian (RP2040 + Linux host).
 */
typedef struct __attribute__((packed)) {
    uint8_t magic0;
    uint8_t magic1;
    uint8_t version;
    uint8_t type;
    uint32_t session;
    uint16_t seq;
    uint8_t payload_len;
    /* scene/count/category/priority/age/persistent/civic/secondary */
    uint8_t payload[DUEL_HOST_PAYLOAD_SIZE];
    uint8_t crc;
} duel_host_packet_t;

_Static_assert(sizeof(duel_host_packet_t) == DUEL_HOST_REPORT_SIZE,
               "host report must match QMK RAW_EPSIZE");

typedef struct {
    uint32_t session;
    uint32_t previous_session;
    uint16_t last_seq;
    uint8_t state_flags;
    uint8_t external;
    uint8_t alert;
    uint8_t civic;     /* last accepted DUEL_CIVIC_* byte (payload[6]) */
    uint8_t secondary; /* last accepted DUEL_SECONDARY_* byte (payload[7]) */
#ifdef ARCANE_DIAGNOSTICS
    uint16_t malformed_packets;
    uint16_t stale_packets;
#endif
} duel_host_state_t;

#define DUEL_HOST_STATE_HAVE_SESSION  0x01u
#define DUEL_HOST_STATE_HAVE_PREVIOUS 0x02u

bool duel_host_packet_valid(const duel_host_packet_t *packet);
// Accept a valid, fresh packet. Returns true only when the accepted packet
// refreshes the heartbeat timeout; NOTIFY still updates absolute semantics.
bool duel_host_accept(duel_host_state_t *state, const duel_host_packet_t *packet);

// Called by the QMK glue after its monotonic heartbeat deadline. Session
// ordering is retained, but all disposable context is cleared immediately.
void duel_host_expire(duel_host_state_t *state);

// Compact absolute context for the master->slave snapshot: bit0 online,
// bits1-2 scene, bits3-6 notification count, bit7 persistent.
#define DUEL_HOST_CONTEXT_PACK(online, scene, notif, persistent)                                   \
    ((uint8_t)(((online) ? 1u : 0u) | (((scene) & 3u) << 1) | (((notif) & 15u) << 3) |             \
               ((persistent) ? 0x80u : 0u)))
#define DUEL_HOST_CONTEXT_ONLINE(value)     ((uint8_t)((value) & 1u))
#define DUEL_HOST_CONTEXT_SCENE(value)      ((uint8_t)(((value) >> 1) & 3u))
#define DUEL_HOST_CONTEXT_NOTIF(value)      ((uint8_t)(((value) >> 3) & 15u))
#define DUEL_HOST_CONTEXT_PERSISTENT(value) ((uint8_t)(((value) >> 7) & 1u))

// Canonical split alert byte: bits0-2 category, bits3-4 priority, bits5-7 age.
#define DUEL_HOST_ALERT_PACK(category, priority, age)                                              \
    ((uint8_t)(((category) & 7u) | (((priority) & 3u) << 3) | (((age) & 7u) << 5)))
#define DUEL_HOST_ALERT_CATEGORY(value) ((uint8_t)((value) & 7u))
#define DUEL_HOST_ALERT_PRIORITY(value) ((uint8_t)(((value) >> 3) & 3u))
#define DUEL_HOST_ALERT_AGE(value)      ((uint8_t)(((value) >> 5) & 7u))

/* ------- Twin Cities civic semantics (shared contract) -----------------
 * The stable interface between the host daemon (produces civic bytes), the split
 * protocol (relays them), and the renderer (derives floors/residents). Pure
 * declarations with zero release footprint. Snapshot packing wires the
 * encode/decode; host adapters produce them; the renderer consumes them. Do
 * not renumber these values. */

// Active tower-floor occupation (civic byte bits 0-1). SPECIAL is reserved.
enum {
    DUEL_CIVIC_FLOOR_COMMONS = 0,
    DUEL_CIVIC_FLOOR_RESEARCH = 1,
    DUEL_CIVIC_FLOOR_WORKSHOP = 2,
    DUEL_CIVIC_FLOOR_SPECIAL = 3,
};
// Civic mode (civic byte bits 2-3): quiets or emphasises the current floor
// without changing which floor is shown.
enum {
    DUEL_CIVIC_MODE_NORMAL = 0,
    DUEL_CIVIC_MODE_QUIET = 1,
    DUEL_CIVIC_MODE_URGENT = 2,
    DUEL_CIVIC_MODE_RESERVED = 3,
};
// Secondary host-activity intensity (civic byte bits 4-5): background host
// workload; local typing intensity stays firmware-derived.
enum {
    DUEL_CIVIC_INTENSITY_CALM = 0,
    DUEL_CIVIC_INTENSITY_ACTIVE = 1,
    DUEL_CIVIC_INTENSITY_BUSY = 2,
    DUEL_CIVIC_INTENSITY_SATURATED = 3,
};
// Secondary activity channel (secondary byte bits 0-2): activates one bounded
// supporting object or ambience.
enum {
    DUEL_CIVIC_SECONDARY_NONE = 0,
    DUEL_CIVIC_SECONDARY_MEDIA = 1,
    DUEL_CIVIC_SECONDARY_TRANSFER = 2,
    DUEL_CIVIC_SECONDARY_SYSTEM = 3,
    DUEL_CIVIC_SECONDARY_CALENDAR = 4,
    DUEL_CIVIC_SECONDARY_SCROLL = 5,
    DUEL_CIVIC_SECONDARY_TAB = 6,
    DUEL_CIVIC_SECONDARY_PAGE = 7,
};

// Civic byte: bits0-1 floor, bits2-3 mode, bits4-5 host intensity. Bits 6-7
// must be clear on Raw HID v3; the split snapshot allocates them to
// residue zone3's element (duel_proto.h) — the master writes them after
// relaying the host's civic bits.
#define DUEL_CIVIC_PACK(floor, mode, intensity)                                                    \
    ((uint8_t)(((floor) & 3u) | (((mode) & 3u) << 2) | (((intensity) & 3u) << 4)))
#define DUEL_CIVIC_FLOOR(value)     ((uint8_t)((value) & 3u))
#define DUEL_CIVIC_MODE(value)      ((uint8_t)(((value) >> 2) & 3u))
#define DUEL_CIVIC_INTENSITY(value) ((uint8_t)(((value) >> 4) & 3u))
#define DUEL_CIVIC_RESERVED_MASK    0xC0u /* Raw HID v3: bits 6-7 must be clear */

// Secondary byte ledger: bits0-2 host activity. The split v12 snapshot owns
// the rest: bits3-4 master sky phase, bits5-6 sky sub-phase (celestial arc
// step within the phase), bit7 residue zone3 intensity high bit (see
// duel_proto.h). Raw HID v3 producers must leave bits3-7 clear — the host
// never supplies sky or residue state.
#define DUEL_SECONDARY_PACK(activity)  ((uint8_t)((activity) & 7u))
#define DUEL_SECONDARY_ACTIVITY(value) ((uint8_t)((value) & 7u))
#define DUEL_SECONDARY_SKY_PACK(secondary, phase)                                                  \
    ((uint8_t)(((secondary) & 7u) | (((phase) & 3u) << 3)))
#define DUEL_SECONDARY_SKY_PHASE(value)         ((uint8_t)(((value) >> 3) & 3u))
#define DUEL_SECONDARY_SKY_SUB_PACK(value, sub) ((uint8_t)(((value) & 0x9Fu) | (((sub) & 3u) << 5)))
#define DUEL_SECONDARY_SKY_SUBPHASE(value)      ((uint8_t)(((value) >> 5) & 3u))
#define DUEL_SECONDARY_HID_RESERVED             0xF8u /* Raw HID v3: bits3-7 must be clear */

// Raw HID range check for the civic byte and the low activity bits of the
// secondary byte. The split snapshot no longer shares it: its civic bits 6-7 carry
// residue state, so duel_decode_valid applies its own checks.
static inline bool duel_civic_semantics_valid(uint8_t civic, uint8_t secondary) {
    return (civic & DUEL_CIVIC_RESERVED_MASK) == 0 &&
           DUEL_SECONDARY_ACTIVITY(secondary) <= DUEL_CIVIC_SECONDARY_PAGE;
}

/* Six renderer-level districts are derived from the existing scene/floor
 * semantics. Applications select broad combinations; no per-app scene enters
 * firmware or the split link. */
enum {
    DUEL_DISTRICT_COMMONS = 0,
    DUEL_DISTRICT_RESEARCH,
    DUEL_DISTRICT_WORKSHOP,
    DUEL_DISTRICT_OBSERVATORY,
    DUEL_DISTRICT_SCRIPTORIUM,
    DUEL_DISTRICT_STUDIO,
    DUEL_DISTRICT_COUNT,
};

static inline uint8_t duel_civic_district(uint8_t civic, uint8_t external) {
    uint8_t floor = DUEL_CIVIC_FLOOR(civic);
    uint8_t scene = DUEL_HOST_CONTEXT_SCENE(external);
    if (floor == DUEL_CIVIC_FLOOR_WORKSHOP)
        return DUEL_DISTRICT_WORKSHOP;
    if (floor == DUEL_CIVIC_FLOOR_SPECIAL && scene == DUEL_HOST_SCENE_FOCUS)
        return DUEL_DISTRICT_OBSERVATORY;
    if (floor == DUEL_CIVIC_FLOOR_RESEARCH && scene == DUEL_HOST_SCENE_DUEL)
        return DUEL_DISTRICT_SCRIPTORIUM;
    if (floor == DUEL_CIVIC_FLOOR_COMMONS && scene == DUEL_HOST_SCENE_ARCHIVE)
        return DUEL_DISTRICT_STUDIO;
    if (floor == DUEL_CIVIC_FLOOR_RESEARCH)
        return DUEL_DISTRICT_RESEARCH;
    if (floor == DUEL_CIVIC_FLOOR_SPECIAL)
        return DUEL_DISTRICT_OBSERVATORY;
    return DUEL_DISTRICT_COMMONS;
}

static inline uint8_t duel_district_floor(uint8_t district) {
    return district == DUEL_DISTRICT_RESEARCH || district == DUEL_DISTRICT_SCRIPTORIUM
               ? DUEL_CIVIC_FLOOR_RESEARCH
           : district == DUEL_DISTRICT_WORKSHOP    ? DUEL_CIVIC_FLOOR_WORKSHOP
           : district == DUEL_DISTRICT_OBSERVATORY ? DUEL_CIVIC_FLOOR_SPECIAL
                                                   : DUEL_CIVIC_FLOOR_COMMONS;
}

// Raw HID v3 payload positions for the always-present civic bytes.
#define DUEL_HOST_PAYLOAD_CIVIC     6
#define DUEL_HOST_PAYLOAD_SECONDARY 7
#define DUEL_HOST_PAYLOAD_LEN       8

uint8_t duel_host_context(const duel_host_state_t *state);
uint8_t duel_host_alert(const duel_host_state_t *state);

// Disposable civic context: mirrors duel_host_context/alert gating — both
// collapse to zero while the daemon is offline (expiry clears them too).
uint8_t duel_host_civic(const duel_host_state_t *state);
uint8_t duel_host_secondary(const duel_host_state_t *state);
