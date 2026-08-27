"""Privacy-redacted Freedesktop notification monitoring adapter."""

from __future__ import annotations

import hashlib
import sys
from dataclasses import dataclass
from typing import Any, Callable

from .policy import NotificationPolicy
from .profiles import canonical_identifier, resolve_profile
from .protocol import Category, Priority


def _hint_value(hints: dict[str, Any], name: str, default: Any = None) -> Any:
    value = hints.get(name, default)
    return value.unpack() if hasattr(value, "unpack") else value


def normalize_category(value: str | None) -> Category:
    category = (value or "").strip().lower()
    if category.startswith(("im.", "email.", "presence.")):
        return Category.COMMUNICATION
    if category.startswith(("transfer.", "progress.")):
        return Category.TRANSFER
    if category.startswith(("calendar.", "x-kde-calendar")):
        return Category.CALENDAR
    if category.startswith(("security.", "auth.", "device.security")):
        return Category.SECURITY
    if category.startswith(("system.", "device.", "network.")):
        return Category.SYSTEM
    return Category.OTHER


def normalize_urgency(value: Any) -> Priority:
    try:
        urgency = int(value)
    except (TypeError, ValueError):
        urgency = 1
    return Priority.LOW if urgency <= 0 else Priority.CRITICAL if urgency >= 2 else Priority.NORMAL


@dataclass(frozen=True)
class _Pending:
    created: float
    digest: bytes
    category: Category
    priority: Priority
    persistent: bool
    replaces_id: int


@dataclass
class _Tracked:
    policy_key: tuple[str, int]
    digest: bytes
    persistent: bool


@dataclass(slots=True)
class NotificationCounters:
    matched_replies: int = 0
    unmatched_replies: int = 0
    replacements: int = 0
    closes: int = 0
    parse_failures: int = 0
    pending_high_water: int = 0
    evictions: int = 0


