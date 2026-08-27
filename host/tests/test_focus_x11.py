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
    parse_gtk_application_id,
    parse_wm_class,
    read_identity,
    watch,
)
from arcane_host.profiles import resolve_profile

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
    def test_returns_both_class_and_instance(self) -> None:
        self.assertEqual(
            parse_wm_class('WM_CLASS(STRING) = "navigator", "Firefox"'), ("Firefox", "navigator")
        )
        self.assertEqual(parse_wm_class('WM_CLASS(STRING) = "code", "Code"'), ("Code", "code"))

    def test_single_valued_and_absent_properties(self) -> None:
        self.assertEqual(parse_wm_class('WM_CLASS(STRING) = "xterm"'), ("xterm", ""))
        self.assertEqual(parse_wm_class("WM_CLASS:  not found."), ("", ""))
        self.assertEqual(parse_wm_class(""), ("", ""))

    def test_gtk_application_id_is_read_per_property_not_per_position(self) -> None:
        text = 'WM_CLASS(STRING) = "nemo", "Nemo"\n_GTK_APPLICATION_ID(STRING) = "org.Nemo"\n'
        self.assertEqual(parse_gtk_application_id(text), "org.Nemo")
        self.assertEqual(parse_wm_class(text), ("Nemo", "nemo"))
        # An unset property must not shift the other one's value.
        unset = 'WM_CLASS(STRING) = "xterm", "XTerm"\n_GTK_APPLICATION_ID:  not found.\n'
        self.assertEqual(parse_gtk_application_id(unset), "")
        self.assertEqual(parse_wm_class(unset), ("XTerm", "xterm"))

    def test_application_id_wins_the_second_slot_over_the_instance(self) -> None:
        gtk = 'WM_CLASS(STRING) = "nemo", "Nemo"\n_GTK_APPLICATION_ID(STRING) = "org.Nemo"\n'
        self.assertEqual(read_identity("0x1", lambda *a, **k: Completed(gtk)), ("Nemo", "org.Nemo"))
        plain = 'WM_CLASS(STRING) = "navigator", "Firefox"\n_GTK_APPLICATION_ID:  not found.\n'
        self.assertEqual(
            read_identity("0x1", lambda *a, **k: Completed(plain)), ("Firefox", "navigator")
        )

    def test_both_identifiers_are_requested_in_one_call(self) -> None:
        seen: list[tuple] = []

        def run(argv, **_kwargs):
            seen.append(argv)
            return Completed('WM_CLASS(STRING) = "a", "B"')

        read_identity("0x1", run)
        self.assertEqual(len(seen), 1, "one xprop invocation per focus change")

    def test_failures_report_no_application_rather_than_guessing(self) -> None:
        self.assertEqual(read_identity("0x1", lambda *a, **k: Completed("", 1)), ("", ""))

        def explode(*_args, **_kwargs):
            raise OSError("xprop vanished")

        self.assertEqual(read_identity("0x1", explode), ("", ""))

        def slow(*_args, **_kwargs):
            raise subprocess.TimeoutExpired("xprop", 2.0)

        self.assertEqual(read_identity("0x1", slow), ("", ""))


class ReporterTests(unittest.TestCase):
    def test_reports_only_on_change(self) -> None:
        sent: list[tuple[str, str]] = []
        reporter = FocusReporter(lambda cls, app: sent.append((cls, app)))
        self.assertTrue(reporter.offer(("Firefox", "navigator")))
        self.assertFalse(reporter.offer(("Firefox", "navigator")))
        self.assertTrue(reporter.offer(("Code", "code")))
        self.assertEqual(sent, [("Firefox", "navigator"), ("Code", "code")])

    def test_losing_focus_reports_no_application(self) -> None:
        sent: list[tuple[str, str]] = []
        reporter = FocusReporter(lambda cls, app: sent.append((cls, app)))
        reporter.offer(("Firefox", "navigator"))
        self.assertTrue(reporter.offer(("", "")))
        self.assertEqual(sent[-1], ("", ""))

    def test_watch_drives_the_full_path_from_xprop_lines(self) -> None:
        sent: list[tuple[str, str]] = []
        windows = {
            "0x1": 'WM_CLASS(STRING) = "navigator", "Firefox"',
            "0x2": 'WM_CLASS(STRING) = "nemo", "Nemo"\n_GTK_APPLICATION_ID(STRING) = "org.Nemo"\n',
        }

        def run(argv, **_kwargs):
            return Completed(windows[argv[2]])

        watch(
            FocusReporter(lambda cls, app: sent.append((cls, app))),
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
        self.assertEqual(sent, [("Firefox", "navigator"), ("Nemo", "org.Nemo")])

    def test_second_identifier_rescues_applications_the_class_misses(self) -> None:
        """The point of sending both: either one may be the one that resolves.

        Distribution builds decorate the class while leaving the instance as the
        upstream name, so the class alone loses applications the table knows.
        """
        resource_class, instance = parse_wm_class('WM_CLASS(STRING) = "code", "Code - OSS"')
        self.assertEqual((resource_class, instance), ("Code - OSS", "code"))
        self.assertIsNone(resolve_profile(resource_class))
        self.assertEqual(resolve_profile(resource_class, instance).identifier, "code")

    def test_gtk_application_id_rescues_what_neither_wm_class_string_knows(self) -> None:
        resource_class, application_id = read_identity(
            "0x1",
            lambda *a, **k: Completed(
                'WM_CLASS(STRING) = "eog", "Image Viewer"\n'
                '_GTK_APPLICATION_ID(STRING) = "org.gnome.eog"\n'
            ),
        )
        self.assertIsNone(resolve_profile(resource_class))
        self.assertEqual(resolve_profile(resource_class, application_id).identifier, "studio")


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
        # Exactly the three properties the producer is allowed to read, all of
        # which are identities rather than content.
        self.assertIn("_NET_ACTIVE_WINDOW", source)
        self.assertIn("WM_CLASS", source)
        self.assertIn("_GTK_APPLICATION_ID", source)


if __name__ == "__main__":
    unittest.main()
