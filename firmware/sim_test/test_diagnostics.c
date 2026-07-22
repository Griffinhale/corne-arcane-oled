#include "test_harness.h"

static duel_host_diag_packet_t request_for(uint8_t page) {
    duel_host_diag_packet_t request = {
        .magic0 = DUEL_HOST_MAGIC0,
        .magic1 = DUEL_HOST_MAGIC1,
        .version = DUEL_HOST_DIAG_VERSION,
        .type = DUEL_HOST_MSG_DIAG_REQUEST,
        .page = page,
        .nonce = 0xbeefu,
    };
    request.crc = duel_crc8(&request, offsetof(duel_host_diag_packet_t, crc));
    return request;
}

static void test_housekeeping_excludes_split_transport(void) {
    bool ok = true;
    EXPECT(duel_diag_housekeeping_work_us(4325u, 3240u) == 1085u);
    EXPECT(duel_diag_housekeeping_work_us(999u, 0u) == 999u);
    EXPECT(duel_diag_housekeeping_work_us(100u, 100u) == 0u);
    EXPECT(duel_diag_housekeeping_work_us(100u, 101u) == 0u);
    CHECK(ok, "diagnostic_housekeeping_excludes_split_transport_safely");
}

static void test_diagnostic_request_crc_and_reserved_bytes(void) {
    bool ok = true;
    duel_host_diag_packet_t request = request_for(0);
    static const uint8_t known_request[32] = {
        0xca, 0x8e, 0x02, 0x70, 0x00, 0x00, 0xef, 0xbe, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4c,
    };
    EXPECT(sizeof(duel_host_diag_packet_t) == 32u && sizeof(duel_split_diag_reply_t) == 18u &&
           sizeof(duel_diag_metrics_t) == 36u &&
           memcmp(&request, known_request, sizeof request) == 0 &&
           duel_diag_request_valid(&request));
    for (size_t i = 0; i < offsetof(duel_host_diag_packet_t, crc); i++) {
        duel_host_diag_packet_t corrupt = request;
        ((uint8_t *)&corrupt)[i] ^= 1u;
        EXPECT(!duel_diag_request_valid(&corrupt));
    }
    duel_host_diag_packet_t reserved = request;
    reserved.payload[12] = 1u;
    reserved.crc = duel_crc8(&reserved, offsetof(duel_host_diag_packet_t, crc));
    EXPECT(!duel_diag_request_valid(&reserved));
    CHECK(ok, "diagnostic_v2_request_vector_crc_coverage_reserved_zero_and_struct_sizes");
}

static void test_diagnostic_response_pages(void) {
    bool ok = true;
    const duel_diag_metrics_t master = {
        .queue_overflow = 0x0102u,
        .catchup_ticks = 0x0304u,
        .missed_tick_resyncs = 0x0506u,
        .stale_split_events = 0x0708u,
        .split_protocol_errors = 0x090au,
        .host_malformed_errors = 0x0b0cu,
        .host_stale_errors = 0x0d0eu,
        .peak_housekeeping_us = 0x11121314u,
        .peak_render_blit_us = 0x15161718u,
        .peak_split_tx_us = 0x21222324u,
        .split_tx_success = 0x2526u,
        .split_tx_failure = 0x2728u,
        .stack_min_free_bytes = 0x292au,
    };
    const duel_split_diag_reply_t peer = {
        .magic = DUEL_MAGIC,
        .version = DUEL_SPLIT_DIAG_VERSION,
        .accepted_seq = 0x3132u,
        .snapshot_age_ms = 0x3334u,
        .peak_housekeeping_us = 0x3536u,
        .peak_render_us = 0x3738u,
        .queue_overflow = 0x393au,
        .missed_tick_resyncs = 0x3b3cu,
        .stale_events = 0x3d3eu,
        .stack_min_free_bytes = 0x3f40u,
    };
    static const uint8_t vectors[3][32] = {
        {0xca, 0x8e, 0x02, 0x71, 0x00, 0x03, 0xef, 0xbe, 0x02, 0x01, 0x04,
         0x03, 0x06, 0x05, 0x08, 0x07, 0x0a, 0x09, 0x0c, 0x0b, 0x0e, 0x0d,
         0x14, 0x13, 0x12, 0x11, 0x18, 0x17, 0x16, 0x15, 0x01, 0x4d},
        {0xca, 0x8e, 0x02, 0x71, 0x01, 0x03, 0xef, 0xbe, 0x24, 0x23, 0x22,
         0x21, 0x26, 0x25, 0x28, 0x27, 0x01, 0x32, 0x31, 0x34, 0x33, 0x36,
         0x35, 0x38, 0x37, 0x3a, 0x39, 0x3c, 0x3b, 0x3e, 0x3d, 0xf8},
        {0xca, 0x8e, 0x02, 0x71, 0x02, 0x03, 0xef, 0xbe, 0x2a, 0x29, 0x40,
         0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc5},
    };
    for (uint8_t page = 0; page < DUEL_HOST_DIAG_PAGES; page++) {
        duel_host_diag_packet_t request = request_for(page);
        duel_host_diag_packet_t response;
        duel_diag_response_pack(&request, &master, &peer, true, &response);
        EXPECT(memcmp(&response, vectors[page], sizeof response) == 0 &&
               response.crc == duel_crc8(&response, offsetof(duel_host_diag_packet_t, crc)));
    }
    duel_host_diag_packet_t request = request_for(2);
    duel_host_diag_packet_t response;
    duel_diag_response_pack(&request, &master, &peer, false, &response);
    for (size_t i = 4; i < sizeof response.payload; i++)
        EXPECT(response.payload[i] == 0u);
    EXPECT(response.payload[0] == 0x2au && response.payload[1] == 0x29u &&
           response.payload[2] == 0x40u && response.payload[3] == 0x3fu);
    CHECK(ok, "diagnostic_v2_three_page_vectors_reserved_zero_and_stack_propagation");
}

void run_diagnostics_tests(void) {
    test_housekeeping_excludes_split_transport();
    test_diagnostic_request_crc_and_reserved_bytes();
    test_diagnostic_response_pages();
}
