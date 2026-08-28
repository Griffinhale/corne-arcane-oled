"""Native city renderer binding: bounded enums in, pixels out.

The renderer itself lives in ``desktop/``, compiled natively over the same
simulation the firmware runs. This module is the whole of the Python side: the
shell holds no presentation policy of its own, and every decision the window
would otherwise make -- scale, backdrop, cadence, the tour -- is asked of the
renderer, so a second shell on another platform repeats none of it. It packs a
:class:`~arcane_host.semantic.SemanticState` into the ten-byte input struct
``duel_city.h`` declares and hands back both canvases as one grey image.

Only integer enums cross the boundary, which is the same contract the Raw HID
payload keeps. There is no field for a window title, a URL, or notification
text, and nothing here reads the keyboard: the desktop shows the city, never
the duel.
"""

from __future__ import annotations

import ctypes
import os
from enum import IntEnum
from pathlib import Path

from .protocol import CivicState, NotificationSummary, Scene
from .semantic import SemanticState

CITY_ABI = 7
LIBRARY_NAME = "libcornearcane.so"


class Layout(IntEnum):
    """What the window is a window onto; ``DUEL_CITY_LAYOUT_*``.

    DESK is the keyboard: two panels with the desk showing between them. CITY
    is the same world with that space unlit, so it reads as one scene rather
    than two screens. LEFT and RIGHT are a single tower. Those four are the
    same 32x128 pixels, reframed.

    TOWN is a second drawing layer on a 256x256 square; LANDSCAPE is its
    400x240 wide counterpart. Both put one wizard tower at the centre of a
    small city and share the world, not the pixels.
    """

    DESK = 0
    CITY = 1
    LEFT = 2
    RIGHT = 3
    TOWN = 4
    LANDSCAPE = 5


# duel_city.h error codes, in the words its callers need.
_ERRORS = {
    -1: "null pointer passed to the renderer",
    -2: "scale outside 1..16",
    -3: "pixel buffer shorter than the geometry",
    -4: "input field outside its enum or bit width",
    -5: "layout outside desk/city/left/right/town/landscape",
}


class CityError(RuntimeError):
    """The native renderer is missing, too old, or refused an input."""


class CityInput(ctypes.Structure):
    """``duel_city_input_t``: the Raw HID v3 semantic payload, unpacked."""

    _fields_ = [
        ("scene", ctypes.c_uint8),
        ("notif_count", ctypes.c_uint8),
        ("category", ctypes.c_uint8),
        ("priority", ctypes.c_uint8),
        ("age", ctypes.c_uint8),
        ("persistent", ctypes.c_uint8),
        ("civic", ctypes.c_uint8),
        ("secondary", ctypes.c_uint8),
        ("online", ctypes.c_uint8),
        ("seed", ctypes.c_uint8),
    ]


class CityState(ctypes.Structure):
    """``duel_city_state_t``: opaque carry-over the caller owns."""

    _fields_ = [("opaque", ctypes.c_uint64 * 4)]


class AmbientState(ctypes.Structure):
    """``duel_ambient_t``: a whole simulation world, owned by the caller."""

    _fields_ = [("opaque", ctypes.c_uint64 * 64)]


class AmbientStats(ctypes.Structure):
    """``duel_ambient_stats_t``: evidence that the city is alive."""

    _fields_ = [
        ("ticks", ctypes.c_uint32),
        ("casts", ctypes.c_uint32),
        ("impacts", ctypes.c_uint32),
        ("knockdowns", ctypes.c_uint32),
    ]


class AmbientWorld:
    """A self-playing duel: the firmware's simulation, driven by nobody.

    The desktop reads no input, so rather than leave the champions standing
    still the renderer runs the real world model with a caster that fabricates
    its own key positions from a seeded generator. Every position is invented.
    Nothing is sampled from a keyboard, a window, or a person.
    """

    def __init__(self, library: ctypes.CDLL, seed: int) -> None:
        self._library = library
        self._state = AmbientState()
        self.seed = seed & 0xFF
        library.duel_ambient_init(ctypes.byref(self._state), self.seed)

    def advance(self, elapsed_ms: int) -> int:
        """Run the world up to `elapsed_ms`. Returns the ticks actually run."""
        return self._library.duel_ambient_advance(
            ctypes.byref(self._state), ctypes.c_uint32(elapsed_ms & 0xFFFFFFFF)
        )

    @property
    def stats(self) -> AmbientStats:
        return self._library.duel_ambient_stats(ctypes.byref(self._state))

    @property
    def handle(self):
        return ctypes.byref(self._state)


