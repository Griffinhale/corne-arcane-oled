"""A stand-in for `systemctl --user` that keeps one unit's state in a directory.

Tests put a `systemctl` wrapper for this script first on PATH, so the code
under test finds it the way it finds the real one. FAKE_SYSTEMCTL_DIR holds:

  state  "active", "inactive" or "missing" (no such unit)
  log    one line per call: the verb, then the state it left behind

Never touches the real user manager.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

# systemctl's exit codes: 3 is inactive, 4 is no such unit, 5 is a failed start/stop.
INACTIVE, NO_UNIT, FAILED = 3, 4, 5


def write_wrapper(bin_dir: Path) -> Path:
    """Put an executable `systemctl` in bin_dir that runs this script."""
    wrapper = bin_dir / "systemctl"
    wrapper.write_text(f"#!/bin/sh\nexec '{sys.executable}' '{Path(__file__).resolve()}' \"$@\"\n")
    wrapper.chmod(0o755)
    return wrapper


def main(argv: list[str]) -> int:
    root = Path(os.environ["FAKE_SYSTEMCTL_DIR"])
    words = [word for word in argv if not word.startswith("--")]
    verb = words[0] if words else ""
    state_file = root / "state"
    state = state_file.read_text().strip()
    code = 0
    if verb == "is-active":
        code = {"active": 0, "inactive": INACTIVE}.get(state, NO_UNIT)
    elif verb == "show":
        # The pre-guard code asked for MainPID; there is no real daemon.
        print("0")
    elif verb in ("stop", "start"):
        if state == "missing":
            print(f"Unit {words[-1]} not loaded.", file=sys.stderr)
            code = FAILED
        else:
            state = "active" if verb == "start" else "inactive"
            state_file.write_text(state + "\n")
    with (root / "log").open("a") as log:
        log.write(f"systemctl {verb} -> {state}\n")
    return code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
