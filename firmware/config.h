/*
This is the c configuration file for the keymap

Copyright 2012 Jun Wako <wakojun@gmail.com>
Copyright 2015 Jack Humbert

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

#pragma once

// Inherited RGBLIGHT stays disabled (rules.mk pins RGBLIGHT_ENABLE = no);
// lighting is RGB Matrix, owned by the world sim in keymap.c.

#define TAPPING_TERM 180

#define VIAL_KEYBOARD_UID      {0x3B, 0x6B, 0xA0, 0x29, 0x80, 0x56, 0xED, 0xD1}
#define VIAL_UNLOCK_COMBO_ROWS {0, 0}
#define VIAL_UNLOCK_COMBO_COLS {0, 1}
#undef DYNAMIC_KEYMAP_LAYER_COUNT
#define DYNAMIC_KEYMAP_LAYER_COUNT 4
#undef DYNAMIC_KEYMAP_MACRO_COUNT
#define DYNAMIC_KEYMAP_MACRO_COUNT 0

// Corne Arcane owns OLED power explicitly so host traffic cannot reset QMK's generic
// timeout. Physical matrix activity is the sole wake source.
#define OLED_BRIGHTNESS      128
#define OLED_TIMEOUT         0
#define OLED_UPDATE_INTERVAL 50

// split: user split RPC carrying the duel world snapshot (master -> slave).
#define SPLIT_TRANSACTION_IDS_USER DUEL_SYNC_SNAPSHOT
