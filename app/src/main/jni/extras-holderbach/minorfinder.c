/*
 * minorfinder.c: "Minor Finder", a puzzle about graph minors.
 *
 * The puzzle shows a small graph (the minor, drawn with red points) and a big
 * graph (drawn with blue points).  You win when the big graph has been turned
 * into the small one, using the three operations that define a graph minor:
 *
 *  - contract an edge (its two end points are merged into one, which keeps
 *    the edges of both),
 *  - delete an edge,
 *  - delete a point together with all the edges that end in it.
 *
 * Points can also be moved around, to get a clear picture of the graph.
 *
 * The game is by Samuel Holderbach ("Minorensucher", from his bachelor
 * thesis, https://github.com/elsamuray7/Minorfinder); the generator of the
 * games below is his.  What is new in this version:
 *
 *  - the position of a game is kept as bit masks (an adjacency matrix),
 *    which makes every move cheap, and makes it simple to check whether the
 *    big graph is the minor;
 *  - an exact solver replaces the brute force search of the first version.
 *    It looks for a division of the points into connected "branch sets", one
 *    for each point of the minor, such that two sets touch if the
 *    corresponding points of the minor are joined; it uses constraint
 *    propagation and prunes symmetric alternatives.  It serves Solve and
 *    "Show next step", and it says so if no solution can be found from the
 *    position that is on the screen;
 *  - the generator also checks that the game it has made can be solved;
 *  - the game description and the moves are checked completely;
 *  - touch screens: the points are easy to hit, a tap and then a tap on a
 *    neighbouring point contracts the edge between them, a deletion is
 *    carried out when the finger or the mouse is let go on the thing that is
 *    going to be deleted, and the two graphs can be shown one above the other
 *    (a preference);
 *  - a status line tells how many points and edges the big graph has, and
 *    how many it needs to get down to.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdint.h>

#include "puzzles.h"
#include "tree234.h"

typedef uint32_t mask_t;               /* a set of at most 32 points */

#define POPCOUNT(x) __builtin_popcount(x)
#define CTZ(x) __builtin_ctz(x)

enum {
    MAXB = 23,                         /* most points of the big graph */
    MAXM = 6                           /* most points of the minor */
};

/*
 * The colours.  The first one is skipped when the night colours are made, as
 * in the other games.
 */
enum {
    COL_SYSBACKGROUND,
    COL_BACKGROUND,
    COL_OUTLINE,
    COL_BASEPOINT,
    COL_MINORPOINT,
    COL_DRAGPOINT,
    COL_DELPOINT,
    COL_HIDEPOINT,
    COL_POINTOUTLINE,
    COL_EDGE,
    COL_DELEDGE,
    COL_HIDEEDGE,
    COL_FLASH,
    COL_FLASH2,
    NCOLOURS
};

/* ----------------------------------------------------------------------
 * The parameters.
 */

enum {
    MODE_DEFAULT,                      /* the minor is a random graph */
    MODE_SPECIAL                       /* the minor is the K5 or the K3,3 */
};

struct game_params {
    int mode;
    int n_base;                        /* points of the big graph */
    int n_min;                         /* points of the minor */
};

static const struct game_params default_presets[] = {
    { MODE_DEFAULT, 15, 4 },
    { MODE_DEFAULT, 19, 5 },
    { MODE_DEFAULT, 23, 6 }
};

static const struct game_params special_presets[] = {
    { MODE_SPECIAL, 19, 5 },
    { MODE_SPECIAL, 23, 6 }
};

#define MIN_BASE_DEFAULT 15
#define MAX_BASE 23
#define MIN_MINOR_DEFAULT 4
#define MAX_MINOR 6
#define MIN_BASE_SPECIAL 19
#define MIN_MINOR_SPECIAL 5

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);
    *ret = default_presets[0];
    return ret;
}

static struct preset_menu *game_preset_menu(void)
{
    char buf[80];
    int i;
    struct preset_menu *ret = preset_menu_new();

    for (i = 0; i < (int)lenof(default_presets); i++) {
        game_params *params = snew(game_params);
        *params = default_presets[i];
        sprintf(buf, "%d points, minor with %d", params->n_base, params->n_min);
        preset_menu_add_preset(ret, dupstr(buf), params);
    }
    for (i = 0; i < (int)lenof(special_presets); i++) {
        game_params *params = snew(game_params);
        *params = special_presets[i];
        sprintf(buf, "%d points, minor K%s", params->n_base,
                params->n_min == 5 ? "5" : "3,3");
        preset_menu_add_preset(ret, dupstr(buf), params);
    }
    return ret;
}

static void free_params(game_params *params)
{
    sfree(params);
}

static game_params *dup_params(const game_params *params)
{
    game_params *ret = snew(game_params);
    *ret = *params;                    /* structure copy */
    return ret;
}

static void decode_params(game_params *params, char const *string)
{
    int mode, nb, nm, n = 0;

    /* mode-basexminor, for example 0-15x4; anything else gives the default */
    if (sscanf(string, "%d-%dx%d%n", &mode, &nb, &nm, &n) == 3) {
        params->mode = mode;
        params->n_base = nb;
        params->n_min = nm;
    } else {
        *params = default_presets[0];
    }
}

static char *encode_params(const game_params *params, bool full)
{
    char buf[80];
    sprintf(buf, "%d-%dx%d", params->mode, params->n_base, params->n_min);
    return dupstr(buf);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret = snewn(4, config_item);
    char buf[80];

    ret[0].name = "Minor";
    ret[0].type = C_CHOICES;
    ret[0].u.choices.choicenames = ":Random graph:K5 or K3,3";
    ret[0].u.choices.selected = params->mode;

    ret[1].name = "Points of the big graph";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->n_base);
    ret[1].u.string.sval = dupstr(buf);

    ret[2].name = "Points of the minor";
    ret[2].type = C_STRING;
    sprintf(buf, "%d", params->n_min);
    ret[2].u.string.sval = dupstr(buf);

    ret[3].name = NULL;
    ret[3].type = C_END;
    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);
    ret->mode = cfg[0].u.choices.selected;
    ret->n_base = atoi(cfg[1].u.string.sval);
    ret->n_min = atoi(cfg[2].u.string.sval);
    return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
    switch (params->mode) {
      case MODE_DEFAULT:
        if (params->n_base < MIN_BASE_DEFAULT || params->n_base > MAX_BASE)
            return "The big graph must have between 15 and 23 points";
        if (params->n_min < MIN_MINOR_DEFAULT || params->n_min > MAX_MINOR)
            return "The minor must have between 4 and 6 points";
        break;
      case MODE_SPECIAL:
        if (params->n_base < MIN_BASE_SPECIAL || params->n_base > MAX_BASE)
            return "With K5 or K3,3, the big graph must have between 19 and 23 points";
        if (params->n_min < MIN_MINOR_SPECIAL || params->n_min > MAX_MINOR)
            return "K5 has 5 points and K3,3 has 6: the minor must have 5 or 6 points";
        break;
      default:
        return "Unknown kind of minor";
    }
    return NULL;
}

/* ----------------------------------------------------------------------
 * The graphs and the state of the game.
 *
 * Points are numbered, and the points of a graph that are still there are a
 * bit mask; the edges are an adjacency matrix of bit masks.  The coordinates
 * of the points are whole numbers in units of which COORDUNIT make a "cell",
 * and a graph of n points lives on a board of n + 2 cells by n + 2 cells,
 * with a margin of one cell.
 */

#define POINTRADIUS 5
#define COORDMARGIN 1
#define COORDLIMIT(n) ((n) + (2 * COORDMARGIN))
#define COORDUNIT 16

struct bgraph {
    mask_t alive;                      /* the points that are there */
    mask_t adj[MAXB];                  /* the edges (symmetric, no loops) */
};
typedef struct bgraph bgraph;

/* What does not change in a game, and is shared by all of its states. */
struct shared {
    int refcount;
    int n_base, n_min;
    long mx[MAXM], my[MAXM];           /* the points of the minor */
    mask_t madj[MAXM];                 /* the edges of the minor */
    int nmedges;
};

struct game_state {
    game_params params;
    struct shared *sh;
    bgraph g;                          /* the big graph as it is now */
    long x[MAXB], y[MAXB];             /* where its points are */
    bool solved, cheated;
};

static int bg_edges(const bgraph *g)
{
    int i, n = 0;
    for (i = 0; i < MAXB; i++)
        if (g->alive >> i & 1)
            n += POPCOUNT(g->adj[i]);
    return n / 2;
}

static void bg_delete_edge(bgraph *g, int a, int b)
{
    g->adj[a] &= ~((mask_t)1 << b);
    g->adj[b] &= ~((mask_t)1 << a);
}

static void bg_delete_vertex(bgraph *g, int v)
{
    mask_t m;
    for (m = g->adj[v]; m; m &= m - 1)
        g->adj[CTZ(m)] &= ~((mask_t)1 << v);
    g->adj[v] = 0;
    g->alive &= ~((mask_t)1 << v);
}

/* Merge rec into dom; the edges of rec go to dom (but no loops, no doubles). */
static void bg_contract(bgraph *g, int dom, int rec)
{
    mask_t m, nb = g->adj[rec] & ~((mask_t)1 << dom);
    bg_delete_vertex(g, rec);
    g->adj[dom] |= nb;
    for (m = nb; m; m &= m - 1)
        g->adj[CTZ(m)] |= (mask_t)1 << dom;
}

/*
 * Is the graph the minor?  (Is there a bijection of the points that keeps
 * the edges and the non-edges?)
 */
static bool iso_search(const bgraph *g, const struct shared *sh, const int *vs,
                       int j, int *img, mask_t used)
{
    int c, i, k = sh->n_min;

    if (j == k)
        return true;
    for (c = 0; c < k; c++) {
        bool ok = true;
        if (used >> c & 1)
            continue;
        if (POPCOUNT(g->adj[vs[c]]) != POPCOUNT(sh->madj[j]))
            continue;
        for (i = 0; i < j; i++) {
            bool ge = g->adj[vs[c]] >> vs[img[i]] & 1;
            bool me = sh->madj[j] >> i & 1;
            if (ge != me) {
                ok = false;
                break;
            }
        }
        if (!ok)
            continue;
        img[j] = c;
        if (iso_search(g, sh, vs, j + 1, img, used | ((mask_t)1 << c)))
            return true;
    }
    return false;
}

static bool bg_is_minor(const bgraph *g, const struct shared *sh)
{
    int vs[MAXB], img[MAXM], k = 0;
    mask_t m;

    if (POPCOUNT(g->alive) != sh->n_min)
        return false;
    for (m = g->alive; m; m &= m - 1)
        vs[k++] = CTZ(m);
    return iso_search(g, sh, vs, 0, img, 0);
}

/* ---- the description of a game ----
 *
 * The description has five parts, each of which ends in a semicolon:
 *
 *   the points of the minor:   index-degree-x-y,index-degree-x-y,...;
 *   the edges of the minor:    a-b,a-b,...;
 *   the points of the big graph, and then its edges, the same way;
 *   the sizes of the pieces that the generator made the big graph of, one
 *   piece for each point of the minor:   index-size,index-size,...;
 *
 * (the generator makes a piece for each point of the minor, joins the pieces
 * as the minor says, adds some points, and then some more edges).
 */

static bool scan_num(const char **p, long maxv, long *out)
{
    const char *q = *p;
    long v = 0;
    int digits = 0;

    while (isdigit((unsigned char)*q)) {
        if (++digits > 9)
            return false;
        v = v * 10 + (*q++ - '0');
    }
    if (!digits || v > maxv)
        return false;
    *p = q;
    *out = v;
    return true;
}

static bool scan_char(const char **p, char c)
{
    if (**p != c)
        return false;
    (*p)++;
    return true;
}

/* the separator after an item: ',' if more items follow, ';' after the last */
static bool scan_sep(const char **p, bool last)
{
    return scan_char(p, last ? ';' : ',');
}

