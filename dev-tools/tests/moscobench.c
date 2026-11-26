/* moscobench.c: how long does an annealing iteration of the generator take?
 *   moscobench <w> <h> <level> <iters-per-tile>   (includes mosco.c itself)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define MOSCO_DEBUG
#include "mosco.c"

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

int main(int argc, char **argv)
{
    int w = atoi(argv[1]), h = atoi(argv[2]), level = atoi(argv[3]);
    long ipt = atol(argv[4]), iters;
    int i, k = 2 * w + 2 * h + 4, n = w * h, fails = 0, goes = 0;
    int *zeros = snewn(k, int);
    struct mosco_clues *cl;
    struct gen *g;
    double t0, dt = 0;

    for (i = 0; i < k; i++) zeros[i] = 0;
    cl = clues_new(w, h, zeros);
    g = gen_new(cl, level, 12345);
    iters = ipt * n;
    for (i = 0; i < 6; i++) {
        bool ok;
        t0 = now();
        ok = gen_attempt(g, iters);
        if (!ok) { dt += now() - t0; fails++; }
        goes++;
    }
    printf("%dx%d level %d: %d goes, %d failed (full length %ld iterations): %.3f s per failed go = %.0f ns per iteration = %.1f ns per iteration*tile\n",
           w, h, level, goes, fails, iters, fails ? dt / fails : 0.0,
           fails ? dt / fails / iters * 1e9 : 0.0, fails ? dt / fails / iters / n * 1e9 : 0.0);
    return 0;
}
