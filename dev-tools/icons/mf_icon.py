#!/usr/bin/env python3
"""Icons for Minor Finder.

  mf_icon.py search PARAMS FIRST LAST     contact sheet of candidate states (/tmp/ic/sheet*.png)
  mf_icon.py make PARAMS SEED STEPS OUTDIR   the ten icons (day and night, five densities)

The positions of the points and edges are those of the game (recorded by icontool); the icon
draws them bolder than the game does, with the minor as a little picture in the corner.
"""
import subprocess, sys, math, os, itertools
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import render
from PIL import Image, ImageDraw

TOOL = os.environ.get('ICONTOOL', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'b', 'icontool_minorfinder'))
SS = 4

def run(params, seed, steps, out='/tmp/ic/s.txt', size=512):
    args = [TOOL, out, str(size), '%s#%s' % (params, seed)] + ['step'] * steps
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0:
        return None
    return render.parse(out)

def state_of(size, pal, cmds):
    rects = [tuple(int(v) for v in c.split()[1:6]) for c in cmds if c.startswith('R ')]
    rects = [r for r in rects if r[4] != 0]          # not the background of the window
    minor, big = rects[0], rects[1]
    circles = []; lines = []
    for c in cmds:
        t = c.split(' ')
        if t[0] == 'C':
            circles.append((float(t[2]), float(t[3]), float(t[4]), int(t[5])))
        elif t[0] == 'TL':
            lines.append((float(t[2]), float(t[3]), float(t[4]), float(t[5]), int(t[6])))
    def inside(p, r):
        return r[0] <= p[0] <= r[0] + r[2] and r[1] <= p[1] <= r[1] + r[3]
    st = {'pal': pal}
    for name, r in (('m', minor), ('b', big)):
        st[name + 'pts'] = [((x - r[0]) / r[2], (y - r[1]) / r[3], col) for (x, y, rad, col) in circles if inside((x, y), r)]
        st[name + 'edges'] = [((x1 - r[0]) / r[2], (y1 - r[1]) / r[3], (x2 - r[0]) / r[2], (y2 - r[1]) / r[3], col)
                              for (x1, y1, x2, y2, col) in lines if inside((x1, y1), r) and inside((x2, y2), r)]
    return st

def seg_cross(a, b, c, d):
    def o(p, q, r):
        return (q[0]-p[0])*(r[1]-p[1]) - (q[1]-p[1])*(r[0]-p[0])
    if a == c or a == d or b == c or b == d:
        return False
    return o(a,b,c)*o(a,b,d) < 0 and o(c,d,a)*o(c,d,b) < 0

def crossings(edges):
    n = 0
    for e, f in itertools.combinations(edges, 2):
        if seg_cross(e[:2], e[2:4], f[:2], f[2:4]):
            n += 1
    return n

def score(st, inset=0.40):
    bp = [p for p in st['bpts']]
    be = st['bedges']
    mp = st['mpts']; me = st['medges']
    if not (7 <= len(bp) <= 11) or len(mp) < 4:
        return -1e9
    if len(be) < len(bp) + 1 or len(be) > len(bp) + 7:
        return -1e9
    if crossings(me) > 0 or len(me) < 4:
        return -1e9
    s = 0.0
    # nothing under the inset in the top left corner
    lim = inset + 0.07
    for (x, y, c) in bp:
        if x < lim + 0.03 and y < lim + 0.03:
            s -= 1000
    for (x1, y1, x2, y2, c) in be:
        # sample along the edge
        for t in (0.25, 0.5, 0.75):
            x = x1 + (x2 - x1) * t; y = y1 + (y2 - y1) * t
            if x < lim and y < lim:
                s -= 30
    s -= 10 * crossings(me)
    s -= 4 * crossings(be)
    # points should not be crowded
    for (a, b) in itertools.combinations(bp, 2):
        d = math.hypot(a[0] - b[0], a[1] - b[1])
        if d < 0.12:
            s -= 40
    # spread
    xs = [p[0] for p in bp]; ys = [p[1] for p in bp]
    s += 30 * (max(xs) - min(xs)) + 30 * (max(ys) - min(ys))
    for (x, y, c) in bp:
        if min(x, y, 1 - x, 1 - y) < 0.07:
            s -= 20
    return s