static const char *scan_points(const char **p, int n, long lo, long hi,
                               long *xs, long *ys, int *degs)
{
    int i;
    bool seen[MAXB] = { false };

    for (i = 0; i < n; i++) {
        long idx, deg, x, y;
        if (!scan_num(p, n - 1, &idx))
            return "Point index out of range in game description";
        if (seen[idx])
            return "Point index repeated in game description";
        seen[idx] = true;
        if (!scan_char(p, '-') || !scan_num(p, n - 1, &deg))
            return "Bad degree of a point in game description";
        if (!scan_char(p, '-') || !scan_num(p, hi, &x) || x < lo)
            return "X-coordinate out of range in game description";
        if (!scan_char(p, '-') || !scan_num(p, hi, &y) || y < lo)
            return "Y-coordinate out of range in game description";
        if (!scan_sep(p, i == n - 1))
            return "Expected ',' or ';' after a point in game description";
        xs[idx] = x;
        ys[idx] = y;
        degs[idx] = deg;
    }
    return NULL;
}

static const char *scan_edges(const char **p, int n, mask_t *adj, int *nedges)
{
    *nedges = 0;
    for (;;) {
        long a, b;
        if (!scan_num(p, n - 1, &a))
            return "Edge end point out of range in game description";
        if (!scan_char(p, '-') || !scan_num(p, n - 1, &b))
            return "Edge end point out of range in game description";
        if (a == b)
            return "An edge from a point to itself in game description";
        if (adj[a] >> b & 1)
            return "Edge repeated in game description";
        adj[a] |= (mask_t)1 << b;
        adj[b] |= (mask_t)1 << a;
        (*nedges)++;
        if (scan_char(p, ';'))
            break;
        if (!scan_char(p, ','))
            return "Expected ',' or ';' after an edge in game description";
    }
    return NULL;
}

/*
 * Read a description.  On success, *out is a new state (unless out is NULL)
 * and subsizes (unless NULL) the sizes of the pieces.
 */
static const char *parse_desc(const game_params *params, const char *desc,
                              game_state **out, int *subsizes)
{
    const char *p = desc;
    const char *err;
    int nb = params->n_base, nm = params->n_min, i;
    long lim = COORDLIMIT(nb) * COORDUNIT - COORDMARGIN * COORDUNIT;
    long mar = COORDMARGIN * COORDUNIT;
    long mx[MAXM], my[MAXM], bx[MAXB], by[MAXB];
    int mdeg[MAXM], bdeg[MAXB], mnedges, bnedges, sizes[MAXM], total = 0;
    mask_t madj[MAXM] = { 0 }, badj[MAXB] = { 0 };
    bool seen[MAXM] = { false };

    if (nb < 1 || nb > MAXB || nm < 1 || nm > MAXM || nm > nb)
        return "Invalid parameters";

    if ((err = scan_points(&p, nm, mar, lim, mx, my, mdeg)) != NULL)
        return err;
    if ((err = scan_edges(&p, nm, madj, &mnedges)) != NULL)
        return err;
    if ((err = scan_points(&p, nb, mar, lim, bx, by, bdeg)) != NULL)
        return err;
    if ((err = scan_edges(&p, nb, badj, &bnedges)) != NULL)
        return err;
    for (i = 0; i < nm; i++) {
        long idx, size;
        if (!scan_num(&p, nm - 1, &idx))
            return "Subgraph index out of range in game description";
        if (seen[idx])
            return "Subgraph index repeated in game description";
        seen[idx] = true;
        if (!scan_char(&p, '-') || !scan_num(&p, nb, &size))
            return "Subgraph size out of range in game description";
        if (!scan_sep(&p, i == nm - 1))
            return "Expected ',' or ';' after a subgraph size in game description";
        sizes[idx] = size;
        total += size;
    }
    if (*p)
        return "Unexpected text at the end of the game description";
    if (total > nb)
        return "The subgraphs are bigger than the graph in game description";
    for (i = 0; i < nm; i++)
        if (mdeg[i] != POPCOUNT(madj[i]))
            return "Degree of a point of the minor does not match its edges";
    for (i = 0; i < nb; i++)
        if (bdeg[i] != POPCOUNT(badj[i]))
            return "Degree of a point does not match its edges";

    if (subsizes)
        for (i = 0; i < nm; i++)
            subsizes[i] = sizes[i];
    if (out) {
        game_state *s = snew(game_state);
        struct shared *sh = snew(struct shared);
        s->params = *params;
        s->sh = sh;
        sh->refcount = 1;
        sh->n_base = nb;
        sh->n_min = nm;
        sh->nmedges = mnedges;
        for (i = 0; i < MAXM; i++) {
            sh->mx[i] = i < nm ? mx[i] : 0;
            sh->my[i] = i < nm ? my[i] : 0;
            sh->madj[i] = i < nm ? madj[i] : 0;
        }
        memset(&s->g, 0, sizeof(s->g));
        memset(s->x, 0, sizeof(s->x));
        memset(s->y, 0, sizeof(s->y));
        s->g.alive = nb == 32 ? ~(mask_t)0 : ((mask_t)1 << nb) - 1;
        for (i = 0; i < nb; i++) {
            s->g.adj[i] = badj[i];
            s->x[i] = bx[i];
            s->y[i] = by[i];
        }
        s->solved = false;
        s->cheated = false;
        *out = s;
    }
    return NULL;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    return parse_desc(params, desc, NULL, NULL);
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    game_state *s = NULL;
    const char *err = parse_desc(params, desc, &s, NULL);
    assert(!err);
    (void)err;
    return s;
}

static game_state *dup_game(const game_state *state)
{
    game_state *ret = snew(game_state);
    *ret = *state;
    ret->sh->refcount++;
    return ret;
}

static void free_game(game_state *state)
{
    if (--state->sh->refcount <= 0)
        sfree(state->sh);
    sfree(state);
}

/* ----------------------------------------------------------------------
 * The solver.
 *
 * H is a minor of the graph G if the points of G can be put in disjoint
 * "branch sets", one for each point of H, such that every branch set is
 * connected and two of the sets have an edge between them whenever the
 * corresponding points of H are joined.  (Contract every set to a point,
 * delete the points that are in no set and the edges that H does not have.)
 *
 * We look for such sets.  Every point gets a label, a point of H or "unused".
 * A point of a connected piece of G with a set in it can be given to a
 * neighbouring set, so only whole pieces of G are left out, and the search
 * labels every other point.  For each label, the set of the points that may
 * still get it is kept as a bit mask, and the masks are narrowed by
 *
 *  - connection: the points that have to get a label must be joined, by
 *    points that may get it;
 *  - the edges of H: the sets of two joined points must be able to touch;
 *  - a piece of G is used or it is not.
 *
 * Then the search takes a point and tries its labels, and it only tries one
 * of the labels that are equivalent (the automorphisms of H that leave the
 * labels that are in use alone).
 */

static mask_t bg_nbrs(const mask_t *adj, mask_t set)
{
    mask_t r = 0;
    for (; set; set &= set - 1)
        r |= adj[CTZ(set)];
    return r;
}

/* the points of "within" that are joined to the point "seed" (one bit) */
static mask_t bg_comp(const mask_t *adj, mask_t within, mask_t seed)
{
    mask_t c = seed, front = seed;
    while (front) {
        mask_t nb = bg_nbrs(adj, front) & within & ~c;
        c |= nb;
        front = nb;
    }
    return c;
}

enum { MAXLAB = MAXM + 1 };            /* the points of H, and "unused" */

struct msolver {
    int k;                             /* points of the minor */
    bgraph g;
    mask_t hadj[MAXM];
    int nhe, hea[MAXM * MAXM], heb[MAXM * MAXM];   /* the edges of H */
    bool hconnected;
    int hdeg[MAXM];
    unsigned char rep[1 << MAXM][MAXM];  /* the first label that is like the label, given those that are in use */
    long nodes, limit;                 /* the nodes that were tried, and the most that are */
    long run_end;                      /* the node at which this try is given up */
    bool limit_hit, cut;               /* ran into the limit; ran into the end of this try */
    unsigned rng;                      /* nonzero: break ties at random */
    int lab[MAXB];                     /* the result: a label, or -1 */
};

static unsigned ms_rand(struct msolver *S)
{
    unsigned x = S->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return S->rng = x;
}

static void ms_autos(struct msolver *S, int *perm, int pos, unsigned used)
{
    int k = S->k, c, i, l;

    if (pos == k) {
        unsigned fixed = 0, m;
        for (i = 0; i < k; i++)
            if (perm[i] == i)
                fixed |= 1u << i;
        for (m = 0; m < (1u << k); m++) {
            if (m & ~fixed)
                continue;
            for (l = 0; l < k; l++)
                if (perm[l] < S->rep[m][l])
                    S->rep[m][l] = perm[l];
        }
        return;
    }
    for (c = 0; c < k; c++) {
        bool ok = true;
        if ((used >> c & 1) || S->hdeg[c] != S->hdeg[pos])
            continue;
        for (i = 0; i < pos && ok; i++)
            if ((S->hadj[pos] >> i & 1) != (S->hadj[c] >> perm[i] & 1))
                ok = false;
        if (!ok)
            continue;
        perm[pos] = c;
        ms_autos(S, perm, pos + 1, used | (1u << c));
    }
}

static void ms_init(struct msolver *S, const bgraph *g, const mask_t *hadj,
                    int k, long limit)
{
    int i, j, l, perm[MAXM];
    mask_t seen, m;

    memset(S, 0, sizeof(*S));
    S->k = k;
    S->g = *g;
    for (i = 0; i < k; i++) {
        S->hadj[i] = hadj[i];
        S->hdeg[i] = POPCOUNT(hadj[i]);
        for (j = i + 1; j < k; j++)
            if (hadj[i] >> j & 1) {
                S->hea[S->nhe] = i;
                S->heb[S->nhe++] = j;
            }
    }
    seen = k ? 1 : 0;
    for (;;) {
        mask_t nb = 0;
        for (m = seen; m; m &= m - 1)
            nb |= hadj[CTZ(m)];
        if (!(nb & ~seen))
            break;
        seen |= nb;
    }
    S->hconnected = seen == (k >= 32 ? ~(mask_t)0 : ((mask_t)1 << k) - 1);
    for (i = 0; i < (1 << k); i++)
        for (l = 0; l < k; l++)
            S->rep[i][l] = l;
    ms_autos(S, perm, 0, 0);
    S->limit = limit;
}

/*
 * Narrow the masks (M[l] for the label l < k, M[k] for "unused") as far as
 * the rules above go.  Returns false if some point has no label left, or a
 * label no point, or the sets cannot be connected, or two cannot touch.
 */
static bool ms_propagate(const struct msolver *S, mask_t *M)
{
    const mask_t *adj = S->g.adj;
    int k = S->k, l, j, e;
    bool changed;

    do {
        mask_t cover = 0, must_use, unused_for_sure, bad;

        changed = false;
        for (l = 0; l <= k; l++)
            cover |= M[l];
        if (S->g.alive & ~cover)
            return false;

        /* a piece of the graph is used as a whole, or not at all */
        must_use = S->g.alive & ~M[k];
        bad = bg_nbrs(adj, must_use) & M[k];
        if (bad) {
            M[k] &= ~bad;
            changed = true;
        }
        unused_for_sure = M[k];
        for (l = 0; l < k; l++)
            unused_for_sure &= ~M[l];
        bad = bg_nbrs(adj, unused_for_sure);
        for (l = 0; l < k; l++)
            if (M[l] & bad) {
                M[l] &= ~bad;
                changed = true;
            }

        /* the points that have to get a label l must be joined */
        for (l = 0; l < k; l++) {
            mask_t others = 0, forced;
            if (!M[l])
                return false;
            for (j = 0; j <= k; j++)
                if (j != l)
                    others |= M[j];
            forced = M[l] & ~others;
            if (forced) {
                mask_t comp = bg_comp(adj, M[l], forced & -forced);
                if (forced & ~comp)
                    return false;
                if (comp != M[l]) {
                    M[l] = comp;
                    changed = true;
                }
            }
        }

        /* the sets of two points that are joined in H have to touch */
        for (e = 0; e < S->nhe; e++)
            if (!(bg_nbrs(adj, M[S->hea[e]]) & M[S->heb[e]]))
                return false;
    } while (changed);
    return true;
}

