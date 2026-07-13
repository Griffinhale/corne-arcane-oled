from __future__ import annotations

import unittest

from arcane_host.protocol import (
    Message,
    Scene,
    build_packet,
    crc8,
    hidraw_frame,
)


class ProtocolTests(unittest.TestCase):
    def test_known_vector(self) -> None:
        report = build_packet(Message.HELLO, 0x11223344, 0, Scene.ARCHIVE, 2)
        self.assertEqual(len(report), 32)
        self.assertEqual(report.hex(), "ca8e0101443322110000020102000000000000000000000000000000000000bc")

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


if __name__ == "__main__":
    unittest.main()
