"""Live-bus check that the notification monitor survives past the first reply.

The adapter eavesdrops with BecomeMonitor. A monitor that sends anything is
terminated by dbus-broker, which is how the adapter previously went deaf after a
single notification while still reporting healthy -- a failure invisible to unit
tests using fake Gio objects, and invisible on dbus-daemon, which tolerates the
same mistake. This exercises the real path, so run it against both bus
implementations: Debian 12 defaults to dbus-daemon and Debian 13 to dbus-broker.

Skipped unless PyGObject and a session bus are both present.
"""

from __future__ import annotations

import os
import unittest

from arcane_host.desktop import DesktopMonitor, DesktopNotificationAdapter
from arcane_host.policy import NotificationPolicy

try:
    import gi

    gi.require_version("Gio", "2.0")
    from gi.repository import Gio, GLib
except (ImportError, ValueError):  # pragma: no cover - environment dependent
    Gio = None
    GLib = None

NOTIFICATIONS_NAME = "org.freedesktop.Notifications"
NOTIFICATIONS_PATH = "/org/freedesktop/Notifications"

STUB_XML = """
<node>
  <interface name='org.freedesktop.Notifications'>
    <method name='Notify'>
      <arg type='s' name='appName' direction='in'/>
      <arg type='u' name='replacesId' direction='in'/>
      <arg type='s' name='appIcon' direction='in'/>
      <arg type='s' name='summary' direction='in'/>
      <arg type='s' name='body' direction='in'/>
      <arg type='as' name='actions' direction='in'/>
      <arg type='a{sv}' name='hints' direction='in'/>
      <arg type='i' name='expireTimeout' direction='in'/>
      <arg type='u' name='id' direction='out'/>
    </method>
  </interface>
</node>
"""

NOTIFICATION_COUNT = 3


def _session_bus_available() -> bool:
    return Gio is not None and bool(os.environ.get("DBUS_SESSION_BUS_ADDRESS"))


@unittest.skipUnless(_session_bus_available(), "needs PyGObject and a session bus")
class NotificationMonitorLiveBusTests(unittest.TestCase):
    def setUp(self) -> None:
        self.connection = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        # A separate connection sends the notifications, as a real application
        # would; the adapter correlates a Notify with its reply by (peer, serial).
        address = Gio.dbus_address_get_for_bus_sync(Gio.BusType.SESSION, None)
        self.client = Gio.DBusConnection.new_for_address_sync(
            address,
            Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT
            | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,
            None,
            None,
        )
        self.addCleanup(self.client.close_sync, None)
        self.replies: list[int] = []
        self.adapter = DesktopNotificationAdapter(
            NotificationPolicy(), b"session salt", lambda _digest: False
        )
        self.monitor = DesktopMonitor(Gio, GLib, self.adapter, lambda: 0.0)
        self.addCleanup(self.monitor.close)
        self._next_id = 0
        self._registration = 0
        self._name_id = 0

    def tearDown(self) -> None:
        if self._registration:
            self.connection.unregister_object(self._registration)
        if self._name_id:
            Gio.bus_unown_name(self._name_id)

    def _serve_notifications(self) -> None:
        """Own org.freedesktop.Notifications so Notify calls get a real reply.

        The adapter correlates a Notify with its method_return, so a bus with no
        notification server would exercise only half the path.
        """
        node = Gio.DBusNodeInfo.new_for_xml(STUB_XML)

        def handle(_conn, _sender, _path, _iface, method, _params, invocation):
            if method == "Notify":
                self._next_id += 1
                invocation.return_value(GLib.Variant("(u)", (self._next_id,)))
            else:  # pragma: no cover - the stub implements one method
                invocation.return_value(None)

        self._registration = self.connection.register_object(
            NOTIFICATIONS_PATH, node.interfaces[0], handle, None, None
        )
        acquired: list[bool] = []
        lost: list[bool] = []
        # DO_NOT_QUEUE, never REPLACE: on a real desktop the session's own
        # notification server owns this name, and stealing it would silence the
        # user's notifications. Skip there and run under dbus-run-session.
        self._name_id = Gio.bus_own_name_on_connection(
            self.connection,
            NOTIFICATIONS_NAME,
            Gio.BusNameOwnerFlags.DO_NOT_QUEUE,
            lambda *_args: acquired.append(True),
            lambda *_args: lost.append(True),
        )
        self._pump(
            lambda: bool(acquired or lost),
            "bus never answered the name request for the notification stub",
        )
        if lost:
            self.skipTest(
                "a notification server already owns org.freedesktop.Notifications; "
                "run this suite under dbus-run-session for an isolated bus"
            )

    def _collect_reply(self, source, result, _user_data) -> None:
        self.replies.append(source.call_finish(result).unpack()[0])

    def _pump(self, done, message, timeout_seconds: float = 5.0) -> None:
        """Drive the main loop until done() or a deadline, then assert progress."""
        loop = GLib.MainLoop()
        state = {"expired": False}

        def poll() -> bool:
            if done():
                loop.quit()
                return False
            return True

        def expire() -> bool:
            state["expired"] = True
            loop.quit()
            return False

        GLib.timeout_add(10, poll)
        GLib.timeout_add(int(timeout_seconds * 1000), expire)
        loop.run()
        self.assertFalse(state["expired"], message)

    def test_every_notification_reaches_the_adapter_not_only_the_first(self) -> None:
        self._serve_notifications()
        self.assertTrue(self.monitor.start(), f"BecomeMonitor failed: {self.adapter.monitor_error}")

        for index in range(NOTIFICATION_COUNT):
            # A callback is required, not cosmetic: passing None makes GDBus set
            # NO_REPLY_EXPECTED, the server suppresses the method_return, and the
            # monitor has nothing to correlate.
            self.client.call(
                NOTIFICATIONS_NAME,
                NOTIFICATIONS_PATH,
                NOTIFICATIONS_NAME,
                "Notify",
                GLib.Variant(
                    "(susssasa{sv}i)",
                    (
                        "corne-arcane-tests",
                        0,
                        "",
                        f"summary {index}",
                        "body",
                        [],
                        {"urgency": GLib.Variant("y", 1)},
                        -1,
                    ),
                ),
                GLib.VariantType.new("(u)"),
                Gio.DBusCallFlags.NONE,
                2000,
                None,
                self._collect_reply,
                None,
            )

        self._pump(
            lambda: self.adapter.counters.matched_replies >= NOTIFICATION_COUNT,
            "monitor stopped correlating replies before every notification arrived "
            f"(matched {self.adapter.counters.matched_replies} of {NOTIFICATION_COUNT}, "
            f"enabled={self.adapter.monitor_enabled}, error={self.adapter.monitor_error})",
        )

        self.assertTrue(self.monitor.connection is not None)
        self.assertTrue(self.adapter.monitor_enabled, "monitor was terminated by the bus")
        self.assertEqual(self.adapter.counters.parse_failures, 0)
        self.assertEqual(self.adapter.retained_plaintext, ())


if __name__ == "__main__":
    unittest.main()
