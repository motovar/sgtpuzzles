/* checks the walls of generated mazes: every wall must be seen the same from both sides,
 * and every cell must be reachable from the target.  Compiled into the maze library itself
 * (-DORIG for the original names). */
#include "puzzles.h"
#ifdef ORIG
#include "maze3dc.h"
#include "maze3dc.c"
#define NEW(a,b,c,d,e,f) maze_new(a,b,c,d,e,f)
#define FREE(m) maze_free(m)
#else
#include "maze3dc.c"
#define NEW(a,b,c,d,e,f) m3d_new(a,b,c,d,e,f)
#define FREE(m) m3d_free(m)
#endif

int main(void)
{
    static const int dims[][3] = {{3,2,1},{3,3,3},{4,4,4},{5,5,5},{8,8,1},{10,10,10},{20,20,1},{6,3,4},{2,3,2},{12,7,9}};
    int bad = 0, total = 0, mazes = 0, nasym = 0, nunr = 0; long solid = 0;
    static int byd[32][2][4][2];
    int di, seed, dbl, tgt;
    for (di = 0; di < (int)lenof(dims); di++)
      for (dbl = 0; dbl < 2; dbl++)
        for (tgt = 0; tgt < 4; tgt++)
          for (seed = 1; seed <= 60; seed++) {
            MAZE *m = NEW(dims[di][0], dims[di][1], dims[di][2], tgt, dbl, seed * 7919);
            int x, y, z, asym = 0, unreach = 0;
            static const int dx[6] = {1,-1,0,0,0,0}, dy[6] = {0,0,1,-1,0,0}, dz[6] = {0,0,0,0,1,-1};
            if (!m) { printf("NULL maze %dx%dx%d\n", dims[di][0], dims[di][1], dims[di][2]); bad++; continue; }
            mazes++;
            for (x = 0; x < m->xx; x++) for (y = 0; y < m->yy; y++) for (z = 0; z < m->zz; z++) {
                int d;
                MAZECELL c = *mazecell(m, x, y, z);
                for (d = 0; d < 6; d++) {
                    int nx = x + dx[d], ny = y + dy[d], nz = z + dz[d];
                    int w1 = (c >> d) & 1, w2;
                    if (nx < 0 || nx >= m->xx || ny < 0 || ny >= m->yy || nz < 0 || nz >= m->zz) {
                        if (!w1) asym++;      /* no wall at the edge of the maze */
                        continue;
                    }
                    w2 = (*mazecell(m, nx, ny, nz) >> (d ^ 1)) & 1;
                    if (w1 != w2) asym++;
                }
            }
            /* flood from the target, using the doors as the cells themselves have them */
            {
                int n = m->xx * m->yy * m->zz, head = 0, tail = 0, i, seen = 0;
                int *q = snewn(n, int); char *vis = snewn(n, char);
                memset(vis, 0, n);
                i = (m->tar_x * m->yy + m->tar_y) * m->zz + m->tar_z; vis[i] = 1; q[tail++] = i;
                while (head < tail) {
                    int cc = q[head++], cz = cc % m->zz, cy = (cc / m->zz) % m->yy, cx = cc / (m->zz * m->yy), d;
                    seen++;
                    for (d = 0; d < 6; d++) {
                        int nx = cx + dx[d], ny = cy + dy[d], nz = cz + dz[d], nb;
                        if (nx < 0 || nx >= m->xx || ny < 0 || ny >= m->yy || nz < 0 || nz >= m->zz) continue;
                        if ((*mazecell(m, cx, cy, cz) >> d) & 1) continue;
                        nb = (nx * m->yy + ny) * m->zz + nz;
                        if (!vis[nb]) { vis[nb] = 1; q[tail++] = nb; }
                    }
                }
                {
                    int c2, init_reached = 0, island = 0;
                    for (c2 = 0; c2 < n; c2++) {
                        int isinit = (m->cell[c2] & 0x40) != 0;
                        if (vis[c2] && isinit) init_reached++;
                        if (!vis[c2] && !isinit) island++;
                    }
                    unreach = init_reached + island;   /* real faults only; unbuilt rock is fine */
                    solid += n - seen - island;
                }
                sfree(q); sfree(vis);
            }
            total++;
            if (asym) { nasym++; byd[di][dbl][tgt][0]++; }
            if (unreach) { nunr++; byd[di][dbl][tgt][1]++; }
            if (asym || unreach) {
                bad++;
                if (0) printf("%dx%dx%d double=%d target=%d seed=%d: %d one-sided walls, %d cells not reachable\n",
                                     dims[di][0], dims[di][1], dims[di][2], dbl, tgt, seed * 7919, asym, unreach);
            }
            FREE(m);
        }
    printf("%d mazes, %d with faults (%d one-sided walls, %d with unreachable built cells / doors into rock); %ld unbuilt rock cells in total\n", total, bad, nasym, nunr, solid);
    for (di = 0; di < (int)lenof(dims); di++)
      for (dbl = 0; dbl < 2; dbl++)
        for (tgt = 0; tgt < 4; tgt++)
          if (byd[di][dbl][tgt][0] || byd[di][dbl][tgt][1])
            printf("  %dx%dx%d double=%d target=%d: one-sided %d, unreachable %d\n", dims[di][0], dims[di][1], dims[di][2], dbl, tgt, byd[di][dbl][tgt][0], byd[di][dbl][tgt][1]);
    return bad != 0;
}
