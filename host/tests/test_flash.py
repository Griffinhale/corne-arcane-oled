"""Flashing the Corne: a fake RPI-RP2 drive, a fake clock, a fake guard and a
fake systemctl. No test reads a real mount table, opens a keyboard or reaches
the desktop's buses."""

from __future__ import annotations

import contextlib
import io
import os
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import fake_systemctl
from arcane_host import flash
from arcane_host.app_controls import Controls, ControlsPanel
from arcane_host.flash import (
    KEYMAP_WARNING,
    BootloaderMissing,
    WrongImage,
    check_image,
    find_bootloader,
    flash_image,
)

HOST_DIR = Path(__file__).resolve().parents[1]
INFO = "UF2 Bootloader v3.0\nModel: Raspberry Pi RP2\nBoard-ID: RPI-RP2\n"


def uf2(payload: bytes, family: int = flash.RP2040_FAMILY) -> bytes:
    """A real UF2 image of payload, 256 bytes per block as the RP2040 expects."""
    chunks = [payload[i : i + 256] for i in range(0, len(payload), 256)] or [b""]
    blocks = []
    for number, chunk in enumerate(chunks):
        header = struct.pack(
            "<8I",
            flash.UF2_MAGIC_START0,
            flash.UF2_MAGIC_START1,
            flash.UF2_FLAG_FAMILY,
            0x10000000 + 256 * number,
            256,
            number,
            len(chunks),
            family,
        )
        data = chunk.ljust(476, b"\0")
        blocks.append(header + data + struct.pack("<I", flash.UF2_MAGIC_END))
    return b"".join(blocks)


