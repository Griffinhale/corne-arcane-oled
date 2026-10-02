from __future__ import annotations

import argparse
import ast
import contextlib
import ctypes
import hashlib
import io
import os
import random
import select
import shutil
import subprocess
import sys
import tempfile
import threading
import unittest
from pathlib import Path
from unittest import mock

from arcane_host import city, city_window, typing_helper
from arcane_host.city import (
    CITY_ABI,
    HOST_SIGNAL_FIELDS,
    OFF_KEYBOARD_COUNTERS,
    OFF_KEYBOARD_FIELDS,
    AmbientState,
    CityError,
    CityInput,
    CityRenderer,
    CitySeason,
    Layout,
    candidate_paths,
    city_input,
    resting_input,
)
from arcane_host.dbus_contract import (
    BUS_NAME,
    CONTROL_INTERFACE,
    EVENTS_INTERFACE,
    FOCUS_INTERFACE,
    INJECT_SYNTHETIC,
    OBJECT_PATH,
    PAUSE,
    REPORT_ACTIVE_WINDOW,
    TYPING_SIGNATURE,
)
from arcane_host.protocol import (
    REPORT_SIZE,
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
)
from arcane_host.semantic import SemanticState
from arcane_host.typing_summary import Row, RowSpread, Spread, Tempo, TypingSummary
from test_dbus_control import FRAME, HOST_DIR, EchoKeyboard, Gio, GLib, holds, wait_until


def library_available() -> bool:
    return any(path.is_file() for path in candidate_paths())


def digest(frame: bytes) -> str:
    """A frame's short name, so a failing comparison prints sixteen characters."""
    return hashlib.sha256(frame).hexdigest()[:16]


requires_library = unittest.skipUnless(
    library_available(), "libcornearcane.so is not built; run `make city-lib`"
)


class CityInputTests(unittest.TestCase):
    def test_struct_is_twenty_seven_bounded_bytes(self) -> None:
        # The privacy boundary is structural: every field is a small integer
        # and there is nowhere a title, URL, or notification body could ride.
        # ABI 9 appended the season and three day tallies to ABI 8's seventeen,
        # and ABI 10 six host signals to those twenty-one.
        self.assertEqual(ctypes.sizeof(CityInput), 27)
        self.assertTrue(all(kind is ctypes.c_uint8 for _, kind in CityInput._fields_))

    def test_off_keyboard_fields_follow_the_payload(self) -> None:
        # The first ten bytes keep their offsets; the off-keyboard signals
        # are appended, so nothing that wrote the old struct by offset moves.
        # The tallies follow the signals, and the host signals the tallies,
        # for the same reason.
        names = [name for name, _ in CityInput._fields_]
        self.assertEqual(names[8:10], ["online", "seed"])
        self.assertEqual(
            names[10:],
            [field for field, _ in OFF_KEYBOARD_FIELDS]
            + list(OFF_KEYBOARD_COUNTERS)
            + [field for field, _ in HOST_SIGNAL_FIELDS],
        )
        self.assertEqual(CityInput.tempo.offset, 10)
        self.assertEqual(CityInput.season.offset, 17)
        self.assertEqual(CityInput.tally_casts.offset, 18)
        self.assertEqual(
            [getattr(CityInput, field).offset for field, _ in HOST_SIGNAL_FIELDS],
            [21, 22, 23, 24, 25, 26],
        )

    def test_off_keyboard_fields_start_at_none(self) -> None:
        packed = city_input(SemanticState(), seed=0x5A)
        for field, _ in OFF_KEYBOARD_FIELDS:
            self.assertEqual(getattr(packed, field), 0, field)
        for field, kind in OFF_KEYBOARD_FIELDS:
            self.assertEqual(kind(0).name, "NONE", field)
        for field in OFF_KEYBOARD_COUNTERS:
            self.assertEqual(getattr(packed, field), 0, field)
        for field, kind in HOST_SIGNAL_FIELDS:
            self.assertEqual(getattr(packed, field), 0, field)
            self.assertEqual(kind(0).name, "NONE", field)

    def test_carries_the_raw_hid_payload_in_payload_order(self) -> None:
        summary = NotificationSummary(3, Category.COMMUNICATION, Priority.CRITICAL, 5, True)
        civic = CivicState(Floor.WORKSHOP, Mode.QUIET, Intensity.BUSY, Secondary.TRANSFER)
        state = SemanticState(Scene.ARCHIVE, summary, civic, 7)
        packed = city_input(state, seed=0x5A)
        self.assertEqual(
            (
                packed.scene,
                packed.notif_count,
                packed.category,
                packed.priority,
                packed.age,
                packed.persistent,
            ),
            (int(Scene.ARCHIVE), 3, int(Category.COMMUNICATION), int(Priority.CRITICAL), 5, 1),
        )
        self.assertEqual(packed.civic, civic.civic_byte())
        self.assertEqual(packed.secondary, civic.secondary_byte())
        self.assertEqual((packed.online, packed.seed), (1, 0x5A))

    def test_offline_and_seed_masking(self) -> None:
        packed = city_input(SemanticState(), online=False, seed=0x1FF)
        self.assertEqual((packed.online, packed.seed), (0, 0xFF))

    def test_resting_input_is_a_calm_commons(self) -> None:
        packed = resting_input()
        self.assertEqual((packed.scene, packed.civic, packed.secondary), (int(Scene.DUEL), 0, 0))
        self.assertEqual(packed.notif_count, 0)


