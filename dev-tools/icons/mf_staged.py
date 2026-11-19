#!/usr/bin/env python3
"""The staged icon of Minor Finder: a big blue graph with a green point that has been picked up, and the red minor in a corner.
   mf_staged.py preview   -> /tmp/ic/staged.png (256 and 64 px, day and night)
   mf_staged.py make OUTDIR  -> the ten icons"""
import sys, os, math, itertools
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import render
from PIL import Image, ImageDraw

SS = 4
# the colours of the game (game_colours in minorfinder.c)
PAL = [(0.8, 0.8, 0.8)] * 1
def game_pal():
    # background (a little lighter than the app's #cccccc, as game_mkhighlight makes it), then the colours
    pal = [None] * 14
    pal[0] = (0.8, 0.8, 0.8)     # COL_SYSBACKGROUND
    pal[1] = (0.8, 0.8, 0.8)     # COL_BACKGROUND
    pal[2] = (0.3, 0.3, 0.3)     # COL_OUTLINE
    pal[3] = (0.0, 0.0, 1.0)     # COL_BASEPOINT
    pal[4] = (1.0, 0.0, 0.0)     # COL_MINORPOINT
    pal[5] = (0.0, 1.0, 0.0)     # COL_DRAGPOINT
    pal[6] = (1.0, 0.5, 0.0)     # COL_DELPOINT
    pal[7] = (0.3, 0.7, 1.0)     # COL_HIDEPOINT
    pal[8] = (0.0, 0.0, 0.0)     # COL_POINTOUTLINE
    pal[9] = (0.0, 0.0, 0.0)     # COL_EDGE
    pal[10] = (1.0, 0.5, 0.0)    # COL_DELEDGE
    pal[11] = (0.4, 0.4, 0.4)    # COL_HIDEEDGE
    pal[12] = (1.0, 1.0, 1.0)    # COL_FLASH
    pal[13] = (0.5, 0.5, 0.5)    # COL_FLASH2
    return pal

# the big graph: a ring of eight points with two chords and a point that hangs on it
BIG = {
    'A': (0.78, 0.13), 'B': (0.93, 0.40), 'C': (0.80, 0.68), 'D': (0.56, 0.90),
    'E': (0.22, 0.84), 'F': (0.08, 0.60), 'G': (0.40, 0.55), 'H': (0.55, 0.30),
    'I': (0.10, 0.88),
}
BIG_EDGES = ['HA', 'AB', 'BC', 'CD', 'DE', 'EF', 'FG', 'GH', 'GC', 'HB', 'EI']
MINOR = {'a': (0.50, 0.12), 'b': (0.88, 0.50), 'c': (0.50, 0.88), 'd': (0.12, 0.50)}
MINOR_EDGES = ['ab', 'bc', 'cd', 'da']

def compose(size, night=False, picked='D', halo=True, inset=0.40, margin=0.045):
    pal = game_pal()
    if night:
        pal = render.night_palette(pal, {3, 4, 5, 6, 7, 10})
    rgb = render.to_rgb
    W = size * SS
    bg = pal[1] if not night else pal[0]
    img = Image.new('RGB', (W, W), rgb(bg))
    d = ImageDraw.Draw(img)
    border = max(SS, round(size / 64 * SS))
    edge_w = max(2, round(size * 0.032 * SS))
    rad = size * 0.052 * SS
    def graph(pts, edges, x0, y0, side, scale, colour_of):
        for e in edges:
            (x1, y1), (x2, y2) = pts[e[0]], pts[e[1]]
            d.line([x0 + x1 * side, y0 + y1 * side, x0 + x2 * side, y0 + y2 * side],
                   fill=rgb(pal[9]), width=max(2, round(edge_w * scale)))
        for k, (x, y) in pts.items():
            r = rad * scale
            cx, cy = x0 + x * side, y0 + y * side
            col = colour_of(k)
            rr = r
            if col == 'halo':
                continue
            d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], fill=rgb(pal[col]), outline=rgb(pal[8]),
                      width=max(1, round(rr * 0.2)))
    pad = 0.035
    side = W * (1 - 2 * pad)
    x0 = y0 = W * pad
    nbrs = {e[1] for e in BIG_EDGES if e[0] == picked} | {e[0] for e in BIG_EDGES if e[1] == picked}
    if halo and picked:
        for k in nbrs:
            x, y = BIG[k]
            r = rad * 1.75
            cx, cy = x0 + x * side, y0 + y * side
            d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=rgb(pal[7]))
    graph(BIG, BIG_EDGES, x0, y0, side, 1.0, lambda k: 5 if k == picked else 3)
    ms = W * inset
    mm = W * margin
    mx, my = mm, mm
    d.rectangle([mx, my, mx + ms, my + ms], fill=rgb(bg))
    graph(MINOR, MINOR_EDGES, mx + ms * 0.12, my + ms * 0.12, ms * 0.76, 0.85, lambda k: 4)
    d.rectangle([mx, my, mx + ms, my + ms], outline=rgb(pal[2]), width=max(2, round(border * 0.9)))
    d.rectangle([0, 0, W - 1, W - 1], outline=rgb(pal[2]), width=border)
    return img.resize((size, size), Image.LANCZOS)

if __name__ == '__main__':
    if sys.argv[1] == 'preview':
        sheet = Image.new('RGB', (256 * 2 + 64 * 2 + 40, 256 + 20), (255, 255, 255))
        sheet.paste(compose(256, False), (0, 0))
        sheet.paste(compose(256, True), (256, 0))
        sheet.paste(compose(64, False), (512 + 10, 0))
        sheet.paste(compose(64, True), (512 + 64 + 20, 0))
        sheet.save('/tmp/ic/staged.png')
        print('ok')
    elif sys.argv[1] == 'make':
        out = sys.argv[2]
        for dpi, size in (('mdpi', 64), ('hdpi', 96), ('xhdpi', 128), ('xxhdpi', 192), ('xxxhdpi', 256)):
            for night in (False, True):
                dd = os.path.join(out, 'drawable%s-%s' % ('-night' if night else '', dpi))
                os.makedirs(dd, exist_ok=True)
                compose(size, night).save(os.path.join(dd, 'minorfinder.png'))
        print('made')
