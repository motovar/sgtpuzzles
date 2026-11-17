/*
 * decanting.c: "Decanting", the "water sort" puzzle.
 *
 * Several glass tubes hold coloured liquids in layers.  A move pours the
 * top of one tube into another: all the layers of the colour on top
 * of the first tube that fit move across, on to an empty tube or on to a
 * layer of the same colour.  The puzzle is done when every colour is
 * together in a tube of its own, filled up to the top.
 *
 * The idea and the first sketch of the game (a skeleton with the tubes,
 * the selection and the pouring, but without a generator, a solver or a
 * hint) are by Lalo Martins, from his fork of the puzzle collection.
 * This version adds:
 *
 *  - a generator that deals the layers at random and keeps only the games
 *    that the solver can finish (and, for the sizes where that takes too
 *    long to find out, a generator that scrambles a finished game with
 *    moves that run backwards, which is solvable by construction);
 *  - a solver (a best-first search over the positions, with the tubes
 *    treated as an unordered set, and without the moves that cannot
 *    matter), used for "Solve" and for "Show next step";
 *  - "hidden layers": a layer is shown only when it belongs to the colour on
 *    top of its tube, or when it has been seen before;
 *  - the game description is checked completely, a move is checked before
 *    it is made, and the number of moves is counted;
 *  - a way to select and pour with the cursor keys, a selection that moves
 *    to another tube when the pour is not possible, labels on the layers
 *    for those who cannot tell the colours apart (a preference), and an
 *    animation of the pouring.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdint.h>

#include "puzzles.h"

enum {
    MAX_LAYERS = 8,
    MAX_COLOURS = 12,
    MAX_TUBES = 16,
    HISTLEN = 48,                      /* positions that a state remembers */
    WRAP_TUBES = 5,                    /* more tubes than this go in two rows */
    PREFERRED_TILE_SIZE = 32
};

/* The search limits, in positions kept in memory. */
#define HINT_NODES   400000L
#define SOLVE_NODES  1200000L
#define GEN_NODES    60000L
#define GEN_ATTEMPTS 40

enum {
    COL_BACKGROUND,
    COL_TUBE,
    COL_CURSOR,
    COL_HIDDEN,
    COL_LABEL_DARK,
    COL_LABEL_LIGHT,
    COL_0,
    COL_1,
    COL_2,
    COL_3,
    COL_4,
    COL_5,
    COL_6,
    COL_7,
    COL_8,
    COL_9,
    COL_10,
    COL_11,
    NCOLOURS
};

enum {
    PREF_LABELS,
    N_PREF_ITEMS
};

enum {
    LABELS_NONE,
    LABELS_NUMBERS,
    LABELS_LETTERS
};

static const unsigned long palette[MAX_COLOURS] = {
    /* the first ones are the ones that small games use, so they are the ones that differ most */
    0xE53935,                          /* red */
    0x4FA3F7,                          /* light blue */
    0xFDD835,                          /* yellow */
    0x2E9E4F,                          /* green */
    0x8E44AD,                          /* purple */
    0xFB8C00,                          /* orange */
    0x26C6DA,                          /* cyan */
    0xF06292,                          /* pink */
    0x2745C4,                          /* dark blue */
    0x9CCC30,                          /* lime */
    0x8D5A3B,                          /* brown */
    0x90A4AE,                          /* silver */
};

static const char HEX[16] = "0123456789abcdef";

struct game_params {
    int ncolours;
    int ntubes;
    int nlayers;
    bool hiddenlayers;
};

struct game_state {
    game_params p;
    signed char tubes[MAX_TUBES][MAX_LAYERS];  /* bottom first, -1 for none */
    unsigned char known[MAX_TUBES];    /* bit n: the layer n has been seen */
    int moves;
    bool solved;
    int nhist;
    uint64_t hist[HISTLEN];            /* the positions that came before */
};

struct game_ui {
    int selected;
    int cursor;
    bool cursor_visible;
    int labels;
};

/* ----------------------------------------------------------------------
 * Tubes.
 */

static int tube_count(const signed char *t, int nl)
{
    int n = 0;
    while (n < nl && t[n] >= 0) n++;
    return n;
}

/* the colour on top (-1 if there is none), the layers of it, and the layers in all */
static void tube_top(const signed char *t, int nl, int *colour, int *block, int *count)
{
    int n = tube_count(t, nl), b = 0;
    if (n == 0) {
        *colour = -1; *block = 0; *count = 0;
        return;
    }
    *colour = t[n - 1];
    while (b < n && t[n - 1 - b] == *colour) b++;
    *block = b;
    *count = n;
}

/* The number of layers that a pour from one tube to another moves (0: it is not allowed). */
static int pour_amount(const signed char tubes[][MAX_LAYERS], int nl, int from, int to)
{
    int fc, fb, fn, tc, tb, tn, room;
    if (from == to) return 0;
    tube_top(tubes[from], nl, &fc, &fb, &fn);
    if (fn == 0) return 0;
    tube_top(tubes[to], nl, &tc, &tb, &tn);
    room = nl - tn;
    if (room <= 0) return 0;
    if (tn > 0 && tc != fc) return 0;
    return fb < room ? fb : room;
}

static void pour_layers(signed char tubes[][MAX_LAYERS], int nl, int from, int to, int k)
{
    int fn = tube_count(tubes[from], nl), tn = tube_count(tubes[to], nl), c, i;
    c = tubes[from][fn - 1];
    for (i = 0; i < k; i++) tubes[from][fn - 1 - i] = -1;
    for (i = 0; i < k; i++) tubes[to][tn + i] = (signed char)c;
}

static bool tubes_solved(const signed char tubes[][MAX_LAYERS], int nt, int nl)
{
    int i, j;
    for (i = 0; i < nt; i++) {
        if (tubes[i][0] < 0) continue;
        for (j = 1; j < nl; j++)
            if (tubes[i][j] != tubes[i][0]) return false;
    }
    return true;
}

static bool has_pour(const signed char tubes[][MAX_LAYERS], int nt, int nl)
{
    int i, j;
    for (i = 0; i < nt; i++)
        for (j = 0; j < nt; j++)
            if (pour_amount(tubes, nl, i, j)) return true;
    return false;
}

/* ----------------------------------------------------------------------
 * Hashing of positions.  The tubes are an unordered set: two positions
 * that only differ in the order of the tubes have the same hash.
 */

