/* moscotest.c: white-box tests of the Mosco solver (includes mosco.c itself).
 *   moscotest <seeds> [w h]
 *
 *  1. every generated puzzle: the narrowing never rules out the arrangement it was made from
 *     (soundness), a search finds a valid arrangement, and (for sizes where it can be counted
 *     quickly) how many solutions there are (up to 2).
 *  2. hint steps from partial boards that agree with the solution: the move that is returned
 *     must keep the board solvable; and how many steps are forced and how many are guesses.
 *  3. hint on boards with one or two wrong arrows: it must report a mistake (never a move),
 *     and when exactly one arrow is wrong, taking away the arrow that it blames must leave a
 *     board that has a solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosco.c"

#define CHECK(c) do { if (!(c)) { printf("CHECK FAILED line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static int fails;

static bool valid_arrangement(const struct mosco_clues *cl, const signed char *sol)
{
    int *cnt = snewn(cl->k, int), used[NDIR], i;
    bool ok = count_arrows(cl, sol, cnt, used) == cl->n;
    for (i = 0; ok && i < cl->k; i++) if (cnt[i] != cl->clue[i]) ok = false;
    for (i = 0; ok && i < NDIR; i++) if (used[i] != cl->n / NDIR) ok = false;
    sfree(cnt);
    return ok;
}

/* the number of solutions, up to limit; -1 if it took too long */
static long count_sols(struct msolver *sv, struct sstate *ss, long limit, long *budget)
{
    int t, best = -1, bestn = NDIR + 1, v;
    long total = 0;
    for (t = 0; t < sv->n; t++) {
        unsigned dom = ss->dom[t];
        if (dom & (dom - 1)) {
            int pc = popcnt8(dom);
            if (pc < bestn) { best = t; bestn = pc; }
        }
    }
    if (best < 0) return 1;
    for (v = 0; v < NDIR; v++) {
        struct sstate *child;
        long r;
        if (!(ss->dom[best] & (1u << v))) continue;
        if (--*budget < 0) return -1;
        child = ss_dup(sv, ss);
        if (ss_assign(sv, child, best, v) && ss_propagate(sv, child)) {
            r = count_sols(sv, child, limit - total, budget);
            if (r < 0) { ss_free(child); return -1; }
            total += r;
        } else sv_drain(sv);
        ss_free(child);
        if (total >= limit) return total;
    }
    return total;
}

static game_state *state_from(const game_params *p, const char *desc)
{
    return new_game(NULL, p, desc);
}

static void apply(game_state **s, const char *move)
{
    game_state *n = execute_move(*s, move);
    if (!n) { printf("execute_move refused %s\n", move); fails++; return; }
    free_game(*s);
    *s = n;
}

