#include "duel_host.h"
#include "duel_proto.h"

#ifdef ARCANE_DIAGNOSTICS
static void sat_inc(uint16_t *value) {
    if (*value != UINT16_MAX)
        (*value)++;
}
#endif

static bool type_valid(uint8_t type) {
    return type == DUEL_HOST_MSG_HELLO || type == DUEL_HOST_MSG_HEARTBEAT ||
           type == DUEL_HOST_MSG_NOTIFY;
}

static bool envelope_valid(const duel_host_packet_t *packet) {
    if (packet->magic0 != DUEL_HOST_MAGIC0 || packet->magic1 != DUEL_HOST_MAGIC1 ||
        !type_valid(packet->type) || packet->version != DUEL_HOST_VERSION ||
        packet->payload_len != DUEL_HOST_PAYLOAD_LEN ||
        packet->crc != duel_crc8(packet, offsetof(duel_host_packet_t, crc)))
        return false;
    /* Bytes beyond payload_len are reserved and must ship zero (the canonical
     * host encoder always zeroes them). Rejecting CRC-covered garbage here
     * keeps the reserved space genuinely available for future versions. */
    for (uint8_t i = DUEL_HOST_PAYLOAD_LEN; i < DUEL_HOST_PAYLOAD_SIZE; i++)
        if (packet->payload[i] != 0)
            return false;
    return true;
}

static bool civic_bytes_valid(const duel_host_packet_t *packet) {
    return (packet->payload[DUEL_HOST_PAYLOAD_SECONDARY] & DUEL_SECONDARY_HID_RESERVED) == 0 &&
           duel_civic_semantics_valid(packet->payload[DUEL_HOST_PAYLOAD_CIVIC],
                                      packet->payload[DUEL_HOST_PAYLOAD_SECONDARY]);
}

// An empty notification summary must be canonical (all-zero detail); a
// non-empty one must land inside every enum range, and only CRITICAL
// notifications may be flagged persistent.
static bool notification_valid(const duel_host_packet_t *packet) {
    if (packet->payload[0] >= DUEL_HOST_SCENE_COUNT || packet->payload[1] > 15)
        return false;
    bool empty = packet->payload[1] == 0;
    bool canonical_empty = packet->payload[2] == DUEL_HOST_CATEGORY_NONE &&
                           packet->payload[3] == DUEL_HOST_PRIORITY_NONE &&
                           packet->payload[4] == 0 && packet->payload[5] == 0;
    bool nonempty = packet->payload[2] > DUEL_HOST_CATEGORY_NONE &&
                    packet->payload[2] < DUEL_HOST_CATEGORY_COUNT &&
                    packet->payload[3] > DUEL_HOST_PRIORITY_NONE &&
                    packet->payload[3] < DUEL_HOST_PRIORITY_COUNT && packet->payload[4] <= 7 &&
                    packet->payload[5] <= 1 &&
                    (!packet->payload[5] || packet->payload[3] == DUEL_HOST_PRIORITY_CRITICAL);
    return empty ? canonical_empty : nonempty;
}

bool duel_host_packet_valid(const duel_host_packet_t *packet) {
    return envelope_valid(packet) && civic_bytes_valid(packet) && notification_valid(packet);
}

static void stale(duel_host_state_t *state) {
#ifdef ARCANE_DIAGNOSTICS
    sat_inc(&state->stale_packets);
#else
    (void)state;
#endif
}

static void apply_context(duel_host_state_t *state, const duel_host_packet_t *packet, bool online) {
    bool persistent = packet->payload[5] != 0;
    state->external =
        DUEL_HOST_CONTEXT_PACK(online, packet->payload[0], packet->payload[1], persistent);
    state->alert = DUEL_HOST_ALERT_PACK(packet->payload[2], packet->payload[3], packet->payload[4]);
    state->civic = packet->payload[DUEL_HOST_PAYLOAD_CIVIC];
    state->secondary = packet->payload[DUEL_HOST_PAYLOAD_SECONDARY];
}

bool duel_host_accept(duel_host_state_t *state, const duel_host_packet_t *packet) {
    if (!duel_host_packet_valid(packet)) {
#ifdef ARCANE_DIAGNOSTICS
        sat_inc(&state->malformed_packets);
#endif
        return false;
    }

    if (packet->type == DUEL_HOST_MSG_HELLO) {
        // A session can be adopted only through its unique sequence-zero
        // greeting. Remembering the prior ID prevents a delayed old greeting
        // from rolling a freshly restarted daemon backward.
        if (packet->seq != 0 ||
            ((state->state_flags & DUEL_HOST_STATE_HAVE_SESSION) &&
             packet->session == state->session) ||
            ((state->state_flags & DUEL_HOST_STATE_HAVE_PREVIOUS) &&
             packet->session == state->previous_session)) {
            stale(state);
            return false;
        }
        if (state->state_flags & DUEL_HOST_STATE_HAVE_SESSION) {
            state->state_flags |= DUEL_HOST_STATE_HAVE_PREVIOUS;
            state->previous_session = state->session;
        }
        state->state_flags |= DUEL_HOST_STATE_HAVE_SESSION;
        state->session = packet->session;
        state->last_seq = 0;
        apply_context(state, packet, true);
        return true;
    }

    if (!(state->state_flags & DUEL_HOST_STATE_HAVE_SESSION) || packet->session != state->session ||
        (int16_t)(packet->seq - state->last_seq) <= 0) {
        stale(state);
        return false;
    }

    state->last_seq = packet->seq;
    bool online =
        packet->type == DUEL_HOST_MSG_HEARTBEAT || DUEL_HOST_CONTEXT_ONLINE(state->external);
    apply_context(state, packet, online);
    if (packet->type == DUEL_HOST_MSG_HEARTBEAT) {
        return true;
    }
    return false;
}

void duel_host_expire(duel_host_state_t *state) {
    state->external = 0;
    state->alert = 0;
    state->civic = 0;
    state->secondary = 0;
}

uint8_t duel_host_context(const duel_host_state_t *state) {
    return DUEL_HOST_CONTEXT_ONLINE(state->external) ? state->external : 0;
}

uint8_t duel_host_alert(const duel_host_state_t *state) {
    return DUEL_HOST_CONTEXT_ONLINE(state->external) ? state->alert : 0;
}

uint8_t duel_host_civic(const duel_host_state_t *state) {
    return DUEL_HOST_CONTEXT_ONLINE(state->external) ? state->civic : 0;
}

uint8_t duel_host_secondary(const duel_host_state_t *state) {
    return DUEL_HOST_CONTEXT_ONLINE(state->external) ? state->secondary : 0;
}
