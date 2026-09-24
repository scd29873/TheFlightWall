#!/usr/bin/env python3
"""Turn the preview's raw RGB565 frames into PNGs that look like the panel.

Each LED is drawn as a round dot on black at SCALE px per LED, with faint
seams every 64 columns so the four modules of a 4x1 row are visible. Writes
<name>.png beside each <name>.rgb565 and a contact sheet, sheet.png.

    python3 render.py <outdir> [scale]
"""
import glob
import os
import struct
import sys

from PIL import Image, ImageDraw


def load(path):
    with open(path, "rb") as f:
        w, h = struct.unpack("<HH", f.read(4))
        px = struct.unpack("<%dH" % (w * h), f.read(w * h * 2))
    return w, h, px


def to_rgb(c):
    r, g, b = (c >> 11) & 0x1F, (c >> 5) & 0x3F, c & 0x1F
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)


def render(path, scale):
    w, h, px = load(path)
    img = Image.new("RGB", (w * scale, h * scale), (8, 8, 8))
    d = ImageDraw.Draw(img)
    pad = max(1, scale // 5)
    for y in range(h):
        for x in range(w):
            c = px[y * w + x]
            if c:
                x0, y0 = x * scale + pad, y * scale + pad
                d.ellipse([x0, y0, x0 + scale - 2 * pad, y0 + scale - 2 * pad], fill=to_rgb(c))
            else:
                cx, cy = x * scale + scale // 2, y * scale + scale // 2
                d.point((cx, cy), fill=(28, 28, 28))
    for sx in range(64, w, 64):  # module seams
        d.line([(sx * scale, 0), (sx * scale, h * scale)], fill=(40, 40, 60))
    out = path[: -len(".rgb565")] + ".png"
    img.save(out)
    return out, img


def main():
    outdir = sys.argv[1]
    scale = int(sys.argv[2]) if len(sys.argv) > 2 else 5
    images = []
    for p in sorted(glob.glob(os.path.join(outdir, "*.rgb565"))):
        out, img = render(p, scale)
        images.append((os.path.basename(out), img))
        print(out)
    if not images:
        sys.exit("no frames in " + outdir)
    gap, label = 10, 16
    sw = max(i.width for _, i in images)
    sh = sum(i.height + gap + label for _, i in images)
    sheet = Image.new("RGB", (sw, sh), (24, 24, 24))
    d = ImageDraw.Draw(sheet)
    y = 0
    for name, img in images:
        d.text((4, y + 2), name, fill=(200, 200, 200))
        sheet.paste(img, (0, y + label))
        y += img.height + gap + label
    sheet.save(os.path.join(outdir, "sheet.png"))
    print(os.path.join(outdir, "sheet.png"))


if __name__ == "__main__":
    main()
