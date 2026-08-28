from __future__ import annotations

import argparse
import contextlib
import ctypes
import io
import os
import unittest
from pathlib import Path
from unittest import mock

from arcane_host import city, city_window
from arcane_host.city import (
    CITY_ABI,
    CityError,
    CityInput,
    CityRenderer,
    Layout,
    candidate_paths,
    city_input,
    resting_input,
)
from arcane_host.protocol import (
    Category,
    CivicState,
    Floor,
    Intensity,
    Mode,
    NotificationSummary,
    Priority,
    Scene,
    Secondary,
)
from arcane_host.semantic import SemanticState


def library_available() -> bool:
    return any(path.is_file() for path in candidate_paths())


requires_library = unittest.skipUnless(
    library_available(), "libcornearcane.so is not built; run `make city-lib`"
)


class CityInputTests(unittest.TestCase):
    def test_struct_is_ten_bounded_bytes(self) -> None:
        # The privacy boundary is structural: every field is a small integer
        # and there is nowhere a title, URL, or notification body could ride.
        self.assertEqual(ctypes.sizeof(CityInput), 10)
        self.assertTrue(all(kind is ctypes.c_uint8 for _, kind in CityInput._fields_))

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


class FakeRenderer:
    def __init__(self, layout: Layout = Layout.CITY) -> None:
        self.layout = layout
        self.calls = []
        self.worlds = []
        self.backdrop = "#123456"
        self.fps = 25

    def tour_stop(self, index, seed=0) -> CityInput:
        return CityInput(scene=index % 4, seed=seed)

    def ambient(self, seed) -> FakeAmbient:
        world = FakeAmbient()
        self.worlds.append((seed, world))
        return world

    def render(self, packed, elapsed_ms, frame, ambient=None) -> bytes:
        self.calls.append((packed.civic, elapsed_ms, frame, ambient))
        return b"pixels"


class FakeGLib:
    def __init__(self) -> None:
        self.added = []
        self.removed = []
        self.next_id = 1

    def timeout_add(self, delay, callback) -> int:
        self.added.append((delay, callback))
        self.next_id += 1
        return self.next_id - 1

    def source_remove(self, source_id) -> None:
        self.removed.append(source_id)


class FakeLoop:
    def __init__(self) -> None:
        self.quits = 0

    def quit(self) -> None:
        self.quits += 1


class FakeRuntime:
    def __init__(self) -> None:
        self.loop = FakeLoop()


class FakeResolver:
    def __init__(self) -> None:
        self.state = SemanticState()


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


class RuntimePresenterTests(unittest.TestCase):
    def presenter(self, fps=25):
        glib, runtime, resolver = FakeGLib(), FakeRuntime(), FakeResolver()
        window = build_window()
        return city_window.RuntimePresenter(glib, runtime, resolver, window, fps=fps), glib

    def test_redraw_timer_matches_the_requested_cadence(self) -> None:
        presenter, glib = self.presenter(fps=25)
        self.assertEqual(glib.added[0][0], 40)
        self.assertEqual(glib.added[0][1], presenter.tick)

    def test_tick_draws_and_keeps_the_timer(self) -> None:
        presenter, _ = self.presenter()
        self.assertTrue(presenter.tick())
        self.assertEqual(presenter.window.frames, 1)

    def test_tick_stops_once_the_window_is_gone(self) -> None:
        presenter, _ = self.presenter()
        presenter.window.close()
        self.assertFalse(presenter.tick())

    def test_closing_the_window_stops_the_daemon_loop(self) -> None:
        presenter, _ = self.presenter()
        presenter.window.root.close_callback()
        self.assertTrue(presenter.window.closed)
        self.assertEqual(presenter.runtime.loop.quits, 1)

    def test_close_releases_the_timer(self) -> None:
        presenter, glib = self.presenter()
        source_id = glib.added and 1
        presenter.close()
        self.assertEqual(glib.removed, [source_id])
        self.assertTrue(presenter.window.closed)


@requires_library
class PresentationPolicyTests(unittest.TestCase):
    """Decisions a second shell would otherwise repeat, kept in the renderer."""

    def test_the_default_scale_is_the_renderers_decision(self) -> None:
        self.assertEqual(CityRenderer(layout=Layout.CITY).scale, 4)
        self.assertEqual(CityRenderer(layout=Layout.TOWN).scale, 2)

    def test_fit_scale_never_renders_a_fraction_of_a_pixel(self) -> None:
        renderer = CityRenderer(layout=Layout.TOWN)
        self.assertEqual(renderer.fit_scale(512, 512), 2)
        self.assertEqual(renderer.fit_scale(1024, 600), 2)
        self.assertEqual(renderer.fit_scale(10, 10), 1)

    def test_the_backdrop_is_the_renderers_decision(self) -> None:
        # The desk continues around the panels; every other layout sits on the
        # same unlit ground its own gap is drawn in.
        self.assertEqual(CityRenderer(layout=Layout.DESK).backdrop, "#303030")
        for layout in (Layout.CITY, Layout.LEFT, Layout.RIGHT, Layout.TOWN):
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

    def test_the_default_layout_is_one_continuous_scene(self) -> None:
        args, _ = city_window.parse_args([])
        self.assertEqual(args.layout, "city")


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
        self.assertTrue(city_window.parse_args([])[0].duels)
        self.assertFalse(city_window.parse_args(["--no-duels"])[0].duels)


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
    def test_unknown_arguments_are_left_for_the_daemon(self) -> None:
        args, rest = city_window.parse_args(["--scale", "6", "--verbose", "--device", "/dev/x"])
        self.assertEqual(args.scale, 6)
        self.assertEqual(rest, ["--verbose", "--device", "/dev/x"])

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


class DaemonPresenterTests(unittest.TestCase):
    def test_dry_run_refuses_a_presenter_instead_of_ignoring_it(self) -> None:
        from arcane_host import daemon

        args = daemon.parse_args(["--dry-run", "--once"])
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            self.assertEqual(daemon.run(args, presenter_factory=lambda *_: None), 2)
        self.assertIn("no main loop", stderr.getvalue())


if __name__ == "__main__":
    unittest.main()
