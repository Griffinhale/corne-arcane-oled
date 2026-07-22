#include <stddef.h>
#include <string.h>

#include "test_support.h"

void test_encode_snapshot(const sim_world_t *world, uint8_t session, uint16_t sequence,
                          duel_snapshot_t *out) {
    duel_encode_external_alert_display(world, session, sequence, 0, 0, 0, out);
}

void test_build_host_packet(uint8_t type, uint32_t session, uint16_t sequence,
                            uint8_t scene, uint8_t notification_count,
                            uint8_t category, uint8_t priority, uint8_t age,
                            bool persistent, uint8_t civic, uint8_t secondary,
                            duel_host_packet_t *out) {
    memset(out, 0, sizeof *out);
    out->magic0 = DUEL_HOST_MAGIC0;
    out->magic1 = DUEL_HOST_MAGIC1;
    out->version = DUEL_HOST_VERSION;
    out->type = type;
    out->session = session;
    out->seq = sequence;
    out->payload_len = DUEL_HOST_PAYLOAD_LEN;
    out->payload[0] = scene;
    out->payload[1] = notification_count;
    out->payload[2] = category;
    out->payload[3] = priority;
    out->payload[4] = age;
    out->payload[5] = persistent ? 1 : 0;
    out->payload[DUEL_HOST_PAYLOAD_CIVIC] = civic;
    out->payload[DUEL_HOST_PAYLOAD_SECONDARY] = secondary;
    out->crc = duel_crc8(out, offsetof(duel_host_packet_t, crc));
}
