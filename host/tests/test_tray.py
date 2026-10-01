"""The tray icon on a private bus, against a fake Control service and a fake
StatusNotifierWatcher. No real service, panel or desktop bus is involved."""

from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from arcane_host.app_controls import Controls, ServiceView
from arcane_host.dbus_contract import (
    BUS_NAME,
    CONTROL_INTERFACE,
    CONTROL_XML,
    OBJECT_PATH,
    PAUSE,
    STATUS,
    STATUS_CHANGED,
    STATUS_SIGNATURE,
    WORLD,
    WORLD_SIGNATURE,
)
from test_dbus_control import Gio, GLib, wait_until

if Gio is not None:
    from arcane_host import tray

WATCHER_XML = """
<node>
  <interface name='org.kde.StatusNotifierWatcher'>
    <method name='RegisterStatusNotifierItem'>
      <arg type='s' name='service' direction='in'/>
    </method>
  </interface>
</node>
"""


class FakeService:
    """Owns the service's bus name and answers Control like the daemon does."""

    def __init__(self, connection) -> None:
        self.connection = connection
        self.status = ("connected", "/dev/hidraw3", False, "")
        self.calls: list[tuple] = []
        info = Gio.DBusNodeInfo.new_for_xml(CONTROL_XML).interfaces[0]
        self.registration = connection.register_object(OBJECT_PATH, info, self._call, None, None)
        self.owner = Gio.bus_own_name_on_connection(
            connection, BUS_NAME, Gio.BusNameOwnerFlags.NONE, None, None
        )

    def _call(self, _connection, _sender, _path, _interface, method, parameters, invocation):
        self.calls.append((method, parameters.unpack() if parameters else ()))
        if method == STATUS:
            invocation.return_value(GLib.Variant(STATUS_SIGNATURE, self.status))
        elif method == WORLD:
            invocation.return_value(GLib.Variant(WORLD_SIGNATURE, (0,) * 8))
        elif method == PAUSE:
            (label,) = parameters.unpack()
            self.set_status(("paused", "", True, label))
            invocation.return_value(None)
        else:
            invocation.return_value(None)

    def set_status(self, status) -> None:
        self.status = status
        self.connection.emit_signal(
            None,
            OBJECT_PATH,
            CONTROL_INTERFACE,
            STATUS_CHANGED,
            GLib.Variant(STATUS_SIGNATURE, status),
        )

    def leave(self) -> None:
        Gio.bus_unown_name(self.owner)
        self.connection.unregister_object(self.registration)


class FakeWatcher:
    def __init__(self, connection) -> None:
        self.registered: list[tuple[str, str]] = []
        info = Gio.DBusNodeInfo.new_for_xml(WATCHER_XML).interfaces[0]
        connection.register_object(tray.WATCHER_PATH, info, self._call, None, None)
        Gio.bus_own_name_on_connection(
            connection, tray.WATCHER_NAME, Gio.BusNameOwnerFlags.NONE, None, None
        )

    def _call(self, _connection, sender, _path, _interface, _method, parameters, invocation):
        self.registered.append((sender, parameters.unpack()[0]))
        invocation.return_value(None)


