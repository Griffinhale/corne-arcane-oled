"""Render the parity matrix with the native library, via the existing binding.

The reference side of the WASM acceptance test. It deliberately goes through
``host.arcane_host.city`` rather than a fresh ctypes binding, so what the
browser is compared against is the library the desktop shell actually runs,
reached the way the desktop shell actually reaches it.

Writes a hash per frame for the whole matrix, and the raw pixels of one frame
per layout so the comparison can be a byte-for-byte ``cmp`` rather than a
statement about hashes.
"""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "host"))

from arcane_host.city import OFF_KEYBOARD_FIELDS, CityInput, CityRenderer, Layout  # noqa: E402
from arcane_host.protocol import CivicState, Floor, Intensity, Mode, Secondary  # noqa: E402

MATRIX = json.loads((Path(__file__).parent / "parity_matrix.json").read_text())
LAYOUTS = MATRIX["layouts"]
SEEDS = MATRIX["seeds"]
FRAMES = MATRIX["frames"]
TICK_MS = MATRIX["tick_ms"]
SEMANTIC = MATRIX["semantic"]
DUEL_CITY_ERR_LAYOUT = -5


def check_matrix() -> None:
    """Fail unless the matrix covers every layout and ticks at the library's cadence.

    The library is the judge: a new layout or a changed tick must reach the
    matrix, or the parity run would quietly test less than the product ships.
    """
    renderer = CityRenderer(scale=1)
    if TICK_MS != renderer.frame_interval_ms:
        raise SystemExit(
            f"FAIL parity: the matrix says tick_ms {TICK_MS} and the library says "
            f"{renderer.frame_interval_ms}"
        )
    width, height = ctypes.c_int(), ctypes.c_int()
    # Layouts are 0..n-1; the first one the library refuses is n.
    geometry = renderer._library.duel_city_geometry
    count = next(
        (
            n
            for n in range(256)
            if geometry(n, 1, ctypes.byref(width), ctypes.byref(height)) == DUEL_CITY_ERR_LAYOUT
        ),
        None,
    )
    if count is None or LAYOUTS != list(range(count)):
        raise SystemExit(
            f"FAIL parity: the matrix has layouts {LAYOUTS} and the library has {count} layouts"
        )


def semantic_input(row: dict) -> CityInput:
    """A row's input, packed by the daemon's own CivicState rather than by hand.

    The off-keyboard signals go through their enums, so a number the row names
    and the enum lacks fails here, as it does in the Swift leg.
    """
    fields = row["input"]
    signals = row.get("signals", {})
    civic = CivicState(
        Floor(fields["floor"]),
        Mode(fields["mode"]),
        Intensity(fields["intensity"]),
        Secondary(fields["activity"]),
    )
    return CityInput(
        scene=fields["scene"],
        notif_count=fields["count"],
        category=fields["category"],
        priority=fields["priority"],
        age=fields["age"],
        persistent=int(fields["persistent"]),
        civic=civic.civic_byte(),
        secondary=civic.secondary_byte(),
        online=int(fields["online"]),
        seed=row["seed"],
        **{name: int(kind(signals.get(name, 0))) for name, kind in OFF_KEYBOARD_FIELDS},
    )


def semantic_lines() -> list[str]:
    """The semantic rows, in the matrix's line format with the row's name in front."""
    lines = []
    for row in SEMANTIC:
        name, layout, seed = row["name"], row["layout"], row["seed"]
        renderer = CityRenderer(scale=1, layout=Layout(layout))
        world = renderer.ambient(seed)
        city = semantic_input(row)
        for frame in range(row["frames"]):
            now = frame * TICK_MS
            world.advance(now)
            pixels = renderer.render(city, now, frame, ambient=world).split(b"\n", 3)[3]
            digest = hashlib.sha256(pixels).hexdigest()
            lines.append(f"{name} {layout} {seed} {frame} {len(pixels)} {digest}")
        stats = world.stats
        lines.append(
            f"{name} {layout} {seed} stats {stats.ticks} {stats.casts} "
            f"{stats.impacts} {stats.knockdowns}"
        )
    return lines


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path, help="directory for hashes and raw dumps")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    check_matrix()

    lines = []
    for layout in LAYOUTS:
        for seed in SEEDS:
            # A fresh renderer per case, because the floor policy carries over
            # between frames and the WASM side re-inits per case too.
            renderer = CityRenderer(scale=1, layout=Layout(layout))
            world = renderer.ambient(seed)
            city = renderer.tour_stop(0, seed)
            pixels = b""
            for frame in range(FRAMES):
                now = frame * TICK_MS
                world.advance(now)
                image = renderer.render(city, now, frame, ambient=world)
                # render() returns a PGM; the pixels are everything after the
                # third newline of the header.
                pixels = image.split(b"\n", 3)[3]
                digest = hashlib.sha256(pixels).hexdigest()
                lines.append(f"{layout} {seed} {frame} {len(pixels)} {digest}")
            stats = world.stats
            lines.append(
                f"{layout} {seed} stats {stats.ticks} {stats.casts} "
                f"{stats.impacts} {stats.knockdowns}"
            )
            if seed == SEEDS[0]:
                (args.out / f"native-layout{layout}.raw").write_bytes(pixels)

    (args.out / "native.hashes").write_text("\n".join(lines) + "\n")
    print(f"native: {len(lines)} lines", file=sys.stderr)
    # Apart from native.hashes, which the WASM leg is diffed against.
    semantic = semantic_lines()
    (args.out / "native-semantic.hashes").write_text("\n".join(semantic) + "\n")
    print(f"native: {len(semantic)} semantic lines", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
