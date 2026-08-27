from __future__ import annotations

import io
import json
import shlex
import struct
import subprocess
import unittest
from pathlib import Path

from arcane_host.adapters import SemanticAdapters
from arcane_host.browser_bridge import decode_message, read_message, write_reply
from arcane_host.policy import NotificationPolicy
from arcane_host.profiles import resolve_profile
from arcane_host.protocol import Floor, Intensity, Scene, Secondary
from arcane_host.semantic import SemanticResolver

ROOT = Path(__file__).parents[1]


class BrowserActivityTests(unittest.TestCase):
    def test_coalesces_to_four_hz_expires_and_obeys_precedence(self) -> None:
        now = [0.0]
        changes: list[float] = []
        resolver = SemanticResolver()
        adapters = SemanticAdapters(
            resolver, NotificationPolicy(), lambda: changes.append(now[0]), lambda: now[0]
        )

        adapters.browser(Secondary.SCROLL, Intensity.ACTIVE)
        self.assertEqual(
            (resolver.state.civic.secondary, resolver.state.civic.intensity),
            (Secondary.SCROLL, Intensity.ACTIVE),
        )
        now[0] = 0.10
        adapters.browser(Secondary.TAB, Intensity.BUSY)
        now[0] = 0.20
        adapters.browser(Secondary.PAGE, Intensity.SATURATED)
        self.assertEqual(resolver.state.civic.secondary, Secondary.SCROLL)
        self.assertEqual(adapters.next_deadline(now[0]), 0.25)

        now[0] = 0.25
        adapters.poll(now[0])
        self.assertEqual(
            (resolver.state.civic.secondary, resolver.state.civic.intensity),
            (Secondary.PAGE, Intensity.SATURATED),
        )

        resolver.update(media_playing=True, transfer_active=True, system_alert=True)
        self.assertEqual(resolver.state.civic.secondary, Secondary.SYSTEM)
        resolver.update(system_alert=False)
        self.assertEqual(resolver.state.civic.secondary, Secondary.TRANSFER)
        resolver.update(transfer_active=False, pomodoro=True)
        self.assertEqual(resolver.state.civic.secondary, Secondary.CALENDAR)
        resolver.update(pomodoro=False)
        self.assertEqual(resolver.state.civic.secondary, Secondary.PAGE)

        now[0] = 1.70
        adapters.poll(now[0])
        self.assertEqual(resolver.state.civic.secondary, Secondary.MEDIA)
        self.assertEqual(resolver.state.civic.intensity, Intensity.CALM)
        self.assertGreaterEqual(len(changes), 3)

    def test_native_messages_are_exact_bounded_enums(self) -> None:
        self.assertEqual(
            decode_message(b'{"kind":"scroll","intensity":3}'),
            (Secondary.SCROLL, 3),
        )
        for value in (
            {"kind": "scroll", "intensity": 1, "url": "https://example.invalid"},
            {"kind": "history", "intensity": 1},
            {"kind": "tab", "intensity": True},
            {"kind": "page", "intensity": 4},
            ["scroll", 1],
        ):
            with self.subTest(value=value), self.assertRaises(ValueError):
                decode_message(json.dumps(value).encode())

        framed = struct.pack("=I", 2) + b"{}"
        self.assertEqual(read_message(io.BytesIO(framed)), b"{}")
        with self.assertRaisesRegex(ValueError, "bounded"):
            read_message(io.BytesIO(struct.pack("=I", 1025)))
        output = io.BytesIO()
        write_reply(output, True)
        size = struct.unpack("=I", output.getvalue()[:4])[0]
        self.assertEqual(output.getvalue()[4 : 4 + size], b'{"accepted":true}')


