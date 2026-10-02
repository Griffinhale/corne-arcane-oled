"""System load and resource strain, read as aggregate figures only.

Every figure here is a machine-wide number from /proc or statvfs: CPU
pressure, run-queue length, available memory and free disk space. No process,
file or user is named, and the sample that leaves this module is one
intensity level and one flag for the keyboard wire, plus, for a desktop city
(arcane_host.host_signals), a finer load level and which resource is near full.
"""

from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from .host_signals import LoadLevel, StrainKind
from .protocol import Intensity

# CPU pressure ("some avg10", percent of time a task waited for a CPU) at
# which intensity steps up. Without PSI, the 1-minute load per CPU stands in,
# scaled so 1.0 per CPU reads as 50 %.
INTENSITY_STEPS = (10.0, 30.0, 60.0)
# The desktop's finer steps over the same figure. Every wire step is one of
# these cut points, so a finer level always refines the intensity sent.
LOAD_STEPS = (5.0, 10.0, 30.0, 60.0, 80.0)

# Near full. Each resource turns strain on at the first figure and off only
# past the second, so a value sitting on the line does not flicker.
MEMORY_AVAILABLE_ON, MEMORY_AVAILABLE_OFF = 0.05, 0.08
DISK_FREE_ON, DISK_FREE_OFF = 0.05, 0.08
CPU_SOME_AVG60_ON, CPU_SOME_AVG60_OFF = 80.0, 60.0


def pressure_avg(text: str, line: str, field: str) -> float | None:
    """One average from a /proc/pressure file, e.g. ("some", "avg10")."""
    for row in text.splitlines():
        parts = row.split()
        if parts and parts[0] == line:
            for part in parts[1:]:
                key, _, value = part.partition("=")
                if key == field:
                    try:
                        return float(value)
                    except ValueError:
                        return None
    return None


def memory_available_fraction(text: str) -> float | None:
    values: dict[str, int] = {}
    for row in text.splitlines():
        key, _, rest = row.partition(":")
        if key in {"MemTotal", "MemAvailable"}:
            try:
                values[key] = int(rest.split()[0])
            except (IndexError, ValueError):
                return None
    total = values.get("MemTotal")
    available = values.get("MemAvailable")
    if not total or available is None:
        return None
    return available / total


def intensity_for(cpu_pressure: float) -> Intensity:
    level = sum(1 for step in INTENSITY_STEPS if cpu_pressure >= step)
    return Intensity(level)


def load_level_for(cpu_pressure: float | None) -> LoadLevel:
    if cpu_pressure is None:
        return LoadLevel.NONE
    return LoadLevel(1 + sum(1 for step in LOAD_STEPS if cpu_pressure >= step))


@dataclass(slots=True)
class LoadSample:
    cpu_pressure: float | None = None  # percent, avg10
    cpu_pressure_60: float | None = None  # percent, avg60
    memory_available: float | None = None  # fraction
    disk_free: float | None = None  # lowest fraction across the watched mounts


class LoadReader:
    """Read one LoadSample; any missing source leaves its field None."""

    def __init__(
        self,
        read: Callable[[str], str] = lambda path: Path(path).read_text(),
        statvfs: Callable[[str], os.statvfs_result] = os.statvfs,
        cpu_count: Callable[[], int | None] = os.cpu_count,
        mounts: tuple[str, ...] | None = None,
    ) -> None:
        self._read = read
        self._statvfs = statvfs
        self._cpu_count = cpu_count
        self._mounts = mounts if mounts is not None else ("/", str(Path.home()))

    def _text(self, path: str) -> str | None:
        try:
            return self._read(path)
        except (OSError, UnicodeDecodeError):
            return None

    def __call__(self) -> LoadSample:
        sample = LoadSample()
        pressure = self._text("/proc/pressure/cpu")
        if pressure is not None:
            sample.cpu_pressure = pressure_avg(pressure, "some", "avg10")
            sample.cpu_pressure_60 = pressure_avg(pressure, "some", "avg60")
        if sample.cpu_pressure is None:
            loadavg = self._text("/proc/loadavg")
            cpus = self._cpu_count() or 1
            try:
                one, five = (float(value) for value in (loadavg or "").split()[:2])
            except ValueError:
                pass
            else:
                sample.cpu_pressure = min(100.0, one / cpus * 50.0)
                sample.cpu_pressure_60 = min(100.0, five / cpus * 50.0)
        meminfo = self._text("/proc/meminfo")
        if meminfo is not None:
            sample.memory_available = memory_available_fraction(meminfo)
        for mount in self._mounts:
            try:
                stat = self._statvfs(mount)
            except OSError:
                continue
            if stat.f_blocks:
                free = stat.f_bavail / stat.f_blocks
                sample.disk_free = free if sample.disk_free is None else min(sample.disk_free, free)
        return sample


class StrainGauge:
    """Turn samples into (intensity, strain) with hysteresis on strain.

    The last call's finer reading stays in ``level`` and ``kind`` for the
    desktop's host signals; both are NONE until the first sample.
    """

    def __init__(self) -> None:
        self._memory = False
        self._disk = False
        self._cpu = False
        self.level = LoadLevel.NONE
        self.kind = StrainKind.NONE

    @staticmethod
    def _below(value: float | None, held: bool, on: float, off: float) -> bool:
        if value is None:
            return False
        return value < off if held else value < on

    @staticmethod
    def _above(value: float | None, held: bool, on: float, off: float) -> bool:
        if value is None:
            return False
        return value > off if held else value >= on

    def __call__(self, sample: LoadSample) -> tuple[Intensity, bool]:
        self._memory = self._below(
            sample.memory_available, self._memory, MEMORY_AVAILABLE_ON, MEMORY_AVAILABLE_OFF
        )
        self._disk = self._below(sample.disk_free, self._disk, DISK_FREE_ON, DISK_FREE_OFF)
        self._cpu = self._above(
            sample.cpu_pressure_60, self._cpu, CPU_SOME_AVG60_ON, CPU_SOME_AVG60_OFF
        )
        intensity = (
            Intensity.CALM if sample.cpu_pressure is None else intensity_for(sample.cpu_pressure)
        )
        self.level = load_level_for(sample.cpu_pressure)
        self.kind = (
            StrainKind.DISK
            if self._disk
            else StrainKind.MEMORY
            if self._memory
            else StrainKind.CPU
            if self._cpu
            else StrainKind.CLEAR
        )
        return intensity, self._memory or self._disk or self._cpu


class LoadSampler:
    """The callable SemanticAdapters polls: read, then gauge.

    A call returns the wire's (intensity, strain); ``detail`` is the same
    sample's (LoadLevel, StrainKind) for the desktop city.
    """

    def __init__(self, reader: Callable[[], LoadSample]) -> None:
        self._read = reader
        self._gauge = StrainGauge()

    def __call__(self) -> tuple[Intensity, bool]:
        return self._gauge(self._read())

    @property
    def detail(self) -> tuple[LoadLevel, StrainKind]:
        return self._gauge.level, self._gauge.kind


def load_sampler(reader: Callable[[], LoadSample] | None = None) -> LoadSampler:
    return LoadSampler(reader or LoadReader())
