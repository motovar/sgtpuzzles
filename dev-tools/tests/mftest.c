/* White-box tests of Minor Finder: it includes the game.  Build with tests/empty.c as the game source. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "minorfinder.c"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static random_state *rs;
static int rnd(int n) { return (int)random_upto(rs, n); }

/* ---- a plain adjacency matrix, for comparing ---- */
typedef struct { int n; bool alive[MAXB]; bool a[MAXB][MAXB]; } naive;

static void naive_from(naive *g, const bgraph *b, int n)
{
    int i, j;
    g->n = n;
    for (i = 0; i < n; i++) {
        g->alive[i] = b->alive >> i & 1;
        for (j = 0; j < n; j++) g->a[i][j] = b->adj[i] >> j & 1;
    }
}

static void naive_contract(naive *g, int dom, int rec)
{
    int i;
    for (i = 0; i < g->n; i++)
        if (g->a[rec][i] && i != dom && i != rec) { g->a[dom][i] = g->a[i][dom] = true; }
    for (i = 0; i < g->n; i++) g->a[rec][i] = g->a[i][rec] = false;
    g->alive[rec] = false;
}
static void naive_delvert(naive *g, int v)
{
    int i;
    for (i = 0; i < g->n; i++) g->a[v][i] = g->a[i][v] = false;
    g->alive[v] = false;
}

static bool same(const naive *g, const bgraph *b)
{
    int i, j;
    for (i = 0; i < g->n; i++) {
        if (g->alive[i] != (bool)(b->alive >> i & 1)) return false;
        for (j = 0; j < g->n; j++) if (g->a[i][j] != (bool)(b->adj[i] >> j & 1)) return false;
    }
    return true;
}

static void test_ops(int rounds)
{
    int r, i, j;
    for (r = 0; r < rounds; r++) {
        int n = 3 + rnd(MAXB - 2), steps;
        bgraph b;
        naive g;
        memset(&b, 0, sizeof b);
        b.alive = ((mask_t)1 << n) - 1;
        for (i = 0; i < n; i++) for (j = i + 1; j < n; j++)
            if (rnd(100) < 30) { b.adj[i] |= 1u << j; b.adj[j] |= 1u << i; }
        naive_from(&g, &b, n);
        CHECK(same(&g, &b));
        for (steps = 0; steps < 3 * n; steps++) {
            int op = rnd(3), a = rnd(n), c = rnd(n);
            if (!g.alive[a] || !g.alive[c] || a == c) continue;
            if (op == 0 && g.a[a][c]) { bg_contract(&b, a, c); naive_contract(&g, a, c); }
            else if (op == 1 && g.a[a][c]) { bg_delete_edge(&b, a, c); g.a[a][c] = g.a[c][a] = false; }
            else if (op == 2) { bg_delete_vertex(&b, a); naive_delvert(&g, a); }
            CHECK(same(&g, &b));
        }
    }
    printf("ops: %d rounds\n", rounds);
}

/* ---- graphs made for the tests ---- */
typedef struct { int n; bool a[MAXB][MAXB]; } mat;

static void rand_mat(mat *m, int n, int pct)
{
    int i, j;
    memset(m, 0, sizeof *m);
    m->n = n;
    for (i = 0; i < n; i++) for (j = i + 1; j < n; j++)
        if (rnd(100) < pct) m->a[i][j] = m->a[j][i] = true;
}

static void mat_to_bg(const mat *m, bgraph *b)
{
    int i, j;
    memset(b, 0, sizeof *b);
    b->alive = ((mask_t)1 << m->n) - 1;
    for (i = 0; i < m->n; i++) for (j = 0; j < m->n; j++)
        if (m->a[i][j]) b->adj[i] |= 1u << j;
}

