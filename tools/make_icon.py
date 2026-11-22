"""Draws the Coreplay icon and writes res/coreplay.ico (16-256 px) and res/coreplay.png.

Design: the logo's mark (a light-grey disc with the play triangle cut out as negative
space; the triangle's tip breaks through the right edge, and that break is the C) on a
black rounded-square tile that fills the icon. The mark is as large as the tile allows.
A thin grey edge on the tile keeps it visible on a dark taskbar. Monochrome like the app.
The shape comes from tools/make_logo_cs.py (cut_mark) so the icon and the logo stay the same.
Run: python -I tools/make_icon.py
"""
import os
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from make_logo_cs import cut_mark  # noqa: E402

OUT = os.path.join(HERE, "..", "res")
TILE = (0, 0, 0)        # the black background
EDGE = (64, 64, 64)     # the tile's thin edge (reads on a dark taskbar)
RADIUS = 0.22           # tile corner radius / size
MARK = 0.80             # the disc's diameter / the icon's size
DISC = 222              # disc grey


def draw(size):
    S = size * 4
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    edge = max(4, round(S * 0.012))
    r = round(S * RADIUS)
    d.rounded_rectangle([0, 0, S - 1, S - 1], radius=r, fill=EDGE + (255,))
    d.rounded_rectangle([edge, edge, S - 1 - edge, S - 1 - edge], radius=max(1, r - edge), fill=TILE + (255,))
    # cut_mark's disc is 600/640 of its box.
    box = int(round(S * MARK * 640 / 600))
    m = cut_mark(box, 0.78, apex=1.20, stretch=1.15)
    disc = Image.new("RGBA", (box, box), (DISC, DISC, DISC, 255))
    off = (S - box) // 2
    img.paste(disc, (off, off), m)
    return img.resize((size, size), Image.LANCZOS)


def main(preview_only=False):
    sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
    frames = [draw(s) for s in sizes]
    # Preview: every size on dark and light backgrounds.
    sheet = Image.new("RGBA", (1100, 600), (255, 255, 255, 255))
    ImageDraw.Draw(sheet).rectangle([0, 0, 1100, 300], fill=(32, 32, 32, 255))
    x = 20
    for s, f in zip(sizes, frames):
        sheet.alpha_composite(f, (x, 150 - s // 2))
        sheet.alpha_composite(f, (x, 450 - s // 2))
        x += s + 22
    sheet.save(os.path.join(OUT, "icon_preview.png"))
    if preview_only:
        return 0
    os.makedirs(OUT, exist_ok=True)
    draw(1024).save(os.path.join(OUT, "coreplay.png"))
    frames[-1].save(os.path.join(OUT, "coreplay.ico"), sizes=[(s, s) for s in sizes], append_images=frames[:-1])
    print("wrote", os.path.normpath(OUT))
    return 0


if __name__ == "__main__":
    sys.exit(main(preview_only="--preview" in sys.argv))
