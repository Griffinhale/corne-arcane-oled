"""Pure M10 Raw HID packet encoding; no device or timing dependencies."""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
import struct

REPORT_SIZE = 32
MAGIC = (0xCA, 0x8E)
VERSION = 2
LEGACY_VERSION = 1
PAYLOAD_SIZE = 20


class Message(IntEnum):
    HELLO = 1
    HEARTBEAT = 2
    NOTIFY = 3


class Scene(IntEnum):
    DUEL = 0
    ARCHIVE = 1
    FOCUS = 2


class Category(IntEnum):
    NONE = 0
    TERMINAL = 1
    COMMUNICATION = 2
    TRANSFER = 3
    SYSTEM = 4
    CALENDAR = 5
    SECURITY = 6
    OTHER = 7


class Priority(IntEnum):
    NONE = 0
    LOW = 1
    NORMAL = 2
    CRITICAL = 3


@dataclass(frozen=True)
class NotificationSummary:
    count: int = 0
    category: Category = Category.NONE
    priority: Priority = Priority.NONE
    age: int = 0
    persistent: bool = False

    def __post_init__(self) -> None:
        if not 0 <= self.count <= 15:
            raise ValueError("notification count must be in 0..15")
        if not 0 <= int(self.category) <= 7:
            raise ValueError("category must be in 0..7")
        if not 0 <= int(self.priority) <= 3:
            raise ValueError("priority must be in 0..3")
        if not 0 <= self.age <= 7:
            raise ValueError("age must be in 0..7")
        if self.count == 0:
            if (self.category, self.priority, self.age, self.persistent) != (
                Category.NONE, Priority.NONE, 0, False
            ):
                raise ValueError("an empty summary must have canonical zero fields")
        elif self.category == Category.NONE or self.priority == Priority.NONE:
            raise ValueError("a nonempty summary requires category and priority")
        if self.persistent and self.priority != Priority.CRITICAL:
            raise ValueError("only critical notifications may persist")


EMPTY_SUMMARY = NotificationSummary()


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
    notification_count: int | None = None,
    *,
    summary: NotificationSummary | None = None,
) -> bytes:
    """Build a canonical v2 report.

    ``notification_count`` remains as the M9 call-site compatibility form and
    maps nonzero counts to an ``other/normal`` diagnostic summary.
    """
    if not 0 <= session <= 0xFFFFFFFF:
        raise ValueError("session must fit uint32")
    if not 0 <= sequence <= 0xFFFF:
        raise ValueError("sequence must fit uint16")
    if not 0 <= int(scene) < 3:
        raise ValueError("scene must be in 0..2")
    if summary is not None and notification_count is not None:
        raise ValueError("pass either notification_count or summary")
    if summary is None:
        count = notification_count or 0
        summary = (
            EMPTY_SUMMARY
            if count == 0
            else NotificationSummary(count, Category.OTHER, Priority.NORMAL)
        )

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
        6,
    )
    report[11:17] = bytes(
        (
            int(scene),
            summary.count,
            int(summary.category),
            int(summary.priority),
            summary.age,
            int(summary.persistent),
        )
    )
    report[-1] = crc8(report[:-1])
    return bytes(report)


def build_legacy_packet(
    message: Message, session: int, sequence: int, scene: Scene, notification_count: int
) -> bytes:
    """Build a v1 vector for compatibility tests and rollback diagnostics."""
    if not 0 <= notification_count <= 15:
        raise ValueError("notification count must be in 0..15")
    report = bytearray(build_packet(message, session, sequence, scene, notification_count))
    report[2] = LEGACY_VERSION
    report[10] = 2
    report[13:17] = b"\0\0\0\0"
    report[-1] = crc8(report[:-1])
    return bytes(report)


def hidraw_frame(report: bytes) -> bytes:
    """Linux hidraw write framing: report ID 0 followed by the 32-byte report."""
    if len(report) != REPORT_SIZE:
        raise ValueError(f"report must be {REPORT_SIZE} bytes")
    return b"\x00" + report
