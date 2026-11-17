/* White-box tests of Decanting: it includes the game.  Build with tests/empty.c as the game source. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "decanting.c"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* ---- exhaustive search for the truth ---- */

struct bfs {
    int nt, nl, sb;
    unsigned char *keys;       /* sorted tubes, nt*nl bytes each */
    int *dist, *queue;
    int n, cap;
    int *table;                /* index+1 */
    int tcap;
};

static int tube_cmp_ctx_nl;
static int tube_cmp(const void *a, const void *b)
{
    return memcmp(a, b, tube_cmp_ctx_nl);
}

static void canon(const signed char t[][MAX_LAYERS], int nt, int nl, unsigned char *out)
{
    int i, j;
    for (i = 0; i < nt; i++)
        for (j = 0; j < nl; j++)
            out[i * nl + j] = (unsigned char)(t[i][j] + 1);
    tube_cmp_ctx_nl = nl;
    qsort(out, nt, nl, tube_cmp);
}

static uint64_t fnv(const unsigned char *p, int n)
{
    uint64_t h = 14695981039346656037ULL;
    while (n--) h = (h ^ *p++) * 1099511628211ULL;
    return h;
}

static int bfs_find(struct bfs *b, const unsigned char *key, bool insert, int dist)
{
    size_t i = (size_t)(fnv(key, b->sb) & (uint64_t)(b->tcap - 1));
    while (b->table[i]) {
        int id = b->table[i] - 1;
        if (memcmp(b->keys + (size_t)id * b->sb, key, b->sb) == 0) return id;
        i = (i + 1) & (b->tcap - 1);
    }
    if (!insert) return -1;
    if (b->n == b->cap) {
        b->cap *= 2;
        b->keys = realloc(b->keys, (size_t)b->cap * b->sb);
        b->dist = realloc(b->dist, b->cap * sizeof(int));
        b->queue = realloc(b->queue, b->cap * sizeof(int));
    }
    memcpy(b->keys + (size_t)b->n * b->sb, key, b->sb);
    b->dist[b->n] = dist;
    b->queue[b->n] = b->n;
    b->table[i] = b->n + 1;
    return b->n++;
}

/* the length of a shortest solution, -1 if there is none, -2 if the search got too big */
static int bfs_solve(const signed char start[][MAX_LAYERS], int nt, int nl, int maxstates)
{
    struct bfs b;
    unsigned char key[MAX_TUBES * MAX_LAYERS];
    signed char cur[MAX_TUBES][MAX_LAYERS], nb[MAX_TUBES][MAX_LAYERS];
    int head = 0, result = -1, i, j;

    memset(&b, 0, sizeof(b));
    b.nt = nt; b.nl = nl; b.sb = nt * nl;
    b.cap = 1024;
    b.keys = malloc((size_t)b.cap * b.sb);
    b.dist = malloc(b.cap * sizeof(int));
    b.queue = malloc(b.cap * sizeof(int));
    b.tcap = 1 << 22;
    b.table = calloc(b.tcap, sizeof(int));

    canon(start, nt, nl, key);
    bfs_find(&b, key, true, 0);
    while (head < b.n) {
        int id = b.queue[head++];
        memset(cur, -1, sizeof(cur));
        for (i = 0; i < nt; i++)
            for (j = 0; j < nl; j++)
                cur[i][j] = (signed char)((int)b.keys[(size_t)id * b.sb + i * nl + j] - 1);
        if (tubes_solved(cur, nt, nl)) { result = b.dist[id]; break; }
        if (b.n > maxstates) { result = -2; break; }
        for (i = 0; i < nt; i++)
            for (j = 0; j < nt; j++) {
                int k = pour_amount(cur, nl, i, j);
                if (!k) continue;
                memcpy(nb, cur, sizeof(nb));
                pour_layers(nb, nl, i, j, k);
                canon(nb, nt, nl, key);
                if (bfs_find(&b, key, false, 0) < 0) {
                    bfs_find(&b, key, true, b.dist[id] + 1);
                    if (b.tcap < 2 * b.n) { printf("table too small\n"); exit(2); }
                }
            }
    }
    free(b.keys); free(b.dist); free(b.queue); free(b.table);
    return result;
}

static void deal(signed char t[][MAX_LAYERS], int nc, int nt, int nl, random_state *rs)
{
    int units[MAX_COLOURS * MAX_LAYERS], i, j;
    for (i = 0; i < nc; i++)
        for (j = 0; j < nl; j++)
            units[i * nl + j] = i;
    shuffle(units, nc * nl, sizeof(int), rs);
    memset(t, -1, (size_t)MAX_TUBES * MAX_LAYERS);
    for (i = 0; i < nc; i++)
        for (j = 0; j < nl; j++)
            t[i][j] = (signed char)units[i * nl + j];
}

