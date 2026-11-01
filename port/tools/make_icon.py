"""Draws the Barthes Recompiled icons from simple shapes (original art, no game artwork). The same script lives in
the launcher's and each game's tools/, so the family stays one picture:

    python tools/make_icon.py shelf|hp1|hp2|hp3 [out.ico] [preview.png]

shelf: the launcher's three cloth-bound spines on the shelf's dark wall; hp1/hp2/hp3: one book from that shelf,
face on, with its volume number in gold (I, II, III drawn as serifed bars, no font). Needs Pillow.
"""
import colorsys
import sys
from pathlib import Path

from PIL import Image, ImageDraw

WALL = (22, 24, 30, 255)
PAPER = (240, 233, 218, 255)
GOLD = (222, 186, 92, 255)
HUES = {"hp1": 0.00, "hp2": 0.38, "hp3": 0.62}  # the shelf's spines: scarlet, green, blue-violet


def binding(h, v=0.50, s=0.55):
    r, g, b = colorsys.hsv_to_rgb(h, s, v)
    return int(r * 255), int(g * 255), int(b * 255), 255


def numeral(d, n, cx, top, bottom, stem, k):
    """n serifed stems ("I", "II", "III") centred on cx between top and bottom"""
    gap = stem * 1.1
    serif = stem * 0.55
    width = n * stem + (n - 1) * gap
    x = cx - width / 2
    for _ in range(n):
        d.rectangle([x, top, x + stem, bottom], fill=GOLD)
        for y in (top, bottom - max(1, 3 * k)):
            d.rectangle([x - serif, y, x + stem + serif, y + max(1, 3 * k)], fill=GOLD)
        x += stem + gap


def shelf(size):
    k = size / 256
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, size - 1, size - 1], radius=int(40 * k), fill=WALL)
    floor = 214 * k
    books = [(HUES["hp1"], 44, 150, 1), (HUES["hp2"], 100, 166, 2), (HUES["hp3"], 156, 140, 3)]
    for hue, x, h, n in books:
        x0, x1 = x * k, (x + 50) * k
        top = floor - h * k
        d.rounded_rectangle([x0, top, x1, floor], radius=max(1, int(4 * k)), fill=binding(hue))
        band = binding(hue, 0.30, 0.45)
        for y in (top + 14 * k, floor - 22 * k):
            d.rectangle([x0, y, x1, y + 5 * k], fill=band)
        if size >= 48:  # the volume numbers only where they can be read
            mid = (top + floor) / 2
            numeral(d, n, (x0 + x1) / 2, mid - 18 * k, mid + 18 * k, 5 * k, k)
    d.rectangle([24 * k, floor, 232 * k, floor + 7 * k], fill=PAPER)
    return im


def book(size, hue, n):
    k = size / 256
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, size - 1, size - 1], radius=int(40 * k), fill=WALL)
    x0, y0, x1, y1 = 52 * k, 26 * k, 212 * k, 222 * k
    # the page block peeking out at the fore-edge and foot, then the board
    d.rounded_rectangle([x0 + 8 * k, y0 + 8 * k, x1 + 6 * k, y1 + 6 * k], radius=max(1, int(6 * k)), fill=PAPER)
    d.rounded_rectangle([x0, y0, x1, y1], radius=max(1, int(8 * k)), fill=binding(hue))
    # the spine's hinge down the left, and the shelf's bands across it
    dark = binding(hue, 0.30, 0.45)
    d.rectangle([x0, y0 + 4 * k, x0 + 22 * k, y1 - 4 * k], fill=dark)
    for y in (y0 + 22 * k, y1 - 30 * k):
        d.rectangle([x0, y, x1, y + 8 * k], fill=dark)
    # gold label frame and the volume number
    lx0, ly0, lx1, ly1 = x0 + 46 * k, y0 + 56 * k, x1 - 22 * k, y1 - 64 * k
    if size >= 32:
        line = max(1, int(5 * k))
        d.rectangle([lx0, ly0, lx1, ly1], outline=GOLD, width=line)
    stem = 14 * k if n < 3 else 12 * k
    numeral(d, n, (lx0 + lx1) / 2, ly0 + 18 * k, ly1 - 18 * k, max(1.5, stem), k)
    return im


def draw(kind, size):
    return shelf(size) if kind == "shelf" else book(size, HUES[kind], int(kind[-1]))


def main():
    kind = sys.argv[1] if len(sys.argv) > 1 else "shelf"
    if kind != "shelf" and kind not in HUES:
        sys.exit("usage: make_icon.py shelf|hp1|hp2|hp3 [out.ico] [preview.png]")
    out = Path(sys.argv[2] if len(sys.argv) > 2 else Path(__file__).resolve().parent.parent / "res" / "app.ico")
    sizes = [16, 24, 32, 48, 64, 128, 256]
    # each size drawn on its own grid (not scaled down from 256), so the small ones stay crisp
    frames = {s: draw(kind, s * 4).resize((s, s), Image.LANCZOS) for s in sizes}
    frames[256].save(out, format="ICO", sizes=[(s, s) for s in sizes], append_images=[frames[s] for s in sizes[:-1]])
    if len(sys.argv) > 3:
        frames[256].save(sys.argv[3])


if __name__ == "__main__":
    main()
