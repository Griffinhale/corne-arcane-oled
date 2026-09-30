"""Small Linux hidraw transport for QMK's vendor-defined Raw HID interface."""

from __future__ import annotations

import os
import re
import select
from pathlib import Path

from .protocol import REPORT_SIZE, hidraw_frame

# QMK descriptor: Usage Page 0xFF60, Usage 0x61.
QMK_RAW_USAGE = b"\x06\x60\xff\x09\x61"

# The Corne's USB IDs, from firmware/vial.json and Vial-QMK's crkbd rev1.
# Every QMK board with Raw HID shares the usage above, so the usage alone
# cannot tell this keyboard from a second QMK board on the same desk.
CORNE_USB_ID = (0x4653, 0x0001)
# Overrides CORNE_USB_ID as "vvvv:pppp" in hex, for a board built with other IDs.
USB_ID_ENV = "CORNE_ARCANE_USB_ID"


def parse_usb_id(text: str) -> tuple[int, int]:
    match = re.fullmatch(r"(?:0x)?([0-9a-fA-F]{1,4}):(?:0x)?([0-9a-fA-F]{1,4})", text.strip())
    if not match:
        raise ValueError(f"{USB_ID_ENV} must be vvvv:pppp in hex, not {text!r}")
    return int(match.group(1), 16), int(match.group(2), 16)


def _usb_id(device_dir: Path) -> tuple[int, int] | None:
    """Read vendor and product from the hidraw parent's uevent HID_ID line."""
    for line in (device_dir / "uevent").read_text().splitlines():
        if line.startswith("HID_ID="):
            fields = line.removeprefix("HID_ID=").split(":")
            if len(fields) == 3:
                return int(fields[1], 16), int(fields[2], 16)
    return None


def discover(
    sys_class: Path = Path("/sys/class/hidraw"),
    usb_id: tuple[int, int] | None = CORNE_USB_ID,
) -> list[Path]:
    """List Raw HID interfaces, limited to usb_id unless it is None."""
    matches: list[Path] = []
    if not sys_class.is_dir():
        return matches
    for entry in sorted(sys_class.glob("hidraw*")):
        device_dir = entry / "device"
        try:
            if QMK_RAW_USAGE not in (device_dir / "report_descriptor").read_bytes():
                continue
            if usb_id is not None and _usb_id(device_dir) != usb_id:
                continue
        except (OSError, ValueError):
            continue
        matches.append(Path("/dev") / entry.name)
    return matches


def choose_device(explicit: str | None = None) -> Path:
    if explicit:
        return Path(explicit)
    override = os.environ.get(USB_ID_ENV)
    usb_id = parse_usb_id(override) if override else CORNE_USB_ID
    matches = discover(usb_id=usb_id)
    if not matches:
        raise RuntimeError(
            "no Corne Raw HID interface found "
            f"(USB {usb_id[0]:04x}:{usb_id[1]:04x}, usage page FF60, usage 61)"
        )
    if len(matches) > 1:
        joined = ", ".join(str(path) for path in matches)
        raise RuntimeError(f"multiple QMK Raw HID interfaces found: {joined}; pass --device")
    return matches[0]


class Device:
    def __init__(self, path: Path):
        self.path = path
        self.fd = os.open(path, os.O_RDWR)

    def close(self) -> None:
        if self.fd >= 0:
            os.close(self.fd)
            self.fd = -1

    def __enter__(self) -> "Device":
        return self

    def __exit__(self, exc_type, exc, traceback) -> None:
        self.close()

    def send(self, report: bytes) -> None:
        if len(report) != REPORT_SIZE:
            raise ValueError(f"report must be {REPORT_SIZE} bytes")
        frame = hidraw_frame(report)
        written = os.write(self.fd, frame)
        if written != len(frame):
            raise OSError(f"short hidraw write: {written}/{len(frame)} bytes")

    def receive(self, timeout: float) -> bytes:
        if timeout < 0:
            raise ValueError("timeout must be nonnegative")
        readable, _, _ = select.select((self.fd,), (), (), timeout)
        if not readable:
            raise TimeoutError("timed out waiting for Raw HID input report")
        frame = os.read(self.fd, REPORT_SIZE + 1)
        # Linux hidraw includes a leading report ID only when the descriptor
        # defines report IDs. QMK Raw HID does not, but accept an explicit zero
        # prefix to keep test transports and unusual kernels unambiguous.
        if len(frame) == REPORT_SIZE + 1 and frame[0] == 0:
            frame = frame[1:]
        if len(frame) != REPORT_SIZE:
            raise OSError(f"short hidraw read: {len(frame)}/{REPORT_SIZE} bytes")
        return frame
