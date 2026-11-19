/* White-box tests of the user interface of Minor Finder: gestures in pixels go into interpret_move,
 * and what comes out has to be what the player meant, and always a move that execute_move accepts.
 * Build with tests/empty.c as the game source.   mfui [rounds] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "minorfinder.c"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static random_state *rs;
static int rnd(int n) { return (int)random_upto(rs, n); }

struct env {
    game_params p;
    game_state *st;
    game_ui *ui;
    game_drawstate *ds;
    int ts;
    struct geom g;
    long lim;
    long moves, updates, noeffects, unused;
};

static void env_layout(struct env *e)
{
    geometry(&e->p, e->ui->layout, e->ts, &e->g);
}

static struct env *env_new(int mode, int nb, int nm, const char *seed, int layout, int ts)
{
    struct env *e = snew(struct env);
    random_state *r;
    char *aux = NULL, *desc;
    memset(e, 0, sizeof *e);
    e->p.mode = mode; e->p.n_base = nb; e->p.n_min = nm;
    r = random_new(seed, strlen(seed));
    desc = new_game_desc(&e->p, r, &aux, false);
    e->st = new_game(NULL, &e->p, desc);
    e->ui = new_ui(e->st);
    e->ui->layout = layout;
    e->ds = game_new_drawstate(NULL, e->st);
    e->ts = ts;
    game_set_size(NULL, e->ds, &e->p, ts);
    e->lim = COORDLIMIT(nb) * COORDUNIT;
    env_layout(e);
    sfree(desc); sfree(aux); random_free(r);
    return e;
}

static void env_free(struct env *e)
{
    free_game(e->st);
    free_ui(e->ui);
    game_free_drawstate(NULL, e->ds);
    sfree(e);
}

/* the pixel of a position in the units of the big graph */
static void pix(const struct env *e, double ux, double uy, int *x, int *y)
{
    *x = (int)(from_units((float)ux, e->g.bx, e->g.Wb, e->lim) + 0.5F);
    *y = (int)(from_units((float)uy, e->g.by, e->g.Wb, e->lim) + 0.5F);
}

static void pix_of_point(const struct env *e, int v, int *x, int *y)
{
    pix(e, (double)e->st->x[v], (double)e->st->y[v], x, y);
}

/* send one event;  if it makes a move, the move is made.  Returns the move (to free), or NULL */
static char *ev(struct env *e, int button, int x, int y)
{
    char *r = interpret_move(e->st, e->ui, e->ds, x, y, button);
    if (r == MOVE_UI_UPDATE) { e->updates++; return NULL; }
    if (r == MOVE_NO_EFFECT) { e->noeffects++; return NULL; }
    if (r == MOVE_UNUSED) { e->unused++; return NULL; }
    {
        game_state *ns = execute_move(e->st, r);
        if (!ns) {
            printf("FAIL: the move %s was refused\n", r);
            failures++;
            return r;
        }
        game_changed_state(e->ui, e->st, ns);
        free_game(e->st);
        e->st = ns;
        e->moves++;
    }
    return r;
}

static bool is_move(const char *r) { return r != NULL && r != MOVE_UI_UPDATE && r != MOVE_NO_EFFECT && r != MOVE_UNUSED; }

/* how far is the position from every point except one (in units) */
static double dist_to_nearest(const struct env *e, double ux, double uy, int except)
{
    double best = 1e9;
    int v;
    for (v = 0; v < MAXB; v++) {
        double d;
        if (!(e->st->g.alive >> v & 1) || v == except) continue;
        d = hypot(e->st->x[v] - ux, e->st->y[v] - uy);
        if (d < best) best = d;
    }
    return best;
}

static int nearest(const struct env *e, double ux, double uy)
{
    double best = 1e9;
    int v, bv = -1;
    for (v = 0; v < MAXB; v++) {
        double d;
        if (!(e->st->g.alive >> v & 1)) continue;
        d = hypot(e->st->x[v] - ux, e->st->y[v] - uy);
        if (d < best) { best = d; bv = v; }
    }
    return bv;
}

/* the point v has to be the one that is nearest to its own pixel, or the tests cannot tell */
static bool clear_hit(const struct env *e, int v)
{
    int x, y;
    double ux, uy;
    pix_of_point(e, v, &x, &y);
    ux = (double)to_units(x - e->g.bx, 0, e->g.Wb, e->lim);
    uy = (double)to_units(y - e->g.by, 0, e->g.Wb, e->lim);
    return nearest(e, ux, uy) == v && dist_to_nearest(e, ux, uy, v) > 2 * HIT_RADIUS;
}

