"""The Control interface, end to end: a real daemon on a private bus.

The keyboard is a pseudo-terminal in raw mode whose other end echoes every
report, which is all the heartbeat asks of Raw HID. No hidraw node is opened
and the desktop's own buses are never used.
"""

from __future__ import annotations

import os
import pty
import select
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import tty
import unittest
from pathlib import Path

from arcane_host.dbus_contract import (
    BUS_NAME,
    CONTROL_BUSY,
    CONTROL_INTERFACE,
    EVENTS_INTERFACE,
    INJECT_SYNTHETIC,
    OBJECT_PATH,
    PAUSE,
    RESUME,
    STATUS,
    STATUS_CHANGED,
    WORLD,
    WORLD_CHANGED,
)
from arcane_host.protocol import REPORT_SIZE, Category, Priority

try:
    import gi

    gi.require_version("Gio", "2.0")
    from gi.repository import Gio, GLib
except (ImportError, ValueError):  # pragma: no cover - environment dependent
    Gio = None
    GLib = None

HOST_DIR = Path(__file__).resolve().parents[1]
FRAME = REPORT_SIZE + 1  # hidraw writes carry a leading report ID


class EchoKeyboard:
    """A pty whose far side sends every frame straight back."""

    def __init__(self) -> None:
        self.master, self.slave = pty.openpty()
        tty.setraw(self.slave)
        self.path = os.ttyname(self.slave)
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._echo, daemon=True)
        self._thread.start()

    def _echo(self) -> None:
        pending = b""
        while not self._stop.is_set():
            readable, _, _ = select.select((self.master,), (), (), 0.05)
            if not readable:
                continue
            try:
                pending += os.read(self.master, 4096)
            except OSError:
                return
            while len(pending) >= FRAME:
                os.write(self.master, pending[:FRAME])
                pending = pending[FRAME:]

    def close(self) -> None:
        self._stop.set()
        self._thread.join()
        os.close(self.master)
        os.close(self.slave)


def holds(pid: int, path: str) -> bool:
    try:
        entries = os.listdir(f"/proc/{pid}/fd")
    except OSError:
        return False
    for entry in entries:
        try:
            if os.readlink(f"/proc/{pid}/fd/{entry}") == path:
                return True
        except OSError:
            continue
    return False


