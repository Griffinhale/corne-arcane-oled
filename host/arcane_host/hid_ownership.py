"""Exclusive Raw HID ownership shared by diagnostics and the Vial launcher."""

from __future__ import annotations

import fcntl
import os
import signal
import subprocess
import sys
import time
from pathlib import Path
from types import FrameType
from typing import Any

from .dbus_contract import BUS_NAME, CONTROL_BUSY, CONTROL_INTERFACE, OBJECT_PATH, PAUSE, RESUME
from .hidraw import choose_device

SERVICE = os.environ.get("CORNE_ARCANE_SERVICE", "corne-arcane-host.service")
SYSTEMCTL = os.environ.get("CORNE_ARCANE_SYSTEMCTL", "systemctl")


class OwnershipSignal(Exception):
    def __init__(self, signum: int):
        super().__init__(signal.Signals(signum).name)
        self.signum = signum


def _systemctl(*args: str, check: bool = False) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        (SYSTEMCTL, "--user", *args),
        check=check,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def service_is_active() -> bool:
    result = _systemctl("is-active", "--quiet", SERVICE)
    if result.returncode == 0:
        return True
    # 3 is inactive; 4 is no such unit, which is how an install without the
    # optional daemon looks. Neither holds the keyboard.
    if result.returncode in (3, 4):
        return False
    detail = result.stderr.strip() or f"systemctl exited {result.returncode}"
    raise RuntimeError(f"cannot determine daemon state: {detail}")


def stop_service() -> None:
    _systemctl("stop", SERVICE, check=True)


def start_service() -> None:
    _systemctl("start", SERVICE, check=True)


CONTROL_TIMEOUT_MS = 1000


def remote_error_text(error: Any) -> str:
    """The text of a GLib.Error a D-Bus peer returned, without its error name.

    "GDBus.Error:<name>: text" -> "text". Gio.DBusError.strip_remote_error
    edits the C error, not PyGObject's copy of it, so strip the message here.
    """
    message = error.message
    if message.startswith("GDBus.Error:"):
        message = message.split(": ", 1)[-1]
    return message


def pause_daemon(label: str) -> Any | None:
    """Ask a running daemon to lend the keyboard over its Control interface.

    Returns the bus connection the pause is tied to -- the daemon resumes by
    itself when it closes -- or None when no daemon answers, so the caller
    falls back to systemctl. A daemon that has already lent the keyboard to
    someone else is an error, not a fallback.
    """
    try:
        import gi

        gi.require_version("Gio", "2.0")
        from gi.repository import Gio, GLib
    except (ImportError, ValueError):
        return None
    try:
        connection = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        connection.call_sync(
            BUS_NAME,
            OBJECT_PATH,
            CONTROL_INTERFACE,
            PAUSE,
            GLib.Variant("(s)", (label,)),
            None,
            Gio.DBusCallFlags.NO_AUTO_START,
            CONTROL_TIMEOUT_MS,
            None,
        )
    except GLib.Error as error:
        if Gio.DBusError.get_remote_error(error) == CONTROL_BUSY:
            raise RuntimeError(f"{remote_error_text(error)}; close it first") from None
        return None
    return connection


def resume_daemon(connection: Any) -> None:
    from gi.repository import Gio, GLib

    try:
        connection.call_sync(
            BUS_NAME,
            OBJECT_PATH,
            CONTROL_INTERFACE,
            RESUME,
            None,
            None,
            Gio.DBusCallFlags.NO_AUTO_START,
            CONTROL_TIMEOUT_MS,
            None,
        )
    except GLib.Error as error:
        raise RuntimeError(f"daemon did not resume: {remote_error_text(error)}") from None


def hidraw_handles(pid: int, proc_root: Path = Path("/proc")) -> tuple[Path, ...]:
    if pid <= 0:
        return ()
    handles: list[Path] = []
    try:
        entries = tuple((proc_root / str(pid) / "fd").iterdir())
    except OSError:
        return ()
    for entry in entries:
        try:
            target = Path(os.readlink(entry))
        except OSError:
            continue
        if str(target).startswith("/dev/hidraw"):
            handles.append(target)
    return tuple(sorted(handles))


