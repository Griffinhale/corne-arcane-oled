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


# Aliases are matched exactly after normalize_identifier, against whatever a
# focus producer reports: a KWin resourceClass, a GNOME desktop id, or on X11 a
# WM_CLASS pair and _GTK_APPLICATION_ID. That is why the same application
# appears under several spellings -- packaging, WM_CLASS and desktop-file
# identities disagree constantly, and guessing between them is what the alias
# sets exist to avoid.
#
# Scene and Floor are wire values shared with the firmware, so profiles pick
# from what already exists rather than adding to it. Scene.FOCUS and
# Floor.SPECIAL are unavailable here: both are owned by the Pomodoro ritual
# (semantic.py) and read by the firmware as the Observatory flag, so assigning
# either would make an ordinary window impersonate a ritual.
PROFILES = (
    ApplicationProfile(
        "browser",
        frozenset(
            {
                "firefox",
                "firefox-esr",
                "org.mozilla.firefox",
                "librewolf",
                "io.gitlab.librewolf-community",
                "google-chrome",
                "google-chrome-stable",
                "chromium",
                "chromium-browser",
                "org.chromium.chromium",
                "microsoft-edge",
                "opera",
                "brave",
                "brave-browser",
                "com.brave.browser",
                "vivaldi",
                "vivaldi-stable",
                "com.vivaldi.vivaldi",
                "zen",
                "zen-browser",
                "app.zen-browser.zen",
                "epiphany",
                "org.gnome.epiphany",
                "falkon",
                "org.kde.falkon",
                "tor-browser",
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
                "org.gnome.console",
                "kgx",
                "org.gnome.ptyxis",
                "xfce4-terminal",
                "mate-terminal",
                "tilix",
                "terminator",
                "kitty",
                "alacritty",
                "org.alacritty",
                "wezterm",
                "org.wezfurlong.wezterm",
                "foot",
                "xterm",
                "urxvt",
                "rxvt-unicode",
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
                "libreoffice-calc",
                "libreoffice-impress",
                "libreoffice-draw",
                # LibreOffice reports this as its X11 class whatever the module.
                "soffice",
                "abiword",
                "xed",
                "gedit",
                "org.gnome.gedit",
                "org.gnome.texteditor",
                "mousepad",
                "org.xfce.mousepad",
                "pluma",
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
                "darktable",
                "kdenlive",
                "org.kde.kdenlive",
                "obs",
                "com.obsproject.studio",
                "audacity",
                "org.audacityteam.audacity",
                "ardour",
                "org.ardour.ardour",
                "spotify",
                "rhythmbox",
                "org.gnome.rhythmbox",
                "vlc",
                "org.kde.elisa",
                "mpv",
                "celluloid",
                "io.github.celluloid-player.celluloid",
                "totem",
                "org.gnome.totem",
                "xplayer",
                "pix",
                "xviewer",
                "eog",
                "org.gnome.eog",
                "loupe",
                "org.gnome.loupe",
                "shotwell",
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
                "kate",
                "org.kde.kate",
                "kdevelop",
                "org.kde.kdevelop",
                "geany",
                "emacs",
                "gvim",
                "android-studio",
                "jetbrains-idea",
                "jetbrains-pycharm",
                "jetbrains-webstorm",
                "jetbrains-clion",
                "jetbrains-goland",
                "jetbrains-phpstorm",
                "jetbrains-rubymine",
                "jetbrains-datagrip",
                "jetbrains-rider",
            }
        ),
        scene=Scene.DUEL,
        floor=Floor.WORKSHOP,
    ),
    ApplicationProfile(
        "communication",
        frozenset(
            {
                "slack",
                "discord",
                "signal",
                # normalize_identifier strips a .desktop suffix, so the alias
                # has to be written in the form a lookup can actually reach.
                "org.telegram",
                "telegram-desktop",
                "telegramdesktop",
                "thunderbird",
                "org.mozilla.thunderbird",
                "evolution",
                "org.gnome.evolution",
                "geary",
                "org.gnome.geary",
                "hexchat",
                "element",
                "io.element",
                "pidgin",
                "mumble",
                "zoom",
            }
        ),
        scene=Scene.DUEL,
        category_override=Category.COMMUNICATION,
    ),
    ApplicationProfile(
        "files",
        frozenset(
            {
                "nemo",
                "nautilus",
                "org.gnome.nautilus",
                "dolphin",
                "org.kde.dolphin",
                "thunar",
                "caja",
                "pcmanfm",
                "pcmanfm-qt",
                "io.elementary.files",
                "file-roller",
                "org.gnome.fileroller",
                "xarchiver",
                "baobab",
                "org.gnome.baobab",
            }
        ),
        scene=Scene.ARCHIVE,
        floor=Floor.COMMONS,
    ),
    ApplicationProfile(
        # System maintenance, not combat: the one Scene/Floor pair no other
        # profile occupies, so adjusting the machine reads differently from
        # working on it.
        "settings",
        frozenset(
            {
                "cinnamon-settings",
                "cinnamon-control-center",
                "gnome-control-center",
                "org.gnome.settings",
                "gnome-tweaks",
                "org.gnome.tweaks",
                "systemsettings",
                "org.kde.systemsettings",
                "xfce4-settings-manager",
                "mate-control-center",
                "mintinstall",
                "mintupdate",
                "mintsources",
                "synaptic",
                "gnome-software",
                "org.gnome.software",
                "gnome-disks",
                "org.gnome.diskutility",
                "pavucontrol",
                "blueman-manager",
                "timeshift-gtk",
                "gufw",
            }
        ),
        scene=Scene.ARCHIVE,
        floor=Floor.WORKSHOP,
        category_override=Category.SYSTEM,
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
