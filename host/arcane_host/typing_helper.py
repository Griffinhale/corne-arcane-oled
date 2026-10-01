"""Opt-in typing helper for a keyboard without this firmware.

The one place on the host that reads keys, and the rule it keeps is
docs/typing-summary.md. It reads one chosen keyboard through evdev, read-only
and never grabbed, and never the Corne. Each keydown becomes a keyboard row
and a time the moment it is read; the keycode goes no further than
:func:`row_of`. At each wall-clock minute the window of rows and times goes to
:func:`~arcane_host.typing_summary.summarize`, and only its four enums leave
the process, as one signal on the helper's own bus name. The daemon does not
listen; the desktop city does.

Off by default: the unit is installed but not enabled, and the udev rule that
lets the active seat read keyboards ships where udev does not look.
"""

from __future__ import annotations

import argparse
import os
import re
import struct
import sys
import time
from collections.abc import Callable
from pathlib import Path
from typing import NamedTuple

from .dbus_contract import (
    TYPING_BUS_NAME,
    TYPING_INTERFACE,
    TYPING_OBJECT_PATH,
    TYPING_SIGNATURE,
    TYPING_SUMMARY,
    TYPING_XML,
)
from .hidraw import CORNE_USB_ID, USB_ID_ENV, parse_usb_id
from .typing_summary import WINDOW_MS, Row, RowSpread, Spread, Tempo, TypingSummary, summarize

SYS_CLASS_INPUT = Path("/sys/class/input")
DEVICE_ENV = "CORNE_ARCANE_TYPING_DEVICE"

# struct input_event: a timeval, then type, code and value.
EVENT = struct.Struct("llHHi")
EV_KEY = 1
KEY_PRESS = 1  # 0 is release and 2 autorepeat; neither is counted.

# A keyboard is a device that reports every letter.
LETTER_CODES = frozenset((*range(16, 26), *range(30, 39), *range(44, 51)))

# The counted keys and the row each folds into: letters, digits, punctuation,
# space, Enter, Backspace and Tab. The number row, Backspace and Tab are TOP,
# space is THUMB. Everything else -- modifiers, arrows, F-keys, the keypad --
# has no row and is not counted.
_ROWS: dict[int, Row] = {
    **dict.fromkeys((41, *range(2, 16), *range(16, 28), 43), Row.TOP),
    **dict.fromkeys((*range(30, 41), 28), Row.HOME),
    **dict.fromkeys((*range(44, 54), 86), Row.BOTTOM),
    57: Row.THUMB,
}


class HelperError(RuntimeError):
    """No keyboard to read, or a keyboard the helper must not read."""


def row_of(code: int) -> Row | None:
    """The row a counted key folds into, or None for a key that is not counted."""
    return _ROWS.get(code)


class Keyboard(NamedTuple):
    node: Path
    name: str
    usb_id: tuple[int, int]


def _key_bits(text: str) -> int:
    """A sysfs capability bitmap, hex longs with the highest first, as one integer."""
    bits = struct.calcsize("l") * 8
    value = 0
    for word in text.split():
        value = (value << bits) | int(word, 16)
    return value


def _describe(entry: Path) -> Keyboard | None:
    device = entry / "device"
    try:
        keys = _key_bits((device / "capabilities" / "key").read_text())
        usb_id = (
            int((device / "id" / "vendor").read_text(), 16),
            int((device / "id" / "product").read_text(), 16),
        )
        name = (device / "name").read_text().strip()
    except (OSError, ValueError):
        return None
    if not all(keys >> code & 1 for code in LETTER_CODES):
        return None
    return Keyboard(Path("/dev/input") / entry.name, name, usb_id)


def _corne_id() -> tuple[int, int]:
    override = os.environ.get(USB_ID_ENV)
    return parse_usb_id(override) if override else CORNE_USB_ID


