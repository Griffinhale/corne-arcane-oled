"""Run Vial with exclusive ownership of the Corne Arcane Raw HID endpoint."""

from __future__ import annotations

import os
import subprocess
import sys

from .hid_ownership import ExclusiveHidOwnership, OwnershipSignal

VIAL = os.environ.get("CORNE_ARCANE_VIAL_BIN", "vial")


def run_vial(args: list[str]) -> int:
    process: subprocess.Popen[bytes] | None = None
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


def main(argv: list[str] | None = None) -> int:
    args = sys.argv[1:] if argv is None else argv
    result = 1
    try:
        with ExclusiveHidOwnership():
            result = run_vial(args)
    except OwnershipSignal as interrupted:
        result = 128 + interrupted.signum
    except (OSError, RuntimeError, subprocess.SubprocessError, TimeoutError) as error:
        print(f"corne-arcane-vial: {error}", file=sys.stderr)
        result = 1
    return result


if __name__ == "__main__":
    raise SystemExit(main())
