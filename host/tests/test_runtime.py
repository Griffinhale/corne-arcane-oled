from __future__ import annotations

import errno
import io
import os
import time
import unittest
from contextlib import redirect_stderr
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from arcane_host.adapters import SemanticAdapters
from arcane_host.dbus_adapters import DBusAdapterHub
from arcane_host.focus import FocusArbiter
from arcane_host.heartbeat import HidHeartbeat
from arcane_host.hid_ownership import node_openers
from arcane_host.policy import NotificationPolicy
from arcane_host.protocol import Scene
from arcane_host.runtime import DaemonRuntime
from arcane_host.semantic import SemanticResolver
from test_hid_ownership import foreign_opener, release


class FakeGLib:
    next_id = 1
    added = []
    removed = []

    @classmethod
    def reset(cls):
        cls.next_id = 1
        cls.added = []
        cls.removed = []

    @classmethod
    def _add(cls, kind, delay, callback):
        source_id = cls.next_id
        cls.next_id += 1
        cls.added.append((kind, delay, callback, source_id))
        return source_id

    @classmethod
    def idle_add(cls, callback):
        return cls._add("idle", 0, callback)

    @classmethod
    def timeout_add(cls, delay, callback):
        return cls._add("timeout", delay, callback)

    @classmethod
    def source_remove(cls, source_id):
        cls.removed.append(source_id)


class FakeGio:
    unowned = []

    @classmethod
    def bus_unown_name(cls, owner_id):
        cls.unowned.append(owner_id)


class FakeLoop:
    def __init__(self):
        self.quit_calls = 0

    def quit(self):
        self.quit_calls += 1


class FakeHeartbeat:
    def __init__(self, sent=False):
        self.sent = sent
        self.notify_requests = 0
        self.closed = False
        self.device = None
        self.link = None

    def tick(self, _now):
        return self.sent

    def request_notify(self):
        self.notify_requests += 1

    def next_deadline(self, now):
        return now + 0.5

    def close(self):
        self.closed = True


class RuntimeTests(unittest.TestCase):
    def setUp(self):
        FakeGLib.reset()
        FakeGio.unowned = []

    def runtime(self, *, sent=False, once=False):
        now = [10.0]
        heartbeat = FakeHeartbeat(sent)
        resolver = SemanticResolver()
        policy = NotificationPolicy()
        arbiter = FocusArbiter(settle_seconds=0)
        loop = FakeLoop()
        runtime = DaemonRuntime(
            FakeGio,
            FakeGLib,
            loop,
            heartbeat,
            resolver,
            policy,
            arbiter,
            once=once,
            clock=lambda: now[0],
        )
        adapters = SemanticAdapters(resolver, policy, runtime.wake, lambda: now[0])
        runtime.bind_adapters(adapters)
        return runtime, heartbeat, resolver, arbiter, loop

    def test_semantic_revision_requests_notify_and_schedules_deadline(self):
        runtime, heartbeat, resolver, arbiter, _loop = self.runtime()
        arbiter.report("firefox", "", 10.0)
        runtime.tick()
        self.assertEqual(resolver.state.scene.name, "ARCHIVE")
        self.assertEqual(heartbeat.notify_requests, 1)
        self.assertEqual(FakeGLib.added[-1][0], "timeout")

    def test_wake_coalesces_inside_tick(self):
        runtime, _heartbeat, _resolver, _arbiter, _loop = self.runtime()
        runtime.in_tick = True
        runtime.wake()
        runtime.wake()
        self.assertTrue(runtime.wake_pending)
        self.assertEqual(FakeGLib.added, [])

    def test_once_quits_without_rescheduling(self):
        runtime, _heartbeat, _resolver, _arbiter, loop = self.runtime(sent=True, once=True)
        runtime.tick()
        self.assertEqual(loop.quit_calls, 1)
        self.assertEqual(FakeGLib.added, [])

    def test_close_removes_sources_unsubscribes_and_unowns(self):
        runtime, heartbeat, _resolver, _arbiter, _loop = self.runtime()
        closed = []

        class Resource:
            def close(self):
                closed.append(True)

        runtime.own(Resource())
        runtime.set_bus_owner(7)
        runtime.wake()
        source_id = runtime.source_id
        runtime.close()
        runtime.close()
        self.assertEqual(FakeGLib.removed, [source_id])
        self.assertEqual(closed, [True])
        self.assertEqual(FakeGio.unowned, [7])
        self.assertTrue(heartbeat.closed)


class EchoDevice:
    """Echoes each report, or fails every send once unplugged."""

    path = "/dev/hidraw-fake"

    def __init__(self) -> None:
        self.unplugged = False
        self.last = b""

    def send(self, report: bytes) -> None:
        if self.unplugged:
            raise OSError(errno.ENODEV, "No such device")
        self.last = report

    def receive(self, timeout: float) -> bytes:
        del timeout
        return self.last

    def close(self) -> None:
        pass


