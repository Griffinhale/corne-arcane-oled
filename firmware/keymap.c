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
#ifdef ARCANE_M12
#include "sim/duel_courier.h"
#include "sim/duel_event.h"
#endif

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
#ifdef ARCANE_M13
    uint16_t stack_min_free_bytes;
#endif
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

/* ChibiOS diagnostic builds fill each working area with 0x55. Sampling the
 * untouched prefix of the current main-thread stack records a true high-water
 * minimum without adding any release work or relying on a large stack local.
 * The non-static field is debugger/map-visible; Raw HID v2 remains unchanged. */
#ifdef ARCANE_M13
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
#ifdef ARCANE_M13
    bool layer_l = (duel_rows[SCRY_KEY_L_ROW] & ((matrix_row_t)1u << SCRY_KEY_L_COL)) != 0;
    bool layer_r = (duel_rows[SCRY_KEY_R_ROW] & ((matrix_row_t)1u << SCRY_KEY_R_COL)) != 0;
    for (uint8_t side = 0; side < 2; side++) {
        uint8_t row0 = side == SIM_SIDE_L ? 0u : DUEL_ROWS_PER_HAND;
        for (uint8_t row = 0; row < DUEL_ROWS_PER_HAND; row++) {
            matrix_row_t held = duel_rows[row0 + row];
            for (uint8_t col = 0; col < 6u; col++)
                if (held & ((matrix_row_t)1u << col))
                    in.held_pos[side] |= (uint32_t)1u << (row * 6u + col);
        }
        /* Spell layers are physical per-half ingredients, not the global QMK
         * layer selected for host output. A lone thumb therefore influences
         * only its wizard; the deliberate two-thumb chord is layer 3 for both. */
        in.layer[side] = layer_l && layer_r ? 3u :
                         side == SIM_SIDE_L && layer_l ? 1u :
                         side == SIM_SIDE_R && layer_r ? 2u : 0u;
    }
#endif
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

#ifdef ARCANE_M12
// Wall-clock period of one civic tick: the bounded cadence at which the resident
// and floor advance (duel_render.civic_phase). ~300 ms keeps each 16-tick action
// (~4.8 s) inside the spec's 3-10 s window while staying far below combat cadence.
#define DUEL_CIVIC_TICK_MS 300u

// Master-derived shared presentation coordination (Waves 6/7). The visitor is a
// pure function of the notification summary; the rare-event deck is deterministic
// from the session seed + civic phase and safety-gated. Both are recomputed each
// housekeeping pass on the master and relayed in the snapshot's shared_pres /
// revision bytes so both halves render the same courier and event.
static uint8_t duel_m12_shared_pres;
static uint8_t duel_m12_revision;
#ifdef ARCANE_M13
#    define DUEL_FLOOR_TRANSITION_MS 600u
#    define DUEL_FLOOR_PHASE_MS      150u
static uint8_t duel_floor_target;
static uint8_t duel_floor_source;
static uint32_t duel_floor_started_ms;
static bool duel_floor_initialized;
static bool duel_floor_active;

static void duel_floor_note_target(uint8_t civic, uint32_t now) {
    uint8_t target = DUEL_CIVIC_FLOOR(civic);
    if (target == DUEL_M12_FLOOR_SPECIAL) return; /* reserved */
    if (!duel_floor_initialized) {
        duel_floor_target = duel_floor_source = target;
        duel_floor_initialized = true;
        return;
    }
    if (target == duel_floor_target) return;
    /* A rapid change always departs from the latest authoritative target; no
     * transition is queued behind an animation already in flight. */
    duel_floor_source = duel_floor_target;
    duel_floor_target = target;
    if (duel_display.phase == DUEL_DISPLAY_SLEEP) {
        duel_floor_active = false; /* sleeping panels snap on their next wake */
    } else {
        duel_floor_started_ms = now;
        duel_floor_active = true;
    }
}
#endif

