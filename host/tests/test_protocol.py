from __future__ import annotations

import ast
import ctypes
import inspect
import unittest
from pathlib import Path

from arcane_host.city import candidate_paths
from arcane_host.protocol import (
    MAGIC,
    PAYLOAD_SIZE,
    REPORT_SIZE,
    VERSION,
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


def load_c() -> ctypes.CDLL | None:
    """The firmware's own validator and constants, via the desktop library."""
    for path in candidate_paths():
        if path.is_file():
            library = ctypes.CDLL(str(path))
            library.duel_city_wire_constant.argtypes = [ctypes.c_char_p]
            library.duel_city_wire_constant.restype = ctypes.c_long
            library.duel_host_packet_valid.argtypes = [ctypes.c_char_p]
            library.duel_host_packet_valid.restype = ctypes.c_bool
            return library
    return None


C = load_c()
requires_c = unittest.skipUnless(C, "libcornearcane.so is not built; run `make city-lib`")


@requires_c
class CContractTests(unittest.TestCase):
    """Python's restated wire constants and packets, judged by the C that receives them."""

    def c(self, name: str) -> int:
        value = C.duel_city_wire_constant(name.encode())
        self.assertNotEqual(value, -1, f"C does not export {name}")
        return value

    def test_constants_match_c(self) -> None:
        self.assertEqual(REPORT_SIZE, self.c("REPORT_SIZE"))
        self.assertEqual(MAGIC, (self.c("MAGIC0"), self.c("MAGIC1")))
        self.assertEqual(VERSION, self.c("VERSION"))
        self.assertEqual(PAYLOAD_SIZE, self.c("PAYLOAD_SIZE"))
        for message in Message:
            self.assertEqual(message, self.c(f"MSG_{message.name}"))
        self.assertEqual(len(Category), self.c("CATEGORY_COUNT"))
        self.assertEqual(len(Priority), self.c("PRIORITY_COUNT"))
        self.assertEqual(len(Scene), self.c("SCENE_COUNT"))
        self.assertEqual(C.duel_city_wire_constant(b"NO_SUCH_CONSTANT"), -1)

    def test_c_validator_accepts(self) -> None:
        civic = CivicState(Floor.WORKSHOP, Mode.URGENT, Intensity.BUSY, Secondary.SYSTEM)
        summary = NotificationSummary(15, Category.SECURITY, Priority.CRITICAL, 7, True)
        packets = [build_packet(Message.HELLO, 0x11223344, 0, Scene.ARCHIVE, 2)]
        for message in Message:
            for scene in Scene:
                packets.append(build_packet(message, 7, 65535, scene, 0, civic=civic))
            packets.append(build_packet(message, 0xFFFFFFFF, 1, Scene.FOCUS, summary=summary))
        for report in packets:
            with self.subTest(report=report.hex()):
                self.assertTrue(C.duel_host_packet_valid(report))

        # The validator is not a rubber stamp: a version Python does not send
        # is refused even with a correct CRC.
        wrong = bytearray(packets[0])
        wrong[2] = VERSION + 1
        wrong[-1] = crc8(wrong[:-1])
        self.assertFalse(C.duel_host_packet_valid(bytes(wrong)))


class ProtocolTests(unittest.TestCase):
    def test_known_vector(self) -> None:
        report = build_packet(Message.HELLO, 0x11223344, 0, Scene.ARCHIVE, 2)
        self.assertEqual(len(report), 32)
        self.assertEqual(
            report.hex(), "ca8e030144332211000008010207020000000000000000000000000000000095"
        )

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
        self.assertEqual(
            report[10:17], bytes((8, Scene.FOCUS, 15, Category.SECURITY, Priority.CRITICAL, 7, 1))
        )

    def test_civic_pack_matches_duel_host_macros(self) -> None:
        # Mirrors DUEL_CIVIC_PACK / DUEL_SECONDARY_PACK bit-for-bit: floor bits
        # 0-1, mode bits 2-3, intensity bits 4-5; secondary bits 0-2.
        self.assertEqual(CivicState().civic_byte(), 0x00)
        self.assertEqual(CivicState(floor=Floor.RESEARCH).civic_byte(), 0x01)
        self.assertEqual(CivicState(floor=Floor.WORKSHOP).civic_byte(), 0x02)
        self.assertEqual(CivicState(mode=Mode.QUIET).civic_byte(), 0x04)
        self.assertEqual(CivicState(mode=Mode.URGENT).civic_byte(), 0x08)
        self.assertEqual(CivicState(mode=Mode.STRAIN).civic_byte(), 0x0C)
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
        self.assertEqual(
            report[11:17], bytes((Scene.ARCHIVE, 2, Category.OTHER, Priority.NORMAL, 0, 0))
        )
        self.assertEqual(report[-1], crc8(report[:-1]))
        default = build_packet(Message.HEARTBEAT, 0x11223344, 0, Scene.ARCHIVE, 2)
        self.assertEqual(default[10], 8)
        self.assertEqual((default[17], default[18]), (0, 0))

    def test_civic_bounds(self) -> None:
        with self.assertRaises(ValueError):
            CivicState(secondary=8)  # exceeds the 3-bit secondary field


# The only modules that may know the typing summary exists: the reducer, the
# helper that sends it, the bus names, and the desktop window that listens.
TYPING_MODULES = {"typing_summary", "typing_helper", "dbus_contract", "city_window"}


class TypingSummaryTests(unittest.TestCase):
    def test_summary_never_on_wire(self) -> None:
        # The report has no field a summary could ride in: its builder takes
        # the scene, the notification summary and the civic state, nothing more.
        self.assertEqual(
            set(inspect.signature(build_packet).parameters),
            {"message", "session", "sequence", "scene", "notification_count", "summary", "civic"},
        )
        # And nothing that builds, sends or serves the heartbeat can reach one:
        # outside the helper and the window, no module imports the reducer or
        # the helper, or names the helper's bus.
        package = Path(build_packet.__code__.co_filename).parent
        for path in sorted(package.glob("*.py")):
            if path.stem in TYPING_MODULES:
                continue
            source = path.read_text()
            tree = ast.parse(source)
            imported = {
                node.module or "" for node in ast.walk(tree) if isinstance(node, ast.ImportFrom)
            } | {
                alias.name
                for node in ast.walk(tree)
                if isinstance(node, ast.Import)
                for alias in node.names
            }
            for module in ("typing_summary", "typing_helper", "city_window"):
                self.assertFalse(
                    any(name.endswith(module) for name in imported), f"{path.name} imports {module}"
                )
            self.assertNotIn("TYPING_", source, path.name)


if __name__ == "__main__":
    unittest.main()