@requires_library
class CityRendererTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.renderer = CityRenderer(scale=1, layout=Layout.DESK)

    def render(self, packed: CityInput, elapsed_ms: int = 400_000, frame: int = 0) -> bytes:
        return self.renderer.render(packed, elapsed_ms, frame)

    def test_abi_and_geometry(self) -> None:
        self.assertEqual(self.renderer._library.duel_city_abi_version(), CITY_ABI)
        # Both canvases plus the three-pixel desk gap.
        self.assertEqual((self.renderer.width, self.renderer.height), (67, 128))
        scaled = CityRenderer(scale=4)
        self.assertEqual((scaled.width, scaled.height), (67 * 4, 128 * 4))

    def test_frame_is_a_complete_pgm(self) -> None:
        # The desk layout, so the gap is visible and can be checked.
        blob = self.render(resting_input(seed=0x5A))
        self.assertTrue(blob.startswith(b"P5\n67 128\n255\n"))
        header, _, pixels = blob.partition(b"255\n")
        self.assertEqual(len(pixels), 67 * 128)
        self.assertEqual(set(pixels), {0, 96, 255})
        # The desk gap is exactly three columns of mid-grey on every row.
        for row in range(128):
            self.assertEqual(pixels[row * 67 + 32 : row * 67 + 35], b"\x60\x60\x60")

    def test_a_city_is_actually_drawn(self) -> None:
        _, _, pixels = self.render(resting_input(seed=0x5A)).partition(b"255\n")
        self.assertGreater(sum(1 for value in pixels if value == 255), 500)

    def test_each_floor_draws_its_own_room(self) -> None:
        seen = {}
        for floor in Floor:
            scene = Scene.FOCUS if floor is Floor.SPECIAL else Scene.DUEL
            state = SemanticState(scene, NotificationSummary(), CivicState(floor=floor))
            seen[floor] = self.render(city_input(state, seed=0x5A))
        self.assertEqual(len(set(seen.values())), len(Floor))

    def test_the_sky_follows_the_clock(self) -> None:
        packed = resting_input(seed=0x5A)
        # Dawn, day, dusk, night: the four phases of the firmware's sky cycle.
        frames = {
            self.render(packed, elapsed_ms=ms) for ms in (10_000, 400_000, 1_400_000, 1_700_000)
        }
        self.assertEqual(len(frames), 4)

    def test_rendering_is_deterministic(self) -> None:
        packed = resting_input(seed=0x5A)
        self.assertEqual(self.render(packed, 400_000, 9), self.render(packed, 400_000, 9))

    def test_an_offline_daemon_empties_the_disposable_context(self) -> None:
        state = SemanticState(
            Scene.ARCHIVE,
            NotificationSummary(4, Category.SYSTEM, Priority.NORMAL, 1, False),
            CivicState(Floor.RESEARCH, Mode.URGENT, Intensity.BUSY, Secondary.SYSTEM),
        )
        online = self.render(city_input(state, online=True, seed=0x5A))
        offline = self.render(city_input(state, online=False, seed=0x5A))
        self.assertNotEqual(online, offline)
        # Expiry is the heartbeat timeout's: the city returns to its resting
        # reading rather than to an empty screen.
        self.assertEqual(offline, self.render(resting_input(online=False, seed=0x5A)))

    def test_a_state_the_firmware_would_reject_is_refused(self) -> None:
        # Civic bits 6-7 are reserved on Raw HID v3, and an empty summary must
        # be canonical. Both are rejected by the firmware's own acceptance path.
        for field, value in (("civic", 0xC0), ("category", int(Category.SYSTEM))):
            packed = resting_input()
            setattr(packed, field, value)
            with self.assertRaisesRegex(CityError, "outside its enum"):
                self.render(packed)

    def test_off_keyboard_values_outside_their_enum_are_refused(self) -> None:
        # Each new field takes every value of its enum and nothing past it.
        # Zero is "none", so a struct that never heard of these fields is valid.
        for field, kind in (*OFF_KEYBOARD_FIELDS, *HOST_SIGNAL_FIELDS):
            top = max(kind)
            for value in (top + 1, 0xFF):
                packed = resting_input(seed=0x5A)
                setattr(packed, field, value)
                with self.assertRaisesRegex(CityError, "outside its enum", msg=f"{field}={value}"):
                    self.render(packed)

    def test_host_signals_are_accepted_and_draw_nothing_yet(self) -> None:
        # ABI 10's host signals are accepted by every shell before any art
        # reads them (SH11): each takes every value of its enum and changes no
        # frame in any layout.
        for layout in Layout:
            renderer = CityRenderer(scale=1, layout=layout)
            base = digest(renderer.render(resting_input(seed=0x5A), 400_000, 12))
            for field, kind in HOST_SIGNAL_FIELDS:
                for value in kind:
                    packed = resting_input(seed=0x5A)
                    setattr(packed, field, int(value))
                    frame = digest(renderer.render(packed, 400_000, 12))
                    self.assertEqual(frame, base, f"{layout} {field}={value.name}")

    def test_the_season_is_accepted_and_draws_nothing_yet(self) -> None:
        # ABI 9's calendar is accepted by every shell before any art reads it:
        # a season takes its four values and changes no frame in any layout.
        for layout in Layout:
            renderer = CityRenderer(scale=1, layout=layout)
            base = digest(renderer.render(resting_input(seed=0x5A), 400_000, 12))
            for season in CitySeason:
                packed = resting_input(seed=0x5A)
                packed.season = int(season)
                self.assertEqual(digest(renderer.render(packed, 400_000, 12)), base, season)

    @staticmethod
    def day(**tallies: int) -> CityInput:
        packed = resting_input(seed=0x5A)
        for field, value in tallies.items():
            setattr(packed, field, value)
        return packed

    def test_the_days_tallies_draw_the_almanac_in_the_town_only(self) -> None:
        # DC9: the town and landscape keep the day on a notice board in the
        # square. The four panel layouts are the keyboard's own screens, and
        # the keyboard has no day to keep.
        for layout in Layout:
            renderer = CityRenderer(scale=1, layout=layout)
            base = renderer.render(resting_input(seed=0x5A), 400_000, 12)
            for field in OFF_KEYBOARD_COUNTERS:
                for value in (1, 128, 255):
                    frame = renderer.render(self.day(**{field: value}), 400_000, 12)
                    if layout in self.TYPING_LAYOUTS:
                        self.assertNotEqual(frame, base, f"{layout} {field}={value}")
                    else:
                        self.assertEqual(frame, base, f"{layout} {field}={value}")

    def test_the_almanac_counts_in_steps_on_one_board(self) -> None:
        # A stroke per step, at 1, 4, 16, 64 and a full byte: a level, never a
        # reading. Counts inside one step draw the same board, the first count
        # of the next step draws another, and every pixel the day moves is on
        # the board, not elsewhere in the town.
        for layout in self.TYPING_LAYOUTS:
            renderer = CityRenderer(scale=1, layout=layout)
            width = renderer.width

            def pixels(packed: CityInput) -> bytes:
                return renderer.render(packed, 400_000, 12).partition(b"255\n")[2]

            base = pixels(resting_input(seed=0x5A))
            for field in OFF_KEYBOARD_COUNTERS:
                boards = [
                    pixels(self.day(**{field: n})) for n in (1, 3, 4, 15, 16, 63, 64, 254, 255)
                ]
                self.assertEqual(boards[1], boards[0], f"{layout} {field} 1..3")
                self.assertEqual(boards[3], boards[2], f"{layout} {field} 4..15")
                self.assertEqual(boards[5], boards[4], f"{layout} {field} 16..63")
                self.assertEqual(boards[7], boards[6], f"{layout} {field} 64..254")
                steps = [boards[0], boards[2], boards[4], boards[6], boards[8]]
                self.assertEqual(len(set(steps)), 5, f"{layout} {field}")
            full = pixels(self.day(tally_casts=255, tally_impacts=255, tally_knockdowns=255))
            moved = [i for i, (a, b) in enumerate(zip(base, full, strict=True)) if a != b]
            xs = [i % width for i in moved]
            ys = [i // width for i in moved]
            self.assertLessEqual(max(xs) - min(xs) + 1, 25, layout)
            self.assertLessEqual(max(ys) - min(ys) + 1, 28, layout)

    # Today's resting frame in each layout with every off-keyboard field at
    # none. The panels are as the renderer drew them before those fields
    # existed; the town and landscape moved once, reviewed, when the lit tower
    # storey stopped being drawn inverted (NF22). The resting frame holds a jam
    # in the right city, so the panels that show it moved once, reviewed, when
    # a rare event began gathering a crowd. The town and landscape moved
    # again, reviewed, when they began drawing that event (DC1): the deck's
    # damage complaint being put right, a crack and a ladder on the end house.
    # They moved once more, reviewed, when the storeys became districts (DC2):
    # the Research floor above the Commons is now the Research district's
    # telescope and cabinet, the books having gone to the Scriptorium.
    RESTING_FRAMES = {
        Layout.DESK: "0b41d7c8dd9fa0b0",
        Layout.CITY: "5e29dc8ca422df26",
        Layout.LEFT: "0693730495095ab7",
        Layout.RIGHT: "5b7fcdb5d8b0fa7e",
        Layout.TOWN: "c4401dbb55dc9646",
        Layout.LANDSCAPE: "d246b2e8962eced2",
    }
    # Only the town layers draw the typing summary and the health buckets. The
    # four panel layouts are the keyboard's own two screens, and the keyboard
    # never sees either.
    TYPING_LAYOUTS = (Layout.TOWN, Layout.LANDSCAPE)
    TYPING_FIELDS = ("tempo", "spread", "row", "row_spread")
    HEALTH_FIELDS = ("body", "heart", "sleep")

    def test_none_renders_today_unchanged(self) -> None:
        for layout, pinned in self.RESTING_FRAMES.items():
            frame = CityRenderer(scale=1, layout=layout).render(
                resting_input(seed=0x5A), 400_000, 12
            )
            self.assertEqual(digest(frame), pinned, layout)

    # The tower's three storeys at scale 1 in the town layout: the band
    # between ROOM_X0 and ROOM_X1 in desktop/duel_town_draw.c, and the rows of
    # the upper neighbour, the tall active storey and the lower neighbour.
    TOWER_ROOM_X = (112, 145)
    TOWER_STOREY_ROWS = ((110, 125), (131, 159), (165, 180))

    def storey_ink(self, layout: Layout, packed: CityInput) -> list[float]:
        renderer = CityRenderer(scale=1, layout=layout)
        _, _, pixels = renderer.render(packed, 400_000, 12).partition(b"255\n")
        offset = (renderer.width - 256) // 2
        x0, x1 = (x + offset for x in self.TOWER_ROOM_X)
        shares = []
        for y0, y1 in self.TOWER_STOREY_ROWS:
            lit = sum(
                1 for y in range(y0, y1) for x in range(x0, x1) if pixels[y * renderer.width + x]
            )
            shares.append(lit / ((y1 - y0) * (x1 - x0)))
        return shares

    def storey_pixels(self, layout: Layout, packed: CityInput, slot: int) -> bytes:
        renderer = CityRenderer(scale=1, layout=layout)
        _, _, pixels = renderer.render(packed, 400_000, 12).partition(b"255\n")
        offset = (renderer.width - 256) // 2
        x0, x1 = (x + offset for x in self.TOWER_ROOM_X)
        y0, y1 = self.TOWER_STOREY_ROWS[slot]
        return b"".join(
            pixels[y * renderer.width + x0 : y * renderer.width + x1] for y in range(y0, y1)
        )

    def test_the_observatory_stage_shows_in_the_town(self) -> None:
        # DC3: the four-stage instrument the panels draw from the civic
        # intensity is drawn in the Observatory room too, inside the room and
        # not only as which landings are lit, so a watch whose intensity
        # follows its body bucket (R1) shows the stage. The other rooms do not
        # change between calm and active, which is what makes it the
        # Observatory's instrument rather than a brighter tower.
        for layout in self.TYPING_LAYOUTS:
            rooms = set()
            for level in Intensity:
                civic = CivicState(floor=Floor.SPECIAL, intensity=level)
                state = SemanticState(Scene.FOCUS, NotificationSummary(), civic)
                rooms.add(self.storey_pixels(layout, city_input(state, seed=0x5A), 1))
            self.assertEqual(len(rooms), len(Intensity), layout)
            for floor in (Floor.COMMONS, Floor.RESEARCH, Floor.WORKSHOP):
                calm, active = (
                    self.storey_pixels(
                        layout,
                        city_input(
                            SemanticState(
                                Scene.DUEL,
                                NotificationSummary(),
                                CivicState(floor=floor, intensity=level),
                            ),
                            seed=0x5A,
                        ),
                        1,
                    )
                    for level in (Intensity.CALM, Intensity.ACTIVE)
                )
                self.assertEqual(calm, active, f"{layout} {floor}")

    def test_the_active_storey_is_lamplit_not_inverted(self) -> None:
        # A lit storey is a dark room with its furniture and its own light in
        # white, like the rest of the town, not a white block. It still holds
        # more light than either neighbour, on every floor.
        for layout in self.TYPING_LAYOUTS:
            for floor in Floor:
                scene = Scene.FOCUS if floor is Floor.SPECIAL else Scene.DUEL
                state = SemanticState(scene, NotificationSummary(), CivicState(floor=floor))
                above, active, below = self.storey_ink(layout, city_input(state, seed=0x5A))
                self.assertLess(active, 0.5, f"{layout} {floor}")
                self.assertGreater(active, max(above, below), f"{layout} {floor}")

    # The eight districts, each as the (floor, scene) pair duel_civic_district
    # in firmware/sim/duel_host.h derives it from.
    DISTRICTS = {
        "commons": (Floor.COMMONS, Scene.DUEL),
        "research": (Floor.RESEARCH, Scene.ARCHIVE),
        "workshop": (Floor.WORKSHOP, Scene.DUEL),
        "observatory": (Floor.SPECIAL, Scene.FOCUS),
        "scriptorium": (Floor.RESEARCH, Scene.DUEL),
        "studio": (Floor.COMMONS, Scene.ARCHIVE),
        "arena": (Floor.COMMONS, Scene.REVEL),
        "undercroft": (Floor.WORKSHOP, Scene.ARCHIVE),
    }

    def test_each_district_has_its_own_room_in_the_town(self) -> None:
        # DC2: the active storey is the district's room, not the floor's, so
        # the three districts on the Commons floor and the two on each of
        # Research and Workshop are told apart. Every room is still lamplit
        # rather than inverted, and brighter than its neighbours.
        for layout in self.TYPING_LAYOUTS:
            rooms: dict[str, str] = {}
            for name, (floor, scene) in self.DISTRICTS.items():
                state = SemanticState(scene, NotificationSummary(), CivicState(floor=floor))
                packed = city_input(state, seed=0x5A)
                room = digest(self.storey_pixels(layout, packed, 1))
                self.assertNotIn(room, rooms.values(), f"{layout} {name}")
                rooms[name] = room
                above, active, below = self.storey_ink(layout, packed)
                self.assertLess(active, 0.5, f"{layout} {name}")
                self.assertGreater(active, max(above, below), f"{layout} {name}")

    def test_typing_values_change_the_frame(self) -> None:
        # Every typing value draws in the town layers, each differently from
        # the others, and nothing in the panels.
        for layout in Layout:
            renderer = CityRenderer(scale=1, layout=layout)
            base = digest(renderer.render(resting_input(seed=0x5A), 400_000, 12))
            for field, kind in OFF_KEYBOARD_FIELDS:
                if field not in self.TYPING_FIELDS:
                    continue
                frames = {base}
                for value in kind:
                    if value == 0:
                        continue
                    packed = resting_input(seed=0x5A)
                    setattr(packed, field, int(value))
                    frame = digest(renderer.render(packed, 400_000, 12))
                    if layout in self.TYPING_LAYOUTS:
                        self.assertNotIn(frame, frames, f"{layout} {field}={value}")
                        frames.add(frame)
                    else:
                        self.assertEqual(frame, base, f"{layout} {field}={value}")

    def test_health_values_change_the_frame(self) -> None:
        # Body, heart and sleep draw in the town layers too, each value
        # differently from the others, and nothing in the panels.
        for layout in Layout:
            renderer = CityRenderer(scale=1, layout=layout)
            base = digest(renderer.render(resting_input(seed=0x5A), 400_000, 12))
            for field, kind in OFF_KEYBOARD_FIELDS:
                if field not in self.HEALTH_FIELDS:
                    continue
                frames = {base}
                for value in kind:
                    if value == 0:
                        continue
                    packed = resting_input(seed=0x5A)
                    setattr(packed, field, int(value))
                    frame = digest(renderer.render(packed, 400_000, 12))
                    if layout in self.TYPING_LAYOUTS:
                        self.assertNotIn(frame, frames, f"{layout} {field}={value}")
                        frames.add(frame)
                    else:
                        self.assertEqual(frame, base, f"{layout} {field}={value}")

    # The signals that reach the residents on the square (DC4): body activity
    # is how many are out, tempo how fast they walk, sleep whether they step
    # briskly or sit down. The other four keep to their own objects.
    PLAZA_FIELDS = ("tempo", "body", "sleep")

    def plaza_pixels(self, layout: Layout, packed: CityInput) -> bytes:
        renderer = CityRenderer(scale=1, layout=layout)
        _, _, pixels = renderer.render(packed, 400_000, 12).partition(b"255\n")
        # Everything below the ground line, which is 48 rows up from the bottom
        # of either town composition.
        return pixels[(renderer.height - 47) * renderer.width :]

    def test_the_square_answers_to_body_tempo_and_sleep(self) -> None:
        for layout in self.TYPING_LAYOUTS:
            base = digest(self.plaza_pixels(layout, resting_input(seed=0x5A)))
            for field, kind in OFF_KEYBOARD_FIELDS:
                plazas = {base}
                for value in kind:
                    if value == 0:
                        continue
                    packed = resting_input(seed=0x5A)
                    setattr(packed, field, int(value))
                    plaza = digest(self.plaza_pixels(layout, packed))
                    if field in self.PLAZA_FIELDS:
                        self.assertNotIn(plaza, plazas, f"{layout} {field}={value}")
                        plazas.add(plaza)
                    else:
                        self.assertEqual(plaza, base, f"{layout} {field}={value}")

    def test_the_square_is_deterministic_in_its_signals(self) -> None:
        # Same inputs, same square, in a fresh renderer: nothing on the plaza
        # reads a clock or a random source.
        for layout in self.TYPING_LAYOUTS:
            packed = resting_input(seed=0x5A)
            packed.tempo, packed.body, packed.sleep = 4, 4, 2
            first = self.plaza_pixels(layout, packed)
            self.assertEqual(first, self.plaza_pixels(layout, packed), layout)
            self.assertGreater(first.count(255), 0, layout)

    def test_scale_is_bounded(self) -> None:
        with self.assertRaisesRegex(CityError, "scale"):
            CityRenderer(scale=0)
        with self.assertRaisesRegex(CityError, "scale"):
            CityRenderer(scale=17)

    def test_an_unloadable_library_is_named(self) -> None:
        with self.assertRaisesRegex(CityError, "cannot load"):
            CityRenderer(path="/nonexistent/libcornearcane.so")


class LibraryDiscoveryTests(unittest.TestCase):
    def test_the_override_is_searched_before_the_installed_package(self) -> None:
        with mock.patch.dict(os.environ, {"CORNE_ARCANE_CITY_LIB": "/opt/city.so"}):
            self.assertEqual(candidate_paths()[0], Path("/opt/city.so"))

    def test_a_checkout_finds_its_own_build_output(self) -> None:
        with mock.patch.dict(os.environ, {}, clear=True):
            paths = candidate_paths()
        self.assertEqual(len(paths), 2)
        self.assertEqual(paths[-1].parts[-2:], ("desktop", "libcornearcane.so"))

    def test_a_missing_library_says_where_it_looked(self) -> None:
        with mock.patch.object(city, "candidate_paths", lambda: [Path("/nonexistent/city.so")]):
            with self.assertRaisesRegex(CityError, "make city-lib"):
                city.library_path()


class FakePhoto:
    def __init__(self, **kwargs) -> None:
        self.data = None

    def configure(self, **kwargs) -> None:
        self.data = kwargs.get("data")


class FakeWidget:
    def __init__(self, *args, **kwargs) -> None:
        self.kwargs = kwargs
        self.packed = None
        self.placed = None

    def pack(self, **kwargs) -> None:
        self.packed = kwargs

    def place(self, **kwargs) -> None:
        self.placed = kwargs

    def cget(self, name):
        return self.kwargs.get(name)

    def configure(self, **kwargs) -> None:
        self.kwargs.update(kwargs)


class FakeRoot(FakeWidget):
    def __init__(self, *args, **kwargs) -> None:
        super().__init__(*args, **kwargs)
        self.updates = 0
        self.destroyed = False
        self.close_callback = None
        self.after_calls = []
        self.cancelled = []

    def title(self, _value) -> None:
        pass

    def configure(self, **kwargs) -> None:
        pass

    def resizable(self, *args) -> None:
        pass

    def geometry(self, value) -> None:
        self.requested_geometry = value

    def update(self) -> None:
        self.updates += 1

    def destroy(self) -> None:
        self.destroyed = True

    def protocol(self, name, callback) -> None:
        self.close_callback = callback

    def after(self, delay, callback=None) -> int:
        self.after_calls.append((delay, callback))
        return len(self.after_calls)

    def after_cancel(self, after_id) -> None:
        self.cancelled.append(after_id)


class FakeTk:
    Tk = FakeRoot
    Label = FakeWidget
    PhotoImage = FakePhoto


class FakeAmbient:
    def __init__(self) -> None:
        self.advanced = []

    def advance(self, elapsed_ms) -> int:
        self.advanced.append(elapsed_ms)
        return 1


class FakeLife:
    def __init__(self) -> None:
        self.advanced = []

    def advance(self, city, elapsed_ms, ambient=None) -> int:
        self.advanced.append((bytes(city), elapsed_ms, ambient))
        return 1


class FakeRenderer:
    def __init__(self, layout: Layout = Layout.CITY) -> None:
        self.layout = layout
        self.calls = []
        self.inputs = []
        self.worlds = []
        self.lives = []
        self.drawn_lives = []
        self.backdrop = "#123456"
        self.fps = 25
        self.width = 201

    def tour_stop(self, index, seed=0) -> CityInput:
        return CityInput(scene=index % 4, seed=seed)

    def ambient(self, seed) -> FakeAmbient:
        world = FakeAmbient()
        self.worlds.append((seed, world))
        return world

    def life(self, seed) -> FakeLife:
        life = FakeLife()
        self.lives.append((seed, life))
        return life

    def render(self, packed, elapsed_ms, frame, ambient=None, life=None) -> bytes:
        self.calls.append((packed.civic, elapsed_ms, frame, ambient))
        self.inputs.append(bytes(packed))
        self.drawn_lives.append(life)
        return b"pixels"


def build_window(clock=None, **kwargs) -> city_window.CityWindow:
    ticks = iter(clock or [0.0, 0.0, 0.04, 0.08, 0.12, 0.16])
    kwargs.setdefault("duels", False)
    return city_window.CityWindow(
        renderer=FakeRenderer(), tk=FakeTk, seed=0x5A, clock=lambda: next(ticks), **kwargs
    )


class CityWindowTests(unittest.TestCase):
    def test_draw_advances_frames_and_presents(self) -> None:
        window = build_window()
        window.draw_state(SemanticState())
        window.draw_state(SemanticState())
        self.assertEqual(window.frames, 2)
        self.assertEqual(window.photo.data, b"pixels")
        self.assertEqual(window.root.updates, 2)
        # Elapsed milliseconds come from the window's own clock, in order.
        self.assertEqual([call[1] for call in window.renderer.calls], [0, 40])
        self.assertEqual([call[3] for call in window.renderer.calls], [None, None])

    def test_close_cancels_a_pending_redraw(self) -> None:
        # A timer left pending across destroy() fires against a dead widget and
        # Tk reports it on stderr.
        window = build_window()
        window.schedule(40, lambda: None)
        window.close()
        self.assertEqual(window.root.cancelled, [1])
        self.assertTrue(window.root.destroyed)

    def test_draw_after_close_is_a_no_op(self) -> None:
        window = build_window()
        window.close()
        window.draw_state(SemanticState())
        self.assertTrue(window.root.destroyed)
        self.assertEqual(window.renderer.calls, [])


@requires_library
class PresentationPolicyTests(unittest.TestCase):
    """Decisions a second shell would otherwise repeat, kept in the renderer."""

    def test_the_default_scale_is_the_renderers_decision(self) -> None:
        self.assertEqual(CityRenderer(layout=Layout.CITY).scale, 4)
        self.assertEqual(CityRenderer(layout=Layout.TOWN).scale, 2)
        self.assertEqual(CityRenderer(layout=Layout.LANDSCAPE).scale, 2)

    def test_fit_scale_never_renders_a_fraction_of_a_pixel(self) -> None:
        renderer = CityRenderer(layout=Layout.TOWN)
        self.assertEqual(renderer.fit_scale(512, 512), 2)
        self.assertEqual(renderer.fit_scale(1024, 600), 2)
        self.assertEqual(renderer.fit_scale(10, 10), 1)
        wide = CityRenderer(layout=Layout.LANDSCAPE)
        self.assertEqual(wide.fit_scale(800, 480), 2)

    def test_the_backdrop_is_the_renderers_decision(self) -> None:
        # The desk continues around the panels; every other layout sits on the
        # same unlit ground its own gap is drawn in.
        self.assertEqual(CityRenderer(layout=Layout.DESK).backdrop, "#303030")
        for layout in (Layout.CITY, Layout.LEFT, Layout.RIGHT, Layout.TOWN, Layout.LANDSCAPE):
            self.assertEqual(CityRenderer(layout=layout).backdrop, "#000000")

    def test_the_cadence_comes_from_the_simulation(self) -> None:
        renderer = CityRenderer()
        # The simulation's own tick, not the slower civic clock.
        self.assertEqual(renderer.frame_interval_ms, 40)
        self.assertEqual(renderer.fps, 25)

    def test_the_run_up_a_seeking_shell_renders_is_the_renderers_decision(self) -> None:
        renderer = CityRenderer()
        # A second of frames at the world's own cadence. The number belongs to
        # the renderer because every shell that arrives at a moment by link
        # needs the same one, and one that picks its own arrives elsewhere.
        self.assertEqual(renderer.seek_warm_frames, 25)
        self.assertEqual(renderer.seek_warm_frames * renderer.frame_interval_ms, 1000)

    def arrive(self, target: int, render_from: int, seed: int = 0x5A) -> bytes:
        """The frame at `target`, having rendered only from `render_from` on."""
        renderer = CityRenderer(scale=1, layout=Layout.TOWN)
        world = renderer.ambient(seed)
        city = renderer.tour_stop(0, seed)
        image = b""
        for frame in range(target + 1):
            now = frame * renderer.frame_interval_ms
            world.advance(now)
            if frame >= render_from:
                image = renderer.render(city, now, frame, ambient=world)
        return image

    def test_arriving_at_a_moment_takes_the_run_up_to_match_watching_into_it(self) -> None:
        # What the run-up is for, and the reason it is policy rather than a
        # detail of whichever shell noticed first. All three replay the world
        # tick by tick, so the only difference is what was drawn: the renderer
        # carries the floor transition and the outcome flash between frames,
        # and a shell that draws only the frame it wants composes both from a
        # standing start. Frame 900 is a moment where that shows -- not every
        # moment is, because the two policies are not always mid-transition.
        target = 900
        warm = CityRenderer().seek_warm_frames
        watched = self.arrive(target, 0)
        self.assertEqual(self.arrive(target, target - warm), watched)
        self.assertNotEqual(self.arrive(target, target), watched)

    def test_the_tour_visits_every_floor(self) -> None:
        renderer = CityRenderer()
        floors = {renderer.tour_stop(i).civic & 3 for i in range(renderer.tour_length)}
        self.assertEqual(floors, {0, 1, 2, 3})

    def test_the_tour_wraps_and_carries_the_seed(self) -> None:
        renderer = CityRenderer()
        first = renderer.tour_stop(0, 0x5A)
        wrapped = renderer.tour_stop(renderer.tour_length, 0x5A)
        self.assertEqual(bytes(first), bytes(wrapped))
        self.assertEqual(first.seed, 0x5A)

    def test_every_tour_stop_is_a_state_the_renderer_accepts(self) -> None:
        renderer = CityRenderer(scale=1)
        for index in range(renderer.tour_length):
            renderer.render(renderer.tour_stop(index, 0x5A), 400_000, 0)


class LayoutTests(unittest.TestCase):
    """The world is 67x128 whatever the panel layout; only the framing changes."""

    def test_argument_parsing_of_window_size(self) -> None:
        self.assertEqual(city_window.window_size("256x256"), (256, 256))
        self.assertEqual(city_window.window_size("512X384"), (512, 384))
        for bad in ("256", "axb", "0x256", "256x-1"):
            with self.assertRaises(argparse.ArgumentTypeError):
                city_window.window_size(bad)

    def test_scale_and_size_are_alternatives(self) -> None:
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
            city_window.parse_args(["--scale", "4", "--size", "256x256"])

    def test_the_default_layout_is_the_town(self) -> None:
        # The view the phone and the watch open on; the keyboard's own
        # framing is one flag away.
        args = city_window.parse_args([])
        self.assertEqual(args.layout, "town")
        self.assertEqual(city_window.parse_args(["--layout", "city"]).layout, "city")


@requires_library
class LayoutRenderTests(unittest.TestCase):
    def frame(self, layout: Layout) -> bytes:
        renderer = CityRenderer(scale=1, layout=layout)
        return renderer.render(resting_input(seed=0x5A), 400_000, 0)

    def pixels(self, layout: Layout) -> bytes:
        return self.frame(layout).partition(b"255\n")[2]

    def test_geometry_per_layout(self) -> None:
        for layout in (Layout.DESK, Layout.CITY):
            renderer = CityRenderer(scale=1, layout=layout)
            self.assertEqual((renderer.base_width, renderer.base_height), (67, 128))
        for layout in (Layout.LEFT, Layout.RIGHT):
            renderer = CityRenderer(scale=1, layout=layout)
            self.assertEqual((renderer.base_width, renderer.base_height), (32, 128))
        renderer = CityRenderer(scale=1, layout=Layout.LANDSCAPE)
        self.assertEqual((renderer.base_width, renderer.base_height), (400, 240))

    def test_the_city_layout_keeps_the_geometry_and_drops_the_desk(self) -> None:
        desk, city = self.pixels(Layout.DESK), self.pixels(Layout.CITY)
        self.assertEqual(len(desk), len(city))
        self.assertEqual(set(desk), {0, 96, 255})
        self.assertEqual(set(city), {0, 255})
        # Only the three gap columns differ: the towers are untouched.
        differing = {index % 67 for index, (a, b) in enumerate(zip(desk, city)) if a != b}
        self.assertEqual(differing, {32, 33, 34})

    def test_a_single_tower_is_that_half_of_the_pair(self) -> None:
        desk = self.pixels(Layout.DESK)
        for layout, start in ((Layout.LEFT, 0), (Layout.RIGHT, 35)):
            half = self.pixels(layout)
            self.assertEqual(len(half), 32 * 128)
            rows = [desk[row * 67 + start : row * 67 + start + 32] for row in range(128)]
            self.assertEqual(half, b"".join(rows))

    def test_an_unknown_layout_is_refused(self) -> None:
        renderer = CityRenderer(scale=1)
        renderer.layout = 9
        with self.assertRaisesRegex(CityError, "layout"):
            renderer.render(resting_input(), 0, 0)


@requires_library
class TownLayoutTests(unittest.TestCase):
    """The square drawing layer: one tower at the centre of a small city."""

    def town(self, **civic) -> bytes:
        renderer = CityRenderer(scale=1, layout=Layout.TOWN)
        state = SemanticState(Scene.DUEL, NotificationSummary(), CivicState(**civic))
        return renderer.render(city_input(state, seed=0x5A), 400_000, 12).partition(b"255\n")[2]

    def test_it_is_a_square_canvas_of_its_own(self) -> None:
        renderer = CityRenderer(scale=1, layout=Layout.TOWN)
        self.assertEqual((renderer.base_width, renderer.base_height), (256, 256))
        self.assertEqual(len(self.town()), 256 * 256)

    def test_a_town_is_actually_drawn(self) -> None:
        pixels = self.town()
        self.assertEqual(set(pixels), {0, 255})
        # A tower, four houses, a plaza and a sky: far more ink than a panel.
        self.assertGreater(sum(1 for value in pixels if value == 255), 3000)

    def test_the_floor_moves_the_light_up_the_tower(self) -> None:
        # The same thing the panels do by changing room, done by changing which
        # storey is lit, so switching applications is visible either way.
        frames = {self.town(floor=floor) for floor in Floor}
        self.assertEqual(len(frames), len(Floor))

    def test_the_hour_reaches_the_town(self) -> None:
        renderer = CityRenderer(scale=1, layout=Layout.TOWN)
        packed = resting_input(seed=0x5A)
        frames = {renderer.render(packed, ms, 0) for ms in (400_000, 1_700_000)}
        self.assertEqual(len(frames), 2)

    def test_it_shares_the_world_and_not_the_pixels(self) -> None:
        renderer = CityRenderer(scale=1, layout=Layout.TOWN)
        world = renderer.ambient(0x5A)
        for frame in range(400):
            world.advance(frame * 40)
        packed = resting_input(seed=0x5A)
        self.assertNotEqual(
            renderer.render(packed, 16_000, 40, ambient=world),
            renderer.render(packed, 16_000, 40),
        )

    def test_the_landscape_is_a_wide_drawing_layer_not_a_town_crop(self) -> None:
        renderer = CityRenderer(scale=1, layout=Layout.LANDSCAPE)
        pixels = renderer.render(resting_input(seed=0x5A), 400_000, 12).partition(b"255\n")[2]
        self.assertEqual((renderer.base_width, renderer.base_height), (400, 240))
        self.assertEqual(len(pixels), 400 * 240)
        self.assertEqual(set(pixels), {0, 255})
        # Sky, hills, paving and residents are composed through the added
        # wings; they are not black bars around a square image.
        left = b"".join(pixels[y * 400 : y * 400 + 64] for y in range(240))
        right = b"".join(pixels[y * 400 + 336 : (y + 1) * 400] for y in range(240))
        self.assertGreater(left.count(255), 300)
        self.assertGreater(right.count(255), 300)


class CivicEventState(ctypes.Structure):
    """``civic_event_state_t``: the rare-event deck's state for one civic phase."""

    _fields_ = [
        ("id_target", ctypes.c_uint8),
        ("phase", ctypes.c_uint8),
        ("progress", ctypes.c_uint8),
    ]


@requires_library
class TownCivicTests(unittest.TestCase):
    """DC1: couriers, rare events and aftermath residents in the town layers.

    The panels have drawn all three from the civic bytes since the Twin
    Cities; the town read only the aftermath's spell flavor. These cases say
    that every civic state now shows in both town compositions.
    """

    LAYOUTS = (Layout.TOWN, Layout.LANDSCAPE)
    SEED = 0x5A

    @staticmethod
    def courier(category: Category, age: int, count: int, persistent: bool = False) -> CityInput:
        priority = Priority.CRITICAL if persistent else Priority.NORMAL
        summary = NotificationSummary(count, category, priority, age, persistent)
        return city_input(SemanticState(Scene.DUEL, summary, CivicState()), seed=0x5A)

    def test_each_courier_state_changes_the_town(self) -> None:
        # Kind (by category, and a persistent notice stations a sentinel in
        # its own city), lifecycle (by age) and density (by count): every
        # combination is its own frame, and none is the empty street.
        routes = (
            (Category.COMMUNICATION, False),  # messenger, left
            (Category.TRANSFER, False),  # parcel, right
            (Category.SYSTEM, False),  # beacon, right
            (Category.SECURITY, False),  # sentinel, left
            (Category.TRANSFER, True),  # sentinel, right
        )
        for layout in self.LAYOUTS:
            renderer = CityRenderer(scale=1, layout=layout)
            base = digest(renderer.render(resting_input(seed=self.SEED), 400_000, 12))
            frames = {base}
            for category, persistent in routes:
                for age in (0, 1, 3, 7):
                    for count in (1, 3, 5):
                        packed = self.courier(category, age, count, persistent)
                        frame = digest(renderer.render(packed, 400_000, 12))
                        label = f"{layout} {category.name} p={persistent} age={age} n={count}"
                        self.assertNotIn(frame, frames, label)
                        frames.add(frame)

    def deck(self) -> dict[int, tuple[int, int, int]]:
        """The resting town's rare event for every civic phase: (id, phase, target)."""
        library = ctypes.CDLL(str(city.library_path()))
        library.civic_event_derive.argtypes = [
            ctypes.c_uint8,
            ctypes.c_uint8,
            ctypes.c_bool,
            ctypes.c_int8,
        ]
        library.civic_event_derive.restype = CivicEventState
        states = {}
        for phase in range(256):
            state = library.civic_event_derive(self.SEED, phase, True, 0)
            states[phase] = (state.id_target & 7, state.phase & 3, (state.id_target >> 5) & 3)
        return states

    # 153.6 s is civic phase 512, so phase p is at 153_600 + 300 p; all 256
    # phases fall inside the first quarter of the day, so the sky is the same
    # in every one of them. What the civic clock also moves -- the residents
    # on the square and the tower's rooms -- is masked out.
    DECK_ORIGIN_MS = 153_600

    def event_view(self, renderer: CityRenderer, phase: int) -> bytes:
        ms = self.DECK_ORIGIN_MS + 300 * phase + 150
        frame = renderer.render(resting_input(seed=self.SEED), ms, 12)
        pixels = bytearray(frame.partition(b"255\n")[2])
        width, height = renderer.width, renderer.height
        ground = height - 48
        centre = width // 2
        for y in range(height):
            for x in range(width):
                if y >= ground + 2 or (104 <= y < ground and centre - 20 <= x <= centre + 20):
                    pixels[y * width + x] = 0
        return bytes(pixels)

    def test_each_rare_event_changes_the_town(self) -> None:
        # Every family in every one of its four phases is its own picture, and
        # the same state at two different moments is the same picture.
        deck = self.deck()
        first: dict[tuple[int, int], int] = {}
        repeat: dict[tuple[int, int, int], list[int]] = {}
        for phase, state in deck.items():
            first.setdefault(state[:2], phase)
            repeat.setdefault(state, []).append(phase)
        self.assertEqual({key[0] for key in first}, set(range(1, 7)), "every family is dealt")
        for layout in self.LAYOUTS:
            renderer = CityRenderer(scale=1, layout=layout)
            views: dict[tuple[int, int], str] = {}
            for key, phase in sorted(first.items()):
                view = digest(self.event_view(renderer, phase))
                self.assertNotIn(view, views.values(), f"{layout} event={key} phase={phase}")
                views[key] = view
            for state, phases in repeat.items():
                if len(phases) > 1:
                    self.assertEqual(
                        self.event_view(renderer, phases[0]),
                        self.event_view(renderer, phases[1]),
                        f"{layout} {state} at {phases[:2]}",
                    )

    # The street at the tower's foot either side of the door, in town
    # coordinates: where each half's aftermath resident stands.
    AFTERMATH_BOX = {0: (88, 108), 1: (148, 168)}

    def test_each_aftermath_kind_puts_a_resident_at_the_tower(self) -> None:
        renderer = CityRenderer(scale=1, layout=Layout.TOWN)
        library = renderer._library
        library.duel_ambient_world.argtypes = [ctypes.c_void_p]
        library.duel_ambient_world.restype = ctypes.c_void_p
        for name in ("incantation_aftermath_revision", "incantation_aftermath_shared"):
            getattr(library, name).argtypes = [ctypes.c_void_p]
            getattr(library, name).restype = ctypes.c_uint8
        world = renderer.ambient(self.SEED)
        ground = 208
        seen: dict[int, dict[int, bytes]] = {0: {}, 1: {}}
        for ms in range(0, 120_000, 40):
            world.advance(ms)
            handle = library.duel_ambient_world(ctypes.addressof(world._state))
            if not library.incantation_aftermath_revision(handle) & 0x80:
                continue
            shared = library.incantation_aftermath_shared(handle)
            kinds = (shared & 7, (shared >> 3) & 7)
            if all(kind in seen[side] for side, kind in enumerate(kinds)):
                continue
            frame = renderer.render(resting_input(seed=self.SEED), ms, 12, ambient=world)
            pixels = frame.partition(b"255\n")[2]
            for side, kind in enumerate(kinds):
                x0, x1 = self.AFTERMATH_BOX[side]
                rows = range(ground - 22, ground)
                box = b"".join(pixels[y * 256 + x0 : y * 256 + x1] for y in rows)
                seen[side].setdefault(kind, box)
        for side, boxes in seen.items():
            kinds = sorted(boxes)
            self.assertIn(0, boxes, f"side {side} has a moment with nothing to do")
            self.assertGreaterEqual(len(boxes), 5, f"side {side} kinds {kinds}")
            self.assertEqual(len(set(boxes.values())), len(boxes), f"side {side} kinds {kinds}")


class TownLifeView(ctypes.Structure):
    """``duel_town_life_view_t``: one resident, copied out of the opaque state."""

    _fields_ = [
        ("x", ctypes.c_int16),
        ("y", ctypes.c_int16),
        ("state", ctypes.c_uint8),
        ("place", ctypes.c_uint8),
        ("goal", ctypes.c_uint8),
        ("personality", ctypes.c_uint8),
        ("need_top", ctypes.c_uint8),
        ("need_level", ctypes.c_uint8),
        ("visible", ctypes.c_uint8),
        ("facing", ctypes.c_uint8),
    ]


@requires_library
class TownLifeTests(unittest.TestCase):
    """DC8: the town's residents have needs and go places for them.

    The determinism cases are DC-S2's, ported: the same seed gives the same
    chain of states, one call to a moment equals stepping into it, an irregular
    cadence equals a regular one, a backward seek re-derives, and init leaves
    no byte of the handle undefined. The module never reaches the panels, and
    a shell that passes no life draws today's town.
    """

    SEED = 0x5A
    TICK_MS = 300
    RESIDENTS = 12
    WALK, STAY = 1, 2

    def setUp(self) -> None:
        self.renderer = CityRenderer(scale=1, layout=Layout.TOWN)

    def city(self, floor: Floor = Floor.COMMONS, scene: Scene = Scene.DUEL, **civic) -> CityInput:
        state = SemanticState(scene, NotificationSummary(), CivicState(floor=floor, **civic))
        return city_input(state, seed=self.SEED)

    def stepped(self, life, city: CityInput, until_ms: int, every_ms: int = TICK_MS) -> None:
        for t in range(every_ms, until_ms + 1, every_ms):
            life.advance(city, t)

    def test_the_handle_is_caller_owned_and_sized_for_growth(self) -> None:
        life = self.renderer.life(self.SEED)
        self.assertEqual(ctypes.sizeof(life.handle._obj), 384)
        self.assertEqual(life.ticks, 0)

    def test_same_seed_same_chain(self) -> None:
        a, b = self.renderer.life(self.SEED), self.renderer.life(self.SEED)
        city = self.city()
        chain_a, chain_b = hashlib.sha256(), hashlib.sha256()
        for t in range(self.TICK_MS, 2000 * self.TICK_MS + 1, self.TICK_MS):
            a.advance(city, t)
            b.advance(city, t)
            chain_a.update(a.snapshot())
            chain_b.update(b.snapshot())
        self.assertEqual(chain_a.digest(), chain_b.digest())
        self.assertEqual((a.ticks, b.ticks), (2000, 2000))

    def test_seeds_give_different_worlds(self) -> None:
        worlds = set()
        for seed in range(16):
            life = self.renderer.life(seed)
            life.advance(self.city(), 600_000)
            worlds.add(life.snapshot())
        self.assertEqual(len(worlds), 16)

    def test_one_call_seek_equals_stepping(self) -> None:
        # Across the dawn-to-day boundary at 150 s and well into the day: the
        # sky is re-derived for every tick, so one call is not one input.
        city = self.city()
        for target in (60_000, 1_020_000, 3_600_000):
            once = self.renderer.life(self.SEED)
            once.advance(city, target)
            step = self.renderer.life(self.SEED)
            self.stepped(step, city, target)
            self.assertEqual(once.ticks, target // self.TICK_MS, target)
            self.assertEqual(once.snapshot(), step.snapshot(), target)

    def test_irregular_cadence_equals_regular(self) -> None:
        city = self.city()
        regular = self.renderer.life(self.SEED)
        irregular = self.renderer.life(self.SEED)
        stalls = random.Random(self.SEED)
        reference = {}
        for t in range(self.TICK_MS, 600_000 + 1, self.TICK_MS):
            regular.advance(city, t)
            reference[t] = regular.snapshot()
        t, shared = 0, 0
        while t < 600_000:
            t += 40 if stalls.random() < 0.95 else stalls.randrange(40, 2400, 40)
            t = min(t, 600_000)
            irregular.advance(city, t)
            if t % self.TICK_MS == 0:
                self.assertEqual(irregular.snapshot(), reference[t], t)
                shared += 1
        # 40 ms meets 300 ms every 600 ms, less what the stalls step over.
        self.assertGreater(shared, 300)

    def test_backward_seek_rederives_from_the_seed(self) -> None:
        city = self.city()
        back = self.renderer.life(self.SEED)
        back.advance(city, 3_600_000)
        back.advance(city, 600_000)
        fresh = self.renderer.life(self.SEED)
        fresh.advance(city, 600_000)
        self.assertEqual(back.snapshot(), fresh.snapshot())

    def test_init_over_dirty_storage_defines_every_byte(self) -> None:
        handles = []
        for fill in (0xAA, 0x55):
            life = self.renderer.life(self.SEED)
            ctypes.memset(life.handle, fill, 384)
            life.reset(self.SEED)
            handles.append(life.snapshot())
        self.assertEqual(handles[0], handles[1])

    def test_inputs_reach_it(self) -> None:
        def after(city: CityInput) -> bytes:
            life = self.renderer.life(self.SEED)
            life.advance(city, 600_000)
            return life.snapshot()

        commons = after(self.city())
        self.assertNotEqual(after(self.city(Floor.WORKSHOP)), commons)
        self.assertNotEqual(after(self.city(mode=Mode.QUIET)), commons)

    def views(self, life) -> list[TownLifeView]:
        library = self.renderer._library
        library.duel_town_life_resident.argtypes = [
            ctypes.c_void_p,
            ctypes.c_uint8,
            ctypes.POINTER(TownLifeView),
        ]
        library.duel_town_life_resident.restype = ctypes.c_bool
        out = []
        for i in range(self.RESIDENTS):
            view = TownLifeView()
            self.assertTrue(library.duel_town_life_resident(life.handle, i, ctypes.byref(view)))
            out.append(view)
        return out

    def test_residents_go_somewhere_and_come_back(self) -> None:
        # An hour of the working day: residents stay at places, walk between
        # them, and come back to where they were; nobody walks for ever, and
        # the night sends most of them indoors.
        city = self.city()
        life = self.renderer.life(self.SEED)
        stays: list[list[int]] = [[] for _ in range(self.RESIDENTS)]
        walking = [0] * self.RESIDENTS
        longest = 0
        out_by_day, out_by_night, day_ticks, night_ticks = 0, 0, 0, 0
        for t in range(self.TICK_MS, 3_600_000 + 1, self.TICK_MS):
            life.advance(city, t)
            phase = t % 1_800_000
            night = phase >= 1_500_000
            day = 150_000 <= phase < 1_350_000
            visible = 0
            for i, view in enumerate(self.views(life)):
                visible += view.visible
                if view.state == self.WALK:
                    walking[i] += 1
                    longest = max(longest, walking[i])
                else:
                    walking[i] = 0
                if view.state == self.STAY and (not stays[i] or stays[i][-1] != view.place):
                    stays[i].append(view.place)
            if night:
                out_by_night += visible
                night_ticks += 1
            elif day:
                out_by_day += visible
                day_ticks += 1
        round_trips = sum(
            1
            for places in stays
            if any(places[k] in places[: k - 1] for k in range(2, len(places)))
        )
        self.assertGreaterEqual(round_trips, self.RESIDENTS // 2, stays)
        self.assertLess(longest, 600)
        self.assertLess(out_by_night / night_ticks, out_by_day / day_ticks)

    def test_the_town_layers_draw_the_residents(self) -> None:
        for layout in (Layout.TOWN, Layout.LANDSCAPE):
            renderer = CityRenderer(scale=1, layout=layout)
            life = renderer.life(self.SEED)
            city = resting_input(seed=self.SEED)
            life.advance(city, 400_000)
            plain = renderer.render(city, 400_000, 12)
            lived = renderer.render(city, 400_000, 12, life=life)
            self.assertNotEqual(lived, plain, layout)
            # Only the square moves: everything above the ground line is the
            # same town.
            ground = (renderer.height - 48) * renderer.width
            header = len(plain) - renderer.width * renderer.height
            self.assertEqual(lived[: header + ground], plain[: header + ground], layout)

    def test_the_panels_never_draw_it(self) -> None:
        for layout in (Layout.DESK, Layout.CITY, Layout.LEFT, Layout.RIGHT):
            renderer = CityRenderer(scale=1, layout=layout)
            plain_world, lived_world = renderer.ambient(self.SEED), renderer.ambient(self.SEED)
            life = renderer.life(self.SEED)
            city = renderer.tour_stop(1, self.SEED)
            for frame in range(0, 300, 7):
                now = frame * 40
                plain_world.advance(now)
                lived_world.advance(now)
                life.advance(city, now, ambient=lived_world)
                self.assertEqual(
                    renderer.render(city, now, frame, ambient=lived_world, life=life),
                    renderer.render(city, now, frame, ambient=plain_world),
                    f"{layout} frame {frame}",
                )

    def test_no_life_is_todays_town(self) -> None:
        for layout in (Layout.TOWN, Layout.LANDSCAPE):
            renderer = CityRenderer(scale=1, layout=layout)
            city = resting_input(seed=self.SEED)
            self.assertEqual(
                renderer.render(city, 400_000, 12, life=None),
                renderer.render(city, 400_000, 12),
            )


class WindowLayoutTests(unittest.TestCase):
    def test_a_fixed_size_centres_the_city(self) -> None:
        window = city_window.CityWindow(
            renderer=FakeRenderer(), tk=FakeTk, size=(256, 256), clock=lambda: 0.0
        )
        self.assertEqual(window.root.requested_geometry, "256x256")
        self.assertEqual(window.label.placed, {"relx": 0.5, "rely": 0.5, "anchor": "center"})
        self.assertIsNone(window.label.packed)

    def test_without_a_size_the_window_hugs_the_image(self) -> None:
        window = city_window.CityWindow(renderer=FakeRenderer(), tk=FakeTk, clock=lambda: 0.0)
        self.assertEqual(window.label.packed, {"padx": 12, "pady": 12})
        self.assertIsNone(window.label.placed)

    def test_the_window_paints_the_backdrop_the_renderer_names(self) -> None:
        window = city_window.CityWindow(renderer=FakeRenderer(), tk=FakeTk, clock=lambda: 0.0)
        self.assertEqual(window.label.kwargs["background"], "#123456")


class AmbientWindowTests(unittest.TestCase):
    def test_the_world_is_advanced_before_the_frame_is_drawn(self) -> None:
        window = build_window(duels=True)
        window.draw_state(SemanticState())
        window.draw_state(SemanticState())
        # Advanced to each frame's own clock, and handed to the renderer.
        self.assertEqual(window.ambient.advanced, [0, 40])
        self.assertEqual([call[3] for call in window.renderer.calls], [window.ambient] * 2)

    def test_the_world_is_seeded_from_the_window_seed(self) -> None:
        window = build_window(duels=True)
        self.assertEqual(window.renderer.worlds[0][0], 0x5A)

    def test_no_duels_leaves_the_champions_still(self) -> None:
        window = build_window(duels=False)
        self.assertIsNone(window.ambient)
        window.draw_state(SemanticState())
        self.assertEqual(window.renderer.calls[0][3], None)

    def test_duels_are_on_by_default_and_can_be_turned_off(self) -> None:
        self.assertTrue(city_window.parse_args([]).duels)
        self.assertFalse(city_window.parse_args(["--no-duels"]).duels)

    def test_the_residents_live_beside_the_world_by_default(self) -> None:
        # DC8: the window always has the town's residents, seeded like the
        # world, advanced at each frame's clock with that frame's input and
        # world, and handed to the renderer -- with or without a duel.
        for duels in (True, False):
            window = build_window(duels=duels)
            window.draw_state(SemanticState())
            window.draw_state(SemanticState())
            seed, life = window.renderer.lives[0]
            self.assertEqual(seed, 0x5A)
            self.assertIs(window.life, life)
            self.assertEqual([call[1] for call in life.advanced], [0, 40])
            self.assertEqual([call[2] for call in life.advanced], [window.ambient] * 2)
            self.assertEqual(window.renderer.drawn_lives, [life] * 2)


@requires_library
class AmbientWorldTests(unittest.TestCase):
    """The self-playing world: real spell chains from invented input."""

    def run_world(self, seed: int, seconds: int = 60):
        renderer = CityRenderer(scale=1, layout=Layout.CITY)
        world = renderer.ambient(seed)
        for frame in range(seconds * 25):
            world.advance(frame * 40)
        return renderer, world

    def test_the_city_duels_by_itself(self) -> None:
        _, world = self.run_world(0x5A)
        stats = world.stats
        self.assertEqual(stats.ticks, 1500)
        # A minute of city produces a real exchange, not one twitch.
        self.assertGreaterEqual(stats.casts, 5)
        self.assertGreaterEqual(stats.impacts, 1)

    def test_it_never_runs_down(self) -> None:
        # Knockdowns resolve through the lifecycle and a replacement walks in,
        # so a world left running does not reach a state it cannot leave.
        _, world = self.run_world(0x5A, seconds=300)
        stats = world.stats
        self.assertGreater(stats.knockdowns, 0)
        self.assertGreater(stats.casts, 50)

    def test_a_seed_replays_exactly(self) -> None:
        def history(seed):
            _, world = self.run_world(seed, seconds=20)
            stats = world.stats
            return (stats.ticks, stats.casts, stats.impacts, stats.knockdowns)

        self.assertEqual(history(0x5A), history(0x5A))
        self.assertNotEqual(history(0x5A), history(0x11))

    def test_ambient_play_moves_the_diplomacy_balance(self) -> None:
        # The keyboard weights rare events by a session balance that tips
        # whenever a champion falls. The desktop passed a constant zero, so
        # its city never leaned. The balance lives behind the ambient handle on
        # the seam the renderer reads (desktop/duel_ambient.h), not on the city
        # ABI, so this reads it from the library the same way.
        renderer = CityRenderer(scale=1, layout=Layout.CITY)
        balance = renderer._library.duel_ambient_diplomacy_balance
        balance.argtypes = [ctypes.POINTER(AmbientState)]
        balance.restype = ctypes.c_int8
        world = renderer.ambient(0x5A)
        self.assertEqual(balance(world.handle), 0)
        seen = {0}
        for frame in range(300 * 25):
            world.advance(frame * 40)
            value = balance(world.handle)
            # Each point of lean takes one fall, and the lean saturates at 3.
            self.assertLessEqual(abs(value), min(3, world.stats.knockdowns))
            seen.add(value)
        self.assertGreater(world.stats.knockdowns, 0)
        self.assertNotEqual(seen, {0})

    def test_the_clock_paces_the_world_not_the_redraw(self) -> None:
        renderer = CityRenderer(scale=1)
        dense, sparse = renderer.ambient(0x5A), renderer.ambient(0x5A)
        for elapsed_ms in range(0, 10_001, 40):
            dense.advance(elapsed_ms)
        for elapsed_ms in range(0, 10_001, 80):
            sparse.advance(elapsed_ms)
        self.assertEqual(dense.stats.ticks, sparse.stats.ticks)
        # Ten seconds of world, whatever the redraw rate.
        self.assertEqual(dense.stats.ticks, 251)


class ArgumentTests(unittest.TestCase):
    def test_daemon_arguments_are_refused(self) -> None:
        # The window runs no daemon, so there is nothing to hand them to.
        self.assertEqual(city_window.parse_args(["--scale", "6"]).scale, 6)
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
            city_window.parse_args(["--verbose", "--device", "/dev/x"])

    def test_out_of_range_window_options_are_refused(self) -> None:
        for argv in (
            ["--scale", "0"],
            ["--scale", "17"],
            ["--fps", "0"],
            ["--fps", "61"],
            ["--layout", "elsewhere"],
        ):
            with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
                city_window.parse_args(argv)


# Follows the service for a second the way the window does, then prints its line.
FOLLOWER = """
import time
import gi
gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib
from arcane_host.city_window import ServiceView
view = ServiceView(Gio, GLib, Gio.bus_get_sync(Gio.BusType.SESSION, None))
deadline = time.monotonic() + 1.0
while time.monotonic() < deadline:
    view.pump()
    time.sleep(0.02)
print(view.caption())
"""


class CaptionTests(unittest.TestCase):
    def test_each_link_state_has_words(self) -> None:
        caption = city_window.link_caption
        self.assertEqual(caption(None), city_window.NO_SERVICE)
        self.assertEqual(caption(("connected", "/dev/hidraw3", False, "")), "Keyboard connected")
        self.assertEqual(caption(("absent", "", False, "")), "No keyboard found")
        self.assertEqual(
            caption(("paused", "", True, "Vial (pid 9)")), "Keyboard lent to Vial (pid 9)"
        )
        for link in ("starting", "connected", "absent", "denied", "several", "failed"):
            self.assertNotIn(":", caption((link, "", False, "")), link)

    def test_the_caption_line_is_only_there_when_asked_for(self) -> None:
        self.assertIsNone(build_window().caption)
        window = build_window(caption=True)
        self.assertEqual(window.caption.packed, {"padx": 12, "pady": (0, 8)})
        window.set_caption("Keyboard connected")
        self.assertEqual(window.caption.cget("text"), "Keyboard connected")
        build_window().set_caption("ignored")


class NoCorneTests(unittest.TestCase):
    """corne-arcane --no-hid: a keyboard without this firmware, so no Corne is expected."""

    def test_no_hid_follows_the_daemons_option_and_setting(self) -> None:
        with mock.patch.dict(os.environ, {"CORNE_ARCANE_NO_HID": ""}):
            self.assertFalse(city_window.parse_args([]).no_hid)
            self.assertTrue(city_window.parse_args(["--no-hid"]).no_hid)
        with mock.patch.dict(os.environ, {"CORNE_ARCANE_NO_HID": "1"}):
            self.assertTrue(city_window.parse_args([]).no_hid)
        with mock.patch.dict(os.environ, {"CORNE_ARCANE_NO_HID": "0"}):
            self.assertFalse(city_window.parse_args([]).no_hid)

    def test_an_absent_keyboard_is_not_an_error_without_a_corne(self) -> None:
        caption = city_window.service_caption
        absent = ("absent", "", False, "")
        self.assertEqual(caption(absent, no_hid=False), "No keyboard found")
        self.assertEqual(caption(absent, no_hid=True), city_window.NO_CORNE)
        self.assertEqual(caption(None, no_hid=True), city_window.NO_SERVICE)
        for words in (city_window.NO_CORNE, city_window.NO_SERVICE):
            self.assertNotRegex(words.lower(), "hid|error|fail|denied")

    def test_no_corne_shows_none_of_the_corne_controls(self) -> None:
        window = build_window(caption=True)
        view = FakeView(("absent", "", False, ""))
        with mock.patch.object(city_window, "ControlsPanel") as panel:
            window.schedule = lambda _delay, _callback: None
            window.root.mainloop = lambda: None
            city_window.follow_service(window, view, controls=mock.Mock(), no_hid=True)
        panel.assert_not_called()

    def test_the_first_redraw_says_the_city_follows_the_desktop(self) -> None:
        window = build_window(caption=True)
        view = FakeView(("absent", "", False, ""))
        steps = []
        window.schedule = lambda _delay, callback: steps.append(callback)
        window.root.mainloop = lambda: steps.pop(0)()
        city_window.follow_service(window, view, controls=mock.Mock(), no_hid=True)
        self.assertEqual(window.caption.cget("text"), city_window.NO_CORNE)
        self.assertEqual(window.frames, 1)


class FakeView:
    def __init__(self, status) -> None:
        self.status = status
        self.world = None

    def pump(self) -> None:
        pass

    def caption(self) -> str:
        return city_window.link_caption(self.status)

    def city(self, seed: int) -> CityInput:
        return resting_input(online=self.status is not None, seed=seed)


class FakeVariant:
    def __init__(self, values, signature: str = TYPING_SIGNATURE) -> None:
        self.values = values
        self.signature = signature

    def get_type_string(self) -> str:
        return self.signature

    def unpack(self):
        return self.values


def typing_view(clock=lambda: 0.0) -> city_window.TypingView:
    """A TypingView on no bus: summaries are handed to it as the signal would."""
    return city_window.TypingView(mock.Mock(), mock.Mock(), mock.Mock(), clock=clock)


def hear(view: city_window.TypingView, values, signature: str = TYPING_SIGNATURE) -> None:
    view._summary(None, ":1.7", None, None, None, FakeVariant(values, signature))


def typing_fields(packed: CityInput) -> tuple[int, int, int, int]:
    return (packed.tempo, packed.spread, packed.row, packed.row_spread)


class TypingViewTests(unittest.TestCase):
    """The desktop city's side of the opt-in typing summary (NF7)."""

    def test_each_summary_value_is_its_city_value(self) -> None:
        # The city enums are the summary's plus one, zero being none.
        view = typing_view()
        for tempo in Tempo:
            for spread in Spread:
                for row in Row:
                    for row_spread in RowSpread:
                        hear(view, (tempo, spread, row, row_spread))
                        packed = view.apply(resting_input(seed=0x5A))
                        self.assertEqual(
                            typing_fields(packed), (tempo + 1, spread + 1, row + 1, row_spread + 1)
                        )

    def test_no_summary_leaves_the_fields_none(self) -> None:
        packed = typing_view().apply(resting_input(seed=0x5A))
        self.assertEqual(bytes(packed), bytes(resting_input(seed=0x5A)))

    def test_only_the_typing_fields_change(self) -> None:
        state = SemanticState(
            Scene.ARCHIVE,
            NotificationSummary(3, Category.COMMUNICATION, Priority.CRITICAL, 5, True),
            CivicState(Floor.WORKSHOP, Mode.QUIET, Intensity.BUSY, Secondary.TRANSFER),
            7,
        )
        before = city_input(state, seed=0x5A)
        view = typing_view()
        hear(view, (Tempo.FRANTIC, Spread.IRREGULAR, Row.TOP, RowSpread.FOCUSED))
        after = view.apply(city_input(state, seed=0x5A))
        # Civic intensity stays host workload; health fields stay none.
        self.assertEqual(bytes(after)[:10], bytes(before)[:10])
        self.assertEqual(typing_fields(after), (4, 3, 1, 1))
        self.assertEqual((after.body, after.heart, after.sleep), (0, 0, 0))

    def test_a_silent_helper_fades_back_to_none(self) -> None:
        now = [100.0]
        view = typing_view(clock=lambda: now[0])
        hear(view, (Tempo.RAPID, Spread.STEADY, Row.HOME, RowSpread.MIXED))
        # One dropped window between two kept ones is not silence.
        now[0] += 2 * 60.0
        self.assertEqual(typing_fields(view.apply(resting_input())), (3, 1, 2, 2))
        now[0] = 100.0 + city_window.TYPING_STALE_SECONDS
        self.assertEqual(typing_fields(view.apply(resting_input())), (0, 0, 0, 0))
        hear(view, (Tempo.FLOWING, Spread.VARIED, Row.BOTTOM, RowSpread.EVEN))
        self.assertEqual(typing_fields(view.apply(resting_input())), (2, 2, 3, 3))

    def test_the_stale_window_is_two_to_three_minutes(self) -> None:
        self.assertGreaterEqual(city_window.TYPING_STALE_SECONDS, 120.0)
        self.assertLessEqual(city_window.TYPING_STALE_SECONDS, 180.0)

    def test_turning_the_helper_off_drops_the_fields_at_once(self) -> None:
        view = typing_view()
        hear(view, (Tempo.RAPID, Spread.STEADY, Row.HOME, RowSpread.MIXED))
        view._vanished(None, "io.github.Griffinhale.CorneArcane.Typing")
        self.assertEqual(typing_fields(view.apply(resting_input())), (0, 0, 0, 0))

    def test_a_malformed_summary_is_ignored(self) -> None:
        view = typing_view()
        hear(view, (Tempo.RAPID, Spread.STEADY, Row.HOME, RowSpread.MIXED))
        hear(view, (4, 0, 0, 0))
        hear(view, (0, 0, 0, 0, 0), "(yyyyy)")
        hear(view, ("x",), "(s)")
        self.assertEqual(typing_fields(view.apply(resting_input())), (3, 1, 2, 2))

    def test_following_the_service_draws_the_typing_fields(self) -> None:
        window = build_window(caption=True)
        view = FakeView(("absent", "", False, ""))
        typing = typing_view()
        hear(typing, (Tempo.FRANTIC, Spread.IRREGULAR, Row.TOP, RowSpread.FOCUSED))
        steps = []
        window.schedule = lambda _delay, callback: steps.append(callback)
        window.root.mainloop = lambda: steps.pop(0)()
        city_window.follow_service(window, view, controls=mock.Mock(), no_hid=True, typing=typing)
        drawn = CityInput.from_buffer_copy(window.renderer.inputs[-1])
        self.assertEqual(typing_fields(drawn), (4, 3, 1, 1))

    def test_without_a_typing_view_the_fields_stay_none(self) -> None:
        window = build_window(caption=True)
        steps = []
        window.schedule = lambda _delay, callback: steps.append(callback)
        window.root.mainloop = lambda: steps.pop(0)()
        city_window.follow_service(
            window, FakeView(("absent", "", False, "")), controls=mock.Mock(), no_hid=True
        )
        drawn = CityInput.from_buffer_copy(window.renderer.inputs[-1])
        self.assertEqual(typing_fields(drawn), (0, 0, 0, 0))


class RecordingKeyboard(EchoKeyboard):
    """An echoing pty that also keeps every frame the heartbeat wrote to it."""

    def __init__(self) -> None:
        self.frames: list[bytes] = []
        self.lock = threading.Lock()
        super().__init__()

    def _echo(self) -> None:
        pending = b""
        while not self._stop.is_set():
            readable, _, _ = select.select((self.master,), (), (), 0.05)
            if not readable:
                continue
            try:
                pending += os.read(self.master, 4096)
            except OSError:
                return
            while len(pending) >= FRAME:
                frame, pending = pending[:FRAME], pending[FRAME:]
                with self.lock:
                    self.frames.append(frame)
                os.write(self.master, frame)

    def heartbeats(self) -> list[bytes]:
        with self.lock:
            return [frame for frame in self.frames if frame[1 + 3] == Message.HEARTBEAT]


def without_counters(frame: bytes) -> bytes:
    """A heartbeat with its session, sequence and CRC cut out: what it says, not when."""
    report = frame[1:]
    return frame[:1] + report[:4] + report[10 : REPORT_SIZE - 1]


class NoPrivateDaemonTests(unittest.TestCase):
    def test_no_private_daemon(self) -> None:
        """The window module reaches neither the daemon nor the keyboard."""
        tree = ast.parse(Path(city_window.__file__).read_text())
        imported = {
            node.module or "" for node in ast.walk(tree) if isinstance(node, ast.ImportFrom)
        } | {
            alias.name
            for node in ast.walk(tree)
            if isinstance(node, ast.Import)
            for alias in node.names
        }
        for module in ("daemon", "heartbeat", "hidraw", "runtime", "hid_ownership"):
            self.assertNotIn(module, imported)
        self.assertFalse(hasattr(city_window, "RuntimePresenter"))


@unittest.skipUnless(
    Gio is not None and shutil.which("dbus-daemon"), "needs PyGObject and dbus-daemon"
)
class ServiceViewTests(unittest.TestCase):
    """The window's client against a real daemon on a private bus; the keyboard is a pty."""

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
        self.keyboard = EchoKeyboard()
        self.addCleanup(self.keyboard.close)
        runtime = tempfile.TemporaryDirectory()
        self.addCleanup(runtime.cleanup)
        self.env = dict(
            os.environ,
            DBUS_SESSION_BUS_ADDRESS=self.address,
            DBUS_SYSTEM_BUS_ADDRESS=self.address,
            XDG_RUNTIME_DIR=runtime.name,
            CORNE_ARCANE_SYSTEMCTL="false",
            PYTHONDONTWRITEBYTECODE="1",
        )
        self.view = city_window.ServiceView(Gio, GLib, self.connect())
        self.addCleanup(self.view.close)
        self.daemon = None

    def connect(self):
        connection = Gio.DBusConnection.new_for_address_sync(
            self.address,
            Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT
            | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,
            None,
            None,
        )
        self.addCleanup(lambda: connection.is_closed() or connection.close_sync(None))
        return connection

    def start_daemon(self) -> None:
        self.daemon = subprocess.Popen(
            [
                sys.executable,
                "-m",
                "arcane_host.daemon",
                "--device",
                self.keyboard.path,
                "--no-desktop-notifications",
            ],
            cwd=HOST_DIR,
            env=self.env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        self.addCleanup(self.stop_daemon)

    def stop_daemon(self) -> None:
        if self.daemon is None or self.daemon.poll() is not None:
            return
        self.daemon.terminate()
        try:
            self.daemon.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.daemon.kill()
            self.daemon.wait()

    def settle(self, condition, timeout: float = 10.0) -> bool:
        def pumped() -> bool:
            self.view.pump()
            return condition()

        return wait_until(pumped, timeout)

    def test_link_state_shown(self) -> None:
        self.view.pump()
        self.assertEqual(self.view.caption(), city_window.NO_SERVICE)
        self.assertEqual(self.view.city(0x5A).online, 0)

        self.start_daemon()
        self.assertTrue(
            self.settle(
                lambda: self.view.status is not None and self.view.status[0] == "connected"
            ),
            f"never saw the link: {self.view.status}",
        )
        self.assertTrue(self.settle(lambda: self.view.world is not None, 2.0))
        self.assertEqual(self.view.caption(), "Keyboard connected")
        self.assertEqual((self.view.city(0x5A).online, self.view.city(0x5A).seed), (1, 0x5A))

        # Another client borrows the keyboard: the line says who has it.
        borrower = self.connect()
        borrower.call_sync(
            BUS_NAME,
            OBJECT_PATH,
            CONTROL_INTERFACE,
            PAUSE,
            GLib.Variant("(s)", ("Vial (pid 1)",)),
            None,
            Gio.DBusCallFlags.NO_AUTO_START,
            1000,
            None,
        )
        self.assertTrue(
            self.settle(lambda: self.view.caption() == "Keyboard lent to Vial (pid 1)", 3.0),
            self.view.caption(),
        )

        self.stop_daemon()
        self.assertTrue(self.settle(lambda: self.view.status is None, 5.0))
        self.assertEqual(self.view.caption(), city_window.NO_SERVICE)
        self.assertEqual(self.view.city(0x5A).online, 0)

    def test_following_the_service_starts_no_second_heartbeat(self) -> None:
        self.start_daemon()
        self.assertTrue(
            self.settle(lambda: self.view.status is not None and self.view.status[0] == "connected")
        )
        # The window's client, in a process of its own, following the service.
        client = subprocess.run(
            [sys.executable, "-c", FOLLOWER],
            cwd=HOST_DIR,
            env=self.env,
            capture_output=True,
            text=True,
            timeout=30,
        )
        self.assertEqual(client.stdout.strip(), "Keyboard connected", client.stderr)
        # Outside this test (which holds both ends of the pty), only the
        # service has the keyboard open, and it still owns the bus name.
        holders = {
            int(entry)
            for entry in os.listdir("/proc")
            if entry.isdigit()
            and int(entry) != os.getpid()
            and holds(int(entry), self.keyboard.path)
        }
        self.assertEqual(holders, {self.daemon.pid})
        bus = self.connect()

        def ask(method: str, argument: str, reply: str):
            return bus.call_sync(
                "org.freedesktop.DBus",
                "/org/freedesktop/DBus",
                "org.freedesktop.DBus",
                method,
                GLib.Variant("(s)", (argument,)),
                GLib.VariantType(reply),
                Gio.DBusCallFlags.NONE,
                1000,
                None,
            ).unpack()[0]

        owner = ask("GetNameOwner", BUS_NAME, "(s)")
        self.assertEqual(ask("GetConnectionUnixProcessID", owner, "(u)"), self.daemon.pid)

    def test_typing_summary_sets_typing_fields(self) -> None:
        """The helper's signal, over a real bus, fills the window's typing fields."""
        typing = city_window.TypingView(Gio, GLib, self.connect())
        self.addCleanup(typing.close)
        publisher = typing_helper.Publisher(self.connect())
        publisher.own()
        self.assertTrue(self.settle(lambda: typing.present, 3.0), "never saw the helper")
        publisher.send(TypingSummary(Tempo.FRANTIC, Spread.IRREGULAR, Row.TOP, RowSpread.FOCUSED))
        self.assertTrue(self.settle(lambda: typing.summary is not None, 3.0), "no summary")
        self.assertEqual(typing_fields(typing.apply(self.view.city(0x5A))), (4, 3, 1, 1))

    def test_a_summary_from_another_name_is_not_heard(self) -> None:
        typing = city_window.TypingView(Gio, GLib, self.connect())
        self.addCleanup(typing.close)
        impostor = self.connect()
        impostor.emit_signal(
            None,
            city_window.TYPING_OBJECT_PATH,
            city_window.TYPING_INTERFACE,
            city_window.TYPING_SUMMARY,
            GLib.Variant(TYPING_SIGNATURE, (3, 2, 0, 0)),
        )
        impostor.flush_sync(None)
        self.settle(lambda: False, 0.5)
        self.assertIsNone(typing.summary)

    def test_typing_summary_leaves_hid_packets_unchanged(self) -> None:
        """The keyboard is sent the same heartbeat with and without a summary on the bus."""
        self.keyboard = RecordingKeyboard()
        self.addCleanup(self.keyboard.close)
        typing = city_window.TypingView(Gio, GLib, self.connect())
        self.addCleanup(typing.close)
        self.start_daemon()
        self.assertTrue(
            self.settle(lambda: self.view.status is not None and self.view.status[0] == "connected")
        )
        self.assertTrue(self.settle(lambda: len(self.keyboard.heartbeats()) >= 3, 5.0))
        without = self.keyboard.heartbeats()

        publisher = typing_helper.Publisher(self.connect())
        publisher.own()
        publisher.send(TypingSummary(Tempo.FRANTIC, Spread.IRREGULAR, Row.TOP, RowSpread.FOCUSED))
        self.assertTrue(self.settle(lambda: typing.summary is not None, 3.0), "no summary")
        self.assertEqual(typing_fields(typing.apply(self.view.city(0x5A))), (4, 3, 1, 1))
        self.assertTrue(
            self.settle(lambda: len(self.keyboard.heartbeats()) >= len(without) + 3, 5.0)
        )
        with_summary = self.keyboard.heartbeats()[len(without) :]

        for frame in without + with_summary:
            self.assertEqual(len(frame), FRAME)
        self.assertEqual(
            {without_counters(frame) for frame in with_summary},
            {without_counters(frame) for frame in without},
        )
        self.assertEqual(len({without_counters(frame) for frame in without}), 1)

    def test_no_corne_renders_city_only(self) -> None:
        """A --no-hid service: the city is online, follows focus and notifications, and no HID error shows."""
        log = tempfile.TemporaryFile()
        self.addCleanup(log.close)
        self.daemon = subprocess.Popen(
            [sys.executable, "-m", "arcane_host.daemon", "--no-hid", "--no-desktop-notifications"],
            cwd=HOST_DIR,
            env=self.env,
            stdout=subprocess.DEVNULL,
            stderr=log,
        )
        self.addCleanup(self.stop_daemon)
        self.assertTrue(
            self.settle(lambda: self.view.status is not None and self.view.world is not None),
            f"never saw the service: {self.view.status}",
        )
        self.assertEqual(self.view.status[0], "absent")
        self.assertEqual(
            city_window.service_caption(self.view.status, no_hid=True), city_window.NO_CORNE
        )
        resting = self.view.city(0x5A)
        self.assertEqual((resting.online, resting.seed), (1, 0x5A))

        client = self.connect()

        def report(interface: str, method: str, value) -> None:
            client.call_sync(
                BUS_NAME,
                OBJECT_PATH,
                interface,
                method,
                value,
                None,
                Gio.DBusCallFlags.NO_AUTO_START,
                1000,
                None,
            )

        before = self.view.world
        report(
            EVENTS_INTERFACE,
            INJECT_SYNTHETIC,
            GLib.Variant("(yyb)", (int(Category.COMMUNICATION), int(Priority.NORMAL), False)),
        )
        self.assertTrue(self.settle(lambda: self.view.world != before, 3.0), "no notification")
        self.assertEqual(self.view.world[1:3], (1, int(Category.COMMUNICATION)))

        before = self.view.world
        report(FOCUS_INTERFACE, REPORT_ACTIVE_WINDOW, GLib.Variant("(ss)", ("code", "code")))
        self.assertTrue(self.settle(lambda: self.view.world != before, 3.0), "focus not followed")

        city_now = self.view.city(0x5A)
        self.assertEqual(city_now.online, 1)
        if library_available():
            frame = CityRenderer().render(city_now, 400_000, 0)
            self.assertTrue(frame.startswith(b"P5"))

        self.stop_daemon()
        log.seek(0)
        errors = log.read().decode("utf-8", "replace")
        self.assertIn("running without a keyboard", errors)
        self.assertNotIn("HID unavailable", errors)


if __name__ == "__main__":
    unittest.main()
