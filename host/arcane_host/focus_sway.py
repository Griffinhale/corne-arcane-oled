"""Focus producer for Sway.

Sway's IPC has no field selection: the tree dump and the window and workspace
events serialize every property of every container, captions included, so
asking for any of them would put that text into this process before a parser
could look at it. This producer is built so that it never asks.

Privacy boundary: exactly two i3-ipc message types are ever sent.

- GET_SEATS returns, per seat, a seat label, input capabilities, input devices
  and the numeric id of the focused node. No window property is in that reply.
- RUN_COMMAND carries only `nop` probes of the form
  `[con_id=N app_id="(?i)^(alias|alias)$"] nop`, built from the aliases in
  profiles.py. Each probe answers success or a fixed "No matching node." string,
  so the reply is one bit per profile and field. `nop` does nothing.

What leaves the process is therefore the profile that matched, as one of its
own aliases, or "" -- never anything the application chose. The test suite
holds the source to that shape: it rejects the names of the tree query and the
event subscription, and the reply key the tree uses for captions, anywhere in
this file, including comments. That is why they are absent rather than listed.
"""

from __future__ import annotations

import argparse
import json
import os
import socket
import struct
import sys
import time

from .focus_x11 import FocusReporter, _connect
from .profiles import PROFILES, ApplicationProfile, resolve_profile

PROGRAM = "corne-arcane-focus-sway"
SOCKET_VARIABLE = "SWAYSOCK"

MAGIC = b"i3-ipc"
HEADER = struct.Struct("=II")
# The only two message types this module can send.
RUN_COMMAND = 0
GET_SEATS = 101
ALLOWED_TYPES = frozenset({RUN_COMMAND, GET_SEATS})

# The only command verb a probe can carry. It does nothing and succeeds.
NOP = "nop"
# Wayland-native windows match on app_id; Xwayland windows on the X11 pair.
NATIVE_FIELDS = ("app_id",)
XWAYLAND_FIELDS = ("class", "instance")
# Characters every alias in profiles.py is drawn from. Anything else is skipped
# rather than escaped, so no alias can close the quoted criteria value.
ALIAS_CHARACTERS = frozenset("abcdefghijklmnopqrstuvwxyz0123456789.+-_")

POLL_SECONDS = 0.25
# A GET_SEATS or probe reply is a few kilobytes; refuse anything absurd.
MAX_REPLY_BYTES = 1 << 20


def _alias_pattern(alias: str) -> str:
    """One alias as a PCRE2 fragment.

    normalize_identifier lowercases and maps "_" to "-" before matching, so the
    regex is case-insensitive and lets either separator stand for "-".
    """
    out = []
    for character in alias:
        if character in ".+":
            out.append("\\" + character)
        elif character in "-_":
            out.append("(-|_)")
        else:
            out.append(character)
    return "".join(out)


def profile_pattern(profile: ApplicationProfile) -> str:
    fragments = sorted(
        _alias_pattern(alias)
        for alias in profile.aliases
        if alias and set(alias) <= ALIAS_CHARACTERS
    )
    return "(?i)^(" + "|".join(fragments) + ")$"


def representative(profile: ApplicationProfile) -> str:
    """The string reported for a matched profile.

    FocusArbiter resolves what it is given through the alias table, and most
    profile identifiers are not aliases of themselves, so the producer reports
    one fixed alias of the profile instead. Every alias of a profile collapses
    to the same canonical identity in the arbiter, so which one is immaterial.
    """
    if profile.identifier in profile.aliases:
        return profile.identifier
    return min(profile.aliases)


def build_probes(node: int, fields: tuple[str, ...]) -> tuple[str, list[ApplicationProfile]]:
    """One RUN_COMMAND payload: a `nop` probe per field and profile.

    Probes are ordered by field first, so a native app_id answer outranks an
    X11 class, which outranks an instance, matching resolve_profile's order.
    """
    probes: list[str] = []
    order: list[ApplicationProfile] = []
    for field in fields:
        for profile in PROFILES:
            probes.append(f'[con_id={int(node)} {field}="{profile_pattern(profile)}"] {NOP}')
            order.append(profile)
    return "; ".join(probes), order


