"""Pure M8 Raw HID packet encoding; no device or timing dependencies."""

from __future__ import annotations

from enum import IntEnum
import struct

REPORT_SIZE = 32
MAGIC = (0xCA, 0x8E)
VERSION = 1
PAYLOAD_SIZE = 20


class Message(IntEnum):
    HELLO = 1
    HEARTBEAT = 2
    NOTIFY = 3


class Scene(IntEnum):
    DUEL = 0
    ARCHIVE = 1
    FOCUS = 2


def crc8(data: bytes | bytearray) -> int:
    """CRC-8/poly-0x07, matching firmware duel_crc8 exactly."""
    crc = 0
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def build_packet(
    message: Message,
    session: int,
    sequence: int,
    scene: Scene,
    notification_count: int,
) -> bytes:
    if not 0 <= session <= 0xFFFFFFFF:
        raise ValueError("session must fit uint32")
    if not 0 <= sequence <= 0xFFFF:
        raise ValueError("sequence must fit uint16")
    if not 0 <= int(scene) < 3:
        raise ValueError("scene must be in 0..2")
    if not 0 <= notification_count <= 15:
        raise ValueError("notification count must be in 0..15")

    report = bytearray(REPORT_SIZE)
    struct.pack_into(
        "<BBBBIHB",
        report,
        0,
        MAGIC[0],
        MAGIC[1],
        VERSION,
        int(message),
        session,
        sequence,
        2,
    )
    report[11] = int(scene)
    report[12] = notification_count
    report[-1] = crc8(report[:-1])
    return bytes(report)


def hidraw_frame(report: bytes) -> bytes:
    """Linux hidraw write framing: report ID 0 followed by the 32-byte report."""
    if len(report) != REPORT_SIZE:
        raise ValueError(f"report must be {REPORT_SIZE} bytes")
    return b"\x00" + report
