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

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT_split_3x6_3(
  //,-----------------------------------------------------.                    ,-----------------------------------------------------.
       KC_TAB,    KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,                         KC_Y,    KC_U,    KC_I,    KC_O,   KC_P,  KC_BSPC,
  //|--------+--------+--------+--------+--------+--------|                    |--------+--------+--------+--------+--------+--------|
      KC_LCTL,    KC_A,    KC_S,    KC_D,    KC_F,    KC_G,                         KC_H,    KC_J,    KC_K,    KC_L, KC_SCLN, KC_QUOT,
  //|--------+--------+--------+--------+--------+--------|                    |--------+--------+--------+--------+--------+--------|
      KC_LSFT,    KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,                         KC_N,    KC_M, KC_COMM,  KC_DOT, KC_SLSH,  KC_ESC,
  //|--------+--------+--------+--------+--------+--------+--------|  |--------+--------+--------+--------+--------+--------+--------|
                                          KC_LGUI,   MO(1),  KC_SPC,     KC_ENT,   MO(2), KC_RALT
                                      //`--------------------------'  `--------------------------'

  ),

    [1] = LAYOUT_split_3x6_3(
  //,-----------------------------------------------------.                    ,-----------------------------------------------------.
       KC_TAB,    KC_1,    KC_2,    KC_3,    KC_4,    KC_5,                         KC_6,    KC_7,    KC_8,    KC_9,    KC_0, KC_BSPC,
  //|--------+--------+--------+--------+--------+--------|                    |--------+--------+--------+--------+--------+--------|
      KC_LCTL, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,                      KC_LEFT, KC_DOWN,   KC_UP,KC_RIGHT, XXXXXXX, XXXXXXX,
  //|--------+--------+--------+--------+--------+--------|                    |--------+--------+--------+--------+--------+--------|
      KC_LSFT, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,                      XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,
  //|--------+--------+--------+--------+--------+--------+--------|  |--------+--------+--------+--------+--------+--------+--------|
                                          KC_LGUI, _______,  KC_SPC,     KC_ENT,   MO(3), KC_RALT
                                      //`--------------------------'  `--------------------------'
  ),

    [2] = LAYOUT_split_3x6_3(
  //,-----------------------------------------------------.                    ,-----------------------------------------------------.
       KC_TAB, KC_EXLM,   KC_AT, KC_HASH,  KC_DLR, KC_PERC,                      KC_CIRC, KC_AMPR, KC_ASTR, KC_LPRN, KC_RPRN, KC_BSPC,
  //|--------+--------+--------+--------+--------+--------|                    |--------+--------+--------+--------+--------+--------|
      KC_LCTL, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,                      KC_MINS,  KC_EQL, KC_LBRC, KC_RBRC, KC_BSLS,  KC_GRV,
  //|--------+--------+--------+--------+--------+--------|                    |--------+--------+--------+--------+--------+--------|
      KC_LSFT, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,                      KC_UNDS, KC_PLUS, KC_LCBR, KC_RCBR, KC_PIPE, KC_TILD,
  //|--------+--------+--------+--------+--------+--------+--------|  |--------+--------+--------+--------+--------+--------+--------|
                                          KC_LGUI,   MO(3),  KC_SPC,     KC_ENT, _______, KC_RALT
                                      //`--------------------------'  `--------------------------'
  ),

    [3] = LAYOUT_split_3x6_3(
  //,-----------------------------------------------------.                    ,-----------------------------------------------------.
      QK_BOOT, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,                      XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,
  //|--------+--------+--------+--------+--------+--------|                    |--------+--------+--------+--------+--------+--------|
      RM_TOGG, RM_HUEU, RM_SATU, RM_VALU, XXXXXXX, XXXXXXX,                      XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,
  //|--------+--------+--------+--------+--------+--------|                    |--------+--------+--------+--------+--------+--------|
      RM_NEXT, RM_HUED, RM_SATD, RM_VALD, XXXXXXX, XXXXXXX,                      XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,
  //|--------+--------+--------+--------+--------+--------+--------|  |--------+--------+--------+--------+--------+--------+--------|
                                          KC_LGUI, _______,  KC_SPC,     KC_ENT, _______, KC_RALT
                                      //`--------------------------'  `--------------------------'
  )
};

#ifdef ENCODER_MAP_ENABLE
const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][NUM_DIRECTIONS] = {
  [0] = { ENCODER_CCW_CW(KC_VOLD, KC_VOLU), ENCODER_CCW_CW(KC_MPRV, KC_MNXT), ENCODER_CCW_CW(RM_VALD, RM_VALU), ENCODER_CCW_CW(KC_RGHT, KC_LEFT), },
  [1] = { ENCODER_CCW_CW(KC_VOLD, KC_VOLU), ENCODER_CCW_CW(KC_MPRV, KC_MNXT), ENCODER_CCW_CW(RM_VALD, RM_VALU), ENCODER_CCW_CW(KC_RGHT, KC_LEFT), },
  [2] = { ENCODER_CCW_CW(KC_VOLD, KC_VOLU), ENCODER_CCW_CW(KC_MPRV, KC_MNXT), ENCODER_CCW_CW(RM_VALD, RM_VALU), ENCODER_CCW_CW(KC_RGHT, KC_LEFT), },
  [3] = { ENCODER_CCW_CW(KC_VOLD, KC_VOLU), ENCODER_CCW_CW(KC_MPRV, KC_MNXT), ENCODER_CCW_CW(RM_VALD, RM_VALU), ENCODER_CCW_CW(KC_RGHT, KC_LEFT), },
};
#endif


