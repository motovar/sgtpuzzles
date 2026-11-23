/* White-box tests of Decanting: it includes the game.  Build with tests/empty.c as the game source.
 *   dectest [scale [mode]]
 * modes: 0 all, 1 solver against exhaustive search, 2 backwards scrambling + descriptions + moves,
 *        3 generator and hint, 4 hint after wandering, 5 levels of difficulty (efforts), 6 big games,
 *        7 layout and parameters
 */
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

static void canon(const int *t, int nt, int nl, unsigned char *out)
{
    int i, j;
    for (i = 0; i < nt; i++)
        for (j = 0; j < nl; j++)
            out[i * nl + j] = (unsigned char)(t[i * nl + j] + 1);
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
static int bfs_solve(const int *start, int nt, int nl, int maxstates)
{
    struct bfs b;
    unsigned char *key = malloc((size_t)nt * nl);
    int *cur = malloc((size_t)nt * nl * sizeof(int)), *nb = malloc((size_t)nt * nl * sizeof(int));
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
        for (i = 0; i < nt * nl; i++)
            cur[i] = (int)b.keys[(size_t)id * b.sb + i] - 1;
        if (tubes_solved(cur, nt, nl)) { result = b.dist[id]; break; }
        if (b.n > maxstates) { result = -2; break; }
        for (i = 0; i < nt; i++)
            for (j = 0; j < nt; j++) {
                int k = pour_amount(cur, nl, i, j);
                if (!k) continue;
                memcpy(nb, cur, (size_t)nt * nl * sizeof(int));
                pour_layers(nb, nl, i, j, k);
                canon(nb, nt, nl, key);
                if (bfs_find(&b, key, false, 0) < 0) {
                    bfs_find(&b, key, true, b.dist[id] + 1);
                    if (b.tcap < 2 * b.n) { printf("table too small\n"); exit(2); }
                }
            }
    }
    free(b.keys); free(b.dist); free(b.queue); free(b.table);
    free(key); free(cur); free(nb);
    return result;
}

static void deal(int *t, int nc, int nt, int nl, random_state *rs)
{
    int *units = malloc((size_t)nc * nl * sizeof(int));
    deal_random(t, units, nt, nl, nc, rs);
    free(units);
}

/* replays a solution and says whether it is legal and ends in a finished game */
static bool replay(const int *start, int nt, int nl, const struct pour *sol, int n)
{
    int *t = malloc((size_t)nt * nl * sizeof(int));
    bool ok = true;
    int i;
    memcpy(t, start, (size_t)nt * nl * sizeof(int));
    for (i = 0; i < n && ok; i++) {
        int k = pour_amount(t, nl, sol[i].from, sol[i].to);
        if (k != sol[i].n || k == 0) ok = false;
        else pour_layers(t, nl, sol[i].from, sol[i].to, k);
    }
    if (ok) ok = tubes_solved(t, nt, nl);
    free(t);
    return ok;
}

/* the moves "p a b n;p ..." of a string, as a list */
static int parse_pours(const char *s, struct pour *out, int max)
{
    int n = 0, used;
    while (*s) {
        int a, b, k;
        while (*s == ';' || *s == ' ') s++;
        if (!*s) break;
        if (sscanf(s, "p %d %d %d%n", &a, &b, &k, &used) < 3) return -1;
        if (n >= max) return -1;
        out[n].from = a; out[n].to = b; out[n].n = k;
        n++;
        s += used;
    }
    return n;
}