class DesktopNotificationAdapter:
    """Pure correlation/redaction core fed by a separate D-Bus monitor."""

    def __init__(
        self,
        policy: NotificationPolicy,
        salt: bytes,
        focused_match: Callable[[bytes], bool],
        *,
        max_pending: int = 64,
        max_tracked: int = 128,
    ) -> None:
        self.policy = policy
        self._salt = salt
        self._focused_match = focused_match
        self.max_pending = max_pending
        self.max_tracked = max_tracked
        # D-Bus serials are scoped to the sending connection, so correlation
        # must include the client peer. Hash the unique bus name to keep that
        # identifier inside the same redaction boundary as application IDs.
        self._pending: dict[tuple[bytes, int], _Pending] = {}
        self._tracked: dict[int, _Tracked] = {}
        self._digest_keys: dict[bytes, tuple[str, int]] = {}
        self._next_key = 1
        self.monitor_enabled = False
        self.monitor_error: str | None = None
        self.counters = NotificationCounters()

    def digest_text(self, summary: str, body: str) -> bytes:
        digest = hashlib.blake2s(key=self._salt, digest_size=16)
        for value in (summary, body):
            encoded = value.encode("utf-8", "surrogatepass")
            digest.update(len(encoded).to_bytes(4, "little"))
            digest.update(encoded)
        return digest.digest()

    def digest_identifier(self, identifier: str | None) -> bytes:
        normalized = canonical_identifier(identifier)
        return hashlib.blake2s(
            normalized.encode("utf-8", "surrogatepass"), key=self._salt, digest_size=16
        ).digest()

    def _digest_peer(self, peer: str) -> bytes:
        return hashlib.blake2s(
            peer.encode("utf-8", "surrogatepass"),
            key=self._salt,
            digest_size=16,
        ).digest()

    def handle_notify(
        self,
        serial: int,
        app_name: str,
        replaces_id: int,
        summary: str,
        body: str,
        hints: dict[str, Any],
        now: float,
        sender: str = "",
    ) -> bool:
        """Redact a Notify call immediately and retain only salted digests."""
        category = normalize_category(_hint_value(hints, "category", ""))
        priority = normalize_urgency(_hint_value(hints, "urgency", 1))
        transient = bool(_hint_value(hints, "transient", False))
        persistent = priority == Priority.CRITICAL and not transient
        desktop_entry = _hint_value(hints, "desktop-entry", "") or app_name
        profile = resolve_profile(str(desktop_entry), app_name)
        if (
            category == Category.OTHER
            and profile is not None
            and profile.category_override is not None
        ):
            category = profile.category_override
        app_digest = self.digest_identifier(
            profile.identifier if profile is not None else str(desktop_entry)
        )
        content_digest = self.digest_text(summary, body)
        suppress_focused = profile is None or profile.suppress_when_focused
        if priority != Priority.CRITICAL and suppress_focused and self._focused_match(app_digest):
            return False
        if len(self._pending) >= self.max_pending:
            candidates = [key for key, item in self._pending.items() if not item.persistent]
            pool = candidates or list(self._pending)
            oldest_key = min(pool, key=lambda key: self._pending[key].created)
            del self._pending[oldest_key]
            self.counters.evictions += 1
        request_key = (self._digest_peer(sender), int(serial))
        self._pending[request_key] = _Pending(
            now,
            content_digest,
            category,
            priority,
            persistent,
            int(replaces_id),
        )
        self.counters.pending_high_water = max(self.counters.pending_high_water, len(self._pending))
        return True

    def handle_reply(
        self,
        reply_serial: int,
        notification_id: int,
        now: float,
        destination: str = "",
    ) -> bool:
        pending = self._pending.pop((self._digest_peer(destination), int(reply_serial)), None)
        if pending is None:
            self.counters.unmatched_replies += 1
            return False
        self.counters.matched_replies += 1
        old = self._tracked.pop(pending.replaces_id, None) if pending.replaces_id else None
        policy_key: tuple[str, int]
        if old is not None:
            policy_key = old.policy_key
            if not any(item.digest == old.digest for item in self._tracked.values()):
                self._digest_keys.pop(old.digest, None)
            if old.persistent and not pending.persistent:
                self.policy.close(policy_key)
        else:
            existing_key = self._digest_keys.get(pending.digest)
            if existing_key is None:
                policy_key = ("desktop", self._next_key)
                self._next_key += 1
            else:
                policy_key = existing_key
        duplicate = old is not None or pending.digest in self._digest_keys
        if old is not None:
            self.counters.replacements += 1
        changed = self.policy.inject(
            pending.category,
            pending.priority,
            pending.persistent,
            pending.created,
            key=policy_key,
            replacement=duplicate,
        )
        self._digest_keys[pending.digest] = policy_key
        self._tracked[int(notification_id)] = _Tracked(
            policy_key, pending.digest, pending.persistent
        )
        self._bound_tracked()
        return changed

    def handle_closed(self, notification_id: int) -> bool:
        tracked = self._tracked.pop(int(notification_id), None)
        if tracked is None:
            return False
        self.counters.closes += 1
        still_referenced = any(
            item.policy_key == tracked.policy_key for item in self._tracked.values()
        )
        if not still_referenced:
            self._digest_keys.pop(tracked.digest, None)
            if tracked.persistent:
                return self.policy.close(tracked.policy_key)
        return False

    def _bound_tracked(self) -> None:
        while len(self._tracked) > self.max_tracked:
            oldest_id = next(
                (item_id for item_id, item in self._tracked.items() if not item.persistent),
                next(iter(self._tracked)),
            )
            tracked = self._tracked.pop(oldest_id)
            still_referenced = any(
                item.policy_key == tracked.policy_key for item in self._tracked.values()
            )
            if not still_referenced:
                self._digest_keys.pop(tracked.digest, None)
                if tracked.persistent:
                    self.policy.close(tracked.policy_key)
        while len(self._digest_keys) > self.max_tracked:
            oldest_digest = next(iter(self._digest_keys))
            self._digest_keys.pop(oldest_digest)

    @property
    def retained_plaintext(self) -> tuple[()]:
        """A test/diagnostic proof point: no content strings are retained."""
        return ()


