"""Exclusive Raw HID ownership shared by diagnostics and the Vial launcher."""

from __future__ import annotations

import os
import signal
import subprocess
import sys
import time
from pathlib import Path
from types import FrameType

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
    if result.returncode == 3:
        return False
    detail = result.stderr.strip() or f"systemctl exited {result.returncode}"
    raise RuntimeError(f"cannot determine daemon state: {detail}")


def service_main_pid() -> int:
    result = _systemctl("show", "--property=MainPID", "--value", SERVICE, check=True)
    try:
        return int(result.stdout.strip())
    except ValueError:
        return 0


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


def wait_for_hidraw_release(pid: int, timeout: float = 5.0) -> None:
    deadline = time.monotonic() + timeout
    while hidraw_handles(pid):
        if time.monotonic() >= deadline:
            raise TimeoutError("daemon did not release its hidraw handle")
        time.sleep(0.05)


class ExclusiveHidOwnership:
    """Stop an active daemon, then restore exactly that prior active state."""

    def __init__(self, *, service_handoff: bool = True, release_timeout: float = 5.0):
        self.service_handoff = service_handoff
        self.release_timeout = release_timeout
        self._restore_service = False
        self._previous_handlers: dict[int, signal.Handlers] = {}

    def _interrupted(self, signum: int, _frame: FrameType | None) -> None:
        raise OwnershipSignal(signum)

    def __enter__(self) -> ExclusiveHidOwnership:
        for signum in (signal.SIGHUP, signal.SIGINT, signal.SIGTERM):
            self._previous_handlers[signum] = signal.signal(signum, self._interrupted)
        try:
            if self.service_handoff and service_is_active():
                pid = service_main_pid()
                self._restore_service = True
                stop_service()
                wait_for_hidraw_release(pid, self.release_timeout)
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
        if restore_error is not None:
            raise RuntimeError(f"failed to restore service: {restore_error}") from restore_error