static game_state *mkstate(const mat *g, const mat *h)
{
    int i, j, nme = 0;
    game_state *s = snew(game_state);
    struct shared *sh = snew(struct shared);
    memset(s, 0, sizeof *s);
    memset(sh, 0, sizeof *sh);
    s->params.mode = 0; s->params.n_base = g->n; s->params.n_min = h->n;
    s->sh = sh;
    sh->refcount = 1; sh->n_base = g->n; sh->n_min = h->n;
    for (i = 0; i < h->n; i++) {
        sh->mx[i] = 20 + 5 * i; sh->my[i] = 30 + 3 * i;
        for (j = 0; j < h->n; j++) if (h->a[i][j]) { sh->madj[i] |= 1u << j; if (j > i) nme++; }
    }
    sh->nmedges = nme;
    mat_to_bg(g, &s->g);
    for (i = 0; i < g->n; i++) { s->x[i] = 16 + 7 * i; s->y[i] = 16 + 5 * i; }
    return s;
}

/* ---- the truth, by trying every labelling ---- */
static bool brute_has_minor(const mat *g, const mat *h)
{
    bgraph b;
    mask_t hadj[MAXM];
    int lab[MAXB], n = g->n, k = h->n, i, j;
    long total = 1, code;
    mat_to_bg(g, &b);
    for (i = 0; i < k; i++) { hadj[i] = 0; for (j = 0; j < k; j++) if (h->a[i][j]) hadj[i] |= 1u << j; }
    for (i = 0; i < n; i++) total *= (k + 1);
    for (code = 0; code < total; code++) {
        long c = code;
        for (i = 0; i < MAXB; i++) lab[i] = -1;
        for (i = 0; i < n; i++) { lab[i] = (int)(c % (k + 1)) - 1; c /= (k + 1); }
        if (model_valid(&b, hadj, k, lab)) return true;
    }
    return false;
}

/* run a plan (or Solve) on a state and see that it ends in the minor */
static bool run_moves(const game_state *s0, const char *moves)
{
    game_state *s = execute_move(s0, moves);
    bool ok;
    if (!s) return false;
    ok = s->solved && bg_is_minor(&s->g, s->sh);
    free_game(s);
    return ok;
}

static bool check_solution(const game_state *s, const char *label)
{
    const char *err = NULL;
    char *mv = solve_game(s, s, NULL, &err);
    bool ok;
    if (!mv) { printf("FAIL %s: solve gave no moves (%s)\n", label, err); failures++; return false; }
    ok = run_moves(s, mv);
    if (!ok) { printf("FAIL %s: the solution does not work: %s\n", label, mv); failures++; }
    sfree(mv);
    return ok;
}

static void test_brute(int rounds)
{
    int r, yes = 0, no = 0, giveup = 0;
    long nodes = 0;
    for (r = 0; r < rounds; r++) {
        int n = 2 + rnd(6), k = 1 + rnd(5), gp = 10 + rnd(70), hp = rnd(101);
        mat g, h;
        bool truth, got;
        int rc;
        game_state *s;
        struct msolver S;
        bgraph b;
        mask_t hadj[MAXM];
        int i, j;
        if (k > n) k = n;
        if (n >= 7 && k >= 5) k = 4;
        rand_mat(&g, n, gp);
        rand_mat(&h, k, hp);
        truth = brute_has_minor(&g, &h);
        mat_to_bg(&g, &b);
        for (i = 0; i < k; i++) { hadj[i] = 0; for (j = 0; j < k; j++) if (h.a[i][j]) hadj[i] |= 1u << j; }
        ms_init(&S, &b, hadj, k, 10000000L);
        rc = ms_solve(&S);
        nodes += S.nodes;
        got = rc == 1;
        if (rc == -1) { giveup++; continue; }
        if (got != truth) {
            printf("FAIL brute: n=%d k=%d truth=%d solver=%d\n", n, k, truth, rc);
            failures++;
        }
        if (got) {
            CHECK(model_valid(&b, hadj, k, S.lab));
            yes++;
            s = mkstate(&g, &h);
            if (!s->solved && !bg_is_minor(&s->g, s->sh)) check_solution(s, "brute");
            else {
                /* the graph is the minor already */
            }
            free_game(s);
        } else no++;
    }
    printf("brute: %d rounds, %d with a minor, %d without, %d gave up, %.1f nodes on average\n",
           rounds, yes, no, giveup, (double)nodes / rounds);
}