def night_pal(pal, keep=(3, 4, 5, 6, 7, 10)):
    return render.night_palette(pal, set(keep))

def compose(st, size, night=False, inset=0.40, margin=0.045, corner='tl'):
    pal = night_pal(st['pal']) if night else st['pal']
    rgb = render.to_rgb
    W = size * SS
    img = Image.new('RGB', (W, W), rgb(pal[1] if not night else pal[0]))
    d = ImageDraw.Draw(img)
    border = max(1, round(size / 64 * SS)) if size >= 96 else SS
    edge_w = max(2, round(size * 0.030 * SS))
    rad = size * 0.050 * SS
    def draw_graph(pts, edges, x0, y0, side, scale):
        for (x1, y1, x2, y2, c) in edges:
            d.line([x0 + x1 * side, y0 + y1 * side, x0 + x2 * side, y0 + y2 * side],
                   fill=rgb(pal[9]), width=max(2, round(edge_w * scale)))
        for (x, y, c) in pts:
            r = rad * scale
            cx = x0 + x * side; cy = y0 + y * side
            d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=rgb(pal[c]), outline=rgb(pal[8]),
                      width=max(1, round(r * 0.18)))
    # the big graph over the whole picture
    pad = 0.03
    draw_graph(st['bpts'], st['bedges'], W * pad, W * pad, W * (1 - 2 * pad), 1.0)
    # the minor in a corner
    ms = W * inset
    mm = W * margin
    mx = mm if 'l' in corner else W - mm - ms
    my = mm if 't' in corner else W - mm - ms
    d.rectangle([mx, my, mx + ms, my + ms], fill=rgb(pal[1] if not night else pal[0]))
    draw_graph(st['mpts'], st['medges'], mx + ms * 0.06, my + ms * 0.06, ms * 0.88, 0.8)
    d.rectangle([mx, my, mx + ms, my + ms], outline=rgb(pal[2]), width=max(2, round(border * 0.8)))
    # the border of the board
    d.rectangle([0, 0, W - 1, W - 1], outline=rgb(pal[2]), width=border)
    return img.resize((size, size), Image.LANCZOS)

def search(params, s0, s1):
    cands = []
    for seed in range(s0, s1 + 1):
        for steps in range(0, 11):
            r = run(params, 'ic%d' % seed, steps)
            if not r:
                continue
            st = state_of(*r)
            sc = score(st)
            if sc > -500:
                cands.append((sc, seed, steps, st))
    cands.sort(key=lambda c: -c[0])
    top = cands[:24]
    for sheet in range(0, len(top), 12):
        chunk = top[sheet:sheet + 12]
        W = 256
        im = Image.new('RGB', (W * 4, W * 3), (255, 255, 255))
        for i, (sc, seed, steps, st) in enumerate(chunk):
            im.paste(compose(st, W), ((i % 4) * W, (i // 4) * W))
        im.save('/tmp/ic/sheet%d.png' % (sheet // 12))
        print('sheet', sheet // 12, [(c[1], c[2], round(c[0], 1)) for c in chunk])

if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'search':
        search(sys.argv[2], int(sys.argv[3]), int(sys.argv[4]))
    elif cmd == 'make':
        params, seed, steps, outdir = sys.argv[2], sys.argv[3], int(sys.argv[4]), sys.argv[5]
        st = state_of(*run(params, seed, steps))
        for dpi, size in (('mdpi', 64), ('hdpi', 96), ('xhdpi', 128), ('xxhdpi', 192), ('xxxhdpi', 256)):
            for night in (False, True):
                d = os.path.join(outdir, 'drawable%s-%s' % ('-night' if night else '', dpi))
                os.makedirs(d, exist_ok=True)
                compose(st, size, night).save(os.path.join(d, 'minorfinder.png'))
        print('made')
