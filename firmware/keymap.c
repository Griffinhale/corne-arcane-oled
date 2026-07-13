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
static uint32_t duel_local_wake_ms;
static bool duel_local_wake_armed;

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
} duel_diag_t;

// Intentionally non-static: a debugger or map-file budget pass can inspect it
// without adding release protocol traffic or changing presentation behavior.
volatile duel_diag_t duel_diag;

static void duel_diag_peak(volatile uint32_t *peak, uint32_t elapsed) {
    if (elapsed > *peak) *peak = elapsed;
}
#endif

static void duel_note_physical_key(void) {
    uint32_t now = timer_read32();
    duel_display_note_key(&duel_display, now);
    duel_local_wake_ms = now;
    duel_local_wake_armed = true;
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
 * against the previous pass and append compact edge events to a bounded
 * queue. No rendering, no allocation, no split work. The master's matrix
 * already contains the slave's rows (merged in matrix_post_scan before
 * matrix_scan_kb fires — quantum/matrix_common.c), so the master captures
 * both halves; the slave captures only its own. */
static sim_evq_t    duel_evq;
static matrix_row_t duel_prev_rows[MATRIX_ROWS];

static void duel_scan_rows(uint8_t row_first, uint8_t row_last, uint8_t side) {
    for (uint8_t r = row_first; r <= row_last; r++) {
        matrix_row_t cur  = matrix_get_row(r);
        matrix_row_t diff = cur ^ duel_prev_rows[r];
        duel_prev_rows[r] = cur;
        if (!diff) continue;
        for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            if (!(diff & ((matrix_row_t)1 << c))) continue;
            uint8_t kind = (cur & ((matrix_row_t)1 << c)) ? SIM_EV_KEYDOWN : SIM_EV_KEYUP;
            sim_evq_push(&duel_evq, (sim_event_t){kind, side, (uint8_t)(r % DUEL_ROWS_PER_HAND), c});
            if (kind == SIM_EV_KEYDOWN) duel_note_physical_key();
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
 * The master's world is authoritative; it streams 31-byte snapshots to the
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
#    define DUEL_HOST_TIMEOUT_MS 1500

static volatile uint8_t duel_host_rx_ver;
static duel_host_packet_t duel_host_rx_staging;
static uint8_t duel_host_rx_seen_ver;
static duel_host_state_t duel_host_state;
static uint32_t duel_host_last_heartbeat_ms;
static bool duel_host_heartbeat_armed;

void raw_hid_receive(uint8_t *data, uint8_t length) {
    if (length != sizeof(duel_host_packet_t)) return;
    duel_host_rx_ver++;
    __asm__ volatile("" ::: "memory");
    memcpy(&duel_host_rx_staging, data, sizeof duel_host_rx_staging);
    __asm__ volatile("" ::: "memory");
    duel_host_rx_ver++;
}

static void duel_host_housekeeping(uint32_t now) {
    uint8_t v1 = duel_host_rx_ver;
    __asm__ volatile("" ::: "memory");
    duel_host_packet_t packet = duel_host_rx_staging;
    __asm__ volatile("" ::: "memory");
    uint8_t v2 = duel_host_rx_ver;
    if (v1 == v2 && !(v1 & 1) && v1 != duel_host_rx_seen_ver) {
        duel_host_rx_seen_ver = v1;
        duel_host_result_t result = duel_host_accept(&duel_host_state, &packet);
        if (result == DUEL_HOST_APPLIED_HEARTBEAT) {
            duel_host_last_heartbeat_ms = now;
            duel_host_heartbeat_armed   = true;
        }
    }
    if (duel_host_heartbeat_armed &&
        timer_elapsed32(duel_host_last_heartbeat_ms) > DUEL_HOST_TIMEOUT_MS) {
        duel_host_expire(&duel_host_state);
        duel_host_heartbeat_armed = false;
    }
}
#endif

static volatile uint8_t duel_rx_ver;
static duel_snapshot_t  duel_rx_staging;

static void duel_snapshot_rx(uint8_t in_len, const void *in, uint8_t out_len, void *out) {
    (void)out_len;
    (void)out;
    if (in_len != sizeof(duel_snapshot_t)) return;
    duel_rx_ver++;
    __asm__ volatile("" ::: "memory");
    memcpy(&duel_rx_staging, in, sizeof duel_rx_staging);
    __asm__ volatile("" ::: "memory");
    duel_rx_ver++;
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
    if (matrix_get_row(SCRY_KEY_L_ROW) & ((matrix_row_t)1 << SCRY_KEY_L_COL)) mask |= SCRY_M_L;
    if (matrix_get_row(SCRY_KEY_R_ROW) & ((matrix_row_t)1 << SCRY_KEY_R_COL)) mask |= SCRY_M_R;
    // Any key held that is NOT one of the two layer keys disqualifies a chord
    // (this is ordinary layer-3 use) and, once the overlay is up, drives scene
    // selection — so mask it in level-sampled form like the rest of the inputs.
    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        matrix_row_t row = matrix_get_row(r);
        if (r == SCRY_KEY_L_ROW) row &= (matrix_row_t)~((matrix_row_t)1 << SCRY_KEY_L_COL);
        if (r == SCRY_KEY_R_ROW) row &= (matrix_row_t)~((matrix_row_t)1 << SCRY_KEY_R_COL);
        if (row) { mask |= SCRY_M_OTHER; break; }
    }
    return mask;
}

static sim_inputs_t duel_sample_inputs(void) {
    sim_inputs_t in = {0};
    for (uint8_t r = 0; r < DUEL_ROWS_PER_HAND; r++) {
        if (matrix_get_row(r)) { in.down_mask |= 1 << SIM_SIDE_L; break; }
    }
    for (uint8_t r = DUEL_ROWS_PER_HAND; r < MATRIX_ROWS; r++) {
        if (matrix_get_row(r)) { in.down_mask |= 1 << SIM_SIDE_R; break; }
    }
    in.scry_mask = duel_sample_scry();
    return in;
}

// Master -> slave snapshot stream.
static uint8_t  duel_session;
static bool     duel_session_set;
static uint16_t duel_tx_seq;
static uint8_t  duel_fx_sent;

static void duel_master_tx(bool urgent) {
    bool fx_changed = duel_world.fx_seq != duel_fx_sent;
    if ((duel_world.tick & 1) != 0 && !fx_changed && !urgent) return; // every 2nd tick, or immediately on presentation changes
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
    // Fails fast when the cable is out; the next snapshot lands in 80 ms.
    if (transaction_rpc_send(DUEL_SYNC_SNAPSHOT, sizeof pkt, &pkt)) {
        duel_fx_sent = duel_world.fx_seq;
    }
}

// Slave: consistent seqlock read of the latest packet, then accept/reject.
static duel_rx_state_t duel_rx;
static uint8_t         duel_rx_seen_ver;
static uint32_t        duel_last_pkt_ms;

static void duel_slave_rx_consume(void) {
    duel_snapshot_t pkt;
    uint8_t         v1 = duel_rx_ver;
    __asm__ volatile("" ::: "memory");
    memcpy(&pkt, &duel_rx_staging, sizeof pkt);
    __asm__ volatile("" ::: "memory");
    uint8_t v2 = duel_rx_ver;
    if (v1 != v2 || (v1 & 1) || v1 == duel_rx_seen_ver) return; // torn or nothing new
    duel_rx_seen_ver = v1;
    if (!duel_decode_valid(&pkt)) {
#ifdef ARCANE_DIAGNOSTICS
        if (duel_diag.split_protocol_errors < UINT16_MAX) duel_diag.split_protocol_errors++;
#endif
        return;
    }
    bool stale = timer_elapsed32(duel_last_pkt_ms) > DUEL_STALE_MS;
    if (duel_rx_accept(&duel_rx, &pkt, stale)) {
        duel_last_pkt_ms = timer_read32();
    }
}

void housekeeping_task_user(void) {
#ifdef ARCANE_DIAGNOSTICS
    uint32_t diag_start_us = time_us_32();
#endif
    uint32_t now = timer_read32();
#ifdef ARCANE_HOST_ENABLE
    if (is_keyboard_master()) duel_host_housekeeping(now);
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
    while (timer_expired32(now, duel_next_tick_ms)) {
        sim_event_t evs[SIM_EVQ_CAP + 1];
        uint8_t     n = sim_evq_drain(&duel_evq, evs);
        sim_tick(&duel_world, duel_sample_inputs(), evs, n);
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

    if (is_keyboard_master()) {
        if (ticked || display_changed) duel_master_tx(display_changed);
        duel_render.w          = duel_world;
        duel_render.stale_link = false;
#ifdef ARCANE_HOST_ENABLE
        uint8_t external         = duel_host_context(&duel_host_state);
        uint8_t alert            = duel_host_alert(&duel_host_state);
        duel_render.overlay_host = DUEL_HOST_CONTEXT_ONLINE(external);
        duel_render.overlay_scene = DUEL_HOST_CONTEXT_SCENE(external);
        duel_render.overlay_notif = DUEL_HOST_CONTEXT_NOTIF(external);
        duel_render.overlay_category = DUEL_HOST_ALERT_CATEGORY(alert);
        duel_render.overlay_priority = DUEL_HOST_ALERT_PRIORITY(alert);
        duel_render.overlay_age = DUEL_HOST_ALERT_AGE(alert);
        duel_render.overlay_persistent = DUEL_HOST_CONTEXT_PERSISTENT(external);
#else
        duel_render.overlay_host  = 0;
        duel_render.overlay_scene = 0;
        duel_render.overlay_notif = 0;
        duel_render.overlay_category = 0;
        duel_render.overlay_priority = 0;
        duel_render.overlay_age = 0;
        duel_render.overlay_persistent = 0;
#endif
    } else {
        duel_slave_rx_consume();
        bool stale = timer_elapsed32(duel_last_pkt_ms) > DUEL_STALE_MS;
#ifdef ARCANE_DIAGNOSTICS
        static bool diag_was_stale;
        if (stale && !diag_was_stale && duel_diag.stale_split_events < UINT16_MAX)
            duel_diag.stale_split_events++;
        diag_was_stale = stale;
#endif
        if (!stale && duel_rx.have_any) {
            duel_decode_world(&duel_rx.last, &duel_render.w);
            uint8_t remote_phase = DUEL_FLAGS_DISPLAY(duel_rx.last.flags);
            bool local_wake_grace = duel_local_wake_armed &&
                                    timer_elapsed32(duel_local_wake_ms) <= 120;
            if (!local_wake_grace && remote_phase <= DUEL_DISPLAY_SLEEP)
                duel_display_follow(&duel_display, (duel_display_phase_t)remote_phase, now);
            duel_render.overlay_host  = DUEL_HOST_CONTEXT_ONLINE(duel_rx.last.external);
            duel_render.overlay_scene = DUEL_HOST_CONTEXT_SCENE(duel_rx.last.external);
            duel_render.overlay_notif = DUEL_HOST_CONTEXT_NOTIF(duel_rx.last.external);
            duel_render.overlay_category = DUEL_HOST_ALERT_CATEGORY(duel_rx.last.alert);
            duel_render.overlay_priority = DUEL_HOST_ALERT_PRIORITY(duel_rx.last.alert);
            duel_render.overlay_age = DUEL_HOST_ALERT_AGE(duel_rx.last.alert);
            duel_render.overlay_persistent = DUEL_HOST_CONTEXT_PERSISTENT(duel_rx.last.external);
        } else {
            // Local pose-only fallback: never authoritative, never combat.
            duel_render.w = duel_world;
            duel_display_update(&duel_display, now);
            duel_render.overlay_host  = 0;
            duel_render.overlay_scene = 0;
            duel_render.overlay_notif = 0;
            duel_render.overlay_category = 0;
            duel_render.overlay_priority = 0;
            duel_render.overlay_age = 0;
            duel_render.overlay_persistent = 0;
        }
        duel_render.stale_link = stale;
    }
#ifdef ARCANE_DIAGNOSTICS
    duel_diag_peak(&duel_diag.peak_housekeeping_us, time_us_32() - diag_start_us);
#endif
}

// Own the whole screen on both halves (returning false stops oled_task_kb from
// drawing the default layer/keylog + logo).
bool oled_task_user(void) {
    static duel_fb_t fb;
    static duel_fb_t last_fb;
    static bool      last_fb_valid;
    static uint32_t  frame;
    static uint32_t  last_render_ms;
    static uint8_t   applied_phase = 0xFF;
    static uint8_t   seen_fx_seq, flash_frames, flash_kind;
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
        last_fb_valid = false;
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
    uint16_t redraw_ms = duel_display_redraw_ms(&duel_display);
    if (!phase_changed && timer_elapsed32(last_render_ms) < redraw_ms) return false;
    last_render_ms = now;
#ifdef ARCANE_DIAGNOSTICS
    uint32_t diag_render_start_us = time_us_32();
#endif
    // Remember the last visible style in each spell slot. Resolution clears
    // the authoritative slot, but its outcome can still scale from this local
    // presentation cache without growing combat state or the wire again.
    for (int s = 0; s < 2; s++) {
        if (duel_render.w.spell[s].active) last_spell_kind[s] = duel_render.w.spell[s].kind;
    }

    // One-shot fx: arm a short flash each time the world reports a new
    // outcome. Pure presentation — counts render frames, not sim ticks.
    if (duel_render.w.fx_seq != seen_fx_seq) {
        seen_fx_seq  = duel_render.w.fx_seq;
        flash_kind   = duel_render.w.fx_kind;
        bool defender_left = flash_kind == FX_IMPACT_L || flash_kind == FX_DEFLECT_L || flash_kind == FX_FIZZLE_L;
        flash_spell_kind = last_spell_kind[defender_left ? SIM_SIDE_R : SIM_SIDE_L];
        // Impacts linger longer than deflects/fizzles so a hit really lands.
        bool imp     = flash_kind == FX_IMPACT_L || flash_kind == FX_IMPACT_R;
        flash_frames = imp ? 12 : 8;
    } else if (flash_frames) {
        flash_frames--;
    }
    duel_render.flash_frames = flash_frames;
    duel_render.flash_kind   = flash_kind;
    duel_render.flash_spell_kind = flash_spell_kind;

    // M7 overlay content (presentation-only; drawn only while scry_is_open).
    // The layer is the emitted QMK layer — fine to READ for display; the chord
    // that opens the overlay is detected from physical positions, not this.
    duel_render.overlay_layer = get_highest_layer(layer_state);

    duel_fb_clear(&fb);
    wiz_draw_scene(&fb, &duel_render, is_keyboard_left(), frame++, hud);
    if (last_fb_valid && memcmp(&fb, &last_fb, sizeof fb) == 0) {
#ifdef ARCANE_DIAGNOSTICS
        duel_diag_peak(&duel_diag.peak_render_blit_us, time_us_32() - diag_render_start_us);
#endif
        return false;
    }
    oled_clear();
    for (int y = 0; y < DUEL_CANVAS_H; y++) {
        for (int x = 0; x < DUEL_CANVAS_W; x++) {
            if (duel_fb_get(&fb, x, y)) oled_write_pixel((uint8_t)x, (uint8_t)y, true);
        }
    }
    last_fb = fb;
    last_fb_valid = true;
#ifdef ARCANE_DIAGNOSTICS
    duel_diag_peak(&duel_diag.peak_render_blit_us, time_us_32() - diag_render_start_us);
#endif
    return false;
}

#endif