/* ---- the gestures that have a known meaning ---- */

static void scenario_tap_tap(struct env *e)
{
    int v, w;
    for (v = 0; v < MAXB; v++) {
        if (!(e->st->g.alive >> v & 1) || !clear_hit(e, v)) continue;
        for (w = 0; w < MAXB; w++) {
            int vx, vy, wx, wy;
            char *r, expect[32];
            game_state *keep;
            if (!(e->st->g.adj[v] >> w & 1) || !clear_hit(e, w)) continue;
            pix_of_point(e, v, &vx, &vy);
            pix_of_point(e, w, &wx, &wy);
            keep = dup_game(e->st);
            r = ev(e, LEFT_BUTTON, vx, vy); CHECK(!is_move(r));
            r = ev(e, LEFT_RELEASE, vx, vy); CHECK(!is_move(r));
            CHECK(e->ui->selected == v);
            r = ev(e, LEFT_BUTTON, wx, wy); CHECK(!is_move(r));
            r = ev(e, LEFT_RELEASE, wx, wy);
            sprintf(expect, "%d:%d-%d;", MOVE_CONTRACT, w, v);
            CHECK(is_move(r) && !strcmp(r, expect));
            if (is_move(r)) sfree(r);
            CHECK(!(e->st->g.alive >> v & 1) && (e->st->g.alive >> w & 1));
            free_game(e->st);
            e->st = keep;
            e->ui->selected = -1;
            return;                    /* one pair per graph is enough: the state was changed */
        }
    }
}

static void scenario_drag_contract(struct env *e)
{
    int v, w;
    for (v = 0; v < MAXB; v++) {
        if (!(e->st->g.alive >> v & 1) || !clear_hit(e, v)) continue;
        for (w = 0; w < MAXB; w++) {
            int vx, vy, wx, wy, i;
            char *r, expect[32];
            game_state *keep;
            if (!(e->st->g.adj[v] >> w & 1) || !clear_hit(e, w)) continue;
            pix_of_point(e, v, &vx, &vy);
            pix_of_point(e, w, &wx, &wy);
            keep = dup_game(e->st);
            r = ev(e, LEFT_BUTTON, vx, vy); CHECK(!is_move(r));
            for (i = 1; i <= 6; i++) {
                r = ev(e, LEFT_DRAG, vx + (wx - vx) * i / 6, vy + (wy - vy) * i / 6); CHECK(!is_move(r));
            }
            CHECK(e->ui->mode == UI_CONTRACT && e->ui->hover == w);
            r = ev(e, LEFT_RELEASE, wx, wy);
            sprintf(expect, "%d:%d-%d;", MOVE_CONTRACT, w, v);
            CHECK(is_move(r) && !strcmp(r, expect));
            if (is_move(r)) sfree(r);
            free_game(e->st);
            e->st = keep;
            return;
        }
    }
}

static void scenario_drag_free(struct env *e)
{
    int v, tries;
    for (v = 0; v < MAXB; v++) {
        if (!(e->st->g.alive >> v & 1) || !clear_hit(e, v)) continue;
        for (tries = 0; tries < 50; tries++) {
            int vx, vy, tx, ty, i;
            double tux, tuy, du;
            char *r, expect[48];
            long ex, ey, X, Y;
            game_state *keep;
            /* a target in the board, away from all the points */
            tux = 3 * POINTRADIUS + rnd((int)(e->lim - 6 * POINTRADIUS));
            tuy = 3 * POINTRADIUS + rnd((int)(e->lim - 6 * POINTRADIUS));
            if (dist_to_nearest(e, tux, tuy, v) < 3 * OVERLAP_RADIUS) continue;
            if (hypot(e->st->x[v] - tux, e->st->y[v] - tuy) < 3 * MOVE_THRESHOLD) continue;   /* not a move at all */
            pix_of_point(e, v, &vx, &vy);
            pix(e, tux, tuy, &tx, &ty);
            keep = dup_game(e->st);
            r = ev(e, LEFT_BUTTON, vx, vy); CHECK(!is_move(r));
            for (i = 1; i <= 4; i++) {
                r = ev(e, LEFT_DRAG, vx + (tx - vx) * i / 4, vy + (ty - vy) * i / 4);
                CHECK(!is_move(r));
            }
            r = ev(e, LEFT_RELEASE, tx, ty);
            CHECK(is_move(r));
            if (is_move(r)) {
                /* the point goes where it was dragged to, relative to where it was pressed */
                du = (double)e->lim / e->g.Wb;
                ex = (long)(e->st->x[v]);
                ey = (long)(e->st->y[v]);
                X = keep->x[v] + (long)((tx - vx) * du);
                Y = keep->y[v] + (long)((ty - vy) * du);
                sprintf(expect, "%d:%d-", MOVE_DRAG, v);
                CHECK(!strncmp(r, expect, strlen(expect)));
                CHECK(labs(ex - X) <= (long)du + 2 && labs(ey - Y) <= (long)du + 2);
                CHECK(e->st->g.alive == keep->g.alive);
                CHECK(!memcmp(e->st->g.adj, keep->g.adj, sizeof keep->g.adj));
                sfree(r);
            }
            free_game(e->st);
            e->st = keep;
            return;
        }
        return;
    }
}