def holds_node(pid: int, node: Path, proc_root: Path = Path("/proc")) -> bool:
    """Whether pid has node open; reads its /proc fd links, never the node."""
    try:
        entries = tuple((proc_root / str(pid) / "fd").iterdir())
    except OSError:
        return False
    for entry in entries:
        try:
            if Path(os.readlink(entry)) == node:
                return True
        except OSError:
            continue
    return False


def node_openers(node: Path, proc_root: Path = Path("/proc")) -> tuple[tuple[int, str], ...]:
    """(pid, comm) of every other process with node open.

    Reads /proc/*/fd links only; the device itself is never opened. Processes
    of other users are unreadable and so are not seen. One pass costs tens of
    milliseconds on a busy desktop, so callers run it on an event, not a timer.
    """
    openers: list[tuple[int, str]] = []
    try:
        entries = sorted(proc_root.iterdir())
    except OSError:
        return ()
    for entry in entries:
        if not entry.name.isdigit() or int(entry.name) == os.getpid():
            continue
        if not holds_node(int(entry.name), node, proc_root):
            continue
        try:
            name = (entry / "comm").read_text().strip()
        except OSError:
            name = "unknown"
        openers.append((int(entry.name), name))
    return tuple(openers)


def hidraw_openers(node: Path, proc_root: Path = Path("/proc")) -> tuple[str, ...]:
    """Name every other process with node open, as "comm (pid N)"."""
    return tuple(f"{name} (pid {pid})" for pid, name in node_openers(node, proc_root))


IN_OPEN = 0x20


class OpenWatch:
    """Tells when anything opens one node, through inotify, without opening it.

    Costs nothing while the node is idle: opened() is one non-blocking read.
    Our own opens count too; the caller sorts openers out with node_openers.
    """

    def __init__(self, node: Path) -> None:
        import ctypes

        libc = ctypes.CDLL(None, use_errno=True)
        # IN_NONBLOCK and IN_CLOEXEC have the values of O_NONBLOCK and O_CLOEXEC.
        fd = libc.inotify_init1(os.O_NONBLOCK | os.O_CLOEXEC)
        if fd < 0:
            raise OSError(ctypes.get_errno(), "inotify_init1 failed")
        if libc.inotify_add_watch(fd, os.fsencode(node), IN_OPEN) < 0:
            error = ctypes.get_errno()
            os.close(fd)
            raise OSError(error, f"cannot watch {node}")
        self.node = node
        self.fd = fd

    def opened(self) -> bool:
        """Drain pending events; True when the node was opened since the last call."""
        seen = False
        while self.fd >= 0:
            try:
                if not os.read(self.fd, 4096):
                    break
            except OSError:
                break
            seen = True
        return seen

    def close(self) -> None:
        if self.fd >= 0:
            os.close(self.fd)
            self.fd = -1


def wait_for_hidraw_release(
    node: Path, timeout: float = 5.0, proc_root: Path = Path("/proc")
) -> None:
    """Wait until no other process has node open; a zero timeout checks once."""
    deadline = time.monotonic() + timeout
    while openers := hidraw_openers(node, proc_root):
        if time.monotonic() >= deadline:
            raise TimeoutError(f"{node} is still open by {', '.join(openers)}")
        time.sleep(0.05)


def chosen_node(explicit: str | None = None) -> Path | None:
    """The hidraw node the caller will open, or None when there is no keyboard to guard."""
    try:
        return Path(os.path.realpath(choose_device(explicit)))
    except (RuntimeError, ValueError):
        return None


def lock_path() -> Path:
    runtime = os.environ.get("XDG_RUNTIME_DIR")
    if runtime:
        return Path(runtime) / "corne-arcane" / "hid.lock"
    return Path("/tmp") / f"corne-arcane-{os.getuid()}" / "hid.lock"


