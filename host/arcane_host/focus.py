"""Privacy-bounded application focus classification and arbitration."""

from __future__ import annotations

import hashlib
import secrets
from typing import Callable

from .profiles import normalize_identifier, resolve_profile
from .protocol import Floor, Scene


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
        normalized = tuple(
            identifier
            for value in (resource_class, desktop_file_name)
            if (identifier := normalize_identifier(value))
        )
        profile = resolve_profile(*normalized)
        target = profile.scene if profile is not None else Scene.DUEL
        floor = profile.floor if profile is not None else Floor.COMMONS
        terminal = profile is not None and profile.identifier == "terminal"
        canonical = {
            profile.identifier
            if profile is not None and identifier in profile.aliases
            else identifier
            for identifier in normalized
        }
        digests = frozenset(self._identifier_digest(identifier) for identifier in canonical)
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
