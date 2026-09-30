"""Run Vial with exclusive ownership of the Corne Arcane Raw HID endpoint."""

from __future__ import annotations

import os
import shlex
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

from .hid_ownership import ExclusiveHidOwnership, OwnershipSignal, hidraw_handles

ENV_VAR = "CORNE_ARCANE_VIAL_BIN"
# How long Vial's leftover processes may run without the keyboard open before
# the launcher stops waiting for them and hands the keyboard back.
IDLE_GROUP_LIMIT = 30.0


class VialNotFound(RuntimeError):
    pass


def config_path() -> Path:
    base = os.environ.get("XDG_CONFIG_HOME") or str(Path.home() / ".config")
    return Path(base) / "corne-arcane" / "vial"


def _configured_command() -> str | None:
    # Menu launches do not inherit shell exports, so the environment variable
    # alone cannot reach them; the config file can.
    value = os.environ.get(ENV_VAR, "").strip()
    if value:
        return value
    try:
        lines = config_path().read_text().splitlines()
    except OSError:
        return None
    for line in lines:
        line = line.strip()
        if line and not line.startswith("#"):
            return line
    return None


def resolve_vial() -> list[str]:
    """Return the Vial command line, or raise VialNotFound naming both ways to set it.

    The setting is a command, so `flatpak run ...` works. A value that is itself an
    existing file is taken whole, so an AppImage path with spaces also works.
    """
    configured = _configured_command()
    if configured is None:
        words = ["vial"]
    elif Path(configured).expanduser().is_file():
        words = [str(Path(configured).expanduser())]
    else:
        words = shlex.split(configured) or ["vial"]
    words[0] = os.path.expanduser(words[0])
    found = shutil.which(words[0])
    if found is None:
        # Vial ships as an AppImage on most distributions, so unlike systemctl
        # the default name is often absent.
        raise VialNotFound(
            f"Vial executable {words[0]!r} not found. Vial is distributed as an AppImage "
            f"and is usually not on PATH: put its path (or a command such as "
            f"'flatpak run ...') in {config_path()}, or set {ENV_VAR}."
        )
    return [found, *words[1:]]


def notify_failure(message: str) -> None:
    """Show the failure on the desktop when there is no terminal to print it to."""
    if sys.stderr is not None and sys.stderr.isatty():
        return
    try:
        subprocess.run(
            ("notify-send", "--app-name=Corne Arcane", "Corne Arcane Vial", message),
            check=False,
            timeout=5.0,
        )
    except (OSError, subprocess.SubprocessError):
        pass


def group_members(pgid: int, proc_root: Path = Path("/proc")) -> tuple[int, ...]:
    """Live processes in process group pgid, from /proc/*/stat; zombies excluded."""
    members: list[int] = []
    try:
        entries = tuple(proc_root.iterdir())
    except OSError:
        return ()
    for entry in entries:
        if not entry.name.isdigit():
            continue
        try:
            # Fields after the parenthesised comm: state ppid pgrp ...
            fields = (entry / "stat").read_text().rsplit(")", 1)[1].split()
        except (OSError, IndexError):
            continue
        if len(fields) > 2 and fields[0] != "Z" and fields[2] == str(pgid):
            members.append(int(entry.name))
    return tuple(sorted(members))


def wait_for_group(
    pgid: int,
    idle_limit: float = IDLE_GROUP_LIMIT,
    poll: float = 0.2,
    proc_root: Path = Path("/proc"),
) -> None:
    """Wait until Vial's process group is empty.

    A Vial that forks and exits leaves the real window in the group. While any
    member holds a hidraw node the wait has no limit: that is Vial in use. Members
    that hold none are waited for only idle_limit seconds, then left running.
    """
    last_busy = time.monotonic()
    while members := group_members(pgid, proc_root):
        now = time.monotonic()
        if any(hidraw_handles(pid, proc_root) for pid in members):
            last_busy = now
        elif now - last_busy >= idle_limit:
            print(
                f"corne-arcane-vial: {len(members)} leftover Vial process(es) hold no keyboard "
                f"after {idle_limit:g} s; restoring the service anyway",
                file=sys.stderr,
            )
            return
        time.sleep(poll)


def _stop_group(pgid: int) -> None:
    for signum, grace in ((signal.SIGTERM, 2.0), (signal.SIGKILL, 2.0)):
        try:
            os.killpg(pgid, signum)
        except ProcessLookupError:
            return
        deadline = time.monotonic() + grace
        while group_members(pgid) and time.monotonic() < deadline:
            time.sleep(0.05)


def run_vial(command: list[str], args: list[str]) -> int:
    process: subprocess.Popen[bytes] | None = None
    finished = False
    try:
        try:
            # Its own session, so a Vial that forks and exits still leaves a
            # process group to wait on (process_group= needs Python 3.11).
            process = subprocess.Popen((*command, *args), start_new_session=True)
        except FileNotFoundError as missing:
            # Found a moment ago, gone now. The daemon has already been handed
            # off, so say it is coming back.
            raise RuntimeError(
                f"could not start Vial ({command[0]!r}); the service will be restored"
            ) from missing
        status = process.wait()
        wait_for_group(process.pid, IDLE_GROUP_LIMIT)
        finished = True
        # Popen reports a signal death as -N; exit as a shell would, with 128+N.
        return 128 - status if status < 0 else status
    finally:
        if process is not None and not finished:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=2.0)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            _stop_group(process.pid)


def main(argv: list[str] | None = None) -> int:
    args = sys.argv[1:] if argv is None else argv
    result = 1
    try:
        # Resolve before taking ownership, so a missing Vial never stops the daemon.
        command = resolve_vial()
        with ExclusiveHidOwnership(holder="Vial (corne-arcane-vial)"):
            result = run_vial(command, args)
    except OwnershipSignal as interrupted:
        result = 128 + interrupted.signum
    except (OSError, RuntimeError, subprocess.SubprocessError, TimeoutError) as error:
        print(f"corne-arcane-vial: {error}", file=sys.stderr)
        notify_failure(str(error))
        result = 1
    return result


if __name__ == "__main__":
    raise SystemExit(main())
