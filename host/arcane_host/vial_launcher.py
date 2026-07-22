"""Run Vial with exclusive ownership of the Corne Arcane Raw HID endpoint."""

from __future__ import annotations

import os
import signal
import subprocess
import sys
import time
from pathlib import Path

SERVICE = os.environ.get("CORNE_ARCANE_SERVICE", "corne-arcane-host.service")
SYSTEMCTL = os.environ.get("CORNE_ARCANE_SYSTEMCTL", "systemctl")
VIAL = os.environ.get("CORNE_ARCANE_VIAL_BIN", "vial")


class LauncherSignal(Exception):
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


def run_vial(args: list[str]) -> int:
    process: subprocess.Popen[bytes] | None = None
    previous: dict[int, object] = {}

    def interrupted(signum: int, _frame: object) -> None:
        raise LauncherSignal(signum)

    for signum in (signal.SIGHUP, signal.SIGINT, signal.SIGTERM):
        previous[signum] = signal.signal(signum, interrupted)
    try:
        process = subprocess.Popen((VIAL, *args))
        return process.wait()
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        for signum, handler in previous.items():
            signal.signal(signum, handler)


def main(argv: list[str] | None = None) -> int:
    args = sys.argv[1:] if argv is None else argv
    was_active = False
    result = 1
    restore_failed = False
    try:
        was_active = service_is_active()
        if was_active:
            pid = service_main_pid()
            stop_service()
            wait_for_hidraw_release(pid)
        result = run_vial(args)
    except LauncherSignal as interrupted:
        result = 128 + interrupted.signum
    except (OSError, RuntimeError, subprocess.SubprocessError, TimeoutError) as error:
        print(f"corne-arcane-vial: {error}", file=sys.stderr)
        result = 1
    finally:
        if was_active:
            try:
                start_service()
            except (OSError, subprocess.SubprocessError) as error:
                print(f"corne-arcane-vial: failed to restore service: {error}", file=sys.stderr)
                restore_failed = True
    return 1 if restore_failed else result


if __name__ == "__main__":
    raise SystemExit(main())
