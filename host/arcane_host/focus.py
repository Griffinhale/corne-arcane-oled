"""Privacy-bounded application focus classification and arbitration."""

from __future__ import annotations

from pathlib import PurePath

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


def classify_window(resource_class: str | None, desktop_file_name: str | None) -> Scene:
    return Scene.ARCHIVE if is_browser(resource_class, desktop_file_name) else Scene.DUEL


class FocusArbiter:
    """Settle focus reports before exposing a scene, retaining no identifiers."""

    def __init__(self, settle_seconds: float = 0.2) -> None:
        self.settle_seconds = settle_seconds
        self.scene = Scene.DUEL
        self._pending: Scene | None = None
        self._deadline = 0.0

    def report(self, resource_class: str | None, desktop_file_name: str | None, now: float) -> None:
        target = classify_window(resource_class, desktop_file_name)
        if target == self.scene:
            self._pending = None
            return
        self._pending = target
        self._deadline = now + self.settle_seconds

    def poll(self, now: float) -> Scene:
        if self._pending is not None and now >= self._deadline:
            self.scene = self._pending
            self._pending = None
        return self.scene