class Clock:
    def __init__(self) -> None:
        self.now = 0.0

    def __call__(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.now += seconds


class FakeBoard:
    """Two halves that appear as RPI-RP2 one after the other.

    A half is "plugged with BOOT held" after plug_delay seconds of polling;
    a UF2 written to the drive makes it reboot, so the drive goes away, and the
    next half comes along after the same delay. Every image written is kept.
    """

    def __init__(self, root: Path, clock: Clock, halves: int = 2, plug_delay: float = 3.0):
        self.mount = root / "RPI-RP2"
        self.clock = clock
        self.halves_left = halves
        self.plug_delay = plug_delay
        self.next_plug = clock() + plug_delay
        self.written: list[tuple[str, bytes]] = []
        self.calls = 0

    def __call__(self) -> Path | None:
        self.calls += 1
        if self.mount.exists():
            images = sorted(self.mount.glob("*.uf2"))
            if images:
                # The copy landed: record it and reboot out of the bootloader.
                self.written.extend((p.name, p.read_bytes()) for p in images)
                for path in self.mount.iterdir():
                    path.unlink()
                self.mount.rmdir()
                self.next_plug = self.clock() + self.plug_delay
                return None
            return self.mount
        if self.halves_left and self.clock() >= self.next_plug:
            self.halves_left -= 1
            self.mount.mkdir()
            (self.mount / "INFO_UF2.TXT").write_text(INFO)
            return self.mount
        return None


class Fixture(unittest.TestCase):
    def setUp(self) -> None:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.state = self.root / "state"
        self.clock = Clock()
        self.progress: list[str] = []

    def image(self, name: str = "corne_arcane.uf2", payload: bytes = b"A" * 600) -> Path:
        path = self.root / "images" / name
        path.parent.mkdir(exist_ok=True)
        path.write_bytes(uf2(payload))
        return path

    def flash(self, image: Path, board, **kwargs):
        return flash_image(
            image,
            find=board,
            state=self.state,
            clock=self.clock,
            sleep=self.clock.sleep,
            report=self.progress.append,
            **kwargs,
        )


class CheckImageTests(Fixture):
    def test_a_release_image_passes(self) -> None:
        for name in ("corne_arcane.uf2", "griffin_arcane-release.uf2"):
            self.assertEqual(check_image(self.image(name)), uf2(b"A" * 600))

    def test_wrong_files_are_named(self) -> None:
        cases = {
            self.image("crkbd_rev1_vial_rp2040_ce.uf2"): "not a Corne Arcane image",
            self.root / "gone" / "corne_arcane.uf2": "cannot read",
        }
        text = self.root / "images" / "corne_arcane.uf2"
        for path, words in cases.items():
            with self.subTest(path=path.name), self.assertRaises(WrongImage) as caught:
                check_image(path)
            self.assertIn(words, str(caught.exception))
        text.write_bytes(b"not a uf2 at all")
        with self.assertRaises(WrongImage) as caught:
            check_image(text)
        self.assertIn("not a UF2 image", str(caught.exception))
        text.write_bytes(uf2(b"B" * 600, family=0x00FF00FF))
        with self.assertRaises(WrongImage) as caught:
            check_image(text)
        self.assertIn("not for the RP2040", str(caught.exception))
        broken = bytearray(uf2(b"C" * 600))
        broken[512 + 20] = 7  # the second block claims to be block 7
        text.write_bytes(bytes(broken))
        with self.assertRaises(WrongImage) as caught:
            check_image(text)
        self.assertIn("damaged", str(caught.exception))


class FindBootloaderTests(Fixture):
    def test_only_a_readable_rpi_rp2_drive_counts(self) -> None:
        drive = self.root / "media" / "RPI RP2"
        drive.mkdir(parents=True)
        other = self.root / "media" / "USB STICK"
        other.mkdir()
        (other / "INFO_UF2.TXT").write_text("Board-ID: SOMETHING-ELSE\n")
        mounts = self.root / "mounts"
        escaped = str(drive).replace(" ", "\\040")
        mounts.write_text(
            "proc /proc proc rw 0 0\n"
            f"/dev/sdb1 {str(other).replace(' ', chr(92) + '040')} vfat rw 0 0\n"
            f"/dev/sdc1 {escaped} vfat rw 0 0\n"
        )
        self.assertIsNone(find_bootloader(mounts), "no INFO_UF2.TXT yet")
        (drive / "INFO_UF2.TXT").write_text(INFO)
        self.assertEqual(find_bootloader(mounts), drive)
        self.assertIsNone(find_bootloader(self.root / "no-such-table"))


class FlashTests(Fixture):
    def test_copies_named_uf2(self) -> None:
        image = self.image()
        board = FakeBoard(self.root, self.clock)
        result = self.flash(image, board)
        self.assertEqual(board.written, [("corne_arcane.uf2", image.read_bytes())] * 2)
        self.assertEqual(result.halves, 2)
        self.assertEqual(len(result.sha256), 64)
        self.assertTrue(any("first half" in line for line in self.progress))
        self.assertTrue(any("second half" in line for line in self.progress))
        self.assertIn("TRRS", self.progress[0])

    def test_keeps_last_good(self) -> None:
        first = self.image("griffin_arcane-release.uf2", b"1" * 700)
        self.flash(first, FakeBoard(self.root, self.clock))
        self.assertEqual((self.state / "current.uf2").read_bytes(), first.read_bytes())
        self.assertFalse((self.state / "last-good.uf2").exists())

        second = self.image("corne_arcane.uf2", b"2" * 700)
        result = self.flash(second, FakeBoard(self.root, self.clock))
        self.assertEqual((self.state / "current.uf2").read_bytes(), second.read_bytes())
        self.assertEqual((self.state / "last-good.uf2").read_bytes(), first.read_bytes())
        self.assertEqual(result.last_good, self.state / "last-good.uf2")

        # The same image again keeps the way back as it was.
        self.flash(second, FakeBoard(self.root, self.clock))
        self.assertEqual((self.state / "last-good.uf2").read_bytes(), first.read_bytes())

        # Flashing the way back swaps the two.
        board = FakeBoard(self.root, self.clock)
        self.flash(self.state / "last-good.uf2", board)
        self.assertEqual(board.written[0], ("corne_arcane.uf2", first.read_bytes()))
        self.assertEqual((self.state / "current.uf2").read_bytes(), first.read_bytes())
        self.assertEqual((self.state / "last-good.uf2").read_bytes(), second.read_bytes())

    def test_no_bootloader_message(self) -> None:
        board = FakeBoard(self.root, self.clock, halves=0)
        with self.assertRaises(BootloaderMissing) as caught:
            self.flash(self.image(), board, timeout=30)
        message = str(caught.exception)
        self.assertIn("No RPI-RP2 drive", message)
        self.assertIn("BOOT", message)
        self.assertFalse((self.state / "current.uf2").exists(), "nothing was flashed")
        self.assertGreater(board.calls, 1)

    def test_second_half_missing_says_so_and_still_records_the_first(self) -> None:
        board = FakeBoard(self.root, self.clock, halves=1)
        with self.assertRaises(BootloaderMissing) as caught:
            self.flash(self.image(), board, timeout=30)
        self.assertIn("second half", str(caught.exception))
        self.assertTrue((self.state / "current.uf2").exists())

    def test_wrong_file_is_refused_before_waiting(self) -> None:
        board = FakeBoard(self.root, self.clock)
        with self.assertRaises(WrongImage):
            self.flash(self.image("keyboard.uf2"), board)
        self.assertEqual(board.calls, 0)

    def test_a_drive_that_never_reboots_is_an_error(self) -> None:
        drive = self.root / "RPI-RP2"
        drive.mkdir()
        (drive / "INFO_UF2.TXT").write_text(INFO)
        with self.assertRaises(flash.FlashFailed) as caught:
            self.flash(self.image(), lambda: drive, halves=1)
        self.assertIn("did not restart", str(caught.exception))


class FakeGuard:
    log: list[str] = []

    def __init__(self, **kwargs) -> None:
        FakeGuard.log.append(f"guard {kwargs.get('holder')}")

    def __enter__(self):
        FakeGuard.log.append("daemon stopped")
        return self

    def __exit__(self, *_exc) -> None:
        FakeGuard.log.append("daemon restarted")


class MainTests(Fixture):
    def test_the_daemon_is_stopped_around_the_flash(self) -> None:
        FakeGuard.log = []
        board = FakeBoard(self.root, self.clock)

        def find():
            FakeGuard.log.append("looking for the bootloader")
            return board()

        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = flash.main(
                [str(self.image()), "--state", str(self.state)],
                find=find,
                clock=self.clock,
                sleep=self.clock.sleep,
                guard=FakeGuard,
            )
        self.assertEqual(code, 0)
        self.assertIn("Both halves flashed", out.getvalue())
        self.assertEqual(
            FakeGuard.log[:3],
            ["guard corne-arcane-flash", "daemon stopped", "looking for the bootloader"],
        )
        self.assertEqual(FakeGuard.log[-1], "daemon restarted")
        self.assertEqual(len(board.written), 2)

    def test_a_wrong_file_never_stops_the_daemon(self) -> None:
        FakeGuard.log = []
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            code = flash.main([str(self.image("other.uf2"))], guard=FakeGuard)
        self.assertEqual(code, 1)
        self.assertIn("corne-arcane-flash: other.uf2 is not a Corne Arcane image", err.getvalue())
        self.assertEqual(FakeGuard.log, [])

    def test_the_real_guard_restarts_the_service_after_a_missing_bootloader(self) -> None:
        bin_dir = self.root / "bin"
        bin_dir.mkdir()
        fake_systemctl.write_wrapper(bin_dir)
        systemd = self.root / "systemd"
        systemd.mkdir()
        (systemd / "state").write_text("active\n")
        mounts = self.root / "mounts"
        mounts.write_text("")
        env = {
            **os.environ,
            "PATH": f"{bin_dir}{os.pathsep}{os.environ['PATH']}",
            "FAKE_SYSTEMCTL_DIR": str(systemd),
            "DBUS_SESSION_BUS_ADDRESS": "unix:path=/nonexistent/corne-arcane-tests",
            "XDG_RUNTIME_DIR": str(self.root / "run"),
            "XDG_STATE_HOME": str(self.root / "xdg-state"),
            "CORNE_ARCANE_MOUNTS": str(mounts),
            # No keyboard matches, so the guard waits on no node.
            "CORNE_ARCANE_USB_ID": "ffff:fffe",
            "PYTHONPATH": str(HOST_DIR),
        }
        result = subprocess.run(
            [sys.executable, "-m", "arcane_host.flash", str(self.image()), "--timeout", "0"],
            env=env,
            cwd=self.root,
            capture_output=True,
            text=True,
            timeout=30,
        )
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("corne-arcane-flash: No RPI-RP2 drive", result.stderr)
        self.assertEqual(
            (systemd / "log").read_text().splitlines(),
            [
                "systemctl is-active -> active",
                "systemctl stop -> inactive",
                "systemctl start -> active",
            ],
        )


class StubView:
    CALL_TIMEOUT_MS = 1000

    def __init__(self, status=("connected", "/dev/pts/9", False, "")) -> None:
        self.status = status


def script(directory: Path, name: str, body: str) -> list[str]:
    path = directory / name
    path.write_text(f"#!/bin/sh\n{body}\n")
    path.chmod(0o755)
    return [str(path)]


def run_flash(controls: Controls, limit: float = 10.0) -> None:
    import time

    deadline = time.monotonic() + limit
    while controls.flash is not None:
        controls.poll()
        if time.monotonic() > deadline:
            raise AssertionError("the flash child did not finish")
        time.sleep(0.02)


class ControlsFlashTests(Fixture):
    def controls(self, **kwargs) -> Controls:
        kwargs.setdefault("lock", self.root / "hid.lock")
        controls = Controls(StubView(), **kwargs)
        self.addCleanup(controls.close)
        return controls

    def test_the_keymap_warning_comes_before_any_flash(self) -> None:
        argv_log = self.root / "argv"
        controls = self.controls(
            flash_command=script(self.root, "flash", f"echo \"$@\" > '{argv_log}'")
        )
        image = self.image()
        controls.prepare_flash(image)
        self.assertIn(KEYMAP_WARNING, controls.message)
        self.assertEqual(controls.flash_ready, image)
        self.assertIsNone(controls.flash, "nothing runs until the warning is confirmed")
        self.assertFalse(argv_log.exists())

        controls.cancel_flash()
        self.assertIsNone(controls.flash_ready)
        self.assertEqual(controls.message, "")

        controls.prepare_flash(image)
        controls.confirm_flash()
        self.assertFalse(controls.can_use_keyboard)
        run_flash(controls)
        self.assertEqual(argv_log.read_text().split(), [str(image)])

    def test_a_wrong_file_is_shown_without_a_warning(self) -> None:
        controls = self.controls(flash_command=script(self.root, "flash", "exit 0"))
        controls.prepare_flash(self.image("vial.uf2"))
        self.assertTrue(controls.message.startswith("Not flashed: "))
        self.assertIn("not a Corne Arcane image", controls.message)
        self.assertIsNone(controls.flash_ready)
        controls.confirm_flash()
        self.assertIsNone(controls.flash)

    def test_progress_success_and_failure_are_shown(self) -> None:
        release = self.root / "go"
        controls = self.controls(
            flash_command=script(
                self.root,
                "flash",
                "echo 'Hold BOOT on the first half and plug in its USB cable'\n"
                f"while [ ! -e '{release}' ]; do sleep 0.02; done\n"
                "echo 'Both halves flashed'",
            )
        )
        controls.prepare_flash(self.image())
        controls.confirm_flash()
        import time

        for _ in range(250):
            controls.poll()
            if "first half" in controls.message:
                break
            time.sleep(0.02)
        self.assertEqual(controls.message, "Hold BOOT on the first half and plug in its USB cable")
        release.touch()
        run_flash(controls)
        self.assertEqual(
            controls.message, "Both halves flashed. Load your saved layout back in Vial."
        )

        controls.flash_command = script(
            self.root,
            "broken",
            "echo 'corne-arcane-flash: No RPI-RP2 drive appeared for the first half' >&2; exit 1",
        )
        controls.prepare_flash(self.image())
        controls.confirm_flash()
        run_flash(controls)
        self.assertEqual(controls.message, "No RPI-RP2 drive appeared for the first half")

    def test_another_owner_blocks_the_flash(self) -> None:
        controls = Controls(
            StubView(("paused", "", True, "Vial (corne-arcane-vial) (pid 4)")),
            lock=self.root / "hid.lock",
            flash_command=script(self.root, "flash", "exit 0"),
        )
        self.addCleanup(controls.close)
        controls.prepare_flash(self.image())
        self.assertIsNone(controls.flash_ready)
        controls.confirm_flash()
        self.assertIsNone(controls.flash)


class FakeWidget:
    def __init__(self, *args, **kwargs) -> None:
        self.options = {"text": "", "state": "normal", **kwargs}
        self.layout = None

    def pack(self, **kwargs) -> None:
        self.layout = ("pack", kwargs)

    def place(self, **kwargs) -> None:
        pass

    def pack_forget(self) -> None:
        self.layout = None

    def cget(self, name):
        return self.options.get(name)

    def configure(self, **kwargs) -> None:
        self.options.update(kwargs)


class FakeVar:
    def __init__(self, master=None, value="") -> None:
        self.value = value

    def get(self) -> str:
        return self.value

    def set(self, value: str) -> None:
        self.value = value


class FakeTk:
    Frame = Button = Spinbox = Label = FakeWidget
    StringVar = FakeVar


class PanelFlashTests(Fixture):
    def test_flash_button_asks_for_a_file_then_confirms(self) -> None:
        controls = Controls(
            StubView(),
            lock=self.root / "hid.lock",
            flash_command=script(self.root, "flash", "exit 0"),
        )
        self.addCleanup(controls.close)
        image = self.image()
        asked: list[bool] = []
        panel = ControlsPanel(
            FakeTk,
            None,
            controls,
            background="#000",
            ink="#fff",
            ask_image=lambda: (asked.append(True), str(image))[1],
        )
        panel.refresh()
        self.assertEqual(panel.flash_button.cget("text"), "Flash")
        self.assertIsNone(panel.cancel_button.layout, "Cancel is hidden until it means something")
        panel._flash()
        self.assertEqual(asked, [True])
        panel.refresh()
        self.assertEqual(panel.flash_button.cget("text"), "Flash now")
        self.assertIsNotNone(panel.cancel_button.layout)
        self.assertIn(KEYMAP_WARNING, panel.message.cget("text"))
        panel._flash()
        self.assertIsNotNone(controls.flash)
        self.assertEqual(asked, [True], "confirming does not ask again")
        panel.refresh()
        self.assertIsNone(panel.cancel_button.layout)
        self.assertEqual(panel.flash_button.cget("state"), "disabled")
        run_flash(controls)

        # A dismissed file dialog changes nothing.
        panel = ControlsPanel(
            FakeTk, None, controls, background="#000", ink="#fff", ask_image=lambda: ""
        )
        panel._flash()
        self.assertIsNone(controls.flash_ready)


if __name__ == "__main__":
    unittest.main()
