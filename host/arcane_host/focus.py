"""Privacy-bounded application focus classification and arbitration."""

from __future__ import annotations

import hashlib
import secrets
from typing import Callable

from .protocol import Floor, Scene
from .profiles import PROFILES, canonical_identifier, normalize_identifier, resolve_profile


BROWSER_ALIASES = next(profile.aliases for profile in PROFILES if profile.identifier == "browser")
TERMINAL_ALIASES = next(profile.aliases for profile in PROFILES if profile.identifier == "terminal")


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
    profile = resolve_profile(resource_class, desktop_file_name)
    return profile.scene if profile is not None else Scene.DUEL


def classify_floor(resource_class: str | None, desktop_file_name: str | None) -> Floor:
    """Twin Cities floor for the focused window; unknown apps live in COMMONS."""
    profile = resolve_profile(resource_class, desktop_file_name)
    return profile.floor if profile is not None else Floor.COMMONS


class FocusArbiter:
    """Settle focus reports while retaining only salted identifier digests."""

    def __init__(
        self,
        settle_seconds: float = 0.2,
        identifier_digest: Callable[[str], bytes] | None = None,
    ) -> None:
        self.settle_seconds = settle_seconds
        self.scene = Scene.DUEL
        self.floor = Floor.COMMONS
        if identifier_digest is None:
            salt = secrets.token_bytes(16)
            def digest_identifier(value: str) -> bytes:
                return hashlib.blake2s(
                    value.encode("utf-8", "surrogatepass"), key=salt, digest_size=16
                ).digest()
            identifier_digest = digest_identifier
        self._identifier_digest = identifier_digest
        self.terminal_focused = False
        self.focused_digests: frozenset[bytes] = frozenset()
        self._pending: tuple[Scene, Floor, bool, frozenset[bytes]] | None = None
        self._deadline = 0.0

    def report(self, resource_class: str | None, desktop_file_name: str | None, now: float) -> None:
        target = classify_window(resource_class, desktop_file_name)
        floor = classify_floor(resource_class, desktop_file_name)
        terminal = is_terminal(resource_class, desktop_file_name)
        normalized = {
            canonical_identifier(identifier)
            for identifier in (resource_class, desktop_file_name)
            if canonical_identifier(identifier)
        }
        digests = frozenset(self._identifier_digest(identifier) for identifier in normalized)
        self._pending = (target, floor, terminal, digests)
        self._deadline = now + self.settle_seconds

    def poll(self, now: float) -> Scene:
        if self._pending is not None and now >= self._deadline:
            self.scene, self.floor, self.terminal_focused, self.focused_digests = self._pending
            self._pending = None
        return self.scene

    def matches_focused(self, identifier_digest: bytes) -> bool:
        return identifier_digest in self.focused_digests

    def next_deadline(self) -> float | None:
        return self._deadline if self._pending is not None else None
