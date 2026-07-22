"""Diagnostic client for the private Corne Arcane Events interface."""

from __future__ import annotations

import argparse
import sys

from .dbus_contract import (
    BUS_NAME,
    CLEAR_NOTIFICATIONS,
    EVENTS_INTERFACE,
    INJECT_SYNTHETIC,
    OBJECT_PATH,
    REPORT_BROWSER_ACTIVITY,
    REPORT_REPOSITORY_STATE,
    REPORT_TERMINAL_COMPLETION,
    RepositoryState,
)
from .protocol import Category, Intensity, Priority, Secondary

CATEGORIES = {item.name.lower(): item for item in Category if item != Category.NONE}
PRIORITIES = {item.name.lower(): item for item in Priority if item != Priority.NONE}


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    notify = commands.add_parser("notify", help="inject a synthetic normalized alert")
    notify.add_argument("--category", choices=CATEGORIES, required=True)
    notify.add_argument("--priority", choices=PRIORITIES, required=True)
    notify.add_argument("--persistent", action="store_true")
    terminal = commands.add_parser("terminal", help=argparse.SUPPRESS)
    terminal.add_argument("duration_ms", type=int)
    terminal.add_argument("exit_status", type=int)
    repository = commands.add_parser("git", help="report a redacted repository state")
    repository.add_argument("state", choices=("clean", "dirty", "operation", "completion"))
    repository.add_argument("--failed", action="store_true")
    browser = commands.add_parser("browser", help="report bounded browser activity")
    browser.add_argument("kind", choices=("scroll", "tab", "page"))
    browser.add_argument("intensity", type=int, choices=range(4))
    commands.add_parser("clear", help="clear transient and persistent alerts")
    return parser.parse_args(argv)


def run(args: argparse.Namespace) -> int:
    if args.command == "notify" and args.persistent and args.priority != "critical":
        print("corne-arcane-event: --persistent requires --priority critical", file=sys.stderr)
        return 2
    try:
        import gi

        gi.require_version("Gio", "2.0")
        from gi.repository import Gio, GLib
    except (ImportError, ValueError) as error:
        print(f"corne-arcane-event: PyGObject/Gio is required: {error}", file=sys.stderr)
        return 2

    connection = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    if args.command == "notify":
        method = INJECT_SYNTHETIC
        parameters = GLib.Variant(
            "(yyb)",
            (int(CATEGORIES[args.category]), int(PRIORITIES[args.priority]), args.persistent),
        )
    elif args.command == "terminal":
        if not 0 <= args.duration_ms <= 0xFFFFFFFF:
            print("corne-arcane-event: duration must fit uint32", file=sys.stderr)
            return 2
        method = REPORT_TERMINAL_COMPLETION
        parameters = GLib.Variant("(ui)", (args.duration_ms, args.exit_status))
    elif args.command == "git":
        method = REPORT_REPOSITORY_STATE
        state = {
            "clean": RepositoryState.CLEAN,
            "dirty": RepositoryState.DIRTY,
            "operation": RepositoryState.OPERATION,
            "completion": RepositoryState.COMPLETION,
        }[args.state]
        parameters = GLib.Variant("(yb)", (state, not args.failed))
    elif args.command == "browser":
        method = REPORT_BROWSER_ACTIVITY
        kind = {
            "scroll": Secondary.SCROLL,
            "tab": Secondary.TAB,
            "page": Secondary.PAGE,
        }[args.kind]
        parameters = GLib.Variant("(yy)", (int(kind), int(Intensity(args.intensity))))
    else:
        method = CLEAR_NOTIFICATIONS
        parameters = None
    try:
        connection.call_sync(
            BUS_NAME,
            OBJECT_PATH,
            EVENTS_INTERFACE,
            method,
            parameters,
            None,
            Gio.DBusCallFlags.NONE,
            2000,
            None,
        )
    except Exception as error:
        print(f"corne-arcane-event: {error}", file=sys.stderr)
        return 1
    return 0


def main() -> int:
    return run(parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
