"""Canonical privacy-safe application identities and presentation profiles."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import PurePath

from .protocol import Category, Floor, Scene


@dataclass(frozen=True, slots=True)
class ApplicationProfile:
    identifier: str
    aliases: frozenset[str]
    scene: Scene = Scene.DUEL
    floor: Floor = Floor.COMMONS
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
        frozenset(
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
        ),
        scene=Scene.ARCHIVE,
        floor=Floor.RESEARCH,
    ),
    ApplicationProfile(
        "terminal",
        frozenset(
            {
                "konsole",
                "org.kde.konsole",
                "gnome-terminal",
                "org.gnome.terminal",
                "kitty",
                "alacritty",
                "org.alacritty",
                "wezterm",
                "org.wezfurlong.wezterm",
                "foot",
                "xterm",
            }
        ),
        scene=Scene.DUEL,
        floor=Floor.WORKSHOP,
        category_override=Category.TERMINAL,
    ),
    ApplicationProfile(
        "scriptorium",
        frozenset(
            {
                "libreoffice-writer",
                "libreoffice-startcenter",
                "obsidian",
                "md.obsidian.obsidian",
                "zettlr",
                "com.zettlr.zettlr",
                "joplin",
                "net.cozic.joplin-desktop",
                "typora",
                "ghostwriter",
                "org.kde.ghostwriter",
                "focuswriter",
                "org.gottcode.focuswriter",
            }
        ),
        scene=Scene.DUEL,
        floor=Floor.RESEARCH,
    ),
    ApplicationProfile(
        "studio",
        frozenset(
            {
                "krita",
                "org.kde.krita",
                "blender",
                "org.blender.blender",
                "inkscape",
                "org.inkscape.inkscape",
                "gimp",
                "org.gimp.gimp",
                "kdenlive",
                "org.kde.kdenlive",
                "audacity",
                "org.audacityteam.audacity",
                "ardour",
                "org.ardour.ardour",
                "spotify",
                "vlc",
                "org.kde.elisa",
                "mpv",
            }
        ),
        scene=Scene.ARCHIVE,
        floor=Floor.COMMONS,
    ),
    ApplicationProfile(
        "code",
        frozenset(
            {
                "code",
                "visual-studio-code",
                "codium",
                "vscodium",
                "zed",
                "dev.zed.zed",
                "sublime-text",
                "jetbrains-idea",
                "jetbrains-pycharm",
            }
        ),
        scene=Scene.DUEL,
        floor=Floor.WORKSHOP,
    ),
    ApplicationProfile(
        "communication",
        frozenset({"slack", "discord", "signal", "org.telegram.desktop", "thunderbird"}),
        scene=Scene.DUEL,
        category_override=Category.COMMUNICATION,
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
