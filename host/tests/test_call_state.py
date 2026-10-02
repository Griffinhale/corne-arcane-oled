from __future__ import annotations

import unittest
from pathlib import Path

from arcane_host.adapters import RING_LAPSE, URGENT_LAPSE, SemanticAdapters, UrgentKind
from arcane_host.call_state import capture_running, capture_sampler
from arcane_host.desktop import DesktopNotificationAdapter, normalize_call
from arcane_host.policy import NotificationPolicy
from arcane_host.protocol import Mode
from arcane_host.semantic import SemanticResolver

RUNNING = """state: RUNNING
owner_pid   : 2210
trigger_time: 1234.5
"""


class ModePrecedenceTests(unittest.TestCase):
    def test_urgent_strain_quiet_normal(self) -> None:
        resolver = SemanticResolver()
        resolver.update(dnd=True, strain=True, urgent=True)
        # DND does not hide an incoming call (SH-D4p).
        self.assertEqual(resolver.state.civic.mode, Mode.URGENT)
        resolver.update(urgent=False)
        self.assertEqual(resolver.state.civic.mode, Mode.STRAIN)
        resolver.update(strain=False)
        self.assertEqual(resolver.state.civic.mode, Mode.QUIET)
        resolver.update(dnd=False)
        self.assertEqual(resolver.state.civic.mode, Mode.NORMAL)
        resolver.update(call_joined=True)
        self.assertEqual(resolver.state.civic.mode, Mode.QUIET)


class CallStateTests(unittest.TestCase):
    def test_capture_status_is_device_state_only(self) -> None:
        self.assertTrue(capture_running(RUNNING))
        self.assertFalse(capture_running("closed\n"))
        self.assertFalse(capture_running("state: PREPARED\nowner_pid   : 2210\n"))
        files = {"/a": "closed\n", "/b": RUNNING}

        def read(path: str) -> str:
            if path == "/gone":
                raise FileNotFoundError(path)
            return files[path]

        self.assertTrue(capture_sampler(lambda: ["/gone", "/a", "/b"], read)())
        self.assertFalse(capture_sampler(lambda: ["/gone", "/a"], read)())
        self.assertFalse(capture_sampler(lambda: [], read)())

    def test_call_source_reads_no_stream_or_application_names(self) -> None:
        text = (Path(__file__).parents[1] / "arcane_host" / "call_state.py").read_text()
        self.assertIn("/proc/asound/", text)
        for forbidden in ("pw-dump", "pactl", "pw-cli", "media.name", "application.name", "/fd"):
            self.assertNotIn(forbidden, text)


