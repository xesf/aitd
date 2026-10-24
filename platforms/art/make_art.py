#!/usr/bin/env python3
"""Packaging art for the PSP and the Vita, made from your own game data.

The repository holds no game art. This script takes the full screen
pictures the engine dumps from each game's ITD_RESS.PAK (aitd
--stage=pictures) and composes:

    build/art/<GAME>/vita/icon0.png      128 x 128   (8 bit, as the Vita needs)
    build/art/<GAME>/vita/bg.png         840 x 500
    build/art/<GAME>/vita/startup.png    280 x 158
    build/art/<GAME>/psp/ICON0.PNG       144 x 80
    build/art/<GAME>/psp/PIC1.PNG        480 x 272

    python3 platforms/art/make_art.py build/art     (run by make art)
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

# Per game: the background picture, the icon picture and the part of it to
# keep (left, top, right, bottom as fractions), the title to draw (None when
# the art carries its own), and an optional logo picture whose bright pixels
# are laid over the background.
GAMES = {
    "INDARK": dict(bg=16, icon=16, icon_box=(0.22, 0.05, 0.62, 0.65), focus=(0.5, 0.25),
                   title="Alone in the Dark"),
    "JACK": dict(bg=3, icon=3, icon_box=(0.32, 0.25, 0.72, 0.89),
                 title="Jack in the Dark"),
    "INDARK2": dict(bg=30, icon=19, icon_box=(0.10, 0.0, 0.80, 1.0),
                    title="Alone in the Dark 2"),
    "INDARK3": dict(bg=24, icon=31, icon_box=(0.12, 0.08, 0.88, 1.0),
                    title=None, logo=36, logo_box=(0.0, 0.33, 1.0, 0.72)),
    "TIMEGATE": dict(bg=1, icon=1, icon_box=(0.24, 0.12, 0.76, 0.95),
                     title="Time Gate: Knight's Chase"),
    "KNIGHTS": dict(bg=1, icon=1, icon_box=(0.24, 0.12, 0.76, 0.95),
                    title="Time Gate: Knight's Chase"),
}

FONTS = [
    "/System/Library/Fonts/Supplemental/Georgia Bold.ttf",
    "/System/Library/Fonts/Supplemental/Times New Roman Bold.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf",
    "/usr/share/fonts/dejavu/DejaVuSerif-Bold.ttf",
    "C:/Windows/Fonts/georgiab.ttf",
]


def font(size):
    for path in FONTS:
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default(size=size)


def picture(folder, entry):
    path = os.path.join(folder, "pic_%03d.ppm" % entry)
    if not os.path.exists(path):
        return None
    # The 640 x 400 Time Gate pictures are scaled like the 320 x 200 ones.
    return Image.open(path).convert("RGB")


def crop(img, box):
    w, h = img.size
    return img.crop((round(box[0] * w), round(box[1] * h), round(box[2] * w), round(box[3] * h)))


def cover(img, w, h, focus=(0.5, 0.5)):
    """Scale to fill (w, h), cropping what is left over around `focus`.
    The pictures were drawn for a 4:3 screen, so they are stretched 6:5
    in height first."""
    sw, sh = img.size
    img = img.resize((sw, round(sh * 1.2)), Image.LANCZOS)
    sw, sh = img.size
    scale = max(w / sw, h / sh)
    img = img.resize((round(sw * scale), round(sh * scale)), Image.LANCZOS)
    x = min(max(0, round(img.size[0] * focus[0] - w / 2)), img.size[0] - w)
    y = min(max(0, round(img.size[1] * focus[1] - h / 2)), img.size[1] - h)
    return img.crop((x, y, x + w, y + h))


def lay_logo(img, logo, height_frac):
    """Lays the bright pixels of `logo` over the top of `img`."""
    w, h = img.size
    lh = round(h * height_frac)
    lw = round(logo.size[0] * lh / logo.size[1] * 1.0)
    if lw > w * 0.94:
        lw = round(w * 0.94)
        lh = round(logo.size[1] * lw / logo.size[0])
    logo = logo.resize((lw, lh), Image.LANCZOS)
    mask = logo.convert("L").point(lambda v: 0 if v < 40 else min(255, v * 2))
    out = img.copy()
    out.paste(logo, ((w - lw) // 2, round(h * 0.06)), mask)
    return out


def lay_title(img, text, size_frac, y_frac=0.08):
    """Draws `text` centred near the top, with a dark halo so it reads on
    any picture."""
    w, h = img.size
    size = max(10, round(h * size_frac))
    f = font(size)
    while f.getbbox(text)[2] > w * 0.92 and size > 10:
        size -= 1
        f = font(size)
    tw = f.getbbox(text)[2]
    x, y = (w - tw) // 2, round(h * y_frac)
    halo = Image.new("L", img.size, 0)
    ImageDraw.Draw(halo).text((x, y), text, font=f, fill=255)
    halo = halo.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.GaussianBlur(3))
    out = img.copy()
    out.paste((0, 0, 0), (0, 0), halo)
    ImageDraw.Draw(out).text((x, y), text, font=f, fill=(236, 226, 200))
    return out


def dress(img, spec, pics, size_frac, y_frac=0.08):
    if spec.get("logo") is not None:
        logo = picture(pics, spec["logo"])
        if logo is not None:
            return lay_logo(img, crop(logo, spec["logo_box"]), 0.30)
    if spec.get("title"):
        return lay_title(img, spec["title"], size_frac, y_frac)
    return img


def indexed(img):
    """The Vita installs only 8 bit palette PNGs for its LiveArea images."""
    return img.convert("P", palette=Image.ADAPTIVE, colors=256)


def make(game, spec, root):
    pics = os.path.join(root, "pics", game)
    bg = picture(pics, spec["bg"])
    icon = picture(pics, spec["icon"])
    if bg is None or icon is None:
        print("%s: pictures missing, skipped" % game)
        return False
    vita = os.path.join(root, game, "vita")
    psp = os.path.join(root, game, "psp")
    os.makedirs(vita, exist_ok=True)
    os.makedirs(psp, exist_ok=True)

    square = cover(crop(icon, spec["icon_box"]), 128, 128)
    indexed(square).save(os.path.join(vita, "icon0.png"), optimize=True)
    focus = spec.get("focus", (0.5, 0.5))
    indexed(dress(cover(bg, 840, 500, focus), spec, pics, 0.11)).save(os.path.join(vita, "bg.png"), optimize=True)
    indexed(dress(cover(bg, 280, 158, focus), spec, pics, 0.14)).save(os.path.join(vita, "startup.png"), optimize=True)

    dress(cover(bg, 480, 272, focus), spec, pics, 0.10).save(os.path.join(psp, "PIC1.PNG"))
    dress(cover(bg, 144, 80, focus), spec, pics, 0.17, 0.62).save(os.path.join(psp, "ICON0.PNG"))
    print("%s: art written" % game)
    return True


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "build/art"
    done = [g for g, spec in GAMES.items() if make(g, spec, root)]
    return 0 if done else 1


if __name__ == "__main__":
    sys.exit(main())
