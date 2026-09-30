from __future__ import annotations

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
