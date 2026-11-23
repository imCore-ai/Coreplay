"""Coreplay logo in the Core Studio letterforms: the mark, then "Core" exactly as in the
Core Studio logo, then "play" built from Core Studio's own "Studio" letters (the same
bold-Core + light-second-word pairing):
  p = the "d" turned 180 degrees     a = the "d" cut at the x-height
  l = the "i" stem at ascender height y = the "u" with a descender hook
Source: res/logo/source_corestudio_w_logo.png (Core Studio's white logo, 2075 x 340).
Writes res/logo/coreplay_logo_CS_{dark,light}.png and res/logo/logo_CS.png.
Run: python -I tools/make_logo_cs.py
"""
import os
import sys

from PIL import Image, ImageChops, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_logo import OUT  # noqa: E402
from make_logo_a import mark_masks, paint  # noqa: E402

SRC = os.path.join(OUT, "source_corestudio_w_logo.png")
BASE, XTOP, ASC = 293, 73, 0   # baseline, x-height top, ascender top (source pixels)
STEM = 50                      # the i stem width (the l)
SCALE = 3                      # work at 3x the source size for clean edges


def letters():
    a = Image.open(SRC).getchannel("A")
    a = a.resize((a.width * SCALE, a.height * SCALE), Image.LANCZOS)
    s = lambda v: v * SCALE  # noqa: E731
    H = s(420)  # canvas height: room for descenders
    def crop(x0, x1, y0=0, y1=340):
        g = Image.new("L", (s(x1 - x0 + 1), H), 0)
        g.paste(a.crop((s(x0), s(y0), s(x1 + 1), s(y1))), (0, s(y0)))
        return g
    core = crop(0, 898)
    d = crop(1533, 1764)
    i = crop(1787, 1836)
    u = crop(1317, 1513)
    # p: the d turned 180 degrees, its bowl between the baseline and the x-height
    dd = a.crop((s(1533), s(ASC), s(1765), s(BASE + 1)))
    p = Image.new("L", (dd.width, H), 0)
    p.paste(dd.rotate(180), (0, s(XTOP)))
    # a: the d below the x-height only (single-storey a with a flat-topped stem). Only the
    # STEM is cut at the x-height: the round bowl rises a little above it (its overshoot, to
    # row 66 in the source) and keeps that, or its top is cut flat. The stem starts at source
    # column 1731; the bowl's curve is below the x-height from column 1688 on.
    aa = Image.new("L", d.size, 0)
    split = s(1730 - 1533)
    aa.paste(d.crop((0, s(XTOP - 10), split, H)), (0, s(XTOP - 10)))
    aa.paste(d.crop((split, s(XTOP), d.width, H)), (split, s(XTOP)))
    # l: the i stem (no dot), from the ascender to the baseline
    stem = a.crop((s(1787), s(XTOP + 4), s(1837), s(BASE + 1)))
    l = Image.new("L", (stem.width, H), 0)
    l.paste(stem.resize((stem.width, s(BASE + 1 - ASC))), (0, s(ASC)))
    # y: the u, its right stem carried down into a hook to the left
    y = Image.new("L", (u.width + s(10), H), 0)
    y.paste(u, (0, 0))
    dr = ImageDraw.Draw(y)
    sw = s(35)                          # the light letters' stroke (measured on the u)
    xr = s(1478 - 1317)                 # the u's right stem, left edge
    depth = s(BASE + 74)                # the same depth as the p
    r = s(60)                           # hook radius at the stroke's centre line
    xc = xr + sw / 2
    yb = depth - r - sw / 2             # where the stem turns
    R = r + sw / 2                      # outer radius (PIL draws the width inward)
    dr.rectangle([xr, s(BASE - 40), xr + sw - 1, yb], fill=255)
    dr.arc([xc - r - R, yb - R, xc - r + R, yb + R], start=0, end=90, fill=255, width=sw)
    dr.rectangle([xc - r - s(22), yb + r - sw / 2, xc - r, yb + r + sw / 2 - 1], fill=255)
    return core, [p, l, aa, y]


def wordmark():
    core, play = letters()
    gaps = [58, 20, 20, 24]  # Core | p | l | a | y (source pixels)
    W = core.width + sum(g.width for g in play) + sum(gaps) * SCALE + 10
    img = Image.new("L", (W, core.height), 0)
    img.paste(core, (0, 0))
    x = core.width
    for g, gap in zip(play, gaps):
        x += gap * SCALE
        img = ImageChops.lighter(img, _at(g, x, img.size))
        x += g.width
    return img.crop((0, 0, x, img.height))


def _at(g, x, size):
    out = Image.new("L", size, 0)
    out.paste(g, (x, 0))
    return out