/* How the labels are tried on a point: the number is the order. */
static bool ms_search(struct msolver *S, const mask_t *M)
{
    const mask_t *adj = S->g.adj;
    int k = S->k, c, l, j, v, nc, i, bests, score;
    mask_t ge[MAXLAB + 3], open, cand, det, used = 0, forced[MAXM], m, M2[MAXLAB];
    int cl[MAXLAB], cs[MAXLAB];

    if (S->nodes >= S->limit) {
        S->limit_hit = true;
        return false;
    }
    if (S->nodes >= S->run_end) {
        S->cut = true;
        return false;
    }
    S->nodes++;

    /* ge[c]: the points that are in at least c of the masks */
    for (c = 0; c < MAXLAB + 3; c++)
        ge[c] = 0;
    for (l = 0; l <= k; l++) {
        for (c = k + 1; c >= 2; c--)
            ge[c] |= ge[c - 1] & M[l];
        ge[1] |= M[l];
    }
    open = ge[2];
    if (!open) {
        for (v = 0; v < MAXB; v++)
            S->lab[v] = -1;
        for (l = 0; l < k; l++)
            for (m = M[l]; m; m &= m - 1)
                S->lab[CTZ(m)] = l;
        return true;
    }

    for (l = 0; l < k; l++) {
        mask_t others = 0;
        for (j = 0; j <= k; j++)
            if (j != l)
                others |= M[j];
        forced[l] = M[l] & ~others;
        if (forced[l])
            used |= 1u << l;
    }

    /* the open point with the fewest labels, and of those the one with the
     * most points that are settled next to it */
    for (c = 2; c <= k + 1; c++)
        if (ge[c] & ~ge[c + 1])
            break;
    cand = ge[c] & ~ge[c + 1];
    det = S->g.alive & ~open;
    v = -1;
    bests = -1;
    for (m = cand; m; m &= m - 1) {
        int u = CTZ(m);
        score = POPCOUNT(adj[u] & det) * 32 + POPCOUNT(adj[u]);
        if (S->rng)
            score = score * 8 + (int)(ms_rand(S) & 7);
        if (score > bests) {
            bests = score;
            v = u;
        }
    }

    /* the labels to try */
    nc = 0;
    for (l = 0; l < k; l++) {
        if (!(M[l] >> v & 1))
            continue;
        if (!(used >> l & 1) && S->rep[used][l] != l)
            continue;
        cl[nc] = l;
        if (used >> l & 1) {
            int nbc = POPCOUNT(adj[v] & forced[l]);
            cs[nc] = nbc ? 1000 + nbc : 1;
        } else {
            cs[nc] = 500;
        }
        nc++;
    }
    if (M[k] >> v & 1) {
        cl[nc] = k;
        cs[nc] = 0;
        nc++;
    }
    for (i = 1; i < nc; i++) {         /* insertion sort, best first */
        int tl = cl[i], ts = cs[i];
        for (j = i - 1; j >= 0 && cs[j] < ts; j--) {
            cl[j + 1] = cl[j];
            cs[j + 1] = cs[j];
        }
        cl[j + 1] = tl;
        cs[j + 1] = ts;
    }

    for (i = 0; i < nc; i++) {
        for (j = 0; j <= k; j++)
            M2[j] = j == cl[i] ? M[j] : M[j] & ~((mask_t)1 << v);
        if (ms_propagate(S, M2) && ms_search(S, M2))
            return true;
        if (S->limit_hit || S->cut)
            return false;
    }
    return false;
}

/* One try.  1: the labels are in S->lab;  0: there is no way;  -1: gave up
 * (S->cut or S->limit_hit tell why) */
static int ms_solve_once(struct msolver *S)
{
    int k = S->k, i, nc = 0, hedges = S->nhe;
    mask_t M[MAXLAB], rest, comps[MAXB];
    int csize[MAXB];

    if (POPCOUNT(S->g.alive) < k || bg_edges(&S->g) < hedges)
        return 0;

    if (S->hconnected) {
        /* all of the minor is in one piece of the graph: try the pieces,
         * the biggest first */
        for (rest = S->g.alive; rest; ) {
            mask_t comp = bg_comp(S->g.adj, S->g.alive, rest & -rest), m;
            int edges = 0;
            rest &= ~comp;
            for (m = comp; m; m &= m - 1)
                edges += POPCOUNT(S->g.adj[CTZ(m)]);
            if (POPCOUNT(comp) >= k && edges / 2 >= hedges) {
                comps[nc] = comp;
                csize[nc++] = POPCOUNT(comp);
            }
        }
        for (i = 1; i < nc; i++) {
            mask_t tc = comps[i];
            int ts = csize[i], j;
            for (j = i - 1; j >= 0 && csize[j] < ts; j--) {
                comps[j + 1] = comps[j];
                csize[j + 1] = csize[j];
            }
            comps[j + 1] = tc;
            csize[j + 1] = ts;
        }
        for (i = 0; i < nc; i++) {
            int l;
            for (l = 0; l < k; l++)
                M[l] = comps[i];
            M[k] = S->g.alive & ~comps[i];
            if (ms_propagate(S, M) && ms_search(S, M))
                return 1;
            if (S->limit_hit || S->cut)
                return -1;
        }
        return 0;
    } else {
        int l;
        for (l = 0; l <= k; l++)
            M[l] = S->g.alive;
        if (ms_propagate(S, M) && ms_search(S, M))
            return 1;
        return S->limit_hit || S->cut ? -1 : 0;
    }
}

/*
 * The search can get lost in a bad corner for a long time, so a try is given
 * up after a number of nodes, and the search is started again with the ties
 * broken in another way and twice as many nodes to use (so that a search that
 * has to go through everything still gets there).  Returns 1: the labels are
 * in S->lab;  0: there is no way;  -1: gave up, S->limit nodes were not enough.
 */
enum { FIRST_TRY = 20000 };

static int ms_solve(struct msolver *S)
{
    long budget = FIRST_TRY;
    int round = 0;

    for (;;) {
        int rc;
        S->cut = false;
        S->rng = round ? 2463534242u + 0x9e3779b9u * (unsigned)round : 0;
        S->run_end = S->limit - S->nodes < budget ? S->limit : S->nodes + budget;
        rc = ms_solve_once(S);
        if (rc >= 0)
            return rc;
        if (S->limit_hit)
            return -1;
        budget *= 2;
        round++;
    }
}

/* Is lab[] (a label for each point of g, or -1) a way to the minor? */
static bool model_valid(const bgraph *g, const mask_t *hadj, int k, const int *lab)
{
    mask_t part[MAXM] = { 0 };
    int v, i, j;

    for (v = 0; v < MAXB; v++) {
        if (!(g->alive >> v & 1))
            continue;
        if (lab[v] >= k)
            return false;
        if (lab[v] >= 0)
            part[lab[v]] |= (mask_t)1 << v;
    }
    for (i = 0; i < k; i++) {
        if (!part[i] || bg_comp(g->adj, part[i], part[i] & -part[i]) != part[i])
            return false;
        for (j = i + 1; j < k; j++)
            if ((hadj[i] >> j & 1) && !(bg_nbrs(g->adj, part[i]) & part[j]))
                return false;
    }
    return true;
}

/*
 * From a model to moves.  The unused points are deleted; the sets are
 * contracted to a point each (the leaves of a spanning tree first, so that
 * the edge that is contracted is always there), and then the edges that the
 * minor does not have are deleted.
 */

enum { MAXSTEPS = 96 };

struct plan {
    int n;
    char kind[MAXSTEPS];               /* 'v' delete a point, 'c' contract, 'e' delete an edge */
    int a[MAXSTEPS], b[MAXSTEPS];
    int root[MAXM];                    /* the point that is left of each set */
};

/* Give up the points that no set needs. */
static void prune_model(const bgraph *g, const mask_t *hadj, int k, int *lab)
{
    bool progress;
    do {
        int v, j;
        progress = false;
        for (v = MAXB - 1; v >= 0; v--) {
            mask_t part = 0, rest;
            int l = lab[v], i;
            bool ok = true;
            if (!(g->alive >> v & 1) || l < 0)
                continue;
            for (i = 0; i < MAXB; i++)
                if ((g->alive >> i & 1) && lab[i] == l)
                    part |= (mask_t)1 << i;
            rest = part & ~((mask_t)1 << v);
            if (!rest || bg_comp(g->adj, rest, rest & -rest) != rest)
                continue;
            for (j = 0; j < k && ok; j++) {
                mask_t pj = 0;
                if (j == l || !(hadj[l] >> j & 1))
                    continue;
                for (i = 0; i < MAXB; i++)
                    if ((g->alive >> i & 1) && lab[i] == j)
                        pj |= (mask_t)1 << i;
                if (!(bg_nbrs(g->adj, rest) & pj))
                    ok = false;
            }
            if (ok) {
                lab[v] = -1;
                progress = true;
            }
        }
    } while (progress);
}

static bool make_plan(const bgraph *g, const mask_t *hadj, int k, int *lab,
                      struct plan *pl)
{
    bgraph sim = *g;
    int v, l, i, j;

    prune_model(g, hadj, k, lab);
    pl->n = 0;
    for (v = 0; v < MAXB; v++)
        if ((g->alive >> v & 1) && lab[v] < 0) {
            pl->kind[pl->n] = 'v';
            pl->a[pl->n] = v;
            pl->b[pl->n++] = -1;
            bg_delete_vertex(&sim, v);
        }
    for (l = 0; l < k; l++) {
        mask_t part = 0, seen;
        int order[MAXB], parent[MAXB], no = 0, root = -1, best = -1;
        for (v = 0; v < MAXB; v++)
            if ((g->alive >> v & 1) && lab[v] == l)
                part |= (mask_t)1 << v;
        if (!part)
            return false;
        for (v = 0; v < MAXB; v++)           /* the root: the point with most edges */
            if ((part >> v & 1) && POPCOUNT(g->adj[v]) > best) {
                best = POPCOUNT(g->adj[v]);
                root = v;
            }
        pl->root[l] = root;
        order[no] = root;
        parent[no++] = -1;
        seen = (mask_t)1 << root;
        for (i = 0; i < no; i++) {
            mask_t nb = g->adj[order[i]] & part & ~seen;
            for (; nb; nb &= nb - 1) {
                order[no] = CTZ(nb);
                parent[no++] = order[i];
            }
            seen |= g->adj[order[i]] & part;
        }
        if (seen != part || no != POPCOUNT(part))
            return false;
        for (i = no - 1; i >= 1; i--) {
            if (pl->n >= MAXSTEPS)
                return false;
            pl->kind[pl->n] = 'c';
            pl->a[pl->n] = parent[i];     /* stays */
            pl->b[pl->n++] = order[i];    /* goes into it */
            bg_contract(&sim, parent[i], order[i]);
        }
    }
    for (i = 0; i < k; i++)
        for (j = i + 1; j < k; j++) {
            bool has = sim.adj[pl->root[i]] >> pl->root[j] & 1;
            if (has && !(hadj[i] >> j & 1)) {
                if (pl->n >= MAXSTEPS)
                    return false;
                pl->kind[pl->n] = 'e';
                pl->a[pl->n] = pl->root[i];
                pl->b[pl->n++] = pl->root[j];
                bg_delete_edge(&sim, pl->root[i], pl->root[j]);
            }
        }
    /* did that work? */
    {
        mask_t roots = 0;
        for (i = 0; i < k; i++)
            roots |= (mask_t)1 << pl->root[i];
        if (sim.alive != roots)
            return false;
        for (i = 0; i < k; i++)
            for (j = i + 1; j < k; j++)
                if ((sim.adj[pl->root[i]] >> pl->root[j] & 1) != (hadj[i] >> j & 1))
                    return false;
    }
    return true;
}

