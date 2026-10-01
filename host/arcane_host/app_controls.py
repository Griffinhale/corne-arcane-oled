"""The control layer the city app and the tray share: what the service reports
(ServiceView), and lending the keyboard, opening Vial, running an observation
(Controls).

Everything here goes through what already exists. Pause and Resume are the
service's Control interface, called over the app's own bus connection, so a
pause lasts exactly as long as the app does. Open Vial runs the Vial launcher,
Observe runs diagnostics and Flash runs the flasher, each a child process with
its own guard, so the app never opens the keyboard itself. While another tool
holds the keyboard the buttons that would need it are disabled, and the line
says who has it.
"""

from __future__ import annotations

import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import IO, Callable

from .city import CityInput, resting_input
from .dbus_contract import (
    BUS_NAME,
    CONTROL_INTERFACE,
    OBJECT_PATH,
    PAUSE,
    RESUME,
    STATUS,
    STATUS_CHANGED,
    STATUS_SIGNATURE,
    WORLD,
    WORLD_CHANGED,
    WORLD_SIGNATURE,
)
from .flash import (
    KEYMAP_WARNING,
    Suggested,
    WrongImage,
    check_image,
    remember_choice,
    state_dir,
    suggest_image,
)
from .hid_ownership import lock_path, remote_error_text

APP_LABEL = "Corne Arcane app"
OBSERVE_MINUTES = (1, 5, 15, 30)
# How often the keyboard lock is looked at; the redraw runs much faster.
LOCK_POLL_SECONDS = 0.5


def lock_holder(path: Path, locks: Path = Path("/proc/locks")) -> str | None:
    """Who holds the guard's lock on path, or None when nobody does.

    Read from /proc/locks, never by taking the lock: a probe that took it, even
    for a moment, could refuse a launcher starting at that instant.
    """
    try:
        stat = path.stat()
        table = locks.read_text()
    except OSError:
        return None
    wanted = f"{os.major(stat.st_dev):02x}:{os.minor(stat.st_dev):02x}:{stat.st_ino}"
    for line in table.splitlines():
        fields = line.split()
        # "N: FLOCK ADVISORY WRITE pid maj:min:inode start end"; waiters have "->".
        if len(fields) >= 6 and fields[1] == "FLOCK" and fields[5] == wanted:
            try:
                return path.read_text().strip() or "another tool"
            except OSError:
                return "another tool"
    return None


def tool_command(name: str, module: str) -> list[str]:
    """How to run corne-arcane-<name> (plain corne-arcane for ""): the installed
    command beside this one if there is one (on Nix it carries wrapped
    settings), else the module."""
    command = "corne-arcane" + (f"-{name}" if name else "")
    sibling = Path(sys.argv[0]).resolve().parent / command
    if sibling.is_file() and os.access(sibling, os.X_OK):
        return [str(sibling)]
    found = shutil.which(command)
    if found:
        return [found]
    return [sys.executable, "-m", f"arcane_host.{module}"]


def _last_line(output: IO[bytes]) -> str:
    output.seek(0)
    lines = output.read().decode("utf-8", "replace").strip().splitlines()
    if not lines:
        return ""
    # The tools prefix their errors with their own name; the panel already says which.
    return lines[-1].split(": ", 1)[-1] if lines[-1].startswith("corne-arcane") else lines[-1]


class _Child:
    """One tool run in the background, its stderr kept in a file so a chatty
    Vial can never fill a pipe and stall."""

    def __init__(self, argv: list[str], env: dict[str, str]) -> None:
        self.stderr = tempfile.TemporaryFile()
        self.stdout = tempfile.TemporaryFile()
        self.process = subprocess.Popen(
            argv,
            stdin=subprocess.DEVNULL,
            stdout=self.stdout,
            stderr=self.stderr,
            env=env,
            start_new_session=True,
        )

    def poll(self) -> int | None:
        return self.process.poll()

    def output(self) -> str:
        self.stdout.seek(0)
        return self.stdout.read().decode("utf-8", "replace")

    def error(self) -> str:
        return _last_line(self.stderr)

    def close(self) -> None:
        self.stdout.close()
        self.stderr.close()


