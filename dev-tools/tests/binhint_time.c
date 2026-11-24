#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "binary.c"
static double now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }
int main(int argc, char **argv)
{
    int w = atoi(argv[1]), h = atoi(argv[2]), rules = atoi(argv[3]), i;
    game_params p; random_state *rs = random_new("dbg3", 4); char *aux = NULL, *desc, *hint; const char *err = NULL;
    game_state *s; double t0;
    p.w = w; p.h = h; p.goal = GOAL_ENDLESS; p.mode = MODE_RECT; p.rules = rules;
    desc = new_game_desc(&p, rs, &aux, false);
    s = new_game(NULL, &p, desc);
    for (i = 0; i < 3; i++) {
        game_state *n;
        t0 = now();
        hint = hint_game(s, s, &err);
        t0 = now() - t0;
        printf("%dx%d rules %d, move %d: hint %s in %.3f s\n", w, h, rules, i, hint ? hint : "(none)", t0);
        if (!hint) break;
        n = execute_move(s, hint);
        sfree(hint);
        if (!n) break;
        free_game(s); s = n;
    }
    return 0;
}
