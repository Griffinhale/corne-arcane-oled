"""Behaviour and privacy-shape tests for the Sway focus producer.

No compositor is involved. A fake IPC server on a temporary Unix socket speaks
the i3-ipc framing, answers GET_SEATS from a script and evaluates the `nop`
probes against a fake window table the way sway's criteria would. Every window
in that table carries a caption sentinel, and the seat replies carry one too, so
the tests can show the sentinel never reaches what the producer reports or
prints.
"""

from __future__ import annotations

import ast
import contextlib
import io
import json
import re
import socket
import struct
import tempfile
import threading
import unittest
from pathlib import Path

from arcane_host import focus_sway
from arcane_host.focus_sway import (
    GET_SEATS,
    RUN_COMMAND,
    build_probes,
    first_match,
    focused_node,
    frame,
    profile_pattern,
    representative,
    serve,
)
from arcane_host.profiles import PROFILES, resolve_profile

ROOT = Path(__file__).parents[1]
SOURCE = ROOT / "arcane_host" / "focus_sway.py"
SENTINEL = "SECRET-CAPTION-7f3"
PROBE = re.compile(r'^\[con_id=(\d+) (app_id|class|instance)="([^"]*)"\] nop$')


def nothing(_seconds: float) -> None:
    pass


class FakeSway:
    """A scripted stand-in for sway's IPC socket."""

    def __init__(self, focus: list[int], windows: dict[int, dict[str, str]], xwayland=True):
        self.focus = list(focus)
        self.windows = windows
        self.xwayland = xwayland
        self.messages: list[tuple[int, str]] = []
        self.violations: list[str] = []
        self.directory = tempfile.TemporaryDirectory()
        self.path = str(Path(self.directory.name) / "sway-ipc.sock")
        self.server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.server.bind(self.path)
        self.server.listen(1)
        self.thread = threading.Thread(target=self._serve, daemon=True)
        self.thread.start()

    def close(self) -> None:
        self.server.close()
        self.thread.join(timeout=5)
        self.directory.cleanup()

    def _read(self, connection, count: int) -> bytes:
        data = b""
        while len(data) < count:
            chunk = connection.recv(count - len(data))
            if not chunk:
                raise ConnectionError
            data += chunk
        return data

    def _serve(self) -> None:
        try:
            connection, _ = self.server.accept()
        except OSError:
            return
        with connection:
            try:
                while True:
                    header = self._read(connection, 14)
                    if header[:6] != b"i3-ipc":
                        self.violations.append("bad magic")
                        return
                    length, kind = struct.unpack("=II", header[6:])
                    payload = self._read(connection, length).decode()
                    self.messages.append((kind, payload))
                    reply = self.answer(kind, payload)
                    if reply is None:
                        return
                    body = json.dumps(reply).encode()
                    connection.sendall(b"i3-ipc" + struct.pack("=II", len(body), kind) + body)
            except (ConnectionError, OSError):
                return

    def answer(self, kind: int, payload: str):
        if kind == GET_SEATS:
            if not self.focus:
                return None  # sway exits
            node = self.focus.pop(0)
            # Extra keys stand in for everything a seat reply may hold.
            return [
                {
                    "name": SENTINEL,
                    "capabilities": 3,
                    "focus": node,
                    "devices": [{"identifier": SENTINEL}],
                }
            ]
        if kind == RUN_COMMAND:
            results = []
            for probe in payload.split("; "):
                match = PROBE.match(probe)
                if match is None:
                    self.violations.append(probe)
                    results.append({"success": False, "parse_error": True, "error": SENTINEL})
                    return results
                node, field, pattern = int(match[1]), match[2], match[3]
                if field != "app_id" and not self.xwayland:
                    # Sway built without Xwayland rejects the token and stops.
                    results.append(
                        {"success": False, "parse_error": False, "error": "not recognized"}
                    )
                    return results
                window = self.windows.get(node, {})
                value = window.get(field)
                if value is not None and re.search(pattern, value):
                    results.append({"success": True})
                else:
                    results.append(
                        {"success": False, "parse_error": False, "error": "No matching node."}
                    )
            return results
        self.violations.append(f"message type {kind}")
        return None


def window(**fields: str) -> dict[str, str]:
    return {"caption": SENTINEL, **fields}


