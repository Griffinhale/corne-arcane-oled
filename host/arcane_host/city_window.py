"""A desktop window showing the city, for machines with no keyboard displays.

It covers the screenless keyboard and both non-split cases at once, and is the
reference implementation any later platform can read. What it shows is the
city: the tower, its floor, the sky, the resident, and whatever the daemon's
notification summary sends walking through. Never the duel. On the keyboard,
key positions never leave the firmware, and sampling them on a desktop is
exactly the access this project refuses, so nothing here reads input.

Two ways to run it:

    corne-arcane-city                 follow the live daemon in this process
    corne-arcane-city --tour          walk the districts with no daemon at all

The default layout is one continuous scene: the space between the two towers
is world the panels cannot show, so it is drawn unlit rather than as a desk.
`--layout desk` restores the two-panel view the review sheets use, and
`--layout left` or `right` shows a single tower. `town` and `landscape` select
the renderer's square and wide drawing layers; `--size` fits any of them at a
whole-pixel scale and letterboxes the remainder.

The live mode *is* the daemon: it builds the same semantic stack and owns the
same bus name, and the window reads the resolved state the heartbeat already
drives. Arguments this command does not recognise are handed to the daemon
unchanged, so `corne-arcane-city --scale 5 --verbose` works as expected.
"""

from __future__ import annotations

import argparse
import sys
import time
from typing import Callable

from .city import CityInput, CityRenderer, Layout, city_input
from .semantic import SemanticState


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
        title: str = "Corne Arcane",
        renderer: CityRenderer | None = None,
        tk=None,
        clock: Callable[[], float] = time.monotonic,
    ) -> None:
        """Hold one window.

        ``size`` fixes the window and centres the city in it; without it the
        window hugs the image. ``duels`` starts the self-playing world.
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
        if size is None:
            self.label.pack(padx=12, pady=12)
        else:
            self.root.geometry(f"{size[0]}x{size[1]}")
            self.label.place(relx=0.5, rely=0.5, anchor="center")

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


class RuntimePresenter:
    """Drives a CityWindow from the daemon's GLib loop at a fixed cadence.

    The window is a reader, not a source: it samples whatever the resolver has
    settled on at its own redraw rate, so animation stays smooth while the
    semantic stack keeps its own event-driven schedule.
    """

    def __init__(self, GLib, runtime, resolver, window: CityWindow, *, fps: int | None = None):
        self.GLib = GLib
        self.runtime = runtime
        self.resolver = resolver
        self.window = window
        self.interval_ms = max(1, round(1000 / (fps or window.renderer.fps)))
        self.source_id = GLib.timeout_add(self.interval_ms, self.tick)
        window.bind_close(self.quit)

    def tick(self) -> bool:
        if self.window.closed:
            self.source_id = 0
            return False
        try:
            # Always online: these semantics are resolved in this process, not
            # relayed from a keyboard. A machine with no keyboard attached is
            # the case this window exists for, so an absent device must never
            # empty the city.
            self.window.draw_state(self.resolver.state, online=True)
        except Exception:
            self.quit()
            raise
        return True

    def quit(self) -> None:
        self.window.close()
        self.runtime.loop.quit()

    def close(self) -> None:
        if self.source_id:
            try:
                self.GLib.source_remove(self.source_id)
            except Exception:
                pass
            self.source_id = 0
        self.window.close()


def run_tour(window: CityWindow, *, fps: int | None = None, dwell: float = 6.0) -> None:
    """Walk the floors with no daemon, no bus, and no keyboard.

    The stops are the renderer's, so a second shell walks the same tour rather
    than inventing its own and drifting.
    """
    interval_ms = max(1, round(1000 / (fps or window.renderer.fps)))
    started = window.clock()

    def step() -> None:
        if window.closed:
            return
        index = int((window.clock() - started) / dwell)
        window.draw(window.renderer.tour_stop(index, window.seed))
        if not window.closed:
            window.schedule(interval_ms, step)

    window.bind_close(window.close)
    window.schedule(0, step)
    window.root.mainloop()


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


def parse_args(argv: list[str] | None = None) -> tuple[argparse.Namespace, list[str]]:
    parser = argparse.ArgumentParser(
        description=__doc__.splitlines()[0],
        epilog="Unrecognised arguments are passed to the daemon.",
    )
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
        help="walk the districts without starting a daemon or touching the bus",
    )
    parser.add_argument("--tour-dwell", type=float, default=6.0, metavar="SECONDS")
    args, rest = parser.parse_known_args(argv)
    if args.scale is not None and args.size is not None:
        parser.error("--scale and --size are alternatives: --size picks the scale that fits")
    if args.scale is not None and not 1 <= args.scale <= 16:
        parser.error("--scale must be in 1..16")
    if args.fps is not None and not 1 <= args.fps <= 60:
        parser.error("--fps must be in 1..60")
    if args.tour_dwell <= 0:
        parser.error("--tour-dwell must be positive")
    return args, rest


def main(argv: list[str] | None = None) -> int:
    from .city import CityError

    args, rest = parse_args(argv)
    try:
        window = CityWindow(
            scale=args.scale,
            layout=Layout[args.layout.upper()],
            size=args.size,
            seed=args.seed,
            duels=args.duels,
        )
    except CityError as error:
        print(f"arcane-city: {error}", file=sys.stderr)
        return 2

    if args.tour:
        run_tour(window, fps=args.fps, dwell=args.tour_dwell)
        return 0

    from . import daemon

    def presenter_factory(GLib, runtime, resolver):
        return RuntimePresenter(GLib, runtime, resolver, window, fps=args.fps)

    try:
        return daemon.run(daemon.parse_args(rest), presenter_factory=presenter_factory)
    finally:
        window.close()


if __name__ == "__main__":
    raise SystemExit(main())