static void duel_m12_update_shared(uint32_t now) {
#ifdef ARCANE_HOST_ENABLE
    uint8_t ext        = duel_host_context(&duel_host_state);
    uint8_t alr        = duel_host_alert(&duel_host_state);
    uint8_t category   = DUEL_HOST_ALERT_CATEGORY(alr);
    uint8_t count      = DUEL_HOST_CONTEXT_NOTIF(ext);
    uint8_t age        = DUEL_HOST_ALERT_AGE(alr);
    bool    persistent = DUEL_HOST_CONTEXT_PERSISTENT(ext);
#else
    uint8_t category = 0, count = 0, age = 0;
    bool    persistent = false;
#endif
    uint8_t phase = (uint8_t)(now / DUEL_CIVIC_TICK_MS);
    m12_visitor_state_t vis = m12_visitor_derive(duel_session, phase, category,
                                                 count, age, persistent);
    duel_m12_shared_pres = m12_visitor_shared_pres(vis);
    // Rare events are safety-gated (spec §14.1): suppressed while a critical
    // (sentinel) visitor is stationed or a champion is not standing.
    bool eligible = DUEL_VISITOR_KIND(duel_m12_shared_pres) != DUEL_M12_COURIER_SENTINEL &&
                    duel_world.wiz[SIM_SIDE_L].life == LIFE_ACTIVE &&
                    duel_world.wiz[SIM_SIDE_R].life == LIFE_ACTIVE;
    duel_m12_revision = m12_event_revision(m12_event_derive(duel_session, phase, eligible));
#ifdef ARCANE_M13
    /* Lasting spell aftermath temporarily takes precedence over disposable
     * courier/rare-event coordination. The marker bit lets the renderer select
     * the M13 interpretation; ordinary M12 presentation resumes at expiry. */
    uint8_t aftermath_revision = m13_aftermath_revision(&duel_world);
    if (aftermath_revision & M13_AFTERMATH_WIRE) {
        duel_m12_shared_pres = m13_aftermath_shared(&duel_world);
        duel_m12_revision = aftermath_revision;
    }
#endif
}
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
#ifdef ARCANE_M12
    // Relay the host's civic semantics plus the master-derived visitor
    // (shared_pres) and rare-event (revision) coordination. set_civic writes the
    // four bytes and recomputes the CRC over the 31-byte snapshot; release builds
    // omit these bytes entirely.
#  ifdef ARCANE_HOST_ENABLE
    duel_snapshot_set_civic(&pkt, duel_host_civic(&duel_host_state),
                            duel_host_secondary(&duel_host_state),
                            duel_m12_shared_pres, duel_m12_revision);
#  else
    duel_snapshot_set_civic(&pkt, 0, 0, duel_m12_shared_pres, duel_m12_revision);
#  endif
#endif
    bool semantic_changed = !duel_have_tx ||
                            memcmp(&pkt.view, &duel_last_tx.view, sizeof pkt.view) != 0 ||
                            pkt.external != duel_last_tx.external ||
                            pkt.alert != duel_last_tx.alert ||
#ifdef ARCANE_M12
                            pkt.civic != duel_last_tx.civic ||
                            pkt.secondary != duel_last_tx.secondary ||
                            pkt.shared_pres != duel_last_tx.shared_pres ||
                            pkt.revision != duel_last_tx.revision ||
#endif
                            pkt.flags != duel_last_tx.flags;
    // Measure cadence start-to-start. Recording completion would add the
    // blocking split transaction itself (~3 ms on RP2040) to the threshold;
    // with a 40 ms sim tick that pushed an intended 80 ms send to 120 ms.
    uint32_t tx_started_ms = timer_read32();
    uint32_t since_tx = tx_started_ms - duel_last_tx_ms;
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
        // The slave deliberately returns an all-zero reply if housekeeping is
        // updating its seqlock-protected metrics at this exact instant. Keep
        // the last coherent sample instead of making a diagnostic query
        // intermittently report an invalid/empty peer.
        if (peer.magic == DUEL_MAGIC && peer.version == 1) duel_peer_diag = peer;
    } else if (duel_diag.split_tx_failure < UINT16_MAX) {
        duel_diag.split_tx_failure++;
    }
#else
    bool sent = transaction_rpc_send(DUEL_SYNC_SNAPSHOT, sizeof pkt, &pkt);
