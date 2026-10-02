"""Focus producer for Hyprland 0.56 and later running a Lua config.

Hyprland's event socket broadcasts every event to every listener with no
filter, and the query commands that describe windows print every property of
each, captions included. Neither can be used without that text entering this
process, so this producer opens neither.

Privacy boundary: exactly one request is ever sent, the REQUEST constant below,
on the command socket. `repl` evaluates that fixed Lua chunk inside the
compositor and replies with only what the chunk returns: a marker followed by
the focused window's class, which is the application's identity and nothing
else. The class is resolved against profiles.py in this process and only the
matched profile, as one of its own aliases, or "" is reported.

A legacy hyprlang config answers `repl` with a fixed refusal; so does any
release older than 0.56. The producer then exits with a message. It never
falls back to another socket or query, because every fallback carries text this
boundary exists to keep out. The test suite rejects the names of those sockets
and queries anywhere in this file, including comments, which is why they are
absent rather than listed.
"""

from __future__ import annotations

import argparse
import os
import socket
import sys
import time

from .focus_sway import representative
from .focus_x11 import FocusReporter, _connect
from .profiles import resolve_profile

PROGRAM = "corne-arcane-focus-hyprland"
SIGNATURE_VARIABLE = "HYPRLAND_INSTANCE_SIGNATURE"
SOCKET_NAME = ".socket.sock"

MARKER = "corne-arcane-class:"
# The only request this module can send.
REQUEST = (
    f'repl local w = hl.get_active_window(); return "{MARKER}" .. tostring(w and w.class or "")'
)
# What a legacy-config Hyprland answers to `repl`.
LEGACY_CONFIG_REPLY = "only supported with the lua config manager"

POLL_SECONDS = 0.25
MAX_REPLY_BYTES = 4096

UNSUPPORTED = 3


class Unsupported(Exception):
    """This Hyprland cannot answer the request; nothing else will be tried."""


def socket_path(environ) -> str:
    runtime = environ.get("XDG_RUNTIME_DIR", "")
    signature = environ.get(SIGNATURE_VARIABLE, "")
    if not runtime or not signature or "/" in signature:
        return ""
    return os.path.join(runtime, "hypr", signature, SOCKET_NAME)


def ask(path: str) -> str:
    """Send REQUEST on a fresh connection and return the decoded reply.

    The command socket answers one request per connection and then closes it.
    """
    connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        connection.settimeout(2.0)
        connection.connect(path)
        connection.sendall(REQUEST.encode("utf-8"))
        chunks = []
        received = 0
        while received < MAX_REPLY_BYTES:
            chunk = connection.recv(MAX_REPLY_BYTES - received)
            if not chunk:
                break
            chunks.append(chunk)
            received += len(chunk)
    finally:
        connection.close()
    return b"".join(chunks).decode("utf-8", "replace")


def parse_reply(reply: str) -> str:
    """The class from a `repl` reply, or "" when the reply is not ours.

    Raises Unsupported when Hyprland refused `repl` itself, either because the
    config is not Lua or because the release predates it.
    """
    if LEGACY_CONFIG_REPLY in reply:
        raise Unsupported("Hyprland is running a legacy config; the producer needs the Lua config")
    line = reply.strip()
    if not line.startswith(MARKER):
        if line.startswith("unknown request") or line == "ok":
            raise Unsupported("this Hyprland has no repl; the producer needs 0.56 or later")
        if line.startswith("error"):
            # The fixed chunk failed to evaluate, so this Hyprland's Lua API is
            # not the one it was written against. Polling would only repeat it.
            raise Unsupported("Hyprland rejected the focus query; its Lua API may have changed")
        return ""
    if "\n" in line:
        return ""
    return line[len(MARKER) :].strip()


def report_for(class_value: str) -> str:
    """Reduce the class to the matched profile, reported as one fixed alias."""
    profile = resolve_profile(class_value)
    return representative(profile) if profile is not None else ""


class HyprlandFocus:
    def __init__(self, path: str, reporter: FocusReporter) -> None:
        self.path = path
        self.reporter = reporter

    def step(self) -> None:
        self.reporter.offer((report_for(parse_reply(ask(self.path))), ""))


def run(focus: HyprlandFocus, sleep=time.sleep, iterations: int | None = None) -> None:
    count = 0
    while iterations is None or count < iterations:
        focus.step()
        count += 1
        sleep(POLL_SECONDS)


def serve(path: str, send, verbose: bool = False, sleep=time.sleep, iterations=None) -> int:
    reporter = FocusReporter(send)
    if verbose:

        def announce(reported: str, second: str) -> None:
            matched = resolve_profile(reported)
            label = matched.identifier if matched is not None else "UNMATCHED"
            print(f"{PROGRAM}: focus -> {label}", flush=True)
            send(reported, second)

        reporter.send = announce

    try:
        run(HyprlandFocus(path, reporter), sleep, iterations)
    except KeyboardInterrupt:
        return 0
    except Unsupported as error:
        print(f"{PROGRAM}: {error}", file=sys.stderr)
        return UNSUPPORTED
    except OSError as error:
        reporter.offer(("", ""))
        print(f"{PROGRAM}: lost the Hyprland socket: {type(error).__name__}", file=sys.stderr)
        return 1
    return 0


def main(argv: list[str] | None = None, environ=os.environ) -> int:
    parser = argparse.ArgumentParser(
        description="Report Hyprland focus to the Corne Arcane daemon (0.56+, Lua config)."
    )
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args(argv)

    path = socket_path(environ)
    if not path:
        print(
            f"{PROGRAM}: ${SIGNATURE_VARIABLE} or $XDG_RUNTIME_DIR is not set;"
            " is this a Hyprland session?",
            file=sys.stderr,
        )
        return 2
    try:
        send = _connect()
    except (ImportError, ValueError) as error:
        print(f"{PROGRAM}: PyGObject/Gio is required: {error}", file=sys.stderr)
        return 2
    except Exception as error:
        print(f"{PROGRAM}: no session bus: {error}", file=sys.stderr)
        return 2
    return serve(path, send, args.verbose)


if __name__ == "__main__":
    raise SystemExit(main())