# What the link line says, for each Control link state.
LINK_CAPTIONS = {
    "starting": "Connecting to the keyboard",
    "connected": "Keyboard connected",
    "absent": "No keyboard found",
    "denied": "Keyboard found, but not allowed to open it",
    "several": "Several keyboards found; give the service --device",
    "failed": "Keyboard link failed; retrying",
}
NO_SERVICE = "Service not running, so the city is offline"


def link_caption(status: tuple[str, str, bool, str] | None) -> str:
    if status is None:
        return NO_SERVICE
    link, _device, paused, owner = status
    if paused:
        return f"Keyboard lent to {owner}"
    return LINK_CAPTIONS.get(link, f"Keyboard: {link}")


class ServiceView:
    """What the running service reports: the keyboard link and the world it sends.

    A client, not a daemon. It reads the Control interface and its signals and
    never opens the keyboard or sends a heartbeat, so the service's heartbeat
    stays the only one. Until the service answers, and after it goes away,
    ``status`` and ``world`` are None and the city is drawn offline.
    """

    CALL_TIMEOUT_MS = 1000

    def __init__(self, Gio, GLib, connection) -> None:
        self.Gio = Gio
        self.GLib = GLib
        self.connection = connection
        self.status: tuple[str, str, bool, str] | None = None
        self.world: tuple[int, ...] | None = None
        # Called after status or world changes; the tray redraws its icon here.
        self.listeners: list[Callable[[], None]] = []
        self._subscriptions = [
            connection.signal_subscribe(
                BUS_NAME,
                CONTROL_INTERFACE,
                name,
                OBJECT_PATH,
                None,
                Gio.DBusSignalFlags.NONE,
                handler,
            )
            for name, handler in (
                (STATUS_CHANGED, self._status_changed),
                (WORLD_CHANGED, self._world_changed),
            )
        ]
        self._watch_id = Gio.bus_watch_name_on_connection(
            connection, BUS_NAME, Gio.BusNameWatcherFlags.NONE, self._appeared, self._vanished
        )

    def _changed(self) -> None:
        for listener in tuple(self.listeners):
            listener()

    def _status_changed(self, *args) -> None:
        self.status = tuple(args[-1].unpack())
        self._changed()

    def _world_changed(self, *args) -> None:
        self.world = tuple(args[-1].unpack())
        self._changed()

    def _appeared(self, _connection, _name, _owner) -> None:
        self._fetch(STATUS, STATUS_SIGNATURE, "status")
        self._fetch(WORLD, WORLD_SIGNATURE, "world")

    def _vanished(self, _connection, _name) -> None:
        self.status = None
        self.world = None
        self._changed()

    def _fetch(self, method: str, signature: str, attribute: str) -> None:
        def done(connection, result) -> None:
            try:
                value = connection.call_finish(result).unpack()
            except self.GLib.Error:
                # A service older than World still reports its link; the city
                # then rests until a WorldChanged arrives.
                return
            setattr(self, attribute, tuple(value))
            self._changed()

        self.connection.call(
            BUS_NAME,
            OBJECT_PATH,
            CONTROL_INTERFACE,
            method,
            None,
            self.GLib.VariantType(signature),
            self.Gio.DBusCallFlags.NO_AUTO_START,
            self.CALL_TIMEOUT_MS,
            None,
            done,
        )

    def pump(self) -> None:
        """Deliver whatever the bus has sent, without blocking the window."""
        context = self.GLib.MainContext.default()
        while context.iteration(False):
            pass

    def city(self, seed: int) -> CityInput:
        if self.world is None:
            return resting_input(online=self.status is not None, seed=seed)
        return CityInput(*self.world, online=1, seed=seed & 0xFF)

    def caption(self) -> str:
        return link_caption(self.status)

    def close(self) -> None:
        for subscription in self._subscriptions:
            self.connection.signal_unsubscribe(subscription)
        self._subscriptions = []
        if self._watch_id:
            self.Gio.bus_unwatch_name(self._watch_id)
            self._watch_id = 0


