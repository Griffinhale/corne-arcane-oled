"""Fail-soft D-Bus monitors for privacy-bounded semantic adapters."""

from __future__ import annotations

from typing import Any

from .adapters import SemanticAdapters


class DBusAdapterHub:
    """Subscribe to standard property signals; any missing service is isolated."""

    def __init__(self, Gio, session, system, adapters: SemanticAdapters, timer_unit: str | None = None):
        self.Gio = Gio
        self.adapters = adapters
        self.timer_unit = timer_unit
        self.subscriptions: list[tuple[Any, int]] = []
        self._players: dict[str, bool] = {}
        self._pomodoro_active = False
        self._pomodoro_deadline: float | None = None
        self._connectivity = "offline"
        self._vpn_paths: set[str] = set()
        self._subscribe(session, self._session_properties)
        self._subscribe_name_owners(session)
        if system is not None:
            self._subscribe(system, self._system_properties)
            self._subscribe_name_owners(system, self._system_name_owner_changed)
        if hasattr(Gio, "DBusProxy"):
            self._prime(session, system)

    def _subscribe(self, connection, callback) -> None:
        try:
            subscription = connection.signal_subscribe(
                None,
                "org.freedesktop.DBus.Properties",
                "PropertiesChanged",
                None,
                None,
                self.Gio.DBusSignalFlags.NONE,
                callback,
            )
            self.subscriptions.append((connection, subscription))
        except Exception:
            self.adapters.counters.errors += 1

    def _subscribe_name_owners(self, connection, callback=None) -> None:
        try:
            subscription = connection.signal_subscribe(
                "org.freedesktop.DBus",
                "org.freedesktop.DBus",
                "NameOwnerChanged",
                "/org/freedesktop/DBus",
                None,
                self.Gio.DBusSignalFlags.NONE,
                callback or self._name_owner_changed,
            )
            self.subscriptions.append((connection, subscription))
        except Exception:
            self.adapters.counters.errors += 1

    @staticmethod
    def _lookup(changed, key: str):
        try:
            value = changed.get(key) if isinstance(changed, dict) else changed.lookup_value(key, None)
            if value is None:
                return None
            if hasattr(value, "unpack"):
                value = value.unpack()
            while hasattr(value, "unpack"):
                value = value.unpack()
            return value
        except Exception:
            return None

    @staticmethod
    def _unpack(value):
        try:
            while hasattr(value, "unpack"):
                value = value.unpack()
            return value
        except Exception:
            return None

    def _proxy(self, connection, name: str, path: str, interface: str):
        return self.Gio.DBusProxy.new_sync(
            connection,
            self.Gio.DBusProxyFlags.DO_NOT_AUTO_START,
            None,
            name,
            path,
            interface,
            None,
        )

    def _prime(self, session, system) -> None:
        """Read current properties once; subsequent work is signal/deadline driven."""
        try:
            bus = self._proxy(
                session,
                "org.freedesktop.DBus",
                "/org/freedesktop/DBus",
                "org.freedesktop.DBus",
            )
            reply = bus.call_sync(
                "ListNames", None, self.Gio.DBusCallFlags.NONE, 1000, None
            )
            names = self._unpack(reply)
            names = names[0] if names else ()
            for name in names:
                if not str(name).startswith("org.mpris.MediaPlayer2."):
                    continue
                self._prime_player(session, str(name))
        except Exception:
            self.adapters.counters.errors += 1

        try:
            self._prime_notifications(session)
        except Exception:
            self.adapters.counters.errors += 1

        if self.timer_unit:
            path = f"/org/freedesktop/systemd1/unit/{self._unit_path_fragment(self.timer_unit)}"
            try:
                unit = self._proxy(
                    session, "org.freedesktop.systemd1", path,
                    "org.freedesktop.systemd1.Unit"
                )
                timer = self._proxy(
                    session, "org.freedesktop.systemd1", path,
                    "org.freedesktop.systemd1.Timer"
                )
                active_state = self._unpack(unit.get_cached_property("ActiveState"))
                next_elapse = self._unpack(
                    timer.get_cached_property("NextElapseUSecMonotonic")
                )
                self._pomodoro_active = active_state == "active"
                if next_elapse is not None:
                    self._pomodoro_deadline = float(next_elapse) / 1_000_000
                remaining = (
                    max(0.0, self._pomodoro_deadline - self.adapters.clock())
                    if self._pomodoro_deadline is not None else None
                )
                self.adapters.pomodoro(
                    self._pomodoro_active, remaining, active_state == "failed"
                )
            except Exception:
                self.adapters.counters.errors += 1

        if system is not None:
            try:
                self._prime_network(system)
            except Exception:
                self.adapters.counters.errors += 1

    def _prime_player(self, session, name: str) -> None:
        try:
            player = self._proxy(
                session, name, "/org/mpris/MediaPlayer2",
                "org.mpris.MediaPlayer2.Player"
            )
            owner = player.get_name_owner() or name
            status = self._unpack(player.get_cached_property("PlaybackStatus"))
            metadata = self._unpack(player.get_cached_property("Metadata")) or {}
            track_id = self._lookup(metadata, "mpris:trackid")
            self._players[str(owner)] = str(status).lower() == "playing"
            self.adapters.media(
                "Playing" if any(self._players.values()) else "Paused",
                str(track_id) if track_id else None,
            )
        except Exception:
            self.adapters.counters.errors += 1

    def _prime_notifications(self, session) -> None:
        notifications = self._proxy(
            session,
            "org.freedesktop.Notifications",
            "/org/freedesktop/Notifications",
            "org.freedesktop.Notifications",
        )
        inhibited = self._unpack(notifications.get_cached_property("Inhibited"))
        if inhibited is not None:
            self.adapters.dnd(bool(inhibited))

    def _refresh_vpn(self, system, paths) -> None:
        active_vpns: set[str] = set()
        for path in paths or ():
            try:
                active = self._proxy(
                    system,
                    "org.freedesktop.NetworkManager",
                    str(path),
                    "org.freedesktop.NetworkManager.Connection.Active",
                )
                if self._unpack(active.get_cached_property("Vpn")):
                    active_vpns.add(str(path))
            except Exception:
                self.adapters.counters.errors += 1
        self._vpn_paths = active_vpns

    def _prime_network(self, system) -> None:
        network = self._proxy(
            system,
            "org.freedesktop.NetworkManager",
            "/org/freedesktop/NetworkManager",
            "org.freedesktop.NetworkManager",
        )
        connectivity = self._unpack(network.get_cached_property("Connectivity"))
        names = {0: "offline", 1: "offline", 2: "limited", 3: "limited", 4: "online"}
        self._connectivity = names.get(int(connectivity), "offline")
        self._refresh_vpn(
            system, self._unpack(network.get_cached_property("ActiveConnections"))
        )
        self.adapters.network(self._connectivity, bool(self._vpn_paths))

    @staticmethod
    def _unit_path_fragment(unit: str) -> str:
        return "".join(
            character if character.isalnum() else f"_{ord(character):02x}"
            for character in unit
        )

    def _name_owner_changed(self, connection, sender, path, interface, signal, parameters) -> None:
        del sender, path, interface, signal
        try:
            name, old_owner, new_owner = parameters.unpack()
            if str(name).startswith("org.mpris.MediaPlayer2."):
                if old_owner:
                    self._players.pop(str(old_owner), None)
                    self._players.pop(str(name), None)
                if new_owner:
                    self._prime_player(connection, str(name))
                else:
                    self.adapters.media("Playing" if any(self._players.values()) else "Paused")
            elif name == "org.freedesktop.Notifications" and new_owner:
                self._prime_notifications(connection)
        except Exception:
            self.adapters.counters.errors += 1

    def _system_name_owner_changed(
        self, connection, sender, path, interface, signal, parameters
    ) -> None:
        del sender, path, interface, signal
        try:
            name, _old_owner, new_owner = parameters.unpack()
            if name == "org.freedesktop.NetworkManager" and new_owner:
                self._prime_network(connection)
        except Exception:
            self.adapters.counters.errors += 1

    def _session_properties(self, connection, sender, path, interface, signal, parameters) -> None:
        del connection, interface, signal
        try:
            changed_interface = parameters.get_child_value(0).unpack()
            changed = parameters.get_child_value(1)
            if changed_interface == "org.mpris.MediaPlayer2.Player":
                status = self._lookup(changed, "PlaybackStatus")
                metadata = self._lookup(changed, "Metadata")
                track_id = self._lookup(metadata, "mpris:trackid") if metadata is not None else None
                player = str(sender or path)
                if status is not None:
                    self._players[player] = str(status).lower() == "playing"
                if status is not None or track_id is not None:
                    self.adapters.media(
                        "Playing" if any(self._players.values()) else "Paused",
                        str(track_id) if track_id else None,
                    )
            elif changed_interface == "org.freedesktop.Notifications":
                inhibited = self._lookup(changed, "Inhibited")
                if inhibited is not None:
                    self.adapters.dnd(bool(inhibited))
            elif self.timer_unit and changed_interface in {
                "org.freedesktop.systemd1.Timer", "org.freedesktop.systemd1.Unit"
            } and self._unit_path_fragment(self.timer_unit) in path:
                active_state = self._lookup(changed, "ActiveState")
                next_elapse = self._lookup(changed, "NextElapseUSecMonotonic")
                if active_state is not None:
                    self._pomodoro_active = active_state == "active"
                if next_elapse is not None:
                    self._pomodoro_deadline = float(next_elapse) / 1_000_000
                remaining = (
                    max(0.0, self._pomodoro_deadline - self.adapters.clock())
                    if self._pomodoro_deadline is not None
                    else None
                )
                self.adapters.pomodoro(
                    self._pomodoro_active,
                    remaining,
                    active_state == "failed",
                )
        except Exception:
            self.adapters.counters.errors += 1

    def _system_properties(self, connection, sender, path, interface, signal, parameters) -> None:
        del sender, interface, signal
        try:
            changed_interface = parameters.get_child_value(0).unpack()
            changed = parameters.get_child_value(1)
            if changed_interface == "org.freedesktop.NetworkManager":
                connectivity = self._lookup(changed, "Connectivity")
                if connectivity is not None:
                    names = {0: "offline", 1: "offline", 2: "limited", 3: "limited", 4: "online"}
                    self._connectivity = names.get(int(connectivity), "offline")
                active_connections = self._lookup(changed, "ActiveConnections")
                if active_connections is not None:
                    self._refresh_vpn(connection, active_connections)
                if connectivity is not None or active_connections is not None:
                    self.adapters.network(self._connectivity, bool(self._vpn_paths))
            elif changed_interface == "org.freedesktop.NetworkManager.Connection.Active":
                vpn = self._lookup(changed, "Vpn")
                if vpn is not None:
                    if vpn:
                        self._vpn_paths.add(path)
                    else:
                        self._vpn_paths.discard(path)
                    self.adapters.network(self._connectivity, bool(self._vpn_paths))
        except Exception:
            self.adapters.counters.errors += 1

    def close(self) -> None:
        for connection, subscription in self.subscriptions:
            try:
                connection.signal_unsubscribe(subscription)
            except Exception:
                pass
        self.subscriptions.clear()
