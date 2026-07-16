#pragma once

// Static default captured from the corne-arcane.vil export in the project
// parent's directory. The unified firmware uses it both as the compiled
// default and as the seed for persistent dynamic-keymap EEPROM.
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT_split_3x6_3(
        KC_ESC,  KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,                   KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,    KC_BSPC,
        KC_TAB,  KC_A,    KC_S,    KC_D,    KC_F,    KC_G,                   KC_H,    KC_J,    KC_K,    KC_L,    KC_SCLN, KC_QUOT,
        KC_LSFT, KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,                   KC_N,    KC_M,    KC_COMM, KC_DOT,  KC_SLSH, KC_RSFT,
                                   KC_LCTL, MO(1),   KC_SPC,                 KC_SPC,  MO(2),   KC_ENT
    ),

    [1] = LAYOUT_split_3x6_3(
        KC_TAB,  KC_1,    KC_2,    KC_3,    KC_4,    KC_5,                   KC_6,    KC_7,    KC_8,    KC_9,    KC_0,    KC_BSPC,
        KC_LCTL, XXXXXXX, KC_F5,   KC_F11,  KC_F12,  KC_PSCR,                KC_LEFT, KC_DOWN, KC_UP,   KC_RGHT, XXXXXXX, XXXXXXX,
        KC_LSFT, XXXXXXX, KC_PGUP, KC_PGDN, KC_HOME, KC_END,                 XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, KC_ENT,
                                   KC_ENT,  _______, KC_SPC,                 KC_SPC,  MO(3),   KC_RALT
    ),

    [2] = LAYOUT_split_3x6_3(
        KC_ESC,  KC_EXLM, KC_AT,   KC_HASH, KC_DLR,  KC_PERC,                KC_CIRC, KC_AMPR, KC_ASTR, KC_LPRN, KC_RPRN, KC_BSPC,
        KC_ENT,  XXXXXXX, KC_F5,   KC_F11,  KC_F12,  KC_PSCR,                KC_MINS, KC_EQL,  KC_LBRC, KC_RBRC, KC_BSLS, KC_GRV,
        KC_LSFT, XXXXXXX, KC_PGUP, KC_PGDN, KC_HOME, KC_END,                 KC_UNDS, KC_PLUS, KC_LCBR, KC_RCBR, KC_PIPE, KC_TILD,
                                   KC_LGUI, MO(3),   KC_SPC,                 KC_SPC,  _______, KC_RALT
    ),

    [3] = LAYOUT_split_3x6_3(
        QK_BOOT, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,                KC_F1,   KC_F2,   KC_F3,   KC_F4,   XXXXXXX, KC_BSPC,
        RM_TOGG, RM_HUEU, RM_SATU, RM_VALU, XXXXXXX, XXXXXXX,                KC_F5,   KC_F6,   KC_F7,   KC_F8,   XXXXXXX, XXXXXXX,
        RM_NEXT, RM_HUED, RM_SATD, RM_VALD, XXXXXXX, XXXXXXX,                KC_F9,   KC_F10,  KC_F11,  KC_F12,  XXXXXXX, KC_ENT,
                                   KC_LGUI, _______, KC_SPC,                 KC_SPC,  _______, KC_RALT
    )
};