class Controls:
    """What the panel can do, and what it should say, without any Tk in it."""

    def __init__(
        self,
        view,
        *,
        vial_command: list[str] | None = None,
        diagnostics_command: list[str] | None = None,
        flash_command: list[str] | None = None,
        lock: Path | None = None,
        clock: Callable[[], float] = time.monotonic,
        label: str = APP_LABEL,
        suggest: Callable[[], Suggested | None] = suggest_image,
    ) -> None:
        self.view = view
        self.suggest = suggest
        self.vial_command = vial_command or tool_command("vial", "vial_launcher")
        self.diagnostics_command = diagnostics_command or tool_command("diagnostics", "diagnostics")
        self.flash_command = flash_command or tool_command("flash", "flash")
        self.lock = lock or lock_path()
        self.clock = clock
        self.label = f"{label} (pid {os.getpid()})"
        self.message = ""
        self.holding = False
        self.pending = False
        self.vial: _Child | None = None
        self.observation: _Child | None = None
        # A keymap save or load started from the settings window (app_settings).
        self.keymap: _Child | None = None
        # An image checked and waiting for the keymap warning to be confirmed.
        self.flash_ready: Path | None = None
        self.flash: _Child | None = None
        self.observe_until = 0.0
        self.locked_by: str | None = None
        self._lock_checked = -math.inf
        self.env = dict(os.environ)
        # A module fallback must find this package from any working directory.
        package_root = str(Path(__file__).resolve().parents[1])
        self.env["PYTHONPATH"] = os.pathsep.join(
            part for part in (package_root, os.environ.get("PYTHONPATH")) if part
        )

    # -- what the panel shows ------------------------------------------------

    @property
    def lent_to_other(self) -> bool:
        status = self.view.status
        return status is not None and status[2] and status[3] != self.label

    @property
    def other_owner(self) -> str | None:
        """Who else has the keyboard, if anyone: a pause or the guard's lock."""
        if self.lent_to_other:
            return self.view.status[3]
        return self.locked_by

    @property
    def can_pause(self) -> bool:
        return (
            self.view.status is not None
            and not self.view.status[2]
            and not self.pending
            and self.other_owner is None
        )

    @property
    def can_resume(self) -> bool:
        return self.holding and not self.pending

    @property
    def can_use_keyboard(self) -> bool:
        """Open Vial and Observe: each takes the keyboard through its own guard."""
        return (
            not self.holding
            and not self.pending
            and self.other_owner is None
            and self.vial is None
            and self.observation is None
            and self.keymap is None
            and self.flash is None
        )

    def remaining(self) -> int | None:
        if self.observation is None:
            return None
        return max(0, math.ceil(self.observe_until - self.clock()))

    # -- actions ---------------------------------------------------------------

    def pause(self) -> None:
        if not self.can_pause:
            return
        self.pending = True
        self._call(PAUSE, self.view.GLib.Variant("(s)", (self.label,)), self._paused)

    def resume(self) -> None:
        if not self.can_resume:
            return
        self.pending = True
        self._call(RESUME, None, self._resumed)

    def _paused(self, error: str | None) -> None:
        self.pending = False
        if error:
            self.message = f"Could not pause: {error}"
            return
        self.holding = True
        self.message = "Keyboard paused for as long as this app is open"

    def _resumed(self, error: str | None) -> None:
        self.pending = False
        if error:
            self.message = f"Could not resume: {error}"
            return
        self.holding = False
        self.message = ""

    def _call(self, method: str, arguments, done: Callable[[str | None], None]) -> None:
        Gio, GLib = self.view.Gio, self.view.GLib

        def finished(connection, result) -> None:
            try:
                connection.call_finish(result)
            except GLib.Error as error:
                done(remote_error_text(error))
                return
            done(None)

        self.view.connection.call(
            BUS_NAME,
            OBJECT_PATH,
            CONTROL_INTERFACE,
            method,
            arguments,
            None,
            Gio.DBusCallFlags.NO_AUTO_START,
            self.view.CALL_TIMEOUT_MS,
            None,
            finished,
        )

    def open_vial(self) -> None:
        if not self.can_use_keyboard:
            return
        try:
            self.vial = _Child(self.vial_command, self.env)
        except OSError as error:
            self.message = f"Could not start Vial: {error}"
            return
        self.message = "Opening Vial"

    def observe(self, minutes: float) -> None:
        if not self.can_use_keyboard:
            return
        seconds = max(1, round(minutes * 60))
        try:
            self.observation = _Child(
                [*self.diagnostics_command, "--observe", str(seconds), "--json"], self.env
            )
        except OSError as error:
            self.message = f"Could not start diagnostics: {error}"
            return
        self.observe_until = self.clock() + seconds
        self.message = ""

    def suggest_flash(self) -> Suggested | None:
        """The image to offer before asking: a GitHub release, else a local
        build, else nothing and the file dialog starts empty."""
        if not self.can_use_keyboard:
            return None
        return self.suggest()

    def prepare_flash(self, image: Path, source: str | None = None) -> None:
        """Check the image, then show the keymap warning; nothing runs until
        confirm_flash. `source` names where an offered image came from."""
        self.flash_ready = None
        if not self.can_use_keyboard:
            return
        try:
            check_image(image, state_dir())
        except WrongImage as error:
            self.message = f"Not flashed: {error}"
            return
        self.flash_ready = Path(image)
        where = f"{Path(image).name} ({source}). " if source else ""
        self.message = f"{where}{KEYMAP_WARNING} Then press Flash now."

    def cancel_flash(self) -> None:
        if self.flash_ready is not None:
            self.flash_ready = None
            self.message = ""

    def confirm_flash(self) -> None:
        image, self.flash_ready = self.flash_ready, None
        if image is None or not self.can_use_keyboard:
            return
        remember_choice(image)
        try:
            self.flash = _Child([*self.flash_command, str(image)], self.env)
        except OSError as error:
            self.message = f"Could not start the flasher: {error}"
            return
        self.message = "Stopping the service"

    def poll(self) -> None:
        """Called every frame: notice finished tools and who holds the keyboard."""
        now = self.clock()
        status = self.view.status
        if self.holding and not self.pending and (status is None or status[3] != self.label):
            # The service restarted or went away; our pause went with it.
            self.holding = False
            self.message = ""
        if now - self._lock_checked >= LOCK_POLL_SECONDS:
            self._lock_checked = now
            self.locked_by = lock_holder(self.lock)
            children = (self.vial, self.observation, self.keymap, self.flash)
            if any(child is not None for child in children):
                # Our own children hold it; that is not someone else.
                self.locked_by = None
        if self.vial is not None:
            code = self.vial.poll()
            if code is None:
                self.message = "Vial is open; the city resumes when it closes"
            else:
                self.message = (
                    "Vial closed"
                    if code == 0
                    else self.vial.error() or (f"Vial launcher exited {code}")
                )
                self.vial.close()
                self.vial = None
        if self.observation is not None:
            code = self.observation.poll()
            if code is None:
                self.message = f"Observing: {self.remaining()} s left"
            else:
                self.message = self._observation_result(code)
                self.observation.close()
                self.observation = None
        if self.flash is not None:
            code = self.flash.poll()
            if code is None:
                lines = self.flash.output().strip().splitlines()
                if lines:
                    self.message = lines[-1]
            else:
                self.message = (
                    "Both halves flashed. Load your saved layout back in Vial."
                    if code == 0
                    else self.flash.error() or f"Flasher exited {code}"
                )
                self.flash.close()
                self.flash = None

    def _observation_result(self, code: int) -> str:
        if code in (0, 2):
            try:
                payload = json.loads(self.observation.output())
                failed = [name for name, passed in payload["checks"].items() if not passed]
            except (ValueError, KeyError, AttributeError):
                return "Observation finished, but its result could not be read"
            if payload.get("passed") and code == 0:
                return "Observation passed"
            return "Observation failed: " + ", ".join(failed)
        return self.observation.error() or f"Diagnostics exited {code}"

    def close(self) -> None:
        """Stop an observation (its guard restores the service). Leave Vial open,
        and let a keymap write or a flash finish rather than cut it off halfway."""
        if self.observation is not None:
            self.observation.process.terminate()
            try:
                self.observation.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.observation.process.kill()
                self.observation.process.wait()
            self.observation.close()
            self.observation = None
        for name in ("vial", "keymap", "flash"):
            child = getattr(self, name)
            if child is not None:
                child.close()
                setattr(self, name, None)