static void test_solver_vs_bfs(int nc, int nt, int nl, int trials, random_state *rs)
{
    int i, agree = 0, solvable = 0, unsolvable = 0, bad = 0;
    long sumopt = 0, sumsol = 0;
    int worst_extra = 0;
    int *t = malloc((size_t)nt * nl * sizeof(int));
    for (i = 0; i < trials; i++) {
        struct pour *sol = NULL;
        int truth, r;
        deal(t, nc, nt, nl, rs);
        truth = bfs_solve(t, nt, nl, 3000000);
        if (truth == -2) continue;
        r = solve_search(t, nt, nl, nc, 5000000, NULL, 0, &sol, NULL);
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
    free(t);
    printf("  %d colours %d tubes %d layers: %d trials, %d solvable, %d unsolvable, %d wrong; avg optimal %.1f, solver %.1f, worst +%d\n",
           nc, nt, nl, trials, solvable, unsolvable, bad,
           solvable ? (double)sumopt / solvable : 0.0, solvable ? (double)sumsol / solvable : 0.0, worst_extra);
    CHECK(bad == 0);
}

/* positions that arise during play (after some random pours), not only dealt ones */
static void test_solver_midgame(int nc, int nt, int nl, int trials, random_state *rs)
{
    int i, bad = 0, unsolvable = 0, tot = 0;
    int *t = malloc((size_t)nt * nl * sizeof(int));
    for (i = 0; i < trials; i++) {
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
        r = solve_search(t, nt, nl, nc, 5000000, NULL, 0, &sol, NULL);
        if (truth >= 0) {
            if (r < 0 || !replay(t, nt, nl, sol, r)) { bad++; printf("  midgame: solver r=%d truth=%d\n", r, truth); }
        } else {
            unsolvable++;
            if (r != -1) { bad++; printf("  midgame: solver %d for an unsolvable position\n", r); }
        }
        sfree(sol);
    }
    free(t);
    printf("  midgame %d/%d/%d: %d positions, %d unsolvable, %d wrong\n", nc, nt, nl, tot, unsolvable, bad);
    CHECK(bad == 0);
}

static void test_backwards(int nc, int nt, int nl, int trials, random_state *rs)
{
    int i, bad = 0, uniform_start = 0, steps = 14 * nc * nl;
    int *t = malloc((size_t)nt * nl * sizeof(int));
    struct pour *log = malloc((size_t)steps * sizeof(struct pour)), *rev = malloc((size_t)steps * sizeof(struct pour));
    for (i = 0; i < trials; i++) {
        int nlog = 0, j;
        for (j = 0; j < nt * nl; j++) t[j] = -1;
        for (j = 0; j < nc; j++) { int l; for (l = 0; l < nl; l++) t[j * nl + l] = j; }
        scramble_backwards(t, nt, nl, rs, steps, log, &nlog);
        for (j = 0; j < nlog; j++) rev[j] = log[nlog - 1 - j];
        if (!replay(t, nt, nl, rev, nlog)) { bad++; }
        if (count_complete(t, nt, nl) > 0) uniform_start++;
    }
    free(t); free(log); free(rev);
    printf("  backwards %d/%d/%d: %d runs, %d bad replays, %d with a complete tube\n", nc, nt, nl, trials, bad, uniform_start);
    CHECK(bad == 0);
}

static void make_params(game_params *p, int nc, int nt, int nl, bool hidden, int diff)
{
    p->ncolours = nc; p->ntubes = nt; p->nlayers = nl; p->hiddenlayers = hidden; p->difficulty = diff;
}

/* a game that was made: the description is valid, and aux (when there is one) is a solution */
static bool check_made(const game_params *p, const char *desc, const char *aux)
{
    const char *err = validate_desc(p, desc);
    game_state *st;
    bool ok = true;
    if (err) { printf("  invalid desc %s: %s\n", desc, err); return false; }
    st = new_game(NULL, p, desc);
    if (st->solved) { printf("  already solved: %s\n", desc); ok = false; }
    if (aux) {
        int max = (int)strlen(aux) / 6 + 2;
        struct pour *sol = malloc((size_t)max * sizeof(struct pour));
        int n = parse_pours(aux, sol, max);
        if (n < 0 || !replay(st->tubes, p->ntubes, p->nlayers, sol, n)) {
            printf("  aux is not a solution: %.80s\n", aux);
            ok = false;
        }
        free(sol);
    }
    free_game(st);
    return ok;
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
        desc = new_game_desc(p, rs, &aux, false);
        dt = now() - t0;
        tgen += dt; if (dt > tgenmax) tgenmax = dt;
        if (!check_made(p, desc, aux)) { fails++; sfree(desc); sfree(aux); continue; }
        st = new_game(NULL, p, desc);
        if (hint_play) {
            int guard = 0;
            while (!st->solved && guard < 5000) {
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
            else if (guard >= 5000) { printf("  hint did not finish: %s\n", desc); fails++; }
        }
        free_game(st);
        sfree(desc);
        sfree(aux);
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
        make_params(&p, nc, nt, nl, hidden, i % DIFFCOUNT);
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
        while (!st->solved && guard < 6000) {
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
        else if (guard >= 6000) bad++;
        free_game(st);
        sfree(desc);
        sfree(aux);
    }
    printf("  wander %2d/%2d/%d%s: %d games: %d finished by hints, %d with no way out, %d gave up, %d bad; hint avg %.4fs, worst %.3fs\n",
           nc, nt, nl, hidden ? "h" : " ", trials, ok, none, gaveup, bad, steps ? total / steps : 0.0, worst);
    CHECK(bad == 0);
    CHECK(gaveup == 0);
}

static void test_desc_fuzz(random_state *rs)
{
    int i, round;
    for (round = 0; round < 2; round++) {
        game_params p;
        /* the second round has more than 16 colours: decimal numbers with dots */
        if (round == 0) make_params(&p, 4, 6, 4, false, DIFF_NORMAL);
        else make_params(&p, 18, 20, 2, false, DIFF_NORMAL);
        for (i = 0; i < 40000; i++) {
            char buf[160];
            int n = random_upto(rs, 100), j;
            const char *chars = round == 0 ? "0123456789abcdefABCDEF,,,,xyz " : "0123456789.....,,,, 17";
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
                    sprintf(mv, "p %d %d %d", random_upto(rs, p.ntubes), random_upto(rs, p.ntubes), 1 + random_upto(rs, 4));
                    ns = execute_move(st, mv);
                    if (ns) { free_game(st); st = ns; }
                }
                sfree(txt);
                free_game(st);
            }
        }
    }
}

