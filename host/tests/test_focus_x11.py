"""Parsing and privacy tests for the X11 focus producer.

No X server is required: xprop output is captured text, and the reporter is
driven through a fake run() so the whole path is exercised offline.
"""

from __future__ import annotations

import subprocess
import unittest
from pathlib import Path

from arcane_host.focus_x11 import (
    FocusReporter,
    parse_active_window,
    parse_wm_class,
    read_resource_class,
    watch,
)

ROOT = Path(__file__).parents[1]


class Completed:
    def __init__(self, stdout: str, returncode: int = 0) -> None:
        self.stdout = stdout
        self.returncode = returncode


class ActiveWindowParsingTests(unittest.TestCase):
    def test_reads_the_window_id_and_ignores_everything_else(self) -> None:
        self.assertEqual(
            parse_active_window("_NET_ACTIVE_WINDOW(WINDOW): window id # 0x3a00007"),
            "0x3a00007",
        )
        # A property list reports the active window first.
        self.assertEqual(
            parse_active_window("_NET_ACTIVE_WINDOW(WINDOW): window id # 0x1400003, 0x0"),
            "0x1400003",
        )

    def test_no_focused_window_is_not_a_window_id(self) -> None:
        for line in (
            "_NET_ACTIVE_WINDOW(WINDOW): window id # 0x0",
            "_NET_ACTIVE_WINDOW(WINDOW): window id # none",
            "_NET_ACTIVE_WINDOW:  not found.",
            'WM_NAME(STRING) = "a title that must never be parsed"',
            "",
        ):
            self.assertIsNone(parse_active_window(line), line)


class ResourceClassParsingTests(unittest.TestCase):
    def test_prefers_the_class_over_the_instance(self) -> None:
        self.assertEqual(parse_wm_class('WM_CLASS(STRING) = "navigator", "Firefox"'), "Firefox")
        self.assertEqual(parse_wm_class('WM_CLASS(STRING) = "code", "Code"'), "Code")

    def test_single_valued_and_absent_properties(self) -> None:
        self.assertEqual(parse_wm_class('WM_CLASS(STRING) = "xterm"'), "xterm")
        self.assertEqual(parse_wm_class("WM_CLASS:  not found."), "")
        self.assertEqual(parse_wm_class(""), "")

    def test_failures_report_no_application_rather_than_guessing(self) -> None:
        self.assertEqual(read_resource_class("0x1", lambda *a, **k: Completed("", 1)), "")

        def explode(*_args, **_kwargs):
            raise OSError("xprop vanished")

        self.assertEqual(read_resource_class("0x1", explode), "")

        def slow(*_args, **_kwargs):
            raise subprocess.TimeoutExpired("xprop", 2.0)

        self.assertEqual(read_resource_class("0x1", slow), "")


class ReporterTests(unittest.TestCase):
    def test_reports_only_on_change(self) -> None:
        sent: list[tuple[str, str]] = []
        reporter = FocusReporter(lambda cls, desktop: sent.append((cls, desktop)))
        self.assertTrue(reporter.offer("Firefox"))
        self.assertFalse(reporter.offer("Firefox"))
        self.assertTrue(reporter.offer("Code"))
        self.assertEqual(sent, [("Firefox", ""), ("Code", "")])

    def test_losing_focus_reports_no_application(self) -> None:
        sent: list[tuple[str, str]] = []
        reporter = FocusReporter(lambda cls, desktop: sent.append((cls, desktop)))
        reporter.offer("Firefox")
        self.assertTrue(reporter.offer(""))
        self.assertEqual(sent[-1], ("", ""))

    def test_watch_drives_the_full_path_from_xprop_lines(self) -> None:
        sent: list[tuple[str, str]] = []
        classes = {
            "0x1": 'WM_CLASS(STRING) = "navigator", "Firefox"',
            "0x2": 'WM_CLASS(STRING) = "code", "Code"',
        }

        def run(argv, **_kwargs):
            return Completed(classes[argv[2]])

        watch(
            FocusReporter(lambda cls, desktop: sent.append((cls, desktop))),
            iter(
                [
                    "_NET_ACTIVE_WINDOW(WINDOW): window id # 0x1",
                    "some unrelated line",
                    "_NET_ACTIVE_WINDOW(WINDOW): window id # 0x1",
                    "_NET_ACTIVE_WINDOW(WINDOW): window id # 0x2",
                ]
            ),
            run,
        )
        self.assertEqual(sent, [("Firefox", ""), ("Code", "")])


class PrivacyBoundaryTests(unittest.TestCase):
    def test_source_can_never_request_a_title_bearing_property(self) -> None:
        """The boundary is structural: no title property is nameable in the source.

        Reviewers should be able to confirm the guarantee by reading the file,
        which is only true while these strings stay absent.
        """
        source = (ROOT / "arcane_host" / "focus_x11.py").read_text()
        for forbidden in (
            "WM_NAME",
            "_NET_WM_NAME",
            "_NET_WM_PID",
            "WM_ICON_NAME",
            "-name",
            "-root -spy WM_",
        ):
            self.assertNotIn(forbidden, source, f"{forbidden} must not be reachable")
        # Exactly the two properties the producer is allowed to read.
        self.assertIn("_NET_ACTIVE_WINDOW", source)
        self.assertIn("WM_CLASS", source)


if __name__ == "__main__":
    unittest.main()