/* ---- the truth by the rules of the game: contract, delete an edge, delete a point ---- */
static unsigned char *sem_seen;
static long sem_states;
static int sem_n;

static unsigned sem_key(const bgraph *g, int n)
{
    unsigned key = (unsigned)g->alive;
    int i, j, bit = n;
    for (i = 0; i < n; i++) for (j = i + 1; j < n; j++) { if (g->adj[i] >> j & 1) key |= 1u << bit; bit++; }
    return key;
}

static bool sem_search(const bgraph *g, const struct shared *sh)
{
    int n = sem_n, a, b;
    unsigned key = sem_key(g, n);
    int ne = bg_edges(g);
    if (sem_seen[key >> 3] >> (key & 7) & 1) return false;
    sem_seen[key >> 3] |= 1 << (key & 7);
    sem_states++;
    if (POPCOUNT(g->alive) < sh->n_min || ne < sh->nmedges) return false;
    if (bg_is_minor(g, sh)) return true;
    for (a = 0; a < n; a++) {
        bgraph c;
        if (!(g->alive >> a & 1)) continue;
        c = *g; bg_delete_vertex(&c, a);
        if (sem_search(&c, sh)) return true;
        for (b = 0; b < n; b++) {
            if (!(g->adj[a] >> b & 1)) continue;
            if (b > a) { c = *g; bg_delete_edge(&c, a, b); if (sem_search(&c, sh)) return true; }
            c = *g; bg_contract(&c, a, b); if (sem_search(&c, sh)) return true;
        }
    }
    return false;
}

static bool semantic_has_minor(const mat *g, const mat *h)
{
    game_state *s = mkstate(g, h);
    bool r;
    int bits = g->n + g->n * (g->n - 1) / 2;
    sem_n = g->n;
    sem_seen = calloc(((size_t)1 << bits) / 8 + 1, 1);
    r = sem_search(&s->g, s->sh);
    free(sem_seen);
    free_game(s);
    return r;
}

static void test_semantic(int rounds)
{
    int r, yes = 0, no = 0, giveup = 0;
    long nodes = 0;
    for (r = 0; r < rounds; r++) {
        int hard = r & 1;
        int n = hard ? 5 + rnd(3) : 3 + rnd(4), k = hard ? 3 + rnd(3) : 1 + rnd(4);
        int gp = hard ? 25 + rnd(45) : 15 + rnd(70), hp = hard ? 50 + rnd(51) : rnd(101);
        mat g, h;
        bool truth, got;
        int rc, i, j;
        struct msolver S;
        bgraph b;
        mask_t hadj[MAXM];
        if (k > n) k = n;
        rand_mat(&g, n, gp);
        rand_mat(&h, k, hp);
        truth = semantic_has_minor(&g, &h);
        mat_to_bg(&g, &b);
        for (i = 0; i < k; i++) { hadj[i] = 0; for (j = 0; j < k; j++) if (h.a[i][j]) hadj[i] |= 1u << j; }
        ms_init(&S, &b, hadj, k, 10000000L);
        rc = ms_solve(&S);
        nodes += S.nodes;
        if (rc == -1) { giveup++; continue; }
        got = rc == 1;
        if (got != truth) { printf("FAIL semantic: n=%d k=%d truth=%d solver=%d\n", n, k, truth, rc); failures++; }
        if (got) yes++; else no++;
    }
    printf("semantic: %d rounds, %d with a minor, %d without, %d gave up\n", rounds, yes, no, giveup);
}