def wait_until(condition, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if condition():
            return True
        time.sleep(0.02)
    return condition()


@unittest.skipUnless(
    Gio is not None and shutil.which("dbus-daemon"), "needs PyGObject and dbus-daemon"
)
class ControlTests(unittest.TestCase):
    def setUp(self) -> None:
        bus = subprocess.Popen(
            ["dbus-daemon", "--session", "--nofork", "--print-address=1"],
            stdout=subprocess.PIPE,
            text=True,
        )
        self.addCleanup(bus.wait)
        self.addCleanup(bus.terminate)
        self.address = bus.stdout.readline().strip()
        bus.stdout.close()
        self.keyboard = EchoKeyboard()
        self.addCleanup(self.keyboard.close)
        runtime = tempfile.TemporaryDirectory()
        self.addCleanup(runtime.cleanup)
        self.env = dict(
            os.environ,
            DBUS_SESSION_BUS_ADDRESS=self.address,
            DBUS_SYSTEM_BUS_ADDRESS=self.address,
            XDG_RUNTIME_DIR=runtime.name,
            # Never the real service manager: if the guard ever fell back to
            # systemctl here, it would fail loudly instead.
            CORNE_ARCANE_SYSTEMCTL="false",
            PYTHONDONTWRITEBYTECODE="1",
        )
        self.daemon = subprocess.Popen(
            [
                sys.executable,
                "-m",
                "arcane_host.daemon",
                "--device",
                self.keyboard.path,
                "--no-desktop-notifications",
            ],
            cwd=HOST_DIR,
            env=self.env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
        )
        self.addCleanup(self._stop_daemon)
        self.client = self.connect()
        self.assertTrue(
            wait_until(lambda: self.status_or_none() is not None, 10.0), "daemon never answered"
        )
        self.assertTrue(
            wait_until(lambda: self.status()[0] == "connected", 5.0),
            f"daemon never connected: {self.status()}",
        )

    def _stop_daemon(self) -> None:
        self.daemon.terminate()
        try:
            self.daemon.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.daemon.kill()
            self.daemon.wait()
        self.daemon.stderr.close()

    def connect(self):
        connection = Gio.DBusConnection.new_for_address_sync(
            self.address,
            Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT
            | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,
            None,
            None,
        )
        self.addCleanup(lambda: connection.is_closed() or connection.close_sync(None))
        return connection

    def call(self, method: str, arguments=None, reply: str | None = None, connection=None):
        result = (connection or self.client).call_sync(
            BUS_NAME,
            OBJECT_PATH,
            CONTROL_INTERFACE,
            method,
            arguments,
            GLib.VariantType(reply) if reply else None,
            Gio.DBusCallFlags.NO_AUTO_START,
            1000,
            None,
        )
        return result.unpack() if reply else None

    def status(self) -> tuple[str, str, bool, str]:
        return self.call(STATUS, reply="(ssbs)")

    def status_or_none(self):
        try:
            return self.status()
        except GLib.Error:
            return None

    def test_status_fields(self) -> None:
        self.assertEqual(self.status(), ("connected", self.keyboard.path, False, ""))
        self.assertTrue(holds(self.daemon.pid, self.keyboard.path))

    def test_pause_releases_fd(self) -> None:
        signals: list[tuple] = []
        self.client.signal_subscribe(
            BUS_NAME,
            CONTROL_INTERFACE,
            STATUS_CHANGED,
            OBJECT_PATH,
            None,
            Gio.DBusSignalFlags.NONE,
            lambda *args: signals.append(args[-1].unpack()),
        )
        started = time.monotonic()
        self.call(PAUSE, GLib.Variant("(s)", ("test tool\n(pid 1)",)))
        self.assertTrue(wait_until(lambda: not holds(self.daemon.pid, self.keyboard.path), 1.0))
        self.assertLess(time.monotonic() - started, 1.0)
        # A control character in the label is made printable, never stored raw.
        self.assertEqual(self.status(), ("paused", "", True, "test tool?(pid 1)"))

        other = self.connect()
        with self.assertRaises(GLib.Error) as busy:
            self.call(PAUSE, GLib.Variant("(s)", ("second",)), connection=other)
        self.assertEqual(Gio.DBusError.get_remote_error(busy.exception), CONTROL_BUSY)

        self.call(RESUME)
        self.assertTrue(wait_until(lambda: holds(self.daemon.pid, self.keyboard.path), 5.0))
        self.assertEqual(self.status(), ("connected", self.keyboard.path, False, ""))

        context = GLib.MainContext.default()

        def reconnect_signalled() -> bool:
            while context.iteration(False):
                pass
            return bool(signals) and signals[-1][0] == "connected"

        wait_until(reconnect_signalled, 2.0)
        self.assertIn(("paused", "", True, "test tool?(pid 1)"), signals)
        self.assertEqual(signals[-1], ("connected", self.keyboard.path, False, ""))

    def test_world_follows_the_resolved_state(self) -> None:
        worlds: list[tuple] = []
        self.client.signal_subscribe(
            BUS_NAME,
            CONTROL_INTERFACE,
            WORLD_CHANGED,
            OBJECT_PATH,
            None,
            Gio.DBusSignalFlags.NONE,
            lambda *args: worlds.append(args[-1].unpack()),
        )
        resting = self.call(WORLD, reply="(yyyyyyyy)")
        self.assertEqual(len(resting), 8)
        self.assertEqual(resting[1], 0, "no notification is waiting yet")
        self.client.call_sync(
            BUS_NAME,
            OBJECT_PATH,
            EVENTS_INTERFACE,
            INJECT_SYNTHETIC,
            GLib.Variant("(yyb)", (int(Category.COMMUNICATION), int(Priority.NORMAL), False)),
            None,
            Gio.DBusCallFlags.NO_AUTO_START,
            1000,
            None,
        )
        context = GLib.MainContext.default()

        def signalled() -> bool:
            while context.iteration(False):
                pass
            return bool(worlds)

        self.assertTrue(wait_until(signalled, 2.0), "no WorldChanged")
        world = self.call(WORLD, reply="(yyyyyyyy)")
        self.assertEqual(worlds[-1], world)
        self.assertEqual(world[1:3], (1, int(Category.COMMUNICATION)))

    def test_pause_ends_with_the_pausing_connection(self) -> None:
        pauser = self.connect()
        self.call(PAUSE, GLib.Variant("(s)", ("killed tool",)), connection=pauser)
        self.assertTrue(self.status()[2])
        pauser.close_sync(None)
        self.assertTrue(
            wait_until(lambda: self.status()[0] == "connected", 5.0),
            f"pause outlived its client: {self.status()}",
        )

    def guard_child(self) -> subprocess.Popen:
        child = subprocess.Popen(
            [
                sys.executable,
                "-c",
                "import sys; from arcane_host import hid_ownership as h; "
                "h.chosen_node = lambda _explicit=None: None; "
                "g = h.ExclusiveHidOwnership(holder='probe'); g.__enter__(); "
                "print('held', flush=True); sys.stdin.readline(); g.__exit__(None, None, None); "
                "print('released', flush=True)",
            ],
            cwd=HOST_DIR,
            env=self.env,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
        )
        self.addCleanup(child.wait)
        self.assertEqual(child.stdout.readline().strip(), "held")
        return child

    def test_guard_pauses_instead_of_stopping(self) -> None:
        child = self.guard_child()
        self.assertEqual(self.status(), ("paused", "", True, f"probe (pid {child.pid})"))
        self.assertFalse(holds(self.daemon.pid, self.keyboard.path))
        self.assertIsNone(self.daemon.poll(), "the unit's process must keep running")
        child.stdin.write("\n")
        child.stdin.close()
        self.assertEqual(child.stdout.readline().strip(), "released")
        child.stdout.close()
        self.assertTrue(wait_until(lambda: self.status()[0] == "connected", 5.0))

    def test_killed_guard_gives_the_keyboard_back(self) -> None:
        child = self.guard_child()
        self.assertTrue(self.status()[2])
        child.kill()
        child.wait()
        child.stdin.close()
        child.stdout.close()
        self.assertTrue(
            wait_until(lambda: self.status()[0] == "connected", 5.0),
            f"pause outlived a killed guard: {self.status()}",
        )


if __name__ == "__main__":
    unittest.main()
