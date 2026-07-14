/*
Copyright 2019 @foostan
Copyright 2020 Drashna Jaelre <@drashna>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include QMK_KEYBOARD_H

#include "corne_arcane_layout.h"


#ifdef OLED_ENABLE

#include <string.h>

#ifdef ARCANE_DIAGNOSTICS
#    pragma push_macro("TIMER")
#    undef TIMER
#    include "hardware/timer.h"
#    pragma pop_macro("TIMER")
#endif

#include "transactions.h"

#include "sim/duel_display.h"
#include "sim/duel_draw.h"
#include "sim/duel_host.h"
#include "sim/duel_proto.h"
#include "sim/duel_sim.h"

// Portrait canvas must match the rotated OLED exactly (see duel_draw.h).
_Static_assert(DUEL_CANVAS_W == OLED_DISPLAY_HEIGHT && DUEL_CANVAS_H == OLED_DISPLAY_WIDTH,
               "duel canvas dimensions must match the rotated OLED");

// crkbd/rev1: 8 matrix rows, 4 per hand (left = 0..3, right = 4..7).
#define DUEL_ROWS_PER_HAND (MATRIX_ROWS / 2)

static duel_display_policy_t duel_display;
static uint32_t duel_local_wake_until_ms;
static bool duel_render_invalid = true;

#ifdef ARCANE_DIAGNOSTICS
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
} duel_diag_t;

// Intentionally non-static: a debugger or map-file budget pass can inspect it
// without adding release protocol traffic or changing presentation behavior.
volatile duel_diag_t duel_diag;
volatile duel_split_diag_reply_t duel_peer_diag;
static duel_split_diag_reply_t duel_diag_response;
static volatile uint8_t duel_diag_response_ver;

static void duel_diag_peak(volatile uint32_t *peak, uint32_t elapsed) {
    if (elapsed > *peak) *peak = elapsed;
}

static uint16_t duel_diag_u16(uint32_t value) {
    return value > UINT16_MAX ? UINT16_MAX : (uint16_t)value;
}
#endif

static void duel_note_physical_key(void) {
    uint32_t now = timer_read32();
    duel_display_note_key(&duel_display, now);
    duel_local_wake_until_ms = now + 120u;
    duel_render_invalid = true;
}

// Treat both OLEDs as VERTICAL (portrait). On this build both panels are mounted
// the same way, so both halves use the same rotation to stand upright. If one
// half ever appears upside-down, flip only that half's constant (270 <-> 90).
#define WIZ_ROT_MASTER  OLED_ROTATION_270
#define WIZ_ROT_OFFHAND OLED_ROTATION_270
oled_rotation_t oled_init_user(oled_rotation_t rotation) {
    return is_keyboard_master() ? WIZ_ROT_MASTER : WIZ_ROT_OFFHAND;
}

/* ---- event capture (scan hooks) -----------------------------------------
 * The key path never waits: the hooks below only XOR the current matrix rows
 * against the previous pass and append compact key-down events to a bounded
 * queue. Releases remain level-sampled. No rendering, allocation, or split
 * work occurs here. The master's matrix
 * already contains the slave's rows (merged in matrix_post_scan before
 * matrix_scan_kb fires — quantum/matrix_common.c), so the master captures
 * both halves; the slave captures only its own. */
static sim_evq_t    duel_evq;
static matrix_row_t duel_rows[MATRIX_ROWS];

static void duel_scan_rows(uint8_t row_first, uint8_t row_last, uint8_t side) {
    for (uint8_t r = row_first; r <= row_last; r++) {
        matrix_row_t cur  = matrix_get_row(r);
        matrix_row_t diff = cur ^ duel_rows[r];
        duel_rows[r] = cur;
        if (!diff) continue;
        for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            if (!(diff & ((matrix_row_t)1 << c))) continue;
            if (!(cur & ((matrix_row_t)1 << c))) continue;
            sim_evq_push(&duel_evq, SIM_EV_PACK(SIM_EV_KEYDOWN, side,
                                                (uint8_t)(r % DUEL_ROWS_PER_HAND), c));
            duel_note_physical_key();
        }
    }
}

