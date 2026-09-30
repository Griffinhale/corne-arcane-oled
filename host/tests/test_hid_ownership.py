from __future__ import annotations

import contextlib
import io
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from arcane_host import hid_ownership

HOST_DIR = Path(__file__).resolve().parents[1]


class LockTests(unittest.TestCase):
    """The guard's lock, in a private directory; no service, no /sys, no /proc."""

    def setUp(self) -> None:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.lock = Path(directory.name) / "corne-arcane" / "hid.lock"
        for name, value in (
            ("lock_path", lambda: self.lock),
            ("chosen_node", lambda _explicit=None: None),
            ("service_is_active", lambda: False),
        ):
            patcher = patch.object(hid_ownership, name, side_effect=value)
            patcher.start()
            self.addCleanup(patcher.stop)

    def test_second_owner_refused(self) -> None:
        with hid_ownership.ExclusiveHidOwnership(holder="Vial (corne-arcane-vial)"):
            with self.assertRaisesRegex(
                RuntimeError, rf"in use by Vial \(corne-arcane-vial\) \(pid {os.getpid()}\)"
            ):
                with hid_ownership.ExclusiveHidOwnership(holder="corne-arcane-diagnostics"):
                    self.fail("second owner entered")

    def test_lock_is_released_on_exit(self) -> None:
        with hid_ownership.ExclusiveHidOwnership(holder="first"):
            pass
        with hid_ownership.ExclusiveHidOwnership(holder="second"):
            self.assertIn("second", self.lock.read_text())

    def test_owner_in_another_process_is_named(self) -> None:
        holder = subprocess.Popen(
            [
                sys.executable,
                "-c",
                "import sys; from pathlib import Path; "
                "from arcane_host.hid_ownership import take_lock; "
                "take_lock(Path(sys.argv[1]), 'Vial (corne-arcane-vial)'); "
                "print('held', flush=True); sys.stdin.read()",
                str(self.lock),
            ],
            cwd=HOST_DIR,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
        )
        self.addCleanup(holder.wait)
        self.addCleanup(holder.stdin.close)
        self.assertEqual(holder.stdout.readline().strip(), "held")
        with self.assertRaisesRegex(
            RuntimeError, rf"Vial \(corne-arcane-vial\) \(pid {holder.pid}\)"
        ):
            with hid_ownership.ExclusiveHidOwnership(holder="corne-arcane-diagnostics"):
                self.fail("second owner entered")
        holder.stdout.close()