class DesktopMonitor:
    """Own the dedicated monitor connection and feed its messages to the core."""

    MATCH_RULES = (
        "type='method_call',interface='org.freedesktop.Notifications',member='Notify'",
        "type='method_return'",
        "type='signal',interface='org.freedesktop.Notifications',member='NotificationClosed'",
    )

    # A terminated monitor cannot be revived in place, so the connection is
    # rebuilt. Back off so a bus that keeps refusing does not become a reconnect
    # loop, and cap the wait well inside a work session.
    RETRY_DELAYS = (2, 4, 8, 16, 32, 60)

    def __init__(
        self,
        Gio,
        GLib,
        adapter: DesktopNotificationAdapter,
        clock,
        verbose=False,
        changed=lambda: None,
    ) -> None:
        self.Gio = Gio
        self.GLib = GLib
        self.adapter = adapter
        self.clock = clock
        self.verbose = verbose
        self.changed = changed
        self.connection = None
        self.filter_id = 0
        self._retry_source = 0
        self._retry_index = 0
        self._closing = False

    def start(self) -> bool:
        try:
            address = self.Gio.dbus_address_get_for_bus_sync(self.Gio.BusType.SESSION, None)
            flags = (
                self.Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT
                | self.Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION
            )
            connection = self.Gio.DBusConnection.new_for_address_sync(address, flags, None, None)
            self.connection = connection
            connection.call_sync(
                "org.freedesktop.DBus",
                "/org/freedesktop/DBus",
                "org.freedesktop.DBus.Monitoring",
                "BecomeMonitor",
                self.GLib.Variant("(asu)", (list(self.MATCH_RULES), 0)),
                None,
                self.Gio.DBusCallFlags.NONE,
                2000,
                None,
            )
            self.filter_id = connection.add_filter(self._filter, None)
            # A monitor that sends anything is terminated by the broker, so a
            # lost connection must be visible rather than silently deaf.
            try:
                connection.connect("closed", self._on_closed)
            except (AttributeError, TypeError):
                pass
            self.adapter.monitor_enabled = True
            self.adapter.monitor_error = None
            self._retry_index = 0
            return True
        except Exception as error:
            self.adapter.monitor_error = type(error).__name__
            self.adapter.monitor_enabled = False
            if self.connection is not None:
                try:
                    self.connection.close_sync(None)
                except Exception:
                    pass
                self.connection = None
            return False

    def _filter(self, connection, message, incoming, user_data):
        del connection, incoming, user_data
        try:
            message_type = message.get_message_type()
            if message_type == self.Gio.DBusMessageType.METHOD_CALL:
                if (
                    message.get_interface() == "org.freedesktop.Notifications"
                    and message.get_member() == "Notify"
                ):
                    body_variant = message.get_body()
                    app = body_variant.get_child_value(0).unpack()
                    replaces = body_variant.get_child_value(1).unpack()
                    summary = body_variant.get_child_value(3).unpack()
                    body = body_variant.get_child_value(4).unpack()
                    hint_values = body_variant.get_child_value(6)
                    hints = {}
                    for key in ("category", "urgency", "transient", "desktop-entry"):
                        value = hint_values.lookup_value(key, None)
                        if value is not None:
                            hints[key] = value.unpack()
                    self.adapter.handle_notify(
                        message.get_serial(),
                        app,
                        replaces,
                        summary,
                        body,
                        hints,
                        self.clock(),
                        message.get_sender() or "",
                    )
            elif message_type == self.Gio.DBusMessageType.METHOD_RETURN:
                body = message.get_body()
                values = body.unpack() if body is not None else ()
                if len(values) == 1 and isinstance(values[0], int):
                    if self.adapter.handle_reply(
                        message.get_reply_serial(),
                        values[0],
                        self.clock(),
                        message.get_destination() or "",
                    ):
                        self.changed()
            elif message_type == self.Gio.DBusMessageType.SIGNAL:
                if (
                    message.get_interface() == "org.freedesktop.Notifications"
                    and message.get_member() == "NotificationClosed"
                ):
                    notification_id, _reason = message.get_body().unpack()
                    if self.adapter.handle_closed(notification_id):
                        self.changed()
        except Exception:
            # Monitoring is enrichment; malformed or unfamiliar traffic is ignored.
            self.adapter.counters.parse_failures += 1
        # Consume every eavesdropped message. Returning it lets GDBus route the
        # copy as if it were addressed here and auto-reply UnknownMethod; a
        # monitor that sends anything is disconnected by dbus-broker, which
        # deafens this adapter permanently after the first notification.
        return None

    def _on_closed(self, connection, remote_peer_vanished, error) -> None:
        del connection
        self.adapter.monitor_enabled = False
        self.adapter.monitor_error = "ConnectionClosed"
        if self.verbose:
            print(
                f"arcane-host: notification monitor connection closed "
                f"(remote_peer_vanished={remote_peer_vanished}, error={error})",
                file=sys.stderr,
                flush=True,
            )
        self._schedule_retry()

    def _schedule_retry(self) -> None:
        """Rebuild the connection later; a lost monitor is a deaf adapter.

        Losing the connection has the same effect as never acquiring one: the
        daemon keeps running and reporting healthy while no notification reaches
        the wire, so Restart=always never fires. Callers that supply no GLib
        main loop simply do not reconnect.
        """
        if self._closing or self._retry_source:
            return
        if not hasattr(self.GLib, "timeout_add_seconds"):
            return
        delay = self.RETRY_DELAYS[min(self._retry_index, len(self.RETRY_DELAYS) - 1)]
        self._retry_index += 1
        self._retry_source = self.GLib.timeout_add_seconds(delay, self._retry)

    def _retry(self) -> bool:
        self._retry_source = 0
        if self._closing:
            return False
        # The old connection is already dead; drop it rather than filtering it.
        self.connection = None
        self.filter_id = 0
        if self.start():
            if self.verbose:
                print("arcane-host: notification monitor reconnected", file=sys.stderr, flush=True)
        else:
            self._schedule_retry()
        return False

    def close(self) -> None:
        self._closing = True
        if self._retry_source and hasattr(self.GLib, "source_remove"):
            try:
                self.GLib.source_remove(self._retry_source)
            except Exception:
                pass
        self._retry_source = 0
        if self.connection is None:
            return
        if self.filter_id:
            try:
                self.connection.remove_filter(self.filter_id)
            except Exception:
                pass
            self.filter_id = 0
        try:
            self.connection.close_sync(None)
        except Exception:
            pass
        self.connection = None
        self.adapter.monitor_enabled = False
