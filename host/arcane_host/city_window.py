"""A desktop window showing the city, for machines with no keyboard displays.

It covers the screenless keyboard and both non-split cases at once, and is the
reference implementation any later platform can read. What it shows is the
city: the tower, its floor, the sky, the resident, and whatever the daemon's
notification summary sends walking through. Never the duel. On the keyboard,
key positions never leave the firmware, and sampling them on a desktop is
exactly the access this project refuses, so nothing here reads input.

Two ways to run it:

    corne-arcane              follow the running service
    corne-arcane --tour       walk the districts with no service at all

The window is a client of corne-arcane-host.service, never a second daemon: it
reads the service's Control interface for the keyboard link and the world the
keyboard is being sent, and it never opens the keyboard. With no service on
the bus it shows the city offline and says so under the image.

The default layout is one continuous scene: the space between the two towers
is world the panels cannot show, so it is drawn unlit rather than as a desk.
`--layout desk` restores the two-panel view the review sheets use, and
`--layout left` or `right` shows a single tower. `town` and `landscape` select
the renderer's square and wide drawing layers; `--size` fits any of them at a
whole-pixel scale and letterboxes the remainder.
"""

from __future__ import annotations

import argparse
import sys
import time
from typing import Callable

from .city import CityInput, CityRenderer, Layout, city_input, resting_input
from .dbus_contract import (
    BUS_NAME,
    CONTROL_INTERFACE,
    OBJECT_PATH,
    STATUS,
    STATUS_CHANGED,
    STATUS_SIGNATURE,
    WORLD,
    WORLD_CHANGED,
    WORLD_SIGNATURE,
)
from .semantic import SemanticState

CAPTION_INK = "#9aa3b2"


class CityWindow:
    """One Tk window holding both canvases, redrawn from a semantic state."""

    def __init__(
        self,
        *,
        scale: int | None = None,
        layout: Layout | int = Layout.CITY,
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


# What the link line says, for each Control link state.
LINK_CAPTIONS = {
    "starting": "Connecting to the keyboard",
    "connected": "Keyboard connected",
    "absent": "No keyboard found",
    "denied": "Keyboard found, but not allowed to open it",
    "several": "Several keyboards found; give the service --device",
    "failed": "Keyboard link failed; retrying",
}
NO_SERVICE = "Service not running, so the city is offline"


def link_caption(status: tuple[str, str, bool, str] | None) -> str:
    if status is None:
        return NO_SERVICE
    link, _device, paused, owner = status
    if paused:
        return f"Keyboard lent to {owner}"
    return LINK_CAPTIONS.get(link, f"Keyboard: {link}")


class ServiceView:
    """What the running service reports: the keyboard link and the world it sends.

    A client, not a daemon. It reads the Control interface and its signals and
    never opens the keyboard or sends a heartbeat, so the service's heartbeat
    stays the only one. Until the service answers, and after it goes away,
    ``status`` and ``world`` are None and the city is drawn offline.
    """

    CALL_TIMEOUT_MS = 1000

    def __init__(self, Gio, GLib, connection) -> None:
        self.Gio = Gio
        self.GLib = GLib
        self.connection = connection
        self.status: tuple[str, str, bool, str] | None = None
        self.world: tuple[int, ...] | None = None
        self._subscriptions = [
            connection.signal_subscribe(
                BUS_NAME,
                CONTROL_INTERFACE,
                name,
                OBJECT_PATH,
                None,
                Gio.DBusSignalFlags.NONE,
                handler,
            )
            for name, handler in (
                (STATUS_CHANGED, self._status_changed),
                (WORLD_CHANGED, self._world_changed),
            )
        ]
        self._watch_id = Gio.bus_watch_name_on_connection(
            connection, BUS_NAME, Gio.BusNameWatcherFlags.NONE, self._appeared, self._vanished
        )

    def _status_changed(self, *args) -> None:
        self.status = tuple(args[-1].unpack())

    def _world_changed(self, *args) -> None:
        self.world = tuple(args[-1].unpack())

    def _appeared(self, _connection, _name, _owner) -> None:
        self._fetch(STATUS, STATUS_SIGNATURE, "status")
        self._fetch(WORLD, WORLD_SIGNATURE, "world")

    def _vanished(self, _connection, _name) -> None:
        self.status = None
        self.world = None

    def _fetch(self, method: str, signature: str, attribute: str) -> None:
        def done(connection, result) -> None:
            try:
                value = connection.call_finish(result).unpack()
            except self.GLib.Error:
                # A service older than World still reports its link; the city
                # then rests until a WorldChanged arrives.
                return
            setattr(self, attribute, tuple(value))

        self.connection.call(
            BUS_NAME,
            OBJECT_PATH,
            CONTROL_INTERFACE,
            method,
            None,
            self.GLib.VariantType(signature),
            self.Gio.DBusCallFlags.NO_AUTO_START,
            self.CALL_TIMEOUT_MS,
            None,
            done,
        )

    def pump(self) -> None:
        """Deliver whatever the bus has sent, without blocking the window."""
        context = self.GLib.MainContext.default()
        while context.iteration(False):
            pass

    def city(self, seed: int) -> CityInput:
        if self.world is None:
            return resting_input(online=self.status is not None, seed=seed)
        return CityInput(*self.world, online=1, seed=seed & 0xFF)

    def caption(self) -> str:
        return link_caption(self.status)

    def close(self) -> None:
        for subscription in self._subscriptions:
            self.connection.signal_unsubscribe(subscription)
        self._subscriptions = []
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


def follow_service(window: CityWindow, view: ServiceView, *, fps: int | None = None) -> None:
    """Draw what the service reports, and its link state under the image."""

    def next_input() -> CityInput:
        view.pump()
        window.set_caption(view.caption())
        return view.city(window.seed)

    present(window, next_input, fps=fps)


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
        default=Layout.CITY.name.lower(),
        help=(
            "city: one continuous scene (default); desk: two panels; "
            "left/right: one tower; town: one tower at the centre of a 256x256 city"
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
    try:
        view = ServiceView(Gio, GLib, Gio.bus_get_sync(Gio.BusType.SESSION, None))
        follow_service(window, view, fps=args.fps)
    except GLib.Error as error:
        print(f"corne-arcane: no session bus ({error.message})", file=sys.stderr)
        return 2
    finally:
        if view is not None:
            view.close()
        window.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
