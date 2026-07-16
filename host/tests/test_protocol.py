from __future__ import annotations

import unittest

from arcane_host.protocol import (
    Category,
    CivicState,
    Floor,
    Intensity,
    Message,
    Mode,
    NotificationSummary,
    Priority,
    Scene,
    Secondary,
    build_packet,
    crc8,
    hidraw_frame,
)


class ProtocolTests(unittest.TestCase):
    def test_known_vector(self) -> None:
        report = build_packet(Message.HELLO, 0x11223344, 0, Scene.ARCHIVE, 2)
        self.assertEqual(len(report), 32)
        self.assertEqual(report.hex(), "ca8e0201443322110000080102070200000000000000000000000000000000ba")

    def test_crc_covers_every_payload_byte(self) -> None:
        report = bytearray(build_packet(Message.HEARTBEAT, 9, 17, Scene.FOCUS, 3))
        self.assertEqual(report[-1], crc8(report[:-1]))
        report[12] ^= 1
        self.assertNotEqual(report[-1], crc8(report[:-1]))

    def test_hidraw_report_id_prefix(self) -> None:
        report = build_packet(Message.NOTIFY, 1, 1, Scene.DUEL, 4)
        frame = hidraw_frame(report)
        self.assertEqual(len(frame), 33)
        self.assertEqual(frame[0], 0)
        self.assertEqual(frame[1:], report)

    def test_bounds(self) -> None:
        with self.assertRaises(ValueError):
            build_packet(Message.HELLO, -1, 0, Scene.DUEL, 0)
        with self.assertRaises(ValueError):
            build_packet(Message.HELLO, 1, 0x10000, Scene.DUEL, 0)
        with self.assertRaises(ValueError):
            build_packet(Message.HELLO, 1, 0, Scene.DUEL, 16)
        with self.assertRaises(ValueError):
            NotificationSummary(1, Category.NONE, Priority.NORMAL)
        with self.assertRaises(ValueError):
            NotificationSummary(1, Category.SECURITY, Priority.NORMAL, persistent=True)

    def test_complete_absolute_summary(self) -> None:
        summary = NotificationSummary(15, Category.SECURITY, Priority.CRITICAL, 7, True)
        report = build_packet(Message.NOTIFY, 4, 9, Scene.FOCUS, summary=summary)
        self.assertEqual(report[10:17], bytes((8, Scene.FOCUS, 15, Category.SECURITY,
                                              Priority.CRITICAL, 7, 1)))

    def test_civic_pack_matches_duel_host_macros(self) -> None:
        # Mirrors DUEL_CIVIC_PACK / DUEL_SECONDARY_PACK bit-for-bit: floor bits
        # 0-1, mode bits 2-3, intensity bits 4-5; secondary bits 0-2.
        self.assertEqual(CivicState().civic_byte(), 0x00)
        self.assertEqual(CivicState(floor=Floor.RESEARCH).civic_byte(), 0x01)
        self.assertEqual(CivicState(floor=Floor.WORKSHOP).civic_byte(), 0x02)
        self.assertEqual(CivicState(mode=Mode.QUIET).civic_byte(), 0x04)
        self.assertEqual(CivicState(mode=Mode.URGENT).civic_byte(), 0x08)
        self.assertEqual(CivicState(intensity=Intensity.BUSY).civic_byte(), 0x20)
        # All three subfields at once (WORKSHOP|URGENT|BUSY) -> 2|8|32 = 0x2A.
        civic = CivicState(Floor.WORKSHOP, Mode.URGENT, Intensity.BUSY, Secondary.SYSTEM)
        self.assertEqual(civic.civic_byte(), 0x2A)
        self.assertEqual(civic.secondary_byte(), 0x03)
        self.assertEqual(CivicState(secondary=Secondary.MEDIA).secondary_byte(), 0x01)

    def test_civic_known_vector_and_required_payload(self) -> None:
        civic = CivicState(Floor.WORKSHOP, Mode.URGENT, Intensity.BUSY, Secondary.SYSTEM)
        report = build_packet(Message.HEARTBEAT, 0x11223344, 0, Scene.ARCHIVE, 2, civic=civic)
        # payload_len advertises 8 and the civic bytes land at payload[6]/[7].
        self.assertEqual(report[10], 8)
        self.assertEqual(report[17], 0x2A)
        self.assertEqual(report[18], 0x03)
        # The semantic summary remains in payload[0..5].
        self.assertEqual(report[11:17], bytes((Scene.ARCHIVE, 2, Category.OTHER,
                                              Priority.NORMAL, 0, 0)))
        self.assertEqual(report[-1], crc8(report[:-1]))
        default = build_packet(Message.HEARTBEAT, 0x11223344, 0, Scene.ARCHIVE, 2)
        self.assertEqual(default[10], 8)
        self.assertEqual((default[17], default[18]), (0, 0))

    def test_civic_bounds(self) -> None:
        with self.assertRaises(ValueError):
            CivicState(secondary=8)  # exceeds the 3-bit secondary field


if __name__ == "__main__":
    unittest.main()
