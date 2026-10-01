"""A tray icon for the Corne Arcane service: link state, pause, Vial, the app.

The icon is a StatusNotifierItem with a com.canonical.dbusmenu menu, both
spoken directly over Gio, so it needs nothing beyond what the daemon already
uses. KDE Plasma, XFCE, Cinnamon and most panels show such items as they are;
GNOME Shell shows them only with the AppIndicator extension installed.

It keeps no state of its own. What it shows is the app's ServiceView, and what
its menu does is the app's Controls: the same pause, held over the tray's own
bus connection for as long as the tray runs, and the same Vial launcher.
"""

from __future__ import annotations

import os
import signal
import subprocess
import sys
from typing import Callable

from .app_controls import Controls, ServiceView, link_caption, tool_command

ITEM_PATH = "/StatusNotifierItem"
MENU_PATH = "/StatusNotifierItem/Menu"
ITEM_INTERFACE = "org.kde.StatusNotifierItem"
MENU_INTERFACE = "com.canonical.dbusmenu"
WATCHER_NAME = "org.kde.StatusNotifierWatcher"
WATCHER_PATH = "/StatusNotifierWatcher"
WATCHER_INTERFACE = "org.kde.StatusNotifierWatcher"
TRAY_LABEL = "Corne Arcane tray"
POLL_MS = 500

# Freedesktop icon names, so every icon theme has them.
ICONS = {
    "starting": "input-keyboard",
    "connected": "input-keyboard",
    "paused": "media-playback-pause",
}
NO_SERVICE_ICON = "dialog-error"
PROBLEM_ICON = "dialog-warning"

ITEM_XML = f"""
<node>
  <interface name='{ITEM_INTERFACE}'>
    <property name='Category' type='s' access='read'/>
    <property name='Id' type='s' access='read'/>
    <property name='Title' type='s' access='read'/>
    <property name='Status' type='s' access='read'/>
    <property name='IconName' type='s' access='read'/>
    <property name='ToolTip' type='(sa(iiay)ss)' access='read'/>
    <property name='ItemIsMenu' type='b' access='read'/>
    <property name='Menu' type='o' access='read'/>
    <method name='Activate'>
      <arg type='i' name='x' direction='in'/>
      <arg type='i' name='y' direction='in'/>
    </method>
    <method name='SecondaryActivate'>
      <arg type='i' name='x' direction='in'/>
      <arg type='i' name='y' direction='in'/>
    </method>
    <method name='ContextMenu'>
      <arg type='i' name='x' direction='in'/>
      <arg type='i' name='y' direction='in'/>
    </method>
    <method name='Scroll'>
      <arg type='i' name='delta' direction='in'/>
      <arg type='s' name='orientation' direction='in'/>
    </method>
    <signal name='NewTitle'/>
    <signal name='NewIcon'/>
    <signal name='NewToolTip'/>
    <signal name='NewStatus'>
      <arg type='s' name='status'/>
    </signal>
  </interface>
</node>
"""

MENU_XML = f"""
<node>
  <interface name='{MENU_INTERFACE}'>
    <property name='Version' type='u' access='read'/>
    <property name='TextDirection' type='s' access='read'/>
    <property name='Status' type='s' access='read'/>
    <property name='IconThemePath' type='as' access='read'/>
    <method name='GetLayout'>
      <arg type='i' name='parentId' direction='in'/>
      <arg type='i' name='recursionDepth' direction='in'/>
      <arg type='as' name='propertyNames' direction='in'/>
      <arg type='u' name='revision' direction='out'/>
      <arg type='(ia{{sv}}av)' name='layout' direction='out'/>
    </method>
    <method name='GetGroupProperties'>
      <arg type='ai' name='ids' direction='in'/>
      <arg type='as' name='propertyNames' direction='in'/>
      <arg type='a(ia{{sv}})' name='properties' direction='out'/>
    </method>
    <method name='GetProperty'>
      <arg type='i' name='id' direction='in'/>
      <arg type='s' name='name' direction='in'/>
      <arg type='v' name='value' direction='out'/>
    </method>
    <method name='Event'>
      <arg type='i' name='id' direction='in'/>
      <arg type='s' name='eventId' direction='in'/>
      <arg type='v' name='data' direction='in'/>
      <arg type='u' name='timestamp' direction='in'/>
    </method>
    <method name='EventGroup'>
      <arg type='a(isvu)' name='events' direction='in'/>
      <arg type='ai' name='idErrors' direction='out'/>
    </method>
    <method name='AboutToShow'>
      <arg type='i' name='id' direction='in'/>
      <arg type='b' name='needUpdate' direction='out'/>
    </method>
    <method name='AboutToShowGroup'>
      <arg type='ai' name='ids' direction='in'/>
      <arg type='ai' name='updatesNeeded' direction='out'/>
      <arg type='ai' name='idErrors' direction='out'/>
    </method>
    <signal name='LayoutUpdated'>
      <arg type='u' name='revision'/>
      <arg type='i' name='parent'/>
    </signal>
    <signal name='ItemsPropertiesUpdated'>
      <arg type='a(ia{{sv}})' name='updatedProps'/>
      <arg type='a(ias)' name='removedProps'/>
    </signal>
  </interface>
</node>
"""