static void test_move_fuzz(random_state *rs)
{
    int i;
    game_params p;
    char *aux, *desc;
    game_state *st;
    make_params(&p, 5, 7, 4, true, DIFF_NORMAL);
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
            int counts[16] = {0}, t, l;
            for (t = 0; t < p.ntubes; t++) for (l = 0; l < p.nlayers; l++) if (ns->tubes[t * p.nlayers + l] >= 0) counts[ns->tubes[t * p.nlayers + l]]++;
            for (t = 0; t < p.ncolours; t++) CHECK(counts[t] == p.nlayers);
            for (t = 0; t < p.ntubes; t++) { int n2 = tube_count(ns->tubes + t * p.nlayers, p.nlayers), l2; for (l2 = n2; l2 < p.nlayers; l2++) CHECK(ns->tubes[t * p.nlayers + l2] < 0); }
            free_game(st);
            st = ns;
            if (st->solved) { sfree(desc); sfree(aux); desc = new_game_desc(&p, rs, &aux, false); free_game(st); st = new_game(NULL, &p, desc); }
        }
    }
    free_game(st);
    sfree(desc);
    sfree(aux);
}

static bool same_params(const game_params *a, const game_params *b, bool diff)
{
    return a->ncolours == b->ncolours && a->ntubes == b->ntubes && a->nlayers == b->nlayers &&
        a->hiddenlayers == b->hiddenlayers && (!diff || a->difficulty == b->difficulty);
}

static void test_params(void)
{
    game_params p, *q;
    char *s;
    int i;
    for (i = 0; i < 12; i++) {
        char *name; game_params *pp;
        CHECK(game_fetch_preset(i, &name, &pp));
        CHECK(validate_params(pp, true) == NULL);
        s = encode_params(pp, true);
        q = default_params();
        decode_params(q, s);
        CHECK(same_params(q, pp, true));
        sfree(s);
        s = encode_params(pp, false);
        decode_params(q, s);
        CHECK(same_params(q, pp, false));
        {
            char *fs = encode_params(pp, true);
            printf("  preset %-34s full %-14s short %s\n", name, fs, s);
            sfree(fs);
        }
        sfree(s); sfree(name); free_params(pp); free_params(q);
    }
    {
        char *name; game_params *pp;
        CHECK(!game_fetch_preset(12, &name, &pp));
    }
    decode_params(&p, "7"); CHECK(p.ncolours == 7 && p.ntubes == 9 && p.nlayers == 4 && p.difficulty == DIFF_NORMAL);
    decode_params(&p, "5w7"); CHECK(p.ncolours == 7 && p.ntubes == 9 && p.nlayers == 5);
    decode_params(&p, "10x4w7h"); CHECK(p.ncolours == 7 && p.ntubes == 10 && p.nlayers == 4 && p.hiddenlayers);
    decode_params(&p, "7de"); CHECK(p.ncolours == 7 && p.difficulty == DIFF_EASY && !p.hiddenlayers);
    decode_params(&p, "7dh"); CHECK(p.ncolours == 7 && p.difficulty == DIFF_HARD && !p.hiddenlayers);
    decode_params(&p, "9x4w7hdh"); CHECK(p.ntubes == 9 && p.hiddenlayers && p.difficulty == DIFF_HARD);
    decode_params(&p, "9x4w7dn"); CHECK(p.difficulty == DIFF_NORMAL);
    decode_params(&p, "9x4w7dz"); CHECK(p.difficulty == DIFF_NORMAL);
    decode_params(&p, ""); CHECK(validate_params(&p, true) != NULL);
    decode_params(&p, "x"); CHECK(validate_params(&p, true) != NULL);
    decode_params(&p, "99x99w99"); CHECK(validate_params(&p, true) != NULL);
    /* the limits of the sketch are gone */
    decode_params(&p, "30x9w25"); CHECK(validate_params(&p, true) == NULL);
    decode_params(&p, "20x30w16"); CHECK(validate_params(&p, true) == NULL);
    decode_params(&p, "500x2w300"); CHECK(validate_params(&p, true) == NULL);
    decode_params(&p, "5x40w3"); CHECK(validate_params(&p, true) == NULL);
    decode_params(&p, "5x1w3"); CHECK(validate_params(&p, true) != NULL);
    decode_params(&p, "5x4w1"); CHECK(validate_params(&p, true) != NULL);
    decode_params(&p, "5x4w5"); CHECK(validate_params(&p, true) != NULL);
    decode_params(&p, "99999999x99999999w9"); CHECK(validate_params(&p, true) != NULL);
}

