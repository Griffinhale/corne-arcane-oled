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

/* The world sim, split sync, RGB ownership, and host protocol are all
 * unconditional: only the two oled_*_user hooks below depend on the OLED
 * driver. An OLED_ENABLE=no debug build must not silently drop the split
 * RPC registration or re-enable Vial RGB EEPROM writes. */

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
#include "sim/duel_courier.h"
#include "sim/duel_event.h"
#include "sim/duel_runtime.h"
#include "sim/duel_rgb.h"
#include "pico/rand.h"

#ifdef OLED_ENABLE
// Portrait canvas must match the rotated OLED exactly (see duel_draw.h).
_Static_assert(DUEL_CANVAS_W == OLED_DISPLAY_HEIGHT && DUEL_CANVAS_H == OLED_DISPLAY_WIDTH,
               "duel canvas dimensions must match the rotated OLED");
#endif

// crkbd/rev1: 8 matrix rows, 4 per hand (left = 0..3, right = 4..7). The
// hardware-agnostic input sampler in duel_runtime assumes exactly this
// geometry; a different board must update DUEL_INPUT_* alongside.
#define DUEL_ROWS_PER_HAND (MATRIX_ROWS / 2)
_Static_assert(MATRIX_ROWS == DUEL_INPUT_ROWS && MATRIX_COLS == DUEL_INPUT_COLS,
               "duel_runtime input geometry must match the physical matrix");

static duel_display_policy_t duel_display;
static uint32_t duel_local_wake_until_ms;
static bool duel_render_invalid = true;
static duel_flash_policy_t duel_flash;
static uint8_t duel_last_spell_kind[2];

/* Saturating diagnostic counters compile to nothing in release builds, so
 * hot functions stay readable without interleaved #ifdef blocks. */
#ifdef ARCANE_DIAGNOSTICS
#define DUEL_DIAG_INC(field) \
    do { if (duel_diag.field < UINT16_MAX) duel_diag.field++; } while (0)
#else
#define DUEL_DIAG_INC(field) ((void)0)
#endif

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
    uint16_t stack_min_free_bytes;
} duel_diag_t;

// Intentionally non-static: a debugger or map-file budget pass can inspect it
// without adding release protocol traffic or changing presentation behavior.
volatile duel_diag_t duel_diag;
volatile duel_split_diag_reply_t duel_peer_diag;
static duel_mailbox_t duel_diag_response_mailbox;

static void duel_diag_peak(volatile uint32_t *peak, uint32_t elapsed) {
    if (elapsed > *peak) *peak = elapsed;
}

static uint16_t duel_diag_u16(uint32_t value) {
    return value > UINT16_MAX ? UINT16_MAX : (uint16_t)value;
}

/* ChibiOS diagnostic builds fill each working area with 0x55. Sampling the
 * untouched prefix of the current main-thread stack records a true high-water
 * minimum without adding any release work or relying on a large stack local.
 * The non-static field is debugger/map-visible; Raw HID v2 remains unchanged. */
static void duel_diag_stack_sample(void) {
    thread_t *thread = chThdGetSelfX();
    uint8_t *scan = (uint8_t *)chThdGetWorkingAreaX(thread) + sizeof(thread_t);
    uint8_t marker;
    uint8_t *limit = &marker;
    uint16_t free_bytes = 0;
    while (scan < limit && *scan == CH_DBG_STACK_FILL_VALUE && free_bytes < UINT16_MAX) {
        scan++;
        free_bytes++;
    }
    if (!duel_diag.stack_min_free_bytes || free_bytes < duel_diag.stack_min_free_bytes)
        duel_diag.stack_min_free_bytes = free_bytes;
}
#endif

static void duel_note_physical_key(void) {
    uint32_t now = timer_read32();
    duel_display_note_key(&duel_display, now);
    duel_local_wake_until_ms = now + DUEL_WAKE_GRACE_MS;
    duel_render_invalid = true;
}