static uint64_t mix64(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

static uint64_t tube_hash(const signed char *t, int nl)
{
    uint64_t h = 14695981039346656037ULL;
    int i;
    for (i = 0; i < nl; i++)
        h = (h ^ (uint64_t)(t[i] + 1)) * 1099511628211ULL;
    return mix64(h);
}

static uint64_t board_hash(const signed char tubes[][MAX_LAYERS], int nt, int nl)
{
    uint64_t h = 0;
    int i;
    for (i = 0; i < nt; i++) h += tube_hash(tubes[i], nl);
    return h;
}

/* ----------------------------------------------------------------------
 * The solver: a best-first search over the positions that can be reached.
 *
 * A position is stored in a nibble per layer.  Two positions that only
 * differ in the order of the tubes count as the same one, and the moves
 * that cannot be needed are left out:
 *   - nothing is poured out of a tube that is full of one colour,
 *   - a tube that holds a single colour is not poured into an empty tube,
 *   - of several empty tubes (or tubes with the same contents) only one is
 *     tried, and
 *   - two kinds of pour are made at once, as the only move of that
 *     position, because a solution that has them is never worse: a pour
 *     that joins two tubes that hold only the same colour, and a pour
 *     on to such a tube that brings the last layers of that colour together.
 *     (Not every pour on to a tube of one colour is safe: it may be
 *     better to empty that tube instead, to have a tube to work with.)
 */

struct pour {
    unsigned char from, to, n;
};

/* the number of blocks of colour in all the tubes less the number of different colours at the
 * bottom of a tube: every block but the one that stays at the bottom has to be poured at least once */
static int heuristic(const signed char t[][MAX_LAYERS], int nt, int nl)
{
    int blocks = 0, mask = 0, i, j, n;
    for (i = 0; i < nt; i++) {
        n = tube_count(t[i], nl);
        if (!n) continue;
        mask |= 1 << t[i][0];
        blocks++;
        for (j = 1; j < n; j++)
            if (t[i][j] != t[i][j - 1]) blocks++;
    }
    while (mask) {
        blocks--;
        mask &= mask - 1;
    }
    return blocks;
}

static int gen_pours(const signed char t[][MAX_LAYERS], int nt, int nl, struct pour *out)
{
    int cnt[MAX_TUBES], top[MAX_TUBES], blk[MAX_TUBES];
    int i, j, k, n = 0, firstempty = -1;
    int bk = 0, bf = 0, bt = 0;

    for (i = 0; i < nt; i++) {
        tube_top(t[i], nl, &top[i], &blk[i], &cnt[i]);
        if (cnt[i] == 0 && firstempty < 0) firstempty = i;
    }

    /* the pours that do no harm: on to a tube of the same colour that is joined with another one
     * (the whole of this tube is poured), or that complete the colour */
    for (i = 0; i < nt; i++) {
        if (!cnt[i] || (cnt[i] == nl && blk[i] == nl)) continue;
        for (j = 0; j < nt; j++) {
            if (j == i || cnt[j] == 0 || cnt[j] == nl) continue;
            if (blk[j] != cnt[j] || top[j] != top[i]) continue;
            if (blk[i] != cnt[i] && cnt[j] + blk[i] != nl) continue;
            k = blk[i] < nl - cnt[j] ? blk[i] : nl - cnt[j];
            if (k > bk) { bk = k; bf = i; bt = j; }
        }
    }
    if (bk > 0) {
        out[0].from = (unsigned char)bf;
        out[0].to = (unsigned char)bt;
        out[0].n = (unsigned char)bk;
        return 1;
    }

    for (i = 0; i < nt; i++) {
        bool dup = false;
        if (!cnt[i]) continue;
        if (cnt[i] == nl && blk[i] == nl) continue;
        for (j = 0; j < i; j++)
            if (memcmp(t[j], t[i], nl) == 0) { dup = true; break; }
        if (dup) continue;
        for (j = 0; j < nt; j++) {
            int j2;
            if (j == i) continue;
            if (cnt[j] == 0) {
                if (j != firstempty) continue;
                if (blk[i] == cnt[i]) continue;    /* a single colour into an empty tube */
            } else {
                if (cnt[j] == nl || top[j] != top[i]) continue;
            }
            dup = false;
            for (j2 = 0; j2 < j; j2++)
                if (j2 != i && memcmp(t[j2], t[j], nl) == 0) { dup = true; break; }
            if (dup) continue;
            k = blk[i] < nl - cnt[j] ? blk[i] : nl - cnt[j];
            out[n].from = (unsigned char)i;
            out[n].to = (unsigned char)j;
            out[n].n = (unsigned char)k;
            n++;
        }
    }
    return n;
}

struct hent {
    int f, h, node;
    unsigned seq;
};

typedef struct solver {
    int nt, nl, sbytes;
    unsigned char *states;
    int *parent;
    unsigned char *mv;                 /* from, to, number of layers */
    int *g;
    int n, cap;
    struct hent *heap;
    int nheap, capheap;
    uint64_t *hs;
    size_t hcap, hcount;
    unsigned seq;
} solver;

static void solver_init(solver *s, int nt, int nl)
{
    memset(s, 0, sizeof(*s));
    s->nt = nt; s->nl = nl;
    s->sbytes = (nt * nl + 1) / 2;
    s->cap = 1024;
    s->states = snewn((size_t)s->cap * s->sbytes, unsigned char);
    s->parent = snewn(s->cap, int);
    s->mv = snewn((size_t)s->cap * 3, unsigned char);
    s->g = snewn(s->cap, int);
    s->capheap = 1024;
    s->heap = snewn(s->capheap, struct hent);
    s->hcap = 4096;
    s->hs = snewn(s->hcap, uint64_t);
    memset(s->hs, 0, s->hcap * sizeof(uint64_t));
}

static void solver_free(solver *s)
{
    sfree(s->states); sfree(s->parent); sfree(s->mv); sfree(s->g);
    sfree(s->heap); sfree(s->hs);
}

/* true if the key is new */
static bool hs_insert(solver *s, uint64_t key)
{
    size_t i;
    if (key == 0) key = 1;
    if ((s->hcount + 1) * 2 > s->hcap) {
        size_t ncap = s->hcap * 2, j;
        uint64_t *nh = snewn(ncap, uint64_t);
        memset(nh, 0, ncap * sizeof(uint64_t));
        for (j = 0; j < s->hcap; j++) {
            if (s->hs[j]) {
                size_t p = (size_t)(s->hs[j] & (ncap - 1));
                while (nh[p]) p = (p + 1) & (ncap - 1);
                nh[p] = s->hs[j];
            }
        }
        sfree(s->hs);
        s->hs = nh;
        s->hcap = ncap;
    }
    i = (size_t)(key & (s->hcap - 1));
    while (s->hs[i]) {
        if (s->hs[i] == key) return false;
        i = (i + 1) & (s->hcap - 1);
    }
    s->hs[i] = key;
    s->hcount++;
    return true;
}

static void pack_board(const signed char t[][MAX_LAYERS], int nt, int nl, unsigned char *out)
{
    int n = nt * nl, i;
    memset(out, 0, (size_t)(n + 1) / 2);
    for (i = 0; i < n; i++) {
        int v = t[i / nl][i % nl] + 1;
        out[i >> 1] |= (unsigned char)((i & 1) ? v << 4 : v);
    }
}

static void unpack_board(signed char t[][MAX_LAYERS], int nt, int nl, const unsigned char *in)
{
    int n = nt * nl, i;
    memset(t, -1, (size_t)MAX_TUBES * MAX_LAYERS);
    for (i = 0; i < n; i++) {
        int v = (i & 1) ? in[i >> 1] >> 4 : in[i >> 1] & 15;
        t[i / nl][i % nl] = (signed char)(v - 1);
    }
}

static int solver_add(solver *s, const signed char t[][MAX_LAYERS], int parent,
                      const struct pour *m, int g)
{
    int id;
    if (s->n == s->cap) {
        s->cap *= 2;
        s->states = sresize(s->states, (size_t)s->cap * s->sbytes, unsigned char);
        s->parent = sresize(s->parent, s->cap, int);
        s->mv = sresize(s->mv, (size_t)s->cap * 3, unsigned char);
        s->g = sresize(s->g, s->cap, int);
    }
    id = s->n++;
    pack_board(t, s->nt, s->nl, s->states + (size_t)id * s->sbytes);
    s->parent[id] = parent;
    if (m) {
        s->mv[id * 3] = m->from; s->mv[id * 3 + 1] = m->to; s->mv[id * 3 + 2] = m->n;
    } else {
        s->mv[id * 3] = s->mv[id * 3 + 1] = s->mv[id * 3 + 2] = 0;
    }
    s->g[id] = g;
    return id;
}

static bool hent_less(const struct hent *a, const struct hent *b)
{
    if (a->f != b->f) return a->f < b->f;
    if (a->h != b->h) return a->h < b->h;
    return a->seq > b->seq;            /* the newest first */
}

static void heap_push(solver *s, int f, int h, int node)
{
    int i;
    struct hent e;
    if (s->nheap == s->capheap) {
        s->capheap *= 2;
        s->heap = sresize(s->heap, s->capheap, struct hent);
    }
    e.f = f; e.h = h; e.node = node; e.seq = s->seq++;
    i = s->nheap++;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (!hent_less(&e, &s->heap[p])) break;
        s->heap[i] = s->heap[p];
        i = p;
    }
    s->heap[i] = e;
}

