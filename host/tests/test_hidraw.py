from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

from arcane_host.hidraw import QMK_RAW_USAGE, discover


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


if __name__ == "__main__":
    unittest.main()