def keyboards(
    sys_class: Path = SYS_CLASS_INPUT, exclude: tuple[int, int] | None = CORNE_USB_ID
) -> list[Keyboard]:
    """Every keyboard the helper may read, never one with the `exclude` USB ID."""
    found = []
    entries = sys_class.glob("event*") if sys_class.is_dir() else ()
    for entry in sorted(entries, key=lambda path: int(path.name[5:] or 0)):
        board = _describe(entry)
        if board is not None and board.usb_id != exclude:
            found.append(board)
    return found


def choose(
    requested: str | None,
    sys_class: Path = SYS_CLASS_INPUT,
    exclude: tuple[int, int] | None = CORNE_USB_ID,
) -> list[Keyboard]:
    """The event nodes to read: the one named, or the only keyboard there is.

    A name may be an event node, a /dev/input/by-id link to one, or the bare
    eventN. Unnamed, every node of one keyboard is read, since many keyboards
    report their letters on more than one interface; two keyboards need a
    name. The Corne is refused here, before anything is opened.
    """
    if requested:
        node = Path(os.path.realpath(requested)).name
        if not re.fullmatch(r"event[0-9]+", node):
            raise HelperError(f"{requested} is not an evdev node (/dev/input/eventN)")
        board = _describe(sys_class / node)
        if board is None:
            raise HelperError(f"{requested} is not a keyboard")
        if board.usb_id == exclude:
            raise HelperError(f"{requested} is the Corne, which this helper never reads")
        return [board]
    found = keyboards(sys_class, exclude)
    if not found:
        raise HelperError("no keyboard other than the Corne is readable")
    if len({board.usb_id for board in found}) > 1:
        names = ", ".join(f"{board.node.name} ({board.name})" for board in found)
        raise HelperError(f"several keyboards, choose one with --device: {names}")
    return found


