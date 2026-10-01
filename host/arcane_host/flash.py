"""Flash a release image to both halves of the Corne, keeping the last good one.

The steps are the ones docs/flashing.md gives, driven in order: check the
image, stop the daemon (through the same guard Vial and diagnostics use), wait
for each half's RPI-RP2 drive, copy the image onto it, wait for the half to
reboot out of the bootloader, then give the daemon back.

The image this tool flashed last is kept as current.uf2 in the state
directory, and the one before it as last-good.uf2: the way back if a new image
misbehaves. Nothing here opens the keyboard; the drive is found through the
mount table and its INFO_UF2.TXT, and only ever written to.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import struct
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from .hid_ownership import ExclusiveHidOwnership, OwnershipSignal

KEYMAP_WARNING = (
    "Flashing resets your Vial keymap to the default. "
    "Save your layout in Vial first (File > Save current layout)."
)

# The names docs/flashing.md gives the image: a release download, a local
# build, and the image a rebuild keeps (scripts/release_build.sh).
IMAGE_NAMES = frozenset(
    {
        "corne_arcane.uf2",
        "griffin_arcane-release.uf2",
        "griffin_arcane-release.prev.uf2",
        "griffin_arcane-diagnostic.uf2",
        "griffin_arcane-diagnostic.prev.uf2",
    }
)
CURRENT = "current.uf2"
LAST_GOOD = "last-good.uf2"

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_FAMILY = 0x00002000
RP2040_FAMILY = 0xE48BFF56
UF2_BLOCK = 512

MOUNTS_ENV = "CORNE_ARCANE_MOUNTS"
BOOT_HINT = (
    "Hold BOOT on the controller while you plug in its USB cable. "
    "If the drive still does not show, mount it by hand (docs/flashing.md)."
)
WAIT_SECONDS = 180.0
REBOOT_SECONDS = 15.0
POLL_SECONDS = 0.25


class WrongImage(ValueError):
    pass


class BootloaderMissing(RuntimeError):
    pass


class FlashFailed(RuntimeError):
    pass


@dataclass(frozen=True)
class Flashed:
    halves: int
    sha256: str
    # The image to flash to go back, if this tool has flashed an earlier one.
    last_good: Path | None


def state_dir() -> Path:
    base = os.environ.get("XDG_STATE_HOME") or str(Path.home() / ".local" / "state")
    return Path(base) / "corne-arcane" / "flash"


def check_image(path: Path, state: Path | None = None) -> bytes:
    """The image's bytes, once it is known to be a whole RP2040 UF2 by one of
    this project's names (or one this tool kept); else WrongImage saying why."""
    path = Path(path)
    kept = state is not None and path.parent.resolve() == Path(state).resolve()
    if path.name not in IMAGE_NAMES and not (kept and path.name in (CURRENT, LAST_GOOD)):
        names = ", ".join(sorted(IMAGE_NAMES - {n for n in IMAGE_NAMES if "prev" in n}))
        raise WrongImage(f"{path.name} is not a Corne Arcane image (expected {names})")
    try:
        data = path.read_bytes()
    except OSError as error:
        raise WrongImage(f"cannot read {path}: {error.strerror or error}") from None
    if not data or len(data) % UF2_BLOCK:
        raise WrongImage(f"{path.name} is not a UF2 image")
    count = len(data) // UF2_BLOCK
    for number in range(count):
        block = data[number * UF2_BLOCK : (number + 1) * UF2_BLOCK]
        start0, start1, flags, _addr, size, block_no, total, family = struct.unpack_from(
            "<8I", block
        )
        (end,) = struct.unpack_from("<I", block, UF2_BLOCK - 4)
        if (start0, start1, end) != (UF2_MAGIC_START0, UF2_MAGIC_START1, UF2_MAGIC_END):
            raise WrongImage(f"{path.name} is not a UF2 image")
        if not flags & UF2_FLAG_FAMILY or family != RP2040_FAMILY:
            raise WrongImage(f"{path.name} is not for the RP2040")
        if block_no != number or total != count or size > 476:
            raise WrongImage(f"{path.name} is damaged (block {number} of {count} is wrong)")
    return data


def _unescape(field: str) -> str:
    # The mount table writes space, tab, newline and backslash as octal.
    for code, char in (("\\040", " "), ("\\011", "\t"), ("\\012", "\n"), ("\\134", "\\")):
        field = field.replace(code, char)
    return field


def find_bootloader(mounts: Path | None = None) -> Path | None:
    """The mounted RPI-RP2 drive, or None. Reads the mount table and each
    candidate's INFO_UF2.TXT; a drive that cannot be read is not there."""
    table = Path(mounts or os.environ.get(MOUNTS_ENV) or "/proc/self/mounts")
    try:
        lines = table.read_text().splitlines()
    except OSError:
        return None
    for line in lines:
        fields = line.split()
        if len(fields) < 3 or fields[2] not in ("vfat", "msdos", "fat"):
            continue
        mount = Path(_unescape(fields[1]))
        try:
            info = (mount / "INFO_UF2.TXT").read_text(errors="replace")
        except OSError:
            continue
        if "Board-ID: RPI-RP2" in info:
            return mount
    return None