class LinkStateTests(unittest.TestCase):
    """What the journal says about the keyboard at default verbosity."""

    def setUp(self) -> None:
        self.outcomes: list = []
        self.now = 0.0

        def factory():
            outcome = self.outcomes.pop(0)
            if isinstance(outcome, BaseException):
                raise outcome
            return outcome

        self.heartbeat = HidHeartbeat(lambda: Scene.DUEL, factory, lambda: 7, retry_interval=0.0)
        self.stderr = io.StringIO()

    def tick(self, count: int = 1) -> None:
        with redirect_stderr(self.stderr):
            for _ in range(count):
                self.now += 1.0
                self.heartbeat.tick(self.now)

    def lines(self) -> list[str]:
        return self.stderr.getvalue().splitlines()

    def test_link_state_logged_once(self) -> None:
        missing = RuntimeError("no Corne Raw HID interface found (USB 4653:0001)")
        device = EchoDevice()
        self.outcomes = [missing, missing, device, missing, missing, EchoDevice()]
        self.tick(3)  # absent twice, then plugged in
        self.tick(2)  # heartbeats
        device.unplugged = True
        self.tick(3)  # the send fails, then absent twice
        self.tick(1)  # plugged back in
        lines = self.lines()
        self.assertEqual(len(lines), 4, lines)
        self.assertIn("no keyboard found", lines[0])
        self.assertIn("keyboard connected (/dev/hidraw-fake)", lines[1])
        self.assertIn("keyboard disconnected", lines[2])
        self.assertIn("keyboard connected", lines[3])
        self.assertEqual(self.outcomes, [])

    def test_eacces_hint(self) -> None:
        denied = PermissionError(errno.EACCES, "Permission denied", "/dev/hidraw3")
        self.outcomes = [denied, denied, denied]
        self.tick(3)
        lines = self.lines()
        self.assertEqual(len(lines), 1, lines)
        self.assertIn("60-corne-arcane.rules", lines[0])
        self.assertIn("replug", lines[0])

    def test_several_devices_named(self) -> None:
        several = RuntimeError("multiple QMK Raw HID interfaces found: /dev/a, /dev/b")
        self.outcomes = [several, several]
        self.tick(2)
        self.assertEqual(self.lines(), [f"arcane-host: {several}"])


class AdapterFailureTests(unittest.TestCase):
    def test_failure_reported_once_per_site_without_its_text(self) -> None:
        class Refusing:
            def signal_subscribe(self, *_args):
                raise ValueError("Track title that must not be logged")

        class Gio:
            class DBusSignalFlags:
                NONE = 0

        adapters = SemanticAdapters(SemanticResolver(), NotificationPolicy(), lambda: None)
        stderr = io.StringIO()
        with redirect_stderr(stderr):
            hub = DBusAdapterHub(Gio, Refusing(), None, adapters)
            hub._failed("property signals", ValueError("Track title that must not be logged"))
        lines = stderr.getvalue().splitlines()
        self.assertEqual(
            lines,
            [
                "arcane-host: D-Bus adapter property signals failed (ValueError)",
                "arcane-host: D-Bus adapter name-owner signals failed (ValueError)",
            ],
        )
        self.assertEqual(adapters.counters.errors, 3)


class PtyHeartbeat(FakeHeartbeat):
    """Connects to a pty path on each tick while disconnected; opens nothing."""

    def __init__(self, path: str):
        super().__init__()
        self.path = path
        self.connects = 0
        self.next_connect = 0.0

    def tick(self, _now):
        if self.device is None:
            self.device = SimpleNamespace(path=self.path)
            self.connects += 1
            self.link = "connected"
        return False

    def close(self):
        self.closed = True
        self.device = None


