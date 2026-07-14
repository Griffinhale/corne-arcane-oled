#include <string.h>

#include "duel_host.h"
#include "duel_proto.h"

#ifdef ARCANE_DIAGNOSTICS
static void sat_inc(uint16_t *value) {
    if (*value != UINT16_MAX) (*value)++;
}
#endif

static bool type_valid(uint8_t type) {
    return type == DUEL_HOST_MSG_HELLO || type == DUEL_HOST_MSG_HEARTBEAT ||
           type == DUEL_HOST_MSG_NOTIFY;
}

void duel_host_encode_summary(uint8_t type, uint32_t session, uint16_t seq,
                      uint8_t scene, uint8_t notification_count,
                      uint8_t category, uint8_t priority, uint8_t age,
                      bool persistent,
                      duel_host_packet_t *out) {
    memset(out, 0, sizeof *out);
    out->magic0      = DUEL_HOST_MAGIC0;
    out->magic1      = DUEL_HOST_MAGIC1;
    out->version     = DUEL_HOST_VERSION;
    out->type        = type;
    out->session     = session;
    out->seq         = seq;
    out->payload_len = 6;
    out->payload[0]  = scene;
    out->payload[1]  = notification_count;
    out->payload[2]  = category;
    out->payload[3]  = priority;
    out->payload[4]  = age;
    out->payload[5]  = persistent ? 1 : 0;
    out->crc         = duel_crc8(out, offsetof(duel_host_packet_t, crc));
}

void duel_host_encode(uint8_t type, uint32_t session, uint16_t seq,
                      uint8_t scene, uint8_t notification_count,
                      duel_host_packet_t *out) {
    duel_host_encode_summary(type, session, seq, scene, notification_count,
        notification_count ? DUEL_HOST_CATEGORY_OTHER : DUEL_HOST_CATEGORY_NONE,
        notification_count ? DUEL_HOST_PRIORITY_NORMAL : DUEL_HOST_PRIORITY_NONE,
        0, false, out);
}

void duel_host_encode_v1(uint8_t type, uint32_t session, uint16_t seq,
                         uint8_t scene, uint8_t notification_count,
                         duel_host_packet_t *out) {
    duel_host_encode_summary(type, session, seq, scene, notification_count,
                     DUEL_HOST_CATEGORY_NONE, DUEL_HOST_PRIORITY_NONE, 0, false, out);
    out->version = DUEL_HOST_VERSION_V1;
    out->payload_len = 2;
    out->crc = duel_crc8(out, offsetof(duel_host_packet_t, crc));
}

#ifdef ARCANE_M12
void duel_host_encode_civic(uint8_t type, uint32_t session, uint16_t seq,
                            uint8_t scene, uint8_t notification_count,
                            uint8_t category, uint8_t priority, uint8_t age,
                            bool persistent, uint8_t civic, uint8_t secondary,
                            duel_host_packet_t *out) {
    duel_host_encode_summary(type, session, seq, scene, notification_count,
                             category, priority, age, persistent, out);
    out->payload_len = DUEL_HOST_PAYLOAD_LEN_M12;
    out->payload[DUEL_HOST_PAYLOAD_CIVIC]     = civic;
    out->payload[DUEL_HOST_PAYLOAD_SECONDARY] = secondary;
    out->crc = duel_crc8(out, offsetof(duel_host_packet_t, crc));
}
#endif

bool duel_host_packet_valid(const duel_host_packet_t *packet) {
    bool common = packet->magic0 == DUEL_HOST_MAGIC0 &&
           packet->magic1 == DUEL_HOST_MAGIC1 &&
           type_valid(packet->type) &&
           packet->payload[0] < DUEL_HOST_SCENE_COUNT &&
           packet->payload[1] <= 15 &&
           packet->crc == duel_crc8(packet, offsetof(duel_host_packet_t, crc));
    if (!common) return false;
    if (packet->version == DUEL_HOST_VERSION_V1) return packet->payload_len == 2;
    if (packet->version != DUEL_HOST_VERSION) return false;
#ifdef ARCANE_M12
    // v2 accepts the legacy 6-byte summary and the M12 8-byte civic summary.
    if (packet->payload_len != 6 && packet->payload_len != DUEL_HOST_PAYLOAD_LEN_M12) {
        return false;
    }
    if (packet->payload_len == DUEL_HOST_PAYLOAD_LEN_M12) {
        // Reserved civic/secondary bits must be zero; secondary within the enum.
        if ((packet->payload[DUEL_HOST_PAYLOAD_CIVIC] & 0xC0u) != 0) return false;
        if ((packet->payload[DUEL_HOST_PAYLOAD_SECONDARY] & 0xF8u) != 0) return false;
        if (DUEL_SECONDARY_ACTIVITY(packet->payload[DUEL_HOST_PAYLOAD_SECONDARY]) >
            DUEL_M12_SECONDARY_CALENDAR) {
            return false;
        }
    }
#else
    if (packet->payload_len != 6) return false;
#endif
    bool empty = packet->payload[1] == 0;
    bool canonical_empty = packet->payload[2] == DUEL_HOST_CATEGORY_NONE &&
                           packet->payload[3] == DUEL_HOST_PRIORITY_NONE &&
                           packet->payload[4] == 0 && packet->payload[5] == 0;
    bool nonempty = packet->payload[2] > DUEL_HOST_CATEGORY_NONE &&
                    packet->payload[2] < DUEL_HOST_CATEGORY_COUNT &&
                    packet->payload[3] > DUEL_HOST_PRIORITY_NONE &&
                    packet->payload[3] < DUEL_HOST_PRIORITY_COUNT &&
                    packet->payload[4] <= 7 && packet->payload[5] <= 1 &&
                    (!packet->payload[5] || packet->payload[3] == DUEL_HOST_PRIORITY_CRITICAL);
    return empty ? canonical_empty : nonempty;
}

