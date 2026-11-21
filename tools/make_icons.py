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


def tile_mask(size, tray):
    # Tray icons fill the whole square: the shell already spaces them, a margin only shrinks the icon.
    mask = Image.new('L', (size, size), 0)
    margin = round(size * (0.0 if tray else 0.06))
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


def dot(d, x, y, w):
    d.ellipse([x - w / 2, y - w / 2, x + w / 2, y + w / 2], fill=255)


def glyph_mask(S, small, mark, tray):
    """mark: 'wave' or 'x'. All tray states share one speaker spot, so it never jumps between them."""
    k = S / GRID
    dx = -50 if tray else -20
    mask = Image.new('L', (S, S), 0)
    d = ImageDraw.Draw(mask)
    d.rectangle([(225 + dx) * k, 400 * k, (400 + dx) * k, 624 * k], fill=255)
    d.polygon([((380 + dx) * k, 400 * k), ((600 + dx) * k, 255 * k), ((600 + dx) * k, 769 * k),
               ((380 + dx) * k, 624 * k)], fill=255)
    mask = round_corners(mask, (14 if small else 22) * k)
    d = ImageDraw.Draw(mask)
    if mark == 'wave':
        w = (80 if small else 64) * k
        # The tray wave sits further out: the larger tray glyph would otherwise crowd it against the speaker.
        r = (265 if tray else 180 if small else 190) * k
        a0 = 40 if tray else 48
        cx, cy = (600 + dx) * k, 512 * k
        d.arc([cx - r, cy - r, cx + r, cy + r], -a0, a0, fill=255, width=round(w))
        for a in (-a0, a0):
            dot(d, cx + (r - w / 2) * math.cos(math.radians(a)), cy + (r - w / 2) * math.sin(math.radians(a)), w)
    else:
        # The X keeps a clear gap to the speaker, so the two never touch at 16 px.
        w = (84 if small else 66) * k
        cx, cy, h = 775 * k, 512 * k, (92 if small else 96) * k
        for x0, y0, x1, y1 in ((cx - h, cy - h, cx + h, cy + h), (cx - h, cy + h, cx + h, cy - h)):
            d.line([x0, y0, x1, y1], fill=255, width=round(w))
            dot(d, x0, y0, w)
            dot(d, x1, y1, w)
    return mask


def draw(px, state, tray=False):
    """state: 'active', 'muted' or 'off'. tray: no edge margin and a larger glyph."""
    S = px * SUPER
    k = S / GRID
    small = px <= 24
    colours = {'active': VIOLET, 'muted': CORAL, 'off': GREY}[state]
    img = gradient(S, colours)
    mark = 'x' if state == 'muted' else 'wave'
    if tray:
        # Drawn larger and cropped to the middle, which scales the glyph about the centre.
        G = round(S * 1.12)
        off = (G - S) // 2
        glyph = glyph_mask(G, small, mark, tray=True).crop((off, off, off + S, off + S))
    else:
        glyph = glyph_mask(S, small, mark, tray=False)
    if not small and state != 'off':
        blur = glyph.filter(ImageFilter.GaussianBlur(14 * k)).point(lambda v: round(v * 0.35))
        shadow = Image.new('RGBA', (S, S), (20, 10, 60, 0))
        shadow.putalpha(blur)
        img.alpha_composite(shadow, (0, round(10 * k)))
    white = Image.new('RGBA', (S, S), (255, 255, 255, 255) if state != 'off' else (235, 235, 240, 255))
    white.putalpha(glyph)
    img.alpha_composite(white)
    out = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    out.paste(img, (0, 0), tile_mask(S, tray))
    return out.resize((px, px), Image.LANCZOS)


def write_ico(path, state, sizes, tray):
    frames = [draw(s, state, tray) for s in sizes]
    largest = frames[-1]
    largest.save(path, format='ICO', sizes=[(s, s) for s in sizes], append_images=frames[:-1])


def preview(path):
    states = ('active', 'muted', 'off')
    sizes = (16, 20, 24, 32, 48)
    sheet = Image.new('RGBA', (4 * 280, 200 + 3 * 90), (243, 243, 243, 255))
    sheet.alpha_composite(draw(160, 'active'), (60, 20))
    for i, st in enumerate(states):
        sheet.alpha_composite(draw(160, st, True), ((i + 1) * 280 + 60, 20))
    for row, bg in enumerate(((32, 32, 32), (238, 238, 238))):
        y = 200 + row * 90
        sheet.alpha_composite(Image.new('RGBA', (sheet.width, 90), bg + (255,)), (0, y))
        x = 20
        for s in sizes:
            for j, st in enumerate(states):
                sheet.alpha_composite(draw(s, st, True), (x + j * (s + 8), y + 45 - s // 2))
            x += 3 * (s + 8) + 24
    # 16 and 24 px tray frames enlarged 3x, so single pixels can be judged.
    x = 20
    for s in (16, 24):
        for st in states:
            sheet.alpha_composite(draw(s, st, True).resize((s * 3, s * 3), Image.NEAREST), (x, 200 + 2 * 90 + 45 - s * 3 // 2))
            x += s * 3 + 12
        x += 30
    sheet.save(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--preview', help='also write a preview sheet of all states to this PNG file')
    args = parser.parse_args()
    assets = os.path.join(ROOT, 'assets')
    write_ico(os.path.join(assets, 'app.ico'), 'active', APP_SIZES, tray=False)
    write_ico(os.path.join(assets, 'tray_active.ico'), 'active', TRAY_SIZES, tray=True)
    write_ico(os.path.join(assets, 'tray_muted.ico'), 'muted', TRAY_SIZES, tray=True)
    write_ico(os.path.join(assets, 'tray_off.ico'), 'off', TRAY_SIZES, tray=True)
    if args.preview:
        preview(args.preview)
    print('icons written to', assets)


if __name__ == '__main__':
    main()
