from __future__ import annotations

import contextlib
import io
import signal
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from arcane_host import hid_ownership, vial_launcher


class OwnershipGuardTests(unittest.TestCase):
    def setUp(self) -> None:
        # Never the real runtime dir, /sys or /proc: a private lock and a fixed node.
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        lock = Path(directory.name) / "hid.lock"
        for name, value in (
            ("lock_path", lambda: lock),
            ("chosen_node", lambda _explicit=None: Path("/dev/hidraw-test")),
            ("wait_for_hidraw_release", lambda _node, _timeout: None),
        ):
            patcher = patch.object(hid_ownership, name, side_effect=value)
            patcher.start()
            self.addCleanup(patcher.stop)

    def test_active_service_stops_waits_runs_and_restores(self) -> None:
        order: list[str] = []
        with (
            patch.object(hid_ownership, "service_is_active", return_value=True),
            patch.object(hid_ownership, "stop_service", side_effect=lambda: order.append("stop")),
            patch.object(
                hid_ownership,
                "wait_for_hidraw_release",
                side_effect=lambda node, timeout: order.append(f"wait:{node}:{timeout}"),
            ),
            patch.object(hid_ownership, "start_service", side_effect=lambda: order.append("start")),
        ):
            with hid_ownership.ExclusiveHidOwnership(release_timeout=2.0):
                order.append("owned")
        self.assertEqual(order, ["stop", "wait:/dev/hidraw-test:2.0", "owned", "start"])

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
            patch.object(hid_ownership, "stop_service"),
            patch.object(hid_ownership, "wait_for_hidraw_release"),
            patch.object(hid_ownership, "start_service", side_effect=OSError("failed")),
        ):
            with self.assertRaisesRegex(RuntimeError, "failed to restore"):
                with hid_ownership.ExclusiveHidOwnership():
                    pass


class ServiceStateTests(unittest.TestCase):
    @staticmethod
    def _is_active(returncode: int) -> subprocess.CompletedProcess[str]:
        return subprocess.CompletedProcess((), returncode, stdout="", stderr="")

    def test_missing_unit_is_inactive(self) -> None:
        """The daemon is optional; without its unit, is-active exits 4, not 3."""
        with patch.object(hid_ownership, "_systemctl", return_value=self._is_active(4)):
            self.assertFalse(hid_ownership.service_is_active())

    def test_active_and_inactive_codes(self) -> None:
        for returncode, active in ((0, True), (3, False)):
            with patch.object(
                hid_ownership, "_systemctl", return_value=self._is_active(returncode)
            ):
                self.assertIs(hid_ownership.service_is_active(), active)

    def test_other_codes_fail_closed(self) -> None:
        with patch.object(hid_ownership, "_systemctl", return_value=self._is_active(1)):
            with self.assertRaisesRegex(RuntimeError, "systemctl exited 1"):
                hid_ownership.service_is_active()


class LauncherTests(unittest.TestCase):
    def setUp(self) -> None:
        # Tests never depend on a real Vial, and never notify the real desktop.
        for name, value in (("resolve_vial", ["/opt/vial"]), ("notify_failure", None)):
            patcher = patch.object(vial_launcher, name, return_value=value)
            patcher.start()
            self.addCleanup(patcher.stop)

    def test_launcher_runs_inside_shared_guard(self) -> None:
        order: list[str] = []

        class Guard:
            def __init__(self, **_kwargs: object) -> None:
                pass

            def __enter__(self) -> None:
                order.append("enter")

            def __exit__(self, *_args: object) -> None:
                order.append("exit")

        with (
            patch.object(vial_launcher, "ExclusiveHidOwnership", Guard),
            patch.object(
                vial_launcher,
                "run_vial",
                side_effect=lambda command, args: order.append(f"{command[0]}:{args[0]}") or 0,
            ),
        ):
            self.assertEqual(vial_launcher.main(["--verbose"]), 0)
        self.assertEqual(order, ["enter", "/opt/vial:--verbose", "exit"])

    def test_signal_status_and_restore_failure_are_reported(self) -> None:
        class SignalGuard:
            def __init__(self, **_kwargs: object) -> None:
                pass

            def __enter__(self) -> None:
                raise hid_ownership.OwnershipSignal(signal.SIGTERM)

            def __exit__(self, *_args: object) -> None:
                pass

        with patch.object(vial_launcher, "ExclusiveHidOwnership", SignalGuard):
            self.assertEqual(vial_launcher.main([]), 128 + signal.SIGTERM)

        class FailedGuard:
            def __init__(self, **_kwargs: object) -> None:
                pass

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

    def test_vial_vanishing_after_lookup_still_hands_back(self) -> None:
        """Vial was found, then Popen could not start it: the daemon is already stopped."""
        order: list[str] = []

        class Guard:
            def __init__(self, **_kwargs: object) -> None:
                pass

            def __enter__(self) -> None:
                order.append("enter")

            def __exit__(self, *_args: object) -> None:
                order.append("exit")

        stderr = io.StringIO()
        with (
            patch.object(vial_launcher, "resolve_vial", return_value=["/opt/vial"]),
            patch.object(vial_launcher, "ExclusiveHidOwnership", Guard),
            patch.object(vial_launcher.subprocess, "Popen", side_effect=FileNotFoundError()),
            patch.object(vial_launcher, "notify_failure"),
            contextlib.redirect_stderr(stderr),
        ):
            self.assertEqual(vial_launcher.main([]), 1)

        self.assertEqual(order, ["enter", "exit"])
        self.assertIn("could not start", stderr.getvalue())


