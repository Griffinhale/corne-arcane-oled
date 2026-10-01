"""The opt-in typing helper: what it reads, what it keeps, what leaves it.

No test opens a real evdev device. Keyboards are a fake sysfs tree, key events
are packed input_event structs fed straight to the collector, and the bus is a
private dbus-daemon.
"""

from __future__ import annotations

import os
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

from arcane_host import typing_helper
from arcane_host.dbus_contract import (
    TYPING_BUS_NAME,
    TYPING_INTERFACE,
    TYPING_OBJECT_PATH,
    TYPING_SIGNATURE,
    TYPING_SUMMARY,
)
from arcane_host.typing_helper import (
    EVENT,
    Collector,
    HelperError,
    choose,
    encode,
    keyboards,
    row_of,
)
from arcane_host.typing_summary import Row, RowSpread, Spread, Tempo, TypingSummary

try:
    import gi

    gi.require_version("Gio", "2.0")
    from gi.repository import Gio, GLib
except (ImportError, ValueError):
    Gio = GLib = None

# evdev codes, restated so the test does not lean on the module's own table.
KEY_ESC, KEY_1, KEY_Q, KEY_A, KEY_Z, KEY_SPACE, KEY_UP, KEY_F1 = 1, 2, 16, 30, 44, 57, 103, 59
LETTERS = (16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 30, 31, 32, 33, 34, 35, 36, 37, 38)
LETTERS += (44, 45, 46, 47, 48, 49, 50)
LONG_BITS = struct.calcsize("l") * 8
MINUTE = 60_000


def capability_text(codes: tuple[int, ...]) -> str:
    """A key capability bitmap the way the kernel prints it: hex longs, high first."""
    value = 0
    for code in codes:
        value |= 1 << code
    words = []
    while value:
        words.append(value & ((1 << LONG_BITS) - 1))
        value >>= LONG_BITS
    return " ".join(f"{word:x}" for word in reversed(words or [0]))