class ControlsPanel:
    """The Tk row under the city: buttons, an observation length, a message."""

    def __init__(
        self,
        tk,
        master,
        controls: Controls,
        *,
        background: str,
        ink: str,
        on_settings: Callable[[], None] | None = None,
        ask_image: Callable[[Suggested | None], str] | None = None,
    ) -> None:
        self.controls = controls
        self.master = master
        self.ask_image = ask_image or self._ask_image
        self.frame = tk.Frame(master, background=background)
        self.pause_button = tk.Button(self.frame, text="Pause keyboard", command=self._toggle)
        self.vial_button = tk.Button(self.frame, text="Open Vial", command=controls.open_vial)
        self.minutes = tk.StringVar(master=master, value=str(OBSERVE_MINUTES[1]))
        self.minutes_box = tk.Spinbox(
            self.frame, values=OBSERVE_MINUTES, textvariable=self.minutes, width=3
        )
        # A Spinbox given values= starts on the first; set the default after it.
        self.minutes.set(str(OBSERVE_MINUTES[1]))
        self.observe_button = tk.Button(self.frame, text="Observe (min)", command=self._observe)
        self.message = tk.Label(
            master, text="", background=background, foreground=ink, wraplength=360
        )
        self.flash_button = tk.Button(self.frame, text="Flash", command=self._flash)
        # Shown only while the keymap warning waits, so the row stays narrow.
        self.cancel_button = tk.Button(self.frame, text="Cancel", command=controls.cancel_flash)
        self.cancel_shown = False
        widgets = [
            self.pause_button,
            self.vial_button,
            self.observe_button,
            self.minutes_box,
            self.flash_button,
        ]
        self.settings_button = None
        if on_settings is not None:
            self.settings_button = tk.Button(self.frame, text="Settings", command=on_settings)
            widgets.append(self.settings_button)
        for widget in widgets:
            widget.pack(side="left", padx=3)

    def pack(self) -> None:
        self.frame.pack(padx=12, pady=(0, 4))
        self.message.pack(padx=12, pady=(0, 10))

    def place(self) -> None:
        """For a fixed-size window: above the link line at the bottom edge."""
        self.message.place(relx=0.5, rely=1.0, anchor="s", y=-28)
        self.frame.place(relx=0.5, rely=1.0, anchor="s", y=-50)

    def _toggle(self) -> None:
        if self.controls.holding:
            self.controls.resume()
        else:
            self.controls.pause()

    def _observe(self) -> None:
        try:
            minutes = float(self.minutes.get())
        except ValueError:
            self.controls.message = "Observation length must be a number of minutes"
            return
        self.controls.observe(minutes)

    def _ask_image(self, suggested: Suggested | None) -> str:
        from tkinter import filedialog

        where = {}
        if suggested is not None:
            # Opened on the offered image, so Open is all it takes.
            where = {"initialdir": str(suggested.path.parent), "initialfile": suggested.path.name}
        return filedialog.askopenfilename(
            parent=self.master,
            title="Choose the Corne image to flash",
            filetypes=(("UF2 image", "*.uf2"),),
            **where,
        )

    def _flash(self) -> None:
        if self.controls.flash_ready is not None:
            self.controls.confirm_flash()
            return
        suggested = self.controls.suggest_flash()
        chosen = self.ask_image(suggested)
        if chosen:
            offered = suggested is not None and Path(chosen).resolve() == suggested.path.resolve()
            self.controls.prepare_flash(Path(chosen), suggested.source if offered else None)

    def refresh(self) -> None:
        controls = self.controls
        controls.poll()
        holding = controls.holding
        self._set(
            self.pause_button,
            "Resume keyboard" if holding else "Pause keyboard",
            controls.can_resume if holding else controls.can_pause,
        )
        self._set(self.vial_button, "Open Vial", controls.can_use_keyboard)
        self._set(self.observe_button, "Observe (min)", controls.can_use_keyboard)
        ready = controls.flash_ready is not None
        self._set(self.flash_button, "Flash now" if ready else "Flash", controls.can_use_keyboard)
        if ready != self.cancel_shown:
            self.cancel_shown = ready
            if ready:
                self.cancel_button.pack(side="left", padx=3, after=self.flash_button)
            else:
                self.cancel_button.pack_forget()
        owner = controls.other_owner
        text = controls.message or (f"In use by {owner}" if owner else "")
        if self.message.cget("text") != text:
            self.message.configure(text=text)

    @staticmethod
    def _set(button, text: str, enabled: bool) -> None:
        state = "normal" if enabled else "disabled"
        if button.cget("text") != text or button.cget("state") != state:
            button.configure(text=text, state=state)
