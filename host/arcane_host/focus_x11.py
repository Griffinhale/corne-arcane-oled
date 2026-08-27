"""Focus producer for plain X11 sessions.

KWin and GNOME Shell report focus from inside the compositor. Everything else --
XFCE, Cinnamon, i3, older Plasma -- has no producer, so focus semantics sit at
their default forever. ReportActiveWindow is an ordinary D-Bus method taking two
strings, so an external producer needs nothing privileged.

Privacy boundary: exactly three window properties are ever requested --
_NET_ACTIVE_WINDOW, to learn which window is focused, and WM_CLASS plus
_GTK_APPLICATION_ID, which name the application. All three are identities, none
carries a title, document, URL or path, and the test suite enforces that by
rejecting any such property name appearing anywhere in this file, including in
comments. Adding one to explain what is not read would defeat the check, so the
names are absent rather than listed.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys

from .dbus_contract import BUS_NAME, FOCUS_INTERFACE, OBJECT_PATH, REPORT_ACTIVE_WINDOW
from .profiles import resolve_profile

XPROP = "xprop"
ACTIVE_WINDOW_PROPERTY = "_NET_ACTIVE_WINDOW"
WM_CLASS_PROPERTY = "WM_CLASS"
GTK_APPLICATION_ID_PROPERTY = "_GTK_APPLICATION_ID"

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


def _property_line(text: str, name: str) -> str:
    """Return the value part of one xprop line, or "" when it is not set.

    xprop prints one line per property and answers an unset one with
    `NAME:  not found.`, so each is looked up by name rather than by position.
    """
    for line in text.splitlines():
        if line.startswith(name) and "=" in line:
            return line.split("=", 1)[1]
    return ""


def parse_wm_class(text: str) -> tuple[str, str]:
    """Return (class, instance) from `xprop -id N WM_CLASS` output.

    WM_CLASS is `"instance", "class"`. KWin reports the class as resourceClass,
    so that is the primary identity -- but the two disagree often enough to be
    worth keeping both: LibreOffice reports a generic class whose instance names
    the actual module, and several toolkits reverse the convention. Both are fed
    to resolve_profile, which takes the first that matches an alias.
    """
    values = [
        part.strip().strip('"') for part in _property_line(text, WM_CLASS_PROPERTY).split(",")
    ]
    values = [value for value in values if value]
    if not values:
        return "", ""
    if len(values) == 1:
        return values[0], ""
    return values[-1], values[0]


def parse_gtk_application_id(text: str) -> str:
    """Return _GTK_APPLICATION_ID, the desktop-file identity GTK apps publish.

    Cinnamon, XFCE and MATE are GTK-heavy, and this is the identity that matches
    the reverse-DNS aliases in profiles.py where a WM_CLASS never would.
    """
    return _property_line(text, GTK_APPLICATION_ID_PROPERTY).strip().strip('"')


def read_identity(window: str, run=subprocess.run) -> tuple[str, str]:
    """Read the identifying properties of one window in a single xprop call.

    ReportActiveWindow carries two strings, so of the three candidates the
    application id wins the second slot when present -- it is the most precise
    -- and the WM_CLASS instance takes it otherwise.
    """
    try:
        completed = run(
            (XPROP, "-id", window, WM_CLASS_PROPERTY, GTK_APPLICATION_ID_PROPERTY),
            capture_output=True,
            text=True,
            timeout=2.0,
        )
    except (OSError, subprocess.SubprocessError):
        return "", ""
    if completed.returncode != 0:
        return "", ""
    resource_class, instance = parse_wm_class(completed.stdout)
    return resource_class, parse_gtk_application_id(completed.stdout) or instance


class FocusReporter:
    """Report a resource class when it changes, and only when it changes."""

    def __init__(self, send) -> None:
        self.send = send
        self.last: tuple[str, str] | None = None

    def offer(self, identity: tuple[str, str]) -> bool:
        if identity == self.last:
            return False
        self.last = identity
        self.send(*identity)
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
        reporter.offer(read_identity(window, run))


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

        def announce(resource_class: str, application_id: str) -> None:
            # Print what did not match a profile, so the output is a worklist for
            # profiles.py rather than a stream to read by eye.
            reported = [value for value in (resource_class, application_id) if value]
            matched = resolve_profile(*reported)
            label = matched.identifier if matched is not None else "UNMATCHED"
            print(
                f"corne-arcane-focus-x11: {' '.join(reported) or '(none)'} -> {label}", flush=True
            )
            send(resource_class, application_id)

        reporter.send = announce

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
