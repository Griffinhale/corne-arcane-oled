"""A desktop window showing the city, for machines with no keyboard displays.

It covers the screenless keyboard and both non-split cases at once, and is the
reference implementation any later platform can read. What it shows is the
city: the tower, its floor, the sky, the resident, and whatever the daemon's
notification summary sends walking through. Never the duel. On the keyboard,
key positions never leave the firmware, and sampling them on a desktop is
exactly the access this project refuses, so nothing here reads input. The one
thing it hears about typing is the opt-in typing helper's per-minute summary
(docs/typing-summary.md), four enums that fill the city's own typing fields
and go nowhere else: not to the service, and not to the keyboard.

Two ways to run it:

    corne-arcane              follow the running service
    corne-arcane --no-hid     follow a service run with --no-hid, for a keyboard
                              without this firmware: no Corne is expected
    corne-arcane --tour       walk the districts with no service at all

The window is a client of corne-arcane-host.service, never a second daemon: it
reads the service's Control interface for the keyboard link and the world the
keyboard is being sent, and it never opens the keyboard. A service run with
--host-signals also reports the desktop's presence, load, strain, call, alert
and running commands as enums (HostSignalsChanged), which fill the city's host
signal fields at finer detail than the wire; without it they stay none. With
no service on the bus it shows the city offline and says so under the image.

The default layout is the town, the view the phone and the watch open on: one
wizard tower at the centre of a small city. `--layout city` shows the
keyboard's two panels as one continuous scene, the space between the towers
drawn unlit, and `--layout desk` restores the two-panel view the review sheets
use. `left` or `right` shows a single tower, and `landscape` is the town's wide
counterpart. `--size` fits any of them at a whole-pixel scale and letterboxes
the remainder.
"""

from __future__ import annotations

import argparse
import os
import sys
import time
from typing import Callable

from .app_controls import (  # noqa: F401 -- link_caption and NO_SERVICE are re-exported
    NO_SERVICE,
    Controls,
    ControlsPanel,
    ServiceView,
    link_caption,
)
from .app_settings import Settings, SettingsWindow
from .city import (
    CityInput,
    CityRenderer,
    CityRow,
    CityRowSpread,
    CitySpread,
    CityTempo,
    Layout,
    city_input,
)
from .dbus_contract import (
    TYPING_BUS_NAME,
    TYPING_INTERFACE,
    TYPING_OBJECT_PATH,
    TYPING_SIGNATURE,
    TYPING_SUMMARY,
)
from .semantic import SemanticState
from .typing_summary import Row, RowSpread, Spread, Tempo, TypingSummary

CAPTION_INK = "#9aa3b2"
# The link line when no Corne is expected: the city runs on focus and
# notifications alone, so a missing keyboard is not something to fix.
NO_CORNE = "No Corne: the city follows this desktop"
# The helper sends one summary a minute and nothing for a sparse minute, so a
# summary older than two and a half minutes means typing stopped or the helper
# did: one dropped minute between two kept ones is not silence, two are.
TYPING_STALE_SECONDS = 150.0


def service_caption(status: tuple[str, str, bool, str] | None, *, no_hid: bool) -> str:
    if no_hid and status is not None and status[0] == "absent" and not status[2]:
        return NO_CORNE
    return link_caption(status)


