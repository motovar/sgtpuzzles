/* smoke.c: generic per-game smoke test through the midend.
 *   smoke <nseeds> [preset id (0 = all)] [maxsteps] [first seed]
 * For every preset and seed: new game (time), then "next step" (hint) until the game is solved
 * (steps, time of all steps, worst single step), then Solve on a fresh copy of the same game.
 * Prints one line per game and a summary per preset.  Exit status 1 if anything failed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "puzzles.h"

extern const game GAMESYM;
extern const drawing_api null_drawing_api;

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

struct ent { char *title; game_params *params; };
static struct ent ents[200]; static int nents;

static void flatten(struct preset_menu *m)
{
    int i;
    for (i = 0; i < m->n_entries; i++) {
        if (m->entries[i].params) {
            ents[nents].title = m->entries[i].title;
            ents[nents].params = m->entries[i].params;
            nents++;
        } else flatten(m->entries[i].submenu);
    }
}

int main(int argc, char **argv)
{
    int nseeds = argc > 1 ? atoi(argv[1]) : 5;
    int only = argc > 2 ? atoi(argv[2]) : 0;
    int maxsteps = argc > 3 ? atoi(argv[3]) : 3000;
    int seed0 = argc > 4 ? atoi(argv[4]) : 1;
    const game *g = &GAMESYM;
    midend *me = midend_new(NULL, g, &null_drawing_api, NULL);
    int id, failed = 0;
    int limit;

    flatten(midend_get_presets(me, &limit));
    for (id = 0; id < nents; id++) {
        int s, nfail = 0, nsolved = 0, nsolvefail = 0;
        double tgen = 0, tgenmax = 0, thint = 0, thintmax = 0, tstepmax = 0, tsolve = 0;
        long steps = 0; int stepsmax = 0;
        char *pstr;
        if (only && id + 1 != only) continue;
        pstr = g->encode_params(ents[id].params, true);
        for (s = 0; s < nseeds; s++) {
            char idbuf[400];
            const char *err;
            double t0, t1;
            int n = 0, st;
            snprintf(idbuf, sizeof idbuf, "%s#%d", pstr, seed0 + s);
            err = midend_game_id(me, idbuf);
            if (err) { printf("%s: game id error: %s\n", idbuf, err); nfail++; continue; }
            t0 = now();
            midend_new_game(me);
            t1 = now();
            tgen += t1 - t0; if (t1 - t0 > tgenmax) tgenmax = t1 - t0;
            while ((st = midend_status(me)) == 0 && n < maxsteps) {
                double a = now(), b;
                const char *e = midend_solve_step(me);
                b = now();
                thint += b - a; if (b - a > tstepmax) tstepmax = b - a;
                if (e) { printf("%s: step %d: %s\n", idbuf, n, e); break; }
                n++;
            }
            st = midend_status(me);
            if (st == 1) { nsolved++; steps += n; if (n > stepsmax) stepsmax = n; }
            else { nfail++; printf("%s: NOT solved by hints (status %d, %d steps)\n", idbuf, st, n); }
            /* Solve on a fresh copy */
            if (g->can_solve) {
                const char *e;
                midend_game_id(me, idbuf);
                midend_new_game(me);
                t0 = now();
                e = midend_solve(me);
                tsolve += now() - t0;
                if (e) { nsolvefail++; printf("%s: Solve: %s\n", idbuf, e); }
                else if (midend_status(me) != 1) { nsolvefail++; printf("%s: Solve did not complete the game (status %d)\n", idbuf, midend_status(me)); }
            }
        }
        printf("%-28s %-26s gen avg %.3f max %.3f | hint: solved %d/%d steps avg %.1f max %d time/game %.3f worst step %.3f | solve fail %d (%.3fs)\n",
               ents[id].title, pstr, tgen / nseeds, tgenmax, nsolved, nseeds,
               nsolved ? (double)steps / nsolved : 0.0, stepsmax, thint / nseeds, tstepmax, nsolvefail, tsolve);
        failed += nfail + nsolvefail;
        sfree(pstr);
    }
    midend_free(me);
    return failed ? 1 : 0;
}