# Menu item ids. 0 is the root.
LINE, SEPARATOR, PAUSE_ITEM, VIAL_ITEM, APP_ITEM = 1, 2, 3, 4, 5


def tray_icon(status: tuple[str, str, bool, str] | None) -> tuple[str, str]:
    """(icon name, StatusNotifierItem status) for a Control status."""
    if status is None:
        return NO_SERVICE_ICON, "NeedsAttention"
    icon = ICONS.get(status[0])
    if icon is None:
        return PROBLEM_ICON, "NeedsAttention"
    return icon, "Active"


class TrayItem:
    """The StatusNotifierItem and its menu, drawn from a ServiceView and Controls."""

    def __init__(
        self,
        Gio,
        GLib,
        connection,
        view: ServiceView,
        controls: Controls,
        open_app: Callable[[], None],
    ) -> None:
        self.Gio = Gio
        self.GLib = GLib
        self.connection = connection
        self.view = view
        self.controls = controls
        self.open_app = open_app
        self.revision = 1
        self.name = f"org.kde.StatusNotifierItem-{os.getpid()}-1"
        self.icon, self.status = tray_icon(view.status)
        self.tooltip = self._tooltip()
        self.items = self._items()
        item_info = Gio.DBusNodeInfo.new_for_xml(ITEM_XML).interfaces[0]
        menu_info = Gio.DBusNodeInfo.new_for_xml(MENU_XML).interfaces[0]
        self._registrations = [
            connection.register_object(
                ITEM_PATH, item_info, self._item_call, self._item_property, None
            ),
            connection.register_object(
                MENU_PATH, menu_info, self._menu_call, self._menu_property, None
            ),
        ]
        self._own_id = Gio.bus_own_name_on_connection(
            connection, self.name, Gio.BusNameOwnerFlags.NONE, None, None
        )
        self._watch_id = Gio.bus_watch_name_on_connection(
            connection, WATCHER_NAME, Gio.BusNameWatcherFlags.NONE, self._watcher_appeared, None
        )
        view.listeners.append(self.refresh)
        self._timer = GLib.timeout_add(POLL_MS, self._tick)

    # -- what it shows ---------------------------------------------------------

    def _line(self) -> str:
        return self.controls.message or link_caption(self.view.status)

    def _tooltip(self) -> tuple:
        return ("", [], "Corne Arcane", self._line())

    def _items(self) -> dict[int, dict[str, object]]:
        controls = self.controls
        holding = controls.holding
        return {
            LINE: {"label": self._line(), "enabled": False},
            SEPARATOR: {"type": "separator"},
            PAUSE_ITEM: {
                "label": "Resume keyboard" if holding else "Pause keyboard",
                "enabled": controls.can_resume if holding else controls.can_pause,
            },
            VIAL_ITEM: {"label": "Open Vial", "enabled": controls.can_use_keyboard},
            APP_ITEM: {"label": "Open Corne Arcane", "enabled": True},
        }

    def _variant_props(self, item_id: int, names=()) -> dict:
        Variant = self.GLib.Variant
        typed = {
            str: lambda value: Variant("s", value),
            bool: lambda value: Variant("b", value),
        }
        properties = self.items.get(item_id, {"children-display": "submenu"})
        return {
            key: typed[type(value)](value)
            for key, value in properties.items()
            if not names or key in names
        }

    def _layout(self, names=()) -> tuple:
        children = [
            self.GLib.Variant("(ia{sv}av)", (item_id, self._variant_props(item_id, names), []))
            for item_id in sorted(self.items)
        ]
        return (0, self._variant_props(0, names), children)

    def refresh(self) -> None:
        """Bring the icon, tooltip and menu up to date; signal only what changed."""
        icon, status = tray_icon(self.view.status)
        if icon != self.icon:
            self.icon = icon
            self._signal(ITEM_PATH, ITEM_INTERFACE, "NewIcon", None)
        if status != self.status:
            self.status = status
            self._signal(
                ITEM_PATH, ITEM_INTERFACE, "NewStatus", self.GLib.Variant("(s)", (status,))
            )
        tooltip = self._tooltip()
        if tooltip != self.tooltip:
            self.tooltip = tooltip
            self._signal(ITEM_PATH, ITEM_INTERFACE, "NewToolTip", None)
        items = self._items()
        if items != self.items:
            self.items = items
            self.revision += 1
            self._signal(
                MENU_PATH,
                MENU_INTERFACE,
                "LayoutUpdated",
                self.GLib.Variant("(ui)", (self.revision, 0)),
            )

    def _tick(self) -> bool:
        self.controls.poll()
        self.refresh()
        return True

    def _signal(self, path: str, interface: str, name: str, value) -> None:
        try:
            self.connection.emit_signal(None, path, interface, name, value)
        except Exception as error:
            print(f"corne-arcane-tray: {name} not sent ({type(error).__name__})", file=sys.stderr)

    # -- the watcher -----------------------------------------------------------

    def _watcher_appeared(self, _connection, _name, _owner) -> None:
        self.connection.call(
            WATCHER_NAME,
            WATCHER_PATH,
            WATCHER_INTERFACE,
            "RegisterStatusNotifierItem",
            self.GLib.Variant("(s)", (self.name,)),
            None,
            self.Gio.DBusCallFlags.NONE,
            1000,
            None,
            None,
        )

    # -- the item --------------------------------------------------------------

    def _item_property(self, _connection, _sender, _path, _interface, name):
        Variant = self.GLib.Variant
        values = {
            "Category": Variant("s", "Hardware"),
            "Id": Variant("s", "corne-arcane"),
            "Title": Variant("s", "Corne Arcane"),
            "Status": Variant("s", self.status),
            "IconName": Variant("s", self.icon),
            "ToolTip": Variant("(sa(iiay)ss)", self.tooltip),
            "ItemIsMenu": Variant("b", False),
            "Menu": Variant("o", MENU_PATH),
        }
        return values.get(name)

    def _item_call(self, _connection, _sender, _path, _interface, method, _parameters, invocation):
        if method == "Activate":
            self.open_app()
        invocation.return_value(None)

    # -- the menu --------------------------------------------------------------

    def _menu_property(self, _connection, _sender, _path, _interface, name):
        Variant = self.GLib.Variant
        values = {
            "Version": Variant("u", 3),
            "TextDirection": Variant("s", "ltr"),
            "Status": Variant("s", "normal"),
            "IconThemePath": Variant("as", []),
        }
        return values.get(name)

    def activate(self, item_id: int) -> None:
        if item_id == PAUSE_ITEM:
            if self.controls.holding:
                self.controls.resume()
            else:
                self.controls.pause()
        elif item_id == VIAL_ITEM:
            self.controls.open_vial()
        elif item_id == APP_ITEM:
            self.open_app()
        self.refresh()

    def _menu_call(self, _connection, _sender, _path, _interface, method, parameters, invocation):
        Variant = self.GLib.Variant
        if method == "GetLayout":
            _parent, _depth, names = parameters.unpack()
            invocation.return_value(Variant("(u(ia{sv}av))", (self.revision, self._layout(names))))
        elif method == "GetGroupProperties":
            ids, names = parameters.unpack()
            chosen = [i for i in (ids or sorted(self.items)) if i in self.items or i == 0]
            invocation.return_value(
                Variant("(a(ia{sv}))", ([(i, self._variant_props(i, names)) for i in chosen],))
            )
        elif method == "GetProperty":
            item_id, name = parameters.unpack()
            value = self._variant_props(item_id, (name,)).get(name)
            if value is None:
                invocation.return_dbus_error(f"{MENU_INTERFACE}.Error", f"no {name} on {item_id}")
                return
            invocation.return_value(Variant("(v)", (value,)))
        elif method == "Event":
            item_id, event, _data, _timestamp = parameters.unpack()
            if event == "clicked":
                self.activate(item_id)
            invocation.return_value(None)
        elif method == "EventGroup":
            (events,) = parameters.unpack()
            for item_id, event, _data, _timestamp in events:
                if event == "clicked":
                    self.activate(item_id)
            invocation.return_value(Variant("(ai)", ([],)))
        elif method == "AboutToShow":
            invocation.return_value(Variant("(b)", (False,)))
        elif method == "AboutToShowGroup":
            invocation.return_value(Variant("(aiai)", ([], [])))
        else:
            invocation.return_dbus_error(f"{MENU_INTERFACE}.Error", f"unknown method {method}")

    def close(self) -> None:
        if self._timer:
            self.GLib.source_remove(self._timer)
            self._timer = 0
        if self.refresh in self.view.listeners:
            self.view.listeners.remove(self.refresh)
        if self._watch_id:
            self.Gio.bus_unwatch_name(self._watch_id)
            self._watch_id = 0
        if self._own_id:
            self.Gio.bus_unown_name(self._own_id)
            self._own_id = 0
        for registration in self._registrations:
            self.connection.unregister_object(registration)
        self._registrations = []


def main(argv: list[str] | None = None) -> int:
    del argv
    try:
        import gi

        gi.require_version("Gio", "2.0")
        from gi.repository import Gio, GLib
    except (ImportError, ValueError) as error:
        print(f"corne-arcane-tray: PyGObject is required ({error})", file=sys.stderr)
        return 2
    try:
        connection = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    except GLib.Error as error:
        print(f"corne-arcane-tray: no session bus ({error.message})", file=sys.stderr)
        return 2
    view = ServiceView(Gio, GLib, connection)
    controls = Controls(view, label=TRAY_LABEL)
    app = tool_command("", "city_window")

    def open_app() -> None:
        try:
            subprocess.Popen(
                app,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                env=controls.env,
                start_new_session=True,
            )
        except OSError as error:
            controls.message = f"Could not open the app: {error}"

    tray = TrayItem(Gio, GLib, connection, view, controls, open_app)
    loop = GLib.MainLoop()
    for signum in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        GLib.unix_signal_add(GLib.PRIORITY_DEFAULT, signum, loop.quit)
    try:
        loop.run()
    finally:
        tray.close()
        controls.close()
        view.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
