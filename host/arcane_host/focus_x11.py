"""Focus producer for plain X11 sessions.

KWin and GNOME Shell report focus from inside the compositor. Everything else --
XFCE, Cinnamon, i3, older Plasma -- has no producer, so focus semantics sit at
their default forever. ReportActiveWindow is an ordinary D-Bus method taking two
strings, so an external producer needs nothing privileged.

Privacy boundary: exactly two window properties are ever requested --
_NET_ACTIVE_WINDOW, to learn which window is focused, and WM_CLASS, to learn
which application it belongs to. Nothing carrying a title, document, URL or path
is reachable from here, and the test suite enforces that by rejecting any such
property name appearing anywhere in this file, including in comments. Adding one
to explain what is not read would defeat the check, so the names are absent
rather than listed.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys

from .dbus_contract import BUS_NAME, FOCUS_INTERFACE, OBJECT_PATH, REPORT_ACTIVE_WINDOW

XPROP = "xprop"
ACTIVE_WINDOW_PROPERTY = "_NET_ACTIVE_WINDOW"
WM_CLASS_PROPERTY = "WM_CLASS"

# xprop prints an unset or unmapped window as this rather than an id.
NO_WINDOW = ("0x0", "none")


def parse_active_window(line: str) -> str | None:
    """Return the window id from an `xprop -spy _NET_ACTIVE_WINDOW` line.

    The line looks like `_NET_ACTIVE_WINDOW(WINDOW): window id # 0x3a00007`, and
    may carry several ids when the property is a list; the first is the active
    one. Anything else is a property we did not ask for and is ignored.
    """
    if ACTIVE_WINDOW_PROPERTY not in line or "#" not in line:
        return None
    candidates = line.split("#", 1)[1].replace(",", " ").split()
    if not candidates:
        return None
    window = candidates[0].strip()
    if window.lower() in NO_WINDOW:
        return None
    return window


def parse_wm_class(text: str) -> str:
    """Return the resource class from `xprop -id N WM_CLASS` output.

    WM_CLASS is `"instance", "class"`. KWin's resourceClass is the class, so
    prefer the second string and fall back to the instance when a client sets
    only one. An unset property yields the empty string, which reports as no
    identifiable application rather than guessing.
    """
    if "=" not in text:
        return ""
    values = [part.strip().strip('"') for part in text.split("=", 1)[1].split(",")]
    values = [value for value in values if value]
    if not values:
        return ""
    return values[-1]


def read_resource_class(window: str, run=subprocess.run) -> str:
    """Read only WM_CLASS for one window id."""
    try:
        completed = run(
            (XPROP, "-id", window, WM_CLASS_PROPERTY),
            capture_output=True,
            text=True,
            timeout=2.0,
        )
    except (OSError, subprocess.SubprocessError):
        return ""
    if completed.returncode != 0:
        return ""
    return parse_wm_class(completed.stdout)


class FocusReporter:
    """Report a resource class when it changes, and only when it changes."""

    def __init__(self, send) -> None:
        self.send = send
        self.last: str | None = None

    def offer(self, resource_class: str) -> bool:
        if resource_class == self.last:
            return False
        self.last = resource_class
        # X11 has no reliable desktop-file identity, so the second argument is
        # deliberately empty rather than a guess; resolve_profile falls back to
        # the resource class.
        self.send(resource_class, "")
        return True


def _connect():
    import gi

    gi.require_version("Gio", "2.0")
    from gi.repository import Gio, GLib

    connection = Gio.bus_get_sync(Gio.BusType.SESSION, None)

    def send(resource_class: str, desktop_file_name: str) -> None:
        try:
            connection.call_sync(
                BUS_NAME,
                OBJECT_PATH,
                FOCUS_INTERFACE,
                REPORT_ACTIVE_WINDOW,
                GLib.Variant("(ss)", (resource_class, desktop_file_name)),
                None,
                Gio.DBusCallFlags.NONE,
                1000,
                None,
            )
        except Exception:
            # An absent daemon disables only this producer, matching every other
            # adapter: the keyboard keeps working without it.
            pass

    return send


def watch(reporter: FocusReporter, stream, run=subprocess.run) -> None:
    for line in stream:
        window = parse_active_window(line)
        if window is None:
            continue
        reporter.offer(read_resource_class(window, run))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Report X11 focus to the Corne Arcane daemon.")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args(argv)

    if shutil.which(XPROP) is None:
        print(
            f"corne-arcane-focus-x11: {XPROP} not found; install x11-utils",
            file=sys.stderr,
        )
        return 2
    try:
        send = _connect()
    except (ImportError, ValueError) as error:
        print(f"corne-arcane-focus-x11: PyGObject/Gio is required: {error}", file=sys.stderr)
        return 2
    except Exception as error:
        print(f"corne-arcane-focus-x11: no session bus: {error}", file=sys.stderr)
        return 2

    reporter = FocusReporter(send)
    if args.verbose:
        reporter.send = lambda resource_class, desktop: (
            print(f"corne-arcane-focus-x11: {resource_class or '(none)'}", flush=True),
            send(resource_class, desktop),
        )[1]

    process = subprocess.Popen(
        (XPROP, "-root", "-spy", ACTIVE_WINDOW_PROPERTY),
        stdout=subprocess.PIPE,
        text=True,
    )
    try:
        watch(reporter, process.stdout)
    except KeyboardInterrupt:
        return 0
    finally:
        process.terminate()
        try:
            process.wait(timeout=2.0)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
