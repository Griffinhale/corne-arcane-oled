"""Application-aware Corne Arcane semantic heartbeat daemon."""

from __future__ import annotations

import argparse
import hashlib
import os
import secrets
import sys
import time
from pathlib import Path
from typing import Callable

from .adapters import SemanticAdapters
from .dbus_adapters import DBusAdapterHub
from .dbus_contract import BUS_NAME
from .dbus_services import EventService, FocusService, KWinBridgeLoader
from .desktop import DesktopMonitor, DesktopNotificationAdapter
from .focus import FocusArbiter
from .heartbeat import DryRunTransport, HidHeartbeat, HidTransport
from .hidraw import Device, choose_device
from .policy import NotificationPolicy
from .protocol import EMPTY_SUMMARY, Category, NotificationSummary, Priority, Scene
from .runtime import DaemonRuntime
from .semantic import SemanticResolver

SCENES = {scene.name.lower(): scene for scene in Scene}


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
    parser.add_argument(
        "--pomodoro-duration",
        type=float,
        default=1500.0,
        metavar="SECONDS",
        help="Pomodoro ritual duration used for quarter-stage boundaries (default: 1500)",
    )
    parser.add_argument(
        "--once", action="store_true", help="send one heartbeat after HELLO and exit"
    )
    parser.add_argument(
        "--dry-run", action="store_true", help="print reports instead of opening hidraw"
    )
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
    if args.pomodoro_duration <= 0:
        parser.error("--pomodoro-duration must be positive")
    return args


def default_kwin_script() -> Path:
    configured = os.environ.get("CORNE_ARCANE_KWIN_SCRIPT")
    if configured:
        return Path(configured)
    return Path(__file__).resolve().parents[1] / "kwin" / "contents" / "code" / "main.js"


def _run_dry(heartbeat: HidHeartbeat, once: bool) -> int:
    while True:
        sent = heartbeat.tick(time.monotonic())
        if sent and once:
            heartbeat.close()
            return 0
        time.sleep(0.02)


def run(args: argparse.Namespace, *, presenter_factory: Callable | None = None) -> int:
    """Run the daemon.

    ``presenter_factory`` lets one in-process reader ride along on the semantic
    stack -- today the desktop city window. It is called with (GLib, runtime,
    resolver) once the main loop exists, and whatever it returns is owned and
    closed by the runtime. Readers never produce semantics and never touch the
    HID endpoint, so there is still exactly one consumer of the shared
    interface.
    """
    if presenter_factory is not None and args.dry_run:
        print("arcane-host: --dry-run has no main loop to present from", file=sys.stderr)
        return 2

    salt = secrets.token_bytes(16)

    def identifier_digest(value: str) -> bytes:
        return hashlib.blake2s(
            value.encode("utf-8", "surrogatepass"), key=salt, digest_size=16
        ).digest()

    arbiter = FocusArbiter(identifier_digest=identifier_digest)
    policy = NotificationPolicy()
    override = SCENES[args.scene] if args.scene is not None else None
    resolver = SemanticResolver(override)

    if args.dry_run:
        device_factory: Callable[[], HidTransport] = DryRunTransport
    else:

        def device_factory() -> HidTransport:
            return Device(choose_device(args.device))

    fixed_session = args.session
    session_factory = (
        (lambda: fixed_session)
        if fixed_session is not None
        else (lambda: secrets.randbits(32) or 1)
    )
    fixed_summary = (
        EMPTY_SUMMARY
        if args.notify == 0
        else NotificationSummary(args.notify, Category.OTHER, Priority.NORMAL)
    )
    resolver.update(summary=fixed_summary if args.notify else policy.summary(time.monotonic()))
    heartbeat = HidHeartbeat(
        lambda: resolver.state.scene,
        device_factory,
        session_factory,
        summary_provider=lambda: resolver.state.summary,
        civic_provider=lambda: resolver.state.civic,
        interval=args.interval,
        retry_interval=args.retry_interval,
        verbose=args.verbose,
    )

    if args.dry_run:
        return _run_dry(heartbeat, args.once)

    try:
        import gi

        gi.require_version("Gio", "2.0")
        from gi.repository import Gio, GLib
    except (ImportError, ValueError) as error:
        heartbeat.close()
        print(
            f"arcane-host: PyGObject/Gio is required for automatic focus mode: {error}",
            file=sys.stderr,
        )
        return 2

    connection = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    try:
        system_connection = Gio.bus_get_sync(Gio.BusType.SYSTEM, None)
    except Exception:
        system_connection = None

    runtime = DaemonRuntime(
        Gio,
        GLib,
        GLib.MainLoop(),
        heartbeat,
        resolver,
        policy,
        arbiter,
        fixed_summary=fixed_summary if args.notify else None,
        focus_override=override is not None,
        once=args.once,
        verbose=args.verbose,
    )
    adapters = SemanticAdapters(
        resolver, policy, runtime.wake, pomodoro_duration=args.pomodoro_duration
    )
    runtime.bind_adapters(adapters)
    if presenter_factory is not None:
        runtime.own(presenter_factory(GLib, runtime, resolver))

    if override is None:
        runtime.own(FocusService(Gio, connection, arbiter, changed=runtime.wake))
    runtime.own(EventService(Gio, connection, policy, arbiter, adapters, runtime.wake))

    if not args.no_desktop_notifications:
        desktop_adapter = DesktopNotificationAdapter(policy, salt, arbiter.matches_focused)
        desktop_monitor = runtime.own(
            DesktopMonitor(
                Gio,
                GLib,
                desktop_adapter,
                time.monotonic,
                args.verbose,
                runtime.wake,
            )
        )
        if not desktop_monitor.start() and args.verbose:
            print(
                "arcane-host: desktop notification monitor unavailable; adapter disabled",
                file=sys.stderr,
                flush=True,
            )

    runtime.own(DBusAdapterHub(Gio, connection, system_connection, adapters, args.pomodoro_unit))

    def name_acquired(bus_connection, name) -> None:
        del name
        if override is None:
            runtime.own(
                KWinBridgeLoader(
                    Gio,
                    GLib,
                    bus_connection,
                    args.kwin_script or default_kwin_script(),
                    args.verbose,
                )
            )

    runtime.set_bus_owner(
        Gio.bus_own_name_on_connection(
            connection,
            BUS_NAME,
            Gio.BusNameOwnerFlags.NONE,
            name_acquired,
            None,
        )
    )
    runtime.run()
    return 0


def main() -> int:
    return run(parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
