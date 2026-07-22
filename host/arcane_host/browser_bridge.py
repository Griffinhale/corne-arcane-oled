"""Strict Firefox native-messaging bridge for bounded activity enums only."""

from __future__ import annotations

import json
import struct
import sys

from .dbus_contract import (
    BUS_NAME,
    EVENTS_INTERFACE,
    OBJECT_PATH,
    REPORT_BROWSER_ACTIVITY,
)
from .protocol import Secondary

KINDS = {"scroll": Secondary.SCROLL, "tab": Secondary.TAB, "page": Secondary.PAGE}


def decode_message(payload: bytes) -> tuple[Secondary, int]:
    value = json.loads(payload.decode("utf-8"))
    if not isinstance(value, dict) or set(value) != {"kind", "intensity"}:
        raise ValueError("message must contain only kind and intensity")
    kind = KINDS.get(value["kind"])
    intensity = value["intensity"]
    if kind is None or isinstance(intensity, bool) or not isinstance(intensity, int):
        raise ValueError("invalid browser activity")
    if not 0 <= intensity <= 3:
        raise ValueError("intensity must be in 0..3")
    return kind, intensity


def read_message(stream) -> bytes | None:
    header = stream.read(4)
    if not header:
        return None
    if len(header) != 4:
        raise ValueError("truncated native-message header")
    size = struct.unpack("=I", header)[0]
    if size > 1024:
        raise ValueError("native message exceeds bounded input size")
    payload = stream.read(size)
    if len(payload) != size:
        raise ValueError("truncated native message")
    return payload


def write_reply(stream, accepted: bool) -> None:
    payload = json.dumps({"accepted": accepted}, separators=(",", ":")).encode("ascii")
    stream.write(struct.pack("=I", len(payload)))
    stream.write(payload)
    stream.flush()


def main() -> int:
    try:
        import gi

        gi.require_version("Gio", "2.0")
        from gi.repository import Gio, GLib
    except (ImportError, ValueError) as error:
        print(f"corne-arcane-browser-bridge: {error}", file=sys.stderr)
        return 2
    connection = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    while True:
        payload = read_message(sys.stdin.buffer)
        if payload is None:
            break
        try:
            kind, intensity = decode_message(payload)
            connection.call_sync(
                BUS_NAME,
                OBJECT_PATH,
                EVENTS_INTERFACE,
                REPORT_BROWSER_ACTIVITY,
                GLib.Variant("(yy)", (int(kind), intensity)),
                None,
                Gio.DBusCallFlags.NONE,
                1000,
                None,
            )
        except Exception:
            write_reply(sys.stdout.buffer, False)
        else:
            write_reply(sys.stdout.buffer, True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