static duel_host_result_t stale(duel_host_state_t *state) {
#ifdef ARCANE_DIAGNOSTICS
    sat_inc(&state->stale_packets);
#else
    (void)state;
#endif
    return DUEL_HOST_DROP_STALE;
}

static void apply_context(duel_host_state_t *state, const duel_host_packet_t *packet,
                          bool online) {
    bool persistent = packet->version == DUEL_HOST_VERSION && packet->payload[5] != 0;
    state->external = DUEL_HOST_CONTEXT_PACK(online, packet->payload[0],
                                             packet->payload[1], persistent);
    if (packet->version == DUEL_HOST_VERSION) {
        state->alert = DUEL_HOST_ALERT_PACK(packet->payload[2], packet->payload[3],
                                           packet->payload[4]);
    } else {
        state->alert = 0;
    }
#ifdef ARCANE_M12
    if (packet->version == DUEL_HOST_VERSION &&
        packet->payload_len == DUEL_HOST_PAYLOAD_LEN_M12) {
        state->civic     = packet->payload[DUEL_HOST_PAYLOAD_CIVIC];
        state->secondary = packet->payload[DUEL_HOST_PAYLOAD_SECONDARY];
    } else {
        state->civic     = 0;
        state->secondary = 0;
    }
#endif
}

duel_host_result_t duel_host_accept(duel_host_state_t *state,
                                    const duel_host_packet_t *packet) {
    if (!duel_host_packet_valid(packet)) {
#ifdef ARCANE_DIAGNOSTICS
        sat_inc(&state->malformed_packets);
#endif
        return DUEL_HOST_DROP_MALFORMED;
    }

    if (packet->type == DUEL_HOST_MSG_HELLO) {
        // A session can be adopted only through its unique sequence-zero
        // greeting. Remembering the prior ID prevents a delayed old greeting
        // from rolling a freshly restarted daemon backward.
        if (packet->seq != 0 ||
            ((state->state_flags & DUEL_HOST_STATE_HAVE_SESSION) && packet->session == state->session) ||
            ((state->state_flags & DUEL_HOST_STATE_HAVE_PREVIOUS) && packet->session == state->previous_session)) {
            return stale(state);
        }
        if (state->state_flags & DUEL_HOST_STATE_HAVE_SESSION) {
            state->state_flags |= DUEL_HOST_STATE_HAVE_PREVIOUS;
            state->previous_session = state->session;
        }
        state->state_flags |= DUEL_HOST_STATE_HAVE_SESSION;
        state->session      = packet->session;
        state->last_seq     = 0;
        apply_context(state, packet, true);
        return DUEL_HOST_APPLIED_HEARTBEAT;
    }

    if (!(state->state_flags & DUEL_HOST_STATE_HAVE_SESSION) || packet->session != state->session ||
        (int16_t)(packet->seq - state->last_seq) <= 0) {
        return stale(state);
    }

    state->last_seq = packet->seq;
    bool online = packet->type == DUEL_HOST_MSG_HEARTBEAT ||
                  DUEL_HOST_CONTEXT_ONLINE(state->external);
    apply_context(state, packet, online);
    if (packet->type == DUEL_HOST_MSG_HEARTBEAT) {
        return DUEL_HOST_APPLIED_HEARTBEAT;
    }
    return DUEL_HOST_APPLIED;
}

void duel_host_expire(duel_host_state_t *state) {
    state->external = 0;
    state->alert = 0;
#ifdef ARCANE_M12
    state->civic = 0;
    state->secondary = 0;
#endif
}

uint8_t duel_host_context(const duel_host_state_t *state) {
    return DUEL_HOST_CONTEXT_ONLINE(state->external) ? state->external : 0;
}

uint8_t duel_host_alert(const duel_host_state_t *state) {
    return DUEL_HOST_CONTEXT_ONLINE(state->external) ? state->alert : 0;
}

#ifdef ARCANE_M12
uint8_t duel_host_civic(const duel_host_state_t *state) {
    return DUEL_HOST_CONTEXT_ONLINE(state->external) ? state->civic : 0;
}

uint8_t duel_host_secondary(const duel_host_state_t *state) {
    return DUEL_HOST_CONTEXT_ONLINE(state->external) ? state->secondary : 0;
}
#endif
