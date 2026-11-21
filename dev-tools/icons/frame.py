#!/usr/bin/env python3
"""Renders the whole last frame that icontool wrote, as a picture of the same shape as the game's.

  frame.py [--zoom 2] [--night] [--keep 6,7,...] OUT.png CMDS.txt
Several command files can be put side by side with --strip: frame.py --strip OUT.png a.txt b.txt ...
CLIP / UNCLIP are honoured (the clipped part is drawn on a copy and only its clip box is pasted back).
"""
import sys, os, argparse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from render import parse, night_palette, to_rgb, FONT, SS
from PIL import Image, ImageDraw, ImageFont


def render_rect(size, cmds, pal, zoom, lw_px=1.0):
    gw, gh = size
    scale = zoom * SS
    W, H = int(gw * scale), int(gh * scale)
    img = Image.new('RGB', (W, H), to_rgb(pal[0]))
    d = ImageDraw.Draw(img)
    layer = None
    clip_box = None

    def X(v): return v * scale

    def Y(v): return v * scale

    def col(i): return to_rgb(pal[i])
    lw = max(1, int(round(lw_px * SS)))
    for line in cmds:
        t = line.split(' ')
        k = t[0]
        if k == 'CLIP':
            x, y, w, h = (int(v) for v in t[1:5])
            clip_box = (int(round(X(x))), int(round(Y(y))), int(round(X(x + w))), int(round(Y(y + h))))
            layer = img.copy()
            d = ImageDraw.Draw(layer)
        elif k == 'UNCLIP':
            if layer is not None:
                img.paste(layer.crop(clip_box), clip_box[:2])
            layer = None
            d = ImageDraw.Draw(img)
        elif k == 'R':
            x, y, w, h, c = (int(v) for v in t[1:6])
            d.rectangle([X(x), Y(y), X(x + w) - 1, Y(y + h) - 1], fill=col(c))
        elif k == 'L':
            x1, y1, x2, y2, c = (int(v) for v in t[1:6])
            d.line([X(x1 + 0.5), Y(y1 + 0.5), X(x2 + 0.5), Y(y2 + 0.5)], fill=col(c), width=lw)
        elif k == 'TL':
            th = float(t[1]); x1, y1, x2, y2 = (float(v) for v in t[2:6]); c = int(t[6])
            d.line([X(x1 + 0.5), Y(y1 + 0.5), X(x2 + 0.5), Y(y2 + 0.5)], fill=col(c),
                   width=max(lw, int(round(th * scale))))
        elif k == 'P':
            f = int(t[2]); o = int(t[3]); n = int(t[4])
            xy = [int(v) for v in t[5:5 + 2 * n]]
            pts = [(X(xy[2 * i] + 0.5), Y(xy[2 * i + 1] + 0.5)) for i in range(n)]
            if f >= 0:
                d.polygon(pts, fill=col(f))
            if o >= 0:
                d.line(pts + [pts[0]], fill=col(o), width=lw, joint='curve')
        elif k == 'C':
            f = int(t[5]); o = int(t[6])
            cx0, cy0, r = (float(v) for v in t[2:5])
            bb = [X(cx0 + 0.5) - r * scale, Y(cy0 + 0.5) - r * scale,
                  X(cx0 + 0.5) + r * scale, Y(cy0 + 0.5) + r * scale]
            d.ellipse(bb, fill=col(f) if f >= 0 else None, outline=col(o) if o >= 0 else None, width=lw)
        elif k == 'T':
            x, y, ft, fs, al, c = (int(v) for v in t[1:7])
            text = ' '.join(t[7:])
            font = ImageFont.truetype(FONT, max(1, int(round(fs * scale * 0.82))))
            hh = al & 3
            anchor = {0: 'l', 1: 'm', 2: 'r'}[hh]
            anchor += 'm' if al & 0x100 else 's'
            d.text((X(x), Y(y)), text, fill=col(c), font=font, anchor=anchor)
    if layer is not None:      # a clip that was never closed
        img.paste(layer.crop(clip_box), clip_box[:2])
    return img.resize((W // SS, H // SS), Image.LANCZOS)


def load(path, night, keep):
    size, pal, cmds = parse(path)
    if night:
        pal = night_palette(pal, keep)
    return size, pal, cmds


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--strip', action='store_true')
    ap.add_argument('--zoom', type=float, default=2.0)
    ap.add_argument('--night', action='store_true')
    ap.add_argument('--keep', default='')
    ap.add_argument('out')
    ap.add_argument('cmds', nargs='+')
    a = ap.parse_args()
    keep = {int(v) for v in a.keep.split(',') if v}
    imgs = []
    for c in (a.cmds if a.strip else a.cmds[:1]):
        size, pal, cmds = load(c, a.night, keep)
        imgs.append(render_rect(size, cmds, pal, a.zoom))
    gap = 8
    W = sum(i.width for i in imgs) + gap * (len(imgs) - 1)
    H = max(i.height for i in imgs)
    out = Image.new('RGB', (W, H), (255, 255, 255))
    x = 0
    for i in imgs:
        out.paste(i, (x, 0))
        x += i.width + gap
    out.save(out_path := a.out)
    print(out_path, out.size)


if __name__ == '__main__':
    main()
