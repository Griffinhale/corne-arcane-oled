"""Privacy-bounded application focus classification and arbitration."""

from __future__ import annotations

import hashlib
from pathlib import PurePath
import secrets
from typing import Callable

from .protocol import Scene


BROWSER_ALIASES = frozenset(
    {
        "firefox",
        "firefox-esr",
        "org.mozilla.firefox",
        "google-chrome",
        "google-chrome-stable",
        "chromium",
        "chromium-browser",
        "org.chromium.chromium",
        "brave",
        "brave-browser",
        "com.brave.browser",
        "vivaldi",
        "vivaldi-stable",
        "com.vivaldi.vivaldi",
        "zen",
        "zen-browser",
        "app.zen-browser.zen",
    }
)

TERMINAL_ALIASES = frozenset(
    {
        "konsole",
        "org.kde.konsole",
        "gnome-terminal",
        "org.gnome.terminal",
        "alacritty",
        "org.alacritty",
        "kitty",
        "foot",
        "wezterm",
        "org.wezfurlong.wezterm",
        "xterm",
    }
)


def normalize_identifier(value: str | None) -> str:
    """Normalize only an application identifier; titles are never accepted."""
    if not value:
        return ""
    normalized = value.strip().lower().replace("_", "-")
    if "/" in normalized:
        normalized = PurePath(normalized).name
    if normalized.endswith(".desktop"):
        normalized = normalized[:-8]
    return normalized


def is_browser(resource_class: str | None, desktop_file_name: str | None) -> bool:
    return any(
        normalize_identifier(identifier) in BROWSER_ALIASES
        for identifier in (resource_class, desktop_file_name)
    )


def is_terminal(resource_class: str | None, desktop_file_name: str | None) -> bool:
    return any(
        normalize_identifier(identifier) in TERMINAL_ALIASES
        for identifier in (resource_class, desktop_file_name)
    )


def classify_window(resource_class: str | None, desktop_file_name: str | None) -> Scene:
    return Scene.ARCHIVE if is_browser(resource_class, desktop_file_name) else Scene.DUEL


class FocusArbiter:
    """Settle focus reports while retaining only salted identifier digests."""

    def __init__(
        self,
        settle_seconds: float = 0.2,
        identifier_digest: Callable[[str], bytes] | None = None,
    ) -> None:
        self.settle_seconds = settle_seconds
        self.scene = Scene.DUEL
        if identifier_digest is None:
            salt = secrets.token_bytes(16)
            identifier_digest = lambda value: hashlib.blake2s(
                value.encode("utf-8", "surrogatepass"), key=salt, digest_size=16
            ).digest()
        self._identifier_digest = identifier_digest
        self.terminal_focused = False
        self.focused_digests: frozenset[bytes] = frozenset()
        self._pending: tuple[Scene, bool, frozenset[bytes]] | None = None
        self._deadline = 0.0

    def report(self, resource_class: str | None, desktop_file_name: str | None, now: float) -> None:
        target = classify_window(resource_class, desktop_file_name)
        terminal = is_terminal(resource_class, desktop_file_name)
        normalized = {
            normalize_identifier(identifier)
            for identifier in (resource_class, desktop_file_name)
            if normalize_identifier(identifier)
        }
        digests = frozenset(self._identifier_digest(identifier) for identifier in normalized)
        self._pending = (target, terminal, digests)
        self._deadline = now + self.settle_seconds

    def poll(self, now: float) -> Scene:
        if self._pending is not None and now >= self._deadline:
            self.scene, self.terminal_focused, self.focused_digests = self._pending
            self._pending = None
        return self.scene

    def matches_focused(self, identifier_digest: bytes) -> bool:
        return identifier_digest in self.focused_digests
