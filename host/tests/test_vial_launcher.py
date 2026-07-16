from __future__ import annotations

from pathlib import Path
import signal
import tempfile
import unittest
from unittest.mock import patch

from arcane_host import vial_launcher


class HandoffTests(unittest.TestCase):
    def test_active_service_stops_waits_runs_and_restores(self) -> None:
        order: list[str] = []
        with (
            patch.object(vial_launcher, "service_is_active", return_value=True),
            patch.object(vial_launcher, "service_main_pid", return_value=42),
            patch.object(vial_launcher, "stop_service", side_effect=lambda: order.append("stop")),
            patch.object(vial_launcher, "wait_for_hidraw_release",
                         side_effect=lambda pid: order.append(f"wait:{pid}")),
            patch.object(vial_launcher, "run_vial",
                         side_effect=lambda args: order.append(f"vial:{args[0]}") or 0),
            patch.object(vial_launcher, "start_service", side_effect=lambda: order.append("start")),
        ):
            self.assertEqual(vial_launcher.main(["--verbose"]), 0)
        self.assertEqual(order, ["stop", "wait:42", "vial:--verbose", "start"])

    def test_disabled_service_is_neither_stopped_nor_started(self) -> None:
        with (
            patch.object(vial_launcher, "service_is_active", return_value=False),
            patch.object(vial_launcher, "stop_service") as stop,
            patch.object(vial_launcher, "start_service") as start,
            patch.object(vial_launcher, "run_vial", return_value=0),
        ):
            self.assertEqual(vial_launcher.main([]), 0)
        stop.assert_not_called()
        start.assert_not_called()

    def test_vial_crash_status_still_restores_service(self) -> None:
        with (
            patch.object(vial_launcher, "service_is_active", return_value=True),
            patch.object(vial_launcher, "service_main_pid", return_value=7),
            patch.object(vial_launcher, "stop_service"),
            patch.object(vial_launcher, "wait_for_hidraw_release"),
            patch.object(vial_launcher, "run_vial", return_value=23),
            patch.object(vial_launcher, "start_service") as start,
        ):
            self.assertEqual(vial_launcher.main([]), 23)
        start.assert_called_once_with()

    def test_signal_exit_still_restores_service(self) -> None:
        with (
            patch.object(vial_launcher, "service_is_active", return_value=True),
            patch.object(vial_launcher, "service_main_pid", return_value=7),
            patch.object(vial_launcher, "stop_service"),
            patch.object(vial_launcher, "wait_for_hidraw_release"),
            patch.object(vial_launcher, "run_vial",
                         side_effect=vial_launcher.LauncherSignal(signal.SIGTERM)),
            patch.object(vial_launcher, "start_service") as start,
        ):
            self.assertEqual(vial_launcher.main([]), 128 + signal.SIGTERM)
        start.assert_called_once_with()

    def test_restore_failure_is_reported_as_launcher_failure(self) -> None:
        with (
            patch.object(vial_launcher, "service_is_active", return_value=True),
            patch.object(vial_launcher, "service_main_pid", return_value=7),
            patch.object(vial_launcher, "stop_service"),
            patch.object(vial_launcher, "wait_for_hidraw_release"),
            patch.object(vial_launcher, "run_vial", return_value=0),
            patch.object(vial_launcher, "start_service", side_effect=OSError("failed")),
        ):
            self.assertEqual(vial_launcher.main([]), 1)


class HandleTests(unittest.TestCase):
    def test_finds_only_hidraw_descriptors_for_service_pid(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fd = root / "77" / "fd"
            fd.mkdir(parents=True)
            (fd / "3").symlink_to("/dev/hidraw4")
            (fd / "4").symlink_to("/tmp/ordinary")
            self.assertEqual(vial_launcher.hidraw_handles(77, root), (Path("/dev/hidraw4"),))


if __name__ == "__main__":
    unittest.main()