#ifdef OLED_ENABLE
// Treat both OLEDs as VERTICAL (portrait). On this build both panels are mounted
// the same way, so both halves use the same rotation to stand upright. If one
// half ever appears upside-down, flip only that half's constant (270 <-> 90).
#define WIZ_ROT_MASTER  OLED_ROTATION_270
#define WIZ_ROT_OFFHAND OLED_ROTATION_270
oled_rotation_t oled_init_user(oled_rotation_t rotation) {
    return is_keyboard_master() ? WIZ_ROT_MASTER : WIZ_ROT_OFFHAND;
}
#endif

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

/* RGB is a world surface, not a user setting.  This runs before QMK's RGB
 * Matrix keycode processor, so Vial may map these keycodes but they remain
 * deliberate no-ops and cannot mutate EEPROM-backed lighting state. */
bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    (void)record;
    return !IS_RGB_MATRIX_KEYCODE(keycode);
}

/* ---- split snapshot sync -------------------------------------------------
 * The master's world is authoritative; it streams 32-byte snapshots to the
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

/* ---- disposable host context ---------------------------------------------
 * The USB callback remains bounded: one 32-byte seqlock-guarded copy. Parsing,
 * ordering, expiry, and split propagation happen later in housekeeping. The
 * daemon sends absolute context on every report, so latest-wins is safe. */
#include "raw_hid.h"
#include "via.h"
#define DUEL_HOST_TIMEOUT_MS 1500

static duel_mailbox_t duel_host_rx_mailbox;
static uint8_t duel_host_rx_seen_ver;
static duel_host_state_t duel_host_state;
static uint32_t duel_host_expire_ms;
#ifdef ARCANE_DIAGNOSTICS
static duel_mailbox_t duel_diag_usb_rx_mailbox;
static uint8_t duel_diag_usb_rx_seen_ver;
#endif

void raw_hid_receive_kb(uint8_t *data, uint8_t length) {
    if (length != sizeof(duel_host_packet_t) || data[0] != DUEL_HOST_MAGIC0) {
        data[0] = id_unhandled;
        return;
    }
#ifdef ARCANE_DIAGNOSTICS
    if (data[1] == DUEL_HOST_MAGIC1 &&
        data[2] == DUEL_HOST_DIAG_VERSION && data[3] == DUEL_HOST_MSG_DIAG_REQUEST) {
        duel_mailbox_publish(&duel_diag_usb_rx_mailbox, data,
                             sizeof(duel_host_diag_packet_t));
        return;
    }
#endif
    duel_mailbox_publish(&duel_host_rx_mailbox, data, sizeof(duel_host_packet_t));
}