def packed(ms: int, code: int, value: int, kind: int = 1) -> bytes:
    return EVENT.pack(ms // 1000, (ms % 1000) * 1000, kind, code, value)


class FakeInput:
    """A /sys/class/input with a Corne, another keyboard and a mouse."""

    def __init__(self) -> None:
        self._dir = tempfile.TemporaryDirectory()
        self.root = Path(self._dir.name)
        self.add("event3", "Corne", (0x4653, 0x0001), LETTERS)
        self.add("event5", "SINO WEALTH Gaming KB", (0x258A, 0x002A), LETTERS + (KEY_SPACE,))
        self.add("event7", "Logitech Mouse", (0x046D, 0xC077), (272, 273))
        self.add("event9", "Power Button", (0, 1), (116,))

    def add(self, name: str, label: str, usb_id: tuple[int, int], codes) -> None:
        device = self.root / name / "device"
        (device / "id").mkdir(parents=True)
        (device / "capabilities").mkdir()
        (device / "name").write_text(label + "\n")
        (device / "id" / "vendor").write_text(f"{usb_id[0]:04x}\n")
        (device / "id" / "product").write_text(f"{usb_id[1]:04x}\n")
        (device / "capabilities" / "key").write_text(capability_text(tuple(codes)) + "\n")

    def cleanup(self) -> None:
        self._dir.cleanup()


class KeyboardChoiceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.input = FakeInput()
        self.addCleanup(self.input.cleanup)

    def test_only_letter_keyboards_are_offered(self) -> None:
        found = keyboards(self.input.root, exclude=None)
        self.assertEqual([board.node.name for board in found], ["event3", "event5"])

    def test_corne_excluded(self) -> None:
        found = keyboards(self.input.root)
        self.assertEqual([board.node.name for board in found], ["event5"])
        self.assertEqual(found[0].node, Path("/dev/input/event5"))
        self.assertEqual(found[0].name, "SINO WEALTH Gaming KB")
        # Named outright, the Corne is still refused, before anything is opened.
        with self.assertRaisesRegex(HelperError, "Corne"):
            choose("/dev/input/event3", self.input.root)
        # A board built with other IDs is the Corne the environment names.
        with self.assertRaisesRegex(HelperError, "Corne"):
            choose("event5", self.input.root, exclude=(0x258A, 0x002A))

    def test_one_keyboard_is_chosen_without_asking(self) -> None:
        nodes = [board.node for board in choose(None, self.input.root)]
        self.assertEqual(nodes, [Path("/dev/input/event5")])

    def test_every_node_of_one_keyboard_is_read(self) -> None:
        # Many keyboards report letters on two interfaces; both are the board.
        self.input.add("event6", "SINO WEALTH Gaming KB", (0x258A, 0x002A), LETTERS)
        nodes = [board.node.name for board in choose(None, self.input.root)]
        self.assertEqual(nodes, ["event5", "event6"])
        # Naming either node picks the keyboard, and with it both nodes, even
        # beside a second keyboard that makes the name necessary.
        self.input.add("event11", "Compx 2.4G Receiver", (0x25A7, 0xFA61), LETTERS)
        for named in ("event5", "/dev/input/event6"):
            nodes = [board.node.name for board in choose(named, self.input.root)]
            self.assertEqual(nodes, ["event5", "event6"], named)

    def test_two_keyboards_need_a_choice(self) -> None:
        self.input.add("event11", "Laptop keyboard", (0x0001, 0x0001), LETTERS)
        with self.assertRaisesRegex(HelperError, "event5.*event11"):
            choose(None, self.input.root)
        self.assertEqual(
            [board.node.name for board in choose("event11", self.input.root)], ["event11"]
        )

    def test_a_mouse_is_not_a_keyboard(self) -> None:
        with self.assertRaisesRegex(HelperError, "not a keyboard"):
            choose("/dev/input/event7", self.input.root)


class RowTests(unittest.TestCase):
    def test_rows_follow_the_spec(self) -> None:
        self.assertEqual(row_of(KEY_1), Row.TOP)  # the number row folds into TOP
        self.assertEqual(row_of(KEY_Q), Row.TOP)
        self.assertEqual(row_of(14), Row.TOP)  # Backspace
        self.assertEqual(row_of(15), Row.TOP)  # Tab
        self.assertEqual(row_of(KEY_A), Row.HOME)
        self.assertEqual(row_of(KEY_Z), Row.BOTTOM)
        self.assertEqual(row_of(KEY_SPACE), Row.THUMB)

    def test_uncounted_keys_have_no_row(self) -> None:
        for code in (KEY_ESC, KEY_UP, KEY_F1, 29, 42, 56, 71, 125):
            self.assertIsNone(row_of(code), code)


class CollectorTests(unittest.TestCase):
    def setUp(self) -> None:
        self.sent: list[TypingSummary] = []
        self.collector = Collector(self.sent.append)

    def type_a_minute(self, start: int) -> bytes:
        """Sixty keydowns 180 ms apart, each with its key-up, plus noise."""
        stream = bytearray()
        codes = (KEY_A, 31, 32, 33, KEY_Q, KEY_SPACE)
        for index in range(60):
            ms = start + 1_000 + index * 180
            code = codes[index % len(codes)]
            stream += packed(ms, 4, 0x70004, kind=4)  # EV_MSC scan code
            stream += packed(ms, code, 1)
            stream += packed(ms, 0, 0, kind=0)  # SYN_REPORT
            stream += packed(ms + 60, code, 0)  # key-up
        stream += packed(start + 20_000, KEY_A, 2)  # autorepeat
        stream += packed(start + 20_100, KEY_UP, 1)  # an arrow
        return bytes(stream)

    def test_only_summary_crosses(self) -> None:
        start = 1_700_000_040_000  # a wall-clock minute edge
        self.assertEqual(start % MINUTE, 0)
        self.collector.feed(self.type_a_minute(start))
        # Inside the helper, a window is rows and times only. No keycode.
        self.assertEqual(len(self.collector.window), 60)
        self.assertTrue(
            all(type(row) is Row and type(ms) is int for row, ms in self.collector.window)
        )
        self.assertEqual(self.sent, [])
        self.collector.tick(start + MINUTE)
        self.assertEqual(self.collector.window, [])
        self.assertEqual(len(self.sent), 1)
        summary = self.sent[0]
        self.assertIs(type(summary), TypingSummary)
        self.assertEqual(
            summary, TypingSummary(Tempo.FLOWING, Spread.STEADY, Row.HOME, RowSpread.FOCUSED)
        )
        self.assertEqual(encode(summary), (1, 0, 1, 0))

    def test_a_sparse_minute_sends_nothing(self) -> None:
        start = 1_700_000_040_000
        self.collector.feed(b"".join(packed(start + i * 150, KEY_A, 1) for i in range(39)))
        self.collector.tick(start + MINUTE)
        self.assertEqual(self.sent, [])
        self.assertEqual(self.collector.window, [])

    def test_windows_follow_the_wall_clock_minute(self) -> None:
        start = 1_700_000_040_000
        late = start + MINUTE - 3_000  # typing that runs over a minute edge
        self.collector.feed(b"".join(packed(late + i * 100, KEY_A, 1) for i in range(60)))
        # The first 30 keys closed with their minute, too few to send.
        self.assertEqual(self.sent, [])
        self.assertEqual(len(self.collector.window), 30)

    def test_a_key_from_a_closed_minute_is_dropped(self) -> None:
        start = 1_700_000_040_000
        self.collector.tick(start + MINUTE)
        self.collector.feed(packed(start + 500, KEY_A, 1))
        self.assertEqual(self.collector.window, [])

    def test_a_torn_read_keeps_the_partial_event(self) -> None:
        start = 1_700_000_040_000
        data = packed(start, KEY_A, 1) + packed(start + 100, KEY_Z, 1)
        other = packed(start + 50, KEY_SPACE, 1)
        self.collector.feed(data[:30], 3)
        self.collector.feed(other[:5], 4)  # a second node of the same keyboard
        self.collector.feed(data[30:], 3)
        self.collector.feed(other[5:], 4)
        self.assertEqual(
            [row for row, _ in self.collector.window], [Row.HOME, Row.BOTTOM, Row.THUMB]
        )


class EncodeTests(unittest.TestCase):
    def test_emitter_rejects_non_enum(self) -> None:
        good = TypingSummary(Tempo.RAPID, Spread.VARIED, Row.THUMB, RowSpread.EVEN)
        self.assertEqual(encode(good), (2, 1, 3, 2))
        for bad in (
            TypingSummary(4, 0, 0, 0),
            TypingSummary(0, 3, 0, 0),
            TypingSummary(0, 0, 4, 0),
            TypingSummary(0, 0, 0, -1),
            TypingSummary("a", 0, 0, 0),
            TypingSummary(0, 0, 0, 1.5),
        ):
            with self.assertRaises((TypeError, ValueError), msg=repr(bad)):
                encode(bad)
        # Only a summary is a summary: a bare tuple of the right numbers is not.
        with self.assertRaises(TypeError):
            encode((0, 0, 0, 0))

    def test_signature_is_four_bytes(self) -> None:
        self.assertEqual(TYPING_SIGNATURE, "(yyyy)")


class SourceTests(unittest.TestCase):
    def test_never_grabs_or_writes(self) -> None:
        source = Path(typing_helper.__file__).read_text()
        self.assertNotIn("EVIOCGRAB", source)
        self.assertNotIn("O_RDWR", source)
        self.assertNotIn("O_WRONLY", source)
        self.assertNotIn("open(", source.replace("os.open(", ""))

    def test_off_by_default(self) -> None:
        root = Path(__file__).resolve().parents[2]
        unit = (root / "host" / "systemd" / "corne-arcane-typing.service.in").read_text()
        self.assertIn("ExecStart=@PREFIX@/bin/corne-arcane-typing", unit)
        makefile = (root / "host" / "Makefile").read_text()
        # The udev rule is shipped to share/, where nothing reads it, not rules.d.
        self.assertIn("udev/61-corne-arcane-typing.rules", makefile)
        self.assertNotIn("$(UDEVDIR)/61-corne-arcane-typing.rules", makefile)

    # The package builds from host/ and the city's sources, without corne.nix.
    @unittest.skipUnless(
        (Path(__file__).resolve().parents[2] / "corne.nix").is_file(), "needs the full checkout"
    )
    def test_the_nixos_option_is_off_by_default(self) -> None:
        nix = (Path(__file__).resolve().parents[2] / "corne.nix").read_text()
        option = nix.split("typingHelper = lib.mkOption {", 1)[1].split("};", 1)[0]
        self.assertIn("default = false;", option)

    def test_udev_rule_skips_the_corne_and_runs_before_uaccess(self) -> None:
        root = Path(__file__).resolve().parents[2]
        rule = (root / "host" / "udev" / "61-corne-arcane-typing.rules").read_text()
        lines = [line for line in rule.splitlines() if line and not line.startswith("#")]
        skip = next(index for index, line in enumerate(lines) if "4653" in line)
        tag = next(index for index, line in enumerate(lines) if 'TAG+="uaccess"' in line)
        self.assertLess(skip, tag)
        self.assertIn("GOTO=", lines[skip])
        self.assertNotIn("MODE=", rule)
        self.assertNotIn("GROUP=", rule)


@unittest.skipUnless(
    Gio is not None and shutil.which("dbus-daemon"), "needs PyGObject and dbus-daemon"
)
class BusTests(unittest.TestCase):
    def setUp(self) -> None:
        bus = subprocess.Popen(
            ["dbus-daemon", "--session", "--nofork", "--print-address=1"],
            stdout=subprocess.PIPE,
            text=True,
        )
        self.addCleanup(bus.wait)
        self.addCleanup(bus.terminate)
        self.address = bus.stdout.readline().strip()
        bus.stdout.close()

    def connect(self):
        connection = Gio.DBusConnection.new_for_address_sync(
            self.address,
            Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT
            | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,
            None,
            None,
        )
        self.addCleanup(connection.close_sync, None)
        return connection

    def test_the_bus_carries_four_bytes_from_the_helper_name(self) -> None:
        listener = self.connect()
        received = []

        def on_signal(_connection, sender, path, interface, name, parameters):
            received.append((sender, path, interface, name, parameters))

        listener.signal_subscribe(
            None, TYPING_INTERFACE, None, None, None, Gio.DBusSignalFlags.NONE, on_signal
        )
        publisher = typing_helper.Publisher(self.connect())
        publisher.own()
        owner = listener.call_sync(
            "org.freedesktop.DBus",
            "/org/freedesktop/DBus",
            "org.freedesktop.DBus",
            "GetNameOwner",
            GLib.Variant("(s)", (TYPING_BUS_NAME,)),
            GLib.VariantType("(s)"),
            Gio.DBusCallFlags.NONE,
            2000,
            None,
        ).unpack()[0]
        publisher.send(TypingSummary(Tempo.FRANTIC, Spread.IRREGULAR, Row.TOP, RowSpread.FOCUSED))
        context = GLib.MainContext.default()
        for _ in range(200):
            if received:
                break
            context.iteration(False)
            os.sched_yield()
        self.assertEqual(len(received), 1)
        sender, path, interface, name, parameters = received[0]
        self.assertEqual(
            (sender, path, interface, name),
            (owner, TYPING_OBJECT_PATH, TYPING_INTERFACE, TYPING_SUMMARY),
        )
        self.assertEqual(parameters.get_type_string(), "(yyyy)")
        self.assertEqual(parameters.unpack(), (3, 2, 0, 0))

    def test_a_refused_summary_sends_nothing(self) -> None:
        publisher = typing_helper.Publisher(self.connect())
        publisher.own()
        with self.assertRaises(ValueError):
            publisher.send(TypingSummary(9, 0, 0, 0))


if __name__ == "__main__":
    unittest.main()