/* replays a solution and says whether it is legal and ends in a finished game */
static bool replay(const signed char start[][MAX_LAYERS], int nt, int nl, const struct pour *sol, int n)
{
    signed char t[MAX_TUBES][MAX_LAYERS];
    int i;
    memcpy(t, start, sizeof(t));
    for (i = 0; i < n; i++) {
        int k = pour_amount(t, nl, sol[i].from, sol[i].to);
        if (k != sol[i].n || k == 0) return false;
        pour_layers(t, nl, sol[i].from, sol[i].to, k);
    }
    return tubes_solved(t, nt, nl);
}

static void test_solver_vs_bfs(int nc, int nt, int nl, int trials, random_state *rs)
{
    int i, agree = 0, solvable = 0, unsolvable = 0, bad = 0;
    long sumopt = 0, sumsol = 0;
    int worst_extra = 0;
    for (i = 0; i < trials; i++) {
        signed char t[MAX_TUBES][MAX_LAYERS];
        struct pour *sol = NULL;
        int truth, r;
        deal(t, nc, nt, nl, rs);
        truth = bfs_solve(t, nt, nl, 3000000);
        if (truth == -2) continue;
        r = solve_search(t, nt, nl, 5000000, NULL, 0, &sol);
        if (truth >= 0) {
            solvable++;
            if (r < 0) { bad++; printf("  solver misses a solution (%d %d %d trial %d): truth %d r %d\n", nc, nt, nl, i, truth, r); }
            else if (!replay(t, nt, nl, sol, r)) { bad++; printf("  solver's solution is not legal\n"); }
            else { agree++; sumopt += truth; sumsol += r; if (r - truth > worst_extra) worst_extra = r - truth; }
        } else {
            unsolvable++;
            if (r != -1) { bad++; printf("  solver says %d for an unsolvable game\n", r); }
            else agree++;
        }
        sfree(sol);
    }
    printf("  %d colours %d tubes %d layers: %d trials, %d solvable, %d unsolvable, %d wrong; avg optimal %.1f, solver %.1f, worst +%d\n",
           nc, nt, nl, trials, solvable, unsolvable, bad,
           solvable ? (double)sumopt / solvable : 0.0, solvable ? (double)sumsol / solvable : 0.0, worst_extra);
    CHECK(bad == 0);
}

/* positions that arise during play (after some random pours), not only dealt ones */
static void test_solver_midgame(int nc, int nt, int nl, int trials, random_state *rs)
{
    int i, bad = 0, unsolvable = 0, tot = 0;
    for (i = 0; i < trials; i++) {
        signed char t[MAX_TUBES][MAX_LAYERS];
        int steps = random_upto(rs, 12), s;
        struct pour *sol = NULL;
        int truth, r;
        deal(t, nc, nt, nl, rs);
        for (s = 0; s < steps; s++) {
            int a = random_upto(rs, nt), b = random_upto(rs, nt), k = pour_amount(t, nl, a, b);
            if (k) pour_layers(t, nl, a, b, k);
        }
        truth = bfs_solve(t, nt, nl, 3000000);
        if (truth == -2) continue;
        tot++;
        r = solve_search(t, nt, nl, 5000000, NULL, 0, &sol);
        if (truth >= 0) {
            if (r < 0 || !replay(t, nt, nl, sol, r)) { bad++; printf("  midgame: solver r=%d truth=%d\n", r, truth); }
        } else {
            unsolvable++;
            if (r != -1) { bad++; printf("  midgame: solver %d for an unsolvable position\n", r); }
        }
        sfree(sol);
    }
    printf("  midgame %d/%d/%d: %d positions, %d unsolvable, %d wrong\n", nc, nt, nl, tot, unsolvable, bad);
    CHECK(bad == 0);
}

