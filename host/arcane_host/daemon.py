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
from .dbus_services import ControlService, EventService, FocusService, KWinBridgeLoader
from .desktop import DesktopMonitor, DesktopNotificationAdapter
from .focus import FocusArbiter
from .heartbeat import DryRunTransport, HidHeartbeat, HidTransport
from .hidraw import Device, choose_device
from .policy import NotificationPolicy
from .protocol import EMPTY_SUMMARY, Category, NotificationSummary, Priority, Scene
from .runtime import DaemonRuntime
from .semantic import SemanticResolver

SCENES = {scene.name.lower(): scene for scene in Scene}

# org.freedesktop.DBus.RequestName flag and replies, from the D-Bus specification.
_DBUS_NAME_FLAG_DO_NOT_QUEUE = 0x4
_DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER = 1
_DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER = 4


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
    parser.add_argument(
        "--no-hid",
        action="store_true",
        default=os.environ.get("CORNE_ARCANE_NO_HID", "") not in ("", "0"),
        help="run with no keyboard: resolve focus and notifications, write nothing to HID "
        "(service setting: CORNE_ARCANE_NO_HID=1)",
    )
    parser.add_argument(
        "--host-signals",
        action="store_true",
        default=os.environ.get("CORNE_ARCANE_HOST_SIGNALS", "") not in ("", "0"),
        help="read idle and lock state, system load and call state into the city mode "
        "and intensity; booleans and levels only (service setting: CORNE_ARCANE_HOST_SIGNALS=1)",
    )
    parser.add_argument(
        "--no-lend",
        action="store_true",
        help="keep the keyboard when another program opens it outside corne-arcane-vial",
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
    if args.no_hid and (args.dry_run or args.once or args.device):
        parser.error("--no-hid opens no keyboard, so it cannot take --dry-run, --once or --device")
    return args


def transport_factory(args: argparse.Namespace) -> Callable[[], HidTransport] | None:
    """What the heartbeat opens: nothing with --no-hid, echoes with --dry-run, else the Corne."""
    if args.no_hid:
        return None
    if args.dry_run:
        return DryRunTransport

    def device_factory() -> HidTransport:
        return Device(choose_device(args.device))

    return device_factory


def lend_check(args: argparse.Namespace) -> Callable[[Path], bool] | None:
    """Lend a real keyboard to anything that opens it outside the guard, unless --no-lend."""
    if args.no_lend:
        return None
    return lambda node: str(node).startswith("/dev/hidraw")


def default_kwin_script() -> Path:
    configured = os.environ.get("CORNE_ARCANE_KWIN_SCRIPT")
    if configured:
        return Path(configured)
    # Installed, this module is PREFIX/lib/corne-arcane-host/arcane_host/daemon.py
    # and host/Makefile puts the script under PREFIX/share. From a checkout it is
    # host/arcane_host/daemon.py beside host/kwin. The installed path is also the
    # one named when neither exists.
    here = Path(__file__).resolve()
    installed = here.parents[3] / "share/kwin/scripts/cornearcane/contents/code/main.js"
    checkout = here.parents[1] / "kwin" / "contents" / "code" / "main.js"
    for candidate in (installed, checkout):
        if candidate.is_file():
            return candidate
    return installed


def _run_dry(heartbeat: HidHeartbeat, once: bool) -> int:
    while True:
        sent = heartbeat.tick(time.monotonic())
        if sent and once:
            heartbeat.close()
            return 0
        time.sleep(0.02)


def run(args: argparse.Namespace) -> int:
    """Run the daemon. Views such as the city window read it over Control."""
    salt = secrets.token_bytes(16)

    def identifier_digest(value: str) -> bytes:
        return hashlib.blake2s(
            value.encode("utf-8", "surrogatepass"), key=salt, digest_size=16
        ).digest()

    arbiter = FocusArbiter(identifier_digest=identifier_digest)
    policy = NotificationPolicy()
    override = SCENES[args.scene] if args.scene is not None else None
    resolver = SemanticResolver(override)

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
        transport_factory(args),
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
        lend_check=lend_check(args),
    )
    adapters = SemanticAdapters(
        resolver, policy, runtime.wake, pomodoro_duration=args.pomodoro_duration
    )
    runtime.bind_adapters(adapters)

    if override is None:
        runtime.own(FocusService(Gio, connection, arbiter, changed=runtime.wake))
    runtime.own(EventService(Gio, connection, policy, arbiter, adapters, runtime.wake))
    runtime.own(ControlService(Gio, GLib, connection, runtime))

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

    runtime.own(
        DBusAdapterHub(
            Gio,
            connection,
            system_connection,
            adapters,
            args.pomodoro_unit,
            args.verbose,
            host_signals=args.host_signals,
        )
    )

    # Claimed synchronously and with DO_NOT_QUEUE, after every service is
    # exported (systemd treats Type=dbus as started once the name appears) and
    # before the first tick. A second instance must not wait in line while it
    # opens the keyboard, or the two take the endpoint from each other every
    # retry; call_sync does not run the main loop, so a loser never ticks.
    if not _request_bus_name(Gio, GLib, connection):
        holder = _name_holder(Gio, GLib, connection)
        runtime.close()
        print(
            f"arcane-host: {BUS_NAME} is already owned by {holder}, so another daemon "
            "is running. Stop it first: systemctl --user stop corne-arcane-host.service",
            file=sys.stderr,
        )
        return 1
    if override is None:
        runtime.own(
            KWinBridgeLoader(
                Gio,
                GLib,
                connection,
                args.kwin_script or default_kwin_script(),
                args.verbose,
            )
        )
    runtime.run()
    return 0


def _bus_call(Gio, GLib, connection, method: str, arguments, reply: str):
    return connection.call_sync(
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        method,
        arguments,
        GLib.VariantType(reply),
        Gio.DBusCallFlags.NONE,
        1000,
        None,
    ).unpack()[0]


def _request_bus_name(Gio, GLib, connection) -> bool:
    """Own BUS_NAME now or report that someone else does. The name goes with the connection."""
    reply = _bus_call(
        Gio,
        GLib,
        connection,
        "RequestName",
        GLib.Variant("(su)", (BUS_NAME, _DBUS_NAME_FLAG_DO_NOT_QUEUE)),
        "(u)",
    )
    return reply in (_DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER, _DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER)


def _name_holder(Gio, GLib, connection) -> str:
    """Name the process that owns BUS_NAME, for the message a second instance prints."""
    try:
        owner = _bus_call(
            Gio, GLib, connection, "GetNameOwner", GLib.Variant("(s)", (BUS_NAME,)), "(s)"
        )
    except Exception:
        return "another process"
    try:
        pid = _bus_call(
            Gio,
            GLib,
            connection,
            "GetConnectionUnixProcessID",
            GLib.Variant("(s)", (owner,)),
            "(u)",
        )
    except Exception:
        return owner
    return f"{owner} (pid {pid})"


def main() -> int:
    return run(parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
