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


class Floor(IntEnum):
    """Active tower-floor occupation; civic byte bits 0-1 (see duel_host.h)."""

    COMMONS = 0
    RESEARCH = 1
    WORKSHOP = 2
    SPECIAL = 3


class Mode(IntEnum):
    """Civic mode; civic byte bits 2-3. RESERVED is unused under M12."""

    NORMAL = 0
    QUIET = 1
    URGENT = 2
    RESERVED = 3


class Intensity(IntEnum):
    """Secondary host-activity intensity; civic byte bits 4-5."""

    CALM = 0
    ACTIVE = 1
    BUSY = 2
    SATURATED = 3


class Secondary(IntEnum):
    """Secondary activity channel; secondary byte bits 0-2."""

    NONE = 0
    MEDIA = 1
    TRANSFER = 2
    SYSTEM = 3
    CALENDAR = 4


@dataclass(frozen=True, slots=True)
class CivicState:
    """M12 Twin Cities civic bytes: pure enum tuple, never any string.

    Packs exactly as ``duel_host.h``: civic byte bits0-1 floor, bits2-3 mode,
    bits4-5 host intensity (bits6-7 reserved); secondary byte bits0-2 activity.
    """

    floor: Floor = Floor.COMMONS
    mode: Mode = Mode.NORMAL
    intensity: Intensity = Intensity.CALM
    secondary: Secondary = Secondary.NONE

    def __post_init__(self) -> None:
        if not 0 <= int(self.floor) <= 3:
            raise ValueError("floor must be in 0..3")
        if not 0 <= int(self.mode) <= 3:
            raise ValueError("mode must be in 0..3")
        if not 0 <= int(self.intensity) <= 3:
            raise ValueError("intensity must be in 0..3")
        if not 0 <= int(self.secondary) <= 7:
            raise ValueError("secondary activity must be in 0..7")

    def civic_byte(self) -> int:
        """DUEL_CIVIC_PACK(floor, mode, intensity)."""
        return (int(self.floor) & 3) | ((int(self.mode) & 3) << 2) | (
            (int(self.intensity) & 3) << 4
        )

    def secondary_byte(self) -> int:
        """DUEL_SECONDARY_PACK(activity)."""
        return int(self.secondary) & 7


DEFAULT_CIVIC = CivicState()


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
    civic: CivicState | None = None,
) -> bytes:
    """Build a canonical v2 report.

    ``notification_count`` remains as the M9 call-site compatibility form and
    maps nonzero counts to an ``other/normal`` diagnostic summary.

    When ``civic`` is provided the report carries the M12 Twin Cities civic
    bytes at ``payload[6]``/``payload[7]`` and advertises ``payload_len == 8``.
    Omitting it keeps the bit-identical M11.5 six-byte payload, so firmware that
    predates M12 (or ignores the extra bytes) is unaffected.
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

    payload_len = 6 if civic is None else 8
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
        payload_len,
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
    if civic is not None:
        # payload[6] -> report[17], payload[7] -> report[18].
        report[17] = civic.civic_byte()
        report[18] = civic.secondary_byte()
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