/* ----------------------------------------------------------------------
 * The generator, by Samuel Holderbach.
 *
 * It makes a piece of the big graph for each point of the minor (two points
 * or more, joined in a way that does not matter), joins two pieces by an edge
 * where the minor has an edge, adds the points that are left over (up to
 * three edges each), and then more edges to confuse the player.  The points
 * are put at places such that no edge goes through a point.  The minor is
 * drawn as a circle (or the K3,3 in two rows) in the same way.
 *
 * Only some tidying up is new here: no debugging code, no timing code, no
 * (never used) description of a solution, and where the points that are left
 * over could not be put anywhere the board is searched for a place.
 */

typedef unsigned long ulong;

typedef struct point {
    long x, y;
    long d;                            /* always 1 */
} point;

typedef struct vertex {
    int idx;
    int deg;
} vertex;

typedef struct edge {
    int src, tgt;
} edge;

static ulong squarert(ulong n) {
    ulong d, a, b, di;

    d = n;
    a = 0;
    b = 1L << 30;		       /* largest available power of 4 */
    do {
        a >>= 1;
        di = 2*a + b;
        if (di <= d) {
            d -= di;
            a += b;
        }
        b >>= 2;
    } while (b);

    return a;
}

static void addedge(tree234 *edges, int s, int t)
{
    edge *e = snew(edge);


    e->src = min(s, t);
    e->tgt = max(s, t);

    add234(edges, e);
}

static bool isedge(tree234 *edges, int s, int t)
{
    edge e;


    e.src = min(s, t);
    e.tgt = max(s, t);

    return find234(edges, &e, NULL) != NULL;
}

static int edgecmpC(const void *av, const void *bv)
{
    const edge *a = (const edge *)av;
    const edge *b = (const edge *)bv;

    if (a->src < b->src)
	return -1;
    else if (a->src > b->src)
	return +1;
    else if (a->tgt < b->tgt)
	return -1;
    else if (a->tgt > b->tgt)
	return +1;
    return 0;
}

static int edgecmp(void *av, void *bv)
{
    return edgecmpC(av, bv);
}

static int vertcmpC(const void *av, const void *bv)
{
    const vertex *a = (vertex *)av;
    const vertex *b = (vertex *)bv;
    
    if (a->deg < b->deg)
	return -1;
    else if (a->deg > b->deg)
	return +1;
    else if (a->idx < b->idx)
	return -1;
    else if (a->idx > b->idx)
	return +1;
    return 0;
}

static int vertcmp(void *av, void *bv)
{
    return vertcmpC(av, bv);
}

#define CROSSPOINT_THRESHOLD (POINTRADIUS / 2.0)

#define square(x) ((x) * (x))

/*
 * Check whether the edge between s and t crosses the point p
 */
static bool crosspoint(point s, point t, point p)
{
    double dist_st = sqrt(square(t.x - s.x) + square(t.y - s.y));
    double dist_sp = sqrt(square(p.x - s.x) + square(p.y - s.y));
    double dist_pt = sqrt(square(t.x - p.x) + square(t.y - p.y));

    return dist_sp + dist_pt - dist_st < CROSSPOINT_THRESHOLD;
}

/*
 * Add edges between the vertices in the range [offa, offa + cnta] and vertices
 * in the range [offb, offb + cntb]. Make sure that edges don't cross other edges
 * or points and the degree of the involved vertices doesn't increase beyond max_deg.
 */
static void addedges(tree234* edges, vertex* vertices, point* points, int offa, int cnta,
                    int maxdega, int offb, int cntb, int maxdegb, const int n,random_state* rs)
{
    bool* contains;
    int i;
    vertex* vxa;
    vertex* vxb;
    tree234* vtcs;
    tree234* vtcsa;
    tree234** vtcsb;

    if (cnta < 0) cnta = offb + cntb - offa;
    else if (cntb < 0) cntb = offa + cnta - offb;
    if (cnta < cntb) {
        int tmp = cnta;
        cnta = cntb;
        cntb = tmp;
        tmp = offa;
        offa = offb;
        offb = tmp;
    }
    if (maxdega < 0) maxdega = maxdegb;
    else if (maxdegb < 0) maxdegb = maxdega;

    /* add all vertices in range a to a 234-tree */
    vtcsa = newtree234(vertcmp);
    
    for (i = offa; i < offa + cnta; i++)
    {
        add234(vtcsa, vertices + i);
        
    }
    

    /* for every vertex in range a add all vertices in range b to a 234-tree */
    vtcsb = snewn(cnta, tree234*);
    *vtcsb = newtree234(vertcmp);
    
    for (i = offb; i < offb + cntb; i++)
    {
        add234(*vtcsb, vertices + i);
        
    }
    
    for (i = 1; i < cnta; i++)
    {
        vtcsb[i] = copytree234(*vtcsb, NULL, NULL);
    }
    
    /*
     * Start adding edges. Pick the lowest degree vertex from the range a 234-tree
     * and a random vertex from the range b 234-tree and try to add an edge between
     * them. If the edge can't be added delete the range b vertex from the correspon-
     * ding 234-tree. Otherwise add the edge and update the involved vertices in the
     * range a 234-tree and all range b 234-trees. Repeat until there are no more edges
     * to add.
     */
    while (count234(vtcsa))
    {
        vxa = index234(vtcsa, 0);
        vtcs = vtcsb[vxa->idx - offa];
        if (!count234(vtcs))
        {
            del234(vtcsa, vxa);
            
            continue;
        }

        next_vxb:
        vxb = index234(vtcs, random_upto(rs, count234(vtcs)));
        
        if (vxa->idx == vxb->idx || isedge(edges, vxa->idx, vxb->idx))
        {
            del234(vtcs, vxb);
            
            if (count234(vtcs))
            {
                goto next_vxb;
            }
            else
            {
                del234(vtcsa, vxa);
                
                continue;
            }
        }

        /* check for crossing points */
        for (i = 0; i < n; i++)
        {
            if (i == vxa->idx || i == vxb->idx)
            {
                continue;
            }
            else if (crosspoint(points[vxa->idx], points[vxb->idx], points[i]))
            {
                del234(vtcs, vxb);
                
                if (count234(vtcs))
                {
                    goto next_vxb;
                }
                else
                {
                    del234(vtcsa, vxa);
                    
                    goto next_vxa;
                }
            }
        }

        addedge(edges, vxa->idx, vxb->idx);
        
        contains = snewn(cnta, bool);
        del234(vtcsa, vxa);
        for (i = 0; i < cnta; i++)
        {
            contains[i] = del234(vtcsb[i], vxa);
        }
        vxa->deg++;
        if (vxa->deg < maxdega)
        {
            add234(vtcsa, vxa);
            for (i = 0; i < cnta; i++)
            {
                if (contains[i]) add234(vtcsb[i], vxa);
            }
        }
        contains = sresize(contains, cnta + 1, bool);
        *contains = del234(vtcsa, vxb);
        for (i = 0; i < cnta; i++)
        {
            contains[i+1] = del234(vtcsb[i], vxb);
        }
        vxb->deg++;
        if (vxb->deg < maxdegb)
        {
            if (*contains) add234(vtcsa, vxb);
            for (i = 0; i < cnta; i++)
            {
                if (contains[i+1] && i != vxa->idx - offa)
                    add234(vtcsb[i], vxb);
            }
        }
        sfree(contains);
        

        next_vxa:;
    }

    freetree234(vtcsa);
    for (i = 0; i < cnta; i++) freetree234(vtcsb[i]);
    sfree(vtcsb);
}

/*
 * Add edges to a graph with 5 vertices such that it becomes the K_5
 */
static void make_K_5_edges(tree234* edges, vertex* vertices, int n)
{
    int i, j;


    for (i = 0; i < n - 1; i++)
    {
        for (j = i + 1; j < n; j++)
        {
            addedge(edges, i, j);
            vertices[i].deg++;
            vertices[j].deg++;
        }
    }
}

/*
 * Add edges to a graph with 6 vertices such that it becomes the K_33
 */
static void make_K_33_edges(tree234* edges, vertex* vertices, int n)
{
    int i;


    addedge(edges, 5, 2);
    addedge(edges, 5, 3);
    addedge(edges, 5, 4);

    addedge(edges, 0, 2);
    addedge(edges, 0, 3);
    addedge(edges, 0, 4);

    addedge(edges, 1, 2);
    addedge(edges, 1, 3);
    addedge(edges, 1, 4);

    for (i = 0; i < n; i++)
    {
        vertices[i].deg = 3;
    }
}

/*
 * These parameters are highly sensitive, changing them may cause problems when
 * generating new game descriptions.
 */

#define MINORRADIUS(n) ((n) / 3)
#define SUBGRAPH_DISTANCE (2 * POINTRADIUS) + 1
#define SUBGRAPH_POINTENTROPY 2
#define OVERLAYPOINT_TRESHOLD_GAMEGEN square(4 * POINTRADIUS)

/*
 * Arrange the points of a K_33 in two rows of three points each, such that it
 * becomes the default presentation of the K_33.
 */
static void make_K_33_points(point* points, long clim, int n)
{
    int i;


    for (i = 0; i < n; i++)
    {
        switch (i)
        {
            case 0:
            case 3:
                points[i].x = clim * COORDUNIT / 2;
                break;
            case 1:
            case 2:
                points[i].x = clim * COORDUNIT / 5;
                break;
            case 4:
            case 5:
                points[i].x = clim * COORDUNIT * 4 / 5;
                break;
            default:;
        }
        switch (i)
        {
            case 5:
            case 0:
            case 1:
                points[i].y = clim * COORDUNIT / 4;
                break;
            case 2:
            case 3:
            case 4:
                points[i].y = clim * COORDUNIT * 3 / 4;
                break;
            default:;
        }
    }
}

/*
 * Extended edge. Extends an edge by the degree sum of its incident
 * vertices.
 */
typedef struct edge_ext {
    edge e;
    int degsum;
} edge_ext;

/*
 *  Compares two constant extended edges by their degsum and edge
 */
static int eextcmpC(const void *av, const void  *bv)
{
    const edge_ext *a = (edge_ext *)av;
    const edge_ext *b = (edge_ext *)bv;

    if (a->degsum < b->degsum) return -1;
    else if (a->degsum > b->degsum) return 1;
    else return edgecmpC(&a->e, &b->e);
}

/*
 * Compares two extended edges with eextcmpC
 */
static int eextcmp(void *av, void *bv)
{
    return eextcmpC(av, bv);
}

