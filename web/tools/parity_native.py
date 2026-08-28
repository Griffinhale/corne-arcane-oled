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
import hashlib
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "host"))

from arcane_host.city import CityRenderer, Layout  # noqa: E402

MATRIX = json.loads((Path(__file__).parent / "parity_matrix.json").read_text())
LAYOUTS = MATRIX["layouts"]
SEEDS = MATRIX["seeds"]
FRAMES = MATRIX["frames"]
TICK_MS = MATRIX["tick_ms"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path, help="directory for hashes and raw dumps")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

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
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
