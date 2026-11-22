# 8 flat patches for the LUT tests: greys 100 (18% grey in ARRI LogC3), 128, 200,
# skin, red, green, blue, white. Usage: python -I make_patches.py <out.png>
import sys
from PIL import Image, ImageDraw
P = [(100, 100, 100), (128, 128, 128), (200, 200, 200), (200, 150, 120),
     (180, 60, 50), (60, 160, 80), (50, 80, 180), (235, 235, 235)]
img = Image.new("RGB", (1280, 720))
d = ImageDraw.Draw(img)
for i, c in enumerate(P):
    x, y = (i % 4) * 320, (i // 4) * 360
    d.rectangle([x, y, x + 319, y + 359], fill=c)
img.save(sys.argv[1])