class Collector:
    """One wall-clock minute of rows and times; `send` gets each kept summary."""

    def __init__(self, send: Callable[[TypingSummary], None]) -> None:
        self.send = send
        self.window: list[tuple[Row, int]] = []
        self.minute: int | None = None
        self._partial: dict[int, bytes] = {}

    def feed(self, data: bytes, source: int = 0) -> None:
        """Take raw input_event bytes from one node; a read may end partway."""
        data = self._partial.get(source, b"") + data
        whole = len(data) - len(data) % EVENT.size
        self._partial[source] = data[whole:]
        for seconds, micros, kind, code, value in EVENT.iter_unpack(data[:whole]):
            if kind != EV_KEY or value != KEY_PRESS:
                continue
            row = row_of(code)
            if row is None:
                continue
            self.key(row, seconds * 1000 + micros // 1000)

    def key(self, row: Row, ms: int) -> None:
        minute = ms // WINDOW_MS
        if self.minute is not None and minute < self.minute:
            return  # its window has already closed
        if minute != self.minute:
            self._close()
            self.minute = minute
        self.window.append((row, ms))

    def tick(self, now_ms: int) -> None:
        """Close the window once the wall clock has left its minute."""
        minute = now_ms // WINDOW_MS
        if self.minute is None or minute > self.minute:
            self._close()
            self.minute = minute

    def _close(self) -> None:
        window, self.window = self.window, []
        if window:
            summary = summarize(window)
            if summary is not None:
                self.send(summary)


def encode(summary: TypingSummary) -> tuple[int, int, int, int]:
    """The four bytes of a summary, or TypeError/ValueError for anything else."""
    if type(summary) is not TypingSummary:
        raise TypeError("only a TypingSummary leaves the helper")
    values = []
    for value, kind in zip(summary, (Tempo, Spread, Row, RowSpread)):
        if type(value) is not kind and type(value) is not int:
            raise TypeError(f"{value!r} is not a {kind.__name__}")
        values.append(int(kind(value)))
    return tuple(values)


class Publisher:
    """The helper's bus name and its one signal."""

    def __init__(self, connection) -> None:
        self.connection = connection

    def own(self) -> None:
        from gi.repository import Gio, GLib

        reply = self.connection.call_sync(
            "org.freedesktop.DBus",
            "/org/freedesktop/DBus",
            "org.freedesktop.DBus",
            "RequestName",
            GLib.Variant("(su)", (TYPING_BUS_NAME, 4)),  # DO_NOT_QUEUE
            GLib.VariantType("(u)"),
            Gio.DBusCallFlags.NONE,
            2000,
            None,
        ).unpack()[0]
        if reply != 1:  # PRIMARY_OWNER
            raise HelperError(f"{TYPING_BUS_NAME} is taken: is a typing helper already running?")
        node = Gio.DBusNodeInfo.new_for_xml(TYPING_XML)
        self.connection.register_object(TYPING_OBJECT_PATH, node.interfaces[0], None, None, None)

    def send(self, summary: TypingSummary) -> None:
        from gi.repository import GLib

        self.connection.emit_signal(
            None,
            TYPING_OBJECT_PATH,
            TYPING_INTERFACE,
            TYPING_SUMMARY,
            GLib.Variant(TYPING_SIGNATURE, encode(summary)),
        )


def _run(boards: list[Keyboard]) -> int:
    try:
        import gi

        gi.require_version("Gio", "2.0")
        from gi.repository import Gio, GLib
    except (ImportError, ValueError) as error:
        print(f"corne-arcane-typing: PyGObject/Gio is required: {error}", file=sys.stderr)
        return 2
    try:
        publisher = Publisher(Gio.bus_get_sync(Gio.BusType.SESSION, None))
        publisher.own()
    except HelperError as error:
        print(f"corne-arcane-typing: {error}", file=sys.stderr)
        return 2
    except Exception as error:
        print(f"corne-arcane-typing: no session bus: {error}", file=sys.stderr)
        return 2
    fds = []
    for board in boards:
        try:
            fds.append(os.open(board.node, os.O_RDONLY | os.O_NONBLOCK | os.O_CLOEXEC))
        except OSError as error:
            for fd in fds:
                os.close(fd)
            print(
                f"corne-arcane-typing: cannot read {board.node}: {error.strerror}; "
                "see host/README.md, Typing helper",
                file=sys.stderr,
            )
            return 2

    collector = Collector(publisher.send)
    loop = GLib.MainLoop()
    status = 0

    def readable(fd, _condition) -> bool:
        nonlocal status
        while True:
            try:
                data = os.read(fd, EVENT.size * 64)
            except BlockingIOError:
                return GLib.SOURCE_CONTINUE
            except OSError:
                data = b""
            if not data:  # unplugged: the unit restarts and chooses again
                status = 1
                loop.quit()
                return GLib.SOURCE_REMOVE
            collector.feed(data, fd)

    def minute_edge() -> bool:
        now = int(time.time() * 1000)
        collector.tick(now)
        GLib.timeout_add(WINDOW_MS - now % WINDOW_MS + 50, minute_edge)
        return GLib.SOURCE_REMOVE

    for fd in fds:
        GLib.unix_fd_add_full(
            GLib.PRIORITY_DEFAULT,
            fd,
            GLib.IOCondition.IN | GLib.IOCondition.HUP | GLib.IOCondition.ERR,
            readable,
        )
    minute_edge()
    try:
        loop.run()
    except KeyboardInterrupt:
        pass
    finally:
        for fd in fds:
            os.close(fd)
    return status


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Send a four-value typing summary per minute to the desktop city."
    )
    parser.add_argument(
        "--device",
        default=os.environ.get(DEVICE_ENV),
        help=f"keyboard to read, as /dev/input/eventN or a by-id link (default ${DEVICE_ENV})",
    )
    parser.add_argument("--list", action="store_true", help="list the keyboards it may read")
    args = parser.parse_args(argv)

    try:
        exclude = _corne_id()
        if args.list:
            for board in keyboards(exclude=exclude):
                print(f"{board.node}\t{board.usb_id[0]:04x}:{board.usb_id[1]:04x}\t{board.name}")
            return 0
        boards = choose(args.device, exclude=exclude)
    except (HelperError, ValueError) as error:
        print(f"corne-arcane-typing: {error}", file=sys.stderr)
        return 2
    return _run(boards)


if __name__ == "__main__":
    raise SystemExit(main())