static char *generate_once(const game_params *params, random_state *rs)
{
    char* ret;
    
    const int n_min = params->n_min;
    const int n_base = params->n_base;

    int* subsizes;
    int* suboffs;

    long i, j, k, l;
    long tmp, tmp2, tmp3;
    long coord_lim = COORDLIMIT(n_base);
    long circle_rad = MINORRADIUS(n_base);
    long* coords_x;
    long* coords_y;
    long* radii;

    point* pt;
    point* pts_min;
    point* pts_base;

    vertex* vx;
    vertex* vtcs_min;
    vertex* vtcs_base;

    edge* e;
    tree234* edges_min_234;
    tree234* edges_base_234;


    subsizes = snewn(n_min, int);
    for (i = 0; i < n_min; i++)
    {
        subsizes[i] = 2;
    }
    tmp = n_min * 2;
    while (tmp < n_base - (n_base % n_min))
    {
        subsizes[random_upto(rs, n_min)]++;
        tmp++;
    }
    suboffs = snewn(n_min+1, int);
    *suboffs = 0;
    for (i = 1; i <= n_min; i++)
    {
        suboffs[i] = suboffs[i-1] + subsizes[i-1];
    }


    /* Arrange the minor points in a circle with radius circle_rad */
    pts_min = snewn(n_min, point);
    tmp = coord_lim - (2 * COORDMARGIN);
    for (i = 0; i < n_min; i++)
    {
        double angle = ((double) i * 2.0 * PI) / (double) n_min;
        pt = pts_min + i;
        pt->x = (((double) tmp / 2.0) + ((double) circle_rad * sin(angle)) + COORDMARGIN)
                * COORDUNIT;
        pt->y = (((double) tmp / 2.0) + ((double) circle_rad * cos(angle)) + COORDMARGIN)
                * COORDUNIT;
        pt->d = 1;
        
    }

    /* Add edges to the minor */
    vtcs_min = snewn(n_min, vertex);
    for (i = 0; i < n_min; i++)
    {
        vx = vtcs_min + i;
        vx->idx = i;
        vx->deg = 0;
    }
    edges_min_234 = newtree234(edgecmp);
    switch (params->mode)
    {
        case MODE_DEFAULT:
            addedges(edges_min_234, vtcs_min, pts_min, 0, n_min, n_min - 2, 0, -1, -1, n_min,
                    rs);
            break;
        case MODE_SPECIAL:
            switch (n_min) {
                case 5:
                    make_K_5_edges(edges_min_234, vtcs_min, n_min);
                    break;
                case 6:
                    make_K_33_edges(edges_min_234, vtcs_min, n_min);
                    break;
                default:;
            }
            break;
        default:;
    }

    /*
     * To create the base graph we need to replace all minor points by subgraphs.
     * Therefore we need to determine the areas in which we can place the subgraphs
     * which depend on the shortest distance between the minor points and their
     * neirest neighbours and the grid border respectively. For every minor point
     * the minimum of these two values is used to calculate the radius of a circular
     * subgraph area around the minor point.
     */
    radii = snewn(n_min, long);
    tmp = COORDMARGIN * COORDUNIT;
    tmp2 = (coord_lim - COORDMARGIN) * COORDUNIT;
    for (i = 0; i < n_min; i++)
    {
        pt = pts_min + i;
        radii[i] = min(min(min(pt->x - tmp, pt->y - tmp), min(tmp2 - pt->x, tmp2 - pt->y)),
                        ((long) squarert(square(pts_min[1].x - pts_min[0].x) + square(pts_min[1].y
                            - pts_min[0].y)) / 2)) - (SUBGRAPH_DISTANCE / 2);
        
    }
    
    /*
     * Assign coordinates to the subgraphs. The coordinates must lie in the previously
     * calculated subgraph areas.
     */
    pts_base = snewn(n_base, point);
    for (i = 0; i < n_min; i++)
    {
        double rotation = (double) random_upto(rs, (100.0 * 2.0 * PI)
                            / (double) subsizes[i]) / 100.0;
        double* angles;
        tmp = subsizes[i] * SUBGRAPH_POINTENTROPY;
        angles = snewn(tmp, double);
        for (j = 0; j < tmp; j++)
        {
            angles[j] = ((double) j * 2.0 * PI) / (double) tmp;
        }
        shuffle(angles, tmp, sizeof(double), rs);
        for (j = 0; j < subsizes[i]; j++)
        {
            pt = pts_base + suboffs[i] + j;
            pt->x = (double) pts_min[i].x + ((double) radii[i] * sin(angles[j] + rotation));
            pt->y = (double) pts_min[i].y + ((double) radii[i] * cos(angles[j] + rotation));
            pt->d = 1;
            
        }
        sfree(angles);
    }
    sfree(radii);

    /* Arrange the K_33 minor points in two rows of three points each */
    if (params->mode == MODE_SPECIAL && n_min == 6)
    {
        make_K_33_points(pts_min, coord_lim, n_min);
    }

    /* Add edges to the subgraphs */
    vtcs_base = snewn(n_base, vertex);
    for (i = 0; i < n_base; i++)
    {
        vx = vtcs_base + i;
        vx->idx = i;
        vx->deg = 0;
    }
    edges_base_234 = newtree234(edgecmp);
    for (i = 0; i < n_min; i++)
    {
        addedges(edges_base_234, vtcs_base, pts_base,  suboffs[i], subsizes[i],
                2 * subsizes[i] / 3, suboffs[i], -1, -1, suboffs[n_min], rs);
    }

    /*
     * For every minor edge add an edge between random points of the subgraphs that
     * correspond to the adjacent minor points.
     */
    for (i = 0; (e = index234(edges_min_234, i)) != NULL; i++)
    {
        bool added = false;
        edge beste;
        edge_ext* eext;
        tree234* eexts = newtree234(eextcmp);
        beste.src = beste.tgt = -1;
        for (j = suboffs[e->src]; j < suboffs[e->src+1]; j++)
        {
            for (k = suboffs[e->tgt]; k < suboffs[e->tgt+1]; k++)
            {
                eext = snew(edge_ext);
                eext->e = (edge) { j, k };
                eext->degsum = vtcs_base[j].deg + vtcs_base[k].deg;
                add234(eexts, eext);
            }
        }
        for (j = 0; (eext = index234(eexts, j)) != NULL; j++)
        {
            beste = eext->e;
            /* check for crossing points */
            for (l = 0; l < suboffs[n_min]; l++)
            {
                if (l == beste.src || l == beste.tgt)
                    continue;
                else if (crosspoint(pts_base[beste.src], pts_base[beste.tgt], pts_base[l]))
                    goto next_subedge; /* this edge crosses a point => next target */
            }
            addedge(edges_base_234, beste.src, beste.tgt);
            added = true;
            goto next_minedge;
            next_subedge:;
        }
        next_minedge:
        if (!added)
        {
            beste = ((edge_ext*) index234(eexts, 0))->e;
            addedge(edges_base_234, beste.src, beste.tgt);
        }
        
        vtcs_base[beste.src].deg++;
        vtcs_base[beste.tgt].deg++;
        
        while ((eext = delpos234(eexts, 0)) != NULL) sfree(eext);
        freetree234(eexts);
    }

    /*
     * Assign coordinates to the remaining points. The coordinates may not overlay
     * other points or edges.
     */
    tmp = coord_lim - (2 * COORDMARGIN) + 1;
    coords_x = snewn(tmp, long);
    coords_y = snewn(tmp, long);
    for (i = 0; i < tmp; i++)
    {
        coords_x[i] = (i + COORDMARGIN) * COORDUNIT;
        coords_y[i] = (i + COORDMARGIN) * COORDUNIT;
    }
    shuffle(coords_x, tmp, sizeof(long), rs);
    shuffle(coords_y, tmp, sizeof(long), rs);
    tmp2 = suboffs[n_min];
    tmp3 = 0;
    for (i = 0; i < tmp && tmp2 < n_base; i++)
    {
        point p;
        p.d = 1;
        p.x = coords_x[i];
        p.y = coords_y[i];
        /* check for a crossing with an edge */
        for (k = 0; (e = index234(edges_base_234, k)) != NULL; k++)
        {
            if (crosspoint(pts_base[e->src], pts_base[e->tgt], p))
                goto next_coords; /* an edge crosses the point => next coords */
        }
        /* check for an overlaying with a point */
        for (k = 0; k < tmp2 + tmp3; k++)
        {
            pt = pts_base + k;
            if (square(pt->x - p.x) + square(pt->y - p.y) < OVERLAYPOINT_TRESHOLD_GAMEGEN)
                goto next_coords; /* the point overlays another point => next coords */
        }
        pt = pts_base + tmp2 + tmp3;
        *pt = p;
        
        if (tmp2 + ++tmp3 >= n_base) break;
        next_coords:;
    }
    sfree(coords_x);
    sfree(coords_y);

    /*
     * The loop above tries a few positions only, and it may fail to place all
     * the remaining points on a crowded board. Look at every position of the grid
     * then, and in the worst case settle for any position that is free.
     */
    for (l = 0; tmp2 + tmp3 < n_base && l < 2; l++)
    {
        long cx, cy;
        for (cx = COORDMARGIN; cx <= coord_lim - COORDMARGIN && tmp2 + tmp3 < n_base; cx++)
        {
            for (cy = COORDMARGIN; cy <= coord_lim - COORDMARGIN && tmp2 + tmp3 < n_base; cy++)
            {
                point p;
                p.d = 1;
                p.x = cx * COORDUNIT;
                p.y = cy * COORDUNIT;
                if (l == 0)
                {
                    for (k = 0; (e = index234(edges_base_234, k)) != NULL; k++)
                        if (crosspoint(pts_base[e->src], pts_base[e->tgt], p))
                            goto next_cell;
                }
                for (k = 0; k < tmp2 + tmp3; k++)
                {
                    pt = pts_base + k;
                    if (l == 0 ? (square(pt->x - p.x) + square(pt->y - p.y)
                                    < OVERLAYPOINT_TRESHOLD_GAMEGEN)
                               : (pt->x == p.x && pt->y == p.y))
                        goto next_cell;
                }
                pts_base[tmp2 + tmp3++] = p;
                next_cell:;
            }
        }
    }

    /* Add edges to the remaining points */
    addedges(edges_base_234, vtcs_base, pts_base, 0, n_base, n_base - 1, suboffs[n_min],
            -1, 3, n_base, rs);
    
    /* Add more edges to confuse the player */
    addedges(edges_base_234, vtcs_base, pts_base, 0, n_base, 5, 0, -1, -1, n_base, rs);



    /*
     * The generation of a new game description is finished. Now we need to encode
     * the description in a dynamically allocated string and connect this string to
     * the return value.
     */
    ret = NULL;
    {
    const char* sep = ",";
    char buf[80];
    int len = 0;
    int off = 0;
    long count_min = count234(edges_min_234);
    long count_base = count234(edges_base_234);
    edge* edges_min = snewn(count_min, edge);
    edge* edges_base = snewn(count_base, edge);

    /*
     * Calculate the length of the game description. It contains information about
     * the points and edges of the minor and base graph. Points, edges and subgraph
     * sizes are encoded in the following way:
     * 
     * (1) point: <index> - <degree> - <x coordinate> - <y coordinate>
     * (2) edge: <source index> - <target index>
     * (3) subgraph size: <subgraph index> - <subgraph size>
     * 
     * Single points, edges or subgraph sizes are separated by a comma while sets of
     * points, edges or subgraph sizes are separated by a semicolon:
     * 
     * (1),...,(1);(2),...,(2);(1),...,(1);(2),...,(2);(3),...,(3);
     */
    for (i = 0; i < n_min; i++)
    {
        pt = pts_min + i;
        len += (sprintf(buf, "%d-%d-%ld-%ld", vtcs_min[i].idx, vtcs_min[i].deg,
                        pt->x, pt->y) + 1);
    }
    for (i = 0; (e = index234(edges_min_234, i)) != NULL; i++)
    {
        edges_min[i] = *e;
        len += (sprintf(buf, "%d-%d", e->src, e->tgt) + 1);
    }
    for (i = 0; i < n_base; i++)
    {
        pt = pts_base + i;
        len += (sprintf(buf, "%d-%d-%ld-%ld", vtcs_base[i].idx, vtcs_base[i].deg,
                        pt->x, pt->y) + 1);
    }
    for (i = 0; (e = index234(edges_base_234, i)) != NULL; i++)
    {
        edges_base[i] = *e;
        len += (sprintf(buf, "%d-%d", e->src, e->tgt) + 1);
    }
    for (i = 0; i < n_min; i++)
    {
        vx = vtcs_min + i;
        len += (sprintf(buf, "%d-%d", vx->idx, subsizes[vx->idx]) + 1);
    }

    /*
     * Allocate memory for len+1 chars, that is exactly the length of the game
     * description including a trailing '\0'.
     */
    ret = snewn(++len, char);
    
    /*
     * Now encode the game description and write it into the allocated string
     * that will be connected to the return value.
     */
    for (i = 0; i < n_min - 1; i++)
    {
        pt = pts_min + vtcs_min[i].idx;
        off += sprintf(ret + off, "%d-%d-%ld-%ld%s", vtcs_min[i].idx, vtcs_min[i].deg,
                        pt->x, pt->y, sep);
    }
    sep = ";";
    pt = pts_min + vtcs_min[n_min-1].idx;
    off += sprintf(ret + off, "%d-%d-%ld-%ld%s", vtcs_min[n_min-1].idx,
                vtcs_min[n_min-1].deg, pt->x, pt->y, sep);
    sep = ",";
    for (i = 0; i < count_min - 1; i++)
    {
        e = edges_min + i;
        off += sprintf(ret + off, "%d-%d%s",e->src, e->tgt, sep);
    }
    sep = ";";
    e = edges_min + count_min - 1;
    off += sprintf(ret + off, "%d-%d%s", e->src, e->tgt, sep);
    sep = ",";
    for (i = 0; i < n_base - 1; i++)
    {
        pt = pts_base + vtcs_base[i].idx;
        off += sprintf(ret + off, "%d-%d-%ld-%ld%s", vtcs_base[i].idx, vtcs_base[i].deg,
                        pt->x, pt->y, sep);
    }
    sep = ";";
    pt = pts_base + vtcs_base[n_base-1].idx;
    off += sprintf(ret + off, "%d-%d-%ld-%ld%s", vtcs_base[n_base-1].idx,
                vtcs_base[n_base-1].deg, pt->x, pt->y, sep);
    sep = ",";
    for (i = 0; i < count_base - 1; i++)
    {
        e = edges_base + i;
        off += sprintf(ret + off, "%d-%d%s", e->src, e->tgt, sep);
    }
    sep = ";";
    e = edges_base + count_base - 1;
    off += sprintf(ret + off, "%d-%d%s", e->src, e->tgt, sep);
    sep = ",";
    for (i = 0; i < n_min - 1; i++)
    {
        vx = vtcs_min + i;
        off += sprintf(ret + off, "%d-%d%s", vx->idx, subsizes[vx->idx], sep);
    }
    sep = ";";
    vx = vtcs_min + n_min - 1;
    sprintf(ret + off, "%d-%d%s", vx->idx, subsizes[vx->idx], sep);

    sfree(edges_min);
    sfree(edges_base);
    }

    sfree(vtcs_min);
    sfree(pts_min);
    while ((e = delpos234(edges_min_234, 0)) != NULL) sfree(e);
    freetree234(edges_min_234);

    sfree(vtcs_base);
    sfree(subsizes);
    sfree(suboffs);
    sfree(pts_base);
    while ((e = delpos234(edges_base_234, 0)) != NULL) sfree(e);
    freetree234(edges_base_234);
    return ret;
}

