from __future__ import annotations

import contextlib
import io
import json
import os
import random
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from arcane_host import keymap_profiles
from arcane_host.keymap_profiles import (
    MATRIX_COLS,
    MATRIX_ROWS,
    ProfileError,
    ViaKeymap,
    load,
    save,
)
from arcane_host.protocol import REPORT_SIZE

UID = bytes((0x3B, 0x6B, 0xA0, 0x29, 0x80, 0x56, 0xED, 0xD1))  # firmware/config.h


class FakeVia:
    """The keymap commands of via.c and vial.c over a byte array; no hidraw."""

    def __init__(self, uid: bytes = UID, layers: int = 4, seed: int = 1) -> None:
        self.uid = uid
        self.layers = layers
        size = layers * MATRIX_ROWS * MATRIX_COLS * 2
        self.memory = bytearray(random.Random(seed).randbytes(size))
        self.replies: list[bytes] = []
        self.writes = 0

    def send(self, report: bytes) -> None:
        assert len(report) == REPORT_SIZE
        data = bytearray(report)
        if data[0] == 0x11:
            data[1] = self.layers
        elif data[0] in (0x12, 0x13):
            offset, size = (data[1] << 8) | data[2], data[3]
            if size <= 28 and offset + size <= len(self.memory):
                if data[0] == 0x12:
                    data[4 : 4 + size] = self.memory[offset : offset + size]
                else:
                    self.memory[offset : offset + size] = data[4 : 4 + size]
                    self.writes += 1
        elif data[0] == 0xFE and data[1] == 0x00:
            data = bytearray(REPORT_SIZE)
            data[0:4] = (6).to_bytes(4, "little")
            data[4:12] = self.uid
        else:
            data[0] = 0xFF  # id_unhandled
        self.replies.append(bytes(data))

    def receive(self, _timeout: float) -> bytes:
        return self.replies.pop(0)

    def close(self) -> None:
        pass


class ProfileTests(unittest.TestCase):
    def setUp(self) -> None:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.data = Path(directory.name)
        patcher = patch.dict(os.environ, {"XDG_DATA_HOME": str(self.data)})
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_roundtrip(self) -> None:
        keyboard = FakeVia(seed=1)
        original = bytes(keyboard.memory)
        path = save(ViaKeymap(keyboard), "work")
        self.assertEqual(path, self.data / "corne-arcane" / "keymaps" / "work.json")
        document = json.loads(path.read_text())
        self.assertEqual(document["layers"], 4)
        self.assertEqual([len(layer) for layer in document["keymap"]], [48] * 4)
        self.assertEqual(document["keymap"][0][0], int.from_bytes(original[:2], "big"))

        keyboard.memory[:] = FakeVia(seed=2).memory  # someone edits in Vial
        self.assertNotEqual(bytes(keyboard.memory), original)
        load(ViaKeymap(keyboard), "work")
        self.assertEqual(bytes(keyboard.memory), original)

    def test_hash_mismatch_refused(self) -> None:
        save(ViaKeymap(FakeVia(seed=1)), "work")
        for other in (FakeVia(uid=bytes(range(1, 9)), seed=3), FakeVia(layers=5, seed=3)):
            with self.subTest(uid=other.uid.hex(), layers=other.layers):
                before = bytes(other.memory)
                with self.assertRaisesRegex(ProfileError, "different layout"):
                    load(ViaKeymap(other), "work")
                self.assertEqual(bytes(other.memory), before)
                self.assertEqual(other.writes, 0)

    def test_existing_profile_needs_force(self) -> None:
        via = ViaKeymap(FakeVia())
        save(via, "work")
        with self.assertRaisesRegex(ProfileError, "--force"):
            save(via, "work")
        save(via, "work", force=True)

    def test_names_cannot_leave_the_directory(self) -> None:
        for name in ("../escape", ".hidden", "a/b", "", "x" * 65):
            with self.subTest(name=name), self.assertRaises(ProfileError):
                keymap_profiles.profile_path(name)

    def test_damaged_profile_is_refused(self) -> None:
        keyboard = FakeVia()
        path = save(ViaKeymap(keyboard), "work")
        document = json.loads(path.read_text())
        document["keymap"][1].pop()
        path.write_text(json.dumps(document))
        with self.assertRaisesRegex(ProfileError, "shape"):
            load(ViaKeymap(keyboard), "work")
        self.assertEqual(keyboard.writes, 0)

    def test_wrong_device_is_named(self) -> None:
        class NotVia(FakeVia):
            def send(self, report: bytes) -> None:
                self.replies.append(bytes(REPORT_SIZE))

        with self.assertRaisesRegex(ProfileError, "layer count"):
            save(ViaKeymap(NotVia()), "work")


class NullOwnership:
    def __init__(self, **_kwargs: object) -> None:
        pass

    def __enter__(self) -> None:
        return None

    def __exit__(self, *_args: object) -> None:
        return None


class CommandTests(unittest.TestCase):
    def setUp(self) -> None:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.keyboard = FakeVia()

        @contextlib.contextmanager
        def opened(_path: str):
            yield self.keyboard

        for patcher in (
            patch.dict(os.environ, {"XDG_DATA_HOME": directory.name}),
            patch.object(keymap_profiles, "ExclusiveHidOwnership", NullOwnership),
            patch.object(keymap_profiles, "choose_device", return_value="/dev/hidraw-test"),
            patch.object(keymap_profiles, "Device", opened),
        ):
            patcher.start()
            self.addCleanup(patcher.stop)

    def run_command(self, *argv: str) -> tuple[int, str, str]:
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            status = keymap_profiles.main(list(argv))
        return status, out.getvalue(), err.getvalue()

    def test_save_list_load_delete(self) -> None:
        original = bytes(self.keyboard.memory)
        self.assertEqual(self.run_command("save", "gaming")[0], 0)
        self.assertEqual(self.run_command("list"), (0, "gaming\n", ""))
        self.keyboard.memory[:] = bytes(len(self.keyboard.memory))
        self.assertEqual(self.run_command("load", "gaming")[:2], (0, "loaded gaming\n"))
        self.assertEqual(bytes(self.keyboard.memory), original)
        self.assertEqual(self.run_command("delete", "gaming")[0], 0)
        self.assertEqual(self.run_command("list"), (0, "", ""))

    def test_missing_profile_fails_before_taking_the_keyboard(self) -> None:
        with patch.object(keymap_profiles, "ExclusiveHidOwnership") as guard:
            status, _out, err = self.run_command("load", "nope")
        self.assertEqual(status, 1)
        self.assertIn("no profile named 'nope'", err)
        guard.assert_not_called()


if __name__ == "__main__":
    unittest.main()