class RestoreMarkerTests(unittest.TestCase):
    """A killed guard's daemon debt, with a fake systemctl and a private runtime dir."""

    def setUp(self) -> None:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        root = Path(directory.name)
        self.runtime = root / "run"
        self.runtime.mkdir()
        self.state = root / "state"
        self.state.write_text("active\n")
        self.systemctl = root / "systemctl"
        self.systemctl.write_text(
            "#!/bin/sh\n"
            f"state='{self.state}'\n"
            'case "$2" in\n'
            '  is-active) read -r s < "$state"; [ "$s" = active ] && exit 0; exit 3 ;;\n'
            '  stop) echo inactive > "$state" ;;\n'
            '  start) echo active > "$state" ;;\n'
            "esac\n"
        )
        self.systemctl.chmod(0o755)
        lock = self.runtime / "corne-arcane" / "hid.lock"
        for name, value in (
            ("lock_path", lambda: lock),
            ("chosen_node", lambda _explicit=None: None),
        ):
            patcher = patch.object(hid_ownership, name, side_effect=value)
            patcher.start()
            self.addCleanup(patcher.stop)
        patcher = patch.object(hid_ownership, "SYSTEMCTL", str(self.systemctl))
        patcher.start()
        self.addCleanup(patcher.stop)
        self.marker = lock.with_name("restore-service")

    def kill_holder(self) -> None:
        """Run a real guard in a child, then SIGKILL it while it holds the keyboard."""
        child = subprocess.Popen(
            [
                sys.executable,
                "-c",
                "import sys; from arcane_host import hid_ownership as h; "
                "h.chosen_node = lambda _explicit=None: None; h.SYSTEMCTL = sys.argv[1]; "
                "g = h.ExclusiveHidOwnership(holder='Vial (corne-arcane-vial)'); g.__enter__(); "
                "print('held', flush=True); sys.stdin.read()",
                str(self.systemctl),
            ],
            cwd=HOST_DIR,
            env={**os.environ, "XDG_RUNTIME_DIR": str(self.runtime)},
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
        )
        self.assertEqual(child.stdout.readline().strip(), "held")
        child.kill()
        child.wait()
        child.stdin.close()
        child.stdout.close()

    def test_stale_marker_restores(self) -> None:
        self.kill_holder()
        self.assertEqual(self.state.read_text().strip(), "inactive")
        self.assertIn("Vial (corne-arcane-vial)", self.marker.read_text())
        with contextlib.redirect_stderr(io.StringIO()) as err:
            with hid_ownership.ExclusiveHidOwnership(holder="Vial (corne-arcane-vial)"):
                self.assertEqual(self.state.read_text().strip(), "inactive")
        self.assertIn("left stopped by Vial (corne-arcane-vial)", err.getvalue())
        self.assertEqual(self.state.read_text().strip(), "active")
        self.assertFalse(self.marker.exists())

    def test_stale_marker_restores_without_handoff(self) -> None:
        self.kill_holder()
        with contextlib.redirect_stderr(io.StringIO()):
            with hid_ownership.ExclusiveHidOwnership(service_handoff=False):
                pass
        self.assertEqual(self.state.read_text().strip(), "active")
        self.assertFalse(self.marker.exists())

    def test_live_holders_marker_is_left_alone(self) -> None:
        with hid_ownership.ExclusiveHidOwnership(holder="first"):
            self.assertTrue(self.marker.exists())
            with self.assertRaises(RuntimeError):
                with hid_ownership.ExclusiveHidOwnership(holder="second"):
                    self.fail("second owner entered")
            self.assertTrue(self.marker.exists())
            self.assertEqual(self.state.read_text().strip(), "inactive")
        self.assertFalse(self.marker.exists())
        self.assertEqual(self.state.read_text().strip(), "active")

    def test_failed_restart_keeps_the_marker(self) -> None:
        with patch.object(hid_ownership, "start_service", side_effect=OSError("no bus")):
            with self.assertRaisesRegex(RuntimeError, "failed to restore service"):
                with hid_ownership.ExclusiveHidOwnership(holder="first"):
                    pass
        self.assertTrue(self.marker.exists())
        with contextlib.redirect_stderr(io.StringIO()):
            with hid_ownership.ExclusiveHidOwnership(service_handoff=False):
                pass
        self.assertEqual(self.state.read_text().strip(), "active")

    def test_inactive_daemon_leaves_no_marker(self) -> None:
        self.state.write_text("inactive\n")
        with hid_ownership.ExclusiveHidOwnership(holder="first"):
            self.assertFalse(self.marker.exists())
        self.assertEqual(self.state.read_text().strip(), "inactive")


class OpenerScanTests(unittest.TestCase):
    """A fake /proc: symlinks named like fds, never a device node."""

    def fake_proc(self, root: Path, pid: int, name: str, targets: tuple[str, ...]) -> None:
        fd = root / str(pid) / "fd"
        fd.mkdir(parents=True)
        (root / str(pid) / "comm").write_text(f"{name}\n")
        for number, target in enumerate(targets, start=3):
            (fd / str(number)).symlink_to(target)

    def test_fd_scan_detects_opener(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fake_proc(root, 123, "vial", ("/dev/hidraw5",))
            self.fake_proc(root, 124, "firefox", ("/dev/hidraw2", "/tmp/cache"))
            self.fake_proc(root, os.getpid(), "python3", ("/dev/hidraw5",))
            (root / "self").mkdir()
            node = Path("/dev/hidraw5")
            self.assertEqual(hid_ownership.hidraw_openers(node, root), ("vial (pid 123)",))
            with self.assertRaisesRegex(TimeoutError, r"hidraw5 is still open by vial \(pid 123\)"):
                hid_ownership.wait_for_hidraw_release(node, 0.0, root)

    def test_free_node_returns_at_once(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fake_proc(root, 124, "firefox", ("/dev/hidraw2",))
            hid_ownership.wait_for_hidraw_release(Path("/dev/hidraw5"), 0.0, root)


if __name__ == "__main__":
    unittest.main()
