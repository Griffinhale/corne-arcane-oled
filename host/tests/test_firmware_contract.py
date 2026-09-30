from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# An object-like #define: name, then a value up to any trailing comment. How
# the value is aligned is clang-format's business, not the contract's.
DEFINE = re.compile(r"(?m)^[ \t]*#[ \t]*define[ \t]+(\w+)[ \t]+(.+?)[ \t]*(?:/[*/].*)?$")


def defines(text: str) -> dict[str, str]:
    return dict(DEFINE.findall(text))


def code_pattern(snippet: str) -> str:
    """A regex for snippet in which any run of whitespace, or none beside punctuation, matches."""
    tokens = re.findall(r"\w+|[^\w\s]", snippet)
    return r"\s*".join(re.escape(token) for token in tokens)


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
        self.assertEqual(defines(config).get("DYNAMIC_KEYMAP_LAYER_COUNT"), "4")
        self.assertEqual(defines(config).get("DYNAMIC_KEYMAP_MACRO_COUNT"), "0")
        self.assertIn("VIAL_UNLOCK_COMBO_ROWS", config)
        self.assertNotIn("VIAL_INSECURE", config)
        self.assertEqual(len(re.findall(r"(?m)^\s*\[[0-3]\]\s*=", layout)), 4)

    def test_rgb_is_world_owned_not_keymap_or_eeprom_owned(self) -> None:
        layout = (ROOT / "firmware" / "corne_arcane_layout.h").read_text()
        keymap = (ROOT / "firmware" / "keymap.c").read_text()
        self.assertNotRegex(
            layout, r"\bRM_(?:ON|OFF|TOGG|NEXT|PREV|HUEU|HUED|SATU|SATD|VALU|VALD|SPDU|SPDD)\b"
        )
        self.assertRegex(keymap, code_pattern("return !IS_RGB_MATRIX_KEYCODE(keycode);"))
        self.assertRegex(keymap, code_pattern("rgb_matrix_enable_noeeprom();"))
        self.assertRegex(keymap, r"\brgb_matrix_indicators_advanced_user\b")

    def test_daemon_uses_via_unknown_command_hook(self) -> None:
        keymap = (ROOT / "firmware" / "keymap.c").read_text()
        self.assertRegex(
            keymap, code_pattern("void raw_hid_receive_kb(uint8_t *data, uint8_t length)")
        )
        self.assertNotRegex(
            keymap, code_pattern("void raw_hid_receive(uint8_t *data, uint8_t length)")
        )
        self.assertRegex(keymap, code_pattern("data[0] != DUEL_HOST_MAGIC0"))
        self.assertRegex(keymap, code_pattern("data[0] = id_unhandled"))

    def test_host_protocol_constants(self) -> None:
        # That the validator rejects any other payload length is behaviour, and
        # firmware/sim_test/test_protocol_view.c checks it by running it.
        header = defines((ROOT / "firmware" / "sim" / "duel_host.h").read_text())
        self.assertEqual(header.get("DUEL_HOST_PAYLOAD_LEN"), "8")

    def test_define_parsing_ignores_alignment_but_not_value(self) -> None:
        self.assertEqual(defines("#define DUEL_HOST_PAYLOAD_LEN 8\n")["DUEL_HOST_PAYLOAD_LEN"], "8")
        self.assertEqual(
            defines("  #  define DUEL_HOST_PAYLOAD_LEN\t\t8 /* bytes */\n")[
                "DUEL_HOST_PAYLOAD_LEN"
            ],
            "8",
        )
        self.assertNotEqual(
            defines("#define DUEL_HOST_PAYLOAD_LEN 9\n")["DUEL_HOST_PAYLOAD_LEN"], "8"
        )
        self.assertRegex("data [0]!=DUEL_HOST_MAGIC0", code_pattern("data[0] != DUEL_HOST_MAGIC0"))
        self.assertNotRegex(
            "data[1] != DUEL_HOST_MAGIC0", code_pattern("data[0] != DUEL_HOST_MAGIC0")
        )


if __name__ == "__main__":
    unittest.main()
