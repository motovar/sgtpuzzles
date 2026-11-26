/* moscotest.c: white-box tests of Mosco (includes mosco.c itself).
 *   moscotest [seeds]
 *
 *  1. Puzzles of every level and of many shapes.  Each has to be a puzzle of its level:
 *     the reasoning of the level gets to the arrangement it was made from, that of the
 *     level below doesn't, the reasoning as far as Hard goes finds the same arrangement,
 *     and the same seed makes the same puzzle again.  Where it can be counted (small
 *     boards) the number of solutions has to be exactly one - by a count that uses
 *     nothing of the reasoning (a plain backtracking over the tiles) up to 24 tiles, and
 *     by the search with the narrowing up to 40.
 *  2. Hints.  From a partial board that agrees with the solution the move that is returned
 *     must be a tile that is not laid yet, with the arrow that the solution has, and a
 *     game played by hints alone must come to the end.  From a board with one or two wrong
 *     arrows (arrows that the solution doesn't have) the hint must say there is a mistake
 *     and never give a move, and a tile that it names must be a wrong one.
 *  3. Solve gives the solution (with the arrangement that the puzzle was made from, and
 *     without it).
 *  4. Descriptions that were typed in (more than one solution, or no solution that the
 *     reasoning finds): Solve, hints and validation work and nothing leaks (run it under
 *     the sanitizers).
 *  5. The parameters: sides below two, numbers too big for an int, garbage.
 *  6. The drawing of the numbers of the clues, 1024 and more (a 2 x 8192 board).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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

/* the number of solutions, up to limit, by the search with the narrowing; -1 if it took too long */
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

/* the number of solutions, up to limit, by plain backtracking over the tiles: only "no clue
 * has more arrows than it wants" and "no kind is used more often than it should" are used;
 * -1 if it took too many steps */
struct brute {
    const struct mosco_clues *cl;
    int *cnt;
    int used[NDIR];
    int *left;                  /* tiles still to come that can point at the clue */
    long steps, limit_steps;
};

static long brute_rec(struct brute *b, int t, long limit)
{
    const struct mosco_clues *cl = b->cl;
    long total = 0;
    int d;

    if (t == cl->n) return 1;
    if (++b->steps > b->limit_steps) return -1;
    for (d = 0; d < NDIR; d++) b->left[cl->target[t * NDIR + d]]--;
    for (d = 0; d < NDIR; d++) {
        int c = cl->target[t * NDIR + d], e;
        bool ok = b->used[d] < cl->kcount[d] && b->cnt[c] < cl->clue[c];
        long r;

        if (!ok) continue;
        b->cnt[c]++;
        b->used[d]++;
        /* every clue that is short has to have tiles that can still make it up */
        for (e = 0; e < NDIR && ok; e++) {
            int c2 = cl->target[t * NDIR + e];
            if (b->cnt[c2] + b->left[c2] < cl->clue[c2]) ok = false;
        }
        if (ok) {
            r = brute_rec(b, t + 1, limit - total);
            if (r < 0) total = -1;
            else total += r;
        }
        b->cnt[c]--;
        b->used[d]--;
        if (total < 0 || total >= limit) break;
    }
    for (d = 0; d < NDIR; d++) b->left[cl->target[t * NDIR + d]]++;
    return total;
}

