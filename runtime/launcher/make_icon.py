"""Draws LBP3Launcher's own icon (lbp3launcher.ico next to this script): a rounded square in the
in-game menu's blues with a stitched border and a bold "3", at 16-256 px. Our own artwork (no
game art: the game's pictures are only ever read from the player's dump at run time).

    python make_icon.py
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
SIZE = 256


def draw(size):
    s = size / SIZE
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # background: a vertical blue gradient in a rounded square
    grad = Image.new("RGBA", (size, size))
    gd = ImageDraw.Draw(grad)
    for y in range(size):
        t = y / max(1, size - 1)
        r = int(52 + (26 - 52) * t)
        g = int(96 + (40 - 96) * t)
        b = int(196 + (110 - 196) * t)
        gd.line([(0, y), (size, y)], fill=(r, g, b, 255))
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, size - 1, size - 1], radius=int(52 * s), fill=255)
    img.paste(grad, (0, 0), mask)
    # stitched border (dashes), the crafted look
    if size >= 48:
        inset = 18 * s
        dash, gap = 14 * s, 9 * s
        w = max(1, int(6 * s))
        box = [inset, inset, size - 1 - inset, size - 1 - inset]
        x0, y0, x1, y1 = box
        def dashes(a, b, horizontal, at):
            p = a
            while p < b:
                q = min(b, p + dash)
                if horizontal:
                    d.line([(p, at), (q, at)], fill=(235, 240, 255, 230), width=w)
                else:
                    d.line([(at, p), (at, q)], fill=(235, 240, 255, 230), width=w)
                p = q + gap
        dashes(x0 + 30 * s, x1 - 30 * s, True, y0)
        dashes(x0 + 30 * s, x1 - 30 * s, True, y1)
        dashes(y0 + 30 * s, y1 - 30 * s, False, x0)
        dashes(y0 + 30 * s, y1 - 30 * s, False, x1)
    # a bold "3"
    font = None
    for name in ("seguibl.ttf", "segoeuib.ttf", "arialbd.ttf"):
        try:
            font = ImageFont.truetype(name, int(178 * s))
            break
        except OSError:
            continue
    if font is None:
        font = ImageFont.load_default()
    text = "3"
    bbox = d.textbbox((0, 0), text, font=font)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    pos = ((size - tw) / 2 - bbox[0], (size - th) / 2 - bbox[1] - 4 * s)
    shadow = max(1, int(5 * s))
    d.text((pos[0] + shadow, pos[1] + shadow), text, font=font, fill=(10, 16, 40, 150))
    d.text(pos, text, font=font, fill=(255, 255, 255, 255))
    return img


def main():
    sizes = [256, 128, 64, 48, 32, 24, 16]
    images = [draw(n) for n in sizes]
    images[0].save(HERE / "lbp3launcher.ico", sizes=[(n, n) for n in sizes], append_images=images[1:])
    images[0].save(HERE / "lbp3launcher_256.png")
    print("wrote", HERE / "lbp3launcher.ico")


if __name__ == "__main__":
    main()