static struct hent heap_pop(solver *s)
{
    struct hent top = s->heap[0], e = s->heap[--s->nheap];
    int i = 0;
    for (;;) {
        int c = 2 * i + 1;
        if (c >= s->nheap) break;
        if (c + 1 < s->nheap && hent_less(&s->heap[c + 1], &s->heap[c])) c++;
        if (!hent_less(&s->heap[c], &e)) break;
        s->heap[i] = s->heap[c];
        i = c;
    }
    if (s->nheap > 0) s->heap[i] = e;
    return top;
}

#define HEURISTIC_WEIGHT 2

/*
 * Looks for a way to finish the game from a position.  Returns the number of
 * pours of the solution (stored in *out, to be freed; 0 if the position is
 * finished already), -1 if there is no solution (the whole search space was
 * looked at) or -2 if the search was given up because of the limit.
 * Positions in hist are not used by the solution.
 */
static int solve_search(const signed char start[][MAX_LAYERS], int nt, int nl, long nodelimit,
                        const uint64_t *hist, int nhist, struct pour **out)
{
    solver s;
    signed char cur[MAX_TUBES][MAX_LAYERS], nb[MAX_TUBES][MAX_LAYERS];
    uint64_t th[MAX_TUBES], hsum, roothash;
    struct pour moves[MAX_TUBES * MAX_TUBES];
    int h0 = heuristic(start, nt, nl), i, result = -1;

    if (out) *out = NULL;
    if (h0 == 0) return 0;

    solver_init(&s, nt, nl);
    solver_add(&s, start, -1, NULL, 0);
    roothash = board_hash(start, nt, nl);
    hs_insert(&s, roothash);
    for (i = 0; i < nhist; i++)
        if (hist[i] != roothash) hs_insert(&s, hist[i]);
    heap_push(&s, HEURISTIC_WEIGHT * h0, h0, 0);

    while (s.nheap > 0) {
        struct hent e = heap_pop(&s);
        int nm, mi;
        unpack_board(cur, nt, nl, s.states + (size_t)e.node * s.sbytes);
        hsum = 0;
        for (i = 0; i < nt; i++) {
            th[i] = tube_hash(cur[i], nl);
            hsum += th[i];
        }
        nm = gen_pours(cur, nt, nl, moves);
        for (mi = 0; mi < nm; mi++) {
            const struct pour *m = &moves[mi];
            uint64_t ch;
            int h, id;
            memcpy(nb, cur, sizeof(nb));
            pour_layers(nb, nl, m->from, m->to, m->n);
            ch = hsum - th[m->from] - th[m->to] + tube_hash(nb[m->from], nl) + tube_hash(nb[m->to], nl);
            if (!hs_insert(&s, ch)) continue;
            h = heuristic(nb, nt, nl);
            id = solver_add(&s, nb, e.node, m, s.g[e.node] + 1);
            if (h == 0) {
                int cnt = 0, x;
                struct pour *sol;
                for (x = id; s.parent[x] >= 0; x = s.parent[x]) cnt++;
                sol = snewn(cnt, struct pour);
                for (x = id, i = cnt - 1; s.parent[x] >= 0; x = s.parent[x], i--) {
                    sol[i].from = s.mv[x * 3];
                    sol[i].to = s.mv[x * 3 + 1];
                    sol[i].n = s.mv[x * 3 + 2];
                }
                if (out) *out = sol; else sfree(sol);
                result = cnt;
                goto done;
            }
            heap_push(&s, s.g[id] + HEURISTIC_WEIGHT * h, h, id);
        }
        if (s.n >= nodelimit) {
            result = -2;
            goto done;
        }
    }
    result = -1;
  done:
    solver_free(&s);
    return result;
}

/* ----------------------------------------------------------------------
 * Parameters.
 */

static void free_game(game_state *state)
{
    sfree(state);
}

static void free_ui(game_ui *ui)
{
    sfree(ui);
}

static void free_params(game_params *params)
{
    sfree(params);
}

static game_params *default_params(void)
{
    game_params *params = snew(game_params);

    params->ncolours = 7;
    params->ntubes = 9;
    params->nlayers = 4;
    params->hiddenlayers = false;

    return params;
}

