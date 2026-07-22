from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from arcane_host.hidraw import QMK_RAW_USAGE, Device, discover


class DiscoveryTests(unittest.TestCase):
    def test_filters_by_qmk_raw_usage(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            raw = root / "hidraw7" / "device"
            raw.mkdir(parents=True)
            (raw / "report_descriptor").write_bytes(b"prefix" + QMK_RAW_USAGE + b"suffix")
            other = root / "hidraw8" / "device"
            other.mkdir(parents=True)
            (other / "report_descriptor").write_bytes(b"ordinary keyboard")
            self.assertEqual(discover(root), [Path("/dev/hidraw7")])


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
