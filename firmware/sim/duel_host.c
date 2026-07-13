#include <string.h>

#include "duel_host.h"
#include "duel_proto.h"

static void sat_inc(uint16_t *value) {
    if (*value != UINT16_MAX) (*value)++;
}

static bool type_valid(uint8_t type) {
    return type == DUEL_HOST_MSG_HELLO || type == DUEL_HOST_MSG_HEARTBEAT ||
           type == DUEL_HOST_MSG_NOTIFY;
}

void duel_host_encode(uint8_t type, uint32_t session, uint16_t seq,
                      uint8_t scene, uint8_t notification_count,
                      duel_host_packet_t *out) {
    memset(out, 0, sizeof *out);
    out->magic0      = DUEL_HOST_MAGIC0;
    out->magic1      = DUEL_HOST_MAGIC1;
    out->version     = DUEL_HOST_VERSION;
    out->type        = type;
    out->session     = session;
    out->seq         = seq;
    out->payload_len = 2;
    out->payload[0]  = scene;
    out->payload[1]  = notification_count;
    out->crc         = duel_crc8(out, offsetof(duel_host_packet_t, crc));
}

bool duel_host_packet_valid(const duel_host_packet_t *packet) {
    return packet->magic0 == DUEL_HOST_MAGIC0 &&
           packet->magic1 == DUEL_HOST_MAGIC1 &&
           packet->version == DUEL_HOST_VERSION &&
           type_valid(packet->type) && packet->payload_len == 2 &&
           packet->payload[0] < DUEL_HOST_SCENE_COUNT &&
           packet->payload[1] <= 15 &&
           packet->crc == duel_crc8(packet, offsetof(duel_host_packet_t, crc));
}

static duel_host_result_t stale(duel_host_state_t *state) {
    sat_inc(&state->stale_packets);
    return DUEL_HOST_DROP_STALE;
}

static void apply_context(duel_host_state_t *state, const duel_host_packet_t *packet) {
    state->scene              = packet->payload[0];
    state->notification_count = packet->payload[1];
}

duel_host_result_t duel_host_accept(duel_host_state_t *state,
                                    const duel_host_packet_t *packet) {
    if (!duel_host_packet_valid(packet)) {
        sat_inc(&state->malformed_packets);
        return DUEL_HOST_DROP_MALFORMED;
    }

    if (packet->type == DUEL_HOST_MSG_HELLO) {
        // A session can be adopted only through its unique sequence-zero
        // greeting. Remembering the prior ID prevents a delayed old greeting
        // from rolling a freshly restarted daemon backward.
        if (packet->seq != 0 ||
            (state->have_session && packet->session == state->session) ||
            (state->have_previous && packet->session == state->previous_session)) {
            return stale(state);
        }
        if (state->have_session) {
            state->have_previous   = true;
            state->previous_session = state->session;
        }
        state->have_session = true;
        state->session      = packet->session;
        state->last_seq     = 0;
        apply_context(state, packet);
        state->online = true;
        return DUEL_HOST_APPLIED_HEARTBEAT;
    }

    if (!state->have_session || packet->session != state->session ||
        (int16_t)(packet->seq - state->last_seq) <= 0) {
        return stale(state);
    }

    state->last_seq = packet->seq;
    apply_context(state, packet);
    if (packet->type == DUEL_HOST_MSG_HEARTBEAT) {
        state->online = true;
        return DUEL_HOST_APPLIED_HEARTBEAT;
    }
    return DUEL_HOST_APPLIED;
}

void duel_host_expire(duel_host_state_t *state) {
    state->online             = false;
    state->scene              = DUEL_HOST_SCENE_DUEL;
    state->notification_count = 0;
}

uint8_t duel_host_context(const duel_host_state_t *state) {
    return DUEL_HOST_CONTEXT_PACK(state->online, state->scene,
                                  state->notification_count);
}