class ProbeTests(unittest.TestCase):
    def test_each_profile_pattern_matches_its_own_aliases_only(self) -> None:
        for profile in PROFILES:
            pattern = profile_pattern(profile)
            self.assertTrue(pattern.startswith("(?i)^(") and pattern.endswith(")$"))
            self.assertNotIn('"', pattern)
            for alias in profile.aliases:
                self.assertTrue(re.search(pattern, alias), (profile.identifier, alias))
                # normalize_identifier folds case and "_" into "-".
                self.assertTrue(re.search(pattern, alias.upper().replace("-", "_")), alias)
            for other in PROFILES:
                if other is profile:
                    continue
                for alias in other.aliases:
                    self.assertFalse(re.search(pattern, alias), (profile.identifier, alias))

    def test_patterns_do_not_match_supersets_or_wildcards(self) -> None:
        browser = resolve_profile("org.mozilla.firefox")
        pattern = profile_pattern(browser)
        self.assertFalse(re.search(pattern, "org-mozilla-firefox"))
        self.assertFalse(re.search(pattern, "firefoxx"))
        self.assertFalse(re.search(pattern, "my-firefox"))

    def test_probes_are_nop_only_and_name_the_node(self) -> None:
        payload, order = build_probes(42, ("app_id", "class", "instance"))
        probes = payload.split("; ")
        self.assertEqual(len(probes), 3 * len(PROFILES))
        self.assertEqual(len(order), len(probes))
        for probe in probes:
            match = PROBE.match(probe)
            self.assertIsNotNone(match, probe)
            self.assertEqual(match[1], "42")

    def test_frame_refuses_every_other_message_type(self) -> None:
        self.assertTrue(frame(GET_SEATS, "").startswith(b"i3-ipc"))
        for kind in (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 100, 102):
            with self.assertRaises(ValueError):
                frame(kind, "")

    def test_seat_parsing_reads_only_the_focus_id(self) -> None:
        self.assertEqual(focused_node([{"focus": 9, "name": SENTINEL}]), 9)
        self.assertEqual(focused_node([{"focus": 0}, {"focus": 4}]), 4)
        for reply in ([], {}, None, [{"focus": "9"}], [{"focus": True}], [SENTINEL]):
            self.assertEqual(focused_node(reply), 0)

    def test_short_probe_reply_means_rejected_criteria(self) -> None:
        payload, order = build_probes(1, ("class",))
        self.assertIs(first_match([{"success": False}], order), False)
        self.assertIsNone(first_match([{"success": False}] * len(order), order))

    def test_every_report_is_a_profile_alias_or_empty(self) -> None:
        for profile in PROFILES:
            reported = representative(profile)
            self.assertIn(reported, profile.aliases)
            self.assertIs(resolve_profile(reported), profile)


