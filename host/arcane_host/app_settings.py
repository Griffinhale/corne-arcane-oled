"""The city app's settings: the Vial command, focus producers, keymap profiles.

Menu launches never see shell exports, so each setting lives where its reader
already looks. The Vial command goes in the file the Vial launcher reads
(~/.config/corne-arcane/vial). Focus producers are switched where they are
switched by hand: the X11 producer is a systemd user unit, the GNOME extension
belongs to gnome-extensions. Keymap profiles are corne-arcane-keymap's: listing
and deleting are file operations, and saving or loading runs the command, which
takes the keyboard through its own guard.
"""

from __future__ import annotations

import os
import shutil
import subprocess
from dataclasses import dataclass
from typing import Callable

from . import keymap_profiles
from .app_controls import Controls, _Child, tool_command
from .hid_ownership import SYSTEMCTL
from .vial_launcher import ENV_VAR as VIAL_ENV_VAR
from .vial_launcher import config_path as vial_config_path

X11_UNIT = "corne-arcane-focus-x11.service"
GNOME_UUID = "corne-arcane-focus@griffinhale.github.io"
VIAL_HEADER = (
    "# The command corne-arcane-vial runs, set from the Corne Arcane app.\n"
    "# A path to Vial, or a command such as 'flatpak run ...'.\n"
)

Runner = Callable[..., subprocess.CompletedProcess]


