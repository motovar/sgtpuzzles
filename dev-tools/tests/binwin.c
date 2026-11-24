#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "binary.c"
static double now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }
int main(int argc, char **argv)
{
    int w = atoi(argv[1]), h = atoi(argv[2]), goal = atoi(argv[3]), rules = atoi(argv[4]), games = atoi(argv[5]), g, wins = 0, maxmoves = atoi(argv[6]);
    long totmoves = 0, minmoves = 1 << 30, best = 0;
    double t0 = now();
    for (g = 0; g < games; g++) {
        game_params p; random_state *rs; char *aux = NULL, *desc, *hint; const char *err = NULL;
        game_state *s; int moves = 0; char seed[32];
        sprintf(seed, "win%d", g);
        rs = random_new(seed, strlen(seed));
        p.w = w; p.h = h; p.goal = goal; p.mode = MODE_RECT; p.rules = rules;
        desc = new_game_desc(&p, rs, &aux, false);
        s = new_game(NULL, &p, desc);
        while (!s->finished && !s->won && moves < maxmoves) {
            game_state *n;
            hint = hint_game(s, s, &err);
            if (!hint) break;
            n = execute_move(s, hint);
            sfree(hint);
            if (!n) break;
            free_game(s); s = n; moves++;
        }
        if (s->won) { wins++; totmoves += moves; if (moves < minmoves) minmoves = moves; }
        { int i, m = 0; for (i = 0; i < w * h; i++) if (s->tiles[i] > m) m = s->tiles[i]; if (m > best) best = m; }
        free_game(s); sfree(desc); sfree(aux); random_free(rs);
    }
    printf("%dx%d goal %d rules %d: won %d of %d games, avg %.0f moves, biggest tile 2^%ld, %.1f s\n", w, h, goal, rules, wins, games, wins ? (double)totmoves / wins : 0.0, best, now() - t0);
    return 0;
}