/* the layout: every tube has a place, the places do not overlap, and the middle of the place gives the tube back */
static void test_layout(void)
{
    int nt, bad = 0;
    for (nt = 3; nt <= 400; nt += (nt < 40 ? 1 : 37)) {
        int nl;
        for (nl = 2; nl <= 12; nl += 5) {
            game_params p;
            game_state st;
            game_drawstate ds;
            int cols, rows, ts = 20, w, h, t;
            make_params(&p, nt - 2, nt, nl, false, DIFF_NORMAL);
            tube_layout(&p, &cols, &rows);
            if (cols * rows < nt || (rows - 1) * cols >= nt) { bad++; printf("  layout %d/%d: %d x %d\n", nt, nl, cols, rows); }
            game_compute_size(&p, ts, NULL, &w, &h);
            memset(&st, 0, sizeof(st));
            memset(&ds, 0, sizeof(ds));
            st.p = p;
            ds.tilesize = ts;
            for (t = 0; t < nt; t++) {
                int x, y, tw, th;
                tube_slot(&p, ts, t, &x, &y, &tw, &th);
                if (x < 0 || y < 0 || x + tw > w || y + th > h) { bad++; printf("  slot of %d outside (nt %d)\n", t, nt); break; }
                if (tube_at(&st, &ds, x + tw / 2, y + th / 2) != t) { bad++; printf("  tube_at fails for %d (nt %d)\n", t, nt); break; }
            }
        }
    }
    CHECK(bad == 0);
}

/* the colours of the liquids: they are different from one another as far as that can be told */
static void test_colours(void)
{
    int n, c, i, bad = 0, close = 0, total = 0;
    float *prev;
    for (n = 13; n <= 300; n += 31) {
        for (c = 0; c < n; c++) {
            float a[3];
            liquid_rgb(c, a);
            for (i = 0; i < 3; i++) if (a[i] < 0.0F || a[i] > 1.0F) bad++;
        }
    }
    prev = malloc(3 * 200 * sizeof(float));
    for (c = 0; c < 200; c++) liquid_rgb(c, prev + 3 * c);
    for (c = 12; c < 40; c++) {
        for (i = 0; i < c; i++) {
            float d = fabsf(prev[3 * c] - prev[3 * i]) + fabsf(prev[3 * c + 1] - prev[3 * i + 1]) + fabsf(prev[3 * c + 2] - prev[3 * i + 2]);
            total++;
            if (d < 0.12F) close++;
        }
    }
    free(prev);
    printf("  colours: %d out of range; %d of %d pairs among the first 40 are very close\n", bad, close, total);
    CHECK(bad == 0);
}

/* the levels of difficulty: how the work of the solver is spread over dealt games, and what the levels make of it */
static int cmp_long(const void *a, const void *b)
{
    long x = *(const long *)a, y = *(const long *)b;
    return x < y ? -1 : x > y;
}

