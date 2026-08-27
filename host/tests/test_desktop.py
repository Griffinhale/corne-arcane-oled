from __future__ import annotations

import unittest

from arcane_host.desktop import DesktopMonitor, DesktopNotificationAdapter
from arcane_host.policy import NotificationPolicy
from arcane_host.protocol import Category, Priority


class DesktopTests(unittest.TestCase):
    def setUp(self) -> None:
        self.policy = NotificationPolicy()
        self.focused = set()
        self.adapter = DesktopNotificationAdapter(
            self.policy, b"session salt", self.focused.__contains__
        )

    def notify(
        self, serial=1, replaces=0, *, summary="secret", body="body", hints=None, now=0, sender=""
    ):
        return self.adapter.handle_notify(
            serial, "org.example.App", replaces, summary, body, hints or {}, now, sender
        )

    def test_plaintext_redaction_and_salted_deduplication(self) -> None:
        first = self.adapter.digest_text("secret", "body")
        other_session = DesktopNotificationAdapter(
            NotificationPolicy(), b"different salt", lambda _: False
        ).digest_text("secret", "body")
        self.assertNotEqual(first, other_session)
        self.notify(serial=1)
        self.adapter.handle_reply(1, 10, 0)
        self.notify(serial=2)
        self.adapter.handle_reply(2, 11, 0)
        self.assertEqual(self.policy.summary(0).count, 1)
        self.assertEqual(self.adapter.retained_plaintext, ())
        self.assertNotIn("secret", repr(self.adapter.__dict__))
        self.assertNotIn("body", repr(self.adapter.__dict__))

    def test_focused_source_suppression_but_critical_passes(self) -> None:
        digest = self.adapter.digest_identifier("org.example.App")
        self.focused.add(digest)
        self.assertFalse(self.notify(serial=1))
        self.assertTrue(self.notify(serial=2, hints={"urgency": 2}))

    def test_profile_aliases_share_focus_digest_and_override_category(self) -> None:
        self.assertEqual(
            self.adapter.digest_identifier("firefox"),
            self.adapter.digest_identifier("org.mozilla.firefox.desktop"),
        )
        self.adapter.handle_notify(1, "Slack", 0, "secret", "body", {}, 0, "")
        self.adapter.handle_reply(1, 10, 0)
        self.assertEqual(self.policy.summary(0).category, Category.COMMUNICATION)

    def test_replacement_and_close_correlation(self) -> None:
        self.notify(serial=1, hints={"urgency": 2, "category": "security.auth"})
        self.adapter.handle_reply(1, 50, 0)
        self.assertTrue(self.policy.summary(0).persistent)
        self.notify(serial=2, replaces=50, summary="updated", hints={"urgency": 2}, now=1)
        self.adapter.handle_reply(2, 50, 1)
        self.assertEqual(self.policy.summary(1).count, 1)
        self.assertTrue(self.adapter.handle_closed(50))
        self.assertEqual(self.policy.summary(1).count, 0)

    def test_transient_critical_is_not_persistent(self) -> None:
        self.notify(hints={"urgency": 2, "transient": True})
        self.adapter.handle_reply(1, 3, 0)
        summary = self.policy.summary(0)
        self.assertEqual(summary.priority, Priority.CRITICAL)
        self.assertFalse(summary.persistent)

    def test_request_serials_are_correlated_per_redacted_bus_peer(self) -> None:
        self.notify(serial=1, sender=":1.20", summary="first")
        self.notify(
            serial=1,
            sender=":1.21",
            summary="second",
            hints={"urgency": 2, "category": "security.auth"},
        )
        self.assertEqual(len(self.adapter._pending), 2)
        self.adapter.handle_reply(1, 21, 0, destination=":1.21")
        summary = self.policy.summary(0)
        self.assertEqual(summary.category, Category.SECURITY)
        self.assertTrue(summary.persistent)
        self.adapter.handle_reply(1, 20, 0, destination=":1.20")
        self.assertEqual(self.policy.summary(0).count, 2)
        self.assertNotIn(":1.20", repr(self.adapter.__dict__))
        self.assertNotIn(":1.21", repr(self.adapter.__dict__))

    def test_replacement_can_promote_normal_to_persistent_without_increment(self) -> None:
        self.notify(serial=1)
        self.adapter.handle_reply(1, 8, 0)
        self.notify(serial=2, replaces=8, summary="critical update", hints={"urgency": 2}, now=1)
        self.adapter.handle_reply(2, 8, 1)
        summary = self.policy.summary(1)
        self.assertEqual(summary.count, 1)
        self.assertEqual(summary.priority, Priority.CRITICAL)
        self.assertTrue(summary.persistent)

    def test_monitor_consumes_eavesdropped_traffic(self) -> None:
        """A monitor that returns messages is disconnected by dbus-broker.

        GDBus routes anything the filter returns, so an eavesdropped Notify
        would be answered with UnknownMethod from the monitor connection. The
        broker terminates monitors that send, permanently deafening the
        adapter after the first notification.
        """

        class Gio:
            class DBusMessageType:
                METHOD_CALL = 1
                METHOD_RETURN = 2
                SIGNAL = 4

        class Message:
            def __init__(self, message_type, member=None):
                self._type = message_type
                self._member = member

            def get_message_type(self):
                return self._type

            def get_interface(self):
                return "org.freedesktop.Notifications"

            def get_member(self):
                return self._member

            def get_body(self):
                return None

            def get_serial(self):
                return 1

            def get_reply_serial(self):
                return 1

            def get_sender(self):
                return ":1.20"

            def get_destination(self):
                return ":1.20"

        monitor = DesktopMonitor(Gio, object(), self.adapter, lambda: 0)
        for message in (
            Message(Gio.DBusMessageType.METHOD_CALL, "Notify"),
            Message(Gio.DBusMessageType.METHOD_RETURN),
            Message(Gio.DBusMessageType.SIGNAL, "NotificationClosed"),
            Message(99),
        ):
            self.assertIsNone(monitor._filter(None, message, True, None))

    def test_closed_monitor_stops_reporting_as_enabled(self) -> None:
        class Gio:
            pass

        monitor = DesktopMonitor(Gio, object(), self.adapter, lambda: 0)
        self.adapter.monitor_enabled = True
        monitor._on_closed(object(), True, None)
        self.assertFalse(self.adapter.monitor_enabled)
        self.assertEqual(self.adapter.monitor_error, "ConnectionClosed")

    def test_monitor_denied_disables_only_adapter(self) -> None:
        class DeniedGio:
            class BusType:
                SESSION = 0

            @staticmethod
            def dbus_address_get_for_bus_sync(*_args):
                raise RuntimeError("denied")

        monitor = DesktopMonitor(DeniedGio, object(), self.adapter, lambda: 0)
        self.assertFalse(monitor.start())
        self.assertFalse(self.adapter.monitor_enabled)


if __name__ == "__main__":
    unittest.main()
