"""Corne Arcane D-Bus service implementations."""

from __future__ import annotations

import sys
import time
from pathlib import Path
from typing import Callable

from .adapters import SemanticAdapters
from .dbus_contract import (
    CLEAR_NOTIFICATIONS,
    EVENTS_INTERFACE,
    EVENTS_XML,
    FOCUS_INTERFACE,
    FOCUS_XML,
    INJECT_SYNTHETIC,
    KWIN_SERVICE,
    OBJECT_PATH,
    REPORT_ACTIVE_WINDOW,
    REPORT_BROWSER_ACTIVITY,
    REPORT_REPOSITORY_STATE,
    REPORT_TERMINAL_COMPLETION,
    RepositoryState,
)
from .focus import FocusArbiter
from .policy import NotificationPolicy
from .protocol import Category, Intensity, Priority, Secondary


class FocusService:
    def __init__(
        self,
        Gio,
        connection,
        arbiter: FocusArbiter,
        clock=time.monotonic,
        changed: Callable[[], None] = lambda: None,
    ) -> None:
        self.connection = connection
        self.arbiter = arbiter
        self.clock = clock
        self.changed = changed
        info = Gio.DBusNodeInfo.new_for_xml(FOCUS_XML)
        self.registration_id = connection.register_object(
            OBJECT_PATH, info.interfaces[0], self._method_call, None, None
        )

    def _method_call(
        self, connection, sender, path, interface, method, parameters, invocation
    ) -> None:
        del connection, sender, path, interface
        if method == REPORT_ACTIVE_WINDOW:
            resource_class, desktop_file_name = parameters.unpack()
            self.arbiter.report(resource_class, desktop_file_name, self.clock())
            self.changed()
            invocation.return_value(None)
            return
        invocation.return_dbus_error(f"{FOCUS_INTERFACE}.UnknownMethod", method)

    def close(self) -> None:
        if self.registration_id:
            self.connection.unregister_object(self.registration_id)
            self.registration_id = 0


class EventService:
    """Private diagnostic and redacted terminal-completion ingress."""

    def __init__(
        self,
        Gio,
        connection,
        policy: NotificationPolicy,
        focus: FocusArbiter,
        adapters: SemanticAdapters,
        changed: Callable[[], None],
        clock=time.monotonic,
    ) -> None:
        self.connection = connection
        self.policy = policy
        self.focus = focus
        self.adapters = adapters
        self.changed = changed
        self.clock = clock
        info = Gio.DBusNodeInfo.new_for_xml(EVENTS_XML)
        self.registration_id = connection.register_object(
            OBJECT_PATH, info.interfaces[0], self._method_call, None, None
        )

    def report_terminal_completion(self, duration_ms: int, exit_status: int) -> bool:
        if duration_ms < 10_000 or self.focus.terminal_focused:
            return False
        priority = Priority.LOW if exit_status == 0 else Priority.NORMAL
        changed = self.policy.inject(Category.TERMINAL, priority, False, self.clock())
        if changed:
            self.changed()
        return changed

    def inject_synthetic(self, category: Category, priority: Priority, persistent: bool) -> bool:
        changed = self.policy.inject(category, priority, persistent, self.clock())
        if changed:
            self.changed()
        return changed

    def report_repository_state(self, state: RepositoryState, success: bool) -> bool:
        return self.adapters.repository(state, success)

    def report_browser_activity(self, kind: Secondary, intensity: Intensity) -> None:
        self.adapters.browser(kind, intensity)

    def clear(self) -> None:
        self.policy.clear()
        self.changed()

    def _method_call(
        self, connection, sender, path, interface, method, parameters, invocation
    ) -> None:
        del connection, sender, path, interface
        if method == REPORT_TERMINAL_COMPLETION:
            self.report_terminal_completion(*parameters.unpack())
            invocation.return_value(None)
            return
        if method == REPORT_REPOSITORY_STATE:
            state_value, success = parameters.unpack()
            try:
                state = RepositoryState(state_value)
            except (ValueError, TypeError):
                invocation.return_dbus_error(
                    f"{EVENTS_INTERFACE}.InvalidArguments", "invalid repository state"
                )
            else:
                self.report_repository_state(state, bool(success))
                invocation.return_value(None)
            return
        if method == REPORT_BROWSER_ACTIVITY:
            kind_value, intensity_value = parameters.unpack()
            try:
                kind = Secondary(kind_value)
                intensity = Intensity(intensity_value)
                if kind not in {Secondary.SCROLL, Secondary.TAB, Secondary.PAGE}:
                    raise ValueError
            except (ValueError, TypeError):
                invocation.return_dbus_error(
                    f"{EVENTS_INTERFACE}.InvalidArguments", "invalid browser activity"
                )
            else:
                self.report_browser_activity(kind, intensity)
                invocation.return_value(None)
            return
        if method == INJECT_SYNTHETIC:
            category_value, priority_value, persistent = parameters.unpack()
            try:
                category = Category(category_value)
                priority = Priority(priority_value)
                if category == Category.NONE or priority == Priority.NONE:
                    raise ValueError
                if persistent and priority != Priority.CRITICAL:
                    raise ValueError
            except (ValueError, TypeError):
                invocation.return_dbus_error(
                    f"{EVENTS_INTERFACE}.InvalidArguments", "invalid notification fields"
                )
            else:
                self.inject_synthetic(category, priority, bool(persistent))
                invocation.return_value(None)
            return
        if method == CLEAR_NOTIFICATIONS:
            self.clear()
            invocation.return_value(None)
            return
        invocation.return_dbus_error(f"{EVENTS_INTERFACE}.UnknownMethod", method)

    def close(self) -> None:
        if self.registration_id:
            self.connection.unregister_object(self.registration_id)
            self.registration_id = 0


