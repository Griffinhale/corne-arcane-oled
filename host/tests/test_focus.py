from __future__ import annotations

import unittest

from arcane_host.daemon import parse_args
from arcane_host.focus import FocusArbiter, classify_window, is_terminal, normalize_identifier
from arcane_host.protocol import Scene


class FocusTests(unittest.TestCase):
    def test_identifier_normalization(self) -> None:
        self.assertEqual(normalize_identifier(" /opt/apps/Firefox_ESR.desktop "), "firefox-esr")
        self.assertEqual(normalize_identifier(None), "")

    def test_browser_aliases(self) -> None:
        aliases = (
            ("firefox", ""),
            ("", "org.mozilla.firefox"),
            ("Google-Chrome", ""),
            ("chromium-browser", ""),
            ("Brave-browser", ""),
            ("vivaldi-stable", ""),
            ("zen-browser", ""),
        )
        for resource_class, desktop_file_name in aliases:
            with self.subTest(resource_class=resource_class, desktop=desktop_file_name):
                self.assertEqual(classify_window(resource_class, desktop_file_name), Scene.ARCHIVE)

    def test_empty_unknown_and_desktop_are_duel(self) -> None:
        for pair in (("", ""), ("org.kde.kate", "org.kde.kate"), ("plasmashell", "org.kde.plasmashell")):
            self.assertEqual(classify_window(*pair), Scene.DUEL)

    def test_settles_for_200_ms(self) -> None:
        focus = FocusArbiter()
        self.assertEqual(focus.scene, Scene.DUEL)
        focus.report("firefox", "", 10.0)
        self.assertEqual(focus.poll(10.199), Scene.DUEL)
        self.assertEqual(focus.poll(10.2), Scene.ARCHIVE)

    def test_rapid_alt_tab_cancels_pending_archive(self) -> None:
        focus = FocusArbiter()
        focus.report("firefox", "", 1.0)
        focus.report("org.kde.konsole", "", 1.1)
        self.assertEqual(focus.poll(1.3), Scene.DUEL)

    def test_manual_override_is_explicit(self) -> None:
        self.assertIsNone(parse_args([]).scene)
        self.assertEqual(parse_args(["--scene", "focus"]).scene, "focus")

    def test_coarse_terminal_focus_and_digest_only_retention(self) -> None:
        focus = FocusArbiter(settle_seconds=0, identifier_digest=lambda value: value.encode())
        self.assertTrue(is_terminal("org.kde.konsole", ""))
        focus.report("org.kde.konsole", "org.kde.konsole.desktop", 1)
        focus.poll(1)
        self.assertTrue(focus.terminal_focused)
        self.assertEqual(focus.focused_digests, frozenset({b"org.kde.konsole"}))


if __name__ == "__main__":
    unittest.main()