def city_input(state: SemanticState, *, online: bool = True, seed: int = 0) -> CityInput:
    """Pack one resolved semantic state for the renderer.

    The civic bytes come from :class:`~arcane_host.protocol.CivicState`, which
    already packs exactly as ``duel_host.h``; the rest are the payload bytes
    ``build_packet`` writes, in the same order.
    """
    summary = state.summary
    return CityInput(
        scene=int(state.scene),
        notif_count=summary.count,
        category=int(summary.category),
        priority=int(summary.priority),
        age=summary.age,
        persistent=1 if summary.persistent else 0,
        civic=state.civic.civic_byte(),
        secondary=state.civic.secondary_byte(),
        online=1 if online else 0,
        seed=seed & 0xFF,
    )


def candidate_paths() -> list[Path]:
    """Where the shared library is looked for, in order.

    An explicit override first, then the installed package directory, then the
    repository's own build output so a checkout works with nothing installed.
    """
    override = os.environ.get("CORNE_ARCANE_CITY_LIB")
    here = Path(__file__).resolve()
    paths = [] if override is None else [Path(override)]
    paths.append(here.parent / LIBRARY_NAME)
    paths.append(here.parents[2] / "desktop" / LIBRARY_NAME)
    return paths


def library_path() -> Path:
    for path in candidate_paths():
        if path.is_file():
            return path
    tried = "\n  ".join(str(path) for path in candidate_paths())
    raise CityError(
        f"{LIBRARY_NAME} not found; build it with `make city-lib`. Looked in:\n  {tried}"
    )


