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

from .adapters import DBusAdapterHub, SemanticAdapters

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
from .semantic import SemanticResolver

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
    parser.add_argument(
        "--pomodoro-unit",
        default=os.environ.get("CORNE_ARCANE_POMODORO_UNIT"),
        help="optional systemd user timer unit to treat as a Pomodoro source",
    )
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

    def next_deadline(self, now: float) -> float:
        if self.device is None:
            return max(now, self.next_connect)
        if self.notify_pending:
            return now
        return self.next_heartbeat

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
    def __init__(self, Gio, connection, arbiter: FocusArbiter, clock=time.monotonic,
                 changed: Callable[[], None] = lambda: None) -> None:
        self.Gio = Gio
        self.connection = connection
        self.arbiter = arbiter
        self.clock = clock
        self.changed = changed
        info = Gio.DBusNodeInfo.new_for_xml(INTROSPECTION_XML)
        self.registration_id = connection.register_object(
            OBJECT_PATH, info.interfaces[0], self._method_call, None, None
        )

    def _method_call(self, connection, sender, path, interface, method, parameters, invocation) -> None:
        del connection, sender, path, interface
        if method == "ReportActiveWindow":
            resource_class, desktop_file_name = parameters.unpack()
            self.arbiter.report(resource_class, desktop_file_name, self.clock())
            self.changed()
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
    <method name='ReportRepositoryState'>
      <arg type='y' name='state' direction='in'/>
      <arg type='b' name='success' direction='in'/>
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
        adapters: SemanticAdapters | None = None,
        clock=time.monotonic,
    ) -> None:
        self.policy = policy
        self.focus = focus
        self.changed = changed
        self.adapters = adapters
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

    def report_repository_state(self, state: int, success: bool) -> bool:
        if self.adapters is not None:
            return self.adapters.repository(int(state), bool(success))
        if state not in range(4):
            return False
        priority = Priority.CRITICAL if state == 3 and not success else (
            Priority.LOW if state == 0 else Priority.NORMAL
        )
        changed = self.policy.inject(
            Category.TRANSFER,
            priority,
            False,
            self.clock(),
            key="repository-state",
            replacement=True,
        )
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
        if method == "ReportRepositoryState":
            state, success = parameters.unpack()
            if state not in range(4):
                invocation.return_dbus_error(
                    f"{EVENTS_INTERFACE}.InvalidArguments", "invalid repository state"
                )
            else:
                self.report_repository_state(state, success)
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
    resolver = SemanticResolver(override)

    def scene_provider() -> Scene:
        return resolver.state.scene

    if args.dry_run:
        class PrintDevice:
            def send(self, report: bytes) -> None:
                print(report.hex())

            def close(self) -> None:
                pass

        device_factory: Callable[[], object] = PrintDevice
    else:
        def device_factory() -> object:
            return Device(choose_device(args.device))

    fixed_session = args.session
    session_factory = (
        (lambda: fixed_session) if fixed_session is not None else (lambda: secrets.randbits(32) or 1)
    )
    legacy_summary = (
        EMPTY_SUMMARY
        if args.notify == 0
        else NotificationSummary(args.notify, Category.OTHER, Priority.NORMAL)
    )

    resolver.update(summary=legacy_summary if args.notify else policy.summary(time.monotonic()))

    def summary_provider() -> NotificationSummary:
        return resolver.state.summary

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
    try:
        system_connection = Gio.bus_get_sync(Gio.BusType.SYSTEM, None)
    except Exception:
        system_connection = None
    owner_id = 0
    focus_service = None
    event_service = None
    desktop_monitor = None
    adapter_hub = None
    owned_objects = {}
    loop = GLib.MainLoop()
    source_id = 0
    last_revision = resolver.state.revision
    in_tick = False
    wake_pending = False

    def service_tick() -> bool:
        nonlocal source_id, last_revision, in_tick, wake_pending
        source_id = 0
        in_tick = True
        now = time.monotonic()
        adapters.poll(now)
        if override is None:
            arbiter.poll(now)
        summary = legacy_summary if args.notify else policy.summary(now)
        resolver.update(summary=summary, focus_scene=arbiter.scene)
        if resolver.state.revision != last_revision:
            last_revision = resolver.state.revision
            heartbeat.request_notify()
        sent = heartbeat.tick(now)
        if sent and args.once:
            in_tick = False
            loop.quit()
            return False
        deadlines = [heartbeat.next_deadline(now), now + 1.0]
        focus_deadline = arbiter.next_deadline() if override is None else None
        policy_deadline = None if args.notify else policy.next_deadline(now)
        if focus_deadline is not None:
            deadlines.append(focus_deadline)
        if policy_deadline is not None:
            deadlines.append(policy_deadline)
        adapter_deadline = adapters.next_deadline(now)
        if adapter_deadline is not None:
            deadlines.append(adapter_deadline)
        delay_ms = (
            1 if wake_pending else
            max(1, min(1000, int(max(0.0, min(deadlines) - now) * 1000)))
        )
        wake_pending = False
        source_id = GLib.timeout_add(delay_ms, service_tick)
        in_tick = False
        return False

    def wake() -> None:
        nonlocal source_id, wake_pending
        if in_tick:
            wake_pending = True
            return
        if source_id:
            try:
                GLib.source_remove(source_id)
            except Exception:
                pass
        source_id = GLib.idle_add(service_tick)

    adapters = SemanticAdapters(resolver, policy, wake)
    if override is None:
        focus_service = FocusService(Gio, connection, arbiter, changed=wake)

    event_service = EventService(Gio, connection, policy, arbiter, wake, adapters)

    if not args.no_desktop_notifications:
        desktop_adapter = DesktopNotificationAdapter(
            policy, salt, arbiter.matches_focused
        )
        desktop_monitor = DesktopMonitor(
            Gio, GLib, desktop_adapter, time.monotonic, args.verbose, wake
        )
        if not desktop_monitor.start() and args.verbose:
            print(
                "arcane-host: desktop notification monitor unavailable; adapter disabled",
                file=sys.stderr,
                flush=True,
            )

    adapter_hub = DBusAdapterHub(
        Gio, connection, system_connection, adapters, args.pomodoro_unit
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

    wake()
    try:
        loop.run()
    except KeyboardInterrupt:
        if args.verbose:
            print("arcane-host: stopped; firmware context expires within 1.5 s")
    finally:
        if adapter_hub is not None:
            adapter_hub.close()
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