/*
 * Can the game be solved?  The pieces that the generator made are a solution if
 * they are connected, but they might not be (the pieces are made at
 * random); then the solver has to be asked, and if it can say that there is no
 * solution the game is not used.
 */
#define GEN_ATTEMPTS 60
#define GEN_NODES 30000L

static bool desc_is_good(const game_params *params, const char *desc)
{
    game_state *s = NULL;
    int sizes[MAXM], lab[MAXB], i, v, off = 0;
    bool good;

    if (parse_desc(params, desc, &s, sizes) != NULL)
        return false;
    for (v = 0; v < MAXB; v++)
        lab[v] = -1;
    for (i = 0; i < params->n_min; i++) {
        for (v = off; v < off + sizes[i]; v++)
            lab[v] = i;
        off += sizes[i];
    }
    good = model_valid(&s->g, s->sh->madj, params->n_min, lab);
    if (!good) {
        struct msolver S;
        ms_init(&S, &s->g, s->sh->madj, params->n_min, GEN_NODES);
        good = ms_solve(&S) != 0;
    }
    free_game(s);
    return good;
}

static char *new_game_desc(const game_params *params, random_state *rs,
                           char **aux, bool interactive)
{
    char *desc = NULL;
    int attempt;

    for (attempt = 0; attempt < GEN_ATTEMPTS; attempt++) {
        sfree(desc);
        desc = generate_once(params, rs);
        if (desc_is_good(params, desc))
            break;
    }
    *aux = NULL;
    return desc;
}

/* ----------------------------------------------------------------------
 * Moves.
 *
 * A move is one or more of these, one after the other (and "S" in front if
 * the move comes from Solve):
 *
 *   1:i-x-y;      move the point i to x, y
 *   2:a-b;        contract the edge a-b, the point a stays
 *   4:i;          delete the point i (and all of its edges)
 *   8:a-b;        delete the edge a-b
 */

enum {
    MOVE_DRAG = 1,
    MOVE_CONTRACT = 2,
    MOVE_DELPOINT = 4,
    MOVE_DELEDGE = 8
};

static game_state *execute_move(const game_state *state, const char *move)
{
    game_state *ret = dup_game(state);
    const char *p = move;
    bool solve = false, any = false;
    long lim = COORDLIMIT(state->params.n_base) * COORDUNIT;

    if (*p == 'S') {
        solve = true;
        p++;
    }
    while (*p) {
        long t, a, b, x, y;

        if (!scan_num(&p, 8, &t) || !scan_char(&p, ':'))
            goto bad;
        switch (t) {
          case MOVE_DRAG:
            if (!scan_num(&p, MAXB - 1, &a) || !scan_char(&p, '-') ||
                !scan_num(&p, lim, &x) || !scan_char(&p, '-') ||
                !scan_num(&p, lim, &y) || !scan_char(&p, ';'))
                goto bad;
            if (!(ret->g.alive >> a & 1))
                goto bad;
            ret->x[a] = x;
            ret->y[a] = y;
            break;
          case MOVE_CONTRACT:
          case MOVE_DELEDGE:
            if (!scan_num(&p, MAXB - 1, &a) || !scan_char(&p, '-') ||
                !scan_num(&p, MAXB - 1, &b) || !scan_char(&p, ';'))
                goto bad;
            if (a == b || !(ret->g.alive >> a & 1) || !(ret->g.alive >> b & 1) ||
                !(ret->g.adj[a] >> b & 1))
                goto bad;
            if (t == MOVE_CONTRACT)
                bg_contract(&ret->g, a, b);
            else
                bg_delete_edge(&ret->g, a, b);
            break;
          case MOVE_DELPOINT:
            if (!scan_num(&p, MAXB - 1, &a) || !scan_char(&p, ';'))
                goto bad;
            if (!(ret->g.alive >> a & 1))
                goto bad;
            bg_delete_vertex(&ret->g, a);
            break;
          default:
            goto bad;
        }
        any = true;
    }
    if (!any)
        goto bad;

    if (solve && !state->solved)
        ret->cheated = true;
    ret->solved = bg_is_minor(&ret->g, ret->sh);
    return ret;

  bad:
    free_game(ret);
    return NULL;
}

/* ----------------------------------------------------------------------
 * Solve, and "Show next step".
 */

#define SOLVE_NODES 400000L

/*
 * A solution that was found for a "Show next step" is kept, so that the next
 * step can be taken from it (which is quicker, and does not change the
 * mind of the solver halfway).  It is only used for the very graph that it
 * was made for.
 */
static struct {
    bool valid;
    int k;
    bgraph g;
    mask_t hadj[MAXM];
    int lab[MAXB];
} hint_cache;

/* 1: the labels are in lab[];  0: no solution;  -1: not found in time */
static int find_model(const game_state *s, int *lab, bool use_cache)
{
    const struct shared *sh = s->sh;
    int k = sh->n_min, v;
    struct msolver S;
    int r;

    if (use_cache && hint_cache.valid && hint_cache.k == k &&
        !memcmp(&hint_cache.g, &s->g, sizeof(bgraph)) &&
        !memcmp(hint_cache.hadj, sh->madj, sizeof(mask_t) * k) &&
        model_valid(&s->g, sh->madj, k, hint_cache.lab)) {
        memcpy(lab, hint_cache.lab, sizeof(hint_cache.lab));
        return 1;
    }
    ms_init(&S, &s->g, sh->madj, k, SOLVE_NODES);
    r = ms_solve(&S);
    for (v = 0; v < MAXB; v++)
        lab[v] = r == 1 ? S.lab[v] : -1;
    return r;
}

static const char *no_model_message(int r, const game_state *orig,
                                    const game_state *cur)
{
    if (r < 0)
        return "No solution was found in time";
    if (orig && !memcmp(&orig->g, &cur->g, sizeof(bgraph)))
        return "This puzzle has no solution";
    return "This position cannot be solved; undo some moves";
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, const char **error)
{
    const struct shared *sh = currstate->sh;
    int lab[MAXB], r, i, k = sh->n_min;
    struct plan pl;
    char *ret, *q;

    if (currstate->solved) {
        *error = "The puzzle is solved already";
        return NULL;
    }
    r = find_model(currstate, lab, true);
    if (r != 1) {
        *error = no_model_message(r, state, currstate);
        return NULL;
    }
    if (!make_plan(&currstate->g, sh->madj, k, lab, &pl)) {
        *error = "No solution was found; undo some moves";
        return NULL;
    }
    ret = snewn(pl.n * 24 + k * 32 + 8, char);
    q = ret;
    *q++ = 'S';
    for (i = 0; i < pl.n; i++) {
        switch (pl.kind[i]) {
          case 'v': q += sprintf(q, "%d:%d;", MOVE_DELPOINT, pl.a[i]); break;
          case 'c': q += sprintf(q, "%d:%d-%d;", MOVE_CONTRACT, pl.a[i], pl.b[i]); break;
          default:  q += sprintf(q, "%d:%d-%d;", MOVE_DELEDGE, pl.a[i], pl.b[i]); break;
        }
    }
    /* and the points that are left go to the places of the points of the minor */
    for (i = 0; i < k; i++)
        q += sprintf(q, "%d:%d-%ld-%ld;", MOVE_DRAG, pl.root[i], sh->mx[i], sh->my[i]);
    return ret;
}

static char *hint_game(const game_state *orig, const game_state *curr, const char **error)
{
    const struct shared *sh = curr->sh;
    int lab[MAXB], r, k = sh->n_min, v;
    struct plan pl;
    char buf[64];

    if (curr->solved) {
        *error = "The puzzle is solved already";
        return NULL;
    }
    r = find_model(curr, lab, true);
    if (r != 1) {
        *error = no_model_message(r, orig, curr);
        return NULL;
    }
    if (!make_plan(&curr->g, sh->madj, k, lab, &pl) || pl.n == 0) {
        *error = "No next step was found; undo some moves";
        return NULL;
    }

    /* keep the solution for the graph that comes of this step */
    hint_cache.g = curr->g;
    switch (pl.kind[0]) {
      case 'v':
        sprintf(buf, "%d:%d;", MOVE_DELPOINT, pl.a[0]);
        bg_delete_vertex(&hint_cache.g, pl.a[0]);
        lab[pl.a[0]] = -1;
        break;
      case 'c':
        sprintf(buf, "%d:%d-%d;", MOVE_CONTRACT, pl.a[0], pl.b[0]);
        bg_contract(&hint_cache.g, pl.a[0], pl.b[0]);
        lab[pl.b[0]] = -1;
        break;
      default:
        sprintf(buf, "%d:%d-%d;", MOVE_DELEDGE, pl.a[0], pl.b[0]);
        bg_delete_edge(&hint_cache.g, pl.a[0], pl.b[0]);
        break;
    }
    hint_cache.valid = true;
    hint_cache.k = k;
    memset(hint_cache.hadj, 0, sizeof(hint_cache.hadj));
    memcpy(hint_cache.hadj, sh->madj, sizeof(mask_t) * k);
    for (v = 0; v < MAXB; v++)
        hint_cache.lab[v] = lab[v];
    return dupstr(buf);
}