static const struct {
    const char *name;
    int ncolours, ntubes, nlayers;
    bool hidden;
} presets[] = {
    { "4 colours, 6 tubes", 4, 6, 4, false },
    { "7 colours, 9 tubes", 7, 9, 4, false },
    { "10 colours, 12 tubes", 10, 12, 4, false },
    { "12 colours, 14 tubes, 5 layers", 12, 14, 5, false },
    { "7 colours, 8 tubes", 7, 8, 4, false },
    { "7 colours, 9 tubes, hidden layers", 7, 9, 4, true },
};

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;

    if (i < 0 || i >= (int)lenof(presets)) return false;
    ret = snew(game_params);
    ret->ncolours = presets[i].ncolours;
    ret->ntubes = presets[i].ntubes;
    ret->nlayers = presets[i].nlayers;
    ret->hiddenlayers = presets[i].hidden;
    *name = dupstr(presets[i].name);
    *params = ret;
    return true;
}

static game_params *dup_params(const game_params *params)
{
    game_params *ret = snew(game_params);
    *ret = *params;
    return ret;
}

/*
 * "7" is 7 colours in 9 tubes of 4 layers, "5w7" is 7 colours of 5 layers
 * each (in 9 tubes), "9x4w7" is 7 colours in 9 tubes of 4 layers.  An "h"
 * at the end is for hidden layers.
 */
static void decode_params(game_params *params, char const *string)
{
    int first = atoi(string), second;
    bool have_tubes = false, have_colours = false;

    params->ncolours = 0;
    params->ntubes = 0;
    params->nlayers = 4;
    params->hiddenlayers = false;
    while (*string && isdigit((unsigned char)*string)) string++;
    if (*string == 'x') {
        string++;
        params->ntubes = first;
        have_tubes = true;
        params->nlayers = atoi(string);
        while (*string && isdigit((unsigned char)*string)) string++;
        if (*string == 'w') {
            string++;
            params->ncolours = atoi(string);
            have_colours = true;
            while (*string && isdigit((unsigned char)*string)) string++;
        }
    } else if (*string == 'w') {
        string++;
        second = atoi(string);
        params->nlayers = first;
        params->ncolours = second;
        have_colours = true;
        while (*string && isdigit((unsigned char)*string)) string++;
    } else {
        params->ncolours = first;
        have_colours = true;
    }
    if (*string == 'h') params->hiddenlayers = true;
    if (!have_tubes) params->ntubes = params->ncolours + 2;
    if (!have_colours) params->ncolours = params->ntubes - 2;
}

static char *encode_params(const game_params *params, bool full)
{
    char data[256];

    if (full || params->ncolours + 2 != params->ntubes)
        sprintf(data, "%dx%dw%d", params->ntubes, params->nlayers, params->ncolours);
    else if (params->nlayers != 4)
        sprintf(data, "%dw%d", params->nlayers, params->ncolours);
    else
        sprintf(data, "%d", params->ncolours);

    if (params->hiddenlayers)
        strcat(data, "h");

    return dupstr(data);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(5, config_item);

    ret[0].name = "Colours";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->ncolours);
    ret[0].u.string.sval = dupstr(buf);

    ret[1].name = "Tubes";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->ntubes);
    ret[1].u.string.sval = dupstr(buf);

    ret[2].name = "Layers";
    ret[2].type = C_STRING;
    sprintf(buf, "%d", params->nlayers);
    ret[2].u.string.sval = dupstr(buf);

    ret[3].name = "Hide the layers below the top";
    ret[3].type = C_BOOLEAN;
    ret[3].u.boolean.bval = params->hiddenlayers;

    ret[4].name = NULL;
    ret[4].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *params = snew(game_params);

    params->ncolours = atoi(cfg[0].u.string.sval);
    params->ntubes = atoi(cfg[1].u.string.sval);
    params->nlayers = atoi(cfg[2].u.string.sval);
    params->hiddenlayers = cfg[3].u.boolean.bval;

    return params;
}

static const char *validate_params(const game_params *params, bool full)
{
    if (params->ncolours < 2) return "Colours must be at least 2";
    if (params->nlayers < 2) return "Layers must be at least 2";
    if (params->ncolours > MAX_COLOURS) return "Colours must be at most 12";
    if (params->nlayers > MAX_LAYERS) return "Layers must be at most 8";
    if (params->ntubes > MAX_TUBES) return "Tubes must be at most 16";
    if (params->ntubes <= params->ncolours)
        return "There must be more tubes than colours";
    return NULL;
}

/* ----------------------------------------------------------------------
 * Game descriptions: the tubes, separated by commas, each as the colours (0 to b) from the
 * bottom to the top.  Tubes that are empty have nothing between the commas.
 */

static char *tubes_desc(const signed char t[][MAX_LAYERS], int nt, int nl)
{
    char *buf = snewn(nt * (nl + 1) + 1, char);
    int i, j, c = 0;
    for (i = 0; i < nt; i++) {
        if (i) buf[c++] = ',';
        for (j = 0; j < nl && t[i][j] >= 0; j++)
            buf[c++] = HEX[(int)t[i][j]];
    }
    buf[c] = '\0';
    return buf;
}

/* Scrambles a finished game with pours that run backwards (the reverse of a pour that was
 * possible): the game that comes out can be solved by those pours, in reverse order. */
static void scramble_backwards(signed char t[][MAX_LAYERS], int nt, int nl, random_state *rs,
                               int steps, struct pour *log, int *nlog)
{
    struct pour *cand = snewn(MAX_TUBES * MAX_TUBES * MAX_LAYERS, struct pour);
    int s, n;

    if (nlog) *nlog = 0;
    for (s = 0; s < steps; s++) {
        int b, a, k, pick;
        n = 0;
        for (b = 0; b < nt; b++) {
            int c, tb, nb;
            tube_top(t[b], nl, &c, &tb, &nb);
            if (nb == 0) continue;
            for (k = 1; k <= tb; k++) {
                if (!(k < tb || nb == tb)) continue;
                for (a = 0; a < nt; a++) {
                    int na;
                    if (a == b) continue;
                    na = tube_count(t[a], nl);
                    if (nl - na < k) continue;
                    if (na > 0 && t[a][na - 1] == c && nb != nl) continue;
                    cand[n].from = (unsigned char)b;
                    cand[n].to = (unsigned char)a;
                    cand[n].n = (unsigned char)k;
                    n++;
                }
            }
        }
        if (!n) break;
        pick = random_upto(rs, n);
        {
            int c = t[cand[pick].from][tube_count(t[cand[pick].from], nl) - 1];
            int i, bn = tube_count(t[cand[pick].from], nl), an = tube_count(t[cand[pick].to], nl);
            for (i = 0; i < cand[pick].n; i++) t[cand[pick].from][bn - 1 - i] = -1;
            for (i = 0; i < cand[pick].n; i++) t[cand[pick].to][an + i] = (signed char)c;
            if (log && nlog) {
                /* the pour that undoes it */
                log[*nlog].from = cand[pick].to;
                log[*nlog].to = cand[pick].from;
                log[*nlog].n = cand[pick].n;
                (*nlog)++;
            }
        }
    }
    sfree(cand);
}