static void scenario_delete(struct env *e)
{
    int v, a, b;
    /* a point */
    for (v = 0; v < MAXB; v++) {
        if (!(e->st->g.alive >> v & 1) || !clear_hit(e, v)) continue;
        {
            int vx, vy;
            char *r, expect[32];
            game_state *keep = dup_game(e->st);
            pix_of_point(e, v, &vx, &vy);
            r = ev(e, RIGHT_BUTTON, vx, vy); CHECK(!is_move(r));
            CHECK(e->ui->mode == UI_DELPOINT && e->ui->armed);
            r = ev(e, RIGHT_RELEASE, vx, vy);
            sprintf(expect, "%d:%d;", MOVE_DELPOINT, v);
            CHECK(is_move(r) && !strcmp(r, expect));
            if (is_move(r)) sfree(r);
            CHECK(!(e->st->g.alive >> v & 1));
            free_game(e->st);
            e->st = keep;
            /* and not when the pointer was taken away before it was let go */
            {
                int away = (int)(4.0 * HIT_RADIUS * e->g.Wb / e->lim) + 4;
                game_state *same = dup_game(e->st);
                r = ev(e, RIGHT_BUTTON, vx, vy); CHECK(!is_move(r));
                r = ev(e, RIGHT_DRAG, vx + away, vy + away); CHECK(!is_move(r));
                CHECK(!e->ui->armed);
                r = ev(e, RIGHT_RELEASE, vx + away, vy + away); CHECK(!is_move(r));
                CHECK(e->st->g.alive == same->g.alive);
                free_game(same);
            }
        }
        break;
    }
    /* an edge: press in the middle of it */
    for (a = 0; a < MAXB; a++) {
        for (b = a + 1; b < MAXB; b++) {
            double mx, my;
            int x, y, nv;
            char *r;
            if (!(e->st->g.alive >> a & 1) || !(e->st->g.adj[a] >> b & 1)) continue;
            mx = (e->st->x[a] + e->st->x[b]) / 2.0;
            my = (e->st->y[a] + e->st->y[b]) / 2.0;
            nv = nearest(e, mx, my);
            if (hypot(e->st->x[nv] - mx, e->st->y[nv] - my) < 2 * HIT_RADIUS) continue;
            pix(e, mx, my, &x, &y);
            {
                game_state *keep = dup_game(e->st);
                int ea, eb;
                r = ev(e, RIGHT_BUTTON, x, y); CHECK(!is_move(r));
                CHECK(e->ui->mode == UI_DELEDGE);
                ea = e->ui->ea; eb = e->ui->eb;
                r = ev(e, RIGHT_RELEASE, x, y);
                CHECK(is_move(r));
                if (is_move(r)) {
                    char expect[32];
                    sprintf(expect, "%d:%d-%d;", MOVE_DELEDGE, ea, eb);
                    CHECK(!strcmp(r, expect));
                    sfree(r);
                    CHECK(!(e->st->g.adj[ea] >> eb & 1));
                    CHECK(e->st->g.alive == keep->g.alive);
                }
                free_game(e->st);
                e->st = keep;
            }
            return;
        }
    }
}

