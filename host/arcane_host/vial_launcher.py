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
        try:
            process = subprocess.Popen((VIAL, *args))
        except FileNotFoundError as missing:
            # Vial ships as an AppImage on most distributions, so unlike
            # systemctl the default name is often absent. Say so rather than
            # reporting a bare ENOENT: the daemon has already been handed off by
            # this point and the user needs to know it is coming back.
            raise RuntimeError(
                f"Vial executable {VIAL!r} not found. Set CORNE_ARCANE_VIAL_BIN to its "
                "path; Vial is distributed as an AppImage and is usually not on PATH."
            ) from missing
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