/* ----------------------------------------------------------------------
 * The user interface.
 *
 * Left button (or a finger), on a point:
 *   - drag it to move it; drop it on a point that it has an edge to (the
 *     point that is dropped on gets bigger) to contract that edge, the
 *     dropped point is merged into the one that it is dropped on;
 *   - or tap it, and then tap a neighbour: the first point goes into the
 *     second one (tap on another point that is no neighbour to select that
 *     one instead, or tap beside the points to let go of the selection).
 * Right button (a long press): on a point or on an edge, to delete it; it
 * is deleted when the button is let go with the pointer still on it.
 */

enum {
    LAYOUT_SIDE,                       /* the minor on the left */
    LAYOUT_STACKED                     /* the minor above the big graph */
};

enum {
    UI_NONE,
    UI_PRESS,                          /* a point is pressed, but not moved */
    UI_DRAG,                           /* a point is being moved */
    UI_CONTRACT,                       /* ... and it is above a neighbour */
    UI_DELPOINT,                       /* a point is to be deleted */
    UI_DELEDGE                         /* an edge is to be deleted */
};

#define HIT_RADIUS 12                  /* how near to a point to press it */
#define OVERLAP_RADIUS (2 * POINTRADIUS)
#define HOVER_RADIUS (POINTRADIUS + 4)
#define MOVE_THRESHOLD 4               /* how far to go to drag a point */
#define EDGE_THRESHOLD 2.5             /* how near to an edge, see edge_excess */

struct game_ui {
    int layout;
    int mode;
    int pt;                            /* the point that is pressed, moved or to be deleted */
    int hover;                         /* UI_CONTRACT: the point that it is dropped on */
    int ea, eb;                        /* UI_DELEDGE: the edge */
    bool armed;                        /* UI_DEL...: the pointer is still on it */
    long sx, sy;                       /* where the press began */
    long ox, oy;                       /* the place of the point minus that of the pointer, when pressed */
    long dx, dy;                       /* where the dragged point is */
    int selected;                      /* the point that was tapped, or -1 */
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);
    memset(ui, 0, sizeof(*ui));
    ui->layout = LAYOUT_STACKED;
    ui->mode = UI_NONE;
    ui->pt = ui->hover = ui->ea = ui->eb = ui->selected = -1;
    return ui;
}

static void free_ui(game_ui *ui)
{
    sfree(ui);
}

enum { PREF_LAYOUT, N_PREF_ITEMS };

static config_item *get_prefs(game_ui *ui)
{
    config_item *ret = snewn(N_PREF_ITEMS + 1, config_item);

    ret[PREF_LAYOUT].name = "Position of the minor";
    ret[PREF_LAYOUT].kw = "layout";
    ret[PREF_LAYOUT].type = C_CHOICES;
    ret[PREF_LAYOUT].u.choices.choicenames = ":Beside the big graph:Above the big graph";
    ret[PREF_LAYOUT].u.choices.choicekws = ":beside:above";
    ret[PREF_LAYOUT].u.choices.selected = ui->layout;

    ret[N_PREF_ITEMS].name = NULL;
    ret[N_PREF_ITEMS].type = C_END;
    return ret;
}

static void set_prefs(game_ui *ui, const config_item *cfg)
{
    ui->layout = cfg[PREF_LAYOUT].u.choices.selected;
}

static bool game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
    ui->mode = UI_NONE;
    ui->pt = ui->hover = ui->ea = ui->eb = ui->selected = -1;
    ui->armed = false;
    return newstate->solved && !newstate->cheated && oldstate && !oldstate->solved;
}

struct game_drawstate {
    int tilesize;
};

/* ---- the layout ---- */

struct geom {
    int W, H;                          /* the size of the window */
    int bx, by, Wb;                    /* the big graph: corner and side of its square */
    int mx, my, Wm;                    /* and the minor */
};

static void geometry(const game_params *p, int layout, int ts, struct geom *g)
{
    int nb = p->n_base, nm = p->n_min;

    if (ts < 1)
        ts = 1;
    g->Wb = (nb + COORDMARGIN * 2) * ts;
    if (layout == LAYOUT_STACKED) {
        int gap = ts / 2;
        g->Wm = g->Wb / 2;
        g->W = g->Wb;
        g->H = g->Wm + gap + g->Wb;
        g->mx = (g->W - g->Wm) / 2;
        g->my = 0;
        g->bx = 0;
        g->by = g->Wm + gap;
    } else {
        g->Wm = (nm + COORDMARGIN * 2) * ts;
        g->W = g->Wm + g->Wb;
        g->H = g->Wb;
        g->mx = 0;
        g->my = 0;
        g->bx = g->Wm;
        g->by = 0;
    }
}

static void game_compute_size(const game_params *params, int tilesize,
                              const game_ui *ui, int *x, int *y)
{
    struct geom g;
    geometry(params, ui ? ui->layout : LAYOUT_STACKED, tilesize, &g);
    *x = g.W;
    *y = g.H;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
}

/* from a position in pixels to units of the big graph, and back */
static long to_units(int px, int org, int side, long lim)
{
    long a = (long)(px - org) * lim, b = side > 0 ? side : 1;
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

static float from_units(float u, int org, int side, long lim)
{
    return org + u * side / lim;
}

/* ---- finding the things that were pressed ---- */

/* the point of the set that is nearest to x, y, but not further than the radius */
static int nearest_point(const game_state *s, long x, long y, long radius, mask_t among)
{
    int best = -1, v;
    long bd = radius * radius + 1;
    mask_t m;

    for (m = s->g.alive & among; m; m &= m - 1) {
        long dx, dy, d;
        v = CTZ(m);
        dx = s->x[v] - x;
        dy = s->y[v] - y;
        d = dx * dx + dy * dy;
        if (d < bd) {
            bd = d;
            best = v;
        }
    }
    return best;
}

/*
 * How much longer is the way from one end of the edge to the point and on to
 * the other end than the edge itself?  (It is nothing if the point is on the
 * edge, and small if it is near.)
 */
static double edge_excess(const game_state *s, int a, int b, long x, long y)
{
    double ab = hypot((double)(s->x[b] - s->x[a]), (double)(s->y[b] - s->y[a]));
    double ac = hypot((double)(x - s->x[a]), (double)(y - s->y[a]));
    double cb = hypot((double)(s->x[b] - x), (double)(s->y[b] - y));
    return ac + cb - ab;
}

static bool nearest_edge(const game_state *s, long x, long y, double limit,
                         int *ea, int *eb)
{
    int a, b;
    double best = limit;
    bool found = false;

    for (a = 0; a < MAXB; a++) {
        if (!(s->g.alive >> a & 1))
            continue;
        for (b = a + 1; b < MAXB; b++) {
            double e;
            if (!(s->g.adj[a] >> b & 1))
                continue;
            e = edge_excess(s, a, b, x, y);
            if (e < best) {
                best = e;
                *ea = a;
                *eb = b;
                found = true;
            }
        }
    }
    return found;
}

static bool in_panel(const struct geom *g, int x, int y)
{
    return x >= g->bx && x < g->bx + g->Wb && y >= g->by && y < g->by + g->Wb;
}

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds, int x, int y, int button)
{
    const game_params *p = &state->params;
    long lim = COORDLIMIT(p->n_base) * COORDUNIT;
    struct geom g;
    long ux, uy;
    int v, w;
    char buf[64];

    if (button != LEFT_BUTTON && button != LEFT_DRAG && button != LEFT_RELEASE &&
        button != RIGHT_BUTTON && button != RIGHT_DRAG && button != RIGHT_RELEASE)
        return MOVE_UNUSED;

    geometry(p, ui->layout, ds->tilesize, &g);
    ux = to_units(x - g.bx, 0, g.Wb, lim);
    uy = to_units(y - g.by, 0, g.Wb, lim);

    if (button == LEFT_BUTTON) {
        ui->mode = UI_NONE;
        ui->pt = ui->hover = -1;
        v = nearest_point(state, ux, uy, HIT_RADIUS, ~(mask_t)0);
        if (v >= 0) {
            ui->mode = UI_PRESS;
            ui->pt = v;
            ui->sx = ux;
            ui->sy = uy;
            ui->ox = state->x[v] - ux;
            ui->oy = state->y[v] - uy;
            ui->dx = state->x[v];
            ui->dy = state->y[v];
            return MOVE_UI_UPDATE;
        }
        if (ui->selected >= 0) {
            ui->selected = -1;
            return MOVE_UI_UPDATE;
        }
        return MOVE_NO_EFFECT;
    }

    if (button == RIGHT_BUTTON) {
        ui->mode = UI_NONE;
        ui->pt = ui->hover = -1;
        ui->selected = -1;
        v = nearest_point(state, ux, uy, HIT_RADIUS, ~(mask_t)0);
        if (v >= 0) {
            ui->mode = UI_DELPOINT;
            ui->pt = v;
            ui->armed = true;
        } else if (nearest_edge(state, ux, uy, EDGE_THRESHOLD, &ui->ea, &ui->eb)) {
            ui->mode = UI_DELEDGE;
            ui->armed = true;
        }
        return MOVE_UI_UPDATE;
    }

    if (button == LEFT_DRAG) {
        if (ui->mode != UI_PRESS && ui->mode != UI_DRAG && ui->mode != UI_CONTRACT)
            return MOVE_NO_EFFECT;
        if (ui->mode == UI_PRESS) {
            long dx = ux - ui->sx, dy = uy - ui->sy;
            if (dx * dx + dy * dy < (long)MOVE_THRESHOLD * MOVE_THRESHOLD)
                return MOVE_NO_EFFECT;
        }
        ui->dx = ux + ui->ox;
        ui->dy = uy + ui->oy;
        w = nearest_point(state, ui->dx, ui->dy, OVERLAP_RADIUS,
                          state->g.adj[ui->pt]);
        if (w >= 0) {
            ui->mode = UI_CONTRACT;
            ui->hover = w;
        } else {
            ui->mode = UI_DRAG;
            ui->hover = -1;
        }
        return MOVE_UI_UPDATE;
    }

    if (button == LEFT_RELEASE) {
        int mode = ui->mode;
        int pt = ui->pt;

        if (mode != UI_PRESS && mode != UI_DRAG && mode != UI_CONTRACT)
            return MOVE_NO_EFFECT;
        ui->mode = UI_NONE;
        ui->pt = ui->hover = -1;

        if (mode == UI_PRESS) {
            /* a tap */
            if (ui->selected < 0) {
                ui->selected = pt;
            } else if (ui->selected == pt) {
                ui->selected = -1;
            } else if (state->g.adj[pt] >> ui->selected & 1) {
                sprintf(buf, "%d:%d-%d;", MOVE_CONTRACT, pt, ui->selected);
                ui->selected = -1;
                return dupstr(buf);
            } else {
                ui->selected = pt;
            }
            return MOVE_UI_UPDATE;
        }

        if (!in_panel(&g, x, y))
            return MOVE_UI_UPDATE;     /* let go outside: nothing happens */

        ui->dx = ux + ui->ox;
        ui->dy = uy + ui->oy;
        w = nearest_point(state, ui->dx, ui->dy, OVERLAP_RADIUS,
                          state->g.adj[pt]);
        if (w >= 0) {
            sprintf(buf, "%d:%d-%d;", MOVE_CONTRACT, w, pt);
            return dupstr(buf);
        }
        if (nearest_point(state, ui->dx, ui->dy, OVERLAP_RADIUS,
                          ~((mask_t)1 << pt)) >= 0)
            return MOVE_UI_UPDATE;     /* on top of a point that is no neighbour */
        if (ui->dx < 2 * POINTRADIUS || ui->dx > lim - 2 * POINTRADIUS ||
            ui->dy < 2 * POINTRADIUS || ui->dy > lim - 2 * POINTRADIUS)
            return MOVE_UI_UPDATE;     /* too near to the border */
        if (ui->dx == state->x[pt] && ui->dy == state->y[pt])
            return MOVE_UI_UPDATE;
        sprintf(buf, "%d:%d-%ld-%ld;", MOVE_DRAG, pt, ui->dx, ui->dy);
        return dupstr(buf);
    }

    if (button == RIGHT_DRAG || button == RIGHT_RELEASE) {
        bool armed;
        int mode = ui->mode;

        if (mode != UI_DELPOINT && mode != UI_DELEDGE)
            return MOVE_NO_EFFECT;
        if (mode == UI_DELPOINT) {
            long dx = state->x[ui->pt] - ux, dy = state->y[ui->pt] - uy;
            armed = dx * dx + dy * dy < 4 * (long)HIT_RADIUS * HIT_RADIUS;
        } else {
            armed = edge_excess(state, ui->ea, ui->eb, ux, uy) < 2 * EDGE_THRESHOLD;
        }
        if (button == RIGHT_DRAG) {
            if (armed == ui->armed)
                return MOVE_NO_EFFECT;
            ui->armed = armed;
            return MOVE_UI_UPDATE;
        }
        armed = armed && in_panel(&g, x, y);
        ui->mode = UI_NONE;
        ui->armed = false;
        if (!armed)
            return MOVE_UI_UPDATE;
        if (mode == UI_DELPOINT)
            sprintf(buf, "%d:%d;", MOVE_DELPOINT, ui->pt);
        else
            sprintf(buf, "%d:%d-%d;", MOVE_DELEDGE, ui->ea, ui->eb);
        ui->pt = ui->ea = ui->eb = -1;
        return dupstr(buf);
    }

    return MOVE_UNUSED;
}

