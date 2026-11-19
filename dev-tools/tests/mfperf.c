/* How hard is the first hint of a generated game?  mfperf <seeds> [limit] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "minorfinder.c"

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static int cmpl(const void *a, const void *b)
{
    long x = *(const long *)a, y = *(const long *)b;
    return x < y ? -1 : x > y;
}

int main(int argc, char **argv)
{
    int seeds = argc > 1 ? atoi(argv[1]) : 1000;
    long limit = argc > 2 ? atol(argv[2]) : SOLVE_NODES;
    int damage = argc > 3 ? atoi(argv[3]) : 0;       /* delete this many random points / edges first */
    int only_nb = argc > 4 ? atoi(argv[4]) : 0;
    int mode, nb, nm;
    for (mode = 0; mode < 2; mode++)
        for (nb = 15; nb <= 23; nb++)
            for (nm = 4; nm <= 6; nm++) {
                game_params p;
                long *nodes = malloc(sizeof(long) * seeds);
                int seed, giveups = 0, nos = 0;
                double t0, worst = 0, total = 0;
                p.mode = mode; p.n_base = nb; p.n_min = nm;
                if (validate_params(&p, true) || (only_nb && (nb != only_nb))) { free(nodes); continue; }
                for (seed = 0; seed < seeds; seed++) {
                    char sbuf[32], *aux = NULL, *desc;
                    random_state *r;
                    game_state *s;
                    struct msolver S;
                    int i, rc;
                    mask_t hadj[MAXM];
                    sprintf(sbuf, "p%d", seed);
                    r = random_new(sbuf, strlen(sbuf));
                    desc = new_game_desc(&p, r, &aux, false);
                    s = new_game(NULL, &p, desc);
                    for (i = 0; i < nm; i++) hadj[i] = s->sh->madj[i];
                    for (i = 0; i < damage; i++) {
                        int v = (int)random_upto(r, nb), w = (int)random_upto(r, nb);
                        if (getenv("MF_CONTRACT")) { if (s->g.adj[v] >> w & 1) bg_contract(&s->g, v, w); }
                        else if (i & 1) { if (s->g.adj[v] >> w & 1) bg_delete_edge(&s->g, v, w); }
                        else if (s->g.alive >> v & 1) bg_delete_vertex(&s->g, v);
                    }
                    ms_init(&S, &s->g, hadj, nm, limit);
                    t0 = now();
                    rc = ms_solve(&S);
                    t0 = now() - t0;
                    total += t0;
                    if (t0 > worst) worst = t0;
                    nodes[seed] = S.nodes;
                    if (rc < 0) giveups++;
                    if (rc == 0) nos++;
                    free_game(s); sfree(desc); sfree(aux); random_free(r);
                }
                qsort(nodes, seeds, sizeof(long), cmpl);
                printf("%s %2d/%d: median %ld, 99%% %ld, 99.9%% %ld, max %ld nodes; %d gave up, %d refused; worst %.3f s, mean %.5f s\n",
                       mode ? "special" : "default", nb, nm, nodes[seeds / 2], nodes[seeds * 99 / 100],
                       nodes[seeds * 999 / 1000], nodes[seeds - 1], giveups, nos, worst, total / seeds);
                free(nodes);
            }
    return 0;
}
