#!/usr/bin/env python3
"""The share pictures of the site: one 1200x630 Open Graph image per page.

What a link to the site shows on Discord, Facebook, X, WhatsApp or LinkedIn:
"TigerTouch for <Brand> printers", the photo-real device showing one of the
brand's printers (assets/mockups/angle/), a few of its printers
(assets/printers/models/), on the site's dark background. The installer page gets the same with the cards.

A tool for the person changing the pictures, not part of the site build (that
needs no font and no Pillow): the JPEGs are committed in assets/og/ and the site
build copies them. Run again when a page's picture or lineup changes:

  python3 scripts/make-og-images.py        (needs Pillow and a bold font:
                                            Helvetica Neue on macOS, or --font
                                            and --regular)
"""
import argparse
import json
import pathlib

from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parent.parent
W, H = 1200, 630
BG, TEXT, MUTED, ACCENT = (20, 22, 26), (242, 242, 242), (139, 144, 154), (245, 130, 32)


def fonts(path, regular=None):
    def f(size, bold=True):
        if regular and not bold:
            return ImageFont.truetype(str(regular), size)
        if path.suffix == ".ttc":
            return ImageFont.truetype(str(path), size, index=1 if bold else 0)
        return ImageFont.truetype(str(path), size)
    return f


def fit(draw, lines, font, max_w, start):
    """The biggest size, from `start` down, at which every line fits in max_w."""
    size = start
    while size > 30:
        fo = font(size)
        if all(sum(draw.textlength(t, font=fo) for t, _c in ln) <= max_w for ln in lines):
            return fo
        size -= 2
    return font(30)


def line(draw, x, y, parts, fo):
    for t, c in parts:
        draw.text((x, y), t, font=fo, fill=c)
        x += draw.textlength(t, font=fo)


def paste_fit(canvas, path, box, trim=False):
    """The picture scaled into box (x, y, w, h), its bottom on the box's."""
    im = Image.open(path).convert("RGBA")
    if trim:
        im = im.crop(im.getbbox())
    x, y, w, h = box
    im.thumbnail((w, h), Image.LANCZOS)
    canvas.alpha_composite(im, (x + (w - im.width) // 2, y + h - im.height))


def card(title2, sub, device, printers, footer, font):
    im = Image.new("RGBA", (W, H), BG + (255,))
    d = ImageDraw.Draw(im)
    d.rectangle((0, 0, 10, H), fill=ACCENT)
    # The screen on the right, the words on the left.
    paste_fit(im, device, (640, 40, 548, 540), trim=True)
    d.text((64, 52), "Tiger", font=font(30), fill=TEXT)
    d.text((64 + d.textlength("Tiger", font=font(30)), 52), "System", font=font(30, False), fill=MUTED)
    l1 = [("Tiger", TEXT), ("Touch", ACCENT), (" for" if title2 else "", TEXT)]
    fo = fit(d, [l1, title2] if title2 else [l1], font, 570, 92)
    line(d, 64, 112, l1, fo)
    if title2:
        line(d, 64, 112 + fo.size + 10, title2, fo)
    y = 112 + (fo.size + 10) * (2 if title2 else 1) + 14
    so = font(28, False)
    words, row, rows = sub.split(), "", []
    for w_ in words:
        if d.textlength((row + " " + w_).strip(), font=so) > 560:
            rows.append(row)
            row = w_
        else:
            row = (row + " " + w_).strip()
    rows.append(row)
    for r in rows:
        d.text((64, y), r, font=so, fill=MUTED)
        y += so.size + 8
    # What to do next, as the site's own button, big, under the words.
    cta = "Install for free"
    co = font(36)
    tw = d.textlength(cta, font=co) + 44  # the text, then a drawn arrow (no font has one here)
    top = y + 6
    d.rounded_rectangle((64, top, 64 + tw + 64, top + 70), radius=35, fill=ACCENT)
    d.text((64 + 32, top + 35), cta, font=co, fill=(255, 255, 255), anchor="lm")
    ax = 64 + 32 + tw - 30
    d.line((ax, top + 35, ax + 26, top + 35), fill=(255, 255, 255), width=5)
    d.line((ax + 15, top + 24, ax + 27, top + 35, ax + 15, top + 46), fill=(255, 255, 255), width=5, joint="curve")
    # A few of the brand's printers along the bottom.
    x = 52
    for p in printers[:4]:
        paste_fit(im, p, (x, 476, 140, 140))
        x += 146
    # The small print, under the screen.
    d.text((915, 606), footer, font=font(20, False), fill=MUTED, anchor="mm")
    return im.convert("RGB")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", default="/System/Library/Fonts/HelveticaNeue.ttc")
    ap.add_argument("--regular", help="the regular face, when --font is a bold one-face file (Windows: arialbd.ttf and arial.ttf)")
    a = ap.parse_args()
    font = fonts(pathlib.Path(a.font), pathlib.Path(a.regular) if a.regular else None)
    out = ROOT / "assets" / "og"
    out.mkdir(parents=True, exist_ok=True)
    data = json.loads((ROOT / "installer" / "brand-pages.json").read_text())["brands"]
    models = ROOT / "assets" / "printers" / "models"
    footer = "Free and open source  ·  9 brands, one screen"
    angle = ROOT / "assets" / "mockups" / "angle"
    for b in data:
        # The photo-real Panda Touch on one of the brand's printers (founder's call:
        # the share picture shows the real device, never the line drawing).
        shot = next(angle.glob("panda-touch-angle-printer-%s*.png" % b["shot"].split("-")[0]))
        im = card([(b["name"], ACCENT), (" 3D printers", TEXT)],
                  "Free firmware for the BIGTREETECH Panda Touch and K-Touch",
                  shot,
                  [models / m[0] for m in b["lineup"]], footer, font)
        im.save(out / (b["slug"] + ".jpg"), quality=86, optimize=True, progressive=True)
    lead = [models / f for f in ("bambu_lab_x1_carbon.png", "creality_k2_plus.png", "elegoo_centauri_carbon_2.png",
                                 "prusa_core_one.png")]
    home = card([], "Every 3D printer on one screen: free firmware for the BIGTREETECH Panda Touch and K-Touch",
                angle / "panda-touch-angle-home-cards.png", lead,
                "9 brands, one screen  ·  open source", font)
    home.save(out / "home.jpg", quality=86, optimize=True, progressive=True)
    print("og images: %s" % ", ".join(sorted(p.name for p in out.glob("*.jpg"))))


if __name__ == "__main__":
    main()
