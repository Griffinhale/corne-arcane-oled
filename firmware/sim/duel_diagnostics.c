#include "duel_diagnostics.h"

#include <stddef.h>
#include <string.h>

#include "duel_proto.h"

uint32_t duel_diag_housekeeping_work_us(uint32_t total_us, uint32_t split_transport_us) {
    return split_transport_us < total_us ? total_us - split_transport_us : 0u;
}

static void put_u16(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

bool duel_diag_request_valid(const duel_host_diag_packet_t *request) {
    if (request->magic0 != DUEL_HOST_MAGIC0 || request->magic1 != DUEL_HOST_MAGIC1 ||
        request->version != DUEL_HOST_DIAG_VERSION || request->type != DUEL_HOST_MSG_DIAG_REQUEST ||
        request->page >= DUEL_HOST_DIAG_PAGES || request->page_count != 0 ||
        request->crc != duel_crc8(request, offsetof(duel_host_diag_packet_t, crc)))
        return false;
    for (size_t i = 0; i < sizeof request->payload; i++)
        if (request->payload[i] != 0)
            return false;
    return true;
}

void duel_diag_response_pack(const duel_host_diag_packet_t *request,
                             const duel_diag_metrics_t *master, const duel_split_diag_reply_t *peer,
                             bool fixed_split_cadence, duel_host_diag_packet_t *response) {
    memset(response, 0, sizeof *response);
    response->magic0 = DUEL_HOST_MAGIC0;
    response->magic1 = DUEL_HOST_MAGIC1;
    response->version = DUEL_HOST_DIAG_VERSION;
    response->type = DUEL_HOST_MSG_DIAG_RESPONSE;
    response->page = request->page;
    response->page_count = DUEL_HOST_DIAG_PAGES;
    response->nonce = request->nonce;

    if (request->page == 0) {
        put_u16(&response->payload[0], master->queue_overflow);
        put_u16(&response->payload[2], master->catchup_ticks);
        put_u16(&response->payload[4], master->missed_tick_resyncs);
        put_u16(&response->payload[6], master->stale_split_events);
        put_u16(&response->payload[8], master->split_protocol_errors);
        put_u16(&response->payload[10], master->host_malformed_errors);
        put_u16(&response->payload[12], master->host_stale_errors);
        put_u32(&response->payload[14], master->peak_housekeeping_us);
        put_u32(&response->payload[18], master->peak_render_blit_us);
        if (fixed_split_cadence)
            response->payload[22] = DUEL_HOST_DIAG_FLAG_FIXED_SPLIT_CADENCE;
    } else if (request->page == 1) {
        put_u32(&response->payload[0], master->peak_split_tx_us);
        put_u16(&response->payload[4], master->split_tx_success);
        put_u16(&response->payload[6], master->split_tx_failure);
        response->payload[8] =
            peer->magic == DUEL_MAGIC && peer->version == DUEL_SPLIT_DIAG_VERSION;
        put_u16(&response->payload[9], peer->accepted_seq);
        put_u16(&response->payload[11], peer->snapshot_age_ms);
        put_u16(&response->payload[13], peer->peak_housekeeping_us);
        put_u16(&response->payload[15], peer->peak_render_us);
        put_u16(&response->payload[17], peer->queue_overflow);
        put_u16(&response->payload[19], peer->missed_tick_resyncs);
        put_u16(&response->payload[21], peer->stale_events);
    } else if (request->page == 2) {
        put_u16(&response->payload[0], master->stack_min_free_bytes);
        put_u16(&response->payload[2], peer->stack_min_free_bytes);
    }
    response->crc = duel_crc8(response, offsetof(duel_host_diag_packet_t, crc));
}
