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


def hidraw_openers(node: Path, proc_root: Path = Path("/proc")) -> tuple[str, ...]:
    """Name every other process with node open, as "comm (pid N)".

    Reads /proc/*/fd links only; the device itself is never opened. Processes
    of other users are unreadable and so are not seen.
    """
    openers: list[str] = []
    try:
        entries = sorted(proc_root.iterdir())
    except OSError:
        return ()
    for entry in entries:
        if not entry.name.isdigit() or int(entry.name) == os.getpid():
            continue
        if node not in hidraw_handles(int(entry.name), proc_root):
            continue
        try:
            name = (entry / "comm").read_text().strip()
        except OSError:
            name = "unknown"
        openers.append(f"{name} (pid {entry.name})")
    return tuple(openers)


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


class ExclusiveHidOwnership:
    """Hold the keyboard alone: take the lock, stop an active daemon, wait for the node.

    The lock keeps the tools that share this guard (the Vial launcher,
    diagnostics) from overlapping; the /proc scan catches anything else that
    has the node open. On exit the daemon's prior active state is restored.
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
            stopped = False
            if self.service_handoff and service_is_active():
                self._restore_service = True
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
            if self._restore_service:
                start_service()
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
