/* Stable civic presentation enums and byte-packing contracts. */
#pragma once

#include <stdint.h>

enum {
    DUEL_CIVIC_PERSONALITY_DILIGENT = 0,
    DUEL_CIVIC_PERSONALITY_CURIOUS,
    DUEL_CIVIC_PERSONALITY_NERVOUS,
    DUEL_CIVIC_PERSONALITY_PROUD,
    DUEL_CIVIC_PERSONALITY_DISTRACTED,
    DUEL_CIVIC_PERSONALITY_COUNT,
};
enum {
    DUEL_CIVIC_ACTION_WORK = 0,
    DUEL_CIVIC_ACTION_WALK,
    DUEL_CIVIC_ACTION_INSPECT,
    DUEL_CIVIC_ACTION_REST,
    DUEL_CIVIC_ACTION_WATCH_ROOF,
    DUEL_CIVIC_ACTION_HANDLE_DELIVERY,
    DUEL_CIVIC_ACTION_REACT,
    DUEL_CIVIC_ACTION_COUNT,
};
enum {
    DUEL_CIVIC_COURIER_NONE = 0,
    DUEL_CIVIC_COURIER_MESSENGER,
    DUEL_CIVIC_COURIER_PARCEL,
    DUEL_CIVIC_COURIER_BEACON,
    DUEL_CIVIC_COURIER_SENTINEL,
    DUEL_CIVIC_COURIER_COUNT,
};
enum {
    DUEL_CIVIC_EVENT_NONE = 0,
    DUEL_CIVIC_EVENT_RUNAWAY_SCROLL,
    DUEL_CIVIC_EVENT_JAMMED_GEAR,
    DUEL_CIVIC_EVENT_WORK_BREAK,
    DUEL_CIVIC_EVENT_DAMAGE_COMPLAINT,
    DUEL_CIVIC_EVENT_DIPLOMATIC_COURIER,
    DUEL_CIVIC_EVENT_CIVIC_SKY,
    DUEL_CIVIC_EVENT_COUNT,
};

typedef struct { uint8_t kind_target; uint8_t lifecycle_phase; uint8_t progress_flags; }
    civic_visitor_state_t;
typedef struct { uint8_t id_target; uint8_t phase; uint8_t progress; }
    civic_event_state_t;

enum {
    DUEL_CIVIC_VISIT_ARRIVING = 0,
    DUEL_CIVIC_VISIT_WAITING,
    DUEL_CIVIC_VISIT_AGING,
    DUEL_CIVIC_VISIT_RESOLVING,
};
#define DUEL_VISITOR_PACK(kind, city, life) \
    ((uint8_t)(((kind) & 7u) | (((city) & 1u) << 3) | (((life) & 3u) << 4)))
#define DUEL_VISITOR_KIND(v)      ((uint8_t)((v) & 7u))
#define DUEL_VISITOR_CITY(v)      ((uint8_t)(((v) >> 3) & 1u))
#define DUEL_VISITOR_LIFECYCLE(v) ((uint8_t)(((v) >> 4) & 3u))

enum {
    DUEL_CIVIC_EVENT_PHASE_ARMED = 0,
    DUEL_CIVIC_EVENT_PHASE_ACTIVE,
    DUEL_CIVIC_EVENT_PHASE_RESOLVING,
    DUEL_CIVIC_EVENT_PHASE_COOLDOWN,
};
enum {
    DUEL_CIVIC_EVENT_TARGET_LEFT = 0,
    DUEL_CIVIC_EVENT_TARGET_RIGHT,
    DUEL_CIVIC_EVENT_TARGET_SHARED,
};
#define DUEL_EVENT_PACK(id, phase, target) \
    ((uint8_t)(((id) & 7u) | (((phase) & 3u) << 3) | (((target) & 3u) << 5)))
#define DUEL_EVENT_ID(v)     ((uint8_t)((v) & 7u))
#define DUEL_EVENT_PHASE(v)  ((uint8_t)(((v) >> 3) & 3u))
#define DUEL_EVENT_TARGET(v) ((uint8_t)(((v) >> 5) & 3u))

#define INCANTATION_AFTERMATH_WIRE          0x80u
#define INCANTATION_AFTERMATH_REV_RESERVED  0x70u
#define INCANTATION_AFTER_KIND(v, side) \
    ((uint8_t)(((v) >> ((side) * 3u)) & 7u))
#define INCANTATION_AFTER_WORLD(v) ((uint8_t)(((v) >> 6) & 3u))
#define INCANTATION_AFTER_PHASE(v, side) \
    ((uint8_t)(((v) >> ((side) * 2u)) & 3u))
#define INCANTATION_FLOOR_TRANSITION_PACK(source, phase, active) \
    ((uint8_t)(((source) & 3u) | (((phase) & 3u) << 2) | ((active) ? 0x10u : 0u)))
#define INCANTATION_FLOOR_TRANSITION_SOURCE(v) ((uint8_t)((v) & 3u))
#define INCANTATION_FLOOR_TRANSITION_PHASE(v)  ((uint8_t)(((v) >> 2) & 3u))
#define INCANTATION_FLOOR_TRANSITION_ACTIVE(v) (((v) & 0x10u) != 0u)
