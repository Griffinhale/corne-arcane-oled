"""Behaviour and privacy-shape tests for the Hyprland focus producer.

No compositor is involved. A fake command socket in a temporary directory
records each request and answers from a script, one reply per connection as
Hyprland does.
"""

from __future__ import annotations

import ast
import contextlib
import io
import socket
import tempfile
import threading
import unittest
from pathlib import Path

from arcane_host import focus_hyprland
from arcane_host.focus_hyprland import (
    MARKER,
    REQUEST,
    UNSUPPORTED,
    Unsupported,
    parse_reply,
    report_for,
    serve,
    socket_path,
)
from arcane_host.profiles import PROFILES, resolve_profile

ROOT = Path(__file__).parents[1]
SOURCE = ROOT / "arcane_host" / "focus_hyprland.py"
SENTINEL = "SECRET-CAPTION-7f3"
LEGACY = "eval is only supported with the lua config manager"


def nothing(_seconds: float) -> None:
    pass


class FakeHyprland:
    def __init__(self, replies: list[str]) -> None:
        self.replies = list(replies)
        self.requests: list[str] = []
        self.directory = tempfile.TemporaryDirectory()
        self.path = str(Path(self.directory.name) / ".socket.sock")
        self.server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.server.bind(self.path)
        self.server.listen(4)
        self.thread = threading.Thread(target=self._serve, daemon=True)
        self.thread.start()

    def close(self) -> None:
        self.server.close()
        self.thread.join(timeout=5)
        self.directory.cleanup()

    def _serve(self) -> None:
        while self.replies:
            try:
                connection, _ = self.server.accept()
            except OSError:
                return
            with connection:
                self.requests.append(connection.recv(65536).decode())
                connection.sendall(self.replies.pop(0).encode())
        # Script exhausted: stop listening, as an exited compositor would.
        self.server.close()


class RequestTests(unittest.TestCase):
    def test_the_request_is_one_fixed_repl_chunk(self) -> None:
        self.assertTrue(REQUEST.startswith("repl "))
        # A "/" before the command would be read as output flags.
        self.assertNotIn("/", REQUEST)
        self.assertIn("hl.get_active_window()", REQUEST)
        self.assertIn("w.class", REQUEST)

    def test_reply_parsing(self) -> None:
        self.assertEqual(parse_reply(f"{MARKER}firefox\n"), "firefox")
        self.assertEqual(parse_reply(f"{MARKER}\n"), "")
        self.assertEqual(parse_reply("something else"), "")
        self.assertEqual(parse_reply(f"{MARKER}a\n{SENTINEL}\n"), "")
        for refusal in (LEGACY, "unknown request", "ok", "error: attempt to index a nil value"):
            with self.assertRaises(Unsupported):
                parse_reply(refusal)

    def test_every_report_is_a_profile_alias_or_empty(self) -> None:
        self.assertEqual(report_for(SENTINEL), "")
        self.assertEqual(report_for(""), "")
        for profile in PROFILES:
            for alias in profile.aliases:
                reported = report_for(alias)
                self.assertIn(reported, profile.aliases)
                self.assertIs(resolve_profile(reported), profile)

    def test_socket_path_follows_the_instance_signature(self) -> None:
        env = {"XDG_RUNTIME_DIR": "/run/user/1000", "HYPRLAND_INSTANCE_SIGNATURE": "abc_1_2"}
        self.assertEqual(socket_path(env), "/run/user/1000/hypr/abc_1_2/.socket.sock")
        self.assertEqual(socket_path({"XDG_RUNTIME_DIR": "/run/user/1000"}), "")
        self.assertEqual(socket_path({**env, "HYPRLAND_INSTANCE_SIGNATURE": "../x"}), "")


class ProducerTests(unittest.TestCase):
    def run_fake(self, replies: list[str], verbose: bool = True):
        fake = FakeHyprland(replies)
        sent: list[tuple[str, str]] = []
        output = io.StringIO()
        errors = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
            code = serve(
                fake.path, lambda a, b: sent.append((a, b)), verbose=verbose, sleep=nothing
            )
        fake.close()
        self.assertTrue(fake.requests)
        self.assertEqual(set(fake.requests), {REQUEST}, "only the one request is ever sent")
        self.assertNotIn(SENTINEL, repr(sent))
        self.assertNotIn(SENTINEL, output.getvalue())
        self.assertNotIn(SENTINEL, errors.getvalue())
        return code, sent, output.getvalue(), errors.getvalue()

    def test_full_path_reports_profiles_and_nothing_else(self) -> None:
        code, sent, output, _ = self.run_fake(
            [
                f"{MARKER}firefox\n",
                f"{MARKER}firefox\n",
                f"{MARKER}{SENTINEL}\n",
                f"{MARKER}kitty\n",
                f"{MARKER}\n",
            ]
        )
        self.assertEqual(code, 1, "the fake stops answering, as an exited compositor does")
        self.assertEqual(
            [resolve_profile(cls).identifier if resolve_profile(cls) else None for cls, _ in sent],
            ["browser", None, "terminal", None],
        )
        self.assertTrue(all(second == "" for _, second in sent))
        self.assertIn("focus -> browser", output)

    def test_legacy_config_exits_with_a_message_and_no_fallback(self) -> None:
        code, sent, _, errors = self.run_fake([LEGACY])
        self.assertEqual(code, UNSUPPORTED)
        self.assertEqual(sent, [])
        self.assertIn("Lua config", errors)

    def test_an_unreachable_socket_fails_soft(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            with contextlib.redirect_stderr(io.StringIO()):
                code = serve(str(Path(directory) / "absent"), lambda *_: None, sleep=nothing)
        self.assertEqual(code, 1)

    def test_main_refuses_without_a_hyprland_session(self) -> None:
        errors = io.StringIO()
        with contextlib.redirect_stderr(errors):
            self.assertEqual(focus_hyprland.main([], environ={}), 2)
        self.assertIn("HYPRLAND_INSTANCE_SIGNATURE", errors.getvalue())


class PrivacyShapeTests(unittest.TestCase):
    """The boundary is structural: the source cannot name a caption-bearing request.

    The event socket, the window queries and JSON output flags are absent from
    the file, comments included, and the one request is a constant.
    """

    def setUp(self) -> None:
        self.source = SOURCE.read_text()
        self.tree = ast.parse(self.source)

    def test_forbidden_sockets_and_queries_are_unnameable(self) -> None:
        lowered = self.source.lower()
        for forbidden in (
            "socket2",
            "active" + "window",
            "clie" + "nts",
            "j/",
            "tit" + "le",
            "dispatch",
            "exec",
            "hyprctl",
        ):
            self.assertNotIn(forbidden, lowered, f"{forbidden} must not be reachable")

    def test_the_only_bytes_sent_are_the_request_constant(self) -> None:
        sends = [
            node
            for node in ast.walk(self.tree)
            if isinstance(node, ast.Call)
            and isinstance(node.func, ast.Attribute)
            and node.func.attr in ("send", "sendall", "sendto", "sendmsg")
        ]
        self.assertEqual(len(sends), 1)
        argument = sends[0].args[0]
        # REQUEST.encode("utf-8")
        self.assertIsInstance(argument, ast.Call)
        self.assertEqual(argument.func.value.id, "REQUEST")
        assigned = [
            node
            for node in ast.walk(self.tree)
            if isinstance(node, ast.Assign)
            and any(isinstance(t, ast.Name) and t.id == "REQUEST" for t in node.targets)
        ]
        self.assertEqual(len(assigned), 1)


if __name__ == "__main__":
    unittest.main()
