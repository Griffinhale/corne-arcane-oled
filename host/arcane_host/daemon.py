"""Application-aware Corne Arcane semantic heartbeat daemon."""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import secrets
import sys
import time
from typing import Callable

from .desktop import DesktopMonitor, DesktopNotificationAdapter
from .focus import FocusArbiter
from .hidraw import Device, choose_device
from .policy import NotificationPolicy
from .protocol import (
    Category,
    EMPTY_SUMMARY,
    Message,
    NotificationSummary,
    Priority,
    Scene,
    build_packet,
)

SCENES = {scene.name.lower(): scene for scene in Scene}
BUS_NAME = "io.github.Griffinhale.CorneArcane"
OBJECT_PATH = "/io/github/Griffinhale/CorneArcane"
FOCUS_INTERFACE = "io.github.Griffinhale.CorneArcane.Focus"
EVENTS_INTERFACE = "io.github.Griffinhale.CorneArcane.Events"
KWIN_SERVICE = "org.kde.KWin"


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", help="explicit /dev/hidrawN path")
    parser.add_argument(
        "--scene",
        choices=SCENES,
        default=None,
        help="diagnostic fixed-scene override; disables focus arbitration",
    )
    parser.add_argument("--notify", type=int, default=0, metavar="COUNT")
    parser.add_argument(
        "--no-desktop-notifications",
        action="store_true",
        help="disable privacy-redacted Freedesktop notification monitoring",
    )
    parser.add_argument("--interval", type=float, default=0.5, metavar="SECONDS")
    parser.add_argument("--retry-interval", type=float, default=2.0, metavar="SECONDS")
    parser.add_argument("--once", action="store_true", help="send one heartbeat after HELLO and exit")
    parser.add_argument("--dry-run", action="store_true", help="print reports instead of opening hidraw")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--session", type=lambda value: int(value, 0), help=argparse.SUPPRESS)
    parser.add_argument("--kwin-script", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    if not 0 <= args.notify <= 15:
        parser.error("--notify must be in 0..15")
    if not 0.1 <= args.interval < 1.5:
        parser.error("--interval must be at least 0.1 and below the 1.5 s firmware timeout")
    if args.retry_interval <= 0:
        parser.error("--retry-interval must be positive")
    return args


class HidHeartbeat:
    """Reconnectable HELLO/heartbeat state machine with injectable I/O."""

    def __init__(
        self,
        scene_provider: Callable[[], Scene],
        device_factory: Callable[[], object],
        session_factory: Callable[[], int],
        *,
        summary_provider: Callable[[], NotificationSummary] | None = None,
        notification_count: int = 0,
        interval: float = 0.5,
        retry_interval: float = 2.0,
        verbose: bool = False,
    ) -> None:
        self.scene_provider = scene_provider
        self.device_factory = device_factory
        self.session_factory = session_factory
        self.summary_provider = summary_provider or (
            lambda: EMPTY_SUMMARY
            if notification_count == 0
            else NotificationSummary(notification_count, Category.OTHER, Priority.NORMAL)
        )
        self.interval = interval
        self.retry_interval = retry_interval
        self.verbose = verbose
        self.device: object | None = None
        self.session = 0
        self.sequence = 0
        self.next_connect = 0.0
        self.next_heartbeat = 0.0
        self.heartbeats = 0
        self.notifications = 0
        self.notify_pending = False

    def _log(self, message: str) -> None:
        if self.verbose:
            print(f"arcane-host: {message}", flush=True)

    def _disconnect(self, now: float, error: BaseException | None = None) -> None:
        if self.device is not None:
            try:
                self.device.close()  # type: ignore[attr-defined]
            except OSError:
                pass
        self.device = None
        self.next_connect = now + self.retry_interval
        if error is not None:
            self._log(f"HID disconnected ({error}); retrying in {self.retry_interval:g}s")

    def _connect(self, now: float) -> None:
        try:
            device = self.device_factory()
            session = self.session_factory() & 0xFFFFFFFF
            candidate = session or 1
            if candidate == self.session:
                candidate = ((candidate + 1) & 0xFFFFFFFF) or 1
            self.session = candidate
            self.sequence = 0
            device.send(  # type: ignore[attr-defined]
                build_packet(
                    Message.HELLO,
                    self.session,
                    0,
                    self.scene_provider(),
                    summary=self.summary_provider(),
                )
            )
        except (OSError, RuntimeError) as error:
            try:
                device.close()  # type: ignore[possibly-undefined, attr-defined]
            except (NameError, OSError):
                pass
            self.device = None
            self.next_connect = now + self.retry_interval
            self._log(f"HID unavailable ({error}); retrying in {self.retry_interval:g}s")
            return
        self.device = device
        self.next_heartbeat = now + 0.1
        self.notify_pending = False
        self._log(f"connected session=0x{self.session:08x}")

    def tick(self, now: float) -> bool:
        """Advance I/O. Return True exactly when a heartbeat was sent."""
        if self.device is None:
            if now >= self.next_connect:
                self._connect(now)
            return False
        heartbeat_due = now >= self.next_heartbeat
        if not heartbeat_due and not self.notify_pending:
            return False
        self.sequence = (self.sequence + 1) & 0xFFFF
        message = Message.HEARTBEAT if heartbeat_due else Message.NOTIFY
        summary = self.summary_provider()
        try:
            self.device.send(  # type: ignore[attr-defined]
                build_packet(
                    message,
                    self.session,
                    self.sequence,
                    self.scene_provider(),
                    summary=summary,
                )
            )
        except OSError as error:
            self._disconnect(now, error)
            return False
        if message == Message.NOTIFY:
            self.notify_pending = False
            self.notifications += 1
            self._log(
                f"notify seq={self.sequence} category={summary.category.name.lower()} "
                f"priority={summary.priority.name.lower()} count={summary.count}"
            )
            return False
        self.notify_pending = False
        self.next_heartbeat = now + self.interval
        self.heartbeats += 1
        self._log(
            f"heartbeat seq={self.sequence} scene={self.scene_provider().name.lower()} "
            f"notify={summary.count}"
        )
        return True

    def request_heartbeat(self, now: float) -> None:
        """Make a settled semantic change visible without waiting 500 ms."""
        if self.device is not None:
            self.next_heartbeat = min(self.next_heartbeat, now)

    def request_notify(self) -> None:
        """Coalesce an absolute semantic update behind any due heartbeat."""
        if self.device is not None:
            self.notify_pending = True

    def close(self) -> None:
        self._disconnect(time.monotonic())


INTROSPECTION_XML = f"""
<node>
  <interface name='{FOCUS_INTERFACE}'>
    <method name='ReportActiveWindow'>
      <arg type='s' name='resourceClass' direction='in'/>
      <arg type='s' name='desktopFileName' direction='in'/>
    </method>
  </interface>
</node>
"""


class FocusService:
    def __init__(self, Gio, connection, arbiter: FocusArbiter, clock=time.monotonic) -> None:
        self.Gio = Gio
        self.connection = connection
        self.arbiter = arbiter
        self.clock = clock
        info = Gio.DBusNodeInfo.new_for_xml(INTROSPECTION_XML)
        self.registration_id = connection.register_object(
            OBJECT_PATH, info.interfaces[0], self._method_call, None, None
        )

    def _method_call(self, connection, sender, path, interface, method, parameters, invocation) -> None:
        del connection, sender, path, interface
        if method == "ReportActiveWindow":
            resource_class, desktop_file_name = parameters.unpack()
            self.arbiter.report(resource_class, desktop_file_name, self.clock())
            invocation.return_value(None)
            return
        invocation.return_dbus_error(f"{FOCUS_INTERFACE}.UnknownMethod", method)


EVENTS_XML = f"""
<node>
  <interface name='{EVENTS_INTERFACE}'>
    <method name='ReportTerminalCompletion'>
      <arg type='u' name='durationMilliseconds' direction='in'/>
      <arg type='i' name='exitStatus' direction='in'/>
    </method>
    <method name='InjectSynthetic'>
      <arg type='y' name='category' direction='in'/>
      <arg type='y' name='priority' direction='in'/>
      <arg type='b' name='persistent' direction='in'/>
    </method>
    <method name='ClearNotifications'/>
  </interface>
</node>
"""


class EventService:
    """Private diagnostic and redacted terminal-completion ingress."""

    def __init__(
        self,
        Gio,
        connection,
        policy: NotificationPolicy,
        focus: FocusArbiter,
        changed: Callable[[], None],
        clock=time.monotonic,
    ) -> None:
        self.policy = policy
        self.focus = focus
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
        changed = self.policy.inject(
            Category.TERMINAL,
            priority,
            False,
            self.clock(),
        )
        if changed:
            self.changed()
        return changed

    def inject_synthetic(self, category: int, priority: int, persistent: bool) -> bool:
        try:
            changed = self.policy.inject(
                Category(category), Priority(priority), bool(persistent), self.clock()
            )
        except (ValueError, TypeError):
            return False
        if changed:
            self.changed()
        return changed

    def clear(self) -> None:
        self.policy.clear()
        self.changed()

    def _method_call(self, connection, sender, path, interface, method, parameters, invocation) -> None:
        del connection, sender, path, interface
        if method == "ReportTerminalCompletion":
            self.report_terminal_completion(*parameters.unpack())
            invocation.return_value(None)
            return
        if method == "InjectSynthetic":
            category, priority, persistent = parameters.unpack()
            try:
                Category(category)
                parsed_priority = Priority(priority)
                if category == Category.NONE or parsed_priority == Priority.NONE:
                    raise ValueError
                if persistent and parsed_priority != Priority.CRITICAL:
                    raise ValueError
            except (ValueError, TypeError):
                invocation.return_dbus_error(
                    f"{EVENTS_INTERFACE}.InvalidArguments", "invalid notification fields"
                )
            else:
                self.inject_synthetic(category, priority, persistent)
                invocation.return_value(None)
            return
        if method == "ClearNotifications":
            self.clear()
            invocation.return_value(None)
            return
        invocation.return_dbus_error(f"{EVENTS_INTERFACE}.UnknownMethod", method)


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
        # A daemon restart can leave the prior script live in the same KWin
        # process. Reload it so its startup report covers the current window.
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
        except Exception as error:  # PyGObject raises GLib.Error; keep imports optional for tests.
            if self.verbose:
                print(f"arcane-host: KWin bridge not ready ({error})", file=sys.stderr, flush=True)


def default_kwin_script() -> Path:
    configured = os.environ.get("CORNE_ARCANE_KWIN_SCRIPT")
    if configured:
        return Path(configured)
    return Path(__file__).resolve().parents[1] / "kwin" / "contents" / "code" / "main.js"


def run(args: argparse.Namespace) -> int:
    salt = secrets.token_bytes(16)

    def identifier_digest(value: str) -> bytes:
        return hashlib.blake2s(
            value.encode("utf-8", "surrogatepass"), key=salt, digest_size=16
        ).digest()

    arbiter = FocusArbiter(identifier_digest=identifier_digest)
    policy = NotificationPolicy()
    override = SCENES[args.scene] if args.scene is not None else None

    def scene_provider() -> Scene:
        return override if override is not None else arbiter.scene

    if args.dry_run:
        class PrintDevice:
            def send(self, report: bytes) -> None:
                print(report.hex())

            def close(self) -> None:
                pass

        device_factory: Callable[[], object] = PrintDevice
    else:
        device_factory = lambda: Device(choose_device(args.device))

    fixed_session = args.session
    session_factory = (
        (lambda: fixed_session) if fixed_session is not None else (lambda: secrets.randbits(32) or 1)
    )
    legacy_summary = (
        EMPTY_SUMMARY
        if args.notify == 0
        else NotificationSummary(args.notify, Category.OTHER, Priority.NORMAL)
    )

    def summary_provider() -> NotificationSummary:
        return legacy_summary if args.notify else policy.summary(time.monotonic())

    heartbeat = HidHeartbeat(
        scene_provider,
        device_factory,
        session_factory,
        summary_provider=summary_provider,
        interval=args.interval,
        retry_interval=args.retry_interval,
        verbose=args.verbose,
    )

    # Dry-run stays usable in build/test environments without a session bus.
    if args.dry_run:
        while True:
            sent = heartbeat.tick(time.monotonic())
            if sent and args.once:
                heartbeat.close()
                return 0
            time.sleep(0.02)

    try:
        import gi

        gi.require_version("Gio", "2.0")
        from gi.repository import Gio, GLib
    except (ImportError, ValueError) as error:
        print(f"arcane-host: PyGObject/Gio is required for automatic focus mode: {error}", file=sys.stderr)
        return 2

    connection = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    owner_id = 0
    focus_service = None
    event_service = None
    desktop_monitor = None
    owned_objects = {}
    if override is None:
        focus_service = FocusService(Gio, connection, arbiter)

    event_service = EventService(Gio, connection, policy, arbiter, heartbeat.request_notify)

    if not args.no_desktop_notifications:
        desktop_adapter = DesktopNotificationAdapter(
            policy, salt, arbiter.matches_focused
        )
        desktop_monitor = DesktopMonitor(
            Gio, GLib, desktop_adapter, time.monotonic, args.verbose
        )
        if not desktop_monitor.start() and args.verbose:
            print(
                "arcane-host: desktop notification monitor unavailable; adapter disabled",
                file=sys.stderr,
                flush=True,
            )

    def name_acquired(bus_connection, name) -> None:
        del name
        if override is None:
            owned_objects["bridge"] = KWinBridgeLoader(
                Gio,
                GLib,
                bus_connection,
                args.kwin_script or default_kwin_script(),
                args.verbose,
            )

    owner_id = Gio.bus_own_name_on_connection(
        connection, BUS_NAME, Gio.BusNameOwnerFlags.NONE, name_acquired, None
    )

    loop = GLib.MainLoop()

    def service_tick() -> bool:
        now = time.monotonic()
        if override is None:
            prior_scene = arbiter.scene
            if arbiter.poll(now) != prior_scene:
                heartbeat.request_heartbeat(now)
        current_summary = summary_provider()
        prior_summary = owned_objects.get("last_summary")
        if prior_summary != current_summary:
            owned_objects["last_summary"] = current_summary
            heartbeat.request_notify()
        sent = heartbeat.tick(now)
        if sent and args.once:
            loop.quit()
            return False
        return True

    GLib.timeout_add(25, service_tick)
    try:
        loop.run()
    except KeyboardInterrupt:
        if args.verbose:
            print("arcane-host: stopped; firmware context expires within 1.5 s")
    finally:
        del focus_service, event_service, desktop_monitor
        owned_objects.clear()
        if owner_id:
            Gio.bus_unown_name(owner_id)
        heartbeat.close()
    return 0


def main() -> int:
    return run(parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