class KWinBridgeLoader:
    """Reload the packaged script whenever KWin acquires its D-Bus name."""

    def __init__(self, Gio, GLib, connection, script_path: Path, verbose: bool = False) -> None:
        self.Gio = Gio
        self.GLib = GLib
        self.connection = connection
        self.script_path = script_path
        self.verbose = verbose
        self.subscription_id = connection.signal_subscribe(
            "org.freedesktop.DBus",
            "org.freedesktop.DBus",
            "NameOwnerChanged",
            "/org/freedesktop/DBus",
            KWIN_SERVICE,
            Gio.DBusSignalFlags.NONE,
            self._owner_changed,
        )
        self.load()

    def _owner_changed(self, connection, sender, path, interface, signal, parameters) -> None:
        del connection, sender, path, interface, signal
        name, _old_owner, new_owner = parameters.unpack()
        if name == KWIN_SERVICE and new_owner:
            self.load()

    def load(self) -> None:
        if not self.script_path.is_file():
            if self.verbose:
                print(f"arcane-host: KWin bridge missing: {self.script_path}", file=sys.stderr)
            return
        try:
            self.connection.call_sync(
                KWIN_SERVICE,
                "/Scripting",
                "org.kde.kwin.Scripting",
                "unloadScript",
                self.GLib.Variant("(s)", ("cornearcane",)),
                None,
                self.Gio.DBusCallFlags.NONE,
                1000,
                None,
            )
        except Exception:
            pass
        try:
            result = self.connection.call_sync(
                KWIN_SERVICE,
                "/Scripting",
                "org.kde.kwin.Scripting",
                "loadScript",
                self.GLib.Variant("(ss)", (str(self.script_path), "cornearcane")),
                self.GLib.VariantType.new("(i)"),
                self.Gio.DBusCallFlags.NONE,
                1000,
                None,
            )
            script_id = result.unpack()[0]
            if script_id >= 0:
                self.connection.call_sync(
                    KWIN_SERVICE,
                    f"/Scripting/Script{script_id}",
                    "org.kde.kwin.Script",
                    "run",
                    None,
                    None,
                    self.Gio.DBusCallFlags.NONE,
                    1000,
                    None,
                )
                if self.verbose:
                    print("arcane-host: KWin focus bridge loaded", flush=True)
        except Exception as error:
            if self.verbose:
                print(
                    f"arcane-host: KWin bridge not ready ({error})",
                    file=sys.stderr,
                    flush=True,
                )

    def close(self) -> None:
        if self.subscription_id:
            self.connection.signal_unsubscribe(self.subscription_id)
            self.subscription_id = 0