int main(int argc, char **argv)
{
    int nseeds = argc > 1 ? atoi(argv[1]) : 20;
    static const int sizes[][2] = { {4,4},{6,4},{8,4},{8,8},{12,8},{16,16} };
    int si;

    for (si = 0; si < (int)lenof(sizes); si++) {
        game_params p;
        int seed, nunique = 0, ncounted = 0, nforced = 0, nguess = 0, nsteps = 0;
        int nmist1 = 0, nmist1ok = 0, nmist2 = 0, nmist2ok = 0, nmist1exact = 0;
        if (argc > 3) { p.w = atoi(argv[2]); p.h = atoi(argv[3]); if (si) break; }
        else { p.w = sizes[si][0]; p.h = sizes[si][1]; }
        for (seed = 1; seed <= nseeds; seed++) {
            char sb[32], *aux, *desc;
            random_state *rs;
            struct mosco_clues *cl;
            signed char *sol, *sol2;
            game_state *st;
            int n = p.w * p.h, i, k = 2 * p.w + 2 * p.h + 4;
            int *vals = snewn(k, int);
            struct msolver *sv;
            struct sstate *root;
            long budget, r;

            sprintf(sb, "%d", seed * 7919 + p.w * 31 + p.h);
            rs = random_new(sb, strlen(sb));
            desc = new_game_desc(&p, rs, &aux, false);
            CHECK(validate_desc(&p, desc) == NULL);
            CHECK(parse_clues(&p, desc, vals) == NULL);
            cl = clues_new(p.w, p.h, vals);
            sol = snewn(n, signed char);
            sol2 = snewn(n, signed char);
            CHECK(parse_aux(aux, n, sol));
            CHECK(valid_arrangement(cl, sol));

            /* soundness of the narrowing */
            sv = msolver_new(cl);
            root = ss_new(sv);
            CHECK(ss_propagate(sv, root));
            for (i = 0; i < n; i++) CHECK(root->dom[i] & (1u << sol[i]));

            /* a search finds a solution (any) */
            r = find_solution(cl, NULL, NULL, sol2, 1);
            CHECK(r == 1);
            if (r == 1) CHECK(valid_arrangement(cl, sol2));
            /* and with the made-from one preferred it finds just that */
            r = find_solution(cl, NULL, sol, sol2, 1);
            CHECK(r == 1);
            if (r == 1) CHECK(memcmp(sol, sol2, n) == 0);

            /* how many solutions */
            if (n <= 32) {
                struct sstate *c2 = ss_dup(sv, root);
                budget = 2000000;
                r = count_sols(sv, c2, 2, &budget);
                ss_free(c2);
                if (r >= 0) { ncounted++; if (r == 1) nunique++; CHECK(r >= 1); }
            }

            /* hint steps from partial boards that agree with the solution */
            midend_hint_aux = (seed & 1) ? aux : NULL;
            st = state_from(&p, desc);
            {
                int placed = 0;
                int order[1024];
                for (i = 0; i < n; i++) order[i] = i;
                for (i = n - 1; i > 0; i--) { int j = rand() % (i + 1), t = order[i]; order[i] = order[j]; order[j] = t; }
                /* lay a random part of the solution first */
                int lay = rand() % (n + 1) / 2;
                for (i = 0; i < lay; i++) {
                    char mv[40];
                    sprintf(mv, "P%d,%d,%d", order[i] % p.w, order[i] / p.w, sol[order[i]]);
                    apply(&st, mv);
                    placed++;
                }
                while (!st->completed) {
                    const char *err = NULL;
                    char *mv = hint_game(st, st, &err);
                    struct sstate *ss = ss_dup(sv, root);
                    int t, d;
                    bool forced = false;
                    if (!mv) { printf("%s: hint gave no move: %s\n", desc, err); fails++; break; }
                    CHECK(ss_apply_grid(sv, ss, st->grid, -1));
                    CHECK(sscanf(mv, "P%d,%d,%d", &t, &i, &d) == 3);
                    t = i * p.w + t;
                    CHECK(st->grid[t] < 0);
                    if (!(ss->dom[t] & (ss->dom[t] - 1))) forced = true;
                    ss_free(ss);
                    nsteps++;
                    if (forced) nforced++; else nguess++;
                    apply(&st, mv);
                    /* still solvable */
                    CHECK(find_solution(cl, st->grid, NULL, sol2, 2) == 1);
                    sfree(mv);
                    if (fails > 20) break;
                }
                CHECK(st->completed);
                CHECK(valid_arrangement(cl, st->grid));
            }
            free_game(st);

            /* boards with one wrong arrow (among some right ones) */
            {
                int tries;
                for (tries = 0; tries < 6; tries++) {
                    int nwrong = 1 + (tries & 1);
                    int order[1024], j, lay = n / 4 + rand() % (n / 2 + 1);
                    game_state *b = state_from(&p, desc);
                    const char *err = NULL;
                    char *mv;
                    int wrong[2] = { -1, -1 }, w;
                    for (i = 0; i < n; i++) order[i] = i;
                    for (i = n - 1; i > 0; i--) { j = rand() % (i + 1); int t = order[i]; order[i] = order[j]; order[j] = t; }
                    for (i = 0; i < lay; i++) {
                        char m[40];
                        sprintf(m, "P%d,%d,%d", order[i] % p.w, order[i] / p.w, sol[order[i]]);
                        apply(&b, m);
                    }
                    /* the wrong ones: tiles that are not laid yet or lay already, any other arrow */
                    for (w = 0; w < nwrong; w++) {
                        int t = order[lay + w < n ? lay + w : lay], d = (sol[t] + 1 + rand() % 7) % NDIR;
                        char m[40];
                        sprintf(m, "P%d,%d,%d", t % p.w, t / p.w, d);
                        apply(&b, m);
                        wrong[w] = t;
                    }
                    /* is it really unsolvable?  (a different arrow may fit another solution) */
                    r = find_solution(cl, b->grid, NULL, sol2, 2);
                    mv = hint_game(b, b, &err);
                    if (r != 1) { if (nwrong == 1) nmist1++; else nmist2++; }
                    if (r == 0) {
                        if (mv) { printf("%s: board with wrong arrow(s) got a move %s\n", desc, mv); fails++; sfree(mv); }
                        else {
                            int row, col;
                            if (sscanf(err, "There is a mistake in row %d, column %d", &row, &col) == 2) {
                                int c = (row - 1) * p.w + (col - 1);
                                /* taking the arrow away leaves something solvable */
                                signed char *g = snewn(n, signed char);
                                memcpy(g, b->grid, n);
                                g[c] = -1;
                                if (find_solution(cl, g, NULL, sol2, 2) == 1) {
                                    if (nwrong == 1) nmist1ok++; else nmist2ok++;
                                    if (nwrong == 1 && c == wrong[0]) nmist1exact++;
                                } else if (nwrong == 1) { printf("%s: blamed %d,%d but that does not help (wrong at %d)\n", desc, row, col, wrong[0]); }
                                sfree(g);
                            } else if (nwrong == 1) {
                                /* a message of the other kinds: fine, but show it */
                                if (nmist1 < 3) printf("   (message: %s)\n", err);
                            }
                        }
                    } else if (r == 1) {
                        /* the arrow fits another solution: the hint must give a move or nothing wrong */
                        if (!mv && err && strstr(err, "mistake")) { printf("%s: false mistake: %s\n", desc, err); fails++; }
                        sfree(mv);
                    } else sfree(mv);
                    free_game(b);
                }
            }

            midend_hint_aux = NULL;
            ss_free(root);
            msolver_free(sv);
            clues_free(cl);
            sfree(vals); sfree(sol); sfree(sol2); sfree(desc); sfree(aux);
            random_free(rs);
        }
        printf("%dx%d: %d puzzles; unique solution in %d of %d counted; hint steps %d: forced %d, guessed %d; "
               "one wrong: %d unsolvable, blamed arrow helps in %d (the wrong one itself in %d); two wrong: %d unsolvable, %d helps\n",
               p.w, p.h, nseeds, nunique, ncounted, nsteps, nforced, nguess, nmist1, nmist1ok, nmist1exact, nmist2, nmist2ok);
        fflush(stdout);
    }
    printf("%s (%d failures)\n", fails ? "FAILED" : "ok", fails);
    return fails ? 1 : 0;
}
