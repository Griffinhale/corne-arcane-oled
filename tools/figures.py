#!/usr/bin/env python3
"""Documentation figures, rendered from the visual catalog.

Usage:
    firmware/sim_test/visual_runner --dump-pgm /tmp/frames
    python3 tools/figures.py /tmp/frames docs/images

Every image under docs/images that shows the displays is generated here, so a
figure in the documentation is derived from the same renderer the goldens
cover rather than drawn by hand. Re-run after a deliberate visual change and
review the result the same way a contact sheet is reviewed.

A dumped frame is 67x128: the left canvas in columns 0-31, a grey separator in
columns 32-34, and the right canvas in columns 35-66. Each canvas is one
physical 32x128 OLED.
"""

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

LEFT = (0, 0, 32, 128)
RIGHT = (35, 0, 67, 128)

BACKDROP = (18, 20, 24)
PANEL = (0, 0, 0)
BEZEL = (58, 64, 74)
INK = (196, 204, 216)
DIM = (128, 136, 150)

# A duel from rest to aftermath. Five catalog cases, each showing a different
# mechanic rather than five frames of the same one.
DUEL = [
    ("scenario_duel-idle", "at rest"),
    ("scenario_long-cast", "casting"),
    ("scenario_deflect", "ward holds"),
    ("scenario_impact", "impact"),
    ("scenario_persistent-critical", "aftermath"),
]

# The eight districts. Each is drawn twice by the catalog, once per
# architectural voice; the left canvas comes from the astral case and the
# right canvas from the mechanical one, which is how both halves render a
# district on hardware.
DISTRICTS = [
    "commons",
    "research",
    "workshop",
    "observatory",
    "scriptorium",
    "studio",
    "arena",
    "undercroft",
]

# Cases worth watching in motion. Neither list is a sequence the simulation
# produces on its own; each is a hand-picked tour of catalog cases, held one
# per animation frame so a reader can see the range without a keyboard.
SPELLS = [
    ("scenario_duel-idle", "at rest"),
    ("scenario_long-cast", "a long cast builds"),
    ("scenario_deflect", "the ward holds"),
    ("scenario_impact", "impact"),
    ("scenario_void-pierce", "void pierces the ward"),
    ("scenario_recipe-void-saturated", "saturated with void"),
    ("scenario_life-collapse", "a duellist goes down"),
    ("scenario_persistent-critical", "aftermath, still scarred"),
]

FIELDS = [
    ("field_rune_zone_%d", "rune field"),
    ("field_vortex_zone_%d", "vortex field"),
    ("field_wall_zone_%d", "wall field"),
    ("field_singularity_zone_%d", "singularity field"),
]


def font(size):
    try:
        return ImageFont.load_default(size=size)
    except TypeError:  # Pillow older than 10.1
        return ImageFont.load_default()


def canvas(frames, name, box):
    path = frames / f"{name}.pgm"
    if not path.exists():
        raise SystemExit(f"missing catalog frame: {path}")
    return Image.open(path).convert("L").crop(box)


def panel(img, scale):
    """One OLED: white art on black, inside a thin bezel."""
    art = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
    tinted = Image.new("RGB", art.size, PANEL)
    tinted.paste(
        Image.new("RGB", art.size, (255, 255, 255)), mask=art.point(lambda v: 255 if v > 127 else 0)
    )
    out = Image.new("RGB", (art.width + 2, art.height + 2), BEZEL)
    out.paste(tinted, (1, 1))
    return out


def cell(frames, left_name, right_name, scale, split):
    """Both halves of one frame, side by side with a physical-looking gap."""
    left = panel(canvas(frames, left_name, LEFT), scale)
    right = panel(canvas(frames, right_name, RIGHT), scale)
    out = Image.new("RGB", (left.width + split + right.width, left.height), BACKDROP)
    out.paste(left, (0, 0))
    out.paste(right, (left.width + split, 0))
    return out