class OptionalAssetTests(unittest.TestCase):
    def test_bash_composes_prompt_array_debug_trap_and_is_idempotent(self) -> None:
        hook = shlex.quote(str(ROOT / "bash" / "corne-arcane.bash"))
        program = f"""
PROMPT_COMMAND=(existing_one existing_two)
trap ': existing_debug' DEBUG
source {hook}
source {hook}
declare -p PROMPT_COMMAND
declare -p PS0
trap -p DEBUG
printf 'ps0-visible=%s\\n' "${{PS0@P}}"
printf 'started=%s\\n' "$_corne_arcane_started_ms"
"""
        result = subprocess.run(
            ["bash", "--noprofile", "--norc", "-c", program],
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(
            'declare -a PROMPT_COMMAND=([0]="_corne_arcane_precmd" '
            '[1]="existing_one" [2]="existing_two")',
            result.stdout,
        )
        self.assertEqual(result.stdout.count("_corne_arcane_precmd"), 1)
        self.assertIn("_corne_arcane_preexec", result.stdout)
        self.assertIn("existing_debug", result.stdout)
        self.assertIn("ps0-visible=\n", result.stdout)
        started = next(line for line in result.stdout.splitlines() if line.startswith("started="))
        self.assertGreater(int(started.removeprefix("started=")), 0)

    def test_shell_hooks_transmit_only_normalized_values(self) -> None:
        for relative in (
            "zsh/corne-arcane.zsh",
            "bash/corne-arcane.bash",
            "fish/conf.d/corne-arcane.fish",
        ):
            text = (ROOT / relative).read_text()
            with self.subTest(relative=relative):
                self.assertIn("/proc/uptime", text)
                self.assertIn("corne-arcane-event terminal", text)
                self.assertIn("corne-arcane-event git", text)
                self.assertIn("--porcelain", text)
                for forbidden in ("BASH_COMMAND", "history", "$PWD", "commandline"):
                    self.assertNotIn(forbidden, text)

    def test_gnome_and_firefox_sources_never_read_sensitive_context(self) -> None:
        gnome = (ROOT / "gnome" / "extension.js").read_text()
        self.assertIn("get_wm_class", gnome)
        self.assertIn("get_id", gnome)
        self.assertIn("catch", gnome)
        self.assertNotIn("get_title", gnome)

        browser = "\n".join(
            (ROOT / "firefox" / name).read_text()
            for name in ("background.js", "scroll.js", "manifest.json")
        )
        for forbidden in (
            ".url",
            ".title",
            "history.",
            "webRequest",
            "forms",
            "referrer",
        ):
            self.assertNotIn(forbidden, browser)
        self.assertIn("{kind, intensity}", browser)

    def test_optional_assets_are_packaged_but_not_auto_enabled(self) -> None:
        package = (ROOT / "package.nix").read_text()
        for asset in (
            "bash/corne-arcane.bash",
            "fish/conf.d/corne-arcane.fish",
            "firefox/manifest.json",
            "gnome/metadata.json",
            "corne-arcane-browser-bridge",
        ):
            self.assertIn(asset, package)
        self.assertNotIn("gnome-extensions enable", package)
        self.assertNotIn("browser.runtime.install", package)


class BroadProfileTests(unittest.TestCase):
    def test_scriptorium_studio_and_code_combinations(self) -> None:
        for application in (
            "libreoffice-writer",
            "obsidian",
            "zettlr",
            "joplin",
            "typora",
            "ghostwriter",
            "focuswriter",
        ):
            profile = resolve_profile(application)
            self.assertEqual((profile.scene, profile.floor), (Scene.DUEL, Floor.RESEARCH))
        for application in (
            "krita",
            "blender",
            "inkscape",
            "gimp",
            "kdenlive",
            "audacity",
            "ardour",
            "spotify",
        ):
            profile = resolve_profile(application)
            self.assertEqual((profile.scene, profile.floor), (Scene.ARCHIVE, Floor.COMMONS))
        for application in ("code", "vscodium", "zed", "jetbrains-idea"):
            profile = resolve_profile(application)
            self.assertEqual((profile.scene, profile.floor), (Scene.DUEL, Floor.WORKSHOP))


if __name__ == "__main__":
    unittest.main()