class CityWindow:
    """One Tk window holding both canvases, redrawn from a semantic state."""

    def __init__(
        self,
        *,
        scale: int | None = None,
        layout: Layout | int = Layout.TOWN,
        size: tuple[int, int] | None = None,
        seed: int = 0,
        duels: bool = True,
        caption: bool = False,
        title: str = "Corne Arcane",
        renderer: CityRenderer | None = None,
        tk=None,
        clock: Callable[[], float] = time.monotonic,
    ) -> None:
        """Hold one window.

        ``size`` fixes the window and centres the city in it; without it the
        window hugs the image. ``duels`` starts the self-playing world.
        ``caption`` adds a line of text under the image, for the link state.
        """
        if tk is None:
            import tkinter

            tk = tkinter
        if renderer is None:
            renderer = CityRenderer(scale=scale, layout=layout, fit=size)
        self.renderer = renderer
        self.seed = seed & 0xFF
        self.ambient = renderer.ambient(self.seed) if duels else None
        self.clock = clock
        self.started = clock()
        self.frames = 0
        self.closed = False
        self._after_id = None

        backdrop = renderer.backdrop
        self.tk = tk
        self.size = size
        self.root = tk.Tk()
        self.root.title(title)
        self.root.configure(background=backdrop)
        self.root.resizable(False, False)
        self.photo = tk.PhotoImage(master=self.root)
        self.label = tk.Label(
            self.root, image=self.photo, background=backdrop, borderwidth=0, highlightthickness=0
        )
        self.caption = None
        if caption:
            # Wrapped at the image's width, so a longer line never widens the
            # window and slides the city sideways.
            self.caption = tk.Label(
                self.root,
                text="",
                background=backdrop,
                foreground=CAPTION_INK,
                wraplength=renderer.width,
            )
        if size is None:
            self.label.pack(padx=12, pady=12)
            if self.caption is not None:
                self.caption.pack(padx=12, pady=(0, 8))
        else:
            self.root.geometry(f"{size[0]}x{size[1]}")
            self.label.place(relx=0.5, rely=0.5, anchor="center")
            if self.caption is not None:
                self.caption.place(relx=0.5, rely=1.0, anchor="s", y=-6)

    def bind_close(self, callback: Callable[[], None]) -> None:
        self.root.protocol("WM_DELETE_WINDOW", callback)

    def schedule(self, delay_ms: int, callback: Callable[[], None]) -> None:
        """Schedule one redraw, remembering it so close() can cancel it.

        A timer left pending across destroy() fires against a widget that no
        longer exists, and Tk reports that on stderr. Only the window knows
        when it is going away, so only the window can cancel it.
        """
        self._after_id = self.root.after(delay_ms, callback)

    def draw(self, city: CityInput) -> None:
        """Render and present one frame. Safe to call after close()."""
        if self.closed:
            return
        elapsed_ms = int((self.clock() - self.started) * 1000.0)
        # The world is advanced to this frame's clock before it is drawn, so a
        # slow or fast redraw changes how often the duel is sampled, never how
        # fast it runs.
        if self.ambient is not None:
            self.ambient.advance(elapsed_ms)
        blob = self.renderer.render(city, elapsed_ms, self.frames, ambient=self.ambient)
        self.frames += 1
        self.photo.configure(data=blob)
        self.root.update()

    def draw_state(self, state: SemanticState, *, online: bool = True) -> None:
        self.draw(city_input(state, online=online, seed=self.seed))

    def attach(self, panel) -> None:
        """Lay out a panel under the image: packed, or placed in a fixed-size window."""
        if self.size is None:
            panel.pack()
        else:
            panel.place()

    def set_caption(self, text: str) -> None:
        if self.closed or self.caption is None:
            return
        if self.caption.cget("text") != text:
            self.caption.configure(text=text)

    def close(self) -> None:
        if self.closed:
            return
        self.closed = True
        if self._after_id is not None:
            try:
                self.root.after_cancel(self._after_id)
            except Exception:
                pass
            self._after_id = None
        try:
            self.root.destroy()
        except Exception:
            pass


