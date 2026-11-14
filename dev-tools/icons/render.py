#!/usr/bin/env python3
"""Renders the drawing commands written by icontool (the last frame) to PNG icons.

  render.py CMDS.txt OUT_PREFIX --crop X Y W H [--night] [--keep 3,4,5] [--sizes 64,96,...]
  render.py CMDS.txt OUT.png --crop X Y W H --size 256 [--night ...]

The night palette is made the way GameView.refreshColours does it for the extra puzzles with
autoNightColours (background black, other colours via autoNightColour / pictureNightColour).
"""
import sys, argparse, colorsys, math
from PIL import Image, ImageDraw, ImageFont

FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
SS = 4   # supersampling

def parse(path):
    pal = []; cmds = []; size = None
    with open(path, errors='replace') as f:
        for line in f:
            line = line.rstrip('\n')
            if line.startswith('SIZE'):
                size = tuple(int(v) for v in line.split()[1:3])
            elif line.startswith('PALETTE'):
                v = line.split(); n = int(v[1])
                pal = [(float(v[2+3*i]), float(v[3+3*i]), float(v[4+3*i])) for i in range(n)]
            elif line == 'FRAME':
                cmds = []
            elif line == 'END':
                break
            else:
                cmds.append(line)
    return size, pal, cmds

def hsl(c):
    h, l, s = colorsys.rgb_to_hls(*c)
    return h, s, l

def from_hsl(h, s, l):
    return colorsys.hls_to_rgb(h, l, s)

def night_palette(pal, keep, bg_l=0.8, exact=()):
    out = [(0.0, 0.0, 0.0)]
    for i in range(1, len(pal)):
        if i in exact:
            out.append(pal[i]); continue
        h, s, l = hsl(pal[i])
        if i in keep:
            l = max(l, 0.28)
        else:
            if s < 0.25:
                d = l - bg_l
                l = 0.08 + d if d > 0 else -d * 1.25
            elif l > 0.7:
                l = 1 - l + 0.05
            elif l < 0.45:
                l = l + 0.3
        l = min(1.0, max(0.0, l))
        out.append(from_hsl(h, s, l))
    return out

def to_rgb(c):
    return tuple(int(round(255 * min(1, max(0, v)))) for v in c)

def render(cmds, pal, crop, size, lw_px):
    cx, cy, cw, ch = crop
    scale = size * SS / cw
    W = H = size * SS
    img = Image.new('RGB', (W, H), to_rgb(pal[0]))
    d = ImageDraw.Draw(img)
    clip = None
    def X(v): return (v - cx) * scale
    def Y(v): return (v - cy) * scale
    def col(i): return to_rgb(pal[i])
    lw = max(1, int(round(lw_px * SS)))
    for line in cmds:
        t = line.split(' ')
        k = t[0]
        if k == 'R':
            x, y, w, h, c = (int(v) for v in t[1:6])
            d.rectangle([X(x), Y(y), X(x + w) - 1, Y(y + h) - 1], fill=col(c))
        elif k == 'L':
            x1, y1, x2, y2, c = (int(v) for v in t[1:6])
            d.line([X(x1 + 0.5), Y(y1 + 0.5), X(x2 + 0.5), Y(y2 + 0.5)], fill=col(c), width=lw)
        elif k == 'TL':
            th = float(t[1]); x1, y1, x2, y2 = (float(v) for v in t[2:6]); c = int(t[6])
            d.line([X(x1 + 0.5), Y(y1 + 0.5), X(x2 + 0.5), Y(y2 + 0.5)], fill=col(c), width=max(lw, int(round(th * scale))))
        elif k == 'P':
            th = float(t[1]); f = int(t[2]); o = int(t[3]); n = int(t[4])
            xy = [int(v) for v in t[5:5 + 2 * n]]
            pts = [(X(xy[2*i] + 0.5), Y(xy[2*i+1] + 0.5)) for i in range(n)]
            if f >= 0: d.polygon(pts, fill=col(f))
            if o >= 0: d.line(pts + [pts[0]], fill=col(o), width=lw, joint='curve')
        elif k == 'C':
            th = float(t[1]); cx0, cy0, r = (float(v) for v in t[2:5]); f = int(t[5]); o = int(t[6])
            bb = [X(cx0 + 0.5) - r * scale, Y(cy0 + 0.5) - r * scale, X(cx0 + 0.5) + r * scale, Y(cy0 + 0.5) + r * scale]
            d.ellipse(bb, fill=col(f) if f >= 0 else None, outline=col(o) if o >= 0 else None, width=lw)
        elif k == 'T':
            x, y, ft, fs, al, c = (int(v) for v in t[1:7]); text = ' '.join(t[7:])
            font = ImageFont.truetype(FONT, max(1, int(round(fs * scale * 0.82))))
            anchor = 'l'
            h = al & 3
            anchor = {0: 'l', 1: 'm', 2: 'r'}[h]
            anchor += 'm' if al & 0x100 else 's'
            d.text((X(x), Y(y)), text, fill=col(c), font=font, anchor=anchor)
        # CLIP / UNCLIP: only the first level is honoured (not needed for the icons)
    return img.resize((size, size), Image.LANCZOS)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cmds'); ap.add_argument('out')
    ap.add_argument('--crop', nargs=4, type=int, required=True)
    ap.add_argument('--night', action='store_true')
    ap.add_argument('--keep', default='')
    ap.add_argument('--exact', default='')
    ap.add_argument('--size', type=int, default=0)
    ap.add_argument('--lw', type=float, default=0.0, help='line width in icon pixels (default size/100, min 1)')
    a = ap.parse_args()
    gsize, pal, cmds = parse(a.cmds)
    keep = {int(v) for v in a.keep.split(',') if v}
    exact = {int(v) for v in a.exact.split(',') if v}
    if a.night:
        pal = night_palette(pal, keep, exact=exact)
    sizes = [a.size] if a.size else [64, 96, 128, 192, 256]
    for sz in sizes:
        lw = a.lw if a.lw else max(1.0, sz / 100.0)
        img = render(cmds, pal, a.crop, sz, lw)
        out = a.out if a.size else '%s_%d.png' % (a.out, sz)
        img.save(out)
        print(out)

if __name__ == '__main__':
    main()