static int count_complete(const signed char t[][MAX_LAYERS], int nt, int nl)
{
    int i, j, n = 0;
    for (i = 0; i < nt; i++) {
        if (tube_count(t[i], nl) != nl) continue;
        for (j = 1; j < nl; j++)
            if (t[i][j] != t[i][0]) break;
        if (j == nl) n++;
    }
    return n;
}

static char *new_game_desc(const game_params *params, random_state *rs,
                           char **aux, bool interactive)
{
    int nt = params->ntubes, nl = params->nlayers, nc = params->ncolours;
    signed char t[MAX_TUBES][MAX_LAYERS];
    int units[MAX_COLOURS * MAX_LAYERS];
    int attempt, i, j;

    *aux = NULL;

    for (attempt = 0; attempt < GEN_ATTEMPTS; attempt++) {
        int res;
        for (i = 0; i < nc; i++)
            for (j = 0; j < nl; j++)
                units[i * nl + j] = i;
        shuffle(units, nc * nl, sizeof(int), rs);
        memset(t, -1, sizeof(t));
        for (i = 0; i < nc; i++)
            for (j = 0; j < nl; j++)
                t[i][j] = (signed char)units[i * nl + j];
        /* a tube that is done already makes the game easier; try again unless it is hard to avoid */
        if (count_complete(t, nt, nl) > 0 && attempt < GEN_ATTEMPTS / 2) continue;
        res = solve_search(t, nt, nl, GEN_NODES, NULL, 0, NULL);
        if (res > 0 || (res == 0 && attempt == GEN_ATTEMPTS - 1))
            return tubes_desc(t, nt, nl);
    }

    /* the games that are dealt are too hard for the solver to tell: scramble a finished one */
    for (attempt = 0; attempt < 100; attempt++) {
        memset(t, -1, sizeof(t));
        for (i = 0; i < nc; i++)
            for (j = 0; j < nl; j++)
                t[i][j] = (signed char)i;
        scramble_backwards(t, nt, nl, rs, 14 * nc * nl, NULL, NULL);
        if (tubes_solved(t, nt, nl)) continue;
        if (count_complete(t, nt, nl) > 0 && attempt < 90) continue;
        break;
    }
    return tubes_desc(t, nt, nl);
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    int tube = 0, layer = 0, c;
    int amounts[MAX_COLOURS];

    for (c = 0; c < MAX_COLOURS; c++) amounts[c] = 0;

    for (; *desc; desc++) {
        char token = *desc;
        if (token == ',') {
            tube++;
            if (tube >= params->ntubes)
                return "Too many tubes";
            layer = 0;
        } else {
            if (token >= '0' && token <= '9') c = token - '0';
            else if (token >= 'a' && token <= 'f') c = token - 'a' + 10;
            else if (token >= 'A' && token <= 'F') c = token - 'A' + 10;
            else return "Invalid character in game description";
            if (layer >= params->nlayers)
                return "Too many layers in a tube";
            if (c >= params->ncolours)
                return "Invalid colour in game description";
            if (++amounts[c] > params->nlayers)
                return "Too many layers of the same colour";
            layer++;
        }
    }
    if (tube + 1 < params->ntubes)
        return "Not enough tubes in game description";
    for (c = 0; c < params->ncolours; c++)
        if (amounts[c] < params->nlayers)
            return "Not enough layers of at least one colour";

    return NULL;
}

/* what can be seen of a tube with hidden layers: the layers of the colour on top (and what has been seen before) */
static void reveal_top_block(game_state *state, int t)
{
    int c, b, n, i;
    tube_top(state->tubes[t], state->p.nlayers, &c, &b, &n);
    state->known[t] = (unsigned char)(state->known[t] & ((1 << n) - 1));
    for (i = n - b; i < n; i++) state->known[t] |= (unsigned char)(1 << i);
}

static void reveal_tops(game_state *state)
{
    int t;
    for (t = 0; t < state->p.ntubes; t++) reveal_top_block(state, t);
}

static game_state *new_game(midend *me, const game_params *params, const char *desc)
{
    game_state *state = snew(game_state);
    int tube = 0, layer = 0;

    memset(state, 0, sizeof(*state));
    state->p = *params;
    memset(state->tubes, -1, sizeof(state->tubes));

    for (; *desc; desc++) {
        char token = *desc;
        if (token == ',') {
            tube++;
            layer = 0;
        } else if (tube < params->ntubes && layer < params->nlayers) {
            int c;
            if (token >= '0' && token <= '9') c = token - '0';
            else if (token >= 'a' && token <= 'f') c = token - 'a' + 10;
            else c = token - 'A' + 10;
            state->tubes[tube][layer++] = (signed char)c;
        }
    }
    reveal_tops(state);
    state->solved = tubes_solved(state->tubes, params->ntubes, params->nlayers);
    return state;
}

static game_state *dup_game(const game_state *state)
{
    game_state *ret = snew(game_state);
    *ret = *state;
    return ret;
}

/* ----------------------------------------------------------------------
 * Moves: "p <from> <to> <number>", several of them separated by ";".
 */

static void push_hist(game_state *state)
{
    uint64_t h = board_hash(state->tubes, state->p.ntubes, state->p.nlayers);
    if (state->nhist == HISTLEN) {
        memmove(state->hist, state->hist + 1, (HISTLEN - 1) * sizeof(uint64_t));
        state->nhist--;
    }
    state->hist[state->nhist++] = h;
}

static void make_pour(game_state *state, int from, int to, int k)
{
    push_hist(state);
    pour_layers(state->tubes, state->p.nlayers, from, to, k);
    reveal_top_block(state, from);
    reveal_top_block(state, to);
    state->moves++;
}

static game_state *execute_move(const game_state *state, const char *move)
{
    game_state *ret = dup_game(state);
    const char *p = move;
    int nl = state->p.nlayers, nt = state->p.ntubes, npours = 0;

    if (state->solved) {
        free_game(ret);
        return NULL;
    }
    for (;;) {
        int from, to, n, used = 0;
        while (*p == ' ' || *p == ';') p++;
        if (!*p) break;
        if (*p != 'p' || sscanf(p, "p %d %d %d%n", &from, &to, &n, &used) < 3 || used <= 0 ||
            from < 0 || from >= nt || to < 0 || to >= nt ||
            n < 1 || pour_amount(ret->tubes, nl, from, to) != n) {
            free_game(ret);
            return NULL;
        }
        make_pour(ret, from, to, n);
        npours++;
        p += used;
    }
    if (!npours) {
        free_game(ret);
        return NULL;
    }
    ret->solved = tubes_solved(ret->tubes, nt, nl);
    return ret;
}

