"""Canonical privacy-safe application identities and presentation profiles."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import PurePath

from .protocol import Category, Scene


@dataclass(frozen=True, slots=True)
class ApplicationProfile:
    identifier: str
    aliases: frozenset[str]
    scene: Scene = Scene.DUEL
    category_override: Category | None = None
    suppress_when_focused: bool = True


def normalize_identifier(value: str | None) -> str:
    normalized = (value or "").strip().lower()
    if "/" in normalized:
        normalized = PurePath(normalized).name
    if normalized.endswith(".desktop"):
        normalized = normalized[:-8]
    return normalized.replace("_", "-")


PROFILES = (
    ApplicationProfile(
        "browser",
        frozenset({"firefox", "firefox-esr", "org.mozilla.firefox", "google-chrome",
                   "google-chrome-stable", "chromium", "chromium-browser",
                   "org.chromium.chromium", "brave", "brave-browser", "com.brave.browser",
                   "vivaldi", "vivaldi-stable", "com.vivaldi.vivaldi", "zen", "zen-browser",
                   "app.zen-browser.zen"}),
        scene=Scene.ARCHIVE,
    ),
    ApplicationProfile(
        "terminal",
        frozenset({"konsole", "org.kde.konsole", "gnome-terminal", "org.gnome.terminal",
                   "kitty", "alacritty", "org.alacritty", "wezterm",
                   "org.wezfurlong.wezterm", "foot", "xterm"}),
        scene=Scene.DUEL,
        category_override=Category.TERMINAL,
    ),
    ApplicationProfile(
        "communication",
        frozenset({"slack", "discord", "signal", "org.telegram.desktop", "thunderbird"}),
        scene=Scene.DUEL,
        category_override=Category.COMMUNICATION,
    ),
    ApplicationProfile(
        "media",
        frozenset({"spotify", "vlc", "org.kde.elisa", "mpv"}),
        scene=Scene.ARCHIVE,
    ),
)

_ALIASES = {alias: profile for profile in PROFILES for alias in profile.aliases}


def resolve_profile(*identifiers: str | None) -> ApplicationProfile | None:
    for identifier in identifiers:
        profile = _ALIASES.get(normalize_identifier(identifier))
        if profile is not None:
            return profile
    return None


def canonical_identifier(value: str | None) -> str:
    """Collapse packaging aliases before the session-salted privacy hash."""
    profile = resolve_profile(value)
    return profile.identifier if profile is not None else normalize_identifier(value)
