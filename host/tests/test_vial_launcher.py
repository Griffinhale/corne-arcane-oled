from __future__ import annotations

import contextlib
import io
import signal
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from arcane_host import hid_ownership, vial_launcher


class OwnershipGuardTests(unittest.TestCase):
    def test_active_service_stops_waits_runs_and_restores(self) -> None:
        order: list[str] = []
        with (
            patch.object(hid_ownership, "service_is_active", return_value=True),
            patch.object(hid_ownership, "service_main_pid", return_value=42),
            patch.object(hid_ownership, "stop_service", side_effect=lambda: order.append("stop")),
            patch.object(
                hid_ownership,
                "wait_for_hidraw_release",
                side_effect=lambda pid, timeout: order.append(f"wait:{pid}:{timeout}"),
            ),
            patch.object(hid_ownership, "start_service", side_effect=lambda: order.append("start")),
        ):
            with hid_ownership.ExclusiveHidOwnership(release_timeout=2.0):
                order.append("owned")
        self.assertEqual(order, ["stop", "wait:42:2.0", "owned", "start"])

    def test_inactive_service_is_neither_stopped_nor_started(self) -> None:
        with (
            patch.object(hid_ownership, "service_is_active", return_value=False),
            patch.object(hid_ownership, "stop_service") as stop,
            patch.object(hid_ownership, "start_service") as start,
        ):
            with hid_ownership.ExclusiveHidOwnership():
                pass
        stop.assert_not_called()
        start.assert_not_called()

    def test_no_handoff_does_not_query_unknown_service_state(self) -> None:
        with patch.object(hid_ownership, "service_is_active") as active:
            with hid_ownership.ExclusiveHidOwnership(service_handoff=False):
                pass
        active.assert_not_called()

    def test_unknown_service_state_fails_closed(self) -> None:
        with (
            patch.object(hid_ownership, "service_is_active", side_effect=RuntimeError("unknown")),
            patch.object(hid_ownership, "stop_service") as stop,
        ):
            with self.assertRaisesRegex(RuntimeError, "unknown"):
                with hid_ownership.ExclusiveHidOwnership():
                    pass
        stop.assert_not_called()

    def test_release_timeout_still_restores_service(self) -> None:
        with (
            patch.object(hid_ownership, "service_is_active", return_value=True),
            patch.object(hid_ownership, "service_main_pid", return_value=7),
            patch.object(hid_ownership, "stop_service"),
            patch.object(
                hid_ownership,
                "wait_for_hidraw_release",
                side_effect=TimeoutError("still owned"),
            ),
            patch.object(hid_ownership, "start_service") as start,
        ):
            with self.assertRaisesRegex(TimeoutError, "still owned"):
                with hid_ownership.ExclusiveHidOwnership():
                    pass
        start.assert_called_once_with()

    def test_body_error_and_signal_both_restore_service(self) -> None:
        for mode in ("error", "signal"):
            with self.subTest(mode=mode):
                with (
                    patch.object(hid_ownership, "service_is_active", return_value=True),
                    patch.object(hid_ownership, "service_main_pid", return_value=7),
                    patch.object(hid_ownership, "stop_service"),
                    patch.object(hid_ownership, "wait_for_hidraw_release"),
                    patch.object(hid_ownership, "start_service") as start,
                ):
                    expected = RuntimeError if mode == "error" else hid_ownership.OwnershipSignal
                    with self.assertRaises(expected):
                        with hid_ownership.ExclusiveHidOwnership() as ownership:
                            if mode == "error":
                                raise RuntimeError("failure")
                            ownership._interrupted(signal.SIGTERM, None)
                start.assert_called_once_with()

    def test_restore_failure_is_reported(self) -> None:
        with (
            patch.object(hid_ownership, "service_is_active", return_value=True),
            patch.object(hid_ownership, "service_main_pid", return_value=7),
            patch.object(hid_ownership, "stop_service"),
            patch.object(hid_ownership, "wait_for_hidraw_release"),
            patch.object(hid_ownership, "start_service", side_effect=OSError("failed")),
        ):
            with self.assertRaisesRegex(RuntimeError, "failed to restore"):
                with hid_ownership.ExclusiveHidOwnership():
                    pass


class LauncherTests(unittest.TestCase):
    def test_launcher_runs_inside_shared_guard(self) -> None:
        order: list[str] = []

        class Guard:
            def __enter__(self) -> None:
                order.append("enter")

            def __exit__(self, *_args: object) -> None:
                order.append("exit")

        with (
            patch.object(vial_launcher, "ExclusiveHidOwnership", Guard),
            patch.object(
                vial_launcher,
                "run_vial",
                side_effect=lambda args: order.append(f"vial:{args[0]}") or 0,
            ),
        ):
            self.assertEqual(vial_launcher.main(["--verbose"]), 0)
        self.assertEqual(order, ["enter", "vial:--verbose", "exit"])

    def test_signal_status_and_restore_failure_are_reported(self) -> None:
        class SignalGuard:
            def __enter__(self) -> None:
                raise hid_ownership.OwnershipSignal(signal.SIGTERM)

            def __exit__(self, *_args: object) -> None:
                pass

        with patch.object(vial_launcher, "ExclusiveHidOwnership", SignalGuard):
            self.assertEqual(vial_launcher.main([]), 128 + signal.SIGTERM)

        class FailedGuard:
            def __enter__(self) -> None:
                raise RuntimeError("failed to restore service")

            def __exit__(self, *_args: object) -> None:
                pass

        with (
            patch.object(vial_launcher, "ExclusiveHidOwnership", FailedGuard),
            contextlib.redirect_stderr(io.StringIO()) as errors,
        ):
            self.assertEqual(vial_launcher.main([]), 1)
        self.assertIn("failed to restore service", errors.getvalue())


class HandleTests(unittest.TestCase):
    def test_finds_only_hidraw_descriptors_for_service_pid(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fd = root / "77" / "fd"
            fd.mkdir(parents=True)
            (fd / "3").symlink_to("/dev/hidraw4")
            (fd / "4").symlink_to("/tmp/ordinary")
            self.assertEqual(hid_ownership.hidraw_handles(77, root), (Path("/dev/hidraw4"),))


if __name__ == "__main__":
    unittest.main()
