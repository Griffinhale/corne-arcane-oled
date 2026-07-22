from __future__ import annotations

import unittest
from pathlib import Path


class ZshHookTests(unittest.TestCase):
    def test_hook_is_redacted_async_and_thresholded(self) -> None:
        text = (Path(__file__).parents[1] / "zsh" / "corne-arcane.zsh").read_text()
        self.assertIn("/proc/uptime", text)
        self.assertIn("_elapsed_ms >= 10000", text)
        self.assertIn("corne-arcane-event terminal $_elapsed_ms $_status", text)
        self.assertIn("&!", text)
        self.assertNotIn("$1", text)
        self.assertNotIn("$PWD", text)


if __name__ == "__main__":
    unittest.main()