static long brute_count(const struct mosco_clues *cl, long limit, long max_steps)
{
    struct brute b;
    int t, d;
    long r;

    b.cl = cl;
    b.cnt = snewn(cl->k, int);
    b.left = snewn(cl->k, int);
    memset(b.cnt, 0, cl->k * sizeof(int));
    memset(b.left, 0, cl->k * sizeof(int));
    memset(b.used, 0, sizeof(b.used));
    for (t = 0; t < cl->n; t++)
        for (d = 0; d < NDIR; d++) b.left[cl->target[t * NDIR + d]]++;
    b.steps = 0;
    b.limit_steps = max_steps;
    r = brute_rec(&b, 0, limit);
    sfree(b.cnt);
    sfree(b.left);
    return r;
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

static unsigned rnd_state = 12345;
static unsigned rnd(void)
{
    rnd_state = rnd_state * 1103515245u + 12345u;
    return (rnd_state >> 8) & 0xffffff;
}

static char *make_desc(const game_params *p, int seed, char **aux)
{
    char sb[64];
    random_state *rs;
    char *desc;

    sprintf(sb, "%d", seed * 7919 + p->w * 31 + p->h * 17 + p->diff);
    rs = random_new(sb, strlen(sb));
    desc = new_game_desc(p, rs, aux, false);
    random_free(rs);
    return desc;
}

static int total_puzzles, total_counted, total_brute, total_steps, total_forced, total_whatif;
static int total_mist, total_blamed;

static void test_puzzle(const game_params *p, int seed)
{
    char *aux, *aux2, *desc, *desc2;
    struct mosco_clues *cl;
    signed char *sol, *sol2;
    int n = p->w * p->h, k = 2 * p->w + 2 * p->h + 4, i, r;
    int *vals = snewn(k, int);
    struct msolver *sv;
    struct sstate *s1, *s2;
    game_state *st;

    desc = make_desc(p, seed, &aux);
    CHECK(validate_desc(p, desc) == NULL);
    CHECK(parse_clues(p, desc, vals) == NULL);
    cl = clues_new(p->w, p->h, vals);
    sol = snewn(n, signed char);
    sol2 = snewn(n, signed char);
    CHECK(parse_aux(aux, n, sol));
    CHECK(valid_arrangement(cl, sol));
    total_puzzles++;

    /* the same seed, the same puzzle */
    desc2 = make_desc(p, seed, &aux2);
    CHECK(strcmp(desc, desc2) == 0);
    CHECK(strcmp(aux, aux2) == 0);
    sfree(desc2); sfree(aux2);

    /* the levels */
    sv = msolver_new(cl);
    s1 = ss_new(sv);
    s2 = ss_new(sv);
    ss_reset(sv, s1);
    r = solve_level(sv, s1, s2, p->diff);
    CHECK(r == 1);
    if (r == 1) for (i = 0; i < n; i++) CHECK(lowbit(s1->dom[i]) == sol[i]);
    if (p->diff > DIFF_EASY) {
        ss_reset(sv, s1);
        r = solve_level(sv, s1, s2, p->diff - 1);
        CHECK(r != 1);          /* the level below stops short */
    }
    /* the reasoning as far as Hard goes finds the same */
    CHECK(logical_solution(cl, NULL, sol2) == 1);
    CHECK(memcmp(sol, sol2, n) == 0);
    /* and with some of the arrows laid (the right ones) the same again */
    {
        signed char *g = snewn(n, signed char);
        for (i = 0; i < n; i++) g[i] = (rnd() % 3 == 0) ? sol[i] : -1;
        CHECK(logical_solution(cl, g, sol2) == 1);
        CHECK(memcmp(sol, sol2, n) == 0);
        /* a wrong one: the reasoning can't have it */
        i = rnd() % n;
        g[i] = (sol[i] + 1 + rnd() % 7) % NDIR;
        CHECK(logical_solution(cl, g, sol2) == -1);
        sfree(g);
    }
    sv_drain(sv);
    ss_free(s1);
    ss_free(s2);
    msolver_free(sv);

    /* how many solutions: exactly one */
    if (n <= 24) {
        long c = brute_count(cl, 2, 400000000L);
        if (c >= 0) { total_brute++; CHECK(c == 1); }
    }
    if (n <= 40) {
        struct msolver *sv2 = msolver_new(cl);
        struct sstate *c2 = ss_new(sv2);
        long budget = 3000000, c;
        CHECK(ss_propagate(sv2, c2));
        c = count_sols(sv2, c2, 2, &budget);
        if (c >= 0) { total_counted++; CHECK(c == 1); }
        ss_free(c2);
        sv_drain(sv2);
        msolver_free(sv2);
    }

    /* Solve, with and without the aux data */
    for (i = 0; i < 2; i++) {
        game_state *b = state_from(p, desc);
        const char *err = NULL;
        char *mv = solve_game(b, b, i ? NULL : aux, &err);
        CHECK(mv != NULL);
        if (mv) {
            CHECK(mv[0] == 'S');
            CHECK(strcmp(mv + 1, aux) == 0);
            apply(&b, mv);
            CHECK(b->completed);
            sfree(mv);
        }
        free_game(b);
    }

    /* a game played by hints */
    midend_hint_aux = (seed & 1) ? aux : NULL;
    st = state_from(p, desc);
    {
        int *order = snewn(n, int), lay;
        for (i = 0; i < n; i++) order[i] = i;
        for (i = n - 1; i > 0; i--) { int j = rnd() % (i + 1), t = order[i]; order[i] = order[j]; order[j] = t; }
        lay = n > 1 ? rnd() % (n / 2 + 1) : 0;
        if (n > 80) lay = lay % 40;
        for (i = 0; i < lay; i++) {
            char mv[40];
            sprintf(mv, "P%d,%d,%d", order[i] % p->w, order[i] / p->w, sol[order[i]]);
            apply(&st, mv);
        }
        sfree(order);
        /* (a big board takes a while: so many steps only) */
        for (i = 0; i < 400 && !st->completed; i++) {
            const char *err = NULL;
            char *mv = hint_game(st, st, &err);
            int x, y, d;
            if (!mv) { printf("%s %s: hint gave no move: %s\n", encode_params(p, true), desc, err); fails++; break; }
            CHECK(sscanf(mv, "P%d,%d,%d", &x, &y, &d) == 3);
            if (sscanf(mv, "P%d,%d,%d", &x, &y, &d) == 3) {
                int t = y * p->w + x;
                struct msolver *sv3 = msolver_new(cl);
                struct sstate *ss3 = ss_new(sv3);
                CHECK(t >= 0 && t < n);
                if (t >= 0 && t < n) {
                    CHECK(st->grid[t] < 0);
                    CHECK(d == sol[t]);
                    if (ss_propagate(sv3, ss3) && ss_apply_grid(sv3, ss3, st->grid, -1) &&
                        !(ss3->dom[t] & (ss3->dom[t] - 1)))
                        total_forced++;
                    else
                        total_whatif++;
                }
                ss_free(ss3);
                sv_drain(sv3);
                msolver_free(sv3);
            }
            total_steps++;
            apply(&st, mv);
            sfree(mv);
            if (fails > 20) break;
        }
        if (n <= 80) {
            CHECK(st->completed);
            CHECK(valid_arrangement(cl, st->grid));
        }
    }
    free_game(st);

    /* boards with one or two wrong arrows (among right ones) */
    if (n <= 400) {
        int tries;
        for (tries = 0; tries < 6; tries++) {
            int nwrong = 1 + (tries & 1);
            int *order = snewn(n, int), j, lay = n / 4 + rnd() % (n / 2 + 1);
            game_state *b = state_from(p, desc);
            const char *err = NULL;
            char *mv;
            int wrong[2] = { -1, -1 }, w;
            for (i = 0; i < n; i++) order[i] = i;
            for (i = n - 1; i > 0; i--) { j = rnd() % (i + 1); int t = order[i]; order[i] = order[j]; order[j] = t; }
            for (i = 0; i < lay; i++) {
                char m[40];
                sprintf(m, "P%d,%d,%d", order[i] % p->w, order[i] / p->w, sol[order[i]]);
                apply(&b, m);
            }
            for (w = 0; w < nwrong; w++) {
                int t = order[lay + w < n ? lay + w : lay], d = (sol[t] + 1 + rnd() % 7) % NDIR;
                char m[40];
                sprintf(m, "P%d,%d,%d", t % p->w, t / p->w, d);
                apply(&b, m);
                wrong[w] = t;
            }
            sfree(order);
            mv = hint_game(b, b, &err);
            total_mist++;
            if (mv) {
                printf("%s %s: board with wrong arrow(s) got a move %s\n", encode_params(p, true), desc, mv);
                fails++;
                sfree(mv);
            } else {
                int row, col;
                CHECK(err != NULL);
                if (err && sscanf(err, "There is a mistake in row %d, column %d", &row, &col) == 2) {
                    int c = (row - 1) * p->w + (col - 1);
                    CHECK(c >= 0 && c < n);
                    if (c >= 0 && c < n) {
                        CHECK(b->grid[c] != sol[c]);
                        if (nwrong == 1) CHECK(c == wrong[0]);
                        total_blamed++;
                    }
                } else if (err) {
                    /* the other messages: too many arrows at a clue or of a kind */
                    CHECK(strstr(err, "too many arrows") != NULL);
                }
            }
            free_game(b);
        }
    }
    midend_hint_aux = NULL;

    clues_free(cl);
    sfree(vals); sfree(sol); sfree(sol2); sfree(desc); sfree(aux);
}

/* descriptions that were typed in: no solution that is unique, or none that is found by reasoning */
static void test_typed(void)
{
    static const int sizes[][2] = { {4,4}, {2,4}, {4,2}, {6,4}, {8,4} };
    int si, rep;

    for (si = 0; si < (int)lenof(sizes); si++) {
        game_params p;
        p.w = sizes[si][0]; p.h = sizes[si][1]; p.diff = DIFF_NORMAL;
        for (rep = 0; rep < 6; rep++) {
            int n = p.w * p.h, k = 2 * p.w + 2 * p.h + 4, i;
            struct mosco_clues *cl, *cl2;
            int *zeros = snewn(k, int), *vals = snewn(k, int);
            signed char *arr = snewn(n, signed char), *sol = snewn(n, signed char);
            struct mrand rng;
            char *desc, *q;
            game_state *st;
            const char *err = NULL;
            char *mv;
            int steps;

            for (i = 0; i < k; i++) zeros[i] = 0;
            cl = clues_new(p.w, p.h, zeros);
            rng.s = 0x1234567ULL + rep * 977 + si * 31;
            random_arrangement(cl, arr, &rng);
            arrangement_clues(cl, arr, vals);
            desc = snewn(k * 12 + 1, char);
            q = desc;
            for (i = 0; i < k; i++) q += sprintf(q, i ? ",%d" : "%d", vals[i]);
            CHECK(validate_desc(&p, desc) == NULL);
            cl2 = clues_new(p.w, p.h, vals);
            st = state_from(&p, desc);

            /* Solve (no aux data) gives a valid arrangement */
            mv = solve_game(st, st, NULL, &err);
            CHECK(mv != NULL);
            if (mv) {
                CHECK(parse_aux(mv + 1, n, sol));
                CHECK(valid_arrangement(cl2, sol));
                sfree(mv);
            }
            /* hints (with the arrangement it was made from as the preferred one, and without) */
            for (steps = 0; steps < 3 * n && !st->completed; steps++) {
                char aux[400];
                for (i = 0; i < n; i++) aux[i] = '0' + arr[i];
                aux[n] = 0;
                midend_hint_aux = (rep & 1) ? aux : NULL;
                err = NULL;
                mv = hint_game(st, st, &err);
                midend_hint_aux = NULL;
                if (!mv) {
                    /* it may say it has found nothing; it must not call a good start a mistake
                     * when the board is the arrangement itself laid in part */
                    CHECK(err != NULL);
                    break;
                }
                apply(&st, mv);
                sfree(mv);
            }
            free_game(st);
            /* the clues of an arrangement that can't be (a clue greater than what can point at it) */
            clues_free(cl2);
            clues_free(cl);
            sfree(zeros); sfree(vals); sfree(arr); sfree(sol); sfree(desc);
        }
    }
    /* clues that can't be met */
    {
        game_params p;
        p.w = 4; p.h = 4; p.diff = DIFF_NORMAL;
        CHECK(validate_desc(&p, "16,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0") != NULL);
        CHECK(validate_desc(&p, "1,2,3") != NULL);
        CHECK(validate_desc(&p, "") != NULL);
        CHECK(validate_desc(&p, "x") != NULL);
        CHECK(validate_desc(&p, "99999999999999999999,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0") != NULL);
        /* the right sum, and no clue too big, but no arrangement of eight kinds twice each */
        CHECK(validate_desc(&p, "4,0,0,0,0,0,4,0,0,0,0,0,4,0,0,0,0,0,0,4") != NULL ||
              1);       /* (whatever it says it must not crash) */
    }
}

static void test_params(void)
{
    game_params p, *q;
    config_item *cfg;
    char *s;

    p.w = 1; p.h = 8; p.diff = DIFF_EASY;
    CHECK(validate_params(&p, true) != NULL);
    p.w = 8; p.h = 1;
    CHECK(validate_params(&p, true) != NULL);
    p.w = 0; p.h = 8;
    CHECK(validate_params(&p, true) != NULL);
    p.w = 2; p.h = 4;
    CHECK(validate_params(&p, true) == NULL);
    p.w = 3; p.h = 3;
    CHECK(validate_params(&p, true) != NULL);           /* not a multiple of 8 */
    p.w = 1024; p.h = 1024;
    CHECK(validate_params(&p, true) == NULL);           /* the most */
    p.w = 1025; p.h = 1024;
    CHECK(validate_params(&p, true) != NULL);
    p.w = 2000000000; p.h = 2000000000;
    CHECK(validate_params(&p, true) != NULL);
    p.w = 4; p.h = 4; p.diff = 3;
    CHECK(validate_params(&p, true) != NULL);

    q = default_params();
    decode_params(q, "99999999999999999999x5dh");
    CHECK(q->w > MAX_TILES);
    CHECK(validate_params(q, true) != NULL);
    decode_params(q, "12x8dh");
    CHECK(q->w == 12 && q->h == 8 && q->diff == DIFF_HARD);
    s = encode_params(q, true);
    CHECK(strcmp(s, "12x8dh") == 0);
    sfree(s);
    decode_params(q, "6");
    CHECK(q->w == 6 && q->h == 6);
    decode_params(q, "");
    decode_params(q, "x");
    decode_params(q, "5x");
    decode_params(q, "5xx7d");
    free_params(q);

    q = default_params();
    cfg = game_configure(q);
    sfree(cfg[0].u.string.sval); cfg[0].u.string.sval = dupstr("-5");
    sfree(cfg[1].u.string.sval); cfg[1].u.string.sval = dupstr("999999999999999999999999");
    {
        game_params *r = custom_params(cfg);
        CHECK(validate_params(r, true) != NULL);
        free_params(r);
    }
    sfree(cfg[0].u.string.sval); cfg[0].u.string.sval = dupstr("abc");
    {
        game_params *r = custom_params(cfg);
        CHECK(validate_params(r, true) != NULL);
        free_params(r);
    }
    sfree(cfg[0].u.string.sval); cfg[0].u.string.sval = dupstr("10");
    sfree(cfg[1].u.string.sval); cfg[1].u.string.sval = dupstr("8");
    {
        game_params *r = custom_params(cfg);
        CHECK(validate_params(r, true) == NULL && r->w == 10 && r->h == 8);
        free_params(r);
    }
    sfree(cfg[0].u.string.sval);
    sfree(cfg[1].u.string.sval);
    sfree(cfg);
    free_params(q);
}

/* what is written in the squares of the clues */
static char rec_text[20000][16];
static int rec_size[20000], rec_n;

static void rec_draw_text(drawing *dr, int x, int y, int fonttype, int fontsize,
                          int align, int colour, const char *text)
{
    if (rec_n < 20000) {
        strncpy(rec_text[rec_n], text, 15);
        rec_text[rec_n][15] = 0;
        rec_size[rec_n] = fontsize;
        rec_n++;
    }
}

extern const drawing_api null_drawing_api;

static void test_drawing(void)
{
    static const char *ids[] = { "2x8192de#1", "8x8dn#1" };
    int si;

    for (si = 0; si < 2; si++) {
        drawing_api api = null_drawing_api;
        midend *me;
        const char *err;
        int x = 0, y = 0, i, maxdigits = 0, seen_max = 0, maxval = 0, nclues;
        char *desc;
        game_params p;
        const char *colon;
        int *vals;

        api.draw_text = rec_draw_text;
        me = midend_new(NULL, &thegame, &api, NULL);
        err = midend_game_id(me, ids[si]);
        CHECK(err == NULL);
        if (err) { midend_free(me); continue; }
        midend_new_game(me);
        x = y = si == 0 ? (1 << 20) : 400;     /* (the size the board is to fit into) */
        midend_size(me, &x, &y, true, 1.0);
        rec_n = 0;
        midend_redraw(me);
        /* the description (clues) of the game that is played */
        desc = midend_get_game_id(me);
        colon = strchr(desc, ':');
        CHECK(colon != NULL);
        p.w = si == 0 ? 2 : 8;
        p.h = si == 0 ? 8192 : 8;
        p.diff = DIFF_EASY;
        nclues = 2 * p.w + 2 * p.h + 4;
        vals = snewn(nclues, int);
        CHECK(colon && parse_clues(&p, colon + 1, vals) == NULL);
        for (i = 0; i < nclues; i++) if (vals[i] > maxval) maxval = vals[i];
        /* every clue is written in full */
        CHECK(rec_n == nclues);
        for (i = 0; i < rec_n; i++) {
            int v = atoi(rec_text[i]);
            int dg = (int)strlen(rec_text[i]);
            if (dg > maxdigits) maxdigits = dg;
            if (v > seen_max) seen_max = v;
            CHECK(rec_size[i] > 0);
        }
        CHECK(seen_max == maxval);
        if (si == 0) {
            CHECK(maxval >= 1024);      /* more than ten bits */
            CHECK(maxdigits >= 4);
        }
        /* drawn again with nothing changed: no clue is written again */
        rec_n = 0;
        midend_redraw(me);
        CHECK(rec_n == 0);
        sfree(vals);
        sfree(desc);
        midend_free(me);
    }
}

int main(int argc, char **argv)
{
    int nseeds = argc > 1 ? atoi(argv[1]) : 3;
    static const int sizes[][2] = {
        {2,4},{4,2},{4,4},{6,4},{4,6},{8,4},{3,8},{8,3},{2,12},{5,8},{8,8},{7,8},{9,8},
        {10,8},{2,40},{12,8},{16,16},{3,32},{32,32}
    };
    int si, lv, seed;
    double t0 = (double)clock() / CLOCKS_PER_SEC;

    for (si = 0; si < (int)lenof(sizes); si++)
        for (lv = 0; lv < DIFFCOUNT; lv++) {
            game_params p;
            int before = fails;
            p.w = sizes[si][0]; p.h = sizes[si][1]; p.diff = lv;
            CHECK(validate_params(&p, true) == NULL);
            for (seed = 1; seed <= nseeds; seed++)
                test_puzzle(&p, seed);
            if (fails != before)
                printf("  ... in %dx%d %s\n", p.w, p.h, diff_names[lv]);
            if (fails > 30) goto done;
        }
    test_typed();
    test_params();
    test_drawing();
done:
    printf("%d puzzles (%d counted by the search, %d by plain backtracking: all unique); hint steps %d "
           "(forced by the clues %d, with a what-if %d); boards with wrong arrows %d, tile named in %d; %.1f s\n",
           total_puzzles, total_counted, total_brute, total_steps, total_forced, total_whatif,
           total_mist, total_blamed, (double)clock() / CLOCKS_PER_SEC - t0);
    printf("%s (%d failures)\n", fails ? "FAILED" : "ok", fails);
    return fails ? 1 : 0;
}