static void test_backwards(int nc, int nt, int nl, int trials, random_state *rs)
{
    int i, bad = 0, uniform_start = 0;
    for (i = 0; i < trials; i++) {
        signed char t[MAX_TUBES][MAX_LAYERS];
        struct pour log[2000], rev[2000];
        int nlog = 0, j, steps = 14 * nc * nl;
        memset(t, -1, sizeof(t));
        for (j = 0; j < nc; j++) { int l; for (l = 0; l < nl; l++) t[j][l] = (signed char)j; }
        scramble_backwards(t, nt, nl, rs, steps, log, &nlog);
        for (j = 0; j < nlog; j++) rev[j] = log[nlog - 1 - j];
        if (!replay(t, nt, nl, rev, nlog)) { bad++; }
        if (count_complete(t, nt, nl) > 0) uniform_start++;
    }
    printf("  backwards %d/%d/%d: %d runs, %d bad replays, %d with a complete tube\n", nc, nt, nl, trials, bad, uniform_start);
    CHECK(bad == 0);
}

static void test_generator(const game_params *p, int trials, random_state *rs, bool hint_play)
{
    int i, fails = 0, unsolved_hint = 0;
    double tgen = 0, tgenmax = 0, thint = 0, thintmax = 0;
    long steps = 0, nsolved = 0;
    char *name = encode_params(p, true);
    for (i = 0; i < trials; i++) {
        char *aux = NULL, *desc;
        double t0 = now(), dt;
        game_state *st;
        const char *err;
        desc = new_game_desc(p, rs, &aux, false);
        dt = now() - t0;
        tgen += dt; if (dt > tgenmax) tgenmax = dt;
        err = validate_desc(p, desc);
        if (err) { printf("  invalid desc %s: %s\n", desc, err); fails++; sfree(desc); continue; }
        st = new_game(NULL, p, desc);
        if (st->solved) { printf("  already solved: %s\n", desc); fails++; }
        if (hint_play) {
            int guard = 0;
            while (!st->solved && guard < 2000) {
                const char *e = NULL;
                char *mv;
                game_state *ns;
                t0 = now();
                mv = hint_game(st, st, &e);
                dt = now() - t0;
                thint += dt; if (dt > thintmax) thintmax = dt;
                if (!mv) { unsolved_hint++; printf("  hint failed (%s) after %d steps on %s\n", e, guard, desc); break; }
                ns = execute_move(st, mv);
                sfree(mv);
                if (!ns) { printf("  hint move rejected\n"); fails++; break; }
                free_game(st);
                st = ns;
                guard++;
            }
            if (st->solved) { nsolved++; steps += st->moves; }
            else if (guard >= 2000) { printf("  hint did not finish: %s\n", desc); fails++; }
        }
        free_game(st);
        sfree(desc);
    }
    printf("  %-14s: gen avg %.3fs max %.3fs", name, tgen / trials, tgenmax);
    if (hint_play) printf("; solved by hints %ld/%d avg %.1f moves, hint avg %.4fs max %.3fs",
                          nsolved, trials, nsolved ? (double)steps / nsolved : 0.0,
                          thint / (steps ? steps : 1), thintmax);
    printf("\n");
    sfree(name);
    CHECK(fails == 0);
    CHECK(unsolved_hint == 0);
}

/* positions that the player gets to after some random pours: the hint has to find the way, or say there is none */
static void test_wander(int nc, int nt, int nl, bool hidden, int trials, int maxwander, random_state *rs)
{
    int i, ok = 0, none = 0, gaveup = 0, bad = 0;
    double worst = 0, total = 0;
    long steps = 0;
    for (i = 0; i < trials; i++) {
        game_params p;
        char *aux = NULL, *desc;
        game_state *st;
        int w, wn = random_upto(rs, maxwander + 1), guard = 0;
        p.ncolours = nc; p.ntubes = nt; p.nlayers = nl; p.hiddenlayers = hidden;
        desc = new_game_desc(&p, rs, &aux, false);
        st = new_game(NULL, &p, desc);
        for (w = 0; w < wn; w++) {
            int a = random_upto(rs, nt), b = random_upto(rs, nt), k = pour_amount(st->tubes, nl, a, b);
            if (k) {
                char mv[32];
                game_state *ns;
                sprintf(mv, "p %d %d %d", a, b, k);
                ns = execute_move(st, mv);
                if (ns) { free_game(st); st = ns; }
            }
            if (st->solved) break;
        }
        while (!st->solved && guard < 3000) {
            const char *e = NULL;
            double t0 = now(), dt;
            char *mv = hint_game(st, st, &e);
            game_state *ns;
            dt = now() - t0;
            total += dt; if (dt > worst) worst = dt;
            if (!mv) {
                if (strstr(e, "cannot be solved")) none++; else gaveup++;
                break;
            }
            ns = execute_move(st, mv);
            sfree(mv);
            if (!ns) { bad++; break; }
            free_game(st);
            st = ns;
            guard++;
            steps++;
        }
        if (st->solved) ok++;
        else if (guard >= 3000) bad++;
        free_game(st);
        sfree(desc);
    }
    printf("  wander %2d/%2d/%d%s: %d games: %d finished by hints, %d with no way out, %d gave up, %d bad; hint avg %.4fs, worst %.3fs\n",
           nc, nt, nl, hidden ? "h" : " ", trials, ok, none, gaveup, bad, steps ? total / steps : 0.0, worst);
    CHECK(bad == 0);
    CHECK(gaveup == 0);
}