def grid(cells, cols, pad, label_h, title_font):
    """Lay labelled cells out on a backdrop."""
    cw = max(c.width for c, _ in cells)
    ch = max(c.height for c, _ in cells)
    rows = (len(cells) + cols - 1) // cols
    out = Image.new(
        "RGB",
        (cols * cw + (cols + 1) * pad, rows * (ch + label_h) + (rows + 1) * pad),
        BACKDROP,
    )
    drawer = ImageDraw.Draw(out)
    for i, (img, label) in enumerate(cells):
        x = pad + (i % cols) * (cw + pad)
        y = pad + (i // cols) * (ch + label_h + pad)
        out.paste(img, (x + (cw - img.width) // 2, y))
        drawer.text(
            (x + cw // 2, y + ch + label_h // 2), label, fill=INK, font=title_font, anchor="mm"
        )
    return out


def animate(cells, pad, label_h, title_font, path, ms):
    """One GIF, one cell per frame, on a common canvas."""
    imgs = [grid([c], 1, pad, label_h, title_font) for c in cells]
    w = max(i.width for i in imgs)
    h = max(i.height for i in imgs)
    out = []
    for i in imgs:
        f = Image.new("RGB", (w, h), BACKDROP)
        f.paste(i, ((w - i.width) // 2, (h - i.height) // 2))
        out.append(f.quantize(colors=8, method=Image.MEDIANCUT))
    out[0].save(
        path, save_all=True, append_images=out[1:], duration=ms, loop=0, optimize=True, disposal=2
    )
    return path, (w, h)


def rooms_animation(frames, out_dir):
    cells = [
        (
            cell(frames, f"occupation_astral_{d}_work", f"occupation_mech_{d}_work", 3, 12),
            d.capitalize(),
        )
        for d in DISTRICTS
    ]
    return animate(cells, 20, 26, font(17), out_dir / "rooms.gif", 1100)


def spells_animation(frames, out_dir):
    cells = [(cell(frames, n, n, 3, 12), label) for n, label in SPELLS]
    return animate(cells, 20, 26, font(17), out_dir / "spells.gif", 1000)


def fields_animation(frames, out_dir):
    cells = []
    for pattern, label in FIELDS:
        for zone in range(4):
            name = pattern % zone
            cells.append((cell(frames, name, name, 3, 12), f"{label}, zone {zone}"))
    return animate(cells, 20, 26, font(17), out_dir / "fields.gif", 550)


# The repository's social preview. GitHub renders this at 1280x640. The three
# panels are one world at three points in its day: the sun high, the right
# wizard mid-cast, and the moon up over a different district.
SOCIAL = [
    ("scenario_deflect", "a cast meets a raised ward"),
    ("scenario_pose-cast", "a long cast gathers overhead"),
    ("scenario_impact", "it lands"),
]
SOCIAL_TEXT = [
    ("Corne Arcane", 62, INK, 0),
    ("A deterministic spell-duel world that lives", 21, DIM, 34),
    ("on a split keyboard's two OLEDs.", 21, DIM, 29),
    ("Key positions, never keycodes and never", 21, DIM, 42),
    ("characters, drive a 25 Hz simulation.", 21, DIM, 29),
]


def social_figure(frames, out_dir):
    """A 1280x640 card: what it is on the left, what it looks like on the right."""
    img = Image.new("RGB", (1280, 640), BACKDROP)
    drawer = ImageDraw.Draw(img)
    y = 150
    for text, size, fill, lead in SOCIAL_TEXT:
        y += lead
        drawer.text((72, y), text, fill=fill, font=font(size))
        y += size
    label_font = font(16)
    x = 566
    for name, label in SOCIAL:
        panels = cell(frames, name, name, 3, 10)
        top = (640 - panels.height) // 2 - 14
        img.paste(panels, (x, top))
        drawer.text(
            (x + panels.width // 2, top + panels.height + 20),
            label,
            fill=DIM,
            font=label_font,
            anchor="mm",
        )
        x += panels.width + 22
    path = out_dir / "social-preview.png"
    img.save(path, optimize=True)
    return path, img.size


def duel_figure(frames, out_dir):
    scale, pad = 3, 34
    cells = [(cell(frames, n, n, scale, 10), label) for n, label in DUEL]
    img = grid(cells, len(DUEL), pad, 26, font(16))
    path = out_dir / "duel.png"
    img.save(path, optimize=True)
    return path, img.size


def districts_figure(frames, out_dir):
    scale, pad = 3, 16
    cells = []
    for d in DISTRICTS:
        cells.append(
            (
                cell(frames, f"occupation_astral_{d}_work", f"occupation_mech_{d}_work", scale, 12),
                d.capitalize(),
            )
        )
    img = grid(cells, 4, pad, 24, font(16))
    path = out_dir / "districts.png"
    img.save(path, optimize=True)
    return path, img.size


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("frames_dir", type=Path, help="directory of --dump-pgm output")
    parser.add_argument("out_dir", type=Path, help="directory to write figures into")
    args = parser.parse_args()

    if not args.frames_dir.is_dir():
        print(f"no such frames directory: {args.frames_dir}", file=sys.stderr)
        return 1
    args.out_dir.mkdir(parents=True, exist_ok=True)

    for build in (
        duel_figure,
        districts_figure,
        social_figure,
        rooms_animation,
        spells_animation,
        fields_animation,
    ):
        path, size = build(args.frames_dir, args.out_dir)
        print(f"wrote {path} ({size[0]}x{size[1]}, {path.stat().st_size // 1024} KiB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
