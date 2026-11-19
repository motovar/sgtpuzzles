/* Compares the generator with the original one, and checks the games that it makes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "minorfinder.c"

extern char *orig_gen(const void *params, random_state *rs, char **aux);

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv)
{
    int nseeds = argc > 1 ? atoi(argv[1]) : 100;
    int seed0 = argc > 2 ? atoi(argv[2]) : 0;
    int mode, nb, nm, seed;
    long total = 0, differ = 0, invalid = 0, notgood = 0, nogen = 0;
    double tgen = 0, tgood = 0;

    for (mode = 0; mode < 2; mode++)
        for (nb = 15; nb <= 23; nb++)
            for (nm = 4; nm <= 6; nm++) {
                game_params p;
                long pdiff = 0, pbad = 0;
                p.mode = mode; p.n_base = nb; p.n_min = nm;
                if (validate_params(&p, true))
                    continue;
                for (seed = seed0; seed < seed0 + nseeds; seed++) {
                    char sbuf[32], *aux = NULL, *d1, *d2;
                    const char *err;
                    random_state *r1, *r2;
                    double t0;
                    sprintf(sbuf, "%d", seed);
                    r1 = random_new(sbuf, strlen(sbuf));
                    r2 = random_new(sbuf, strlen(sbuf));
                    t0 = now();
                    d1 = generate_once(&p, r1);
                    tgen += now() - t0;
                    d2 = orig_gen(&p, r2, &aux);
                    total++;
                    if (strcmp(d1, d2)) {
                        differ++; pdiff++;
                        if (pdiff <= 2) {
                            size_t i = 0;
                            while (d1[i] && d1[i] == d2[i]) i++;
                            printf("DIFF %d-%dx%d seed %d at %zu\n new: %s\n old: %s\n", mode, nb, nm, seed, i, d1, d2);
                        }
                    }
                    err = validate_desc(&p, d1);
                    if (err) {
                        invalid++; pbad++;
                        if (pbad <= 2) printf("INVALID %d-%dx%d seed %d: %s\n  %s\n", mode, nb, nm, seed, err, d1);
                    }
                    t0 = now();
                    if (!desc_is_good(&p, d1)) notgood++;
                    tgood += now() - t0;
                    sfree(d1); free(d2); sfree(aux);
                    random_free(r1); random_free(r2);
                }
                (void)nogen;
            }
    printf("%ld games: %ld differ from the original, %ld invalid, %ld not good; generating %.3f ms/game, checking %.3f ms/game\n",
           total, differ, invalid, notgood, 1000 * tgen / total, 1000 * tgood / total);
    return differ || invalid;
}