/* ----------------------------------------------------------------------
 * Solve and hint.
 */

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, const char **error)
{
    struct pour *sol = NULL;
    int n, i;
    char *ret, *q;

    if (currstate->solved) {
        *error = "The puzzle is solved already";
        return NULL;
    }
    n = solve_search(currstate->tubes, currstate->p.ntubes, currstate->p.nlayers, SOLVE_NODES,
                     NULL, 0, &sol);
    if (n < 0) {
        *error = n == -1 ? "This position cannot be solved; undo some moves" :
            "No solution was found in time; undo some moves";
        return NULL;
    }
    ret = snewn(n * 14 + 1, char);
    q = ret;
    for (i = 0; i < n; i++)
        q += sprintf(q, "%sp %d %d %d", i ? ";" : "", sol[i].from, sol[i].to, sol[i].n);
    sfree(sol);
    return ret;
}

static char *hint_game(const game_state *orig, const game_state *curr, const char **error)
{
    struct pour *sol = NULL;
    int n;
    char buf[32];

    if (curr->solved) {
        *error = "The puzzle is solved already";
        return NULL;
    }
    /* never lead the player round in circles: stay away from the positions that came before */
    n = solve_search(curr->tubes, curr->p.ntubes, curr->p.nlayers, HINT_NODES,
                     curr->hist, curr->nhist, &sol);
    if (n < 0)
        n = solve_search(curr->tubes, curr->p.ntubes, curr->p.nlayers, HINT_NODES, NULL, 0, &sol);
    if (n < 0) {
        *error = n == -1 ? "This position cannot be solved; undo some moves" :
            "No solution was found in time; undo some moves";
        return NULL;
    }
    if (n == 0) {
        *error = "The puzzle is solved already";
        return NULL;
    }
    sprintf(buf, "p %d %d %d", sol[0].from, sol[0].to, sol[0].n);
    sfree(sol);
    return dupstr(buf);
}

/* ----------------------------------------------------------------------
 * Text, UI.
 */

static bool layer_known(const game_state *state, int tube, int layer)
{
    return !state->p.hiddenlayers || ((state->known[tube] >> layer) & 1);
}

static bool game_can_format_as_text_now(const game_params *params)
{
    return true;
}

static char *game_text_format(const game_state *state)
{
    int nt = state->p.ntubes, nl = state->p.nlayers;
    char *buf = snewn(nl * nt * 2 + 1, char);
    int tube, layer, c = 0;
    for (layer = nl - 1; layer >= 0; layer--) {
        for (tube = 0; tube < nt; tube++) {
            if (state->tubes[tube][layer] < 0)
                buf[c++] = '_';
            else if (!layer_known(state, tube, layer))
                buf[c++] = '?';
            else
                buf[c++] = HEX[(int)state->tubes[tube][layer]];
            buf[c++] = ' ';
        }
        buf[c - 1] = '\n';
    }
    buf[c] = 0;
    return buf;
}

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);
    ui->selected = -1;
    ui->cursor = 0;
    ui->cursor_visible = false;
    ui->labels = LABELS_NONE;
    return ui;
}

static config_item *get_prefs(game_ui *ui)
{
    config_item *ret = snewn(N_PREF_ITEMS + 1, config_item);

    ret[PREF_LABELS].name = "Labels on the layers";
    ret[PREF_LABELS].kw = "labels";
    ret[PREF_LABELS].type = C_CHOICES;
    ret[PREF_LABELS].u.choices.choicenames = ":None:Numbers:Letters";
    ret[PREF_LABELS].u.choices.choicekws = ":none:numbers:letters";
    ret[PREF_LABELS].u.choices.selected = ui->labels;

    ret[N_PREF_ITEMS].name = NULL;
    ret[N_PREF_ITEMS].type = C_END;
    return ret;
}

static void set_prefs(game_ui *ui, const config_item *cfg)
{
    ui->labels = cfg[PREF_LABELS].u.choices.selected;
}

static key_label *game_request_keys(const game_params *params, int *nkeys, int *arrow_mode)
{
    *nkeys = 0;
    *arrow_mode = ANDROID_ARROWS_LEFT;
    return NULL;
}

static bool game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
    ui->selected = -1;
    return oldstate && newstate && !oldstate->solved && newstate->solved;
}

struct game_drawstate {
    int tilesize;
    bool started;
    int labels;
    struct tcache {
        signed char layer[MAX_LAYERS]; /* what is shown: -1 nothing, -2 not known, else the colour */
        signed char sel, cur, flash;
    } cache[MAX_TUBES];
    char status[64];
};

/* ---- the layout of the tubes ---- */

#define WALL(ts)    ((ts) / 12 + 1)
#define GAP(ts)     ((ts) / 2)
#define MARGIN(ts)  ((ts) / 2 + 2)
#define TUBEW(ts)   ((ts) + 2 * WALL(ts))
#define ROWH(p, ts) (((p)->nlayers + 1) * (ts) + 2 * WALL(ts))

static void tube_layout(const game_params *p, int *cols, int *rows)
{
    if (p->ntubes > WRAP_TUBES) {
        *cols = (p->ntubes + 1) / 2;
        *rows = 2;
    } else {
        *cols = p->ntubes;
        *rows = 1;
    }
}

static void game_compute_size(const game_params *params, int tilesize,
                              const game_ui *ui, int *x, int *y)
{
    int cols, rows;
    tube_layout(params, &cols, &rows);
    *x = 2 * MARGIN(tilesize) + cols * TUBEW(tilesize) + (cols - 1) * GAP(tilesize);
    *y = 2 * MARGIN(tilesize) + rows * ROWH(params, tilesize) + (rows - 1) * GAP(tilesize);
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
    ds->started = false;
}

/* the space of a tube (with the room that the tube needs when it is lifted) */
static void tube_slot(const game_params *p, int ts, int tube, int *x, int *y, int *w, int *h)
{
    int cols, rows;
    tube_layout(p, &cols, &rows);
    *x = MARGIN(ts) + (tube % cols) * (TUBEW(ts) + GAP(ts));
    *y = MARGIN(ts) + (tube / cols) * (ROWH(p, ts) + GAP(ts));
    *w = TUBEW(ts);
    *h = ROWH(p, ts);
}