class UrgentTests(unittest.TestCase):
    def make(self, joined: list[bool] | None = None):
        now = [100.0]
        resolver = SemanticResolver()
        adapters = SemanticAdapters(
            resolver,
            NotificationPolicy(),
            lambda: None,
            lambda: now[0],
            call_sampler=(lambda: joined[0]) if joined is not None else None,
        )
        return now, resolver, adapters

    def test_critical_notification_lapses_or_closes(self) -> None:
        now, resolver, adapters = self.make()
        adapters.urgent_alert(7, UrgentKind.CRITICAL)
        self.assertEqual(resolver.state.civic.mode, Mode.URGENT)
        self.assertEqual(adapters.next_deadline(now[0]), now[0] + URGENT_LAPSE)
        now[0] += URGENT_LAPSE - 1
        adapters.poll(now[0])
        self.assertEqual(resolver.state.civic.mode, Mode.URGENT)
        now[0] += 1
        adapters.poll(now[0])
        self.assertEqual(resolver.state.civic.mode, Mode.NORMAL)
        adapters.urgent_alert(8, UrgentKind.CRITICAL)
        adapters.urgent_alert(8, UrgentKind.CLOSED)
        self.assertEqual(resolver.state.civic.mode, Mode.NORMAL)
        # A close for an id never seen changes nothing.
        adapters.urgent_alert(99, UrgentKind.CLOSED)
        self.assertEqual(resolver.state.civic.mode, Mode.NORMAL)

    def test_ringing_then_joined_then_ended(self) -> None:
        joined = [False]
        now, resolver, adapters = self.make(joined)
        adapters.poll(now[0])
        adapters.urgent_alert(3, UrgentKind.INCOMING_CALL)
        self.assertEqual(resolver.state.civic.mode, Mode.URGENT)
        # Answered: the microphone opens and the city goes QUIET.
        joined[0] = True
        now[0] += 5
        adapters.poll(now[0])
        self.assertEqual(resolver.state.civic.mode, Mode.QUIET)
        # A late ring notification for a joined call does not flash.
        adapters.urgent_alert(4, UrgentKind.INCOMING_CALL)
        self.assertEqual(resolver.state.civic.mode, Mode.QUIET)
        # A critical notification still breaks through a call.
        adapters.urgent_alert(5, UrgentKind.CRITICAL)
        self.assertEqual(resolver.state.civic.mode, Mode.URGENT)
        adapters.urgent_alert(5, UrgentKind.CLOSED)
        joined[0] = False
        now[0] += 5
        adapters.poll(now[0])
        self.assertEqual(resolver.state.civic.mode, Mode.NORMAL)

    def test_unanswered_ring_ends_or_lapses(self) -> None:
        now, resolver, adapters = self.make()
        adapters.urgent_alert(3, UrgentKind.INCOMING_CALL)
        adapters.urgent_alert(4, UrgentKind.CALL_ENDED)
        self.assertEqual(resolver.state.civic.mode, Mode.NORMAL)
        adapters.urgent_alert(3, UrgentKind.INCOMING_CALL)
        now[0] += RING_LAPSE
        adapters.poll(now[0])
        self.assertEqual(resolver.state.civic.mode, Mode.NORMAL)

    def test_no_urgent_or_call_reader_without_the_opt_in(self) -> None:
        _now, _resolver, adapters = self.make()
        self.assertIsNone(adapters.next_deadline(100.0))


class DesktopUrgentTests(unittest.TestCase):
    def test_notifications_pass_only_an_id_and_a_kind(self) -> None:
        self.assertEqual(normalize_call("call.incoming"), UrgentKind.INCOMING_CALL)
        self.assertEqual(normalize_call("call.unanswered"), UrgentKind.CALL_ENDED)
        self.assertEqual(normalize_call("im.received"), UrgentKind.CLOSED)
        seen: list[tuple] = []
        adapter = DesktopNotificationAdapter(
            NotificationPolicy(),
            b"s" * 16,
            lambda _digest: False,
            urgent=lambda *args: seen.append(args),
        )

        def notify(serial, nid, hints, replaces=0):
            adapter.handle_notify(serial, "Chat", replaces, "Ada is calling", "body", hints, 1.0)
            adapter.handle_reply(serial, nid, 1.0)

        notify(1, 10, {"category": "call.incoming"})
        notify(2, 11, {"urgency": 2})
        notify(3, 12, {"urgency": 1, "category": "im.received"})
        adapter.handle_closed(11)
        notify(4, 10, {"category": "call.ended"}, replaces=10)
        self.assertEqual(
            seen,
            [
                (10, UrgentKind.INCOMING_CALL),
                (11, UrgentKind.CRITICAL),
                (12, UrgentKind.CLOSED),
                (11, UrgentKind.CLOSED),
                (10, UrgentKind.CALL_ENDED),
            ],
        )
        for args in seen:
            self.assertEqual([type(value) for value in args], [int, UrgentKind])

        # Without the opt-in the same traffic calls nothing and still works.
        plain = DesktopNotificationAdapter(NotificationPolicy(), b"s" * 16, lambda _d: False)
        plain.handle_notify(1, "Chat", 0, "Ada is calling", "", {"urgency": 2}, 1.0)
        self.assertTrue(plain.handle_reply(1, 10, 1.0))


if __name__ == "__main__":
    unittest.main()
