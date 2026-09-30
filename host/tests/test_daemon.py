from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

from arcane_host.adapters import SemanticAdapters
from arcane_host.daemon import default_kwin_script
from arcane_host.dbus_contract import (
    BUS_NAME,
    EVENTS_INTERFACE,
    INJECT_SYNTHETIC,
    KWIN_SERVICE,
    REPORT_REPOSITORY_STATE,
    RepositoryState,
)
from arcane_host.dbus_services import EventService, KWinBridgeLoader
from arcane_host.focus import FocusArbiter
from arcane_host.heartbeat import DryRunTransport, HidHeartbeat
from arcane_host.policy import NotificationPolicy
from arcane_host.protocol import (
    Category,
    CivicState,
    Floor,
    Message,
    Mode,
    NotificationSummary,
    Priority,
    Scene,
    Secondary,
)
from arcane_host.semantic import SemanticResolver

try:
    import gi

    gi.require_version("Gio", "2.0")
    from gi.repository import Gio, GLib
except (ImportError, ValueError):  # pragma: no cover - environment dependent
    Gio = None
    GLib = None


class FakeDevice:
    def __init__(
        self, fail_after: int | None = None, *, reply: bytes | None = None, timeout: bool = False
    ) -> None:
        self.reports: list[bytes] = []
        self.fail_after = fail_after
        self.reply = reply
        self.timeout = timeout
        self.closed = False

    def send(self, report: bytes) -> None:
        if self.fail_after is not None and len(self.reports) >= self.fail_after:
            raise OSError("unplugged")
        self.reports.append(report)

    def receive(self, _timeout: float) -> bytes:
        if self.timeout:
            raise TimeoutError("missing echo")
        return self.reply if self.reply is not None else self.reports[-1]

    def close(self) -> None:
        self.closed = True