class CityRenderer:
    """One loaded renderer at one scale, reused frame after frame."""

    def __init__(
        self,
        *,
        scale: int | None = None,
        layout: Layout | int = Layout.CITY,
        fit: tuple[int, int] | None = None,
        path: Path | None = None,
    ) -> None:
        """Load the renderer at one layout and one scale.

        ``fit`` asks for the largest whole-pixel scale that fits a window of
        that size instead of naming one; it and ``scale`` are alternatives.
        With neither, the scale follows the layout's own height.
        """
        self.path = Path(path) if path is not None else library_path()
        self.layout = Layout(layout)
        try:
            library = ctypes.CDLL(str(self.path))
        except OSError as error:
            raise CityError(f"cannot load {self.path}: {error}") from error
        self._bind(library)

        abi = library.duel_city_abi_version()
        if abi != CITY_ABI:
            raise CityError(f"{self.path} speaks city ABI {abi}, this build expects {CITY_ABI}")

        self._library = library
        self._state = CityState()
        library.duel_city_state_init(ctypes.byref(self._state))
        self.base_width, self.base_height = self._geometry(1)
        if fit is not None:
            scale = self.fit_scale(*fit)
        elif scale is None:
            scale = library.duel_city_default_scale(int(self.layout))
            self._check(scale if scale < 0 else 0)
        self._resize(scale)

    def _geometry(self, scale: int) -> tuple[int, int]:
        width = ctypes.c_int()
        height = ctypes.c_int()
        self._check(
            self._library.duel_city_geometry(
                int(self.layout), scale, ctypes.byref(width), ctypes.byref(height)
            )
        )
        return width.value, height.value

    def fit_scale(self, width: int, height: int) -> int:
        """Largest whole-pixel scale that fits a window this size."""
        scale = self._library.duel_city_fit_scale(int(self.layout), width, height)
        self._check(scale if scale < 0 else 0)
        return scale

    def _resize(self, scale: int) -> None:
        self.width, self.height = self._geometry(scale)
        self.scale = scale
        self._pixels = (ctypes.c_uint8 * (self.width * self.height))()
        self._header = f"P5\n{self.width} {self.height}\n255\n".encode("ascii")

    @staticmethod
    def _bind(library: ctypes.CDLL) -> None:
        library.duel_city_abi_version.argtypes = []
        library.duel_city_abi_version.restype = ctypes.c_int
        library.duel_city_state_init.argtypes = [ctypes.POINTER(CityState)]
        library.duel_city_state_init.restype = None
        library.duel_ambient_init.argtypes = [ctypes.POINTER(AmbientState), ctypes.c_uint8]
        library.duel_ambient_init.restype = None
        library.duel_ambient_advance.argtypes = [ctypes.POINTER(AmbientState), ctypes.c_uint32]
        library.duel_ambient_advance.restype = ctypes.c_uint8
        library.duel_ambient_stats.argtypes = [ctypes.POINTER(AmbientState)]
        library.duel_ambient_stats.restype = AmbientStats
        library.duel_city_geometry.argtypes = [
            ctypes.c_int,
            ctypes.c_int,
            ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_int),
        ]
        library.duel_city_geometry.restype = ctypes.c_int
        library.duel_city_default_scale.argtypes = [ctypes.c_int]
        library.duel_city_default_scale.restype = ctypes.c_int
        library.duel_city_fit_scale.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int]
        library.duel_city_fit_scale.restype = ctypes.c_int
        library.duel_city_backdrop.argtypes = [ctypes.c_int]
        library.duel_city_backdrop.restype = ctypes.c_int
        library.duel_city_frame_interval_ms.argtypes = []
        library.duel_city_frame_interval_ms.restype = ctypes.c_uint32
        library.duel_city_seek_warm_frames.argtypes = []
        library.duel_city_seek_warm_frames.restype = ctypes.c_int
        library.duel_city_tour_length.argtypes = []
        library.duel_city_tour_length.restype = ctypes.c_int
        library.duel_city_tour_stop.argtypes = [
            ctypes.c_int,
            ctypes.c_uint8,
            ctypes.POINTER(CityInput),
        ]
        library.duel_city_tour_stop.restype = ctypes.c_int
        library.duel_city_render.argtypes = [
            ctypes.POINTER(CityState),
            ctypes.POINTER(CityInput),
            ctypes.POINTER(AmbientState),
            ctypes.c_uint32,
            ctypes.c_uint32,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_size_t,
        ]
        library.duel_city_render.restype = ctypes.c_int

    @staticmethod
    def _check(code: int) -> None:
        if code != 0:
            raise CityError(_ERRORS.get(code, f"renderer returned {code}"))

    @property
    def backdrop(self) -> str:
        """The colour a shell should paint around the image, as Tk hex."""
        grey = self._library.duel_city_backdrop(int(self.layout))
        self._check(grey if grey < 0 else 0)
        return f"#{grey:02x}{grey:02x}{grey:02x}"

    @property
    def frame_interval_ms(self) -> int:
        """The world's own cadence, in milliseconds."""
        return self._library.duel_city_frame_interval_ms()

    @property
    def seek_warm_frames(self) -> int:
        """Frames a shell arriving at a moment must render, not just simulate."""
        return self._library.duel_city_seek_warm_frames()

    @property
    def fps(self) -> int:
        """The redraw rate that matches the world's cadence."""
        return round(1000 / self.frame_interval_ms)

    @property
    def tour_length(self) -> int:
        return self._library.duel_city_tour_length()

    def tour_stop(self, index: int, seed: int = 0) -> CityInput:
        """One stop on the no-daemon tour: every civic floor once."""
        packed = CityInput()
        self._check(self._library.duel_city_tour_stop(index, seed & 0xFF, ctypes.byref(packed)))
        return packed

    def ambient(self, seed: int = 0) -> AmbientWorld:
        """Start a self-playing world this renderer can draw."""
        return AmbientWorld(self._library, seed)

    def render(
        self,
        city: CityInput,
        elapsed_ms: int,
        frame: int,
        ambient: AmbientWorld | None = None,
    ) -> bytes:
        """Render one frame of both halves as a binary PGM image.

        PGM because it is what the review sheets already are, and because every
        toolkit worth using reads it without a dependency.
        """
        self._check(
            self._library.duel_city_render(
                ctypes.byref(self._state),
                ctypes.byref(city),
                ambient.handle if ambient is not None else None,
                ctypes.c_uint32(elapsed_ms & 0xFFFFFFFF),
                ctypes.c_uint32(frame & 0xFFFFFFFF),
                int(self.layout),
                self.scale,
                self._pixels,
                len(self._pixels),
            )
        )
        return self._header + bytes(self._pixels)


def resting_input(*, online: bool = True, seed: int = 0) -> CityInput:
    """The city with no daemon news: Commons, calm, nothing waiting."""
    return city_input(
        SemanticState(Scene.DUEL, NotificationSummary(), CivicState()),
        online=online,
        seed=seed,
    )
