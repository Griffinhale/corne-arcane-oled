from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(
    (ROOT / "firmware").is_dir(),
    "full-repository firmware contract is outside the host-only package source",
)
class UnifiedFirmwareContractTests(unittest.TestCase):
    def test_vial_raw_hid_and_only_required_features(self) -> None:
        rules = (ROOT / "firmware" / "rules.mk").read_text()
        for feature in (
            "VIA_ENABLE",
            "VIAL_ENABLE",
            "RAW_ENABLE",
            "OLED_ENABLE",
            "RGB_MATRIX_ENABLE",
            "LTO_ENABLE",
        ):
            self.assertRegex(rules, rf"(?m)^{feature}\s*=\s*yes$")
        for feature in (
            "QMK_SETTINGS",
            "DYNAMIC_MACRO_ENABLE",
            "TAP_DANCE_ENABLE",
            "COMBO_ENABLE",
            "KEY_OVERRIDE_ENABLE",
            "CAPS_WORD_ENABLE",
            "LAYER_LOCK_ENABLE",
            "REPEAT_KEY_ENABLE",
            "ENCODER_MAP_ENABLE",
        ):
            self.assertRegex(rules, rf"(?m)^{feature}\s*=\s*no$")

    def test_secure_four_layer_eeprom_seed(self) -> None:
        config = (ROOT / "firmware" / "config.h").read_text()
        layout = (ROOT / "firmware" / "corne_arcane_layout.h").read_text()
        self.assertIn("#define DYNAMIC_KEYMAP_LAYER_COUNT 4", config)
        self.assertIn("#define DYNAMIC_KEYMAP_MACRO_COUNT 0", config)
        self.assertIn("VIAL_UNLOCK_COMBO_ROWS", config)
        self.assertNotIn("VIAL_INSECURE", config)
        self.assertEqual(len(re.findall(r"(?m)^\s*\[[0-3]\]\s*=", layout)), 4)

    def test_rgb_is_world_owned_not_keymap_or_eeprom_owned(self) -> None:
        layout = (ROOT / "firmware" / "corne_arcane_layout.h").read_text()
        keymap = (ROOT / "firmware" / "keymap.c").read_text()
        self.assertNotRegex(
            layout, r"\bRM_(?:ON|OFF|TOGG|NEXT|PREV|HUEU|HUED|SATU|SATD|VALU|VALD|SPDU|SPDD)\b"
        )
        self.assertIn("return !IS_RGB_MATRIX_KEYCODE(keycode);", keymap)
        self.assertIn("rgb_matrix_enable_noeeprom();", keymap)
        self.assertIn("rgb_matrix_indicators_advanced_user", keymap)

    def test_daemon_uses_via_unknown_command_hook(self) -> None:
        keymap = (ROOT / "firmware" / "keymap.c").read_text()
        self.assertIn("void raw_hid_receive_kb(uint8_t *data, uint8_t length)", keymap)
        self.assertNotIn("void raw_hid_receive(uint8_t *data, uint8_t length)", keymap)
        self.assertIn("data[0] != DUEL_HOST_MAGIC0", keymap)
        self.assertIn("data[0] = id_unhandled", keymap)

    def test_current_host_protocol_has_no_compatibility_payload(self) -> None:
        header = (ROOT / "firmware" / "sim" / "duel_host.h").read_text()
        source = (ROOT / "firmware" / "sim" / "duel_host.c").read_text()
        self.assertNotIn("VERSION_V1", header + source)
        self.assertIn("#define DUEL_HOST_PAYLOAD_LEN       8", header)
        # The validator requires the exact payload length (no v1 fallback).
        # Accept either the accepting or rejecting phrasing of the comparison.
        self.assertTrue(
            "packet->payload_len == DUEL_HOST_PAYLOAD_LEN" in source
            or "packet->payload_len != DUEL_HOST_PAYLOAD_LEN" in source
        )


if __name__ == "__main__":
    unittest.main()