class ProducerTests(unittest.TestCase):
    def run_fake(self, fake: FakeSway, verbose: bool = True):
        sent: list[tuple[str, str]] = []
        output = io.StringIO()
        errors = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
            code = serve(
                fake.path, lambda a, b: sent.append((a, b)), verbose=verbose, sleep=nothing
            )
        fake.close()
        self.assertEqual(fake.violations, [])
        for kind, payload in fake.messages:
            self.assertIn(kind, (RUN_COMMAND, GET_SEATS))
            if kind == RUN_COMMAND:
                for probe in payload.split("; "):
                    self.assertRegex(probe, PROBE)
            else:
                self.assertEqual(payload, "")
        self.assertNotIn(SENTINEL, repr(sent))
        self.assertNotIn(SENTINEL, output.getvalue())
        self.assertNotIn(SENTINEL, errors.getvalue())
        return code, sent, output.getvalue()

    def test_full_path_reports_profiles_and_nothing_else(self) -> None:
        fake = FakeSway(
            focus=[0, 5, 5, 6, 7, 8, 0],
            windows={
                5: window(app_id="org.mozilla.firefox"),
                6: window(app_id=SENTINEL),
                7: window(**{"class": "Code - OSS", "instance": "code"}),
                8: window(app_id="Alacritty"),
            },
        )
        code, sent, output = self.run_fake(fake)
        # The fake hangs up when its script ends, as sway does when it exits.
        self.assertEqual(code, 1)
        profiles = [resolve_profile(cls) for cls, _ in sent]
        self.assertEqual(
            [p.identifier if p else None for p in profiles],
            [None, "browser", None, "code", "terminal", None],
        )
        self.assertTrue(all(second == "" for _, second in sent))
        self.assertIn("focus -> browser", output)
        self.assertIn("UNMATCHED", output)
        # Unchanged focus is not probed again.
        probes = [payload for kind, payload in fake.messages if kind == RUN_COMMAND]
        self.assertEqual(sum("con_id=5 " in payload for payload in probes), 1)

    def test_a_build_without_xwayland_stops_receiving_x11_probes(self) -> None:
        fake = FakeSway(
            focus=[3, 4, 3],
            windows={3: window(app_id="unknown.app"), 4: window(app_id="other.app")},
            xwayland=False,
        )
        _code, sent, _output = self.run_fake(fake, verbose=False)
        x11 = [p for k, p in fake.messages if k == RUN_COMMAND and "class=" in p]
        self.assertEqual(len(x11), 1, "after one rejection the X11 fields are not sent again")
        self.assertEqual(sent, [("", "")])

    def test_an_unreachable_socket_fails_soft(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            errors = io.StringIO()
            with contextlib.redirect_stderr(errors):
                code = serve(str(Path(directory) / "absent"), lambda *_: None, sleep=nothing)
        self.assertEqual(code, 1)

    def test_main_refuses_without_a_sway_session(self) -> None:
        errors = io.StringIO()
        with contextlib.redirect_stderr(errors):
            self.assertEqual(focus_sway.main([], environ={}), 2)
        self.assertIn("SWAYSOCK", errors.getvalue())


class PrivacyShapeTests(unittest.TestCase):
    """The boundary is structural: the source cannot name a caption-bearing request.

    Reviewers should be able to confirm the guarantee by reading the file, which
    is only true while these strings stay absent from it, comments included.
    """

    def setUp(self) -> None:
        self.source = SOURCE.read_text()
        self.tree = ast.parse(self.source)

    def test_forbidden_requests_and_keys_are_unnameable(self) -> None:
        lowered = self.source.lower()
        for forbidden in ("get_tree", "subscribe", "tit" + "le", '"na' + 'me"', "get_marks"):
            self.assertNotIn(forbidden, lowered, f"{forbidden} must not be reachable")

    def test_only_run_command_and_get_seats_exist(self) -> None:
        self.assertEqual(focus_sway.ALLOWED_TYPES, frozenset({0, 101}))
        self.assertEqual((RUN_COMMAND, GET_SEATS), (0, 101))
        integer_constants = {
            node.targets[0].id: node.value.value
            for node in self.tree.body
            if isinstance(node, ast.Assign)
            and isinstance(node.targets[0], ast.Name)
            and isinstance(node.value, ast.Constant)
            and type(node.value.value) is int
        }
        self.assertEqual(
            {k: v for k, v in integer_constants.items() if v in range(0, 200)},
            {"RUN_COMMAND": 0, "GET_SEATS": 101},
        )
        for call in ast.walk(self.tree):
            if isinstance(call, ast.Call) and getattr(call.func, "id", None) in (
                "exchange",
                "frame",
            ):
                if getattr(call.func, "id", None) == "frame" and len(call.args) == 2:
                    if isinstance(call.args[0], ast.Name) and call.args[0].id == "kind":
                        continue
                kind = call.args[1] if call.func.id == "exchange" else call.args[0]
                self.assertIsInstance(kind, ast.Name)
                self.assertIn(kind.id, ("RUN_COMMAND", "GET_SEATS"))

    def test_the_only_command_verb_is_nop(self) -> None:
        self.assertEqual(focus_sway.NOP, "nop")
        strings = [
            node.value
            for node in ast.walk(self.tree)
            if isinstance(node, ast.Constant) and isinstance(node.value, str)
        ]
        self.assertEqual(strings.count("nop"), 1)
        # The probe f-string is the only place a command is composed.
        composed = [
            node
            for node in ast.walk(self.tree)
            if isinstance(node, ast.JoinedStr)
            and any(
                isinstance(v, ast.FormattedValue) and getattr(v.value, "id", "") == "NOP"
                for v in node.values
            )
        ]
        self.assertEqual(len(composed), 1)
        self.assertEqual(self.source.count(".sendall("), 1)


if __name__ == "__main__":
    unittest.main()