/* ---- graphs that have the minor by construction ---- */
static void plant(mat *g, const mat *h, int target_n, int extra_pct)
{
    int n = h->n, i, j;
    mat cur = *h;
    /* split points until there are target_n of them (or add isolated/pendant points) */
    while (n < target_n) {
        int op = rnd(10), v = rnd(n);
        if (op < 7) {                   /* split v: the new point w gets some of the edges of v */
            int w = n++;
            for (i = 0; i < w; i++) cur.a[w][i] = cur.a[i][w] = false;
            for (i = 0; i < w; i++)
                if (cur.a[v][i] && rnd(2)) { cur.a[w][i] = cur.a[i][w] = true; if (rnd(2)) {} else cur.a[v][i] = cur.a[i][v] = false; }
            cur.a[v][w] = cur.a[w][v] = true;
        } else if (op < 9) {            /* a point that hangs on v */
            int w = n++;
            for (i = 0; i < w; i++) cur.a[w][i] = cur.a[i][w] = false;
            cur.a[v][w] = cur.a[w][v] = true;
        } else {                        /* a lonely point */
            int w = n++;
            for (i = 0; i < w; i++) cur.a[w][i] = cur.a[i][w] = false;
        }
        cur.n = n;
    }
    /* more edges, but that only helps */
    for (i = 0; i < n; i++) for (j = i + 1; j < n; j++)
        if (rnd(100) < extra_pct) cur.a[i][j] = cur.a[j][i] = true;
    /* shuffle the points */
    {
        int perm[MAXB];
        for (i = 0; i < n; i++) perm[i] = i;
        shuffle(perm, n, sizeof(int), rs);
        memset(g, 0, sizeof *g);
        g->n = n;
        for (i = 0; i < n; i++) for (j = 0; j < n; j++) g->a[perm[i]][perm[j]] = cur.a[i][j];
    }
}

static void test_planted(int rounds)
{
    int r, ok = 0, giveup = 0, bad = 0;
    long nodes = 0, maxnodes = 0;
    double t0 = now(), worst = 0;
    for (r = 0; r < rounds; r++) {
        int k = 3 + rnd(4), n = k + rnd(MAXB - k + 1);
        mat h, g;
        int rc, i, j;
        struct msolver S;
        bgraph b;
        mask_t hadj[MAXM];
        double t1;
        game_state *s;
        rand_mat(&h, k, 30 + rnd(70));
        plant(&g, &h, n, rnd(25));
        mat_to_bg(&g, &b);
        for (i = 0; i < k; i++) { hadj[i] = 0; for (j = 0; j < k; j++) if (h.a[i][j]) hadj[i] |= 1u << j; }
        ms_init(&S, &b, hadj, k, SOLVE_NODES);
        t1 = now();
        rc = ms_solve(&S);
        t1 = now() - t1;
        if (t1 > worst) worst = t1;
        nodes += S.nodes;
        if (S.nodes > maxnodes) maxnodes = S.nodes;
        if (rc == 1) {
            ok++;
            CHECK(model_valid(&b, hadj, k, S.lab));
            s = mkstate(&g, &h);
            if (!bg_is_minor(&s->g, s->sh))
                check_solution(s, "planted");
            free_game(s);
        } else if (rc == -1) {
            giveup++;
            if (getenv("MF_VERBOSE")) {
                struct msolver S2;
                int rc2;
                double t2 = now();
                printf("gave up: n=%d k=%d hedges=%d gedges=%d:", n, k, h.n, bg_edges(&b));
                ms_init(&S2, &b, hadj, k, 300000000L);
                rc2 = ms_solve(&S2);
                printf(" with 300M nodes: rc=%d nodes=%ld in %.2f s\n", rc2, S2.nodes, now() - t2);
            }
        }
        else { bad++; printf("FAIL planted: solver says no minor, n=%d k=%d\n", n, k); failures++; }
    }
    printf("planted: %d rounds: %d solved, %d gave up, %d wrongly refused; %.1f nodes on average, %ld at most, %.4f s at most, %.3f s in all\n",
           rounds, ok, giveup, bad, (double)nodes / rounds, maxnodes, worst, now() - t0);
}