static void test_desc_fuzz(random_state *rs)
{
    int i;
    game_params p;
    p.ncolours = 4; p.ntubes = 6; p.nlayers = 4; p.hiddenlayers = false;
    for (i = 0; i < 20000; i++) {
        char buf[80];
        int n = random_upto(rs, 60), j;
        const char *chars = "0123456789abcdefABCDEF,,,,xyz ";
        const char *err;
        for (j = 0; j < n; j++) buf[j] = chars[random_upto(rs, (int)strlen(chars))];
        buf[n] = 0;
        err = validate_desc(&p, buf);
        if (!err) {
            /* whatever is accepted has to load, and play without crashing */
            game_state *st = new_game(NULL, &p, buf);
            char *txt = game_text_format(st);
            int k;
            for (k = 0; k < 30; k++) {
                char mv[40];
                game_state *ns;
                sprintf(mv, "p %d %d %d", random_upto(rs, 6), random_upto(rs, 6), 1 + random_upto(rs, 4));
                ns = execute_move(st, mv);
                if (ns) { free_game(st); st = ns; }
            }
            sfree(txt);
            free_game(st);
        }
    }
}

static void test_move_fuzz(random_state *rs)
{
    int i;
    game_params p;
    char *aux, *desc;
    game_state *st;
    p.ncolours = 5; p.ntubes = 7; p.nlayers = 4; p.hiddenlayers = true;
    desc = new_game_desc(&p, rs, &aux, false);
    st = new_game(NULL, &p, desc);
    for (i = 0; i < 20000; i++) {
        char mv[64];
        int n = random_upto(rs, 40), j;
        const char *chars = "p ;0123456789-x";
        game_state *ns;
        for (j = 0; j < n; j++) mv[j] = chars[random_upto(rs, (int)strlen(chars))];
        mv[n] = 0;
        ns = execute_move(st, mv);
        if (ns) {
            /* the numbers of layers must be kept */
            int counts[MAX_COLOURS] = {0}, t, l;
            for (t = 0; t < p.ntubes; t++) for (l = 0; l < p.nlayers; l++) if (ns->tubes[t][l] >= 0) counts[(int)ns->tubes[t][l]]++;
            for (t = 0; t < p.ncolours; t++) CHECK(counts[t] == p.nlayers);
            for (t = 0; t < p.ntubes; t++) { int n2 = tube_count(ns->tubes[t], p.nlayers), l2; for (l2 = n2; l2 < p.nlayers; l2++) CHECK(ns->tubes[t][l2] < 0); }
            free_game(st);
            st = ns;
            if (st->solved) { sfree(desc); desc = new_game_desc(&p, rs, &aux, false); free_game(st); st = new_game(NULL, &p, desc); }
        }
    }
    free_game(st);
    sfree(desc);
}

static void test_params(void)
{
    game_params p, *q;
    char *s;
    int i;
    for (i = 0; i < 6; i++) {
        char *name; game_params *pp;
        CHECK(game_fetch_preset(i, &name, &pp));
        CHECK(validate_params(pp, true) == NULL);
        s = encode_params(pp, true);
        q = default_params();
        decode_params(q, s);
        CHECK(q->ncolours == pp->ncolours && q->ntubes == pp->ntubes && q->nlayers == pp->nlayers && q->hiddenlayers == pp->hiddenlayers);
        sfree(s);
        s = encode_params(pp, false);
        decode_params(q, s);
        CHECK(q->ncolours == pp->ncolours && q->ntubes == pp->ntubes && q->nlayers == pp->nlayers && q->hiddenlayers == pp->hiddenlayers);
        {
            char *fs = encode_params(pp, true);
            printf("  preset %-34s full %-10s short %s\n", name, fs, s);
            sfree(fs);
        }
        sfree(s); sfree(name); free_params(pp); free_params(q);
    }
    decode_params(&p, "7"); CHECK(p.ncolours == 7 && p.ntubes == 9 && p.nlayers == 4);
    decode_params(&p, "5w7"); CHECK(p.ncolours == 7 && p.ntubes == 9 && p.nlayers == 5);
    decode_params(&p, "10x4w7h"); CHECK(p.ncolours == 7 && p.ntubes == 10 && p.nlayers == 4 && p.hiddenlayers);
    decode_params(&p, ""); CHECK(validate_params(&p, true) != NULL);
    decode_params(&p, "x"); CHECK(validate_params(&p, true) != NULL);
    decode_params(&p, "99x99w99"); CHECK(validate_params(&p, true) != NULL);
}