#endif
    if (sent) {
        duel_fx_sent = duel_world.fx_seq;
        duel_last_tx_ms = tx_started_ms;
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

#ifdef ARCANE_M12
static void duel_render_set_civic(uint8_t civic, uint8_t secondary,
                                  uint8_t shared_pres, uint8_t revision) {
#ifdef ARCANE_M13
    duel_floor_note_target(civic, timer_read32());
#endif
    duel_render.civic = civic;
    duel_render.secondary = secondary;
    duel_render.shared_pres = shared_pres;
    duel_render.revision = revision;
}
#endif

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
        // The 250 ms repair deadline is not an integer multiple of the 40 ms
        // sim tick. Check it directly so an idle link repairs at 250 ms rather
        // than waiting for the 280 ms tick; this remains a cheap time read and
        // avoids encoding/render work until the deadline is actually due.
        bool repair_due = duel_have_tx &&
                          timer_elapsed32(duel_last_tx_ms) >= DUEL_REPAIR_TX_MS;
#ifdef ARCANE_M12
        // Refresh the visitor + rare-event coordination before both the wire
        // packet and the master's own render read it, so they stay consistent.
        duel_m12_update_shared(now);
#endif
        if (ticked || display_changed || host_changed || repair_due)
            duel_master_tx(display_changed || host_changed);
        if (ticked || display_changed || host_changed || render_invalid) {
            duel_render_from_world(&duel_render, &duel_world);
            duel_render.flags &= (uint8_t)~DUEL_RENDER_STALE;
#ifdef ARCANE_HOST_ENABLE
            duel_render_set_external(duel_host_context(&duel_host_state),
                                     duel_host_alert(&duel_host_state));
#  ifdef ARCANE_M12
            duel_render_set_civic(duel_host_civic(&duel_host_state),
                                  duel_host_secondary(&duel_host_state),
                                  duel_m12_shared_pres, duel_m12_revision);
#  endif
#else
            duel_render_set_external(0, 0);
#  ifdef ARCANE_M12
            duel_render_set_civic(0, 0, duel_m12_shared_pres, duel_m12_revision);
#  endif
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
#ifdef ARCANE_M12
                duel_render_set_civic(duel_rx.last.civic, duel_rx.last.secondary,
                                      duel_rx.last.shared_pres, duel_rx.last.revision);
#endif
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
#ifdef ARCANE_M12
                duel_render_set_civic(0, 0, 0, 0);
#endif
                if (stale) duel_render.flags |= DUEL_RENDER_STALE;
                else duel_render.flags &= (uint8_t)~DUEL_RENDER_STALE;
            }
            using_remote = false;
        }
    }
#ifdef ARCANE_DIAGNOSTICS
#ifdef ARCANE_M13
    duel_diag_stack_sample();
#endif
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
#ifdef ARCANE_M13
        flash_kind   = duel_render.view.outcome_overlay & 0x0fu;
#else
        flash_kind   = duel_render.view.fx_kind;
#endif
#ifdef ARCANE_M13
        bool defender_left = flash_kind == FX_IMPACT_L || flash_kind == FX_DEFLECT_L ||
                             flash_kind == FX_FIZZLE_L || flash_kind == FX_HEAL_L ||
                             flash_kind == FX_WARD_SHATTER_L;
#else
        bool defender_left = flash_kind == FX_IMPACT_L || flash_kind == FX_DEFLECT_L || flash_kind == FX_FIZZLE_L;
#endif
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

#ifdef ARCANE_M12
    // Presentation seed (the shared 1-byte session) plus the bounded civic clock
    // that paces resident/floor motion. A SLEEP phase already returned above, so
    // advancing civic_phase here can trigger a redraw while awake but never
    // re-lights or wakes the panel (plan §2 D3/D4). civic_phase is LOCAL — each
    // half derives its own resident — so it is deliberately not on the wire.
    duel_render.seed = is_keyboard_master() ? duel_session : duel_rx.last.session;
    duel_render.civic_phase = (uint8_t)(now / DUEL_CIVIC_TICK_MS);
#ifdef ARCANE_M13
    if (duel_floor_active &&
        timer_elapsed32(duel_floor_started_ms) >= DUEL_FLOOR_TRANSITION_MS)
        duel_floor_active = false;
    uint8_t floor_phase = duel_floor_active ?
        (uint8_t)(timer_elapsed32(duel_floor_started_ms) / DUEL_FLOOR_PHASE_MS) : 0u;
    duel_render.floor_transition = M13_FLOOR_TRANSITION_PACK(
        duel_floor_source, floor_phase, duel_floor_active);
#endif
#endif

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