def lockup(dark):
    ink = (236, 236, 236, 255) if dark else (22, 22, 22, 255)
    ringc = (212, 212, 212, 255) if dark else (34, 34, 34, 255)
    word = wordmark()
    # The mark: as tall as the Core Studio "C" (y 2..339), centred on it.
    cap = 337 * SCALE
    S = int(cap * 1.12 / 0.94)  # the ring's outer diameter is 0.94 of its box
    ring, play = mark_masks(S, w=76 if dark else 86, tri_scale=1.25)
    m = paint(ring, ringc)
    m.alpha_composite(paint(play, ink))
    gap = int(cap * 0.16)
    cy = (2 + 339) / 2 * SCALE
    top = int(min(0, cy - S / 2))
    img = Image.new("RGBA", (S + gap + word.width, int(max(word.height, cy + S / 2) - top)), (0, 0, 0, 0))
    img.alpha_composite(m, (0, int(cy - S / 2 - top)))
    img.alpha_composite(paint(word, ink), (S + gap, -top))
    img = img.crop(img.getbbox())
    return img.resize((img.width // 2, img.height // 2), Image.LANCZOS)


def main():
    imgs = []
    for dark in (True, False):
        im = lockup(dark)
        im.save(os.path.join(OUT, f"coreplay_logo_CS_{'dark' if dark else 'light'}.png"))
        imgs.append(im)
    W, H = 2400, 420
    sheet = Image.new("RGBA", (W, H), (255, 255, 255, 255))
    ImageDraw.Draw(sheet).rectangle([0, 0, W // 2, H], fill=(20, 20, 20, 255))
    for j, im in enumerate(imgs):
        k = min((W // 2 - 220) / im.width, (H - 150) / im.height)
        r = im.resize((int(im.width * k), int(im.height * k)), Image.LANCZOS)
        sheet.alpha_composite(r, (j * (W // 2) + (W // 2 - r.width) // 2, (H - r.height) // 2))
    sheet.save(os.path.join(OUT, "logo_CS.png"))
    print("wrote", os.path.normpath(OUT))


if __name__ == "__main__":
    sys.exit(main())


# ---------------------------------------------------------------- the "cut C" mark
def cut_mark(S, h_ratio=0.55, apex=1.12, stretch=1.0):
    """A solid disc with the play triangle cut out as negative space; the triangle's tip
    breaks through the right edge, and that break is the C's opening. h_ratio: the
    triangle's half height / the radius; apex: where its tip is, in radii from the centre."""
    from make_logo_a import triangle
    big = S * 4
    u = big / 640.0
    R, cx, cy = 300, 320, 320
    m = Image.new("L", (big, big), 0)
    d = ImageDraw.Draw(m)
    d.ellipse([(cx - R) * u, (cy - R) * u, (cx + R) * u, (cy + R) * u], fill=255)
    s = h_ratio * R / 124.0                       # the play triangle's own proportions
    x_apex = cx + apex * R
    cxt = x_apex - 132 * s                        # triangle() places its tip at cx + 132 s
    rc = 20 * s
    pts = triangle(cxt, cy, s, rc)
    # stretch: wider (to the left) with the tip kept where it is, so the opening stays the same
    pts = [(x_apex - (x_apex - x) * stretch, y) for x, y in pts]
    cut = Image.new("L", (big, big), 0)
    dc = ImageDraw.Draw(cut)
    dc.polygon([(x * u, y * u) for x, y in pts], fill=255)
    dc.line([(x * u, y * u) for x, y in pts + [pts[0]]], fill=255, width=int(2 * rc * u))
    for x, y in pts:
        dc.ellipse([(x - rc) * u, (y - rc) * u, (x + rc) * u, (y + rc) * u], fill=255)
    m = ImageChops.subtract(m, cut)
    return m.resize((S, S), Image.LANCZOS)


def cut_sheet():
    """The cut-C mark in three triangle sizes (dark and light), then in the lockup."""
    sizes = [("small", 0.45), ("medium", 0.55), ("large", 0.66)]
    W, row = 2400, 520
    sheet = Image.new("RGBA", (W, row * 2), (255, 255, 255, 255))
    d = ImageDraw.Draw(sheet)
    d.rectangle([0, 0, W, row], fill=(20, 20, 20, 255))
    from make_logo import font
    lab = font("segoeui.ttf", 26)
    for i, (name, h) in enumerate(sizes):
        m = cut_mark(380, h)
        for j, (bg_y, colour) in enumerate(((0, (236, 236, 236, 255)), (row, (22, 22, 22, 255)))):
            sheet.alpha_composite(paint(m, colour), (i * 800 + 210, bg_y + 70))
            d.text((i * 800 + 20, bg_y + 14), f"triangle {name}", font=lab, fill=(150, 150, 150, 255))
    sheet.save(os.path.join(OUT, "mark_cut_sizes.png"))

    # The lockup with the medium cut mark and the Core Studio wordmark.
    word = wordmark()
    imgs = []
    for dark in (True, False):
        ink = (236, 236, 236, 255) if dark else (22, 22, 22, 255)
        cap = 337 * SCALE
        S = int(cap * 1.3)  # the mark, larger than the word's C
        m = paint(cut_mark(S, 0.78, apex=1.20, stretch=1.15), ink)  # a large triangle, a little wider, moved right
        gap = int(cap * 0.16)
        cyw = (2 + 339) / 2 * SCALE
        top = int(min(0, cyw - S / 2))
        img = Image.new("RGBA", (S + gap + word.width, int(max(word.height, cyw + S / 2) - top)), (0, 0, 0, 0))
        img.alpha_composite(m, (0, int(cyw - S / 2 - top)))
        img.alpha_composite(paint(word, ink), (S + gap, -top))
        img = img.crop(img.getbbox())
        img = img.resize((img.width // 2, img.height // 2), Image.LANCZOS)
        img.save(os.path.join(OUT, f"coreplay_logo_CUT_{'dark' if dark else 'light'}.png"))
        imgs.append(img)
    W, H = 2400, 420
    sh = Image.new("RGBA", (W, H), (255, 255, 255, 255))
    ImageDraw.Draw(sh).rectangle([0, 0, W // 2, H], fill=(20, 20, 20, 255))
    for j, im in enumerate(imgs):
        k = min((W // 2 - 220) / im.width, (H - 150) / im.height)
        r = im.resize((int(im.width * k), int(im.height * k)), Image.LANCZOS)
        sh.alpha_composite(r, (j * (W // 2) + (W // 2 - r.width) // 2, (H - r.height) // 2))
    sh.save(os.path.join(OUT, "logo_CUT.png"))