def _wait(
    want_drive: bool,
    find: Callable[[], Path | None],
    timeout: float,
    clock: Callable[[], float],
    sleep: Callable[[float], None],
) -> Path | None:
    deadline = clock() + timeout
    while True:
        drive = find()
        if (drive is not None) == want_drive:
            return drive
        if clock() >= deadline:
            raise TimeoutError
        sleep(POLL_SECONDS)


def _write(drive: Path, name: str, data: bytes) -> None:
    try:
        with open(drive / name, "wb") as out:
            out.write(data)
            out.flush()
            os.fsync(out.fileno())
    except OSError as error:
        # The half reboots the moment the last block lands, so the drive may
        # vanish under the write; docs/flashing.md says that is normal. A
        # drive that is still there means the copy really failed.
        if (drive / "INFO_UF2.TXT").exists():
            raise FlashFailed(f"could not copy to {drive}: {error.strerror or error}") from None


def _keep(state: Path, data: bytes) -> Path | None:
    """Record data as the image on the board; the one it replaces is the way back."""
    state.mkdir(parents=True, exist_ok=True)
    current, last_good = state / CURRENT, state / LAST_GOOD
    try:
        previous = current.read_bytes()
    except FileNotFoundError:
        previous = None
    if previous is not None and previous != data:
        _replace(last_good, previous)
    _replace(current, data)
    return last_good if last_good.exists() else None


def _replace(path: Path, data: bytes) -> None:
    partial = path.with_name(path.name + ".part")
    partial.write_bytes(data)
    os.replace(partial, path)


HALF_NAMES = ("first", "second")


def flash_image(
    image: Path,
    *,
    halves: int = 2,
    find: Callable[[], Path | None] = find_bootloader,
    state: Path | None = None,
    timeout: float = WAIT_SECONDS,
    clock: Callable[[], float] = time.monotonic,
    sleep: Callable[[float], None] = time.sleep,
    report: Callable[[str], None] = print,
) -> Flashed:
    """Copy image onto each half in turn. The daemon must already be stopped."""
    state = Path(state or state_dir())
    data = check_image(image, state)
    digest = hashlib.sha256(data).hexdigest()
    # Always written under the release name, whatever the file was called.
    name = "corne_arcane.uf2"
    last_good = None
    report("Keep TRRS disconnected while either half has USB power.")
    for index in range(halves):
        half = HALF_NAMES[index] if index < len(HALF_NAMES) else f"half {index + 1}"
        if index:
            report(f"The {HALF_NAMES[index - 1]} half is flashed. Unplug it.")
        report(f"Hold BOOT on the {half} half and plug in its USB cable")
        try:
            drive = _wait(True, find, timeout, clock, sleep)
        except TimeoutError:
            raise BootloaderMissing(
                f"No RPI-RP2 drive appeared for the {half} half in {timeout:.0f} s. {BOOT_HINT}"
            ) from None
        report(f"Copying {Path(image).name} to the {half} half")
        _write(drive, name, data)
        try:
            _wait(False, find, REBOOT_SECONDS, clock, sleep)
        except TimeoutError:
            raise FlashFailed(
                f"The {half} half did not restart after the copy; the image may not have taken"
            ) from None
        if index == 0:
            last_good = _keep(state, data)
    report("Both halves flashed" if halves == 2 else "One half flashed")
    return Flashed(halves=halves, sha256=digest, last_good=last_good)


def _say(line: str) -> None:
    print(line, flush=True)


def main(
    argv: list[str] | None = None,
    *,
    find: Callable[[], Path | None] = find_bootloader,
    clock: Callable[[], float] = time.monotonic,
    sleep: Callable[[float], None] = time.sleep,
    guard=ExclusiveHidOwnership,
) -> int:
    parser = argparse.ArgumentParser(
        prog="corne-arcane-flash", description="Flash a release image to both Corne halves."
    )
    which = parser.add_mutually_exclusive_group(required=True)
    which.add_argument("image", nargs="?", type=Path, help="the .uf2 to flash")
    which.add_argument(
        "--last-good", action="store_true", help="flash the image this tool flashed before"
    )
    parser.add_argument("--halves", type=int, choices=(1, 2), default=2)
    parser.add_argument("--timeout", type=float, default=WAIT_SECONDS, help="seconds per half")
    parser.add_argument("--state", type=Path, default=None, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    state = args.state or state_dir()
    image = state / LAST_GOOD if args.last_good else args.image
    try:
        # Checked before the guard, so a wrong file never stops the daemon.
        check_image(image, state)
        with guard(holder="corne-arcane-flash"):
            result = flash_image(
                image,
                halves=args.halves,
                find=find,
                state=state,
                timeout=args.timeout,
                clock=clock,
                sleep=sleep,
                report=_say,
            )
    except OwnershipSignal as interrupted:
        return 128 + interrupted.signum
    except (OSError, RuntimeError, ValueError) as error:
        print(f"corne-arcane-flash: {error}", file=sys.stderr)
        return 1
    _say(f"sha256 {result.sha256}")
    if result.last_good is not None:
        _say(f"The image before this one is kept at {result.last_good}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