def frame(kind: int, payload: str) -> bytes:
    if kind not in ALLOWED_TYPES:
        raise ValueError(f"message type {kind} is not one this producer sends")
    body = payload.encode("utf-8")
    return MAGIC + HEADER.pack(len(body), kind) + body


def _receive_exactly(connection, count: int) -> bytes:
    chunks = []
    while count:
        chunk = connection.recv(min(count, 65536))
        if not chunk:
            raise ConnectionError("sway closed the IPC socket")
        chunks.append(chunk)
        count -= len(chunk)
    return b"".join(chunks)


def exchange(connection, kind: int, payload: str = ""):
    """Send one message and return its decoded JSON reply."""
    connection.sendall(frame(kind, payload))
    header = _receive_exactly(connection, len(MAGIC) + HEADER.size)
    if header[: len(MAGIC)] != MAGIC:
        raise ConnectionError("not an i3-ipc reply")
    length, reply_kind = HEADER.unpack(header[len(MAGIC) :])
    if reply_kind != kind or length > MAX_REPLY_BYTES:
        raise ConnectionError("unexpected i3-ipc reply")
    return json.loads(_receive_exactly(connection, length))


def focused_node(seats) -> int:
    """The focused node id of the first seat that has one, or 0."""
    if not isinstance(seats, list):
        return 0
    for seat in seats:
        focus = seat.get("focus") if isinstance(seat, dict) else None
        if isinstance(focus, int) and not isinstance(focus, bool) and focus > 0:
            return focus
    return 0


def first_match(results, order: list[ApplicationProfile]) -> ApplicationProfile | None | bool:
    """The first profile whose probe succeeded.

    Returns False when the reply does not hold one result per probe, which is
    what sway sends when it rejects the criteria outright (a build without
    Xwayland does not know the X11 fields) and stops running the list.
    """
    if not isinstance(results, list) or len(results) != len(order):
        return False
    for profile, result in zip(order, results):
        if isinstance(result, dict) and result.get("success") is True:
            return profile
    return None


class SwayFocus:
    """Poll GET_SEATS and probe the focused node when it changes."""

    def __init__(self, connection, reporter: FocusReporter) -> None:
        self.connection = connection
        self.reporter = reporter
        self.node: int | None = None
        self.xwayland = True

    def identify(self, node: int) -> str:
        if node <= 0:
            return ""
        payload, order = build_probes(node, NATIVE_FIELDS)
        matched = first_match(exchange(self.connection, RUN_COMMAND, payload), order)
        if matched:
            return representative(matched)
        if self.xwayland:
            payload, order = build_probes(node, XWAYLAND_FIELDS)
            matched = first_match(exchange(self.connection, RUN_COMMAND, payload), order)
            if matched is False:
                self.xwayland = False
            elif matched:
                return representative(matched)
        return ""

    def step(self) -> None:
        node = focused_node(exchange(self.connection, GET_SEATS))
        if node == self.node:
            return
        self.node = node
        self.reporter.offer((self.identify(node), ""))


def run(focus: SwayFocus, sleep=time.sleep, iterations: int | None = None) -> None:
    count = 0
    while iterations is None or count < iterations:
        focus.step()
        count += 1
        sleep(POLL_SECONDS)


def main(argv: list[str] | None = None, environ=os.environ) -> int:
    parser = argparse.ArgumentParser(description="Report Sway focus to the Corne Arcane daemon.")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args(argv)

    path = environ.get(SOCKET_VARIABLE, "")
    if not path:
        print(f"{PROGRAM}: ${SOCKET_VARIABLE} is not set; is this a Sway session?", file=sys.stderr)
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
        connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        connection.settimeout(2.0)
        connection.connect(path)
    except OSError as error:
        print(f"{PROGRAM}: cannot reach the Sway IPC socket: {error}", file=sys.stderr)
        return 1
    try:
        run(SwayFocus(connection, reporter), sleep, iterations)
    except KeyboardInterrupt:
        return 0
    except (OSError, ValueError) as error:
        # Sway went away or answered something unexpected. Report nothing
        # focused so the daemon does not keep a stale scene, and let the unit
        # restart us.
        reporter.offer(("", ""))
        print(f"{PROGRAM}: lost the Sway IPC socket: {type(error).__name__}", file=sys.stderr)
        return 1
    finally:
        connection.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