/* ----------------------------------------------------------------------
 * Drawing.
 */

#define ANIM_LENGTH 0.7F
#define FLASH_LENGTH 0.3F

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);

    game_mkhighlight(fe, ret, COL_BACKGROUND, -1, COL_SYSBACKGROUND);

#define COLOUR(c, r, g, b) do { \
        ret[(c) * 3 + 0] = (r); ret[(c) * 3 + 1] = (g); ret[(c) * 3 + 2] = (b); \
    } while (0)
    COLOUR(COL_OUTLINE, 0.3F, 0.3F, 0.3F);        /* dark grey */
    COLOUR(COL_BASEPOINT, 0.0F, 0.0F, 1.0F);      /* blue */
    COLOUR(COL_MINORPOINT, 1.0F, 0.0F, 0.0F);     /* red */
    COLOUR(COL_DRAGPOINT, 0.0F, 1.0F, 0.0F);      /* green */
    COLOUR(COL_DELPOINT, 1.0F, 0.5F, 0.0F);       /* orange */
    COLOUR(COL_HIDEPOINT, 0.3F, 0.7F, 1.0F);      /* light blue */
    COLOUR(COL_POINTOUTLINE, 0.0F, 0.0F, 0.0F);   /* black */
    COLOUR(COL_EDGE, 0.0F, 0.0F, 0.0F);           /* black */
    COLOUR(COL_DELEDGE, 1.0F, 0.5F, 0.0F);        /* orange */
    COLOUR(COL_HIDEEDGE, 0.4F, 0.4F, 0.4F);       /* grey */
    COLOUR(COL_FLASH, 1.0F, 1.0F, 1.0F);          /* white */
    COLOUR(COL_FLASH2, 0.5F, 0.5F, 0.5F);         /* grey */
#undef COLOUR

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    game_drawstate *ds = snew(game_drawstate);
    ds->tilesize = 0;
    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds);
}

static float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    const game_params *p = &state->params;
    const struct shared *sh = state->sh;
    long lim = COORDLIMIT(p->n_base) * COORDUNIT;
    int ts = ds->tilesize, bg, a, b, v;
    struct geom g;
    float r_anim, thick;
    int radius;
    /* what is drawn, and what it is compared with, to see what is going away */
    const game_state *from = (dir > 0 && oldstate) ? oldstate : state;
    const game_state *other = (dir < 0 && oldstate) ? oldstate : state;
    float px[MAXB], py[MAXB];
    char status[96];
    bool dragging = ui->mode == UI_DRAG || ui->mode == UI_CONTRACT;
    int del_pt = ui->mode == UI_DELPOINT && ui->armed ? ui->pt : -1;
    int dea = ui->mode == UI_DELEDGE && ui->armed ? ui->ea : -1;
    int deb = ui->mode == UI_DELEDGE && ui->armed ? ui->eb : -1;

    geometry(p, ui->layout, ts, &g);

    if (!flashtime)
        bg = COL_BACKGROUND;
    else if (flashtime < FLASH_LENGTH / 3.0F || flashtime > 2.0F * FLASH_LENGTH / 3.0F)
        bg = COL_FLASH;
    else
        bg = COL_FLASH2;

    r_anim = oldstate ? animtime / ANIM_LENGTH : 1.0F;
    if (r_anim > 1.0F)
        r_anim = 1.0F;
    thick = ts / 12.0F;
    if (thick < 1.0F)
        thick = 1.0F;
    radius = POINTRADIUS * ts / COORDUNIT;
    if (radius < 2)
        radius = 2;

    /* the background, and the two squares */
    draw_rect(dr, 0, 0, g.W, g.H, COL_SYSBACKGROUND);
    draw_rect(dr, g.mx, g.my, g.Wm, g.Wm, bg);
    draw_rect(dr, g.bx, g.by, g.Wb, g.Wb, bg);
    draw_rect_outline(dr, g.mx, g.my, g.Wm, g.Wm, COL_OUTLINE);
    draw_rect_outline(dr, g.bx, g.by, g.Wb, g.Wb, COL_OUTLINE);

    /* the minor */
    for (a = 0; a < sh->n_min; a++)
        for (b = a + 1; b < sh->n_min; b++)
            if (sh->madj[a] >> b & 1)
                draw_thick_line(dr, thick,
                                from_units((float)sh->mx[a], g.mx, g.Wm, lim),
                                from_units((float)sh->my[a], g.my, g.Wm, lim),
                                from_units((float)sh->mx[b], g.mx, g.Wm, lim),
                                from_units((float)sh->my[b], g.my, g.Wm, lim),
                                COL_EDGE);
    for (a = 0; a < sh->n_min; a++)
        draw_circle(dr, (int)(from_units((float)sh->mx[a], g.mx, g.Wm, lim) + 0.5F),
                    (int)(from_units((float)sh->my[a], g.my, g.Wm, lim) + 0.5F),
                    radius, COL_MINORPOINT, COL_POINTOUTLINE);

    /* where the points of the big graph are */
    for (v = 0; v < MAXB; v++) {
        float ux, uy;
        if (!(from->g.alive >> v & 1))
            continue;
        if (dragging && ui->pt == v) {
            ux = (float)ui->dx;
            uy = (float)ui->dy;
        } else if (oldstate) {
            ux = lerp((float)oldstate->x[v], (float)state->x[v], r_anim);
            uy = lerp((float)oldstate->y[v], (float)state->y[v], r_anim);
        } else {
            ux = (float)state->x[v];
            uy = (float)state->y[v];
        }
        px[v] = from_units(ux, g.bx, g.Wb, lim);
        py[v] = from_units(uy, g.by, g.Wb, lim);
    }

    /* its edges */
    for (a = 0; a < MAXB; a++) {
        if (!(from->g.alive >> a & 1))
            continue;
        for (b = a + 1; b < MAXB; b++) {
            bool going;
            if (!(from->g.adj[a] >> b & 1))
                continue;
            if ((a == dea && b == deb) || a == del_pt || b == del_pt)
                continue;              /* drawn below */
            going = !(other->g.adj[a] >> b & 1);
            draw_thick_line(dr, thick, px[a], py[a], px[b], py[b],
                            going ? COL_HIDEEDGE : COL_EDGE);
        }
    }
    if (dea >= 0)
        draw_thick_line(dr, thick, px[dea], py[dea], px[deb], py[deb], COL_DELEDGE);
    if (del_pt >= 0)
        for (b = 0; b < MAXB; b++)
            if (from->g.adj[del_pt] >> b & 1)
                draw_thick_line(dr, thick, px[del_pt], py[del_pt], px[b], py[b],
                                COL_DELEDGE);

    /* and its points: the selected point, the one to drop on and the dragged
     * one stand out, and the neighbours of the selected point have a halo */
    for (v = 0; v < MAXB; v++) {
        int rad = radius, col;
        if (!(from->g.alive >> v & 1) || (dragging && ui->pt == v))
            continue;
        if (ui->selected >= 0 && ui->selected != v &&
            (from->g.adj[ui->selected] >> v & 1) && !oldstate)
            draw_circle(dr, (int)(px[v] + 0.5F), (int)(py[v] + 0.5F),
                        radius + ts / 6 + 2, COL_HIDEPOINT, COL_HIDEPOINT);
        if (dragging && ui->mode == UI_CONTRACT && ui->hover == v)
            rad = HOVER_RADIUS * ts / COORDUNIT;
        if (v == del_pt)
            col = COL_DELPOINT;
        else if (v == ui->selected || (ui->mode == UI_PRESS && ui->pt == v))
            col = COL_DRAGPOINT;
        else if (!(other->g.alive >> v & 1))
            col = COL_HIDEPOINT;
        else
            col = COL_BASEPOINT;
        draw_circle(dr, (int)(px[v] + 0.5F), (int)(py[v] + 0.5F), rad, col,
                    COL_POINTOUTLINE);
    }
    if (dragging && (from->g.alive >> ui->pt & 1))
        draw_circle(dr, (int)(px[ui->pt] + 0.5F), (int)(py[ui->pt] + 0.5F), radius,
                    COL_DRAGPOINT, COL_POINTOUTLINE);

    if (state->solved)
        strcpy(status, state->cheated ? "Auto-solved" : "COMPLETED!");
    else
        sprintf(status, "Points: %d (need %d)   Edges: %d (need %d)",
                POPCOUNT(state->g.alive), sh->n_min, bg_edges(&state->g), sh->nmedges);
    status_bar(dr, status);

    draw_update(dr, 0, 0, g.W, g.H);
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    if (((dir < 0) ? oldstate : newstate)->solved &&
        !((dir > 0) ? oldstate : newstate)->solved &&
        ((dir < 0) ? oldstate : newstate)->cheated)
        return ANIM_LENGTH;
    return 0.0F;
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate, int dir, game_ui *ui)
{
    if (dir > 0 && newstate->solved && !oldstate->solved && !newstate->cheated)
        return FLASH_LENGTH;
    return 0.0F;
}

static int game_status(const game_state *state)
{
    return state->solved ? +1 : 0;
}

static key_label *game_request_keys(const game_params *params, int *nkeys, int *arrow_mode)
{
    *nkeys = 0;
    *arrow_mode = ANDROID_NO_ARROWS;
    return NULL;
}

#ifdef COMBINED
#define thegame minorfinder
#endif

const struct game thegame = {
    "Minor Finder", "games.minorfinder", "minorfinder",
    default_params,
    NULL, game_preset_menu,
    decode_params,
    encode_params,
    free_params,
    dup_params,
    true, game_configure, custom_params,
    validate_params,
    new_game_desc,
    validate_desc,
    new_game,
    NULL, /* set_public_desc */
    dup_game,
    free_game,
    true, solve_game,
    false, NULL, NULL, /* can_format_as_text_now, text_format */
    get_prefs, set_prefs,
    new_ui,
    free_ui,
    NULL, /* encode_ui */
    NULL, /* decode_ui */
    game_request_keys,
    NULL, /* android_cursor_visibility */
    game_changed_state,
    NULL, /* current_key_label */
    interpret_move,
    execute_move,
    24, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    NULL, /* get_cursor_location */
    game_status,
#ifndef NO_PRINTING
    false, false, NULL, NULL,          /* print_size, print */
#endif
    true,                              /* wants_statusbar */
    false, NULL,                       /* timing_state */
    SOLVE_ANIMATES | REQUIRE_RBUTTON,  /* flags */
    hint_game,
};
