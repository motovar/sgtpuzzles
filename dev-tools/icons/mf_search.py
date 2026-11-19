#!/usr/bin/env python3
"""Search for a good icon state of Minor Finder: mf_search.py PARAMS FIRST_SEED LAST_SEED"""
import subprocess, sys, math, itertools, os
sys.path.insert(0, os.path.dirname(__file__))
import render

TOOL = os.environ.get('ICONTOOL', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'b', 'icontool_minorfinder'))

def run(params, seed, steps, out='/tmp/ic/s.txt', size=512):
    args = [TOOL, out, str(size), '%s#%s' % (params, seed)] + ['step'] * steps
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0:
        return None
    return render.parse(out)

def seg_cross(a, b, c, d):
    def o(p, q, r):
        return (q[0]-p[0])*(r[1]-p[1]) - (q[1]-p[1])*(r[0]-p[0])
    if a == c or a == d or b == c or b == d:
        return False
    return o(a,b,c)*o(a,b,d) < 0 and o(c,d,a)*o(c,d,b) < 0

def analyse(size, pal, cmds):
    # panels: the first two outline rectangles are not recorded as such; take the R commands
    rects = [tuple(int(v) for v in c.split()[1:6]) for c in cmds if c.startswith('R ')]
    # R: x y w h colour; the background of the window, then minor panel, then big panel
    panels = [r for r in rects if r[4] == 1 or r[4] == 12 or r[4] == 13]
    pts = []; edges = []
    for c in cmds:
        t = c.split(' ')
        if t[0] == 'C':
            pts.append((float(t[2]), float(t[3]), float(t[4]), int(t[5])))
        elif t[0] == 'TL':
            edges.append((float(t[2]), float(t[3]), float(t[4]), float(t[5]), int(t[6])))
    return panels, pts, edges

if __name__ == '__main__':
    params = sys.argv[1]
    s0, s1 = int(sys.argv[2]), int(sys.argv[3])
    for seed in range(s0, s1 + 1):
        r = run(params, 'ic%d' % seed, 0)
        if not r: continue
        size, pal, cmds = r
        panels, pts, edges = analyse(size, pal, cmds)
        print(seed, size, panels[:3], len(pts), len(edges))
        break
