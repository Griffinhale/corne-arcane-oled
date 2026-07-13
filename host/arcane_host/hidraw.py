"""Small Linux hidraw transport for QMK's vendor-defined Raw HID interface."""

from __future__ import annotations

import os
from pathlib import Path

from .protocol import REPORT_SIZE, hidraw_frame

# QMK descriptor: Usage Page 0xFF60, Usage 0x61.
QMK_RAW_USAGE = b"\x06\x60\xff\x09\x61"


def discover(sys_class: Path = Path("/sys/class/hidraw")) -> list[Path]:
    matches: list[Path] = []
    if not sys_class.is_dir():
        return matches
    for entry in sorted(sys_class.glob("hidraw*")):
        descriptor = entry / "device" / "report_descriptor"
        try:
            if QMK_RAW_USAGE in descriptor.read_bytes():
                matches.append(Path("/dev") / entry.name)
        except OSError:
            continue
    return matches


def choose_device(explicit: str | None = None) -> Path:
    if explicit:
        return Path(explicit)
    matches = discover()
    if not matches:
        raise RuntimeError("no QMK Raw HID interface found (usage page FF60, usage 61)")
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