def _run(argv: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(argv, capture_output=True, text=True, timeout=10, check=False)


@dataclass
class FocusProducer:
    """One focus producer the user can turn on or off."""

    label: str
    status: list[str]
    enable: list[str]
    disable: list[str]
    enabled_when: Callable[[subprocess.CompletedProcess], bool]


def focus_producers() -> list[FocusProducer]:
    """The producers this machine can switch. KWin needs no switch: the daemon
    loads its script itself."""
    producers = [
        FocusProducer(
            "X11 focus producer (plain X11 sessions without KWin or GNOME)",
            [SYSTEMCTL, "--user", "is-enabled", "--quiet", X11_UNIT],
            [SYSTEMCTL, "--user", "enable", "--now", X11_UNIT],
            [SYSTEMCTL, "--user", "disable", "--now", X11_UNIT],
            lambda result: result.returncode == 0,
        )
    ]
    if shutil.which("gnome-extensions"):
        producers.append(
            FocusProducer(
                "GNOME Shell extension",
                ["gnome-extensions", "list", "--enabled"],
                ["gnome-extensions", "enable", GNOME_UUID],
                ["gnome-extensions", "disable", GNOME_UUID],
                lambda result: GNOME_UUID in result.stdout.split(),
            )
        )
    return producers


class Settings:
    """What the settings window can change, without any Tk in it."""

    def __init__(
        self,
        controls: Controls,
        *,
        keymap_command: list[str] | None = None,
        producers: list[FocusProducer] | None = None,
        run: Runner = _run,
    ) -> None:
        self.controls = controls
        self.keymap_command = keymap_command or tool_command("keymap", "keymap_profiles")
        self.producers = focus_producers() if producers is None else producers
        self.run = run
        self.message = ""

    # -- Vial ------------------------------------------------------------------

    def vial_command(self) -> str:
        """The command in the launcher's file; the environment is not ours to edit."""
        try:
            lines = vial_config_path().read_text().splitlines()
        except OSError:
            return ""
        for line in lines:
            line = line.strip()
            if line and not line.startswith("#"):
                return line
        return ""

    def set_vial_command(self, text: str) -> None:
        text = text.strip()
        if "\n" in text or "\r" in text:
            self.message = "The Vial command must be one line"
            return
        path = vial_config_path()
        try:
            if not text:
                path.unlink(missing_ok=True)
            else:
                path.parent.mkdir(parents=True, exist_ok=True)
                temporary = path.with_name(path.name + ".tmp")
                temporary.write_text(VIAL_HEADER + text + "\n")
                temporary.replace(path)
        except OSError as error:
            self.message = f"Could not save the Vial command: {error}"
            return
        self.message = (
            f"Open Vial now runs: {text}" if text else "Cleared; Open Vial looks for vial on PATH"
        )
        if os.environ.get(VIAL_ENV_VAR, "").strip():
            self.message += f" (but {VIAL_ENV_VAR} is set here, and it wins)"

    # -- focus producers -------------------------------------------------------

    def producer_enabled(self, producer: FocusProducer) -> bool | None:
        """On, off, or None when its tool cannot say."""
        try:
            return producer.enabled_when(self.run(producer.status))
        except (OSError, subprocess.SubprocessError):
            return None

    def set_producer(self, producer: FocusProducer, enabled: bool) -> None:
        try:
            result = self.run(producer.enable if enabled else producer.disable)
        except (OSError, subprocess.SubprocessError) as error:
            self.message = f"Could not switch the {producer.label}: {error}"
            return
        if result.returncode != 0:
            detail = (result.stderr or "").strip().splitlines()
            self.message = f"Could not switch the {producer.label}: " + (
                detail[-1] if detail else f"exit {result.returncode}"
            )
            return
        self.message = f"{producer.label}: {'on' if enabled else 'off'}"

    # -- keymap profiles -------------------------------------------------------

    def profiles(self) -> list[str]:
        return keymap_profiles.list_profiles()

    def delete_profile(self, name: str) -> None:
        try:
            keymap_profiles.delete(name)
        except keymap_profiles.ProfileError as error:
            self.message = str(error)
            return
        self.message = f"Deleted {name}"

    def save_profile(self, name: str, *, force: bool = False) -> None:
        self._keymap(["save", name, *(["--force"] if force else [])], name)

    def load_profile(self, name: str) -> None:
        self._keymap(["load", name], name)

    def _keymap(self, arguments: list[str], name: str) -> None:
        try:
            keymap_profiles.profile_path(name)
        except keymap_profiles.ProfileError as error:
            self.message = str(error)
            return
        controls = self.controls
        if not controls.can_use_keyboard:
            owner = controls.other_owner
            self.message = f"The keyboard is in use by {owner}" if owner else "The keyboard is busy"
            return
        try:
            controls.keymap = _Child([*self.keymap_command, *arguments], controls.env)
        except OSError as error:
            self.message = f"Could not start corne-arcane-keymap: {error}"
            return
        self.message = f"{'Saving' if arguments[0] == 'save' else 'Loading'} {name}"

    @property
    def busy(self) -> bool:
        return self.controls.keymap is not None

    def poll(self) -> None:
        child = self.controls.keymap
        if child is None or (code := child.poll()) is None:
            return
        if code == 0:
            lines = child.output().strip().splitlines()
            line = lines[-1] if lines else "done"
            self.message = line[:1].upper() + line[1:]
        else:
            self.message = child.error() or f"corne-arcane-keymap exited {code}"
        child.close()
        self.controls.keymap = None


class SettingsWindow:
    """A Tk window over Settings: one section per setting, one message line."""

    def __init__(self, tk, master, settings: Settings) -> None:
        self.tk = tk
        self.settings = settings
        self.closed = False
        self.window = tk.Toplevel(master)
        self.window.title("Corne Arcane settings")
        self.window.protocol("WM_DELETE_WINDOW", self.close)

        tk.Label(self.window, text="Vial command", anchor="w").pack(fill="x", padx=12, pady=(12, 0))
        vial_row = tk.Frame(self.window)
        vial_row.pack(fill="x", padx=12)
        self.vial = tk.StringVar(master=self.window, value=settings.vial_command())
        tk.Entry(vial_row, textvariable=self.vial, width=40).pack(
            side="left", fill="x", expand=True
        )
        tk.Button(vial_row, text="Save", command=self._save_vial).pack(side="left", padx=(6, 0))

        tk.Label(self.window, text="Focus producers", anchor="w").pack(
            fill="x", padx=12, pady=(12, 0)
        )
        self.switches = []
        for producer in settings.producers:
            state = settings.producer_enabled(producer)
            variable = tk.IntVar(master=self.window, value=1 if state else 0)
            box = tk.Checkbutton(
                self.window,
                text=producer.label,
                variable=variable,
                anchor="w",
                command=lambda p=producer, v=variable: settings.set_producer(p, bool(v.get())),
            )
            if state is None:
                box.configure(state="disabled")
            box.pack(fill="x", padx=12)
            self.switches.append((producer, variable, box))

        tk.Label(self.window, text="Keymap profiles", anchor="w").pack(
            fill="x", padx=12, pady=(12, 0)
        )
        self.listbox = tk.Listbox(self.window, height=6, exportselection=False)
        self.listbox.pack(fill="x", padx=12)
        name_row = tk.Frame(self.window)
        name_row.pack(fill="x", padx=12, pady=(4, 0))
        self.name = tk.StringVar(master=self.window, value="")
        tk.Entry(name_row, textvariable=self.name, width=20).pack(side="left")
        self.save_button = tk.Button(name_row, text="Save keyboard as", command=self._save)
        self.save_button.pack(side="left", padx=(6, 0))
        action_row = tk.Frame(self.window)
        action_row.pack(fill="x", padx=12, pady=(4, 0))
        self.load_button = tk.Button(action_row, text="Load selected", command=self._load)
        self.load_button.pack(side="left")
        self.delete_button = tk.Button(action_row, text="Delete selected", command=self._delete)
        self.delete_button.pack(side="left", padx=(6, 0))
        self.message = tk.Label(self.window, text="", anchor="w", wraplength=360, justify="left")
        self.message.pack(fill="x", padx=12, pady=12)
        self._names: list[str] = []
        self.refresh()

    def _selected(self) -> str | None:
        chosen = self.listbox.curselection()
        return self._names[chosen[0]] if chosen else None

    def _save_vial(self) -> None:
        self.settings.set_vial_command(self.vial.get())

    def _save(self) -> None:
        name = self.name.get().strip()
        self.settings.save_profile(name, force=name in self._names)

    def _load(self) -> None:
        name = self._selected()
        if name is None:
            self.settings.message = "Pick a profile to load"
        else:
            self.settings.load_profile(name)

    def _delete(self) -> None:
        name = self._selected()
        if name is None:
            self.settings.message = "Pick a profile to delete"
        else:
            self.settings.delete_profile(name)

    def refresh(self) -> None:
        """Called every frame while open."""
        if self.closed:
            return
        settings = self.settings
        settings.poll()
        names = settings.profiles()
        if names != self._names:
            self._names = names
            self.listbox.delete(0, "end")
            for name in names:
                self.listbox.insert("end", name)
        usable = "normal" if settings.controls.can_use_keyboard else "disabled"
        for button in (self.save_button, self.load_button):
            if button.cget("state") != usable:
                button.configure(state=usable)
        if self.message.cget("text") != settings.message:
            self.message.configure(text=settings.message)

    def close(self) -> None:
        if self.closed:
            return
        self.closed = True
        try:
            self.window.destroy()
        except Exception:
            pass