def take_lock(path: Path, holder: str) -> int:
    """flock path for this process and record holder in it, or say who has it."""
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    fd = os.open(path, os.O_RDWR | os.O_CREAT, 0o600)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        current = os.read(fd, 256).decode("utf-8", "replace").strip() or "another process"
        os.close(fd)
        raise RuntimeError(f"the keyboard is in use by {current}; close it first") from None
    except BaseException:
        os.close(fd)
        raise
    os.ftruncate(fd, 0)
    os.write(fd, f"{holder} (pid {os.getpid()})\n".encode())
    return fd


def restore_marker_path() -> Path:
    """Present while a guard owes the daemon a restart; holds that guard's name."""
    return lock_path().with_name("restore-service")


class ExclusiveHidOwnership:
    """Hold the keyboard alone: take the lock, stop an active daemon, wait for the node.

    The lock keeps the tools that share this guard (the Vial launcher,
    diagnostics) from overlapping; the /proc scan catches anything else that
    has the node open. A running daemon is asked to pause over D-Bus, which
    keeps the unit up and ends with this process; one that does not answer is
    stopped with systemctl. On exit the daemon's prior state is restored.

    A guard that stops the daemon leaves a marker beside the lock until it has
    restarted it. SIGKILL skips that restart, so a later guard that finds the
    marker while holding the lock -- which proves the old holder is gone --
    takes the debt over and restarts the daemon on its own exit.
    """

    def __init__(
        self,
        *,
        service_handoff: bool = True,
        release_timeout: float = 5.0,
        holder: str | None = None,
        device: str | None = None,
    ):
        self.service_handoff = service_handoff
        self.release_timeout = release_timeout
        self.holder = holder or Path(sys.argv[0]).name
        self.device = device
        self._restore_service = False
        self._owns_marker = False
        self._pause_bus: Any | None = None
        self._lock_fd = -1
        self._previous_handlers: dict[int, signal.Handlers] = {}

    def _interrupted(self, signum: int, _frame: FrameType | None) -> None:
        raise OwnershipSignal(signum)

    def __enter__(self) -> ExclusiveHidOwnership:
        for signum in (signal.SIGHUP, signal.SIGINT, signal.SIGTERM):
            self._previous_handlers[signum] = signal.signal(signum, self._interrupted)
        try:
            self._lock_fd = take_lock(lock_path(), self.holder)
            node = chosen_node(self.device)
            marker = restore_marker_path()
            try:
                stale = marker.read_text().strip()
            except FileNotFoundError:
                stale = ""
            if stale:
                print(
                    f"{SERVICE} was left stopped by {stale}; restarting it on exit", file=sys.stderr
                )
            active = False
            if self.service_handoff:
                self._pause_bus = pause_daemon(f"{self.holder} (pid {os.getpid()})")
                active = self._pause_bus is None and service_is_active()
            stopped = self._pause_bus is not None
            if active or stale:
                self._restore_service = True
                # Written before the stop, so no kill can land between the
                # daemon going down and the debt being recorded.
                marker.write_text(f"{self.holder} (pid {os.getpid()})\n")
                self._owns_marker = True
            if active:
                stop_service()
                stopped = True
            if node is not None:
                # Only a daemon we just stopped is worth waiting for; anything
                # else holding the node is refused at once.
                wait_for_hidraw_release(node, self.release_timeout if stopped else 0.0)
        except BaseException:
            self.__exit__(*sys.exc_info())
            raise
        return self

    def __exit__(self, _type: object, _value: object, _traceback: object) -> None:
        restore_error: BaseException | None = None
        try:
            if self._pause_bus is not None:
                bus, self._pause_bus = self._pause_bus, None
                resume_daemon(bus)
            if self._restore_service:
                start_service()
            # Kept when the restart failed, so the next guard tries again.
            if self._owns_marker:
                restore_marker_path().unlink(missing_ok=True)
                self._owns_marker = False
        except (OSError, RuntimeError, subprocess.SubprocessError) as error:
            restore_error = error
        finally:
            for signum, handler in self._previous_handlers.items():
                signal.signal(signum, handler)
            self._previous_handlers.clear()
            if self._lock_fd >= 0:
                os.close(self._lock_fd)
                self._lock_fd = -1
        if restore_error is not None:
            raise RuntimeError(f"failed to restore service: {restore_error}") from restore_error