class HeartbeatTests(unittest.TestCase):
    def test_dry_run_receive_before_send_fails_clearly(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "before send"):
            DryRunTransport().receive(0.25)

    def test_dry_run_once_prints_hello_and_heartbeat(self) -> None:
        result = subprocess.run(
            [
                sys.executable,
                "-m",
                "arcane_host.daemon",
                "--dry-run",
                "--once",
                "--session",
                "1",
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        reports = [bytes.fromhex(line) for line in result.stdout.splitlines()]
        self.assertEqual(len(reports), 2)
        self.assertEqual([len(report) for report in reports], [32, 32])
        self.assertEqual([report[3] for report in reports], [Message.HELLO, Message.HEARTBEAT])
        self.assertEqual([int.from_bytes(report[4:8], "little") for report in reports], [1, 1])

    def test_initial_duel_hello_and_500ms_continuity(self) -> None:
        device = FakeDevice()
        heartbeat = HidHeartbeat(lambda: Scene.DUEL, lambda: device, lambda: 7)
        self.assertFalse(heartbeat.tick(0.0))
        self.assertEqual(device.reports[0][3], Message.HELLO)
        self.assertEqual(device.reports[0][11], Scene.DUEL)
        self.assertTrue(heartbeat.tick(0.1))
        self.assertFalse(heartbeat.tick(0.59))
        self.assertTrue(heartbeat.tick(0.6))
        self.assertEqual(
            [report[3] for report in device.reports],
            [Message.HELLO, Message.HEARTBEAT, Message.HEARTBEAT],
        )

    def test_absent_hid_retries_every_two_seconds(self) -> None:
        attempts = 0

        def unavailable():
            nonlocal attempts
            attempts += 1
            raise RuntimeError("absent")

        heartbeat = HidHeartbeat(lambda: Scene.DUEL, unavailable, lambda: 1)
        heartbeat.tick(0.0)
        heartbeat.tick(1.99)
        heartbeat.tick(2.0)
        self.assertEqual(attempts, 2)

    def test_unplug_reconnect_gets_fresh_session_and_hello(self) -> None:
        first = FakeDevice(fail_after=1)
        second = FakeDevice()
        devices = iter((first, second))
        sessions = iter((0x11111111, 0x22222222))
        heartbeat = HidHeartbeat(
            lambda: Scene.ARCHIVE, lambda: next(devices), lambda: next(sessions)
        )
        heartbeat.tick(0.0)  # first HELLO
        heartbeat.tick(0.1)  # failed heartbeat -> disconnected
        heartbeat.tick(2.09)  # not yet
        heartbeat.tick(2.1)  # second HELLO
        self.assertTrue(first.closed)
        self.assertEqual(second.reports[0][3], Message.HELLO)
        self.assertEqual(int.from_bytes(second.reports[0][4:8], "little"), 0x22222222)
        self.assertEqual(second.reports[0][11], Scene.ARCHIVE)

    def test_reconnect_never_reuses_same_generated_session(self) -> None:
        first = FakeDevice(fail_after=1)
        second = FakeDevice()
        devices = iter((first, second))
        heartbeat = HidHeartbeat(lambda: Scene.DUEL, lambda: next(devices), lambda: 9)
        heartbeat.tick(0.0)
        heartbeat.tick(0.1)
        heartbeat.tick(2.1)
        self.assertEqual(int.from_bytes(first.reports[0][4:8], "little"), 9)
        self.assertEqual(int.from_bytes(second.reports[0][4:8], "little"), 10)

    def test_settled_scene_change_sends_without_waiting_for_heartbeat(self) -> None:
        scene = Scene.DUEL
        device = FakeDevice()
        heartbeat = HidHeartbeat(lambda: scene, lambda: device, lambda: 7)
        heartbeat.tick(0.0)
        heartbeat.tick(0.1)
        scene = Scene.ARCHIVE
        heartbeat.request_heartbeat(0.2)
        self.assertTrue(heartbeat.tick(0.2))
        self.assertEqual(device.reports[-1][11], Scene.ARCHIVE)

    def test_notify_is_prompt_and_never_delays_heartbeat(self) -> None:
        summary = NotificationSummary()
        device = FakeDevice()
        heartbeat = HidHeartbeat(
            lambda: Scene.DUEL,
            lambda: device,
            lambda: 7,
            summary_provider=lambda: summary,
        )
        heartbeat.tick(0)
        summary = NotificationSummary(1, Category.TERMINAL, Priority.LOW)
        heartbeat.request_notify()
        self.assertFalse(heartbeat.tick(0.05))
        self.assertEqual(device.reports[-1][3], Message.NOTIFY)
        for _ in range(20):
            heartbeat.request_notify()
        self.assertTrue(heartbeat.tick(0.1))
        self.assertEqual(device.reports[-1][3], Message.HEARTBEAT)
        self.assertFalse(heartbeat.tick(0.11))
        self.assertTrue(heartbeat.tick(0.6))

    def test_every_report_carries_complete_eight_byte_semantics(self) -> None:
        civic = CivicState(Floor.WORKSHOP, Mode.QUIET, secondary=Secondary.TRANSFER)
        device = FakeDevice()
        heartbeat = HidHeartbeat(
            lambda: Scene.DUEL,
            lambda: device,
            lambda: 7,
            civic_provider=lambda: civic,
        )
        heartbeat.tick(0.0)  # HELLO
        heartbeat.tick(0.1)  # HEARTBEAT
        for report in device.reports:
            self.assertEqual(report[10], 8)
            self.assertEqual(report[17], civic.civic_byte())  # floor|mode|intensity
            self.assertEqual(report[18], civic.secondary_byte())
        default_device = FakeDevice()
        default = HidHeartbeat(lambda: Scene.DUEL, lambda: default_device, lambda: 7)
        default.tick(0.0)
        self.assertEqual(default_device.reports[0][10], 8)
        self.assertEqual(default_device.reports[0][17:19], b"\0\0")

    def test_missing_hello_echo_retries_with_new_device_and_session(self) -> None:
        first = FakeDevice(timeout=True)
        second = FakeDevice()
        devices = iter((first, second))
        sessions = iter((10, 20))
        heartbeat = HidHeartbeat(lambda: Scene.DUEL, lambda: next(devices), lambda: next(sessions))
        heartbeat.tick(0.0)
        self.assertTrue(first.closed)
        self.assertIsNone(heartbeat.device)
        heartbeat.tick(2.0)
        self.assertEqual(int.from_bytes(second.reports[0][4:8], "little"), 20)

    def test_mismatched_heartbeat_echo_disconnects_and_resets_session(self) -> None:
        first = FakeDevice()
        second = FakeDevice()
        devices = iter((first, second))
        sessions = iter((10, 20))
        heartbeat = HidHeartbeat(lambda: Scene.DUEL, lambda: next(devices), lambda: next(sessions))
        heartbeat.tick(0.0)
        first.reply = bytes(32)
        heartbeat.tick(0.1)
        self.assertTrue(first.closed)
        self.assertIsNone(heartbeat.device)
        heartbeat.tick(2.1)
        self.assertEqual(int.from_bytes(second.reports[0][4:8], "little"), 20)

    def test_reconnect_hello_carries_complete_summary(self) -> None:
        summary = NotificationSummary(2, Category.SECURITY, Priority.CRITICAL, 4, True)
        device = FakeDevice()
        heartbeat = HidHeartbeat(
            lambda: Scene.FOCUS, lambda: device, lambda: 3, summary_provider=lambda: summary
        )
        heartbeat.tick(0)
        self.assertEqual(
            device.reports[0][11:17],
            bytes((Scene.FOCUS, 2, Category.SECURITY, Priority.CRITICAL, 4, 1)),
        )


class EventServiceTests(unittest.TestCase):
    def make_service(self, focus: FocusArbiter, policy: NotificationPolicy, now=20.0):
        service = EventService.__new__(EventService)
        service.policy = policy
        service.focus = focus
        service.clock = lambda: now
        service.changed_calls = 0
        service.changed = lambda: setattr(service, "changed_calls", service.changed_calls + 1)
        service.adapters = SemanticAdapters(
            SemanticResolver(), policy, service.changed, service.clock
        )
        return service

    def test_terminal_threshold_focus_and_priority(self) -> None:
        policy = NotificationPolicy()
        focus = FocusArbiter(settle_seconds=0)
        service = self.make_service(focus, policy)
        self.assertFalse(service.report_terminal_completion(9999, 0))
        self.assertTrue(service.report_terminal_completion(10000, 0))
        self.assertEqual(policy.summary(20).priority, Priority.LOW)
        policy.clear()
        focus.report("org.kde.konsole", "org.kde.konsole", 20)
        focus.poll(20)
        self.assertFalse(service.report_terminal_completion(11000, 1))
        focus.report("org.kde.kate", "org.kde.kate", 20)
        focus.poll(20)
        self.assertTrue(service.report_terminal_completion(11000, 3))
        self.assertEqual(policy.summary(20).priority, Priority.NORMAL)

    def test_repository_state_is_enum_only(self) -> None:
        policy = NotificationPolicy()
        focus = FocusArbiter(settle_seconds=0)
        service = self.make_service(focus, policy)
        self.assertTrue(service.report_repository_state(RepositoryState.DIRTY, True))
        self.assertEqual(policy.summary(20).category, Category.TRANSFER)

    def test_invalid_fields_are_rejected_once_at_dbus_boundary(self) -> None:
        class Invocation:
            error = None
            value = object()

            def return_dbus_error(self, name, message):
                self.error = (name, message)

            def return_value(self, value):
                self.value = value

        service = self.make_service(FocusArbiter(), NotificationPolicy())
        repository = Invocation()
        service._method_call(
            None,
            None,
            None,
            None,
            REPORT_REPOSITORY_STATE,
            FakeVariant((9, True)),
            repository,
        )
        self.assertEqual(
            repository.error,
            (f"{EVENTS_INTERFACE}.InvalidArguments", "invalid repository state"),
        )
        notification = Invocation()
        service._method_call(
            None,
            None,
            None,
            None,
            INJECT_SYNTHETIC,
            FakeVariant((Category.OTHER, Priority.NORMAL, True)),
            notification,
        )
        self.assertEqual(
            notification.error,
            (f"{EVENTS_INTERFACE}.InvalidArguments", "invalid notification fields"),
        )


class FakeVariant:
    def __init__(self, values) -> None:
        self.values = values

    def unpack(self):
        return self.values


class FakeGLib:
    class Variant:
        def __init__(self, signature, values) -> None:
            self.signature = signature
            self.values = values

    class VariantType:
        @staticmethod
        def new(signature):
            return signature


class FakeGio:
    class DBusSignalFlags:
        NONE = 0

    class DBusCallFlags:
        NONE = 0


class FakeConnection:
    def __init__(self) -> None:
        self.calls = []
        self.callback = None

    def signal_subscribe(self, *args):
        self.callback = args[-1]
        return 1

    def call_sync(
        self, service, path, interface, method, parameters, result_type, flags, timeout, cancellable
    ):
        del result_type, flags, timeout, cancellable
        self.calls.append((service, path, interface, method, parameters))
        return FakeVariant((3,)) if method == "loadScript" else None


class KWinRestartTests(unittest.TestCase):
    def test_kwin_name_reappearance_reloads_bridge(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / "main.js"
            script.write_text("// test")
            connection = FakeConnection()
            KWinBridgeLoader(FakeGio, FakeGLib, connection, script)
            connection.callback(
                None, None, None, None, None, FakeVariant((KWIN_SERVICE, "old", "new"))
            )
            loads = [call for call in connection.calls if call[3] == "loadScript"]
            starts = [call for call in connection.calls if call[3] == "run"]
            self.assertEqual(len(loads), 2)
            self.assertEqual(len(starts), 2)


class KWinDefaultPathTests(unittest.TestCase):
    HOST = Path(__file__).resolve().parents[1]

    @unittest.skipUnless(shutil.which("make"), "make is not installed")
    def test_kwin_default_path_installed(self) -> None:
        """The staged daemon's own default must name the script make install placed."""
        with tempfile.TemporaryDirectory() as stage:
            subprocess.run(
                ("make", "-s", "-C", str(self.HOST), "install", f"DESTDIR={stage}", "PREFIX=/usr"),
                check=True,
                stdout=subprocess.DEVNULL,
            )
            libdir = Path(stage) / "usr" / "lib" / "corne-arcane-host"
            env = {k: v for k, v in os.environ.items() if k != "CORNE_ARCANE_KWIN_SCRIPT"}
            env["PYTHONPATH"] = str(libdir)
            env["PYTHONDONTWRITEBYTECODE"] = "1"
            resolved = subprocess.run(
                (
                    sys.executable,
                    "-c",
                    "from arcane_host.daemon import default_kwin_script; "
                    "print(default_kwin_script())",
                ),
                check=True,
                capture_output=True,
                text=True,
                env=env,
                cwd=stage,
            ).stdout.strip()
            installed = Path(stage) / "usr/share/kwin/scripts/cornearcane/contents/code/main.js"
            self.assertTrue(installed.is_file())
            self.assertEqual(Path(resolved), installed)

    def test_source_checkout_uses_its_own_script(self) -> None:
        with patch.dict(os.environ, {}, clear=False):
            os.environ.pop("CORNE_ARCANE_KWIN_SCRIPT", None)
            self.assertEqual(
                default_kwin_script(), self.HOST / "kwin" / "contents" / "code" / "main.js"
            )

    def test_environment_override_wins(self) -> None:
        with patch.dict(os.environ, {"CORNE_ARCANE_KWIN_SCRIPT": "/elsewhere/main.js"}):
            self.assertEqual(default_kwin_script(), Path("/elsewhere/main.js"))


HOST_DIR = Path(__file__).resolve().parents[1]


@unittest.skipUnless(
    Gio is not None and shutil.which("dbus-daemon"), "needs PyGObject and dbus-daemon"
)
class SecondInstanceTests(unittest.TestCase):
    """A second daemon on a private bus, never the desktop's own session bus."""

    def setUp(self) -> None:
        self.bus = subprocess.Popen(
            ["dbus-daemon", "--session", "--nofork", "--print-address=1"],
            stdout=subprocess.PIPE,
            text=True,
        )
        self.addCleanup(self.bus.wait)
        self.addCleanup(self.bus.terminate)
        self.address = self.bus.stdout.readline().strip()
        self.bus.stdout.close()
        self.holder = Gio.DBusConnection.new_for_address_sync(
            self.address,
            Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT
            | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,
            None,
            None,
        )
        self.addCleanup(self.holder.close_sync, None)

    def test_name_taken_exits(self) -> None:
        reply = self.holder.call_sync(
            "org.freedesktop.DBus",
            "/org/freedesktop/DBus",
            "org.freedesktop.DBus",
            "RequestName",
            GLib.Variant("(su)", (BUS_NAME, 4)),
            GLib.VariantType("(u)"),
            Gio.DBusCallFlags.NONE,
            1000,
            None,
        ).unpack()[0]
        self.assertEqual(reply, 1)
        env = dict(
            os.environ,
            DBUS_SESSION_BUS_ADDRESS=self.address,
            DBUS_SYSTEM_BUS_ADDRESS=self.address,
            PYTHONDONTWRITEBYTECODE="1",
        )
        started = time.monotonic()
        # An explicit device path that does not exist: if the name check ever
        # regressed, the heartbeat would fail to open this, not a real keyboard.
        result = subprocess.run(
            [sys.executable, "-m", "arcane_host.daemon", "--device", "/nonexistent/hidraw"],
            cwd=HOST_DIR,
            env=env,
            capture_output=True,
            text=True,
            timeout=10,
        )
        elapsed = time.monotonic() - started
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn(f"{BUS_NAME} is already owned by", result.stderr)
        self.assertIn(f"pid {os.getpid()}", result.stderr)
        self.assertLess(elapsed, 5.0)


if __name__ == "__main__":
    unittest.main()