class TypingView:
    """The opt-in typing helper's latest summary, as the city's typing fields.

    It listens to the helper's bus name and nothing else, holds one summary,
    and lets it go when the helper leaves the bus or falls silent for
    TYPING_STALE_SECONDS. The fields it fills are the desktop city's own; the
    keyboard's payload bytes are never touched.
    """

    def __init__(self, Gio, GLib, connection, *, clock: Callable[[], float] = time.monotonic):
        self.Gio = Gio
        self.connection = connection
        self.clock = clock
        self.summary: TypingSummary | None = None
        self.received = 0.0
        self.present = False
        self._subscription = connection.signal_subscribe(
            TYPING_BUS_NAME,
            TYPING_INTERFACE,
            TYPING_SUMMARY,
            TYPING_OBJECT_PATH,
            None,
            Gio.DBusSignalFlags.NONE,
            self._summary,
        )
        self._watch_id = Gio.bus_watch_name_on_connection(
            connection,
            TYPING_BUS_NAME,
            Gio.BusNameWatcherFlags.NONE,
            self._appeared,
            self._vanished,
        )

    def _summary(self, *args) -> None:
        parameters = args[-1]
        if parameters.get_type_string() != TYPING_SIGNATURE:
            return
        tempo, spread, row, row_spread = parameters.unpack()
        try:
            summary = TypingSummary(Tempo(tempo), Spread(spread), Row(row), RowSpread(row_spread))
        except ValueError:
            return
        self.summary = summary
        self.received = self.clock()

    def _appeared(self, _connection, _name, _owner) -> None:
        self.present = True

    def _vanished(self, _connection, _name) -> None:
        self.present = False
        self.summary = None

    def apply(self, city: CityInput) -> CityInput:
        """Fill the typing fields of one input: the summary plus one, or none."""
        summary = self.summary
        if summary is not None and self.clock() - self.received >= TYPING_STALE_SECONDS:
            summary = self.summary = None
        if summary is None:
            city.tempo = city.spread = city.row = city.row_spread = 0
        else:
            city.tempo = CityTempo(summary.tempo + 1)
            city.spread = CitySpread(summary.spread + 1)
            city.row = CityRow(summary.row + 1)
            city.row_spread = CityRowSpread(summary.row_spread + 1)
        return city

    def close(self) -> None:
        if self._subscription:
            self.connection.signal_unsubscribe(self._subscription)
            self._subscription = 0
        if self._watch_id:
            self.Gio.bus_unwatch_name(self._watch_id)
            self._watch_id = 0


def present(
    window: CityWindow, next_input: Callable[[], CityInput], *, fps: int | None = None
) -> None:
    """Redraw at the simulation's cadence until the window closes."""
    interval_ms = max(1, round(1000 / (fps or window.renderer.fps)))

    def step() -> None:
        if window.closed:
            return
        window.draw(next_input())
        if not window.closed:
            window.schedule(interval_ms, step)

    window.bind_close(window.close)
    window.schedule(0, step)
    window.root.mainloop()


def run_tour(window: CityWindow, *, fps: int | None = None, dwell: float = 6.0) -> None:
    """Walk the floors with no daemon, no bus, and no keyboard.

    The stops are the renderer's, so a second shell walks the same tour rather
    than inventing its own and drifting.
    """
    started = window.clock()
    present(
        window,
        lambda: window.renderer.tour_stop(int((window.clock() - started) / dwell), window.seed),
        fps=fps,
    )


def follow_service(
    window: CityWindow,
    view: ServiceView,
    *,
    fps: int | None = None,
    controls: Controls | None = None,
    no_hid: bool = False,
    typing: TypingView | None = None,
) -> None:
    """Draw what the service reports, its link state, and the controls under it.

    With ``no_hid`` there is no Corne to pause, lend to Vial, observe or flash,
    so the window shows no controls: none of them could do anything but fail.
    ``typing`` fills the city's typing fields from the helper's summaries; it
    is pumped with the service's bus, and without it the fields stay none.
    """
    controls = controls or Controls(view)
    settings = Settings(controls)
    settings_window: SettingsWindow | None = None

    def open_settings() -> None:
        nonlocal settings_window
        if settings_window is None or settings_window.closed:
            settings_window = SettingsWindow(window.tk, window.root, settings)

    panel = None
    if not no_hid:
        panel = ControlsPanel(
            window.tk,
            window.root,
            controls,
            background=window.renderer.backdrop,
            ink=CAPTION_INK,
            on_settings=open_settings,
        )
        window.attach(panel)

    def next_input() -> CityInput:
        view.pump()
        window.set_caption(service_caption(view.status, no_hid=no_hid))
        if panel is not None:
            panel.refresh()
        if settings_window is not None:
            settings_window.refresh()
        city = view.city(window.seed)
        return city if typing is None else typing.apply(city)

    try:
        present(window, next_input, fps=fps)
    finally:
        controls.close()


