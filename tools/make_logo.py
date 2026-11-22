"""Draws the Coreplay logo (mark + wordmark) in three lockups, light-on-dark and
dark-on-light, and a review sheet. Monochrome like the app: no accent colours.

  A  horizontal: the mark, then "Coreplay" (Segoe UI Variable, semibold, tight)
  B  integrated: the mark is the "C" of the word, then "oreplay"
  C  stacked:    the mark above "COREPLAY" (Bahnschrift, light, wide tracking)

Writes res/logo/*.png (transparent background) and res/logo/logo_sheet.png.
Also writes res/logo/icon_taskbar_preview.png: the current icon beside a version whose
ring fills more of the tile, at taskbar sizes, for review (the .ico is not changed).
Run: python -I tools/make_logo.py
"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "res", "logo")
FONTS = r"C:\Windows\Fonts"
SS = 4  # supersampling


def font(name, size, variation=None):
    f = ImageFont.truetype(os.path.join(FONTS, name), size)
    if variation:
        f.set_variation_by_name(variation)
    return f


def mark(size, ring=(212, 212, 212, 255), play=(238, 238, 238, 255)):
    """The C ring open on the right, with a rounded play triangle inside; transparent."""
    S = size * SS
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    u = S / 640.0  # ring outer radius 300 + margin in a 640 box
    cx = cy = 320
    B = lambda x0, y0, x1, y1: [x0 * u, y0 * u, x1 * u, y1 * u]  # noqa: E731
    r, w = 262, 76
    d.arc(B(cx - r - 19 + 19, cy - r, cx + r, cy + r), start=44, end=316, fill=ring, width=int(w * u))
    for a in (44, 316):
        rad = math.radians(a)
        rr = r - w / 2
        px, py = cx + rr * math.cos(rad), cy + rr * math.sin(rad)
        d.ellipse(B(px - w / 2 + 1, py - w / 2 + 1, px + w / 2 - 1, py + w / 2 - 1), fill=ring)
    rc = 22
    tri = [(cx - 62, cy - 124), (cx - 62, cy + 124), (cx + 132, cy)]
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
    d.polygon([(x * u, y * u) for x, y in inset], fill=play)
    d.line([(x * u, y * u) for x, y in inset + [inset[0]]], fill=play, width=int(2 * rc * u))
    for x, y in inset:
        d.ellipse(B(x - rc, y - rc, x + rc, y + rc), fill=play)
    return img.resize((size, size), Image.LANCZOS)


def text_img(text, f, fill, tracking=0):
    """Text on a transparent image cropped to its ink, with extra letter spacing (px)."""
    probe = ImageDraw.Draw(Image.new("RGBA", (1, 1)))
    widths = [probe.textlength(ch, font=f) for ch in text]
    w = int(sum(widths) + tracking * (len(text) - 1) + f.size)
    h = int(f.size * 1.6)
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    if tracking:
        x = f.size * 0.2
        for ch, cw in zip(text, widths):
            d.text((x, f.size * 0.2), ch, font=f, fill=fill)
            x += cw + tracking
    else:
        d.text((f.size * 0.2, f.size * 0.2), text, font=f, fill=fill)
    return img.crop(img.getbbox())


def lockup(kind, dark):
    ink = (232, 232, 232, 255) if dark else (24, 24, 24, 255)
    ring = (212, 212, 212, 255) if dark else (40, 40, 40, 255)
    play = (238, 238, 238, 255) if dark else (24, 24, 24, 255)
    H = 360 * SS // 2  # cap height scale
    if kind == "A":
        word = text_img("Coreplay", font("SegUIVar.ttf", int(H * 1.0), b"Semibold Display"), ink, tracking=-H * 0.01)
        m = mark(int(word.height * 1.55), ring, play)
        gap = int(word.height * 0.42)
        img = Image.new("RGBA", (m.width + gap + word.width, max(m.height, word.height)), (0, 0, 0, 0))
        img.alpha_composite(m, (0, (img.height - m.height) // 2))
        # optical: the x-height centre of the word sits on the mark's centre line
        img.alpha_composite(word, (m.width + gap, (img.height - word.height) // 2 + int(word.height * 0.02)))
    elif kind == "B":
        f = font("SegUIVar.ttf", int(H * 1.0), b"Semibold Display")
        word = text_img("oreplay", f, ink, tracking=-H * 0.01)
        cap = text_img("C", f, ink)  # the mark takes the C's place, a little larger than a capital
        m = mark(int(cap.height * 1.45), ring, play)
        gap = int(cap.height * 0.06)
        base = m.height
        img = Image.new("RGBA", (m.width + gap + word.width, base + word.height), (0, 0, 0, 0))
        y_mark = 0
        # the word's x-height band centred on the mark, descender (p, y) below
        xh_top = word.height - cap.height  # rough: lowercase without ascender over cap height
        img.alpha_composite(m, (0, y_mark))
        y_word = y_mark + m.height // 2 - int(cap.height * 0.36) - max(0, xh_top) // 4
        img.alpha_composite(word, (m.width + gap, y_word))
        img = img.crop(img.getbbox())
    else:  # "C" stacked
        word = text_img("COREPLAY", font("bahnschrift.ttf", int(H * 0.62), b"Light"), ink, tracking=H * 0.24)
        m = mark(int(word.width * 0.42), ring, play)
        gap = int(word.height * 1.1)
        img = Image.new("RGBA", (max(m.width, word.width), m.height + gap + word.height), (0, 0, 0, 0))
        img.alpha_composite(m, ((img.width - m.width) // 2, 0))
        img.alpha_composite(word, ((img.width - word.width) // 2, m.height + gap))
    return img.resize((img.width // SS, img.height // SS), Image.LANCZOS)


def icon_tile(size, ring_scale):
    """The app icon: dark rounded tile + mark, the mark at ring_scale of the tile."""
    S = size * SS
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    pad = S * 40 / 1024
    d.rounded_rectangle([pad, pad, S - pad, S - pad], radius=S * 212 / 1024, fill=(17, 17, 17, 255),
                        outline=(70, 70, 70, 255), width=max(1, int(S * 9 / 1024)))
    m = mark(int(S * ring_scale) // SS * SS)
    m = m.resize((int(S * ring_scale), int(S * ring_scale)), Image.LANCZOS)
    img.alpha_composite(m, ((S - m.width) // 2 - int(S * 0.01), (S - m.height) // 2))
    return img.resize((size, size), Image.LANCZOS)


def main():
    os.makedirs(OUT, exist_ok=True)
    made = {}
    for kind in "ABC":
        for dark in (True, False):
            img = lockup(kind, dark)
            name = f"coreplay_logo_{kind}_{'dark' if dark else 'light'}.png"
            img.save(os.path.join(OUT, name))
            made[(kind, dark)] = img
    # Review sheet: each lockup on dark and on light.
    W, row = 2400, 520
    sheet = Image.new("RGBA", (W, row * 3), (255, 255, 255, 255))
    d = ImageDraw.Draw(sheet)
    label = font("segoeui.ttf", 26)
    names = {"A": "A  mark + wordmark", "B": "B  mark as the C", "C": "C  stacked"}
    for i, kind in enumerate("ABC"):
        y = i * row
        d.rectangle([0, y, W // 2, y + row], fill=(20, 20, 20, 255))
        for j, dark in enumerate((True, False)):
            img = made[(kind, dark)]
            scale = min((W // 2 - 160) / img.width, (row - 160) / img.height)
            im = img.resize((int(img.width * scale), int(img.height * scale)), Image.LANCZOS)
            x0 = j * (W // 2)
            sheet.alpha_composite(im, (x0 + (W // 2 - im.width) // 2, y + (row - im.height) // 2))
        d.text((16, y + 12), names[kind], font=label, fill=(150, 150, 150, 255))
    sheet.save(os.path.join(OUT, "logo_sheet.png"))

    # Taskbar check: the current icon (ring ~59% of the tile) and a fuller one (~80%).
    bar = Image.new("RGBA", (760, 260), (32, 32, 32, 255))
    db = ImageDraw.Draw(bar)
    small = font("segoeui.ttf", 18)
    for r, (title, scale) in enumerate((("current", 0.59), ("proposed: fuller ring", 0.80))):
        y = 30 + r * 115
        db.text((16, y + 30), title, font=small, fill=(180, 180, 180, 255))
        x = 230
        for s in (24, 32, 48, 64):
            bar.alpha_composite(icon_tile(s, scale), (x, y + (64 - s) // 2 + 10))
            x += s + 40
    bar.save(os.path.join(OUT, "icon_taskbar_preview.png"))
    print("wrote", os.path.normpath(OUT))


if __name__ == "__main__":
    sys.exit(main())
