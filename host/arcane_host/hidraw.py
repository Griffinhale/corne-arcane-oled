"""Small Linux hidraw transport for QMK's vendor-defined Raw HID interface."""

from __future__ import annotations

import os
import select
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