def window_size(value: str) -> tuple[int, int]:
    """Parse a WxH window size."""
    width, _, height = value.lower().partition("x")
    try:
        size = (int(width), int(height))
    except ValueError:
        raise argparse.ArgumentTypeError(f"expected WIDTHxHEIGHT, got {value!r}") from None
    if min(size) < 1:
        raise argparse.ArgumentTypeError("window size must be positive")
    return size


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--layout",
        choices=[layout.name.lower() for layout in Layout],
        default=Layout.TOWN.name.lower(),
        help=(
            "town: one tower at the centre of a 256x256 city (default); "
            "landscape: the town, wide; city: the keyboard's panels as one scene; "
            "desk: two panels; left/right: one tower"
        ),
    )
    parser.add_argument(
        "--scale", type=int, help="pixels per canvas pixel; the default follows the layout"
    )
    parser.add_argument(
        "--size",
        type=window_size,
        metavar="WIDTHxHEIGHT",
        help="fixed window size; the city is centred at the largest scale that fits",
    )
    parser.add_argument(
        "--fps", type=int, help="redraw cadence; the default follows the simulation's own tick"
    )
    parser.add_argument(
        "--duels",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="run the self-playing duel; --no-duels stills the champions",
    )
    parser.add_argument(
        "--seed", type=lambda value: int(value, 0), default=0x5A, help="presentation seed"
    )
    parser.add_argument(
        "--tour",
        action="store_true",
        help="walk the districts without the service or the bus",
    )
    parser.add_argument("--tour-dwell", type=float, default=6.0, metavar="SECONDS")
    parser.add_argument(
        "--no-hid",
        action="store_true",
        # The daemon's own setting, so one line in the environment covers both.
        default=os.environ.get("CORNE_ARCANE_NO_HID", "") not in ("", "0"),
        help="no Corne is expected (the service runs with --no-hid): hide its controls "
        "(setting: CORNE_ARCANE_NO_HID=1)",
    )
    args = parser.parse_args(argv)
    if args.scale is not None and args.size is not None:
        parser.error("--scale and --size are alternatives: --size picks the scale that fits")
    if args.scale is not None and not 1 <= args.scale <= 16:
        parser.error("--scale must be in 1..16")
    if args.fps is not None and not 1 <= args.fps <= 60:
        parser.error("--fps must be in 1..60")
    if args.tour_dwell <= 0:
        parser.error("--tour-dwell must be positive")
    return args


def main(argv: list[str] | None = None) -> int:
    from .city import CityError

    args = parse_args(argv)
    if not args.tour:
        try:
            import gi

            gi.require_version("Gio", "2.0")
            from gi.repository import Gio, GLib
        except (ImportError, ValueError) as error:
            print(
                f"corne-arcane: PyGObject is needed to follow the service ({error}); "
                "--tour runs without it",
                file=sys.stderr,
            )
            return 2
    try:
        window = CityWindow(
            scale=args.scale,
            layout=Layout[args.layout.upper()],
            size=args.size,
            seed=args.seed,
            duels=args.duels,
            caption=not args.tour,
        )
    except CityError as error:
        print(f"corne-arcane: {error}", file=sys.stderr)
        return 2

    if args.tour:
        run_tour(window, fps=args.fps, dwell=args.tour_dwell)
        return 0

    view = None
    typing = None
    try:
        bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        view = ServiceView(Gio, GLib, bus)
        typing = TypingView(Gio, GLib, bus)
        follow_service(window, view, fps=args.fps, no_hid=args.no_hid, typing=typing)
    except GLib.Error as error:
        print(f"corne-arcane: no session bus ({error.message})", file=sys.stderr)
        return 2
    finally:
        if typing is not None:
            typing.close()
        if view is not None:
            view.close()
        window.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
