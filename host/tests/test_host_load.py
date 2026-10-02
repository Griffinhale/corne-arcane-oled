from __future__ import annotations

import os
import unittest

from arcane_host.adapters import LOAD_INTERVAL, SemanticAdapters
from arcane_host.host_load import (
    LoadReader,
    LoadSample,
    StrainGauge,
    intensity_for,
    memory_available_fraction,
    pressure_avg,
)
from arcane_host.policy import NotificationPolicy
from arcane_host.protocol import Intensity, Mode
from arcane_host.semantic import SemanticResolver

PSI = """some avg10=42.50 avg60=12.00 avg300=3.00 total=1
full avg10=0.00 avg60=0.00 avg300=0.00 total=0
"""
MEMINFO = """MemTotal:       1000000 kB
MemFree:          10000 kB
MemAvailable:     40000 kB
"""


def statvfs(blocks: int, available: int) -> os.statvfs_result:
    return os.statvfs_result((4096, 4096, blocks, available, available, 0, 0, 0, 0, 255))


class HostLoadTests(unittest.TestCase):
    def test_parsers_read_only_aggregate_figures(self) -> None:
        self.assertEqual(pressure_avg(PSI, "some", "avg10"), 42.5)
        self.assertEqual(pressure_avg(PSI, "some", "avg60"), 12.0)
        self.assertIsNone(pressure_avg("garbage", "some", "avg10"))
        self.assertAlmostEqual(memory_available_fraction(MEMINFO), 0.04)
        self.assertIsNone(memory_available_fraction("MemTotal: 0 kB\n"))
        self.assertEqual(
            [intensity_for(value) for value in (0, 9.9, 10, 30, 59, 60, 100)],
            [
                Intensity.CALM,
                Intensity.CALM,
                Intensity.ACTIVE,
                Intensity.BUSY,
                Intensity.BUSY,
                Intensity.SATURATED,
                Intensity.SATURATED,
            ],
        )

    def test_reader_prefers_pressure_and_falls_back_to_loadavg(self) -> None:
        files = {"/proc/pressure/cpu": PSI, "/proc/meminfo": MEMINFO}

        def read(path: str) -> str:
            if path not in files:
                raise FileNotFoundError(path)
            return files[path]

        disks = {"/": statvfs(1000, 500), "/home/x": statvfs(1000, 30)}
        reader = LoadReader(read, lambda mount: disks[mount], lambda: 4, ("/", "/home/x"))
        sample = reader()
        self.assertEqual(sample.cpu_pressure, 42.5)
        self.assertAlmostEqual(sample.memory_available, 0.04)
        self.assertAlmostEqual(sample.disk_free, 0.03)

        del files["/proc/pressure/cpu"]
        files["/proc/loadavg"] = "4.00 8.00 1.00 1/100 42\n"
        sample = reader()
        # 1.0 per CPU reads as 50 %; 2.0 per CPU caps at 100.
        self.assertEqual(sample.cpu_pressure, 50.0)
        self.assertEqual(sample.cpu_pressure_60, 100.0)

        # Nothing readable: an empty sample, not an exception.
        def missing(_path):
            raise OSError

        empty = LoadReader(lambda _p: missing(_p), lambda _m: missing(_m), lambda: None, ("/",))()
        self.assertEqual(empty, LoadSample())

    def test_strain_needs_near_full_and_has_hysteresis(self) -> None:
        gauge = StrainGauge()
        roomy = LoadSample(cpu_pressure=5, cpu_pressure_60=5, memory_available=0.5, disk_free=0.5)
        self.assertEqual(gauge(roomy), (Intensity.CALM, False))
        # Busy is not strain: a saturated CPU for ten seconds is only intensity.
        busy = LoadSample(cpu_pressure=95, cpu_pressure_60=40, memory_available=0.5, disk_free=0.5)
        self.assertEqual(gauge(busy), (Intensity.SATURATED, False))
        low_memory = LoadSample(memory_available=0.04, disk_free=0.5)
        self.assertEqual(gauge(low_memory), (Intensity.CALM, True))
        # 6 % is above the on line but below the off line: still strain.
        self.assertTrue(gauge(LoadSample(memory_available=0.06, disk_free=0.5))[1])
        self.assertFalse(gauge(LoadSample(memory_available=0.09, disk_free=0.5))[1])
        self.assertFalse(gauge(LoadSample(memory_available=0.06, disk_free=0.5))[1])
        self.assertTrue(gauge(LoadSample(disk_free=0.01))[1])
        self.assertFalse(gauge(LoadSample(disk_free=0.2))[1])
        self.assertTrue(gauge(LoadSample(cpu_pressure=90, cpu_pressure_60=85))[1])
        self.assertTrue(gauge(LoadSample(cpu_pressure=90, cpu_pressure_60=70))[1])
        self.assertFalse(gauge(LoadSample(cpu_pressure=50, cpu_pressure_60=55))[1])

    def test_adapters_sample_on_a_deadline_and_strain_outranks_quiet(self) -> None:
        now = [10.0]
        samples = [(Intensity.BUSY, False)]
        resolver = SemanticResolver()
        adapters = SemanticAdapters(
            resolver,
            NotificationPolicy(),
            lambda: None,
            lambda: now[0],
            load_sampler=lambda: samples[0],
        )
        self.assertEqual(adapters.next_deadline(now[0]), now[0])
        adapters.poll(now[0])
        self.assertEqual(resolver.state.civic.intensity, Intensity.BUSY)
        self.assertEqual(resolver.state.civic.mode, Mode.NORMAL)
        self.assertEqual(adapters.next_deadline(now[0]), now[0] + LOAD_INTERVAL)
        samples[0] = (Intensity.CALM, True)
        # Not due yet: nothing is read.
        adapters.poll(now[0] + 1)
        self.assertEqual(resolver.state.civic.mode, Mode.NORMAL)
        now[0] += LOAD_INTERVAL
        resolver.update(dnd=True)
        adapters.poll(now[0])
        self.assertEqual(resolver.state.civic.mode, Mode.STRAIN)
        self.assertEqual(resolver.state.civic.intensity, Intensity.CALM)
        samples[0] = (Intensity.CALM, False)
        now[0] += LOAD_INTERVAL
        adapters.poll(now[0])
        self.assertEqual(resolver.state.civic.mode, Mode.QUIET)

        # A sampler that raises counts an error and changes nothing.
        def broken():
            raise OSError

        adapters._load_sampler = broken
        now[0] += LOAD_INTERVAL
        adapters.poll(now[0])
        self.assertEqual(adapters.counters.errors, 1)
        self.assertEqual(resolver.state.civic.mode, Mode.QUIET)

    def test_no_sampler_without_the_opt_in(self) -> None:
        adapters = SemanticAdapters(SemanticResolver(), NotificationPolicy(), lambda: None)
        self.assertIsNone(adapters.next_deadline(0.0))


if __name__ == "__main__":
    unittest.main()