static int tube_at(const game_state *state, const game_drawstate *ds, int x, int y)
{
    int ts = ds->tilesize, cols, rows, col, row, tube;
    int px = TUBEW(ts) + GAP(ts), py = ROWH(&state->p, ts) + GAP(ts);

    tube_layout(&state->p, &cols, &rows);
    x -= MARGIN(ts) - GAP(ts) / 2;
    y -= MARGIN(ts) - GAP(ts) / 2;
    if (x < 0 || y < 0) return -1;
    col = x / px;
    row = y / py;
    if (col >= cols || row >= rows) return -1;
    tube = row * cols + col;
    return tube < state->p.ntubes ? tube : -1;
}

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    int tube = -1, k;
    int nt = state->p.ntubes, nl = state->p.nlayers;
    bool deselect_only = false;

    button &= ~MOD_MASK;
    if (state->solved) return MOVE_UNUSED;

    if (button == LEFT_BUTTON) {
        ui->cursor_visible = false;
        tube = tube_at(state, ds, x, y);
        if (tube < 0) {
            if (ui->selected >= 0) {
                ui->selected = -1;
                return MOVE_UI_UPDATE;
            }
            return MOVE_NO_EFFECT;
        }
    } else if (button == LEFT_DRAG || button == LEFT_RELEASE ||
               button == RIGHT_DRAG || button == RIGHT_RELEASE) {
        return MOVE_NO_EFFECT;
    } else if (button == RIGHT_BUTTON) {
        if (ui->selected >= 0) {
            ui->selected = -1;
            return MOVE_UI_UPDATE;
        }
        return MOVE_NO_EFFECT;
    } else if (IS_CURSOR_MOVE(button)) {
        int cols, rows, cur = ui->cursor;
        if (!ui->cursor_visible) {
            ui->cursor_visible = true;
            return MOVE_UI_UPDATE;
        }
        tube_layout(&state->p, &cols, &rows);
        if (button == CURSOR_LEFT) cur = (cur + nt - 1) % nt;
        else if (button == CURSOR_RIGHT) cur = (cur + 1) % nt;
        else if (rows == 2) {
            if (cur < cols) { if (cur + cols < nt) cur += cols; }
            else cur -= cols;
        }
        if (cur == ui->cursor) return MOVE_NO_EFFECT;
        ui->cursor = cur;
        return MOVE_UI_UPDATE;
    } else if (IS_CURSOR_SELECT(button)) {
        if (!ui->cursor_visible) {
            ui->cursor_visible = true;
            return MOVE_UI_UPDATE;
        }
        tube = ui->cursor;
        deselect_only = button == CURSOR_SELECT2;
    } else {
        return MOVE_UNUSED;
    }

    if (deselect_only) {
        if (ui->selected < 0) return MOVE_NO_EFFECT;
        ui->selected = -1;
        return MOVE_UI_UPDATE;
    }
    if (ui->selected == tube) {
        ui->selected = -1;
        return MOVE_UI_UPDATE;
    }
    if (ui->selected < 0) {
        if (tube_count(state->tubes[tube], nl) == 0) return MOVE_NO_EFFECT;
        ui->selected = tube;
        return MOVE_UI_UPDATE;
    }
    k = pour_amount(state->tubes, nl, ui->selected, tube);
    if (k) {
        char buf[32];
        sprintf(buf, "p %d %d %d", ui->selected, tube, k);
        ui->selected = -1;
        ui->cursor = tube;
        return dupstr(buf);
    }
    /* not possible: the tube that was touched is the one that is selected now */
    ui->selected = tube_count(state->tubes[tube], nl) > 0 ? tube : -1;
    return MOVE_UI_UPDATE;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);
    int c, i;

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);
    for (i = 0; i < 3; i++) {
        ret[COL_TUBE * 3 + i] = 0.25F;
        ret[COL_HIDDEN * 3 + i] = 0.62F;
        ret[COL_LABEL_DARK * 3 + i] = 0.10F;
        ret[COL_LABEL_LIGHT * 3 + i] = 1.0F;
    }
    ret[COL_CURSOR * 3 + 0] = 0.0F;
    ret[COL_CURSOR * 3 + 1] = 0.45F;
    ret[COL_CURSOR * 3 + 2] = 1.0F;
    for (c = 0; c < MAX_COLOURS; c++) {
        ret[(COL_0 + c) * 3 + 0] = (float)((palette[c] >> 16) & 0xff) / 255.0F;
        ret[(COL_0 + c) * 3 + 1] = (float)((palette[c] >> 8) & 0xff) / 255.0F;
        ret[(COL_0 + c) * 3 + 2] = (float)(palette[c] & 0xff) / 255.0F;
    }

    *ncolours = NCOLOURS;
    return ret;
}

static bool label_is_light(int colour)
{
    unsigned long p = palette[colour];
    double y = 0.299 * ((p >> 16) & 0xff) + 0.587 * ((p >> 8) & 0xff) + 0.114 * (p & 0xff);
    return y < 128.0;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    game_drawstate *ds = snew(game_drawstate);
    memset(ds, 0, sizeof(*ds));
    ds->tilesize = 0;
    ds->started = false;
    ds->labels = -1;
    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds);
}

/*
 * When a pour is on the screen (the two states differ in two tubes only): which
 * tube loses layers to which, and how many.
 */
static bool find_pour(const game_state *os, const game_state *ns, int *src, int *dst, int *n)
{
    int nt = ns->p.ntubes, nl = ns->p.nlayers, t, changed = 0, a = -1, b = -1;

    if (!os || os->p.ntubes != nt || os->p.nlayers != nl) return false;
    for (t = 0; t < nt; t++) {
        if (memcmp(os->tubes[t], ns->tubes[t], nl) != 0) {
            if (++changed > 2) return false;
            if (a < 0) a = t; else b = t;
        }
    }
    if (changed != 2) return false;
    {
        int ca = tube_count(os->tubes[a], nl), cb = tube_count(os->tubes[b], nl);
        int na = tube_count(ns->tubes[a], nl), nb = tube_count(ns->tubes[b], nl);
        if (ca - na > 0 && nb - cb == ca - na) { *src = a; *dst = b; *n = ca - na; return true; }
        if (cb - nb > 0 && na - ca == cb - nb) { *src = b; *dst = a; *n = cb - nb; return true; }
    }
    return false;
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    int src, dst, n;
    if (!find_pour(oldstate, newstate, &src, &dst, &n)) return 0.0F;
    return 0.12F + 0.07F * (float)n;
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate, int dir, game_ui *ui)
{
    if (dir > 0 && oldstate && !oldstate->solved && newstate->solved) return 0.6F;
    return 0.0F;
}

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
    if (ui->cursor_visible && ds->tilesize > 0)
        tube_slot(params, ds->tilesize, ui->cursor, x, y, w, h);
}

static int game_status(const game_state *state)
{
    return state->solved ? 1 : 0;
}