@unittest.skipUnless(
    Gio is not None and shutil.which("dbus-daemon"), "needs PyGObject and dbus-daemon"
)
class TrayTests(unittest.TestCase):
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
        self.watcher = FakeWatcher(self.connect())
        self.service = FakeService(self.connect())
        self.panel = self.connect()
        self.signals: list[str] = []
        self.panel.signal_subscribe(
            None,
            tray.ITEM_INTERFACE,
            None,
            tray.ITEM_PATH,
            None,
            Gio.DBusSignalFlags.NONE,
            lambda *args: self.signals.append(args[4]),
        )
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        connection = self.connect()
        self.view = ServiceView(Gio, GLib, connection)
        self.addCleanup(self.view.close)
        self.controls = Controls(
            self.view, lock=Path(directory.name) / "hid.lock", label=tray.TRAY_LABEL
        )
        self.addCleanup(self.controls.close)
        self.opened: list[str] = []
        self.item = tray.TrayItem(
            Gio, GLib, connection, self.view, self.controls, lambda: self.opened.append("app")
        )
        self.addCleanup(self.item.close)
        self.tray_name = connection.get_unique_name()

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

    def settle(self, condition, timeout: float = 5.0) -> bool:
        context = GLib.MainContext.default()

        def pumped() -> bool:
            while context.iteration(False):
                pass
            return condition()

        return wait_until(pumped, timeout)

    def ask(self, path: str, interface: str, method: str, arguments, reply: str):
        """Call the tray the way a panel would, without blocking its handlers."""
        result: dict = {}

        def done(connection, outcome) -> None:
            result["value"] = connection.call_finish(outcome).unpack()

        self.panel.call(
            self.tray_name,
            path,
            interface,
            method,
            arguments,
            GLib.VariantType(reply),
            Gio.DBusCallFlags.NONE,
            1000,
            None,
            done,
        )
        self.assertTrue(self.settle(lambda: "value" in result), f"{method} never answered")
        return result["value"]

    def item_property(self, name: str):
        return self.ask(
            tray.ITEM_PATH,
            "org.freedesktop.DBus.Properties",
            "Get",
            GLib.Variant("(ss)", (tray.ITEM_INTERFACE, name)),
            "(v)",
        )[0]

    def menu(self) -> dict[int, dict]:
        _revision, (_root, _props, children) = self.ask(
            tray.MENU_PATH,
            tray.MENU_INTERFACE,
            "GetLayout",
            GLib.Variant("(iias)", (0, -1, [])),
            "(u(ia{sv}av))",
        )
        return {item_id: props for item_id, props, _ in children}

    def test_state_follows_status(self) -> None:
        self.assertTrue(self.settle(lambda: self.view.status is not None))
        self.assertTrue(self.settle(lambda: self.watcher.registered, 3.0))
        self.assertEqual(self.watcher.registered[0][1], self.item.name)
        self.assertEqual(self.item_property("IconName"), "input-keyboard")
        self.assertEqual(self.item_property("Status"), "Active")
        self.assertEqual(self.item_property("ToolTip")[3], "Keyboard connected")

        # Someone else borrows the keyboard: the service says so, the icon follows.
        self.service.set_status(("paused", "", True, "Vial (pid 7)"))
        self.assertTrue(self.settle(lambda: "NewIcon" in self.signals))
        self.assertEqual(self.item_property("IconName"), "media-playback-pause")
        menu = self.menu()
        self.assertEqual(menu[tray.LINE]["label"], "Keyboard lent to Vial (pid 7)")
        self.assertFalse(menu[tray.PAUSE_ITEM]["enabled"])
        self.assertFalse(menu[tray.VIAL_ITEM]["enabled"])

        self.service.set_status(("absent", "", False, ""))
        self.assertTrue(self.settle(lambda: self.item.icon == "dialog-warning"))
        self.assertEqual(self.item_property("Status"), "NeedsAttention")

        self.service.leave()
        self.assertTrue(self.settle(lambda: self.item.icon == "dialog-error"))
        self.assertEqual(
            self.item_property("ToolTip")[3], "Service not running, so the city is offline"
        )

    def test_menu_pauses_through_the_shared_controls(self) -> None:
        self.assertTrue(self.settle(lambda: self.view.status is not None))
        self.assertTrue(self.menu()[tray.PAUSE_ITEM]["enabled"])
        self.ask(
            tray.MENU_PATH,
            tray.MENU_INTERFACE,
            "Event",
            GLib.Variant("(isvu)", (tray.PAUSE_ITEM, "clicked", GLib.Variant("s", ""), 0)),
            "()",
        )
        self.assertTrue(self.settle(lambda: self.controls.holding))
        self.assertEqual(self.service.calls[-1], (PAUSE, (self.controls.label,)))
        self.assertTrue(self.controls.label.startswith("Corne Arcane tray (pid "))
        self.assertTrue(
            self.settle(lambda: self.menu()[tray.PAUSE_ITEM]["label"] == "Resume keyboard")
        )

        self.ask(
            tray.ITEM_PATH, tray.ITEM_INTERFACE, "Activate", GLib.Variant("(ii)", (0, 0)), "()"
        )
        self.assertEqual(self.opened, ["app"])


if __name__ == "__main__":
    unittest.main()