static void scenario_misc(struct env *e)
{
    char *r;
    int x, y, v = -1, i;
    /* a press on nothing, with nothing selected, does nothing */
    for (i = 0; i < 200; i++) {
        double ux = rnd((int)e->lim), uy = rnd((int)e->lim);
        if (dist_to_nearest(e, ux, uy, -1) > 3 * HIT_RADIUS) {
            pix(e, ux, uy, &x, &y);
            r = ev(e, LEFT_BUTTON, x, y);
            CHECK(r == NULL);
            CHECK(e->ui->mode == UI_NONE);
            r = ev(e, LEFT_RELEASE, x, y);
            CHECK(r == NULL);
            break;
        }
    }
    /* a release or a drag with no press before it does nothing */
    r = ev(e, LEFT_RELEASE, 5, 5); CHECK(r == NULL);
    r = ev(e, LEFT_DRAG, 5, 5); CHECK(r == NULL);
    r = ev(e, RIGHT_DRAG, 5, 5); CHECK(r == NULL);
    r = ev(e, RIGHT_RELEASE, 5, 5); CHECK(r == NULL);
    /* the middle button, and keys, are not for us */
    r = interpret_move(e->st, e->ui, e->ds, 10, 10, MIDDLE_BUTTON); CHECK(r == MOVE_UNUSED);
    r = interpret_move(e->st, e->ui, e->ds, 10, 10, CURSOR_UP); CHECK(r == MOVE_UNUSED);
    r = interpret_move(e->st, e->ui, e->ds, 10, 10, 'a'); CHECK(r == MOVE_UNUSED);
    /* let go outside of the window: nothing happens, and the point stays where it was */
    for (v = 0; v < MAXB; v++) {
        if ((e->st->g.alive >> v & 1) && clear_hit(e, v)) {
            int vx, vy;
            game_state *keep = dup_game(e->st);
            pix_of_point(e, v, &vx, &vy);
            r = ev(e, LEFT_BUTTON, vx, vy); CHECK(r == NULL);
            r = ev(e, LEFT_DRAG, -30, vy); CHECK(r == NULL);
            r = ev(e, LEFT_RELEASE, -30, vy); CHECK(r == NULL);
            CHECK(e->st->g.alive == keep->g.alive && e->st->x[v] == keep->x[v] && e->st->y[v] == keep->y[v]);
            r = ev(e, LEFT_BUTTON, vx, vy); CHECK(r == NULL);
            r = ev(e, LEFT_DRAG, -1, -1); CHECK(r == NULL);       /* what the app sends when the finger leaves the screen */
            r = ev(e, LEFT_RELEASE, -1, -1); CHECK(r == NULL);
            CHECK(e->st->g.alive == keep->g.alive && e->st->x[v] == keep->x[v] && e->st->y[v] == keep->y[v]);
            free_game(keep);
            break;
        }
    }
}

/* ---- a lot of random gestures ---- */

static void check_state(const struct env *e)
{
    const game_state *s = e->st;
    int a, b, n = s->params.n_base;
    long hi = (long)(n + 2) * COORDUNIT;
    CHECK(POPCOUNT(s->g.alive) >= 0 && (s->g.alive >> n) == 0);
    for (a = 0; a < MAXB; a++) {
        CHECK(!(s->g.adj[a] >> a & 1));
        if (!(s->g.alive >> a & 1)) {
            CHECK(s->g.adj[a] == 0);
            continue;
        }
        CHECK(s->x[a] >= 0 && s->x[a] <= hi && s->y[a] >= 0 && s->y[a] <= hi);
        CHECK((s->g.adj[a] & ~s->g.alive) == 0);
        for (b = 0; b < MAXB; b++)
            CHECK(((s->g.adj[a] >> b) & 1) == ((s->g.adj[b] >> a) & 1));
    }
    CHECK(s->solved == bg_is_minor(&s->g, s->sh));
}

static int pick_point(const struct env *e)
{
    int v, tries;
    if (!e->st->g.alive) return -1;
    for (tries = 0; tries < 100; tries++) {
        v = rnd(MAXB);
        if (e->st->g.alive >> v & 1) return v;
    }
    return CTZ(e->st->g.alive);
}

static void jitter(const struct env *e, int *x, int *y, int amount)
{
    *x += rnd(2 * amount + 1) - amount;
    *y += rnd(2 * amount + 1) - amount;
}

