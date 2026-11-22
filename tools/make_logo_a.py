"""More refined takes on logo A (mark beside the word), monochrome. Writes
res/logo/coreplay_logo_A<n>_{dark,light}.png and res/logo/logo_sheet_A.png.

  A1 two-weight   "Core" semibold + "play" light, the standard mark
  A2 tile         the app tile as the mark, wordmark, spaced MEDIA PLAYER line
  A3 monoline     thin outline mark, a hairline divider, light lowercase, wide spacing
  A4 bold         heavy C, strong spaced uppercase
  A5 tonal        grey gradients in the ring and on "play"
Run: python -I tools/make_logo_a.py
"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_logo import OUT, SS, font  # noqa: E402

BIG = 1  # all drawing is at SS x, then scaled down once


def gradient(w, h, a, b, angle=45):
    """A grey gradient from a (start) to b (end) along the angle (degrees)."""
    g = Image.new("L", (w, h))
    px = g.load()
    ca, sa = math.cos(math.radians(angle)), math.sin(math.radians(angle))
    span = abs(w * ca) + abs(h * sa) or 1
    for y in range(h):
        for x in range(w):
            t = ((x * ca + y * sa) - min(0, w * ca) - min(0, h * sa)) / span
            px[x, y] = int(a + (b - a) * max(0.0, min(1.0, t)))
    return g


def paint(mask, fill):
    """An RGBA image: fill (a grey value, an RGBA tuple, or an L gradient image) through mask."""
    out = Image.new("RGBA", mask.size, (0, 0, 0, 0))
    if isinstance(fill, Image.Image):
        grey = fill.resize(mask.size)
        layer = Image.merge("RGBA", (grey, grey, grey, mask))
        return layer
    out.paste(fill, (0, 0), mask)
    return out


def triangle(cx, cy, scale=1.0, rc=22):
    tri = [(cx - 62 * scale, cy - 124 * scale), (cx - 62 * scale, cy + 124 * scale), (cx + 132 * scale, cy)]
    inset = []
    for i, (x, y) in enumerate(tri):
        ax, ay = tri[i - 1]
        bx, by = tri[(i + 1) % 3]
        v1, v2 = (ax - x, ay - y), (bx - x, by - y)
        n1, n2 = math.hypot(*v1), math.hypot(*v2)
        u1, u2 = (v1[0] / n1, v1[1] / n1), (v2[0] / n2, v2[1] / n2)
        half = math.acos(max(-1.0, min(1.0, u1[0] * u2[0] + u1[1] * u2[1]))) / 2
        bis = (u1[0] + u2[0], u1[1] + u2[1])
        nb = math.hypot(*bis)
        dist = rc / math.sin(half)
        inset.append((x + bis[0] / nb * dist, y + bis[1] / nb * dist))
    return inset


def mark_masks(S, w=76, tri_scale=1.0, outline=0):
    """Masks (L) of the ring and of the play triangle, in an S x S box (ring outer = 0.47 S)."""
    u = S / 640.0
    cx = cy = 320
    r = 300 - w / 2
    ring = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(ring)
    d.arc([(cx - r - w / 2) * u, (cy - r - w / 2) * u, (cx + r + w / 2) * u, (cy + r + w / 2) * u],
          start=44, end=316, fill=255, width=int(w * u))
    for a in (44, 316):
        rad = math.radians(a)
        px, py = cx + r * math.cos(rad), cy + r * math.sin(rad)
        d.ellipse([(px - w / 2) * u, (py - w / 2) * u, (px + w / 2) * u, (py + w / 2) * u], fill=255)
    play = Image.new("L", (S, S), 0)
    dp = ImageDraw.Draw(play)
    rc = 22 if not outline else outline / 2
    pts = triangle(cx + 6, cy, tri_scale, rc)
    if outline:
        dp.line([(x * u, y * u) for x, y in pts + [pts[0]]], fill=255, width=int(outline * u), joint="curve")
        for x, y in pts:
            dp.ellipse([(x - rc) * u, (y - rc) * u, (x + rc) * u, (y + rc) * u], fill=255)
    else:
        dp.polygon([(x * u, y * u) for x, y in pts], fill=255)
        dp.line([(x * u, y * u) for x, y in pts + [pts[0]]], fill=255, width=int(2 * rc * u))
        for x, y in pts:
            dp.ellipse([(x - rc) * u, (y - rc) * u, (x + rc) * u, (y + rc) * u], fill=255)
    return ring, play


def words(runs, base_size):
    """Text runs [(text, font, fill, tracking_em)] on one baseline. Returns (image, cap height,
    baseline y) with the image cropped horizontally only (so the baseline stays known)."""
    W = int(base_size * 12)
    H = int(base_size * 2.2)
    base = int(base_size * 1.4)
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    x = base_size * 0.2
    for text, f, fill, track in runs:
        for ch in text:
            m = Image.new("L", (W, H), 0)
            ImageDraw.Draw(m).text((x, base), ch, font=f, anchor="ls", fill=255)
            img.alpha_composite(paint(m, fill))
            x += ImageDraw.Draw(m).textlength(ch, font=f) + track * f.size
    cap = base - runs[0][1].getbbox("H", anchor="ls")[1] - base  # bbox top is negative above baseline
    cap = -runs[0][1].getbbox("H", anchor="ls")[1]
    bb = img.getbbox()
    return img.crop((bb[0], 0, bb[2], H)), cap, base


def compose(mark_img, word_img, cap, base, gap, extra=None, divider=None):
    """Mark left, its centre on the middle of the capitals; word right of it."""
    mh = mark_img.height
    cy_word = base - cap / 2
    top = int(min(0, cy_word - mh / 2))
    H = int(max(word_img.height, cy_word + mh / 2) - top)
    W = mark_img.width + gap + word_img.width + (divider[2] if divider else 0)
    img = Image.new("RGBA", (W + 4, H + (extra.height * 3 if extra else 0)), (0, 0, 0, 0))
    img.alpha_composite(mark_img, (0, int(cy_word - mh / 2 - top)))
    x = mark_img.width + gap
    if divider:
        colour, height, width = divider
        d = ImageDraw.Draw(img)
        d.rectangle([x - gap // 2 - 1, cy_word - height / 2 - top, x - gap // 2 + 1 + SS, cy_word + height / 2 - top], fill=colour)
        x += width
    img.alpha_composite(word_img, (x, -top))
    if extra:
        img.alpha_composite(extra, (x + int(cap * 0.04), int(base - top + cap * 0.55)))
    return img.crop(img.getbbox())


def lockup(n, dark):
    ink = (236, 236, 236, 255) if dark else (22, 22, 22, 255)
    soft = (150, 150, 150, 255) if dark else (110, 110, 110, 255)
    ringc = (212, 212, 212, 255) if dark else (34, 34, 34, 255)
    size = 300 * SS  # font size
    semi = font("SegUIVar.ttf", size, b"Semibold Display")
    light = font("SegUIVar.ttf", size, b"Light Display")

    if n == 1:  # two weights
        # Dark on light looks thinner than light on dark (the eye spreads light over dark),
        # so the light-background version is drawn a little heavier.
        if not dark:
            semi = font("SegUIVar.ttf", size)
            semi.set_variation_by_axes([650, 36])
            light = font("SegUIVar.ttf", size)
            light.set_variation_by_axes([350, 36])
        word, cap, base = words([("Core", semi, ink, -0.012), ("play", light, ink, -0.004)], size)
        S = int(cap * 1.95)
        ring, play = mark_masks(S, w=76 if dark else 86, tri_scale=1.25)  # a larger play triangle
        m = paint(ring, ringc)
        m.alpha_composite(paint(play, ink))
        return compose(m, word, cap, base, int(cap * 0.2))

    if n == 2:  # tile + tagline
        word, cap, base = words([("Coreplay", semi, ink, -0.012)], size)
        T = int(cap * 2.05)
        tile = Image.new("RGBA", (T, T), (0, 0, 0, 0))
        tile_fill = (236, 236, 236, 255) if dark else (22, 22, 22, 255)
        on_tile = (24, 24, 24, 255) if dark else (232, 232, 232, 255)
        ImageDraw.Draw(tile).rounded_rectangle([0, 0, T - 1, T - 1], radius=T * 0.23, fill=tile_fill)
        ring, play = mark_masks(int(T * 0.86))
        inner = paint(ring, on_tile)
        inner.alpha_composite(paint(play, on_tile))
        tile.alpha_composite(inner, ((T - inner.width) // 2 - int(T * 0.01), (T - inner.height) // 2))
        tag_font = font("bahnschrift.ttf", int(cap * 0.30), b"SemiLight")
        tag, _, _ = words([("MEDIA PLAYER", tag_font, soft, 0.42)], int(cap * 0.30))
        tag = tag.crop(tag.getbbox())
        img = compose(tile, word, cap, base, int(cap * 0.55))
        # the tagline under the word, its left edge on the word's
        out = Image.new("RGBA", (img.width, img.height + int(tag.height * 2.4)), (0, 0, 0, 0))
        out.alpha_composite(img, (0, 0))
        out.alpha_composite(tag, (T + int(cap * 0.55) + int(cap * 0.03), img.height + int(cap * 0.02)))
        return out.crop(out.getbbox())

    if n == 3:  # monoline
        thin = font("SegUIVar.ttf", size, b"Light Display")
        word, cap, base = words([("coreplay", thin, ink, 0.07)], size)
        cap = -thin.getbbox("x", anchor="ls")[1] * 1.35  # lowercase: centre on the x-height band
        S = int(cap * 2.1)
        ring, play = mark_masks(S, w=30, tri_scale=0.92, outline=26)
        m = paint(ring, ringc)
        m.alpha_composite(paint(play, ink))
        return compose(m, word, cap, base, int(cap * 0.9), divider=(soft, cap * 1.6, int(cap * 0.2)))

    if n == 4:  # bold
        heavy = font("bahnschrift.ttf", size, b"SemiBold")
        word, cap, base = words([("COREPLAY", heavy, ink, 0.10)], size)
        S = int(cap * 1.75)
        ring, play = mark_masks(S, w=124, tri_scale=0.92)
        m = paint(ring, ink)
        m.alpha_composite(paint(play, ink))
        return compose(m, word, cap, base, int(cap * 0.5))

    # 5: tonal
    g_play = gradient(400, 100, 200 if dark else 70, 120 if dark else 150, angle=0)
    word, cap, base = words([("Core", semi, ink, -0.012), ("play", semi, g_play, -0.012)], size)
    S = int(cap * 1.95)
    ring, play = mark_masks(S)
    m = paint(ring, gradient(S // 8, S // 8, 245 if dark else 20, 115 if dark else 140, angle=45))
    m.alpha_composite(paint(play, ink))
    return compose(m, word, cap, base, int(cap * 0.5))


def main():
    os.makedirs(OUT, exist_ok=True)
    made = {}
    for n in range(1, 6):
        for dark in (True, False):
            img = lockup(n, dark)
            img = img.resize((img.width // SS, img.height // SS), Image.LANCZOS)
            img.save(os.path.join(OUT, f"coreplay_logo_A{n}_{'dark' if dark else 'light'}.png"))
            made[(n, dark)] = img
    W, row = 2400, 400
    sheet = Image.new("RGBA", (W, row * 5), (255, 255, 255, 255))
    d = ImageDraw.Draw(sheet)
    label = font("segoeui.ttf", 24)
    names = {1: "A1  two weights", 2: "A2  tile + tagline", 3: "A3  monoline", 4: "A4  bold", 5: "A5  tonal"}
    for i in range(1, 6):
        y = (i - 1) * row
        d.rectangle([0, y, W // 2, y + row], fill=(20, 20, 20, 255))
        for j, dark in enumerate((True, False)):
            img = made[(i, dark)]
            scale = min((W // 2 - 220) / img.width, (row - 150) / img.height)
            im = img.resize((int(img.width * scale), int(img.height * scale)), Image.LANCZOS)
            sheet.alpha_composite(im, (j * (W // 2) + (W // 2 - im.width) // 2, y + (row - im.height) // 2))
        d.text((16, y + 12), names[i], font=label, fill=(150, 150, 150, 255))
        d.line([0, y, W, y], fill=(90, 90, 90, 255))
    sheet.save(os.path.join(OUT, "logo_sheet_A.png"))
    print("wrote", os.path.normpath(OUT))


if __name__ == "__main__":
    sys.exit(main())