static bool duel_host_rx_consume(uint32_t now) {
    duel_host_packet_t packet;
    if (!duel_mailbox_consume(&duel_host_rx_mailbox, &duel_host_rx_seen_ver,
                              &packet, sizeof packet)) return false;
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
    if (!duel_mailbox_consume(&duel_diag_usb_rx_mailbox,
                              &duel_diag_usb_rx_seen_ver,
                              request, sizeof *request)) return false;
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

static duel_mailbox_t duel_rx_mailbox;
static uint32_t         duel_last_pkt_ms;

static void duel_snapshot_rx(uint8_t in_len, const void *in, uint8_t out_len, void *out) {
    if (in_len != sizeof(duel_snapshot_t)) return;
    duel_mailbox_publish(&duel_rx_mailbox, in, sizeof(duel_snapshot_t));
#ifdef ARCANE_DIAGNOSTICS
    if (out_len == sizeof(duel_split_diag_reply_t) && out != NULL) {
        if (!duel_mailbox_read_latest(&duel_diag_response_mailbox, out,
                                      sizeof(duel_split_diag_reply_t)))
            memset(out, 0, sizeof(duel_split_diag_reply_t));
    }
#else
    (void)out_len;
    (void)out;
#endif
}

static void duel_session_init(void);

void keyboard_post_init_user(void) {
    transaction_register_rpc(DUEL_SYNC_SNAPSHOT, duel_snapshot_rx);
    duel_session_init();
    rgb_matrix_enable_noeeprom();
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

/* ---- scry layer-key chord + input sampling: physical positions -------------
 * The chord is detected from raw matrix positions, NOT emitted keycodes or the
 * active layer, so it is immune to whatever Vial maps onto these keys. On
 * crkbd's LAYOUT_split_3x6_3 the two momentary layer thumbs are the middle
 * thumb of each hand: MO(1) at matrix (3,4) on the left, MO(2) at (7,4) on the
 * right (see keyboards/crkbd/info.json; SCRY_KEY_* now live in duel_runtime.h
 * beside the tested sampler). Only the master's merged matrix holds both,
 * which is exactly why the chord machine is authoritative-only. */
static sim_inputs_t duel_sample_inputs(void) {
    uint16_t rows[DUEL_INPUT_ROWS];
    for (uint8_t r = 0; r < DUEL_INPUT_ROWS; r++) rows[r] = duel_rows[r];
    return duel_inputs_from_rows(rows);
}

// Master -> slave snapshot stream.
static uint8_t  duel_session;
static uint8_t  duel_fx_sent;
static duel_snapshot_t duel_last_tx;
static duel_tx_policy_t duel_tx_policy;
static uint32_t duel_sky_started_ms;
static duel_diplomacy_t duel_diplomacy;

// Called exactly once, from keyboard_post_init_user — always before any tx.
static void duel_session_init(void) {
    uint32_t entropy = get_rand_32();
    duel_session = (uint8_t)(entropy ^ (entropy >> 8) ^
                             (entropy >> 16) ^ (entropy >> 24));
    duel_sky_started_ms = timer_read32();
    duel_diplomacy_init(&duel_diplomacy);
}

// Master-derived shared presentation coordination (Waves 6/7), recomputed each
// housekeeping pass via duel_civic_shared_derive (tested, in duel_runtime) and
// relayed in the snapshot's shared_pres / revision bytes so both halves render
// the same courier and event.
static duel_civic_shared_t duel_civic_shared;
static duel_floor_policy_t duel_floor_policy;

static void duel_master_tx(uint32_t now, bool urgent) {
    bool fx_changed = duel_world.fx_seq != duel_fx_sent;
    uint8_t external = duel_host_context(&duel_host_state);
    uint8_t alert = duel_host_alert(&duel_host_state);
    uint8_t civic = duel_host_civic(&duel_host_state);
    uint8_t secondary = DUEL_SECONDARY_SKY_SUB_PACK(
        DUEL_SECONDARY_SKY_PACK(
            duel_host_secondary(&duel_host_state),
            duel_sky_phase(now - duel_sky_started_ms)),
        duel_sky_subphase(now - duel_sky_started_ms));
    uint8_t flags = DUEL_FLAGS_WORLD_VALID | DUEL_FLAGS_DISPLAY_PACK(duel_display.phase);
    duel_view_t candidate_view;
    duel_view_from_world(&duel_world, &candidate_view);
    // The encoder scatters residue (Track A) into the flags/civic/secondary
    // spare bits, so those comparisons mask residue out and the residue
    // change check runs separately against the world's packed zones.
    uint8_t residue_now[2], residue_sent[2];
    duel_residue_pack(&duel_world, residue_now);
    duel_snapshot_residue_render(&duel_last_tx, residue_sent);
    bool semantic_changed = !duel_tx_policy.have_sent ||
                            memcmp(&candidate_view, &duel_last_tx.view,
                                   sizeof candidate_view) != 0 ||
                            external != duel_last_tx.external ||
                            alert != duel_last_tx.alert ||
                            civic != (uint8_t)(duel_last_tx.civic & ~DUEL_CIVIC_RESIDUE_BITS) ||
                            secondary != (uint8_t)(duel_last_tx.secondary & ~DUEL_SECONDARY_RESIDUE_BITS) ||
                            duel_civic_shared.shared_pres != duel_last_tx.shared_pres ||
                            duel_civic_shared.revision != duel_last_tx.revision ||
                            flags != (uint8_t)(duel_last_tx.flags & ~DUEL_FLAGS_RESIDUE_BITS) ||
                            residue_now[0] != residue_sent[0] ||
                            residue_now[1] != residue_sent[1];
    if (!duel_tx_attempt(&duel_tx_policy, now, urgent, fx_changed,
                         semantic_changed)) return;

    duel_snapshot_t pkt;
    duel_encode_external_alert_display(&duel_world, duel_session,
                                       duel_tx_policy.sequence, external, alert,
                                       duel_display.phase, &pkt);
    // Relay the host's civic semantics plus the master-derived visitor
    // (shared_pres) and rare-event (revision) coordination. set_civic writes the
    // four bytes and recomputes the CRC over the 31-byte snapshot; release builds
    // omit these bytes entirely.
    duel_snapshot_set_civic(&pkt, civic, secondary,
                            duel_civic_shared.shared_pres, duel_civic_shared.revision);

#ifdef ARCANE_DIAGNOSTICS
    uint32_t tx_start_us = time_us_32();
    duel_split_diag_reply_t peer = {0};
    bool sent = transaction_rpc_exec(DUEL_SYNC_SNAPSHOT, sizeof pkt, &pkt,
                                     sizeof peer, &peer);
    duel_diag_peak(&duel_diag.peak_split_tx_us, time_us_32() - tx_start_us);
    if (sent) {
        DUEL_DIAG_INC(split_tx_success);
        // The slave deliberately returns an all-zero reply if housekeeping is
        // updating its seqlock-protected metrics at this exact instant. Keep
        // the last coherent sample instead of making a diagnostic query
        // intermittently report an invalid/empty peer.
        if (peer.magic == DUEL_MAGIC && peer.version == 1) duel_peer_diag = peer;
    } else {
        DUEL_DIAG_INC(split_tx_failure);
    }
#else
    bool sent = transaction_rpc_send(DUEL_SYNC_SNAPSHOT, sizeof pkt, &pkt);
#endif
    if (sent) {
        duel_fx_sent = duel_world.fx_seq;
        duel_tx_commit(&duel_tx_policy, now);
        duel_last_tx = pkt;
    }
}

// Slave: consistent seqlock read of the latest packet, then accept/reject.
static duel_rx_state_t duel_rx;
static uint8_t         duel_rx_seen_ver;

static bool duel_slave_rx_consume(void) {
    duel_snapshot_t pkt;
    if (!duel_mailbox_consume(&duel_rx_mailbox, &duel_rx_seen_ver,
                              &pkt, sizeof pkt)) return false;
    if (!duel_decode_valid(&pkt)) {
#ifdef ARCANE_DIAGNOSTICS
        DUEL_DIAG_INC(split_protocol_errors);
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

static void duel_render_set_civic(uint32_t now, uint8_t civic, uint8_t secondary,
                                  uint8_t shared_pres, uint8_t revision) {
    duel_floor_note_target(&duel_floor_policy, civic, now, duel_display.phase);
    duel_render.civic = civic;
    duel_render.secondary = secondary;
    duel_render.shared_pres = shared_pres;
    duel_render.revision = revision;
}

static void duel_housekeeping_master(uint32_t now, bool ticked,
                                     bool display_changed, bool host_changed,
                                     bool render_invalid) {
    duel_diplomacy_update(&duel_diplomacy,
                          duel_world.wiz[SIM_SIDE_L].life,
                          duel_world.wiz[SIM_SIDE_R].life);
    // The 250 ms repair deadline is not an integer multiple of the 40 ms
    // sim tick. Check it directly so an idle link repairs at 250 ms rather
    // than waiting for the 280 ms tick; this remains a cheap time read and
    // avoids encoding/render work until the deadline is actually due.
    bool repair_due = duel_tx_repair_due(&duel_tx_policy, now);
    // Refresh the visitor + rare-event coordination before both the wire
    // packet and the master's own render read it, so they stay consistent.
    duel_civic_shared = duel_civic_shared_derive(duel_session, now,
                                                 &duel_host_state, &duel_world,
                                                 duel_diplomacy.balance);
    if (ticked || display_changed || host_changed || repair_due)
        duel_master_tx(now, display_changed || host_changed);
    if (ticked || display_changed || host_changed || render_invalid) {
        duel_render_from_world(&duel_render, &duel_world);
        duel_render.flags &= (uint8_t)~DUEL_RENDER_STALE;
        duel_render_set_external(duel_host_context(&duel_host_state),
                                 duel_host_alert(&duel_host_state));
        duel_render_set_civic(now, duel_host_civic(&duel_host_state),
                              duel_host_secondary(&duel_host_state),
                              duel_civic_shared.shared_pres,
                              duel_civic_shared.revision);
    }
}

static void duel_housekeeping_slave(uint32_t now, bool ticked,
                                    bool display_changed, bool render_invalid) {
    static duel_slave_presenter_t presenter;
    bool accepted = duel_slave_rx_consume();
    bool stale = timer_elapsed32(duel_last_pkt_ms) > DUEL_STALE_MS;
#ifdef ARCANE_DIAGNOSTICS
    static bool diag_was_stale;
    if (stale && !diag_was_stale) DUEL_DIAG_INC(stale_split_events);
    diag_was_stale = stale;
#endif
    duel_slave_decision_t decide = duel_slave_present(
        &presenter, accepted, duel_rx.have_any, stale, ticked, render_invalid,
        (duel_render.flags & DUEL_RENDER_STALE) != 0);
    if (decide.use_remote) {
        if (decide.consider_follow) {
            duel_display_phase_t before_follow = duel_display.phase;
            uint8_t remote_phase = DUEL_FLAGS_DISPLAY(duel_rx.last.flags);
            if (duel_display_should_follow(remote_phase,
                                           &duel_local_wake_until_ms, now))
                duel_display_follow(&duel_display, (duel_display_phase_t)remote_phase, now);
            display_changed |= duel_display.phase != before_follow;
        }
        if (decide.base_refresh || display_changed) {
            duel_render.view = duel_rx.last.view;
            duel_render_set_external(duel_rx.last.external, duel_rx.last.alert);
            // Residue borrows civic bits 6-7 / secondary bit 7 on the wire;
            // strip them so render civic semantics match the master's.
            duel_render_set_civic(now,
                                  (uint8_t)(duel_rx.last.civic & ~DUEL_CIVIC_RESIDUE_BITS),
                                  (uint8_t)(duel_rx.last.secondary & ~DUEL_SECONDARY_RESIDUE_BITS),
                                  duel_rx.last.shared_pres, duel_rx.last.revision);
            duel_snapshot_residue_render(&duel_rx.last, duel_render.residue);
            duel_render.flags &= (uint8_t)~DUEL_RENDER_STALE;
        }
    } else {
        // Local pose-only fallback: never authoritative, never combat.
        duel_display_phase_t before_update = duel_display.phase;
        duel_display_update(&duel_display, now);
        display_changed |= duel_display.phase != before_update;
        if (decide.base_refresh || display_changed) {
            duel_render_from_world(&duel_render, &duel_world);
            duel_render_set_external(0, 0);
            duel_render_set_civic(now, 0,
                DUEL_SECONDARY_SKY_SUB_PACK(
                    DUEL_SECONDARY_SKY_PACK(0,
                        duel_sky_phase(now - duel_sky_started_ms)),
                    duel_sky_subphase(now - duel_sky_started_ms)), 0, 0);
            if (decide.set_stale) duel_render.flags |= DUEL_RENDER_STALE;
            else duel_render.flags &= (uint8_t)~DUEL_RENDER_STALE;
        }
    }
}

void housekeeping_task_user(void) {
#ifdef ARCANE_DIAGNOSTICS
    uint32_t diag_start_us = time_us_32();
#endif
    uint32_t now = timer_read32();
    /* A Vial lighting command can bypass the keycode path. Reassert world
     * ownership without touching EEPROM; the indicators below set every LED. */
    if (!rgb_matrix_is_enabled()) rgb_matrix_enable_noeeprom();
    bool host_changed = false;
    if (is_keyboard_master()) host_changed = duel_host_housekeeping(now);
    if (!duel_tick_armed) {
        sim_init(&duel_world, is_keyboard_master() ? SIMF_AUTHORITATIVE : 0, 0);
        duel_display_init(&duel_display, now);
        duel_next_tick_ms = now + SIM_TICK_MS;
        duel_tick_armed   = true;
    }
    duel_display_phase_t prior_display = duel_display.phase;
    if (is_keyboard_master()) duel_display_update(&duel_display, now);
    bool display_changed = duel_display.phase != prior_display;

    bool resynced = false;
    uint8_t budget = duel_tick_budget(&duel_next_tick_ms, now, &resynced);
    bool ticked = budget != 0;
    if (resynced) DUEL_DIAG_INC(missed_tick_resyncs);
    if (ticked) {
        // Inputs are sampled once and replayed across catch-up ticks; queued
        // events apply to the first tick only.
        sim_inputs_t inputs = duel_sample_inputs();
        sim_tick(&duel_world, inputs, duel_evq.ev, duel_evq.n, duel_evq.dropped);
        duel_evq.n = 0;
        duel_evq.dropped = 0;
        for (uint8_t t = 1; t < budget; t++)
            sim_tick(&duel_world, inputs, NULL, 0, 0);
    }
#ifdef ARCANE_DIAGNOSTICS
    if (budget > 1) {
        uint16_t add = (uint16_t)(budget - 1);
        duel_diag.catchup_ticks = UINT16_MAX - duel_diag.catchup_ticks < add
                                      ? UINT16_MAX : (uint16_t)(duel_diag.catchup_ticks + add);
    }
    duel_diag.queue_overflow = duel_world.overflow_count;
    duel_diag.host_malformed_errors = duel_host_state.malformed_packets;
    duel_diag.host_stale_errors = duel_host_state.stale_packets;
#endif

    bool render_invalid = duel_render_invalid;
    duel_render_invalid = false;
    if (is_keyboard_master()) {
        duel_housekeeping_master(now, ticked, display_changed, host_changed,
                                 render_invalid);
    } else {
        duel_housekeeping_slave(now, ticked, display_changed, render_invalid);
    }
#ifdef ARCANE_DIAGNOSTICS
    duel_diag_stack_sample();
    duel_render.diag_tick = (uint8_t)(duel_world.tick % 25u);
    duel_render.diag_overflow = duel_world.overflow_count;
    duel_diag_peak(&duel_diag.peak_housekeeping_us, time_us_32() - diag_start_us);
    if (!is_keyboard_master()) {
        duel_split_diag_reply_t response = {
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
        duel_mailbox_publish(&duel_diag_response_mailbox, &response,
                             sizeof response);
    }
    if (is_keyboard_master()) {
        duel_host_diag_packet_t request;
        if (duel_diag_usb_request_consume(&request)) duel_diag_usb_respond(&request);
    }
#endif
}

bool rgb_matrix_indicators_advanced_user(uint8_t led_min, uint8_t led_max) {
    duel_rgb_world_t world = {
        .display_phase = duel_display.phase,
        .stale = (duel_render.flags & DUEL_RENDER_STALE) != 0,
        .observatory = DUEL_CIVIC_FLOOR(duel_render.civic) == DUEL_CIVIC_FLOOR_SPECIAL,
        .flash_kind = duel_flash.kind,
        .flash_active = duel_flash_remaining(&duel_flash, timer_read32()) != 0,
    };
    for (uint8_t side = 0; side < 2; side++) {
        duel_view_wizard_t wizard = duel_view_wizard(&duel_render.view, side);
        world.prepared[side] = wizard.prepared != 0;
        world.prepared_element[side] = VIEW_PHASE_ELEMENT(duel_render.view.phase[side]);
    }
    // g_led_config places both halves on one 0..224 x-axis; the split falls at
    // its midpoint.
#define DUEL_LED_SPLIT_X 112u
    if (led_max > RGB_MATRIX_LED_COUNT) led_max = RGB_MATRIX_LED_COUNT;
    for (uint8_t i = led_min; i < led_max; i++) {
        bool led_is_left = g_led_config.point[i].x < DUEL_LED_SPLIT_X;
        duel_rgb_t color = duel_rgb_policy(&world, g_led_config.flags[i], led_is_left);
        rgb_matrix_set_color(i, color.r, color.g, color.b);
    }
    return true;
}

#ifdef OLED_ENABLE
// Own the whole screen on both halves (returning false stops oled_task_kb from
// drawing the default layer/keylog + logo).
bool oled_task_user(void) {
    static duel_fb_t fb;
    static uint32_t  frame;
    static uint8_t   applied_phase = 0xFF;
    static duel_render_t composed;
    static bool      have_composed;
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
    // One-shot fx: cache each slot's last visible spell style and arm a
    // presentation deadline for each new world outcome (tested policy in
    // duel_runtime). The renderer still receives its historical 50 ms phases,
    // but dim OLED cadence merely samples them instead of stretching them.
    duel_flash_observe_view(&duel_flash, duel_last_spell_kind, &duel_render.view, now);
    uint8_t flash_frames = duel_flash_remaining(&duel_flash, now);
    duel_render.flash_frames = flash_frames;
    duel_render.flash_kind   = duel_flash.kind;
    duel_render.flash_spell_kind = duel_flash.spell_kind;

    // scry overlay content (presentation-only; drawn only while scry_is_open).
    // The layer is the emitted QMK layer — fine to READ for display; the chord
    // that opens the overlay is detected from physical positions, not this.
    uint8_t local_layer = DUEL_RENDER_LOCAL_NONE;
    if (is_keyboard_left()) {
        if (duel_rows[SCRY_KEY_L_ROW] & ((matrix_row_t)1u << SCRY_KEY_L_COL))
            local_layer = DUEL_RENDER_LOCAL_LEFT;
    } else if (duel_rows[SCRY_KEY_R_ROW] & ((matrix_row_t)1u << SCRY_KEY_R_COL)) {
        local_layer = DUEL_RENDER_LOCAL_RIGHT;
    }
    duel_render.layer = DUEL_RENDER_LAYER_PACK(get_highest_layer(layer_state),
                                                local_layer);
    duel_render.local_ambience = incantation_local_ambience(
        &duel_world.wiz[is_keyboard_left() ? SIM_SIDE_L : SIM_SIDE_R]);

    // Presentation seed (the shared 1-byte session) plus the bounded civic clock
    // that paces resident/floor motion. A SLEEP phase already returned above, so
    // advancing civic_phase here can trigger a redraw while awake but never
    // re-lights or wakes the panel (plan §2 D3/D4). civic_phase is LOCAL — each
    // half derives its own resident — so it is deliberately not on the wire.
    duel_render.seed = is_keyboard_master() ? duel_session : duel_rx.last.session;
    duel_render.civic_phase = (uint8_t)(now / DUEL_CIVIC_TICK_MS);
    duel_render.floor_transition = duel_floor_presentation(&duel_floor_policy, now);

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