class VialResolutionTests(unittest.TestCase):
    def setUp(self) -> None:
        self._config = tempfile.TemporaryDirectory()
        self.addCleanup(self._config.cleanup)
        self.config_home = Path(self._config.name)
        env = {"XDG_CONFIG_HOME": str(self.config_home)}
        patcher = patch.dict(vial_launcher.os.environ, env, clear=True)
        patcher.start()
        self.addCleanup(patcher.stop)

    def _write_config(self, text: str) -> None:
        path = self.config_home / "corne-arcane" / "vial"
        path.parent.mkdir(parents=True)
        path.write_text(text)

    def test_missing_vial_no_stop(self) -> None:
        """No Vial anywhere: fail before touching the daemon, and say so on the desktop."""
        stderr = io.StringIO()
        with (
            patch.object(vial_launcher.shutil, "which", return_value=None),
            patch.object(vial_launcher, "ExclusiveHidOwnership") as guard,
            patch.object(hid_ownership, "_systemctl") as systemctl,
            patch.object(vial_launcher, "notify_failure") as notify,
            contextlib.redirect_stderr(stderr),
        ):
            self.assertEqual(vial_launcher.main([]), 1)
        guard.assert_not_called()
        systemctl.assert_not_called()
        notify.assert_called_once()
        message = stderr.getvalue()
        self.assertIn("not found", message)
        self.assertIn("CORNE_ARCANE_VIAL_BIN", message)
        self.assertIn("corne-arcane/vial", message)

    def test_bin_with_args(self) -> None:
        """A Flatpak install is a command with arguments, not a single path."""
        vial_launcher.os.environ["CORNE_ARCANE_VIAL_BIN"] = "flatpak run xyz.Vial"
        with patch.object(vial_launcher.shutil, "which", return_value="/usr/bin/flatpak"):
            self.assertEqual(vial_launcher.resolve_vial(), ["/usr/bin/flatpak", "run", "xyz.Vial"])

    def test_path_with_spaces_is_one_word(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            appimage = Path(directory) / "My Apps" / "Vial.AppImage"
            appimage.parent.mkdir()
            appimage.write_text("")
            appimage.chmod(0o755)
            vial_launcher.os.environ["CORNE_ARCANE_VIAL_BIN"] = str(appimage)
            self.assertEqual(vial_launcher.resolve_vial(), [str(appimage)])

    def test_config_file_is_read_when_the_environment_is_silent(self) -> None:
        """Menu launches do not see shell exports, so the config file has to work alone."""
        self._write_config("# where Vial lives\n\n~/Apps/Vial.AppImage --no-sandbox\n")
        home = str(Path.home())
        with patch.object(vial_launcher.shutil, "which", side_effect=lambda word: word):
            self.assertEqual(
                vial_launcher.resolve_vial(), [f"{home}/Apps/Vial.AppImage", "--no-sandbox"]
            )

    def test_environment_wins_over_config(self) -> None:
        self._write_config("/from/config\n")
        vial_launcher.os.environ["CORNE_ARCANE_VIAL_BIN"] = "/from/env"
        with patch.object(vial_launcher.shutil, "which", side_effect=lambda word: word):
            self.assertEqual(vial_launcher.resolve_vial(), ["/from/env"])

    def test_default_is_vial_on_path(self) -> None:
        with patch.object(vial_launcher.shutil, "which", return_value="/usr/bin/vial") as which:
            self.assertEqual(vial_launcher.resolve_vial(), ["/usr/bin/vial"])
        which.assert_called_once_with("vial")

    def test_notification_only_without_a_terminal(self) -> None:
        with (
            patch.object(vial_launcher.sys.stderr, "isatty", return_value=True),
            patch.object(vial_launcher.subprocess, "run") as run,
        ):
            vial_launcher.notify_failure("x")
        run.assert_not_called()
        with (
            patch.object(vial_launcher.sys.stderr, "isatty", return_value=False),
            patch.object(vial_launcher.subprocess, "run", side_effect=FileNotFoundError()) as run,
        ):
            vial_launcher.notify_failure("x")
        self.assertEqual(run.call_args.args[0][0], "notify-send")


if __name__ == "__main__":
    unittest.main()
