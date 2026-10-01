"""The city app's controls: lend the keyboard, open Vial, run an observation.

Everything here goes through what already exists. Pause and Resume are the
service's Control interface, called over the app's own bus connection, so a
pause lasts exactly as long as the app does. Open Vial runs the Vial launcher
and Observe runs diagnostics, each a child process with its own guard, so the
app never opens the keyboard itself. While another tool holds the keyboard the
buttons that would need it are disabled, and the line says who has it.
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

from .dbus_contract import BUS_NAME, CONTROL_INTERFACE, OBJECT_PATH, PAUSE, RESUME
from .hid_ownership import lock_path

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
    """How to run corne-arcane-<name>: the installed command beside this app if
    there is one (on Nix it carries wrapped settings), else the module."""
    sibling = Path(sys.argv[0]).resolve().parent / f"corne-arcane-{name}"
    if sibling.is_file() and os.access(sibling, os.X_OK):
        return [str(sibling)]
    found = shutil.which(f"corne-arcane-{name}")
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


class Controls:
    """What the panel can do, and what it should say, without any Tk in it."""

    def __init__(
        self,
        view,
        *,
        vial_command: list[str] | None = None,
        diagnostics_command: list[str] | None = None,
        lock: Path | None = None,
        clock: Callable[[], float] = time.monotonic,
    ) -> None:
        self.view = view
        self.vial_command = vial_command or tool_command("vial", "vial_launcher")
        self.diagnostics_command = diagnostics_command or tool_command("diagnostics", "diagnostics")
        self.lock = lock or lock_path()
        self.clock = clock
        self.label = f"{APP_LABEL} (pid {os.getpid()})"
        self.message = ""
        self.holding = False
        self.pending = False
        self.vial: _Child | None = None
        self.observation: _Child | None = None
        # A keymap save or load started from the settings window (app_settings).
        self.keymap: _Child | None = None
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
                # "GDBus.Error:<name>: text" -> "text". strip_remote_error edits
                # the C error, not PyGObject's copy of it, so strip here.
                message = error.message
                if message.startswith("GDBus.Error:"):
                    message = message.split(": ", 1)[-1]
                done(message)
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
            if any(child is not None for child in (self.vial, self.observation, self.keymap)):
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
        and let a keymap write finish rather than cut it off halfway."""
        if self.observation is not None:
            self.observation.process.terminate()
            try:
                self.observation.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.observation.process.kill()
                self.observation.process.wait()
            self.observation.close()
            self.observation = None
        for name in ("vial", "keymap"):
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
    ) -> None:
        self.controls = controls
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
        widgets = [self.pause_button, self.vial_button, self.observe_button, self.minutes_box]
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
        owner = controls.other_owner
        text = controls.message or (f"In use by {owner}" if owner else "")
        if self.message.cget("text") != text:
            self.message.configure(text=text)

    @staticmethod
    def _set(button, text: str, enabled: bool) -> None:
        state = "normal" if enabled else "disabled"
        if button.cget("text") != text or button.cget("state") != state:
            button.configure(text=text, state=state)