#ifdef OLED_ENABLE

#include <string.h>

#include "transactions.h"

#include "sim/duel_draw.h"
#include "sim/duel_proto.h"
#include "sim/duel_sim.h"

// Portrait canvas must match the rotated OLED exactly (see duel_draw.h).
_Static_assert(DUEL_CANVAS_W == OLED_DISPLAY_HEIGHT && DUEL_CANVAS_H == OLED_DISPLAY_WIDTH,
               "duel canvas dimensions must match the rotated OLED");

// M2 verification overlay (tick odometer + overflow dots). Cheap; keep it on
// until M2 sign-off, then comment out.
#define DUEL_DEBUG_HUD

// crkbd/rev1: 8 matrix rows, 4 per hand (left = 0..3, right = 4..7).
#define DUEL_ROWS_PER_HAND (MATRIX_ROWS / 2)

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
 * The master's world is authoritative; it streams 26-byte snapshots to the
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

static void duel_master_tx(void) {
    bool fx_changed = duel_world.fx_seq != duel_fx_sent;
    if ((duel_world.tick & 1) != 0 && !fx_changed) return; // every 2nd tick, or immediately on an outcome
    if (!duel_session_set) {
        // Boot nonce: USB-enumeration timing jitter is the entropy; the
        // slave's stale override backstops the rare collision.
        uint32_t t0     = timer_read32();
        duel_session    = (uint8_t)(t0 ^ (t0 >> 8));
        duel_session_set = true;
    }
    duel_snapshot_t pkt;
    duel_encode(&duel_world, duel_session, ++duel_tx_seq, &pkt);
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
    if (!duel_decode_valid(&pkt)) return;
    bool stale = timer_elapsed32(duel_last_pkt_ms) > DUEL_STALE_MS;
    if (duel_rx_accept(&duel_rx, &pkt, stale)) {
        duel_last_pkt_ms = timer_read32();
    }
}

void housekeeping_task_user(void) {
    uint32_t now = timer_read32();
    if (!duel_tick_armed) {
        sim_init(&duel_world, is_keyboard_master() ? SIMF_AUTHORITATIVE : 0, 0);
        duel_next_tick_ms = now + SIM_TICK_MS;
        duel_tick_armed   = true;
    }
    bool    ticked = false;
    uint8_t guard  = 0;
    while (timer_expired32(now, duel_next_tick_ms)) {
        sim_event_t evs[SIM_EVQ_CAP + 1];
        uint8_t     n = sim_evq_drain(&duel_evq, evs);
        sim_tick(&duel_world, duel_sample_inputs(), evs, n);
        ticked = true;
        duel_next_tick_ms += SIM_TICK_MS;
        if (++guard >= 5) { // long stall (USB suspend): resync instead of replaying
            duel_next_tick_ms = now + SIM_TICK_MS;
            break;
        }
    }

    if (is_keyboard_master()) {
        if (ticked) duel_master_tx();
        duel_render.w          = duel_world;
        duel_render.stale_link = false;
    } else {
        duel_slave_rx_consume();
        bool stale = timer_elapsed32(duel_last_pkt_ms) > DUEL_STALE_MS;
        if (!stale && duel_rx.have_any) {
            duel_decode_world(&duel_rx.last, &duel_render.w);
        } else {
            // Local pose-only fallback: never authoritative, never combat.
            duel_render.w = duel_world;
        }
        duel_render.stale_link = stale;
    }
}

// Own the whole screen on both halves (returning false stops oled_task_kb from
// drawing the default layer/keylog + logo).
bool oled_task_user(void) {
    static duel_fb_t fb;
    static uint32_t  frame;
    static uint8_t   seen_fx_seq, flash_frames, flash_kind;
#ifdef DUEL_DEBUG_HUD
    const bool hud = true;
#else
    const bool hud = false;
#endif
    // One-shot fx: arm a short flash each time the world reports a new
    // outcome. Pure presentation — counts render frames, not sim ticks.
    if (duel_render.w.fx_seq != seen_fx_seq) {
        seen_fx_seq  = duel_render.w.fx_seq;
        flash_kind   = duel_render.w.fx_kind;
        // Impacts linger longer than deflects/fizzles so a hit really lands.
        bool imp     = flash_kind == FX_IMPACT_L || flash_kind == FX_IMPACT_R;
        flash_frames = imp ? 12 : 8;
    } else if (flash_frames) {
        flash_frames--;
    }
    duel_render.flash_frames = flash_frames;
    duel_render.flash_kind   = flash_kind;

    // M7 overlay content (presentation-only; drawn only while scry_is_open).
    // The layer is the emitted QMK layer — fine to READ for display; the chord
    // that opens the overlay is detected from physical positions, not this.
    // Host link and notification count are M8 territory, so they read as the
    // no-daemon stubs until that milestone lands.
    duel_render.overlay_layer = get_highest_layer(layer_state);
    duel_render.overlay_host  = 0; // offline: no host heartbeat yet (M8)
    duel_render.overlay_notif = 0; // no notifications yet (M8)

    duel_fb_clear(&fb);
    wiz_draw_scene(&fb, &duel_render, is_keyboard_left(), frame++, hud);
    oled_clear();
    for (int y = 0; y < DUEL_CANVAS_H; y++) {
        for (int x = 0; x < DUEL_CANVAS_W; x++) {
            if (duel_fb_get(&fb, x, y)) oled_write_pixel((uint8_t)x, (uint8_t)y, true);
        }
    }
    return false;
}

#endif
