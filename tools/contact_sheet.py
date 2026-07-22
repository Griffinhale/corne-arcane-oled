#!/usr/bin/env python3
"""Contact sheets for the visual catalog's --dump-pgm review images.

Usage:
    firmware/sim_test/visual_runner --dump-pgm /tmp/frames
    python3 tools/contact_sheet.py /tmp/frames out_prefix [--only name ...]

Reads every *.pgm in the directory (67x128: left canvas, 3-px gap, right
canvas), scales them up, labels each with its case name, and writes one or
more out_prefix_NN.png sheets. --only filters by substring so a re-baseline
review can sheet just the changed scenes.

This is the committed home of the previously session-scratchpad dumper
tooling (M15 handoff §6): rebuild recipe no longer required.
"""

import argparse
import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw

SCALE = 3
COLS = 8
ROWS = 8
LABEL_H = 12


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("frames_dir", type=Path)
    parser.add_argument("out_prefix")
    parser.add_argument("--only", nargs="*", default=None, help="substring filters on case names")
    args = parser.parse_args()

    paths = sorted(args.frames_dir.glob("*.pgm"))
    if args.only:
        paths = [p for p in paths if any(s in p.stem for s in args.only)]
    if not paths:
        print("no PGM frames matched", file=sys.stderr)
        return 1

    cell_w = 67 * SCALE
    cell_h = 128 * SCALE + LABEL_H
    per_sheet = COLS * ROWS
    sheets = math.ceil(len(paths) / per_sheet)
    for sheet in range(sheets):
        batch = paths[sheet * per_sheet : (sheet + 1) * per_sheet]
        rows = math.ceil(len(batch) / COLS)
        img = Image.new("L", (COLS * cell_w, rows * cell_h), 40)
        drawer = ImageDraw.Draw(img)
        for i, path in enumerate(batch):
            frame = Image.open(path).resize((cell_w, 128 * SCALE), Image.NEAREST)
            x = (i % COLS) * cell_w
            y = (i // COLS) * cell_h
            img.paste(frame, (x, y + LABEL_H))
            drawer.text((x + 2, y + 1), path.stem[:40], fill=255)
        out = f"{args.out_prefix}_{sheet:02d}.png"
        img.save(out)
        print(f"wrote {out} ({len(batch)} frames)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