static void draw_tube(drawing *dr, game_drawstate *ds, const game_params *p, int tube,
                      const signed char *layers, bool selected, bool cursor, bool flash)
{
    int ts = ds->tilesize, nl = p->nlayers, i;
    int sx, sy, sw, sh, wall = WALL(ts), cut = ts / 5 > 2 ? ts / 5 : 2;
    int x0, gtop, gbot;
    int coords[12];
    int glass = (selected || flash) ? COL_CURSOR : COL_TUBE;

    tube_slot(p, ts, tube, &sx, &sy, &sw, &sh);
    draw_rect(dr, sx - 2, sy - 2, sw + 4, sh + 4, COL_BACKGROUND);

    x0 = sx;
    gbot = sy + sh - (selected ? ts : 0);
    gtop = gbot - (nl * ts + 2 * wall);

    /* the liquids */
    for (i = 0; i < nl; i++) {
        int y = gbot - wall - (i + 1) * ts;
        int col = layers[i], tc;
        if (col == -1) continue;
        draw_rect(dr, x0 + wall, y, ts, ts, col == -2 ? COL_HIDDEN : COL_0 + col);
        /* a thin line between two layers of different colours makes the layers easier to tell */
        if (i > 0 && layers[i - 1] != col)
            draw_rect(dr, x0 + wall, y + ts - 1, ts, 1, COL_TUBE);
        if (ds->labels != LABELS_NONE || col == -2) {
            char buf[4];
            if (col == -2) {
                strcpy(buf, "?");
                tc = COL_LABEL_DARK;
            } else {
                if (ds->labels == LABELS_NUMBERS) sprintf(buf, "%d", col + 1);
                else { buf[0] = (char)('A' + col); buf[1] = '\0'; }
                tc = label_is_light(col) ? COL_LABEL_LIGHT : COL_LABEL_DARK;
            }
            draw_text(dr, x0 + wall + ts / 2, y + ts / 2, FONT_VARIABLE,
                      (strlen(buf) > 1 ? ts * 2 / 5 : ts / 2), ALIGN_HCENTRE | ALIGN_VCENTRE, tc, buf);
        }
    }

    /* round the lower corners of the glass: cut the corners of the liquid away */
    {
        int yb = gbot - wall;
        coords[0] = x0 + wall; coords[1] = yb - cut;
        coords[2] = x0 + wall; coords[3] = yb;
        coords[4] = x0 + wall + cut; coords[5] = yb;
        draw_polygon(dr, coords, 3, COL_BACKGROUND, COL_BACKGROUND);
        coords[0] = x0 + wall + ts; coords[1] = yb - cut;
        coords[2] = x0 + wall + ts; coords[3] = yb;
        coords[4] = x0 + wall + ts - cut; coords[5] = yb;
        draw_polygon(dr, coords, 3, COL_BACKGROUND, COL_BACKGROUND);
    }

    /* the glass */
    {
        float th = (float)wall;
        float xl = (float)x0 + th / 2, xr = (float)(x0 + sw) - th / 2;
        float yb = (float)gbot - th / 2, yt = (float)gtop;
        float c = (float)cut + th / 2;
        draw_thick_line(dr, th, xl, yt, xl, yb - c, glass);
        draw_thick_line(dr, th, xl, yb - c, xl + c, yb, glass);
        draw_thick_line(dr, th, xl + c, yb, xr - c, yb, glass);
        draw_thick_line(dr, th, xr - c, yb, xr, yb - c, glass);
        draw_thick_line(dr, th, xr, yb - c, xr, yt, glass);
    }

    if (cursor)
        draw_rect_outline(dr, sx - 1, sy - 1, sw + 2, sh + 2, COL_CURSOR);

    draw_update(dr, sx - 2, sy - 2, sw + 4, sh + 4);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    const game_params *p = &state->p;
    int nt = p->ntubes, nl = p->nlayers, t, l;
    int src = -1, dst = -1, n = 0, moved = 0;
    int flash = flashtime > 0 ? ((int)(flashtime * 6.0F / 0.6F) % 2 == 0) : 0;
    char status[64];

    if (!ds->started || ds->labels != ui->labels) {
        int w, h;
        game_compute_size(p, ds->tilesize, ui, &w, &h);
        draw_rect(dr, 0, 0, w, h, COL_BACKGROUND);
        draw_update(dr, 0, 0, w, h);
        for (t = 0; t < MAX_TUBES; t++) ds->cache[t].sel = -3;
        ds->labels = ui->labels;
        ds->started = true;
    }

    if (animtime > 0 && find_pour(oldstate, state, &src, &dst, &n)) {
        float len = game_anim_length(oldstate, state, dir, NULL);
        moved = (int)(animtime / len * (float)n);
        if (moved < 0) moved = 0;
        if (moved > n) moved = n;
    } else {
        src = dst = -1;
    }

    for (t = 0; t < nt; t++) {
        struct tcache want;
        for (l = 0; l < MAX_LAYERS; l++) want.layer[l] = -1;
        if (src >= 0 && (t == src || t == dst)) {
            /* in the middle of a pour: some of the layers have moved already */
            int sn = tube_count(oldstate->tubes[src], nl), dn = tube_count(oldstate->tubes[dst], nl);
            int colour = oldstate->tubes[src][sn - 1];
            if (t == src) {
                for (l = 0; l < sn - moved; l++)
                    want.layer[l] = layer_known(oldstate, t, l) ? oldstate->tubes[t][l] : -2;
            } else {
                for (l = 0; l < dn; l++)
                    want.layer[l] = layer_known(oldstate, t, l) ? oldstate->tubes[t][l] : -2;
                for (; l < dn + moved; l++) want.layer[l] = (signed char)colour;
            }
        } else {
            for (l = 0; l < nl; l++) {
                if (state->tubes[t][l] < 0) continue;
                want.layer[l] = layer_known(state, t, l) ? state->tubes[t][l] : -2;
            }
        }
        want.sel = (ui->selected == t);
        want.cur = ui->cursor_visible && ui->cursor == t;
        want.flash = (signed char)flash;
        if (memcmp(&want, &ds->cache[t], sizeof(want)) != 0) {
            ds->cache[t] = want;
            draw_tube(dr, ds, p, t, want.layer, want.sel, want.cur, want.flash);
        }
    }

    if (state->solved)
        sprintf(status, "COMPLETED! Moves: %d", state->moves);
    else if (!has_pour(state->tubes, nt, nl))
        sprintf(status, "Moves: %d - no pour is possible, undo some moves", state->moves);
    else
        sprintf(status, "Moves: %d", state->moves);
    if (strcmp(status, ds->status) != 0) {
        strcpy(ds->status, status);
        status_bar(dr, status);
    }
}

#ifdef COMBINED
#define thegame decanting
#endif

const struct game thegame = {
    "Decanting", "games.decanting", "decanting",
    default_params,
    game_fetch_preset, NULL,
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
    true, game_can_format_as_text_now, game_text_format,
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
    PREFERRED_TILE_SIZE, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_get_cursor_location,
    game_status,
#ifndef NO_PRINTING
    false, false, NULL, NULL,          /* print_size, print */
#endif
    true,                              /* wants_statusbar */
    false, NULL,                       /* timing_state */
    0,                                 /* flags */
    hint_game,
};
