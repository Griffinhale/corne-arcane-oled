"""The opt-in host signals at desktop detail (city ABI 10, SH8).

Six enums, each with zero meaning "nothing sent"; read from the same adapter
state that feeds the wire's mode and intensity; published on the Control
interface only with --host-signals; and taken by the desktop city only while
every value is inside its enum.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

from arcane_host.adapters import SemanticAdapters, UrgentKind
from arcane_host.app_controls import ServiceView
from arcane_host.city import CityInput
from arcane_host.dbus_contract import CONTROL_XML, HOST_SIGNALS_CHANGED, HOST_SIGNALS_SIGNATURE
from arcane_host.focus import FocusArbiter
from arcane_host.host_load import LoadSample, load_level_for, load_sampler
from arcane_host.host_signals import (
    HOST_SIGNAL_FIELDS,
    NO_HOST_SIGNALS,
    AlertState,
    CallState,
    CommandState,
    HostSignals,
    LoadLevel,
    Presence,
    StrainKind,
)
from arcane_host.policy import NotificationPolicy
from arcane_host.protocol import Intensity, Mode
from arcane_host.runtime import DaemonRuntime
from arcane_host.semantic import SemanticResolver
from test_runtime import FakeGio, FakeGLib, FakeHeartbeat, FakeLoop

HEADER = Path(__file__).resolve().parents[2] / "desktop" / "duel_city.h"
HEADER_PREFIX = {
    "presence": "PRESENCE",
    "load": "LOAD",
    "strain": "STRAIN",
    "call": "CALL",
    "alert": "ALERT",
    "command": "COMMAND",
}


class ShapeTests(unittest.TestCase):
    def test_every_enum_is_the_headers_and_zero_is_none(self) -> None:
        text = HEADER.read_text()
        self.assertEqual([name for name, _ in HOST_SIGNAL_FIELDS], list(HostSignals._fields))
        for field, kind in HOST_SIGNAL_FIELDS:
            prefix = HEADER_PREFIX[field]
            declared = {
                name: int(value)
                for name, value in re.findall(rf"DUEL_CITY_{prefix}_(\w+) = (\d+),", text)
            }
            count = declared.pop("COUNT")
            self.assertEqual(declared, {member.name: int(member) for member in kind}, field)
            self.assertEqual(count, len(kind), field)
            self.assertEqual(kind(0).name, "NONE", field)
            # One byte each, with room to spare: nothing larger can ride here.
            self.assertLess(count, 16, field)

    def test_nothing_sent_is_all_zero(self) -> None:
        self.assertEqual(NO_HOST_SIGNALS.as_bytes(), (0,) * 6)
        self.assertEqual(HOST_SIGNALS_SIGNATURE, "(yyyyyy)")
        # The Control XML declares the signal and the method with six bytes each.
        for name in ("presence", "load", "strain", "call", "alert", "command"):
            self.assertEqual(CONTROL_XML.count(f"name='{name}'"), 2, name)

    def test_values_outside_an_enum_are_rejected(self) -> None:
        for index, (field, kind) in enumerate(HOST_SIGNAL_FIELDS):
            for bad in (len(kind), 0xFF):
                values = [0] * 6
                values[index] = bad
                with self.assertRaises(ValueError, msg=f"{field}={bad}"):
                    HostSignals.from_bytes(tuple(values))
        for length in (0, 5, 7, 8):
            with self.assertRaises(ValueError):
                HostSignals.from_bytes((0,) * length)
        top = tuple(len(kind) - 1 for _, kind in HOST_SIGNAL_FIELDS)
        self.assertEqual(HostSignals.from_bytes(top).as_bytes(), top)


class LoadDetailTests(unittest.TestCase):
    def test_finer_levels_refine_the_wire_intensity(self) -> None:
        cases = {
            None: LoadLevel.NONE,
            0.0: LoadLevel.IDLE,
            4.9: LoadLevel.IDLE,
            5.0: LoadLevel.LIGHT,
            10.0: LoadLevel.STEADY,
            30.0: LoadLevel.BUSY,
            60.0: LoadLevel.HEAVY,
            80.0: LoadLevel.SATURATED,
            100.0: LoadLevel.SATURATED,
        }
        for pressure, level in cases.items():
            self.assertEqual(load_level_for(pressure), level, pressure)
        # Each wire step is a cut point: a finer level never straddles two.
        wire = {
            LoadLevel.IDLE: Intensity.CALM,
            LoadLevel.LIGHT: Intensity.CALM,
            LoadLevel.STEADY: Intensity.ACTIVE,
            LoadLevel.BUSY: Intensity.BUSY,
            LoadLevel.HEAVY: Intensity.SATURATED,
            LoadLevel.SATURATED: Intensity.SATURATED,
        }
        for pressure in [step / 2 for step in range(0, 201)]:
            samples = [LoadSample(cpu_pressure=pressure)]
            sampler = load_sampler(lambda: samples[0])
            intensity, _ = sampler()
            self.assertEqual(wire[sampler.detail[0]], intensity, pressure)

    def test_strain_names_the_resource_disk_then_memory_then_cpu(self) -> None:
        samples = [LoadSample()]
        sampler = load_sampler(lambda: samples[0])
        self.assertEqual(sampler.detail, (LoadLevel.NONE, StrainKind.NONE))
        sampler()
        self.assertEqual(sampler.detail, (LoadLevel.NONE, StrainKind.CLEAR))
        samples[0] = LoadSample(cpu_pressure=90, cpu_pressure_60=90)
        self.assertTrue(sampler()[1])
        self.assertEqual(sampler.detail, (LoadLevel.SATURATED, StrainKind.CPU))
        samples[0] = LoadSample(cpu_pressure=90, cpu_pressure_60=90, memory_available=0.01)
        sampler()
        self.assertEqual(sampler.detail[1], StrainKind.MEMORY)
        samples[0] = LoadSample(
            cpu_pressure=90, cpu_pressure_60=90, memory_available=0.01, disk_free=0.01
        )
        sampler()
        self.assertEqual(sampler.detail[1], StrainKind.DISK)


def adapters_with(**samplers):
    now = [100.0]
    woken: list[int] = []
    resolver = SemanticResolver()
    adapters = SemanticAdapters(
        resolver,
        NotificationPolicy(),
        lambda: woken.append(1),
        lambda: now[0],
        **samplers,
    )
    return now, resolver, adapters, woken


class AdapterTests(unittest.TestCase):
    def test_reads_the_same_state_the_wire_folds(self) -> None:
        joined = [False]
        samples = [LoadSample(cpu_pressure=7.0, memory_available=0.5, disk_free=0.5)]
        now, resolver, adapters, _ = adapters_with(
            load_sampler=load_sampler(lambda: samples[0]), call_sampler=lambda: joined[0]
        )
        # Before anything is sampled: present, and nothing else known.
        self.assertEqual(
            adapters.host_signals(),
            HostSignals(
                Presence.ACTIVE,
                LoadLevel.NONE,
                StrainKind.NONE,
                CallState.CLEAR,
                AlertState.CLEAR,
                CommandState.IDLE,
            ),
        )
        adapters.poll(now[0])
        signals = adapters.host_signals()
        self.assertEqual((signals.load, signals.strain), (LoadLevel.LIGHT, StrainKind.CLEAR))
        self.assertEqual(resolver.state.civic.intensity, Intensity.CALM)

        adapters.session_presence(idle=True)
        self.assertEqual(adapters.host_signals().presence, Presence.IDLE)
        adapters.session_presence(locked=True)
        self.assertEqual(adapters.host_signals().presence, Presence.LOCKED)
        self.assertEqual(resolver.state.civic.mode, Mode.QUIET)
        adapters.session_presence(idle=False, locked=False)

        adapters.urgent_alert(1, UrgentKind.INCOMING_CALL)
        self.assertEqual(adapters.host_signals().call, CallState.RINGING)
        self.assertEqual(adapters.host_signals().alert, AlertState.CLEAR)
        adapters.urgent_alert(2, UrgentKind.CRITICAL)
        self.assertEqual(adapters.host_signals().alert, AlertState.CRITICAL)
        self.assertEqual(resolver.state.civic.mode, Mode.URGENT)
        joined[0] = True
        now[0] += 10
        adapters.poll(now[0])
        self.assertEqual(adapters.host_signals().call, CallState.JOINED)
        # Answering ends the ring; the critical notice still holds URGENT.
        self.assertEqual(resolver.state.civic.mode, Mode.URGENT)
        adapters.urgent_alert(2, UrgentKind.CLOSED)
        self.assertEqual(adapters.host_signals().alert, AlertState.CLEAR)
        self.assertEqual(resolver.state.civic.mode, Mode.QUIET)

        adapters.terminal_started()
        self.assertEqual(adapters.host_signals().command, CommandState.RUNNING)
        adapters.terminal_started()
        self.assertEqual(adapters.host_signals().command, CommandState.SEVERAL)
        adapters.terminal_finished()
        adapters.terminal_finished()
        self.assertEqual(adapters.host_signals().command, CommandState.IDLE)

    def test_a_change_only_the_desktop_can_see_still_wakes_the_tick(self) -> None:
        _, resolver, adapters, woken = adapters_with()
        adapters.terminal_started()
        revision = resolver.state.revision
        before = len(woken)
        adapters.terminal_started()
        # One command or several is the same on the wire, so no new revision.
        self.assertEqual(resolver.state.revision, revision)
        self.assertGreater(len(woken), before)

    def test_a_sampler_without_detail_reports_none(self) -> None:
        now, _, adapters, _ = adapters_with(load_sampler=lambda: (Intensity.BUSY, True))
        adapters.poll(now[0])
        signals = adapters.host_signals()
        self.assertEqual((signals.load, signals.strain), (LoadLevel.NONE, StrainKind.NONE))


class RuntimeTests(unittest.TestCase):
    def setUp(self) -> None:
        FakeGLib.reset()

    def runtime(self, *, host_signals: bool):
        now = [10.0]
        resolver = SemanticResolver()
        policy = NotificationPolicy()
        runtime = DaemonRuntime(
            FakeGio,
            FakeGLib,
            FakeLoop(),
            FakeHeartbeat(),
            resolver,
            policy,
            FocusArbiter(settle_seconds=0),
            clock=lambda: now[0],
            host_signals=host_signals,
        )
        adapters = SemanticAdapters(resolver, policy, runtime.wake, lambda: now[0])
        runtime.bind_adapters(adapters)
        published: list[tuple[int, ...]] = []
        runtime.add_host_listener(published.append)
        return runtime, adapters, published

    def test_off_nothing_is_published_and_the_method_reports_none(self) -> None:
        runtime, adapters, published = self.runtime(host_signals=False)
        runtime.tick()
        adapters.terminal_started()
        adapters.session_presence(locked=True)
        adapters.urgent_alert(3, UrgentKind.CRITICAL)
        runtime.tick()
        runtime.tick()
        self.assertEqual(published, [])
        self.assertEqual(runtime.host_signals(), (0,) * 6)

    def test_on_publishes_once_and_then_on_each_change(self) -> None:
        runtime, adapters, published = self.runtime(host_signals=True)
        runtime.tick()
        runtime.tick()
        self.assertEqual(
            published,
            [
                (
                    Presence.ACTIVE,
                    LoadLevel.NONE,
                    StrainKind.NONE,
                    CallState.CLEAR,
                    AlertState.CLEAR,
                    CommandState.IDLE,
                )
            ],
        )
        adapters.terminal_started()
        runtime.tick()
        adapters.terminal_started()
        runtime.tick()
        runtime.tick()
        self.assertEqual(
            [signals[5] for signals in published],
            [CommandState.IDLE, CommandState.RUNNING, CommandState.SEVERAL],
        )
        self.assertEqual(runtime.host_signals(), published[-1])
        for signals in published:
            HostSignals.from_bytes(signals)


class ServiceViewTests(unittest.TestCase):
    def view(self) -> ServiceView:
        # The view's bus handling is exercised end to end in test_dbus_control;
        # here only what it keeps and what it hands the renderer.
        view = ServiceView.__new__(ServiceView)
        view.status = ("connected", "", False, "")
        view.world = (0,) * 8
        view.host_signals = NO_HOST_SIGNALS
        view.listeners = []
        return view

    def test_the_desktop_city_takes_the_signals_into_its_input(self) -> None:
        view = self.view()
        self.assertEqual([getattr(view.city(0x5A), f) for f in HostSignals._fields], [0] * 6)
        top = tuple(len(kind) - 1 for _, kind in HOST_SIGNAL_FIELDS)
        view._take_host_signals(top)
        city = view.city(0x5A)
        self.assertIsInstance(city, CityInput)
        self.assertEqual(tuple(getattr(city, f) for f in HostSignals._fields), top)

    def test_a_value_outside_its_enum_is_dropped_whole(self) -> None:
        view = self.view()
        view._take_host_signals((1, 2, 1, 1, 1, 1))
        kept = view.host_signals
        for index, (_, kind) in enumerate(HOST_SIGNAL_FIELDS):
            values = [1] * 6
            values[index] = len(kind)
            view._take_host_signals(tuple(values))
            self.assertEqual(view.host_signals, kept)
        view._take_host_signals((1,) * 7)
        self.assertEqual(view.host_signals, kept)

    def test_a_signal_of_another_shape_is_ignored(self) -> None:
        class Variant:
            def __init__(self, signature, value):
                self.signature, self.value = signature, value

            def get_type_string(self):
                return self.signature

            def unpack(self):
                return self.value

        view = self.view()
        view._host_signals_changed(None, Variant("(yyyyyyy)", (1,) * 7))
        self.assertEqual(view.host_signals, NO_HOST_SIGNALS)
        view._host_signals_changed(None, Variant(HOST_SIGNALS_SIGNATURE, (3, 6, 4, 3, 2, 3)))
        self.assertEqual(view.host_signals.as_bytes(), (3, 6, 4, 3, 2, 3))
        self.assertEqual(HOST_SIGNALS_CHANGED, "HostSignalsChanged")


if __name__ == "__main__":
    unittest.main()