static long work_of(const game_params *p, const int *t, int *pours)
{
    struct pour *sol = NULL;
    long expanded = 0;
    int res = solve_search(t, p->ntubes, p->nlayers, p->ncolours,
                           node_limit(GEN_NODES, p->ntubes, p->nlayers, p->ncolours), NULL, 0, &sol, &expanded);
    sfree(sol);
    if (pours) *pours = res;
    return res > 0 ? expanded : -1;
}

static void test_efforts(int nc, int nt, int nl, int trials, random_state *rs)
{
    long *eff = malloc((size_t)trials * sizeof(long));
    int i, n = 0, nsolved = 0, nlimit = 0, nunsolv = 0;
    int *t = malloc((size_t)nt * nl * sizeof(int));
    double t0 = now();
    game_params p;
    make_params(&p, nc, nt, nl, false, DIFF_NORMAL);
    for (i = 0; i < trials; i++) {
        int res;
        long w;
        deal(t, nc, nt, nl, rs);
        if (count_complete(t, nt, nl) > 0) continue;
        w = work_of(&p, t, &res);
        if (w >= 0) { eff[n++] = w; nsolved++; }
        else if (res == -2) nlimit++;
        else if (res == -1) nunsolv++;
    }
    free(t);
    qsort(eff, n, sizeof(long), cmp_long);
    printf("  work %2d/%2d/%d: %d solved, %d unsolvable, %d gave up (%.2fs); quantiles 0,10%%..90%%,100%%:", nc, nt, nl, nsolved, nunsolv, nlimit, now() - t0);
    if (n > 0) printf(" %ld", eff[0]);
    for (i = 1; i <= 9 && n > 0; i++) printf(" %ld", eff[(long)n * i / 10]);
    if (n > 0) printf(" %ld", eff[n - 1]);
    printf("\n");
    free(eff);
}

static void test_levels(int nc, int nt, int nl, int trials, bool strict, random_state *rs)
{
    int lvl, i;
    double avg[DIFFCOUNT], avglen[DIFFCOUNT];
    for (lvl = 0; lvl < DIFFCOUNT; lvl++) {
        game_params p;
        int bad = 0, nres = 0;
        double tg = 0, tmax = 0;
        long wsum = 0, lsum = 0;
        make_params(&p, nc, nt, nl, false, lvl);
        for (i = 0; i < trials; i++) {
            char *aux = NULL, *desc;
            double t0 = now(), dt;
            game_state *st;
            int res;
            long w;
            desc = new_game_desc(&p, rs, &aux, false);
            dt = now() - t0;
            tg += dt; if (dt > tmax) tmax = dt;
            if (!check_made(&p, desc, aux)) bad++;
            st = new_game(NULL, &p, desc);
            w = work_of(&p, st->tubes, &res);
            if (w >= 0) { wsum += w; lsum += res; nres++; }
            free_game(st);
            sfree(desc);
            sfree(aux);
        }
        avg[lvl] = nres ? (double)wsum / nres : 0.0;
        avglen[lvl] = nres ? (double)lsum / nres : 0.0;
        printf("  level %d on %2d/%2d/%d: avg work %.1f, avg pours %.1f, %d bad; generation avg %.3fs max %.3fs\n",
               lvl, nc, nt, nl, avg[lvl], avglen[lvl], bad, tg / trials, tmax);
        CHECK(bad == 0);
    }
    if (strict) {
        CHECK(avg[DIFF_EASY] < avg[DIFF_NORMAL]);
        CHECK(avg[DIFF_NORMAL] < avg[DIFF_HARD]);
    }
}

