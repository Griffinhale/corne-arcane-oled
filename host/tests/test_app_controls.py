"""The app's controls: against a real service on a private bus, fake tools, and
a fake lock table. No test opens a keyboard or reaches the desktop's buses."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from arcane_host import app_controls
from arcane_host.app_controls import Controls, ControlsPanel, lock_holder
from arcane_host.dbus_contract import BUS_NAME, CONTROL_INTERFACE, OBJECT_PATH, PAUSE
from test_dbus_control import HOST_DIR, EchoKeyboard, Gio, GLib, wait_until


class StubView:
    """A service view with a fixed status and no bus, for the tool-running paths."""

    CALL_TIMEOUT_MS = 1000

    def __init__(self, status=("connected", "/dev/pts/9", False, "")) -> None:
        self.status = status


def script(directory: Path, name: str, body: str) -> list[str]:
    path = directory / name
    path.write_text(f"#!/bin/sh\n{body}\n")
    path.chmod(0o755)
    return [str(path)]


class Clock:
    def __init__(self) -> None:
        self.now = 100.0

    def __call__(self) -> float:
        return self.now


def run_until_done(controls: Controls, child: str, timeout: float = 10.0) -> None:
    assert wait_until(lambda: (controls.poll(), getattr(controls, child) is None)[1], timeout)


class LockHolderTests(unittest.TestCase):
    def setUp(self) -> None:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.lock = self.root / "corne-arcane" / "hid.lock"

    def test_a_real_holder_is_named_and_a_released_lock_is_free(self) -> None:
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
        self.assertEqual(holder.stdout.readline().strip(), "held")
        self.assertEqual(lock_holder(self.lock), f"Vial (corne-arcane-vial) (pid {holder.pid})")
        holder.stdin.close()
        holder.wait()
        holder.stdout.close()
        # The holder's name stays in the file; only the lock says it is gone.
        self.assertIn("Vial", self.lock.read_text())
        self.assertIsNone(lock_holder(self.lock))

    def test_lock_table_parsing(self) -> None:
        self.lock.parent.mkdir()
        self.lock.write_text("corne-arcane-diagnostics (pid 7)\n")
        stat = self.lock.stat()
        node = f"{os.major(stat.st_dev):02x}:{os.minor(stat.st_dev):02x}:{stat.st_ino}"
        table = self.root / "locks"
        table.write_text(f"1: POSIX  ADVISORY  WRITE 7 {node} 0 EOF\n")
        self.assertIsNone(lock_holder(self.lock, table), "a POSIX lock is not the guard's")
        table.write_text(f"1: FLOCK  ADVISORY  WRITE 7 {node} 0 EOF\n")
        self.assertEqual(lock_holder(self.lock, table), "corne-arcane-diagnostics (pid 7)")
        table.write_text("1: FLOCK  ADVISORY  WRITE 7 00:00:1 0 EOF\n")
        self.assertIsNone(lock_holder(self.lock, table))
        self.assertIsNone(lock_holder(self.root / "missing", table))


class ToolTests(unittest.TestCase):
    """Open Vial and Observe, with stand-in tools."""

    def setUp(self) -> None:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.clock = Clock()

    def controls(self, view=None, **kwargs) -> Controls:
        kwargs.setdefault("lock", self.root / "hid.lock")
        controls = Controls(view or StubView(), clock=self.clock, **kwargs)
        self.addCleanup(controls.close)
        return controls

    def test_vial_runs_and_its_failure_is_shown(self) -> None:
        release = self.root / "release"
        controls = self.controls(
            vial_command=script(
                self.root, "vial", f"while [ ! -e '{release}' ]; do sleep 0.05; done"
            )
        )
        controls.open_vial()
        controls.poll()
        self.assertEqual(controls.message, "Vial is open; the city resumes when it closes")
        self.assertFalse(controls.can_use_keyboard)
        release.touch()
        run_until_done(controls, "vial")
        self.assertEqual(controls.message, "Vial closed")
        self.assertTrue(controls.can_use_keyboard)

        controls.vial_command = script(
            self.root,
            "broken",
            "echo 'corne-arcane-vial: the keyboard is in use by x; close it first' >&2; exit 1",
        )
        controls.open_vial()
        run_until_done(controls, "vial")
        self.assertEqual(controls.message, "the keyboard is in use by x; close it first")

    def test_observation_counts_down_and_reports(self) -> None:
        passed = json.dumps({"checks": {"counters_monotonic": True}, "passed": True})
        failed = json.dumps(
            {"checks": {"counters_monotonic": True, "split_link_ok": False}, "passed": False}
        )
        args = self.root / "args"
        release = self.root / "release"
        controls = self.controls(
            diagnostics_command=script(
                self.root,
                "diagnostics",
                f"echo \"$@\" > '{args}'\n"
                f"while [ ! -e '{release}' ]; do sleep 0.05; done\n"
                f"echo '{passed}'",
            )
        )
        controls.observe(5)
        self.clock.now += 61
        controls.poll()
        self.assertEqual(controls.message, "Observing: 239 s left")
        self.assertFalse(controls.can_use_keyboard)
        release.touch()
        run_until_done(controls, "observation")
        self.assertEqual(args.read_text().split(), ["--observe", "300", "--json"])
        self.assertEqual(controls.message, "Observation passed")

        controls.diagnostics_command = script(self.root, "failing", f"echo '{failed}'; exit 2")
        controls.observe(1)
        run_until_done(controls, "observation")
        self.assertEqual(controls.message, "Observation failed: split_link_ok")

        controls.diagnostics_command = script(
            self.root,
            "silent",
            "echo 'corne-arcane-diagnostics: the device did not answer' >&2; exit 1",
        )
        controls.observe(1)
        run_until_done(controls, "observation")
        self.assertEqual(controls.message, "the device did not answer")

    def test_closing_the_app_stops_an_observation(self) -> None:
        controls = self.controls(diagnostics_command=script(self.root, "slow", "exec sleep 30"))
        controls.observe(1)
        process = controls.observation.process
        controls.close()
        self.assertIsNotNone(process.poll())

    def test_another_owner_disables_the_keyboard_buttons(self) -> None:
        view = StubView(("paused", "", True, "Vial (corne-arcane-vial) (pid 4)"))
        controls = self.controls(view)
        controls.poll()
        self.assertEqual(controls.other_owner, "Vial (corne-arcane-vial) (pid 4)")
        self.assertFalse(controls.can_pause or controls.can_use_keyboard)
        controls.open_vial()
        self.assertIsNone(controls.vial)

        # A guard that fell back to systemctl leaves no service, only its lock.
        view.status = None
        controls.lock.write_text("corne-arcane-diagnostics (pid 5)\n")
        locked = Path(self.root / "locks")
        stat = controls.lock.stat()
        node = f"{os.major(stat.st_dev):02x}:{os.minor(stat.st_dev):02x}:{stat.st_ino}"
        locked.write_text(f"1: FLOCK  ADVISORY  WRITE 5 {node} 0 EOF\n")
        original = app_controls.lock_holder
        app_controls.lock_holder = lambda path: original(path, locked)
        self.addCleanup(setattr, app_controls, "lock_holder", original)
        self.clock.now += 1
        controls.poll()
        self.assertEqual(controls.other_owner, "corne-arcane-diagnostics (pid 5)")
        self.assertFalse(controls.can_use_keyboard)


class FakeVar:
    def __init__(self, master=None, value="") -> None:
        self.value = value

    def get(self) -> str:
        return self.value

    def set(self, value: str) -> None:
        self.value = value


class FakeWidget:
    def __init__(self, *args, **kwargs) -> None:
        self.options = {"text": "", "state": "normal", **kwargs}
        self.layout = None

    def pack(self, **kwargs) -> None:
        self.layout = ("pack", kwargs)

    def place(self, **kwargs) -> None:
        self.layout = ("place", kwargs)

    def cget(self, name):
        return self.options.get(name)

    def configure(self, **kwargs) -> None:
        self.options.update(kwargs)


class FakeTk:
    Frame = Button = Spinbox = Label = FakeWidget
    StringVar = FakeVar


class PanelTests(unittest.TestCase):
    def test_buttons_follow_the_controls_and_errors_show_inline(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            view = StubView()
            controls = Controls(view, lock=Path(directory) / "hid.lock")
            panel = ControlsPanel(FakeTk, None, controls, background="#000", ink="#fff")
            panel.refresh()
            self.assertEqual(panel.pause_button.cget("state"), "normal")
            self.assertEqual(panel.vial_button.cget("state"), "normal")
            view.status = ("paused", "", True, "Vial (corne-arcane-vial) (pid 4)")
            controls._lock_checked = float("-inf")
            panel.refresh()
            self.assertEqual(
                [
                    b.cget("state")
                    for b in (panel.pause_button, panel.vial_button, panel.observe_button)
                ],
                ["disabled"] * 3,
            )
            self.assertEqual(
                panel.message.cget("text"), "In use by Vial (corne-arcane-vial) (pid 4)"
            )
            panel.minutes.value = "soon"
            panel._observe()
            panel.refresh()
            self.assertEqual(
                panel.message.cget("text"), "Observation length must be a number of minutes"
            )


@unittest.skipUnless(
    Gio is not None and shutil.which("dbus-daemon"), "needs PyGObject and dbus-daemon"
)
class ServicePauseTests(unittest.TestCase):
    """Pause and Resume from the app, against a real daemon on a private bus."""

    def setUp(self) -> None:
        from arcane_host.city_window import ServiceView

        bus = subprocess.Popen(
            ["dbus-daemon", "--session", "--nofork", "--print-address=1"],
            stdout=subprocess.PIPE,
            text=True,
        )
        self.addCleanup(bus.wait)
        self.addCleanup(bus.terminate)
        self.address = bus.stdout.readline().strip()
        bus.stdout.close()
        keyboard = EchoKeyboard()
        self.addCleanup(keyboard.close)
        runtime = tempfile.TemporaryDirectory()
        self.addCleanup(runtime.cleanup)
        env = dict(
            os.environ,
            DBUS_SESSION_BUS_ADDRESS=self.address,
            DBUS_SYSTEM_BUS_ADDRESS=self.address,
            XDG_RUNTIME_DIR=runtime.name,
            CORNE_ARCANE_SYSTEMCTL="false",
            PYTHONDONTWRITEBYTECODE="1",
        )
        daemon = subprocess.Popen(
            [
                sys.executable,
                "-m",
                "arcane_host.daemon",
                "--device",
                keyboard.path,
                "--no-desktop-notifications",
            ],
            cwd=HOST_DIR,
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        self.addCleanup(daemon.wait)
        self.addCleanup(daemon.terminate)
        self.view = ServiceView(Gio, GLib, self.connect())
        self.addCleanup(self.view.close)
        self.controls = Controls(self.view, lock=Path(runtime.name) / "hid.lock")
        self.addCleanup(self.controls.close)
        self.assertTrue(self.settle(lambda: self.status_link() == "connected"))

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

    def status_link(self) -> str | None:
        return None if self.view.status is None else self.view.status[0]

    def settle(self, condition, timeout: float = 10.0) -> bool:
        def pumped() -> bool:
            self.view.pump()
            self.controls.poll()
            return condition()

        return wait_until(pumped, timeout)

    def test_pause_and_resume(self) -> None:
        self.assertTrue(self.controls.can_pause)
        self.controls.pause()
        self.assertTrue(self.settle(lambda: self.status_link() == "paused"))
        self.assertTrue(self.controls.holding)
        self.assertEqual(self.view.status[3], self.controls.label)
        self.assertIsNone(self.controls.other_owner, "our own pause is not someone else's")
        self.assertFalse(self.controls.can_use_keyboard)
        self.assertTrue(self.controls.can_resume)
        self.controls.resume()
        self.assertTrue(self.settle(lambda: self.status_link() == "connected"))
        self.assertFalse(self.controls.holding)

    def test_a_refused_pause_is_shown_inline(self) -> None:
        other = self.connect()
        other.call_sync(
            BUS_NAME,
            OBJECT_PATH,
            CONTROL_INTERFACE,
            PAUSE,
            GLib.Variant("(s)", ("Vial (pid 2)",)),
            None,
            Gio.DBusCallFlags.NO_AUTO_START,
            1000,
            None,
        )
        # Before the signal arrives the app still thinks the keyboard is free.
        self.controls.pause()
        self.assertTrue(self.settle(lambda: not self.controls.pending, 3.0))
        self.assertEqual(
            self.controls.message, "Could not pause: the keyboard is lent to Vial (pid 2)"
        )
        self.assertFalse(self.controls.holding)
        self.assertTrue(self.settle(lambda: self.controls.other_owner == "Vial (pid 2)", 3.0))


if __name__ == "__main__":
    unittest.main()