/* ---- a game, played with hints ---- */
static void test_hints(int per_preset, int seed0)
{
    int mode, nb, nm, seed;
    long games = 0, steps = 0, maxsteps = 0, fails = 0;
    double t0 = now(), worst = 0;
    for (mode = 0; mode < 2; mode++)
        for (nb = 15; nb <= 23; nb += (nb == 15 ? 4 : 4))
            for (nm = 4; nm <= 6; nm++) {
                game_params p;
                p.mode = mode; p.n_base = nb; p.n_min = nm;
                if (validate_params(&p, true)) continue;
                for (seed = seed0; seed < seed0 + per_preset; seed++) {
                    char sbuf[32], *aux = NULL, *desc;
                    random_state *r;
                    game_state *orig, *cur;
                    int n = 0;
                    sprintf(sbuf, "h%d", seed);
                    r = random_new(sbuf, strlen(sbuf));
                    desc = new_game_desc(&p, r, &aux, false);
                    orig = new_game(NULL, &p, desc);
                    cur = dup_game(orig);
                    games++;
                    while (!cur->solved && n < 200) {
                        const char *err = NULL;
                        char *mv;
                        game_state *nx;
                        double t1 = now();
                        int before = POPCOUNT(cur->g.alive) + bg_edges(&cur->g);
                        mv = hint_game(orig, cur, &err);
                        t1 = now() - t1;
                        if (t1 > worst) worst = t1;
                        if (!mv) { printf("FAIL hints: no hint (%s) %d-%dx%d %s step %d\n", err, mode, nb, nm, sbuf, n); fails++; failures++; break; }
                        nx = execute_move(cur, mv);
                        if (!nx) { printf("FAIL hints: bad move %s\n", mv); fails++; failures++; sfree(mv); break; }
                        if (POPCOUNT(nx->g.alive) + bg_edges(&nx->g) >= before) { printf("FAIL hints: no progress with %s\n", mv); failures++; }
                        sfree(mv);
                        free_game(cur);
                        cur = nx;
                        n++;
                    }
                    if (!cur->solved) { fails++; if (n >= 200) { printf("FAIL hints: too many steps\n"); failures++; } }
                    steps += n;
                    if (n > maxsteps) maxsteps = n;
                    /* and Solve from the start */
                    {
                        const char *err = NULL;
                        char *mv = solve_game(orig, orig, NULL, &err);
                        if (!mv) { printf("FAIL solve: %s\n", err); failures++; }
                        else {
                            game_state *s = execute_move(orig, mv);
                            if (!s || !s->solved || !s->cheated) { printf("FAIL solve: result %s\n", mv); failures++; }
                            if (s) free_game(s);
                            sfree(mv);
                        }
                    }
                    free_game(cur); free_game(orig); sfree(desc); sfree(aux); random_free(r);
                }
            }
    printf("hints: %ld games, %ld steps (at most %ld), %ld failed, slowest hint %.4f s, %.2f s in all\n",
           games, steps, maxsteps, fails, worst, now() - t0);
}

int main(int argc, char **argv)
{
    const char *what = argc > 1 ? argv[1] : "all";
    int n = argc > 2 ? atoi(argv[2]) : 200;
    rs = random_new("mftest", 6);
    if (!strcmp(what, "all") || !strcmp(what, "ops")) test_ops(n);
    if (!strcmp(what, "all") || !strcmp(what, "brute")) test_brute(n);
    if (!strcmp(what, "all") || !strcmp(what, "semantic")) test_semantic(n);
    if (!strcmp(what, "all") || !strcmp(what, "planted")) test_planted(n);
    if (!strcmp(what, "all") || !strcmp(what, "hints")) test_hints(n > 20 ? 20 : n, 0);
    printf(failures ? "%d FAILURES\n" : "all passed\n", failures);
    return failures != 0;
}
