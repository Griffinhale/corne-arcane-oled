/* Pure diagnostics-only wire packing shared by QMK glue and native tests. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "duel_host.h"

#define DUEL_HOST_DIAG_VERSION  2
#define DUEL_HOST_DIAG_PAGES    3
#define DUEL_SPLIT_DIAG_VERSION 2

enum {
    DUEL_HOST_MSG_DIAG_REQUEST = 0x70,
    DUEL_HOST_MSG_DIAG_RESPONSE = 0x71,
};

enum {
    DUEL_HOST_DIAG_FLAG_FIXED_SPLIT_CADENCE = 0x01,
};

typedef struct __attribute__((packed)) {
    uint8_t magic0;
    uint8_t magic1;
    uint8_t version;
    uint8_t type;
    uint8_t page;
    uint8_t page_count;
    uint16_t nonce;
    uint8_t payload[23];
    uint8_t crc;
} duel_host_diag_packet_t;

_Static_assert(sizeof(duel_host_diag_packet_t) == DUEL_HOST_REPORT_SIZE,
               "diagnostic report must match QMK RAW_EPSIZE");

typedef struct __attribute__((packed)) {
    uint8_t magic;
    uint8_t version;
    uint16_t accepted_seq;
    uint16_t snapshot_age_ms;
    uint16_t peak_housekeeping_us;
    uint16_t peak_render_us;
    uint16_t queue_overflow;
    uint16_t missed_tick_resyncs;
    uint16_t stale_events;
    uint16_t stack_min_free_bytes;
} duel_split_diag_reply_t;

_Static_assert(sizeof(duel_split_diag_reply_t) == 18,
               "diagnostic split response must remain fixed at 18 bytes");
_Static_assert(sizeof(duel_split_diag_reply_t) <= 32,
               "diagnostic split response must fit QMK reverse RPC capacity");

typedef struct {
    uint16_t queue_overflow;
    uint16_t catchup_ticks;
    uint16_t missed_tick_resyncs;
    uint16_t stale_split_events;
    uint16_t split_protocol_errors;
    uint16_t host_malformed_errors;
    uint16_t host_stale_errors;
    uint32_t peak_housekeeping_us;
    uint32_t peak_render_blit_us;
    uint32_t peak_split_tx_us;
    uint16_t split_tx_success;
    uint16_t split_tx_failure;
    uint16_t stack_min_free_bytes;
} duel_diag_metrics_t;

_Static_assert(sizeof(duel_diag_metrics_t) == 36,
               "diagnostic master metrics layout changed unexpectedly");

bool duel_diag_request_valid(const duel_host_diag_packet_t *request);
void duel_diag_response_pack(const duel_host_diag_packet_t *request,
                             const duel_diag_metrics_t *master, const duel_split_diag_reply_t *peer,
                             bool fixed_split_cadence, duel_host_diag_packet_t *response);