static void random_gesture(struct env *e)
{
    int kind = rnd(100);
    int v = pick_point(e), w, x, y, tx, ty, i, n;
    char *r;
    int maxx = e->g.W + 40, maxy = e->g.H + 40;

    if (v < 0) return;
    pix_of_point(e, v, &x, &y);
    jitter(e, &x, &y, rnd(3) == 0 ? 6 : 1);
    if (kind < 25) {                       /* tap, perhaps twice */
        n = 1 + rnd(2);
        for (i = 0; i < n; i++) {
            if (i) { w = pick_point(e); pix_of_point(e, rnd(2) ? w : v, &x, &y); jitter(e, &x, &y, 2); }
            r = ev(e, LEFT_BUTTON, x, y); if (is_move(r)) sfree(r);
            r = ev(e, LEFT_RELEASE, x, y); if (is_move(r)) sfree(r);
        }
    } else if (kind < 55) {                /* a drag onto a point or somewhere */
        w = pick_point(e);
        if (rnd(3) == 0) { tx = rnd(maxx + 40) - 40; ty = rnd(maxy + 40) - 40; }
        else { pix_of_point(e, w, &tx, &ty); jitter(e, &tx, &ty, rnd(2) ? 3 : 9); }
        r = ev(e, LEFT_BUTTON, x, y); if (is_move(r)) sfree(r);
        n = 1 + rnd(5);
        for (i = 1; i <= n; i++) {
            r = ev(e, LEFT_DRAG, x + (tx - x) * i / n, y + (ty - y) * i / n); if (is_move(r)) sfree(r);
        }
        r = ev(e, LEFT_RELEASE, tx, ty); if (is_move(r)) sfree(r);
    } else if (kind < 70) {                /* delete a point */
        r = ev(e, RIGHT_BUTTON, x, y); if (is_move(r)) sfree(r);
        if (rnd(3) == 0) { r = ev(e, RIGHT_DRAG, x + rnd(41) - 20, y + rnd(41) - 20); if (is_move(r)) sfree(r); }
        r = ev(e, RIGHT_RELEASE, x + (rnd(4) == 0 ? rnd(41) - 20 : 0), y); if (is_move(r)) sfree(r);
    } else if (kind < 85) {                /* delete an edge, or something near it */
        int a = v, b = -1;
        mask_t m = e->st->g.adj[a];
        if (m) {
            int c = rnd(POPCOUNT(m));
            for (; c > 0; c--) m &= m - 1;
            b = CTZ(m);
            pix(e, (e->st->x[a] * 2.0 + e->st->x[b]) / 3.0 + 0.5 * (rnd(5) - 2), (e->st->y[a] * 2.0 + e->st->y[b]) / 3.0, &x, &y);
        }
        r = ev(e, RIGHT_BUTTON, x, y); if (is_move(r)) sfree(r);
        r = ev(e, RIGHT_RELEASE, x, y); if (is_move(r)) sfree(r);
    } else {                               /* anything at all */
        static const int bt[] = { LEFT_BUTTON, LEFT_DRAG, LEFT_RELEASE, RIGHT_BUTTON, RIGHT_DRAG, RIGHT_RELEASE,
                                  MIDDLE_BUTTON, MIDDLE_DRAG, MIDDLE_RELEASE, 'x', CURSOR_UP };
        n = 1 + rnd(6);
        for (i = 0; i < n; i++) {
            int b = bt[rnd((int)lenof(bt))];
            x = rnd(maxx + 80) - 40; y = rnd(maxy + 80) - 40;
            r = ev(e, b, x, y); if (is_move(r)) sfree(r);
        }
    }
    check_state(e);
}

int main(int argc, char **argv)
{
    int rounds = argc > 1 ? atoi(argv[1]) : 40;
    static const int sizes[] = { 8, 12, 24, 37, 64 };
    int r;
    long moves = 0, updates = 0;
    rs = random_new("mfui", 4);

    for (r = 0; r < rounds; r++) {
        int mode = rnd(2), nb = mode ? 19 + rnd(5) : 15 + rnd(9), nm = mode ? 5 + rnd(2) : 4 + rnd(3);
        int layout = rnd(2) ? LAYOUT_SIDE : LAYOUT_STACKED, ts = sizes[rnd((int)lenof(sizes))], i;
        char seed[32];
        struct env *e;
        sprintf(seed, "ui%d", r);
        e = env_new(mode, nb, nm, seed, layout, ts);
        scenario_tap_tap(e);
        scenario_drag_contract(e);
        scenario_drag_free(e);
        scenario_delete(e);
        scenario_misc(e);
        check_state(e);
        for (i = 0; i < 150 && !e->st->solved; i++)
            random_gesture(e);
        /* the same with the other layout */
        e->ui->layout = layout == LAYOUT_SIDE ? LAYOUT_STACKED : LAYOUT_SIDE;
        env_layout(e);
        for (i = 0; i < 100 && !e->st->solved; i++)
            random_gesture(e);
        moves += e->moves; updates += e->updates;
        env_free(e);
    }
    printf("mfui: %d games, %ld moves made from gestures, %ld ui updates\n", rounds, moves, updates);
    printf(failures ? "%d FAILURES\n" : "all passed\n", failures);
    random_free(rs);
    return failures != 0;
}