/* games that were out of reach of the sketch: the colours beyond 16 (decimal descriptions), many tubes, many layers */
static void test_big(int nc, int nt, int nl, bool hidden, int diff, int trials, random_state *rs)
{
    game_params p;
    int i, bad = 0, played = 0, nfinished = 0;
    double tg = 0, tmax = 0, th = 0, thmax = 0;
    make_params(&p, nc, nt, nl, hidden, diff);
    CHECK(validate_params(&p, true) == NULL);
    for (i = 0; i < trials; i++) {
        char *aux = NULL, *desc;
        double t0 = now(), dt;
        game_state *st, *st2;
        const char *err = NULL;
        char *mv, *txt, *solved_moves;
        desc = new_game_desc(&p, rs, &aux, false);
        dt = now() - t0;
        tg += dt; if (dt > tmax) tmax = dt;
        if (!check_made(&p, desc, aux)) bad++;
        st = new_game(NULL, &p, desc);
        txt = game_text_format(st);
        sfree(txt);
        /* a hint and a few moves */
        t0 = now();
        mv = hint_game(st, st, &err);
        dt = now() - t0;
        th += dt; if (dt > thmax) thmax = dt;
        if (mv) {
            st2 = execute_move(st, mv);
            if (!st2) { printf("  hint move rejected\n"); bad++; }
            else free_game(st2);
            sfree(mv);
            played++;
        } else if (!aux) {
            /* nothing was found, and nothing is known */
        }
        /* "Solve" gives a way, from the start */
        err = NULL;
        solved_moves = solve_game(st, st, aux, &err);
        if (solved_moves) {
            game_state *cur = dup_game(st);
            const char *s = solved_moves;
            int n = 0;
            while (*s && !cur->solved) {
                int a, b, k, used = 0;
                game_state *ns;
                char one[64];
                while (*s == ';') s++;
                if (sscanf(s, "p %d %d %d%n", &a, &b, &k, &used) < 3) break;
                sprintf(one, "p %d %d %d", a, b, k);
                ns = execute_move(cur, one);
                if (!ns) break;
                free_game(cur);
                cur = ns;
                s += used;
                n++;
            }
            if (cur->solved) nfinished++; else { printf("  the solution does not finish (%d moves)\n", n); bad++; }
            free_game(cur);
            sfree(solved_moves);
        } else {
            printf("  solve: %s\n", err ? err : "(nothing)");
        }
        free_game(st);
        sfree(desc);
        sfree(aux);
    }
    printf("  big %d colours %d tubes %d layers%s level %d: gen avg %.3fs max %.3fs; hint avg %.3fs max %.3fs; %d hints, %d solutions that finish of %d; %d bad\n",
           nc, nt, nl, hidden ? " hidden" : "", diff, tg / trials, tmax, th / trials, thmax, played, nfinished, trials, bad);
    CHECK(bad == 0);
}

extern const drawing_api null_drawing_api;

/* the drawing: every kind of picture (labels, selection, cursor, a pour in the middle, the flash) of games with a lot of colours and tubes */
static void test_draw(int nc, int nt, int nl, bool hidden, random_state *rs)
{
    game_params p;
    char *aux = NULL, *desc;
    game_state *st, *st2 = NULL;
    game_ui *ui;
    game_drawstate *ds;
    midend *me = midend_new(NULL, &decanting, &null_drawing_api, NULL);
    drawing *dr = drawing_new(&null_drawing_api, me, NULL);
    int w, h, labels, a, b, k, step;
    float *cols;
    int ncols = 0;

    make_params(&p, nc, nt, nl, hidden, DIFF_EASY);
    desc = new_game_desc(&p, rs, &aux, false);
    st = new_game(NULL, &p, desc);
    ui = new_ui(st);
    cols = game_colours(NULL, &ncols);
    CHECK(ncols >= COL_0 + (nc > NFIXED_COLOURS ? nc : NFIXED_COLOURS) && ncols == COL_0 + npal_given);
    free(cols);
    ds = game_new_drawstate(dr, st);
    game_compute_size(&p, 24, ui, &w, &h);
    game_set_size(dr, ds, &p, 24);
    CHECK(w > 0 && h > 0);
    for (labels = 0; labels < 3; labels++) {
        ui->labels = labels;
        game_redraw(dr, ds, NULL, st, 1, ui, 0.0F, 0.0F);
        ui->selected = nt - 1;
        ui->cursor = nt / 2;
        ui->cursor_visible = true;
        game_redraw(dr, ds, NULL, st, 1, ui, 0.0F, 0.0F);
        game_redraw(dr, ds, NULL, st, 1, ui, 0.0F, 0.3F);
    }
    /* a pour: find one, make it, and draw the middle of it */
    for (a = 0; a < nt; a++)
        for (b = 0; b < nt && !st2; b++)
            if ((k = pour_amount(st->tubes, nl, a, b)) > 0) {
                char mv[64];
                sprintf(mv, "p %d %d %d", a, b, k);
                st2 = execute_move(st, mv);
            }
    CHECK(st2 != NULL);
    if (st2) {
        float len = game_anim_length(st, st2, 1, ui);
        CHECK(len > 0.0F);
        for (step = 0; step <= 10; step++) {
            game_redraw(dr, ds, st, st2, 1, ui, len * (float)step / 10.0F, 0.0F);
            game_redraw(dr, ds, st2, st, -1, ui, len * (float)step / 10.0F, 0.0F);
        }
        game_redraw(dr, ds, st, st2, 1, ui, 0.0F, 0.0F);
        free_game(st2);
    }
    game_free_drawstate(dr, ds);
    free_ui(ui);
    free_game(st);
    sfree(desc);
    sfree(aux);
    drawing_free(dr);
    midend_free(me);
}