void matrix_scan_user(void) {
    duel_scan_rows(0, DUEL_ROWS_PER_HAND - 1, SIM_SIDE_L);
    duel_scan_rows(DUEL_ROWS_PER_HAND, MATRIX_ROWS - 1, SIM_SIDE_R);
}

void matrix_slave_scan_user(void) {
    uint8_t off = is_keyboard_left() ? 0 : DUEL_ROWS_PER_HAND;
    duel_scan_rows(off, off + DUEL_ROWS_PER_HAND - 1, is_keyboard_left() ? SIM_SIDE_L : SIM_SIDE_R);
}

/* ---- split snapshot sync (M3) --------------------------------------------
 * The master's world is authoritative; it streams 27-byte snapshots to the
 * slave every 2nd tick (12.5 Hz). The slave renders the last accepted packet
 * and can never be rolled backward (see duel_rx_accept). If snapshots stop
 * for DUEL_STALE_MS the slave shows a broken-link glyph and falls back to
 * its own local, non-authoritative sim so its wizard still reacts to typing.
 *
 * The receive callback runs in the serial driver's HIGHPRIO thread
 * (platforms/chibios/drivers/serial_protocol.c), NOT the main loop — it must
 * stay a guarded memcpy and nothing more. The seqlock (odd version = write
 * in progress) lets housekeeping take a consistent copy without locking. */
#define DUEL_STALE_MS 500

/* ---- M8 disposable host context -----------------------------------------
 * Only griffin_hostoled defines ARCANE_HOST_ENABLE. The Vial build compiles
 * the protocol tests/core but never claims Vial's Raw HID callback.
 *
 * The USB callback remains bounded: one 32-byte seqlock-guarded copy. Parsing,
 * ordering, expiry, and split propagation happen later in housekeeping. The
 * daemon sends absolute context on every report, so latest-wins is safe. */
#ifdef ARCANE_HOST_ENABLE
#    include "raw_hid.h"
#    define DUEL_HOST_TIMEOUT_MS 1500

static volatile uint8_t duel_host_rx_ver;
static duel_host_packet_t duel_host_rx_staging;
static uint8_t duel_host_rx_seen_ver;
static duel_host_state_t duel_host_state;
static uint32_t duel_host_expire_ms;
#ifdef ARCANE_DIAGNOSTICS
static volatile uint8_t duel_diag_usb_rx_ver;
static duel_host_diag_packet_t duel_diag_usb_rx_staging;
static uint8_t duel_diag_usb_rx_seen_ver;
#endif

void raw_hid_receive(uint8_t *data, uint8_t length) {
    if (length != sizeof(duel_host_packet_t)) return;
#ifdef ARCANE_DIAGNOSTICS
    if (data[0] == DUEL_HOST_MAGIC0 && data[1] == DUEL_HOST_MAGIC1 &&
        data[2] == DUEL_HOST_DIAG_VERSION && data[3] == DUEL_HOST_MSG_DIAG_REQUEST) {
        duel_diag_usb_rx_ver++;
        __asm__ volatile("" ::: "memory");
        memcpy(&duel_diag_usb_rx_staging, data, sizeof duel_diag_usb_rx_staging);
        __asm__ volatile("" ::: "memory");
        duel_diag_usb_rx_ver++;
        return;
    }
#endif
    duel_host_rx_ver++;
    __asm__ volatile("" ::: "memory");
    memcpy(&duel_host_rx_staging, data, sizeof duel_host_rx_staging);
    __asm__ volatile("" ::: "memory");
    duel_host_rx_ver++;
}

static bool duel_host_rx_consume(uint32_t now) {
    uint8_t v1 = duel_host_rx_ver;
    if (v1 == duel_host_rx_seen_ver || (v1 & 1)) return false;
    __asm__ volatile("" ::: "memory");
    duel_host_packet_t packet = duel_host_rx_staging;
    __asm__ volatile("" ::: "memory");
    uint8_t v2 = duel_host_rx_ver;
    if (v1 != v2) return false;
    duel_host_rx_seen_ver = v1;
    uint8_t before_context = duel_host_context(&duel_host_state);
    uint8_t before_alert   = duel_host_alert(&duel_host_state);
    duel_host_result_t result = duel_host_accept(&duel_host_state, &packet);
    if (result == DUEL_HOST_APPLIED_HEARTBEAT) {
        duel_host_expire_ms = now + DUEL_HOST_TIMEOUT_MS;
    }
    return before_context != duel_host_context(&duel_host_state) ||
           before_alert != duel_host_alert(&duel_host_state);
}

