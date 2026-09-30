from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from arcane_host.hidraw import QMK_RAW_USAGE, USB_ID_ENV, Device, choose_device, discover


def fake_hidraw(root: Path, name: str, descriptor: bytes, hid_id: str) -> None:
    """A sysfs hidraw entry: the files discover reads, and no device node."""
    device = root / name / "device"
    device.mkdir(parents=True)
    (device / "report_descriptor").write_bytes(descriptor)
    (device / "uevent").write_text(f"DRIVER=hid-generic\nHID_ID={hid_id}\nHID_NAME=x\n")


RAW = b"prefix" + QMK_RAW_USAGE + b"suffix"
CORNE = "0003:00004653:00000001"
OTHER_QMK = "0003:0000FEED:00006060"


class DiscoveryTests(unittest.TestCase):
    def test_filters_by_qmk_raw_usage(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fake_hidraw(root, "hidraw7", RAW, CORNE)
            fake_hidraw(root, "hidraw8", b"ordinary keyboard", CORNE)
            self.assertEqual(discover(root), [Path("/dev/hidraw7")])

    def test_prefers_corne_vid_pid(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fake_hidraw(root, "hidraw3", RAW, OTHER_QMK)
            fake_hidraw(root, "hidraw5", RAW, CORNE)
            self.assertEqual(discover(root), [Path("/dev/hidraw5")])
            self.assertEqual(
                discover(root, usb_id=None), [Path("/dev/hidraw3"), Path("/dev/hidraw5")]
            )

    def test_entry_without_hid_id_is_skipped(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            device = root / "hidraw2" / "device"
            device.mkdir(parents=True)
            (device / "report_descriptor").write_bytes(RAW)
            self.assertEqual(discover(root), [])

    def test_choose_device_uses_the_usb_id_override(self) -> None:
        with patch("arcane_host.hidraw.discover", return_value=[Path("/dev/hidraw3")]) as found:
            with patch.dict("os.environ", {USB_ID_ENV: "feed:6060"}):
                self.assertEqual(choose_device(), Path("/dev/hidraw3"))
        found.assert_called_once_with(usb_id=(0xFEED, 0x6060))

    def test_choose_device_rejects_a_malformed_override(self) -> None:
        with patch.dict("os.environ", {USB_ID_ENV: "corne"}):
            with self.assertRaises(ValueError):
                choose_device()

    def test_choose_device_names_the_usb_id_when_nothing_matches(self) -> None:
        with patch("arcane_host.hidraw.discover", return_value=[]):
            with patch.dict("os.environ", {}, clear=True):
                with self.assertRaisesRegex(RuntimeError, "4653:0001"):
                    choose_device()


class DeviceReadTests(unittest.TestCase):
    def device(self) -> Device:
        device = Device.__new__(Device)
        device.path = Path("/dev/hidraw-test")
        device.fd = 17
        return device

    @patch("arcane_host.hidraw.os.read", return_value=b"x" * 32)
    @patch("arcane_host.hidraw.select.select", return_value=([17], [], []))
    def test_receive_raw_report(self, select_mock, read_mock) -> None:
        self.assertEqual(self.device().receive(0.5), b"x" * 32)
        select_mock.assert_called_once_with((17,), (), (), 0.5)
        read_mock.assert_called_once_with(17, 33)

    @patch("arcane_host.hidraw.os.read", return_value=b"\0" + b"y" * 32)
    @patch("arcane_host.hidraw.select.select", return_value=([17], [], []))
    def test_receive_accepts_zero_report_id_prefix(self, _select, _read) -> None:
        self.assertEqual(self.device().receive(0.5), b"y" * 32)

    @patch("arcane_host.hidraw.select.select", return_value=([], [], []))
    def test_receive_timeout(self, _select) -> None:
        with self.assertRaises(TimeoutError):
            self.device().receive(0.01)


if __name__ == "__main__":
    unittest.main()
