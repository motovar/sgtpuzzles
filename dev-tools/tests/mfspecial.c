#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "minorfinder.c"
int main(void)
{
    int nb, nm, seed;
    for (nm = 5; nm <= 6; nm++) {
        long e5 = 0, e33 = 0, other = 0;
        for (nb = 19; nb <= 23; nb++)
            for (seed = 0; seed < 200; seed++) {
                game_params p;
                char sbuf[32], *aux = NULL, *desc;
                random_state *r;
                game_state *s;
                int i, regular = 1, d0;
                p.mode = 1; p.n_base = nb; p.n_min = nm;
                sprintf(sbuf, "k%d", seed);
                r = random_new(sbuf, strlen(sbuf));
                desc = new_game_desc(&p, r, &aux, false);
                s = new_game(NULL, &p, desc);
                d0 = POPCOUNT(s->sh->madj[0]);
                for (i = 0; i < nm; i++) if (POPCOUNT(s->sh->madj[i]) != d0) regular = 0;
                if (nm == 5 && s->sh->nmedges == 10 && regular && d0 == 4) e5++;
                else if (nm == 6 && s->sh->nmedges == 9 && regular && d0 == 3) e33++;
                else other++;
                free_game(s); sfree(desc); sfree(aux); random_free(r);
            }
        printf("n_min %d: %ld K5, %ld K3,3-like (9 edges, 3-regular), %ld other\n", nm, e5, e33, other);
    }
    return 0;
}