int main(int argc, char **argv)
{
    random_state *rs = random_new("decanting-test", 14);
    int scale = argc > 1 ? atoi(argv[1]) : 1;
    int mode = argc > 2 ? atoi(argv[2]) : 0;
    double t0 = now();

    printf("params\n");
    test_params();
    if (mode == 0 || mode == 1) {
        printf("solver against exhaustive search (dealt games)\n");
        test_solver_vs_bfs(2, 3, 3, 60 * scale, rs);
        test_solver_vs_bfs(2, 3, 4, 60 * scale, rs);
        test_solver_vs_bfs(3, 4, 3, 60 * scale, rs);
        test_solver_vs_bfs(3, 5, 3, 40 * scale, rs);
        test_solver_vs_bfs(3, 4, 4, 30 * scale, rs);
        test_solver_vs_bfs(4, 5, 3, 30 * scale, rs);
        test_solver_vs_bfs(4, 6, 3, 15 * scale, rs);
        test_solver_vs_bfs(3, 5, 4, 10 * scale, rs);
        printf("solver against exhaustive search (positions in play)\n");
        test_solver_midgame(3, 4, 3, 60 * scale, rs);
        test_solver_midgame(3, 5, 3, 40 * scale, rs);
        test_solver_midgame(4, 5, 3, 30 * scale, rs);
        test_solver_midgame(4, 6, 3, 10 * scale, rs);
    }
    if (mode == 0 || mode == 2) {
        printf("scrambling backwards\n");
        test_backwards(2, 3, 3, 200, rs);
        test_backwards(4, 6, 4, 200, rs);
        test_backwards(7, 9, 4, 200, rs);
        test_backwards(12, 14, 5, 100, rs);
        test_backwards(12, 13, 8, 50, rs);
        test_backwards(12, 16, 8, 50, rs);
        printf("descriptions and moves\n");
        test_desc_fuzz(rs);
        test_move_fuzz(rs);
    }
    if (mode == 0 || mode == 3) {
        int i;
        printf("generator and hint\n");
        for (i = 0; i < 6; i++) {
            char *name; game_params *pp;
            game_fetch_preset(i, &name, &pp);
            test_generator(pp, 10 * scale, rs, true);
            sfree(name); free_params(pp);
        }
        {
            static const int extra[][3] = { {2, 3, 2}, {2, 3, 8}, {3, 4, 3}, {5, 6, 4}, {8, 9, 4}, {6, 7, 6}, {10, 11, 5},
                                            {12, 13, 4}, {12, 16, 8}, {9, 10, 8}, {12, 14, 8}, {11, 12, 7} };
            for (i = 0; i < (int)lenof(extra); i++) {
                game_params p;
                p.ncolours = extra[i][0]; p.ntubes = extra[i][1]; p.nlayers = extra[i][2]; p.hiddenlayers = false;
                test_generator(&p, 4 * scale, rs, true);
            }
        }
    }
    if (mode == 0 || mode == 4) {
        printf("hint after wandering\n");
        test_wander(4, 6, 4, false, 100 * scale, 25, rs);
        test_wander(7, 9, 4, false, 100 * scale, 40, rs);
        test_wander(7, 9, 4, true, 100 * scale, 40, rs);
        test_wander(10, 12, 4, false, 60 * scale, 60, rs);
        test_wander(12, 14, 5, false, 40 * scale, 80, rs);
        test_wander(7, 8, 4, false, 100 * scale, 30, rs);
        test_wander(10, 11, 4, false, 50 * scale, 40, rs);
        test_wander(12, 13, 5, false, 30 * scale, 40, rs);
        test_wander(12, 16, 8, false, 20 * scale, 80, rs);
        test_wander(12, 13, 8, false, 10 * scale, 40, rs);
    }
    printf("%s (%d failures, %.1fs)\n", failures ? "FAILED" : "all passed", failures, now() - t0);
    random_free(rs);
    return failures != 0;
}
