#!/usr/bin/env python3
"""Build the full logo (emblem, "Open Firenet", tagline) of assets/brand-logo.png and brand-logo@2x.png.

The emblem is taken from assets/brand-icon@2x.png; the texts are drawn with DejaVu Sans Bold.

    python3 tools/gen_brand_logo.py
"""
import pathlib
import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets"
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

BG = (11, 18, 27)
WHITE, GREY = (255, 255, 255), (148, 163, 184)
# Gradient of the emblem, measured on the icon: from its bottom-left to its top-right.
GRAD_FROM, GRAD_TO = (247, 168, 28), (249, 69, 29)
W, H = 1024, 336                  # size of the @2x file
SS = 4                            # drawn 4x larger, then reduced, for smooth edges
PAD = 12                          # transparent margin around the frame
RADIUS = 56
EMBLEM = 232                      # emblem diameter
TITLE_W, TAG_W = 610, 452       # width of "Open Firenet" and of the tagline (the old logo: 495 and 366)
GAP = 44                          # between emblem and text


def emblem():
    """The emblem of the icon, with the dark background turned transparent."""
    icon = np.asarray(Image.open(ASSETS / "brand-icon@2x.png").convert("RGB")).astype(float)
    bg = icon[30, 256]
    # Opacity from "how orange": red minus blue goes from about -15 (background) to about 225 (emblem), and stays
    # near 0 on the faint grey outline of the icon, which is left out.
    warm = (icon[..., 0] - icon[..., 2]) - (bg[0] - bg[2])
    alpha = np.clip((warm / 240.0 - 0.12) / 0.88, 0, 1)
    a3 = np.maximum(alpha, 1e-3)[..., None]
    rgb = np.clip((icon - bg * (1 - a3)) / a3, 0, 255)
    out = Image.fromarray(np.dstack([rgb, alpha * 255]).astype("uint8"))
    ys, xs = np.where(alpha > 0.5)
    return out.crop((xs.min() - 2, ys.min() - 2, xs.max() + 3, ys.max() + 3))


def main():
    s = SS
    img = Image.new("RGBA", (W * s, H * s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([PAD * s, PAD * s, (W - PAD) * s - 1, (H - PAD) * s - 1], RADIUS * s, fill=BG + (255,),
                        outline=(255, 255, 255, 46), width=3 * s // 2)
    def fit(text, width):               # largest font size whose text is not wider than width
        size = 10
        while d.textlength(text, font=ImageFont.truetype(FONT, size + 1)) <= width:
            size += 1
        return ImageFont.truetype(FONT, size)
    title = fit("Open Firenet", TITLE_W * s)
    tag = fit("Local Smart Stove Bridge", TAG_W * s)
    w_open = d.textlength("Open ", font=title)
    w_title = w_open + d.textlength("Firenet", font=title)
    total = EMBLEM * s + GAP * s + w_title
    x0 = (W * s - total) / 2                                   # the whole group is centred in the frame
    e = emblem().resize((EMBLEM * s, EMBLEM * s), Image.LANCZOS)
    img.alpha_composite(e, (int(x0), (H * s - EMBLEM * s) // 2))
    tx = x0 + EMBLEM * s + GAP * s
    # Title and tagline as one block, centred vertically on the emblem.
    tb = d.textbbox((0, 0), "Open Firenet", font=title)
    gb = d.textbbox((0, 0), "Local Smart Stove Bridge", font=tag)
    line_gap = 22 * s
    block = (tb[3] - tb[1]) + line_gap + (gb[3] - gb[1])
    ty = (H * s - block) / 2 - tb[1]
    d.text((tx, ty), "Open ", font=title, fill=WHITE)
    # "Firenet" carries the gradient of the emblem, in the same direction (bottom-left to top-right).
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).text((tx + w_open, ty), "Firenet", font=title, fill=255)
    x0, y0, x1, y1 = mask.getbbox()
    ys, xs = np.mgrid[0:img.size[1], 0:img.size[0]]
    t = np.clip(((xs - x0) / (x1 - x0) + (y1 - ys) / (y1 - y0)) / 2, 0, 1)[..., None]
    grad = (np.array(GRAD_FROM) * (1 - t) + np.array(GRAD_TO) * t).astype("uint8")
    img.paste(Image.fromarray(grad).convert("RGBA"), (0, 0), mask)
    d.text((tx + 3 * s, ty + tb[3] + line_gap - gb[1]), "Local Smart Stove Bridge", font=tag, fill=GREY)
    big = img.resize((W, H), Image.LANCZOS)
    big.save(ASSETS / "brand-logo@2x.png", optimize=True)
    big.resize((W // 2, H // 2), Image.LANCZOS).save(ASSETS / "brand-logo.png", optimize=True)
    print(f"wrote brand-logo@2x.png ({W}x{H}) and brand-logo.png ({W // 2}x{H // 2})")


if __name__ == "__main__":
    main()