class LendTests(unittest.TestCase):
    """A real child opens a pty that stands in for the keyboard node.

    Real inotify and a real /proc scan, so this is the daemon's check end to
    end short of D-Bus; the node is a pty, never /dev/hidraw.
    """

    def setUp(self):
        FakeGLib.reset()
        master, slave = os.openpty()
        self.addCleanup(os.close, master)
        self.addCleanup(os.close, slave)
        self.path = os.ttyname(slave)
        self.stderr = io.StringIO()
        quiet = redirect_stderr(self.stderr)
        quiet.__enter__()
        self.addCleanup(quiet.__exit__, None, None, None)

    def runtime(self, lend_check=lambda _node: True, clock=time.monotonic):
        heartbeat = PtyHeartbeat(self.path)
        resolver = SemanticResolver()
        policy = NotificationPolicy()
        runtime = DaemonRuntime(
            FakeGio,
            FakeGLib,
            FakeLoop(),
            heartbeat,
            resolver,
            policy,
            FocusArbiter(settle_seconds=0),
            clock=clock,
            lend_check=lend_check,
        )
        runtime.bind_adapters(SemanticAdapters(resolver, policy, runtime.wake))
        self.addCleanup(runtime.close)
        return runtime, heartbeat

    def test_lends_on_a_foreign_open_and_takes_it_back(self):
        runtime, heartbeat = self.runtime()
        runtime.tick()
        runtime.tick()
        self.assertEqual(runtime.status(), ("connected", self.path, False, ""))
        child = foreign_opener(self.path)
        try:
            started = time.monotonic()
            runtime.tick()
            self.assertLess(time.monotonic() - started, 1.0)
            comm = Path(f"/proc/{child.pid}/comm").read_text().strip()
            self.assertEqual(runtime.status(), ("paused", "", True, f"{comm} (pid {child.pid})"))
            self.assertIsNone(heartbeat.device, "the keyboard was released")
            runtime.tick()
            self.assertTrue(runtime.paused, "held while the opener keeps it open")
            # Paused, the loop wakes once a second to check that one process.
            self.assertEqual(FakeGLib.added[-1][:2], ("timeout", 1000))
        finally:
            release(child)
        runtime.tick()
        self.assertFalse(runtime.paused)
        runtime.tick()
        self.assertEqual(heartbeat.connects, 2)
        self.assertEqual(runtime.status(), ("connected", self.path, False, ""))
        self.assertIn("keyboard returned", self.stderr.getvalue())

    def test_opener_present_before_connect_is_found(self):
        child = foreign_opener(self.path)
        try:
            runtime, _heartbeat = self.runtime()
            runtime.tick()
            self.assertEqual(runtime.lent_to, (child.pid,))
        finally:
            release(child)

    def test_unchecked_node_is_never_lent(self):
        runtime, _heartbeat = self.runtime(lend_check=lambda _node: False)
        runtime.tick()
        child = foreign_opener(self.path)
        try:
            runtime.tick()
            self.assertFalse(runtime.paused)
            self.assertIsNone(runtime.open_watch)
        finally:
            release(child)

    def test_unwatchable_node_is_tried_once(self):
        self.path = "/nonexistent/hidraw9"
        runtime, _heartbeat = self.runtime()
        runtime.tick()
        runtime.tick()
        self.assertIsNone(runtime.open_watch)
        self.assertEqual(runtime._unwatchable, Path(self.path))
        self.assertFalse(runtime.paused)

    def test_control_pause_is_kept_and_the_opener_found_after_it(self):
        runtime, _heartbeat = self.runtime()
        runtime.tick()
        runtime.pause("Vial guard (pid 1)")
        child = foreign_opener(self.path)
        try:
            runtime.tick()
            self.assertEqual(runtime.status()[3], "Vial guard (pid 1)")
            self.assertEqual(runtime.lent_to, ())
            runtime.resume()
            runtime.tick()
            self.assertEqual(runtime.lent_to, (child.pid,))
        finally:
            release(child)

    def counted_scans(self):
        scans = []

        def counting(node):
            scans.append(node)
            return node_openers(node)

        patcher = patch("arcane_host.runtime.node_openers", counting)
        patcher.start()
        self.addCleanup(patcher.stop)
        return scans

    def open_and_close(self):
        os.close(os.open(self.path, os.O_RDWR | os.O_NOCTTY))

    def test_repeated_opens_inside_the_window_scan_once(self):
        now = [100.0]
        scans = self.counted_scans()
        runtime, _heartbeat = self.runtime(clock=lambda: now[0])
        runtime.tick()
        self.assertEqual(len(scans), 1, "a new connection is scanned once")
        self.open_and_close()
        runtime.tick()
        self.assertEqual(len(scans), 2, "the first open is scanned at once")
        for second in (101.0, 102.0, 103.0, 104.8):
            now[0] = second
            self.open_and_close()
            runtime.tick()
        self.assertEqual(len(scans), 2, "opens inside the window wait")
        # The waiting scan sets the next wake, not the 1 s cap.
        self.assertEqual(FakeGLib.added[-1][:2], ("timeout", 200))
        now[0] = 105.0
        runtime.tick()
        self.assertEqual(len(scans), 3, "one scan at the end of the window")
        runtime.tick()
        self.assertEqual(len(scans), 3)

    def test_scan_after_the_window_still_lends(self):
        now = [100.0]
        scans = self.counted_scans()
        runtime, _heartbeat = self.runtime(clock=lambda: now[0])
        runtime.tick()
        self.open_and_close()
        runtime.tick()
        now[0] = 101.0
        child = foreign_opener(self.path)
        try:
            runtime.tick()
            self.assertFalse(runtime.paused, "inside the window")
            now[0] = 105.0
            runtime.tick()
            self.assertEqual(runtime.lent_to, (child.pid,))
            self.assertEqual(len(scans), 3)
        finally:
            release(child)


if __name__ == "__main__":
    unittest.main()
