#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "minorfinder.c"
int main(void)
{
    int mode, nb, nm, seed;
    long total = 0, disconnected = 0, isolated = 0, mind = 99;
    long deg_hist[8] = {0};
    for (mode = 0; mode < 2; mode++)
        for (nb = 15; nb <= 23; nb++)
            for (nm = 4; nm <= 6; nm++) {
                game_params p;
                p.mode = mode; p.n_base = nb; p.n_min = nm;
                if (validate_params(&p, true)) continue;
                for (seed = 0; seed < 300; seed++) {
                    char sbuf[32], *aux = NULL, *desc;
                    random_state *r;
                    game_state *s;
                    mask_t seen, front;
                    int i, ne = 0;
                    sprintf(sbuf, "c%d", seed);
                    r = random_new(sbuf, strlen(sbuf));
                    desc = new_game_desc(&p, r, &aux, false);
                    s = new_game(NULL, &p, desc);
                    seen = front = 1;
                    while (front) {
                        mask_t nbm = bg_nbrs(s->sh->madj, front) & ~seen;
                        seen |= nbm; front = nbm;
                    }
                    total++;
                    if (seen != ((mask_t)1 << nm) - 1) disconnected++;
                    for (i = 0; i < nm; i++) { if (!s->sh->madj[i]) isolated++; ne += POPCOUNT(s->sh->madj[i]); }
                    deg_hist[ne / 2 > 7 ? 7 : ne / 2]++;
                    free_game(s); sfree(desc); sfree(aux); random_free(r);
                }
            }
    printf("%ld games: %ld with a disconnected minor, %ld isolated minor points\n", total, disconnected, isolated);
    return 0;
}
