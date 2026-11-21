"""Draws the AdMutify icon set as vector shapes and writes the .ico files to assets/.

    python tools/make_icons.py [--preview FILE]

Needs Pillow. Every size is drawn on its own at 8x and downsampled, so small sizes get thicker strokes and no
shadow instead of a shrunken large icon. The geometry uses a 1024 unit design grid.
"""
import argparse
import math
import os

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GRID = 1024
SUPER = 8

# Tile gradients (top right, bottom left) per state.
VIOLET = ((124, 58, 237), (49, 46, 129))
CORAL = ((255, 122, 92), (214, 54, 74))
GREY = ((150, 150, 158), (92, 92, 100))

APP_SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
TRAY_SIZES = (16, 20, 24, 32, 40, 48, 64)


def tile_mask(size):
    mask = Image.new('L', (size, size), 0)
    margin = round(size * 0.06)
    ImageDraw.Draw(mask).rounded_rectangle([margin, margin, size - 1 - margin, size - 1 - margin],
                                           radius=round(size * 0.22), fill=255)
    return mask


def gradient(size, colours):
    top, bottom = colours
    # A small gradient image scaled up is smooth and much faster than a per-pixel loop at 8x.
    n = 64
    small = Image.new('RGB', (n, n))
    px = small.load()
    for y in range(n):
        for x in range(n):
            t = min(1.0, max(0.0, (x * 0.35 + (n - 1 - y) * 0.65) / (n - 1)))
            px[x, y] = tuple(round(bottom[i] + (top[i] - bottom[i]) * t) for i in range(3))
    return small.resize((size, size), Image.BICUBIC).convert('RGBA')


def round_corners(alpha, radius):
    # Blur and threshold rounds every corner of the glyph by about the blur radius.
    if radius < 1:
        return alpha
    return alpha.filter(ImageFilter.GaussianBlur(radius)).point(lambda v: 255 if v >= 128 else 0)


def glyph_mask(S, small, with_wave):
    k = S / GRID
    mask = Image.new('L', (S, S), 0)
    d = ImageDraw.Draw(mask)
    dx = -20
    d.rectangle([(225 + dx) * k, 400 * k, (400 + dx) * k, 624 * k], fill=255)
    d.polygon([((380 + dx) * k, 400 * k), ((600 + dx) * k, 255 * k), ((600 + dx) * k, 769 * k),
               ((380 + dx) * k, 624 * k)], fill=255)
    mask = round_corners(mask, (14 if small else 22) * k)
    if with_wave:
        w = (80 if small else 64) * k
        cx, cy, r = (600 + dx) * k, 512 * k, (180 if small else 190) * k
        d = ImageDraw.Draw(mask)
        d.arc([cx - r, cy - r, cx + r, cy + r], -48, 48, fill=255, width=round(w))
        for a in (-48, 48):
            ex = cx + (r - w / 2) * math.cos(math.radians(a))
            ey = cy + (r - w / 2) * math.sin(math.radians(a))
            d.ellipse([ex - w / 2, ey - w / 2, ex + w / 2, ey + w / 2], fill=255)
    return mask


def slash(S, small, gap_only):
    k = S / GRID
    x0, y0, x1, y1 = 790 * k, 230 * k, 230 * k, 790 * k
    width = (110 if small else 72) * k
    if gap_only:
        width += 2 * (44 if small else 34) * k
    mask = Image.new('L', (S, S), 0)
    d = ImageDraw.Draw(mask)
    d.line([x0, y0, x1, y1], fill=255, width=round(width))
    for x, y in ((x0, y0), (x1, y1)):
        d.ellipse([x - width / 2, y - width / 2, x + width / 2, y + width / 2], fill=255)
    return mask


def draw(px, state):
    """state: 'active', 'muted' or 'off'."""
    S = px * SUPER
    k = S / GRID
    small = px <= 24
    colours = {'active': VIOLET, 'muted': CORAL, 'off': GREY}[state]
    img = gradient(S, colours)
    # The muted speaker has no wave: wave pieces beside the slash read as a second stroke at tray size.
    glyph = glyph_mask(S, small, with_wave=state != 'muted')
    if state == 'muted':
        glyph = ImageChops.subtract(glyph, slash(S, small, gap_only=True))
        glyph = ImageChops.lighter(glyph, slash(S, small, gap_only=False))
    if not small and state != 'off':
        blur = glyph.filter(ImageFilter.GaussianBlur(14 * k)).point(lambda v: round(v * 0.35))
        shadow = Image.new('RGBA', (S, S), (20, 10, 60, 0))
        shadow.putalpha(blur)
        img.alpha_composite(shadow, (0, round(10 * k)))
    white = Image.new('RGBA', (S, S), (255, 255, 255, 255) if state != 'off' else (235, 235, 240, 255))
    white.putalpha(glyph)
    img.alpha_composite(white)
    out = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    out.paste(img, (0, 0), tile_mask(S))
    return out.resize((px, px), Image.LANCZOS)


def write_ico(path, state, sizes):
    frames = [draw(s, state) for s in sizes]
    largest = frames[-1]
    largest.save(path, format='ICO', sizes=[(s, s) for s in sizes], append_images=frames[:-1])


def preview(path):
    states = ('active', 'muted', 'off')
    sizes = (16, 20, 24, 32, 48)
    sheet = Image.new('RGBA', (3 * 280, 200 + 2 * 90), (243, 243, 243, 255))
    for i, st in enumerate(states):
        sheet.alpha_composite(draw(160, st), (i * 280 + 60, 20))
    for row, bg in enumerate(((32, 32, 32), (238, 238, 238))):
        y = 200 + row * 90
        sheet.alpha_composite(Image.new('RGBA', (sheet.width, 90), bg + (255,)), (0, y))
        x = 20
        for s in sizes:
            for j, st in enumerate(states):
                sheet.alpha_composite(draw(s, st), (x + j * (s + 8), y + 45 - s // 2))
            x += 3 * (s + 8) + 24
    sheet.save(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--preview', help='also write a preview sheet of all states to this PNG file')
    args = parser.parse_args()
    assets = os.path.join(ROOT, 'assets')
    write_ico(os.path.join(assets, 'app.ico'), 'active', APP_SIZES)
    write_ico(os.path.join(assets, 'tray_muted.ico'), 'muted', TRAY_SIZES)
    write_ico(os.path.join(assets, 'tray_off.ico'), 'off', TRAY_SIZES)
    if args.preview:
        preview(args.preview)
    print('icons written to', assets)


if __name__ == '__main__':
    main()