int main(int argc, char **argv)
{
    random_state *rs = random_new("decanting-test", 14);
    int scale = argc > 1 ? atoi(argv[1]) : 1;
    int mode = argc > 2 ? atoi(argv[2]) : 0;
    double t0 = now();

    if (mode == 0 || mode == 7) {
        printf("params\n");
        test_params();
        printf("layout and colours\n");
        test_layout();
        test_colours();
        printf("drawing\n");
        test_draw(4, 6, 4, false, rs);
        test_draw(7, 9, 4, true, rs);
        test_draw(17, 19, 3, false, rs);
        test_draw(45, 48, 3, true, rs);
        test_draw(130, 133, 2, false, rs);
        test_draw(3, 5, 40, false, rs);
    }
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
        test_backwards(30, 33, 6, 30, rs);
        printf("descriptions and moves\n");
        test_desc_fuzz(rs);
        test_move_fuzz(rs);
    }
    if (mode == 0 || mode == 3) {
        int i;
        printf("generator and hint\n");
        for (i = 0; i < 12; i++) {
            char *name; game_params *pp;
            game_fetch_preset(i, &name, &pp);
            test_generator(pp, 10 * scale, rs, true);
            sfree(name); free_params(pp);
        }
        {
            static const int extra[][3] = { {2, 3, 2}, {2, 3, 8}, {3, 4, 3}, {5, 6, 4}, {8, 9, 4}, {6, 7, 6}, {10, 11, 5},
                                            {12, 13, 4}, {12, 16, 8}, {9, 10, 8}, {12, 14, 8}, {11, 12, 7}, {13, 15, 4},
                                            {16, 18, 3}, {17, 19, 3}, {20, 22, 3} };
            for (i = 0; i < (int)lenof(extra); i++) {
                game_params p;
                make_params(&p, extra[i][0], extra[i][1], extra[i][2], false, i % DIFFCOUNT);
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
        test_wander(18, 20, 3, false, 30 * scale, 40, rs);
    }
    if (mode == 5) {
        printf("work of the solver on dealt games\n");
        test_efforts(4, 6, 4, 400 * scale, rs);
        test_efforts(5, 7, 4, 400 * scale, rs);
        test_efforts(7, 9, 4, 400 * scale, rs);
        test_efforts(10, 12, 4, 300 * scale, rs);
        test_efforts(12, 14, 5, 200 * scale, rs);
        test_efforts(7, 8, 4, 300 * scale, rs);
        test_efforts(12, 14, 4, 200 * scale, rs);
        test_efforts(16, 18, 4, 100 * scale, rs);
        test_efforts(10, 12, 6, 100 * scale, rs);
        printf("levels\n");
        test_levels(4, 6, 4, 60 * scale, true, rs);
        test_levels(7, 9, 4, 60 * scale, true, rs);
        test_levels(10, 12, 4, 40 * scale, true, rs);
        test_levels(12, 14, 5, 20 * scale, true, rs);
        test_levels(7, 8, 4, 40 * scale, true, rs);
    }
    if (mode == 0 || mode == 6) {
        printf("big games\n");
        test_big(17, 19, 3, false, DIFF_NORMAL, 3 * scale, rs);
        test_big(20, 22, 4, true, DIFF_HARD, 3 * scale, rs);
        test_big(30, 33, 4, false, DIFF_EASY, 2 * scale, rs);
        test_big(40, 43, 5, false, DIFF_NORMAL, 2 * scale, rs);
        test_big(5, 8, 24, false, DIFF_NORMAL, 3 * scale, rs);
        test_big(3, 5, 60, false, DIFF_HARD, 2 * scale, rs);
        test_big(100, 103, 4, false, DIFF_NORMAL, scale, rs);
        test_big(300, 304, 3, true, DIFF_HARD, scale, rs);
    }
    printf("%s (%d failures, %.1fs)\n", failures ? "FAILED" : "all passed", failures, now() - t0);
    random_free(rs);
    return failures != 0;
}
