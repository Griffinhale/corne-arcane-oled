from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

from arcane_host.daemon import HidHeartbeat, KWinBridgeLoader, KWIN_SERVICE
from arcane_host.protocol import Message, Scene


class FakeDevice:
    def __init__(self, fail_after: int | None = None) -> None:
        self.reports: list[bytes] = []
        self.fail_after = fail_after
        self.closed = False

    def send(self, report: bytes) -> None:
        if self.fail_after is not None and len(self.reports) >= self.fail_after:
            raise OSError("unplugged")
        self.reports.append(report)

    def close(self) -> None:
        self.closed = True


class HeartbeatTests(unittest.TestCase):
    def test_initial_duel_hello_and_500ms_continuity(self) -> None:
        device = FakeDevice()
        heartbeat = HidHeartbeat(lambda: Scene.DUEL, lambda: device, lambda: 7)
        self.assertFalse(heartbeat.tick(0.0))
        self.assertEqual(device.reports[0][3], Message.HELLO)
        self.assertEqual(device.reports[0][11], Scene.DUEL)
        self.assertTrue(heartbeat.tick(0.1))
        self.assertFalse(heartbeat.tick(0.59))
        self.assertTrue(heartbeat.tick(0.6))
        self.assertEqual([report[3] for report in device.reports], [Message.HELLO, Message.HEARTBEAT, Message.HEARTBEAT])

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
        heartbeat = HidHeartbeat(lambda: Scene.ARCHIVE, lambda: next(devices), lambda: next(sessions))
        heartbeat.tick(0.0)   # first HELLO
        heartbeat.tick(0.1)   # failed heartbeat -> disconnected
        heartbeat.tick(2.09)  # not yet
        heartbeat.tick(2.1)   # second HELLO
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

    def call_sync(self, service, path, interface, method, parameters, result_type, flags, timeout, cancellable):
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
            connection.callback(None, None, None, None, None, FakeVariant((KWIN_SERVICE, "old", "new")))
            loads = [call for call in connection.calls if call[3] == "loadScript"]
            starts = [call for call in connection.calls if call[3] == "run"]
            self.assertEqual(len(loads), 2)
            self.assertEqual(len(starts), 2)


if __name__ == "__main__":
    unittest.main()