static bool duel_host_housekeeping(uint32_t now) {
    bool visible_changed = duel_host_rx_consume(now);
    if (duel_host_expire_ms && timer_expired32(now, duel_host_expire_ms)) {
        uint8_t before_context = duel_host_context(&duel_host_state);
        uint8_t before_alert   = duel_host_alert(&duel_host_state);
        duel_host_expire(&duel_host_state);
        duel_host_expire_ms = 0;
        visible_changed |= before_context != duel_host_context(&duel_host_state) ||
                           before_alert != duel_host_alert(&duel_host_state);
    }
    return visible_changed;
}

#ifdef ARCANE_DIAGNOSTICS
static void duel_diag_put_u16(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

static void duel_diag_put_u32(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

static bool duel_diag_usb_request_consume(duel_host_diag_packet_t *request) {
    uint8_t v1 = duel_diag_usb_rx_ver;
    if (v1 == duel_diag_usb_rx_seen_ver || (v1 & 1u)) return false;
    __asm__ volatile("" ::: "memory");
    memcpy(request, &duel_diag_usb_rx_staging, sizeof *request);
    __asm__ volatile("" ::: "memory");
    uint8_t v2 = duel_diag_usb_rx_ver;
    if (v1 != v2) return false;
    duel_diag_usb_rx_seen_ver = v1;
    return request->magic0 == DUEL_HOST_MAGIC0 &&
           request->magic1 == DUEL_HOST_MAGIC1 &&
           request->version == DUEL_HOST_DIAG_VERSION &&
           request->type == DUEL_HOST_MSG_DIAG_REQUEST &&
           request->page < DUEL_HOST_DIAG_PAGES &&
           request->page_count == 0 &&
           request->crc == duel_crc8(request, offsetof(duel_host_diag_packet_t, crc));
}

static void duel_diag_usb_respond(const duel_host_diag_packet_t *request) {
    duel_host_diag_packet_t response = {0};
    response.magic0 = DUEL_HOST_MAGIC0;
    response.magic1 = DUEL_HOST_MAGIC1;
    response.version = DUEL_HOST_DIAG_VERSION;
    response.type = DUEL_HOST_MSG_DIAG_RESPONSE;
    response.page = request->page;
    response.page_count = DUEL_HOST_DIAG_PAGES;
    response.nonce = request->nonce;

    if (request->page == 0) {
        duel_diag_put_u16(&response.payload[0], duel_diag.queue_overflow);
        duel_diag_put_u16(&response.payload[2], duel_diag.catchup_ticks);
        duel_diag_put_u16(&response.payload[4], duel_diag.missed_tick_resyncs);
        duel_diag_put_u16(&response.payload[6], duel_diag.stale_split_events);
        duel_diag_put_u16(&response.payload[8], duel_diag.split_protocol_errors);
        duel_diag_put_u16(&response.payload[10], duel_diag.host_malformed_errors);
        duel_diag_put_u16(&response.payload[12], duel_diag.host_stale_errors);
        duel_diag_put_u32(&response.payload[14], duel_diag.peak_housekeeping_us);
        duel_diag_put_u32(&response.payload[18], duel_diag.peak_render_blit_us);
#ifdef ARCANE_FIXED_SPLIT_CADENCE
        response.payload[22] = DUEL_HOST_DIAG_FLAG_FIXED_SPLIT_CADENCE;
#endif
    } else {
        duel_split_diag_reply_t peer = duel_peer_diag;
        duel_diag_put_u32(&response.payload[0], duel_diag.peak_split_tx_us);
        duel_diag_put_u16(&response.payload[4], duel_diag.split_tx_success);
        duel_diag_put_u16(&response.payload[6], duel_diag.split_tx_failure);
        response.payload[8] = peer.magic == DUEL_MAGIC && peer.version == 1;
        duel_diag_put_u16(&response.payload[9], peer.accepted_seq);
        duel_diag_put_u16(&response.payload[11], peer.snapshot_age_ms);
        duel_diag_put_u16(&response.payload[13], peer.peak_housekeeping_us);
        duel_diag_put_u16(&response.payload[15], peer.peak_render_us);
        duel_diag_put_u16(&response.payload[17], peer.queue_overflow);
        duel_diag_put_u16(&response.payload[19], peer.missed_tick_resyncs);
        duel_diag_put_u16(&response.payload[21], peer.stale_events);
    }
    response.crc = duel_crc8(&response, offsetof(duel_host_diag_packet_t, crc));
    raw_hid_send((uint8_t *)&response, sizeof response);
}
#endif
#endif

static volatile uint8_t duel_rx_ver;
static duel_snapshot_t  duel_rx_staging;
static uint32_t         duel_last_pkt_ms;

static void duel_snapshot_rx(uint8_t in_len, const void *in, uint8_t out_len, void *out) {
    if (in_len != sizeof(duel_snapshot_t)) return;
    duel_rx_ver++;
    __asm__ volatile("" ::: "memory");
    memcpy(&duel_rx_staging, in, sizeof duel_rx_staging);
    __asm__ volatile("" ::: "memory");
    duel_rx_ver++;
#ifdef ARCANE_DIAGNOSTICS
    if (out_len == sizeof(duel_diag_response) && out != NULL) {
        uint8_t v1 = duel_diag_response_ver;
        if (v1 & 1u) {
            memset(out, 0, sizeof duel_diag_response);
        } else {
            memcpy(out, &duel_diag_response, sizeof duel_diag_response);
            __asm__ volatile("" ::: "memory");
            if (v1 != duel_diag_response_ver) memset(out, 0, sizeof duel_diag_response);
        }
    }
#else
    (void)out_len;
    (void)out;
#endif
}

void keyboard_post_init_user(void) {
    transaction_register_rpc(DUEL_SYNC_SNAPSHOT, duel_snapshot_rx);
}

/* ---- deterministic tick loop --------------------------------------------
 * Fixed 25 Hz integer tick in housekeeping (runs on both halves every main
 * loop pass). Wall time never reaches the sim: it only decides HOW MANY
 * ticks to run. Render reads only the copied snapshot, so OLED cadence
 * cannot change outcomes. */
static sim_world_t   duel_world;
static duel_render_t duel_render; // presentation reads ONLY this
static uint32_t      duel_next_tick_ms;
static bool          duel_tick_armed;

/* ---- M7 layer-key scry chord: physical-position detection -----------------
 * The chord is detected from raw matrix positions, NOT emitted keycodes or the
 * active layer, so it is immune to whatever Vial maps onto these keys. On
 * crkbd's LAYOUT_split_3x6_3 the two momentary layer thumbs are the middle
 * thumb of each hand: MO(1) at matrix (3,4) on the left, MO(2) at (7,4) on the
 * right (see keyboards/crkbd/info.json). Only the master's merged matrix holds
 * both, which is exactly why the chord machine is authoritative-only. */
#define SCRY_KEY_L_ROW 3
#define SCRY_KEY_L_COL 4
#define SCRY_KEY_R_ROW 7
#define SCRY_KEY_R_COL 4

static uint8_t duel_sample_scry(void) {
    uint8_t mask = 0;
    if (duel_rows[SCRY_KEY_L_ROW] & ((matrix_row_t)1 << SCRY_KEY_L_COL)) mask |= SCRY_M_L;
    if (duel_rows[SCRY_KEY_R_ROW] & ((matrix_row_t)1 << SCRY_KEY_R_COL)) mask |= SCRY_M_R;
    // Any key held that is NOT one of the two layer keys disqualifies a chord
    // (this is ordinary layer-3 use) and, once the overlay is up, drives scene
    // selection — so mask it in level-sampled form like the rest of the inputs.
    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        matrix_row_t row = duel_rows[r];
        if (r == SCRY_KEY_L_ROW) row &= (matrix_row_t)~((matrix_row_t)1 << SCRY_KEY_L_COL);
        if (r == SCRY_KEY_R_ROW) row &= (matrix_row_t)~((matrix_row_t)1 << SCRY_KEY_R_COL);
        if (row) { mask |= SCRY_M_OTHER; break; }
    }
    return mask;
}

static sim_inputs_t duel_sample_inputs(void) {
    sim_inputs_t in = {0};
    for (uint8_t r = 0; r < DUEL_ROWS_PER_HAND; r++) {
        if (duel_rows[r]) { in.down_mask |= 1 << SIM_SIDE_L; break; }
    }
    for (uint8_t r = DUEL_ROWS_PER_HAND; r < MATRIX_ROWS; r++) {
        if (duel_rows[r]) { in.down_mask |= 1 << SIM_SIDE_R; break; }
    }
    in.scry_mask = duel_sample_scry();
    return in;
}

// Master -> slave snapshot stream.
static uint8_t  duel_session;
static bool     duel_session_set;
static uint16_t duel_tx_seq;
static uint8_t  duel_fx_sent;
static uint32_t duel_last_tx_ms;
static duel_snapshot_t duel_last_tx;
static bool duel_have_tx;

#define DUEL_ACTIVE_TX_MS 80u
#ifdef ARCANE_FIXED_SPLIT_CADENCE
#    define DUEL_REPAIR_TX_MS DUEL_ACTIVE_TX_MS
#else
#    define DUEL_REPAIR_TX_MS 250u
#endif

static void duel_master_tx(bool urgent) {
    bool fx_changed = duel_world.fx_seq != duel_fx_sent;
    if (!duel_session_set) {
        // Boot nonce: USB-enumeration timing jitter is the entropy; the
        // slave's stale override backstops the rare collision.
        uint32_t t0     = timer_read32();
        duel_session    = (uint8_t)(t0 ^ (t0 >> 8));
        duel_session_set = true;
    }
    duel_snapshot_t pkt;
#ifdef ARCANE_HOST_ENABLE
    uint8_t external = duel_host_context(&duel_host_state);
    uint8_t alert = duel_host_alert(&duel_host_state);
#else
    uint8_t external = 0;
    uint8_t alert = 0;
#endif
    duel_encode_external_alert_display(&duel_world, duel_session, ++duel_tx_seq,
                                       external, alert, duel_display.phase, &pkt);
    bool semantic_changed = !duel_have_tx ||
                            memcmp(&pkt.view, &duel_last_tx.view, sizeof pkt.view) != 0 ||
                            pkt.external != duel_last_tx.external ||
                            pkt.alert != duel_last_tx.alert ||
                            pkt.flags != duel_last_tx.flags;
    uint32_t since_tx = timer_elapsed32(duel_last_tx_ms);
    if (!urgent && !fx_changed) {
        if (semantic_changed && duel_have_tx && since_tx < DUEL_ACTIVE_TX_MS) return;
        if (!semantic_changed && duel_have_tx && since_tx < DUEL_REPAIR_TX_MS) return;
    }

#ifdef ARCANE_DIAGNOSTICS
    uint32_t tx_start_us = time_us_32();
    duel_split_diag_reply_t peer = {0};
    bool sent = transaction_rpc_exec(DUEL_SYNC_SNAPSHOT, sizeof pkt, &pkt,
                                     sizeof peer, &peer);
    duel_diag_peak(&duel_diag.peak_split_tx_us, time_us_32() - tx_start_us);
    if (sent) {
        if (duel_diag.split_tx_success < UINT16_MAX) duel_diag.split_tx_success++;
        duel_peer_diag = peer;
    } else if (duel_diag.split_tx_failure < UINT16_MAX) {
        duel_diag.split_tx_failure++;
    }
#else
    bool sent = transaction_rpc_send(DUEL_SYNC_SNAPSHOT, sizeof pkt, &pkt);
#endif
    if (sent) {
        duel_fx_sent = duel_world.fx_seq;
        duel_last_tx_ms = timer_read32();
        duel_last_tx = pkt;
        duel_have_tx = true;
    }
}

// Slave: consistent seqlock read of the latest packet, then accept/reject.
static duel_rx_state_t duel_rx;
static uint8_t         duel_rx_seen_ver;

static bool duel_slave_rx_consume(void) {
    uint8_t v1 = duel_rx_ver;
    if (v1 == duel_rx_seen_ver || (v1 & 1)) return false;
    __asm__ volatile("" ::: "memory");
    duel_snapshot_t pkt;
    memcpy(&pkt, &duel_rx_staging, sizeof pkt);
    __asm__ volatile("" ::: "memory");
    uint8_t v2 = duel_rx_ver;
    if (v1 != v2) return false; // torn; retry the stable version next pass
    duel_rx_seen_ver = v1;
    if (!duel_decode_valid(&pkt)) {
#ifdef ARCANE_DIAGNOSTICS
        if (duel_diag.split_protocol_errors < UINT16_MAX) duel_diag.split_protocol_errors++;
#endif
        return false;
    }
    bool stale = timer_elapsed32(duel_last_pkt_ms) > DUEL_STALE_MS;
    if (duel_rx_accept(&duel_rx, &pkt, stale)) {
        duel_last_pkt_ms = timer_read32();
        return true;
    }
    return false;
}

static void duel_render_set_external(uint8_t external, uint8_t alert) {
    duel_render.external = external;
    duel_render.alert = alert;
}

void housekeeping_task_user(void) {
#ifdef ARCANE_DIAGNOSTICS
    uint32_t diag_start_us = time_us_32();
#endif
    uint32_t now = timer_read32();
    bool host_changed = false;
#ifdef ARCANE_HOST_ENABLE
    if (is_keyboard_master()) host_changed = duel_host_housekeeping(now);
#endif
    if (!duel_tick_armed) {
        sim_init(&duel_world, is_keyboard_master() ? SIMF_AUTHORITATIVE : 0, 0);
        duel_display_init(&duel_display, now);
        duel_next_tick_ms = now + SIM_TICK_MS;
        duel_tick_armed   = true;
    }
    duel_display_phase_t prior_display = duel_display.phase;
    if (is_keyboard_master()) duel_display_update(&duel_display, now);
    bool display_changed = duel_display.phase != prior_display;
    bool    ticked = false;
    uint8_t guard  = 0;
    sim_inputs_t inputs = {0};
    uint8_t queued = 0;
    uint8_t dropped = 0;
    if (timer_expired32(now, duel_next_tick_ms)) {
        inputs = duel_sample_inputs();
        queued = duel_evq.n;
        dropped = duel_evq.dropped;
    }
    while (timer_expired32(now, duel_next_tick_ms)) {
        sim_tick(&duel_world, inputs, duel_evq.ev, queued, dropped);
        duel_evq.n = 0;
        duel_evq.dropped = 0;
        queued = 0;
        dropped = 0;
        ticked = true;
        duel_next_tick_ms += SIM_TICK_MS;
        if (++guard >= 5) { // long stall (USB suspend): resync instead of replaying
#ifdef ARCANE_DIAGNOSTICS
            if (duel_diag.missed_tick_resyncs < UINT16_MAX) duel_diag.missed_tick_resyncs++;
#endif
            duel_next_tick_ms = now + SIM_TICK_MS;
            break;
        }
    }
#ifdef ARCANE_DIAGNOSTICS
    if (guard > 1) {
        uint16_t add = (uint16_t)(guard - 1);
        duel_diag.catchup_ticks = UINT16_MAX - duel_diag.catchup_ticks < add
                                      ? UINT16_MAX : (uint16_t)(duel_diag.catchup_ticks + add);
    }
    duel_diag.queue_overflow = duel_world.overflow_count;
#    ifdef ARCANE_HOST_ENABLE
    duel_diag.host_malformed_errors = duel_host_state.malformed_packets;
    duel_diag.host_stale_errors = duel_host_state.stale_packets;
#    endif
#endif

    bool render_invalid = duel_render_invalid;
    duel_render_invalid = false;
    if (is_keyboard_master()) {
        if (ticked || display_changed || host_changed)
            duel_master_tx(display_changed || host_changed);
        if (ticked || display_changed || host_changed || render_invalid) {
            duel_render_from_world(&duel_render, &duel_world);
            duel_render.flags &= (uint8_t)~DUEL_RENDER_STALE;
#ifdef ARCANE_HOST_ENABLE
            duel_render_set_external(duel_host_context(&duel_host_state),
                                     duel_host_alert(&duel_host_state));
#else
            duel_render_set_external(0, 0);
#endif
        }
    } else {
        bool accepted = duel_slave_rx_consume();
        bool stale = timer_elapsed32(duel_last_pkt_ms) > DUEL_STALE_MS;
        bool stale_edge = stale != ((duel_render.flags & DUEL_RENDER_STALE) != 0);
        static bool using_remote;
#ifdef ARCANE_DIAGNOSTICS
        static bool diag_was_stale;
        if (stale && !diag_was_stale && duel_diag.stale_split_events < UINT16_MAX)
            duel_diag.stale_split_events++;
        diag_was_stale = stale;
#endif
        if (!stale && duel_rx.have_any) {
            if (accepted || !using_remote) {
                duel_display_phase_t before_follow = duel_display.phase;
                uint8_t remote_phase = DUEL_FLAGS_DISPLAY(duel_rx.last.flags);
                bool local_wake_grace = duel_local_wake_until_ms &&
                                        !timer_expired32(now, duel_local_wake_until_ms);
                if (!local_wake_grace) duel_local_wake_until_ms = 0;
                if (!local_wake_grace && remote_phase <= DUEL_DISPLAY_SLEEP)
                    duel_display_follow(&duel_display, (duel_display_phase_t)remote_phase, now);
                display_changed |= duel_display.phase != before_follow;
            }
            if (accepted || !using_remote || stale_edge || display_changed || render_invalid) {
                duel_render.view = duel_rx.last.view;
                duel_render_set_external(duel_rx.last.external, duel_rx.last.alert);
                duel_render.flags &= (uint8_t)~DUEL_RENDER_STALE;
            }
            using_remote = true;
        } else {
            // Local pose-only fallback: never authoritative, never combat.
            duel_display_phase_t before_update = duel_display.phase;
            duel_display_update(&duel_display, now);
            display_changed |= duel_display.phase != before_update;
            if (ticked || using_remote || stale_edge || display_changed || render_invalid) {
                duel_render_from_world(&duel_render, &duel_world);
                duel_render_set_external(0, 0);
                if (stale) duel_render.flags |= DUEL_RENDER_STALE;
                else duel_render.flags &= (uint8_t)~DUEL_RENDER_STALE;
            }
            using_remote = false;
        }
    }
#ifdef ARCANE_DIAGNOSTICS
    duel_render.diag_tick = (uint8_t)(duel_world.tick % 25u);
    duel_render.diag_overflow = duel_world.overflow_count;
    duel_diag_peak(&duel_diag.peak_housekeeping_us, time_us_32() - diag_start_us);
    if (!is_keyboard_master()) {
        duel_diag_response_ver++;
        __asm__ volatile("" ::: "memory");
        duel_diag_response = (duel_split_diag_reply_t){
            .magic = DUEL_MAGIC,
            .version = 1,
            .accepted_seq = duel_rx.have_any ? duel_rx.last.seq : 0,
            .snapshot_age_ms = duel_diag_u16(timer_elapsed32(duel_last_pkt_ms)),
            .peak_housekeeping_us = duel_diag_u16(duel_diag.peak_housekeeping_us),
            .peak_render_us = duel_diag_u16(duel_diag.peak_render_blit_us),
            .queue_overflow = duel_diag.queue_overflow,
            .missed_tick_resyncs = duel_diag.missed_tick_resyncs,
            .stale_events = duel_diag.stale_split_events,
        };
        __asm__ volatile("" ::: "memory");
        duel_diag_response_ver++;
    }
#    ifdef ARCANE_HOST_ENABLE
    if (is_keyboard_master()) {
        duel_host_diag_packet_t request;
        if (duel_diag_usb_request_consume(&request)) duel_diag_usb_respond(&request);
    }
#    endif
#endif
}

// Own the whole screen on both halves (returning false stops oled_task_kb from
// drawing the default layer/keylog + logo).
bool oled_task_user(void) {
    static duel_fb_t fb;
    static uint32_t  frame;
    static uint8_t   applied_phase = 0xFF;
    static duel_render_t composed;
    static bool      have_composed;
    static uint8_t   seen_fx_seq, flash_kind;
    static uint32_t  flash_started_ms;
    static uint16_t  flash_duration_ms;
    static uint8_t   last_spell_kind[2], flash_spell_kind;
#ifdef ARCANE_DIAGNOSTICS
    const bool hud = true;
#else
    const bool hud = false;
#endif
    uint32_t now = timer_read32();
    bool phase_changed = applied_phase != (uint8_t)duel_display.phase;
    if (phase_changed) {
        applied_phase = (uint8_t)duel_display.phase;
        if (duel_display.phase == DUEL_DISPLAY_SLEEP) {
            // Stop animation first, then explicitly commit a black framebuffer
            // before removing panel power. The ordinary OLED task sees no new
            // dirty blocks while asleep.
            oled_clear();
            oled_render_dirty(true);
            oled_off();
            return false;
        }
        oled_on();
    }
    if (duel_display.phase == DUEL_DISPLAY_SLEEP) return false;

    oled_set_brightness(duel_display_brightness(&duel_display, now));
    // Remember the last visible style in each spell slot. Resolution clears
    // the authoritative slot, but its outcome can still scale from this local
    // presentation cache without growing combat state or the wire again.
    for (int s = 0; s < 2; s++) {
        duel_view_spell_t spell = duel_view_spell(&duel_render.view, (uint8_t)s);
        if (spell.active) last_spell_kind[s] = spell.kind;
    }

    // One-shot fx: arm a presentation deadline for each new world outcome.
    // The renderer still receives its historical 50 ms phases, but dim OLED
    // cadence merely samples them instead of stretching their duration.
    if (duel_render.view.fx_seq != seen_fx_seq) {
        seen_fx_seq  = duel_render.view.fx_seq;
        flash_kind   = duel_render.view.fx_kind;
        bool defender_left = flash_kind == FX_IMPACT_L || flash_kind == FX_DEFLECT_L || flash_kind == FX_FIZZLE_L;
        flash_spell_kind = last_spell_kind[defender_left ? SIM_SIDE_R : SIM_SIDE_L];
        // Impacts linger longer than deflects/fizzles so a hit really lands.
        bool imp = flash_kind == FX_IMPACT_L || flash_kind == FX_IMPACT_R;
        flash_started_ms  = now;
        flash_duration_ms = imp ? DUEL_PRESENTATION_IMPACT_MS : DUEL_PRESENTATION_OTHER_MS;
    }
    uint8_t flash_frames = duel_presentation_remaining(flash_started_ms,
                                                       flash_duration_ms, now);
    duel_render.flash_frames = flash_frames;
    duel_render.flash_kind   = flash_kind;
    duel_render.flash_spell_kind = flash_spell_kind;

    // M7 overlay content (presentation-only; drawn only while scry_is_open).
    // The layer is the emitted QMK layer — fine to READ for display; the chord
    // that opens the overlay is detected from physical positions, not this.
    duel_render.layer = get_highest_layer(layer_state);

    bool semantic_changed = !have_composed ||
                            memcmp(&duel_render, &composed, sizeof duel_render) != 0;
    if (!phase_changed && !semantic_changed) return false;
#ifdef ARCANE_DIAGNOSTICS
    uint32_t diag_render_start_us = time_us_32();
#endif

    duel_fb_clear(&fb);
    wiz_draw_scene(&fb, &duel_render, is_keyboard_left(), frame++, hud);
    // duel_fb_t is already in QMK's page-major layout. The OLED driver compares
    // these bytes with its own buffer and dirties only changed transfer blocks.
    oled_set_cursor(0, 0);
    oled_write_raw((const char *)fb.bits, sizeof fb.bits);
    composed = duel_render;
    have_composed = true;
#ifdef ARCANE_DIAGNOSTICS
    duel_diag_peak(&duel_diag.peak_render_blit_us, time_us_32() - diag_render_start_us);
#endif
    return false;
}

#endif
