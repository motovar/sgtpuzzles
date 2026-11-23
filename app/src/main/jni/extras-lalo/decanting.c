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
 *  - three levels of difficulty: the generator deals a number of games that
 *    the solver can finish and keeps the one that took the solver the least
 *    work (Easy), a typical one (Normal) or the one that took the most
 *    (Hard); see "The levels of difficulty" below;
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
 *    animation of the pouring;
 *  - any number of colours, layers and tubes (the sketch stopped at 12, 8
 *    and 16): the tubes are stored in arrays of the size that the game needs,
 *    the colours after the twelfth are made up from their number, and a
 *    game with more than 16 colours is written with decimal numbers
 *    instead of one digit per layer.  What is still limited is the work
 *    that the solver does: it takes as many positions as fit in a fixed
 *    amount of memory (and time), so on a game that is too big for that
 *    "Solve" and "Show next step" may not find the way, and then only the
 *    game that was generated in the same session has its solution to show.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <limits.h>
#include <stdint.h>

#include "puzzles.h"

enum {
    HISTLEN = 48,                      /* positions that a state remembers */
    WRAP_TUBES = 5,                    /* more tubes than this go in two (or more) rows */
    PREFERRED_TILE_SIZE = 32
};

/*
 * What a game may take in memory: the number of cells (layers that a tube
 * can hold, over all the tubes).  This is not a limit of the game, it is
 * there so that the sizes of the arrays cannot overflow.
 */
#define MAX_CELLS (1 << 24)

/*
 * The search limits, in positions kept in memory; they are cut down for the
 * games where a position takes much room, or where it takes much work to
 * go through the moves of a position (see node_limit).
 */
#define HINT_NODES   400000L
#define SOLVE_NODES  1200000L
#define GEN_NODES    60000L
#define GEN_ATTEMPTS 5000              /* deals that are tried, at most */
#define GEN_CANDIDATES 48              /* solvable deals that one of them is chosen from */
#define GEN_MEMORY   4000000.0         /* layers that those deals may hold together, at most */
#define GEN_WORK     60000000.0        /* what all the searches of one game may do together (see node_limit) */
#define LOG_MAX_STEPS 4000000          /* the longest scramble that keeps its pours for "Solve" */
#define SEARCH_BYTES 200000000.0
#define SEARCH_WORK  300000000.0

enum {
    COL_BACKGROUND,
    COL_TUBE,
    COL_CURSOR,
    COL_HIDDEN,
    COL_LABEL_DARK,
    COL_LABEL_LIGHT,
    COL_0                              /* and as many more as the game has colours */
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

#define NFIXED_COLOURS 12
static const unsigned long palette[NFIXED_COLOURS] = {
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

/*
 * The number of colours of the liquids that the colour table has: it
 * follows the biggest game that has been started (the front end asks for the
 * colours when a game is started, never before), and it is never less than
 * the twelve of the palette.  npal_given is how many of them the front end
 * was given the last time that it asked (a game that has more colours than
 * that, because it was started after the front end asked, is drawn with the
 * colour of the same number modulo this, never with one that does not exist).
 */
static int npal = NFIXED_COLOURS;
static int npal_given = NFIXED_COLOURS;

static float hue_to_rgb(float p, float q, float t)
{
    if (t < 0.0F) t += 1.0F;
    if (t > 1.0F) t -= 1.0F;
    if (t < 1.0F / 6.0F) return p + (q - p) * 6.0F * t;
    if (t < 0.5F) return q;
    if (t < 2.0F / 3.0F) return p + (q - p) * (2.0F / 3.0F - t) * 6.0F;
    return p;
}

/* the colour of the liquid number c, as red, green and blue between 0 and 1 */
static void liquid_rgb(int c, float *rgb)
{
    if (c < NFIXED_COLOURS) {
        rgb[0] = (float)((palette[c] >> 16) & 0xff) / 255.0F;
        rgb[1] = (float)((palette[c] >> 8) & 0xff) / 255.0F;
        rgb[2] = (float)(palette[c] & 0xff) / 255.0F;
    } else {
        /* the hues that are a golden angle apart are never near one another for long; the
         * lightness and the saturation are changed as well, to have more colours to choose from */
        static const float sat[3] = { 0.80F, 0.55F, 0.70F };
        static const float lig[4] = { 0.50F, 0.64F, 0.38F, 0.56F };
        double ip;
        float h = (float)modf((double)c * 0.6180339887498949 + 0.13, &ip);
        float s = sat[(c / NFIXED_COLOURS) % 3], l = lig[(c / 3) % 4];
        float q = l < 0.5F ? l * (1.0F + s) : l + s - l * s, p = 2.0F * l - q;
        rgb[0] = hue_to_rgb(p, q, h + 1.0F / 3.0F);
        rgb[1] = hue_to_rgb(p, q, h);
        rgb[2] = hue_to_rgb(p, q, h - 1.0F / 3.0F);
    }
}

enum { DIFF_EASY, DIFF_NORMAL, DIFF_HARD, DIFFCOUNT };

static const char diff_chars[DIFFCOUNT + 1] = "enh";

struct game_params {
    int ncolours;
    int ntubes;
    int nlayers;
    bool hiddenlayers;
    int difficulty;
};

struct game_state {
    game_params p;
    int *tubes;                        /* ntubes * nlayers: the colours, bottom first, -1 for none */
    unsigned char *known;              /* the same: 1 if the layer has been seen */
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
 * Tubes: nl layers each, in an array of nt * nl whole numbers (a tube
 * is given by where it starts).
 */

static int tube_count(const int *t, int nl)
{
    int n = 0;
    while (n < nl && t[n] >= 0) n++;
    return n;
}

/* the colour on top (-1 if there is none), the layers of it, and the layers in all */
static void tube_top(const int *t, int nl, int *colour, int *block, int *count)
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
static int pour_amount(const int *tubes, int nl, int from, int to)
{
    int fc, fb, fn, tc, tb, tn, room;
    if (from == to) return 0;
    tube_top(tubes + (size_t)from * nl, nl, &fc, &fb, &fn);
    if (fn == 0) return 0;
    tube_top(tubes + (size_t)to * nl, nl, &tc, &tb, &tn);
    room = nl - tn;
    if (room <= 0) return 0;
    if (tn > 0 && tc != fc) return 0;
    return fb < room ? fb : room;
}

static void pour_layers(int *tubes, int nl, int from, int to, int k)
{
    int *f = tubes + (size_t)from * nl, *t = tubes + (size_t)to * nl;
    int fn = tube_count(f, nl), tn = tube_count(t, nl), c, i;
    c = f[fn - 1];
    for (i = 0; i < k; i++) f[fn - 1 - i] = -1;
    for (i = 0; i < k; i++) t[tn + i] = c;
}

static bool tubes_solved(const int *tubes, int nt, int nl)
{
    int i, j;
    for (i = 0; i < nt; i++) {
        const int *t = tubes + (size_t)i * nl;
        if (t[0] < 0) continue;
        for (j = 1; j < nl; j++)
            if (t[j] != t[0]) return false;
    }
    return true;
}

/* is there any pour at all?  A tube that is not empty, and another one that has room for it:
 * empty, or with the same colour on top. */
static bool has_pour(const int *tubes, int nt, int nl, int nc)
{
    int *room_top = snewn(nc > 0 ? nc : 1, int);
    int i, nempty = 0;
    bool found = false;

    memset(room_top, 0, (size_t)(nc > 0 ? nc : 1) * sizeof(int));
    for (i = 0; i < nt; i++) {
        const int *t = tubes + (size_t)i * nl;
        int n = tube_count(t, nl);
        if (n == 0) nempty++;
        else if (n < nl && t[n - 1] < nc) room_top[t[n - 1]]++;
    }
    for (i = 0; i < nt && !found; i++) {
        const int *t = tubes + (size_t)i * nl;
        int n = tube_count(t, nl), c, others;
        if (n == 0) continue;
        c = t[n - 1];
        others = nempty + (c < nc ? room_top[c] : 0) - (n < nl ? 1 : 0);
        if (others > 0) found = true;
    }
    sfree(room_top);
    return found;
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

static uint64_t tube_hash(const int *t, int nl)
{
    uint64_t h = 14695981039346656037ULL;
    int i;
    for (i = 0; i < nl; i++)
        h = (h ^ (uint64_t)(t[i] + 1)) * 1099511628211ULL;
    return mix64(h);
}

static uint64_t board_hash(const int *tubes, int nt, int nl)
{
    uint64_t h = 0;
    int i;
    for (i = 0; i < nt; i++) h += tube_hash(tubes + (size_t)i * nl, nl);
    return h;
}

/* ----------------------------------------------------------------------
 * The solver: a best-first search over the positions that can be reached.
 *
 * A position is stored with as few bits per layer as the colours need (a
 * nibble, a byte, two bytes or four).  Two positions that only differ in
 * the order of the tubes count as the same one, and the moves
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
    int from, to, n;
};

/* what the search needs to have at hand while it looks at a position */
struct work {
    int nt, nl, nc;
    int *cnt, *top, *blk;              /* nt each */
    unsigned *seen;                    /* nc: the colours that a count has met */
    unsigned stamp;
    struct pour *mv;                   /* the moves of the position that was looked at last */
    int mvcap;
};

static void work_init(struct work *w, int nt, int nl, int nc)
{
    w->nt = nt; w->nl = nl; w->nc = nc;
    w->cnt = snewn(nt, int);
    w->top = snewn(nt, int);
    w->blk = snewn(nt, int);
    w->seen = snewn(nc > 0 ? nc : 1, unsigned);
    memset(w->seen, 0, (size_t)(nc > 0 ? nc : 1) * sizeof(unsigned));
    w->stamp = 0;
    w->mvcap = 64;
    w->mv = snewn(w->mvcap, struct pour);
}

static void work_free(struct work *w)
{
    sfree(w->cnt); sfree(w->top); sfree(w->blk); sfree(w->seen); sfree(w->mv);
}

static void add_pour(struct work *w, int n, int from, int to, int k)
{
    if (n == w->mvcap) {
        w->mvcap *= 2;
        w->mv = sresize(w->mv, w->mvcap, struct pour);
    }
    w->mv[n].from = from;
    w->mv[n].to = to;
    w->mv[n].n = k;
}

/* the number of blocks of colour in all the tubes less the number of different colours at the
 * bottom of a tube: every block but the one that stays at the bottom has to be poured at least once */
static int heuristic(const int *t, struct work *w)
{
    int nt = w->nt, nl = w->nl, blocks = 0, colours = 0, i, j, n;
    if (++w->stamp == 0) {
        memset(w->seen, 0, (size_t)(w->nc > 0 ? w->nc : 1) * sizeof(unsigned));
        w->stamp = 1;
    }
    for (i = 0; i < nt; i++) {
        const int *ti = t + (size_t)i * nl;
        n = tube_count(ti, nl);
        if (!n) continue;
        if (w->seen[ti[0]] != w->stamp) {
            w->seen[ti[0]] = w->stamp;
            colours++;
        }
        blocks++;
        for (j = 1; j < n; j++)
            if (ti[j] != ti[j - 1]) blocks++;
    }
    return blocks - colours;
}

/* the moves of a position: how many, and they are in w->mv */
static int gen_pours(const int *t, struct work *w)
{
    int nt = w->nt, nl = w->nl;
    int *cnt = w->cnt, *top = w->top, *blk = w->blk;
    int i, j, k, n = 0, firstempty = -1;
    int bk = 0, bf = 0, bt = 0;

    for (i = 0; i < nt; i++) {
        tube_top(t + (size_t)i * nl, nl, &top[i], &blk[i], &cnt[i]);
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
        add_pour(w, 0, bf, bt, bk);
        return 1;
    }

    for (i = 0; i < nt; i++) {
        bool dup = false;
        if (!cnt[i]) continue;
        if (cnt[i] == nl && blk[i] == nl) continue;
        for (j = 0; j < i; j++)
            if (memcmp(t + (size_t)j * nl, t + (size_t)i * nl, (size_t)nl * sizeof(int)) == 0) {
                dup = true;
                break;
            }
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
                if (j2 != i && memcmp(t + (size_t)j2 * nl, t + (size_t)j * nl,
                                      (size_t)nl * sizeof(int)) == 0) {
                    dup = true;
                    break;
                }
            if (dup) continue;
            k = blk[i] < nl - cnt[j] ? blk[i] : nl - cnt[j];
            add_pour(w, n, i, j, k);
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
    int nt, nl, nc, bpc, sbytes;
    unsigned char *states;
    int *parent;
    struct pour *mv;                   /* the pour that led to the position */
    int *g;
    int n, cap;
    struct hent *heap;
    int nheap, capheap;
    uint64_t *hs;
    size_t hcap, hcount;
    unsigned seq;
} solver;

/* the bits that a layer takes in a stored position (the colour and one more for "no layer") */
static int bits_for(int nc)
{
    return nc + 1 <= 16 ? 4 : nc + 1 <= 256 ? 8 : nc + 1 <= 65536 ? 16 : 32;
}

/*
 * How many positions a search may keep: the number that is given, but not
 * more than what a fixed amount of memory holds (that is much more than
 * the games of the presets need) and not more than what can be gone
 * through in about as long as it takes to go through that number of
 * positions of such a game.
 */
static long node_limit(long nodes, int nt, int nl, int nc)
{
    double sbytes = ((double)nt * nl * bits_for(nc) + 7.0) / 8.0;
    double by_mem = SEARCH_BYTES / (sbytes + 64.0);
    double by_work = SEARCH_WORK / ((double)nt * nl + (double)nt * nt / 4.0 + 1.0);
    double lim = (double)nodes;

    if (by_mem < lim) lim = by_mem;
    if (by_work < lim) lim = by_work;
    if (lim < 2000.0) lim = 2000.0;
    return (long)lim;
}

static void solver_init(solver *s, int nt, int nl, int nc)
{
    memset(s, 0, sizeof(*s));
    s->nt = nt; s->nl = nl; s->nc = nc;
    s->bpc = bits_for(nc);
    s->sbytes = (int)(((size_t)nt * nl * (size_t)s->bpc + 7) / 8);
    s->cap = 1024;
    s->states = snewn((size_t)s->cap * s->sbytes, unsigned char);
    s->parent = snewn(s->cap, int);
    s->mv = snewn(s->cap, struct pour);
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

static void pack_board(const int *t, int n, int bpc, unsigned char *out)
{
    int i;
    switch (bpc) {
      case 4:
        memset(out, 0, (size_t)(n + 1) / 2);
        for (i = 0; i < n; i++) {
            int v = t[i] + 1;
            out[i >> 1] |= (unsigned char)((i & 1) ? v << 4 : v);
        }
        break;
      case 8:
        for (i = 0; i < n; i++) out[i] = (unsigned char)(t[i] + 1);
        break;
      case 16:
        for (i = 0; i < n; i++) {
            int v = t[i] + 1;
            out[2 * i] = (unsigned char)(v & 255);
            out[2 * i + 1] = (unsigned char)(v >> 8);
        }
        break;
      default:
        for (i = 0; i < n; i++) {
            unsigned v = (unsigned)(t[i] + 1);
            out[4 * i] = (unsigned char)(v & 255);
            out[4 * i + 1] = (unsigned char)((v >> 8) & 255);
            out[4 * i + 2] = (unsigned char)((v >> 16) & 255);
            out[4 * i + 3] = (unsigned char)((v >> 24) & 255);
        }
        break;
    }
}

static void unpack_board(int *t, int n, int bpc, const unsigned char *in)
{
    int i;
    switch (bpc) {
      case 4:
        for (i = 0; i < n; i++)
            t[i] = ((i & 1) ? in[i >> 1] >> 4 : in[i >> 1] & 15) - 1;
        break;
      case 8:
        for (i = 0; i < n; i++) t[i] = (int)in[i] - 1;
        break;
      case 16:
        for (i = 0; i < n; i++) t[i] = (int)(in[2 * i] | (in[2 * i + 1] << 8)) - 1;
        break;
      default:
        for (i = 0; i < n; i++)
            t[i] = (int)((unsigned)in[4 * i] | ((unsigned)in[4 * i + 1] << 8) |
                         ((unsigned)in[4 * i + 2] << 16) | ((unsigned)in[4 * i + 3] << 24)) - 1;
        break;
    }
}

static int solver_add(solver *s, const int *t, int parent, const struct pour *m, int g)
{
    int id;
    if (s->n == s->cap) {
        s->cap *= 2;
        s->states = sresize(s->states, (size_t)s->cap * s->sbytes, unsigned char);
        s->parent = sresize(s->parent, s->cap, int);
        s->mv = sresize(s->mv, s->cap, struct pour);
        s->g = sresize(s->g, s->cap, int);
    }
    id = s->n++;
    pack_board(t, s->nt * s->nl, s->bpc, s->states + (size_t)id * s->sbytes);
    s->parent[id] = parent;
    if (m) {
        s->mv[id] = *m;
    } else {
        s->mv[id].from = s->mv[id].to = s->mv[id].n = 0;
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
 * Positions in hist are not used by the solution.  If expanded is given,
 * it gets the number of positions whose moves were looked at: that is what
 * the levels of difficulty are measured by.
 */
static int solve_search(const int *start, int nt, int nl, int nc, long nodelimit,
                        const uint64_t *hist, int nhist, struct pour **out, long *expanded)
{
    solver s;
    struct work w;
    int ncell = nt * nl;
    int *cur = snewn(ncell, int), *nb = snewn(ncell, int);
    uint64_t *th = snewn(nt, uint64_t), hsum, roothash;
    int i, result = -1, h0;
    long nexp = 0;

    work_init(&w, nt, nl, nc);
    if (out) *out = NULL;
    h0 = heuristic(start, &w);
    if (h0 == 0) {
        result = 0;
        goto early;
    }

    solver_init(&s, nt, nl, nc);
    solver_add(&s, start, -1, NULL, 0);
    roothash = board_hash(start, nt, nl);
    hs_insert(&s, roothash);
    for (i = 0; i < nhist; i++)
        if (hist[i] != roothash) hs_insert(&s, hist[i]);
    heap_push(&s, HEURISTIC_WEIGHT * h0, h0, 0);

    while (s.nheap > 0) {
        struct hent e = heap_pop(&s);
        int nm, mi;
        nexp++;
        unpack_board(cur, ncell, s.bpc, s.states + (size_t)e.node * s.sbytes);
        hsum = 0;
        for (i = 0; i < nt; i++) {
            th[i] = tube_hash(cur + (size_t)i * nl, nl);
            hsum += th[i];
        }
        nm = gen_pours(cur, &w);
        for (mi = 0; mi < nm; mi++) {
            const struct pour *m = &w.mv[mi];
            uint64_t ch;
            int h, id;
            memcpy(nb, cur, (size_t)ncell * sizeof(int));
            pour_layers(nb, nl, m->from, m->to, m->n);
            ch = hsum - th[m->from] - th[m->to] +
                tube_hash(nb + (size_t)m->from * nl, nl) + tube_hash(nb + (size_t)m->to * nl, nl);
            if (!hs_insert(&s, ch)) continue;
            h = heuristic(nb, &w);
            id = solver_add(&s, nb, e.node, m, s.g[e.node] + 1);
            if (h == 0) {
                int cnt = 0, x;
                struct pour *sol;
                for (x = id; s.parent[x] >= 0; x = s.parent[x]) cnt++;
                sol = snewn(cnt, struct pour);
                for (x = id, i = cnt - 1; s.parent[x] >= 0; x = s.parent[x], i--)
                    sol[i] = s.mv[x];
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
  early:
    work_free(&w);
    sfree(cur); sfree(nb); sfree(th);
    if (expanded) *expanded = nexp;
    return result;
}

/* ----------------------------------------------------------------------
 * Parameters.
 */

static void free_game(game_state *state)
{
    sfree(state->tubes);
    sfree(state->known);
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
    params->difficulty = DIFF_NORMAL;

    return params;
}

static const struct {
    const char *name;
    int ncolours, ntubes, nlayers;
    bool hidden;
    int diff;
} presets[] = {
    { "4 colours, 6 tubes, Easy", 4, 6, 4, false, DIFF_EASY },
    { "4 colours, 6 tubes, Normal", 4, 6, 4, false, DIFF_NORMAL },
    { "4 colours, 6 tubes, Hard", 4, 6, 4, false, DIFF_HARD },
    { "7 colours, 9 tubes, Easy", 7, 9, 4, false, DIFF_EASY },
    { "7 colours, 9 tubes, Normal", 7, 9, 4, false, DIFF_NORMAL },
    { "7 colours, 9 tubes, Hard", 7, 9, 4, false, DIFF_HARD },
    { "10 colours, 12 tubes, Easy", 10, 12, 4, false, DIFF_EASY },
    { "10 colours, 12 tubes, Normal", 10, 12, 4, false, DIFF_NORMAL },
    { "10 colours, 12 tubes, Hard", 10, 12, 4, false, DIFF_HARD },
    { "12 colours, 14 tubes, 5 layers", 12, 14, 5, false, DIFF_NORMAL },
    { "7 colours, 8 tubes (one spare)", 7, 8, 4, false, DIFF_NORMAL },
    { "7 colours, 9 tubes, hidden layers", 7, 9, 4, true, DIFF_NORMAL },
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
    ret->difficulty = presets[i].diff;
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

/* a number from the digits that are there, clipped so that it cannot overflow */
static int parse_number(const char **string)
{
    long v = 0;
    while (**string && isdigit((unsigned char)**string)) {
        if (v < 100000000L) v = v * 10 + (**string - '0');
        (*string)++;
    }
    return (int)v;
}

/*
 * "7" is 7 colours in 9 tubes of 4 layers, "5w7" is 7 colours of 5 layers
 * each (in 9 tubes), "9x4w7" is 7 colours in 9 tubes of 4 layers.  An "h"
 * after that is for hidden layers, and "d" and a letter (e, n or h) for the
 * level of difficulty (Normal when it is not there).
 */
static void decode_params(game_params *params, char const *string)
{
    int first = parse_number(&string), second;
    bool have_tubes = false, have_colours = false;

    params->ncolours = 0;
    params->ntubes = 0;
    params->nlayers = 4;
    params->hiddenlayers = false;
    params->difficulty = DIFF_NORMAL;
    if (*string == 'x') {
        string++;
        params->ntubes = first;
        have_tubes = true;
        params->nlayers = parse_number(&string);
        if (*string == 'w') {
            string++;
            params->ncolours = parse_number(&string);
            have_colours = true;
        }
    } else if (*string == 'w') {
        string++;
        second = parse_number(&string);
        params->nlayers = first;
        params->ncolours = second;
        have_colours = true;
    } else {
        params->ncolours = first;
        have_colours = true;
    }
    if (*string == 'h') {
        params->hiddenlayers = true;
        string++;
    }
    if (*string == 'd') {
        const char *q = strchr(diff_chars, string[1]);
        if (string[1] && q) params->difficulty = (int)(q - diff_chars);
    }
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
    if (full && params->difficulty >= 0 && params->difficulty < DIFFCOUNT) {
        int n = (int)strlen(data);
        data[n] = 'd';
        data[n + 1] = diff_chars[params->difficulty];
        data[n + 2] = '\0';
    }

    return dupstr(data);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(6, config_item);

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

    ret[4].name = "Difficulty";
    ret[4].type = C_CHOICES;
    ret[4].u.choices.choicenames = ":Easy:Normal:Hard";
    ret[4].u.choices.selected = params->difficulty;

    ret[5].name = NULL;
    ret[5].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *params = snew(game_params);
    const char *s;

    s = cfg[0].u.string.sval; params->ncolours = parse_number(&s);
    s = cfg[1].u.string.sval; params->ntubes = parse_number(&s);
    s = cfg[2].u.string.sval; params->nlayers = parse_number(&s);
    params->hiddenlayers = cfg[3].u.boolean.bval;
    params->difficulty = cfg[4].u.choices.selected;

    return params;
}

static const char *validate_params(const game_params *params, bool full)
{
    if (params->ncolours < 2) return "Colours must be at least 2";
    if (params->nlayers < 2) return "Layers must be at least 2";
    if (params->ntubes <= params->ncolours)
        return "There must be more tubes than colours";
    if (params->difficulty < 0 || params->difficulty >= DIFFCOUNT)
        return "Unknown level of difficulty";
    if ((double)params->ntubes * (double)params->nlayers > (double)MAX_CELLS)
        return "The tubes would hold more than 16 million layers (the memory)";
    return NULL;
}

/* ----------------------------------------------------------------------
 * Game descriptions: the tubes, separated by commas, each as the colours from the bottom to the
 * top.  Tubes that are empty have nothing between the commas.  A colour is a digit of the base 16
 * (0 to f) when the game has no more than 16 colours; with more colours every layer is its number
 * in the decimal system, and the layers of a tube are separated by dots.
 */

static const char HEX[16] = "0123456789abcdef";

static char *tubes_desc(const int *t, int nt, int nl, int nc)
{
    char *buf = snewn((size_t)nt * nl * 12 + (size_t)nt + 1, char);
    int i, j, c = 0;
    for (i = 0; i < nt; i++) {
        if (i) buf[c++] = ',';
        for (j = 0; j < nl && t[(size_t)i * nl + j] >= 0; j++) {
            if (nc <= 16) {
                buf[c++] = HEX[t[(size_t)i * nl + j]];
            } else {
                if (j) buf[c++] = '.';
                c += sprintf(buf + c, "%d", t[(size_t)i * nl + j]);
            }
        }
    }
    buf[c] = '\0';
    return buf;
}

/*
 * Reads a description: NULL if it is right (and then the tubes are in out, which has room for nt *
 * nl whole numbers, if that is given), otherwise what is wrong with it.
 */
static const char *parse_desc(const game_params *p, const char *desc, int *out)
{
    int nt = p->ntubes, nl = p->nlayers, nc = p->ncolours;
    int tube = 0, layer = 0, c;
    int *amounts = snewn(nc, int);
    const char *err = NULL;
    bool hex = nc <= 16, need_digit = false;
    int i;

    for (i = 0; i < nc; i++) amounts[i] = 0;
    if (out)
        for (i = 0; i < nt * nl; i++) out[i] = -1;

    while (*desc) {
        char token = *desc;
        if (token == ',') {
            if (need_digit) { err = "A colour is missing after a dot"; goto done; }
            tube++;
            if (tube >= nt) { err = "Too many tubes"; goto done; }
            layer = 0;
            desc++;
            continue;
        }
        if (hex) {
            if (token >= '0' && token <= '9') c = token - '0';
            else if (token >= 'a' && token <= 'f') c = token - 'a' + 10;
            else if (token >= 'A' && token <= 'F') c = token - 'A' + 10;
            else { err = "Invalid character in game description"; goto done; }
            desc++;
        } else {
            long v = 0;
            if (token == '.') {
                if (layer == 0 || need_digit) { err = "Invalid character in game description"; goto done; }
                need_digit = true;
                desc++;
                continue;
            }
            if (!isdigit((unsigned char)token)) { err = "Invalid character in game description"; goto done; }
            if (layer > 0 && !need_digit) { err = "Layers must be separated by dots"; goto done; }
            while (isdigit((unsigned char)*desc)) {
                if (v < 100000000L) v = v * 10 + (*desc - '0');
                desc++;
            }
            c = v > INT_MAX ? INT_MAX : (int)v;
            need_digit = false;
        }
        if (layer >= nl) { err = "Too many layers in a tube"; goto done; }
        if (c >= nc) { err = "Invalid colour in game description"; goto done; }
        if (++amounts[c] > nl) { err = "Too many layers of the same colour"; goto done; }
        if (out) out[(size_t)tube * nl + layer] = c;
        layer++;
    }
    if (need_digit) { err = "A colour is missing after a dot"; goto done; }
    if (tube + 1 < nt) { err = "Not enough tubes in game description"; goto done; }
    for (c = 0; c < nc; c++)
        if (amounts[c] < nl) { err = "Not enough layers of at least one colour"; goto done; }
  done:
    sfree(amounts);
    return err;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    return parse_desc(params, desc, NULL);
}

/* Scrambles a finished game with pours that run backwards (the reverse of a pour that was
 * possible): the game that comes out can be solved by those pours, in reverse order.  log has
 * room for steps pours. */
static void scramble_backwards(int *t, int nt, int nl, random_state *rs,
                               int steps, struct pour *log, int *nlog)
{
    int s;

    if (nlog) *nlog = 0;
    for (s = 0; s < steps; s++) {
        struct pour pick = { -1, -1, 0 };
        int tries, b, a;

        /* a few random tries are very likely to get a move (and cost very little) */
        for (tries = 0; tries < 64 && pick.n == 0; tries++) {
            int c, tb, nb, k, na;
            b = random_upto(rs, nt);
            tube_top(t + (size_t)b * nl, nl, &c, &tb, &nb);
            if (nb == 0) continue;
            k = 1 + random_upto(rs, tb);
            if (!(k < tb || nb == tb)) continue;
            a = random_upto(rs, nt - 1);
            if (a >= b) a++;
            na = tube_count(t + (size_t)a * nl, nl);
            if (nl - na < k) continue;
            if (na > 0 && t[(size_t)a * nl + na - 1] == c && nb != nl) continue;
            pick.from = b; pick.to = a; pick.n = k;
        }
        if (pick.n == 0) {
            /* count the moves there are, and take one of them */
            long n = 0, which;
            int pass;
            for (pass = 0; pass < 2; pass++) {
                long seen = 0;
                if (pass == 1) {
                    if (!n) break;
                    which = random_upto(rs, n > INT_MAX ? INT_MAX : (int)n);
                }
                for (b = 0; b < nt; b++) {
                    int c, tb, nb, k;
                    tube_top(t + (size_t)b * nl, nl, &c, &tb, &nb);
                    if (nb == 0) continue;
                    for (k = 1; k <= tb; k++) {
                        if (!(k < tb || nb == tb)) continue;
                        for (a = 0; a < nt; a++) {
                            int na;
                            if (a == b) continue;
                            na = tube_count(t + (size_t)a * nl, nl);
                            if (nl - na < k) continue;
                            if (na > 0 && t[(size_t)a * nl + na - 1] == c && nb != nl) continue;
                            if (pass == 0) n++;
                            else if (seen++ == which) {
                                pick.from = b; pick.to = a; pick.n = k;
                            }
                        }
                    }
                }
            }
            if (pick.n == 0) break;
        }
        {
            int bn = tube_count(t + (size_t)pick.from * nl, nl);
            int an = tube_count(t + (size_t)pick.to * nl, nl);
            int c = t[(size_t)pick.from * nl + bn - 1], i;
            for (i = 0; i < pick.n; i++) t[(size_t)pick.from * nl + bn - 1 - i] = -1;
            for (i = 0; i < pick.n; i++) t[(size_t)pick.to * nl + an + i] = c;
            if (log && nlog) {
                /* the pour that undoes it */
                log[*nlog].from = pick.to;
                log[*nlog].to = pick.from;
                log[*nlog].n = pick.n;
                (*nlog)++;
            }
        }
    }
}

static int count_complete(const int *t, int nt, int nl)
{
    int i, j, n = 0;
    for (i = 0; i < nt; i++) {
        const int *ti = t + (size_t)i * nl;
        if (tube_count(ti, nl) != nl) continue;
        for (j = 1; j < nl; j++)
            if (ti[j] != ti[0]) break;
        if (j == nl) n++;
    }
    return n;
}

/* the moves as a string ("p from to number", separated by ";") */
static char *pours_string(const struct pour *sol, int n)
{
    char *ret = snewn((size_t)n * 40 + 1, char), *q = ret;
    int i;
    for (i = 0; i < n; i++)
        q += sprintf(q, "%sp %d %d %d", i ? ";" : "", sol[i].from, sol[i].to, sol[i].n);
    *q = '\0';
    return ret;
}

/*
 * The levels of difficulty.  A game is worse for the player the more has to
 * be poured and the more of it is a detour that does not look like progress,
 * and the number of positions that the solver's search takes out of the queue
 * (the "work") measures both: it is at least the number of pours of the
 * solution, and every dead end that the search has to look into on the way
 * adds to it.  The generator deals a number of games that the solver can
 * finish and keeps one of them: the one with the least work for Easy, the one
 * in the middle for Normal and the one with the most for Hard.
 */
struct cand {
    long work;
    int seq;
    int *t;
    char *aux;
};

static int cand_cmp(const void *a, const void *b)
{
    const struct cand *x = (const struct cand *)a, *y = (const struct cand *)b;
    if (x->work != y->work) return x->work < y->work ? -1 : 1;
    return x->seq - y->seq;
}

static void deal_random(int *t, int *units, int nt, int nl, int nc, random_state *rs)
{
    int i, j;
    for (i = 0; i < nc; i++)
        for (j = 0; j < nl; j++)
            units[i * nl + j] = i;
    shuffle(units, nc * nl, sizeof(int), rs);
    for (i = 0; i < nt * nl; i++) t[i] = -1;
    for (i = 0; i < nc; i++)
        for (j = 0; j < nl; j++)
            t[(size_t)i * nl + j] = units[i * nl + j];
}

/*
 * What a game that is too big for the search (or that the search could not tell
 * anything about) is made of: a finished game scrambled by pours that run
 * backwards.  It can always be solved, by the same pours the other way round,
 * and those are what "Solve" shows if the search cannot find a way of its own.
 * Easy is scrambled less than the others; for Hard, the best of a few is
 * taken, the one with the most blocks of colour (the most that has to be
 * poured).
 */
static const int scramble_factor[DIFFCOUNT] = { 3, 14, 14 };

static void scrambled_game(int *t, int nt, int nl, int nc, int level, random_state *rs, char **aux)
{
    double want = (double)scramble_factor[level] * nc * nl;
    int steps = (int)(want < 2000000000.0 ? want : 2000000000.0);
    bool keep_log = steps <= LOG_MAX_STEPS;
    struct pour *log = keep_log ? snewn(steps > 0 ? steps : 1, struct pour) : NULL;
    int *cand = snewn((size_t)nt * nl, int), *bestt = snewn((size_t)nt * nl, int);
    struct pour *bestlog = keep_log ? snewn(steps > 0 ? steps : 1, struct pour) : NULL;
    int nlog = 0, bestnlog = 0, bestblocks = -1, ntries = level == DIFF_HARD ? 5 : 1;
    int attempt, got = 0, i, j;
    struct work w;

    work_init(&w, nt, nl, nc);
    for (attempt = 0; attempt < 100 + ntries && got < ntries; attempt++) {
        for (i = 0; i < nt * nl; i++) cand[i] = -1;
        for (i = 0; i < nc; i++)
            for (j = 0; j < nl; j++)
                cand[(size_t)i * nl + j] = i;
        nlog = 0;
        scramble_backwards(cand, nt, nl, rs, steps, log, keep_log ? &nlog : NULL);
        if (tubes_solved(cand, nt, nl)) continue;
        if (count_complete(cand, nt, nl) > 0 && attempt < 90) continue;
        {
            int blocks = heuristic(cand, &w);
            if (blocks > bestblocks) {
                bestblocks = blocks;
                memcpy(bestt, cand, (size_t)nt * nl * sizeof(int));
                if (keep_log) {
                    memcpy(bestlog, log, (size_t)nlog * sizeof(struct pour));
                    bestnlog = nlog;
                }
            }
        }
        got++;
    }
    if (bestblocks < 0) {
        /* nothing but finished games came out (only in games that are tiny): take the last one */
        memcpy(bestt, cand, (size_t)nt * nl * sizeof(int));
        bestnlog = keep_log ? nlog : 0;
        if (keep_log) memcpy(bestlog, log, (size_t)nlog * sizeof(struct pour));
    }
    memcpy(t, bestt, (size_t)nt * nl * sizeof(int));
    /* the pours that undo it, last one first */
    sfree(*aux);
    *aux = NULL;
    if (keep_log && bestnlog > 0) {
        struct pour *rev = snewn(bestnlog, struct pour);
        for (i = 0; i < bestnlog; i++) rev[i] = bestlog[bestnlog - 1 - i];
        *aux = pours_string(rev, bestnlog);
        sfree(rev);
    }
    work_free(&w);
    sfree(cand);
    sfree(bestt);
    sfree(log);
    sfree(bestlog);
}

static char *new_game_desc(const game_params *params, random_state *rs,
                           char **aux, bool interactive)
{
    int nt = params->ntubes, nl = params->nlayers, nc = params->ncolours;
    int level = params->difficulty;
    size_t ncell = (size_t)nt * nl;
    int *t = snewn(ncell, int);
    int *units = snewn((size_t)nc * nl, int);
    long gen_nodes = node_limit(GEN_NODES, nt, nl, nc);
    bool scaled = gen_nodes < GEN_NODES;
    double budget = GEN_WORK, unit = (double)ncell + (double)nt * nt / 4.0 + 1.0;
    int kmax = (int)(GEN_MEMORY / (double)(ncell + 1));
    struct cand *cands;
    int ncands = 0, attempt, giveups = 0, pick, i;
    char *desc;

    *aux = NULL;
    if (level < 0 || level >= DIFFCOUNT) level = DIFF_NORMAL;
    if (nc > npal) npal = nc;
    if (kmax > GEN_CANDIDATES) kmax = GEN_CANDIDATES;
    if (kmax < 3) kmax = 3;
    cands = snewn(kmax, struct cand);

    for (attempt = 0; attempt < GEN_ATTEMPTS && ncands < kmax && budget > 0; attempt++) {
        struct pour *sol = NULL;
        long expanded = 0;
        int res;
        deal_random(t, units, nt, nl, nc, rs);
        /* a tube that is done already makes the game easier; deal again */
        if (count_complete(t, nt, nl) > 0 && attempt < GEN_ATTEMPTS / 2) continue;
        res = solve_search(t, nt, nl, nc, gen_nodes, NULL, 0, &sol, &expanded);
        budget -= ((double)expanded + 1.0) * unit;
        if (res > 0) {
            cands[ncands].work = expanded;
            cands[ncands].seq = ncands;
            cands[ncands].t = snewn(ncell, int);
            memcpy(cands[ncands].t, t, ncell * sizeof(int));
            cands[ncands].aux = pours_string(sol, res);
            ncands++;
        }
        sfree(sol);
        /* where the games that are dealt are too big for the search, do not keep trying */
        if (ncands == 0 && res == -2 && scaled && ++giveups >= 3) break;
    }

    if (ncands > 0) {
        qsort(cands, ncands, sizeof(struct cand), cand_cmp);
        pick = level == DIFF_EASY ? 0 : level == DIFF_HARD ? ncands - 1 : ncands / 2;
        desc = tubes_desc(cands[pick].t, nt, nl, nc);
        *aux = cands[pick].aux;
        cands[pick].aux = NULL;
        for (i = 0; i < ncands; i++) {
            sfree(cands[i].t);
            sfree(cands[i].aux);
        }
    } else {
        scrambled_game(t, nt, nl, nc, level, rs, aux);
        desc = tubes_desc(t, nt, nl, nc);
    }
    sfree(cands);
    sfree(t);
    sfree(units);
    return desc;
}

/* what can be seen of a tube with hidden layers: the layers of the colour on top (and what has been seen before) */
static void reveal_top_block(game_state *state, int t)
{
    int nl = state->p.nlayers, c, b, n, i;
    tube_top(state->tubes + (size_t)t * nl, nl, &c, &b, &n);
    for (i = n; i < nl; i++) state->known[(size_t)t * nl + i] = 0;
    for (i = n - b; i < n; i++) state->known[(size_t)t * nl + i] = 1;
}

static void reveal_tops(game_state *state)
{
    int t;
    for (t = 0; t < state->p.ntubes; t++) reveal_top_block(state, t);
}

static game_state *new_game(midend *me, const game_params *params, const char *desc)
{
    game_state *state = snew(game_state);
    size_t n = (size_t)params->ntubes * params->nlayers;

    memset(state, 0, sizeof(*state));
    state->p = *params;
    state->tubes = snewn(n, int);
    state->known = snewn(n, unsigned char);
    memset(state->known, 0, n);
    if (params->ncolours > npal) npal = params->ncolours;

    parse_desc(params, desc, state->tubes);
    reveal_tops(state);
    state->solved = tubes_solved(state->tubes, params->ntubes, params->nlayers);
    return state;
}

static game_state *dup_game(const game_state *state)
{
    game_state *ret = snew(game_state);
    size_t n = (size_t)state->p.ntubes * state->p.nlayers;

    *ret = *state;
    ret->tubes = snewn(n, int);
    memcpy(ret->tubes, state->tubes, n * sizeof(int));
    ret->known = snewn(n, unsigned char);
    memcpy(ret->known, state->known, n);
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
    const game_params *p = &currstate->p;
    int n;
    char *ret;

    if (currstate->solved) {
        *error = "The puzzle is solved already";
        return NULL;
    }
    n = solve_search(currstate->tubes, p->ntubes, p->nlayers, p->ncolours,
                     node_limit(SOLVE_NODES, p->ntubes, p->nlayers, p->ncolours), NULL, 0, &sol, NULL);
    if (n < 0) {
        /* the game that was made in this session knows its solution, and it is good for the
         * position that it was made in */
        if (n == -2 && aux && state &&
            memcmp(state->tubes, currstate->tubes,
                   (size_t)p->ntubes * p->nlayers * sizeof(int)) == 0)
            return dupstr(aux);
        *error = n == -1 ? "This position cannot be solved; undo some moves" :
            "No solution was found in time; undo some moves";
        return NULL;
    }
    ret = pours_string(sol, n);
    sfree(sol);
    return ret;
}

static char *hint_game(const game_state *orig, const game_state *curr, const char **error)
{
    struct pour *sol = NULL;
    const game_params *p = &curr->p;
    long limit = node_limit(HINT_NODES, p->ntubes, p->nlayers, p->ncolours);
    int n;
    char buf[64];

    if (curr->solved) {
        *error = "The puzzle is solved already";
        return NULL;
    }
    /* never lead the player round in circles: stay away from the positions that came before */
    n = solve_search(curr->tubes, p->ntubes, p->nlayers, p->ncolours, limit,
                     curr->hist, curr->nhist, &sol, NULL);
    if (n < 0)
        n = solve_search(curr->tubes, p->ntubes, p->nlayers, p->ncolours, limit, NULL, 0, &sol, NULL);
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
    return !state->p.hiddenlayers || state->known[(size_t)tube * state->p.nlayers + layer];
}

static bool game_can_format_as_text_now(const game_params *params)
{
    return true;
}

static char *game_text_format(const game_state *state)
{
    int nt = state->p.ntubes, nl = state->p.nlayers, nc = state->p.ncolours;
    int width = 1, tube, layer;
    size_t c = 0;
    char *buf;

    if (nc > 16) {
        int m = nc - 1;
        for (width = 0; m > 0; m /= 10) width++;
        if (width < 1) width = 1;
    }
    buf = snewn((size_t)nl * nt * (width + 1) + 1, char);
    for (layer = nl - 1; layer >= 0; layer--) {
        for (tube = 0; tube < nt; tube++) {
            int col = state->tubes[(size_t)tube * nl + layer], len;
            char num[16];
            const char *txt = num;
            if (col < 0) txt = "_";
            else if (!layer_known(state, tube, layer)) txt = "?";
            else if (nc <= 16) { num[0] = HEX[col]; num[1] = '\0'; }
            else sprintf(num, "%d", col);
            len = (int)strlen(txt);
            if (len > width) len = width;
            /* the text is as wide as the biggest number, to the right */
            memset(buf + c, ' ', (size_t)(width - len));
            memcpy(buf + c + (width - len), txt, (size_t)len);
            c += (size_t)width;
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
    int nt, nl;
    int *layer;                        /* nt * nl: what is shown: -1 nothing, -2 not known, else the colour */
    int *want;                         /* nl: the same, for the tube that is being drawn */
    signed char *sel, *cur, *flash;    /* nt each */
    char status[96];
};

/* ---- the layout of the tubes ---- */

#define WALL(ts)    ((ts) / 12 + 1)
#define GAP(ts)     ((ts) / 2)
#define MARGIN(ts)  ((ts) / 2 + 2)
#define TUBEW(ts)   ((ts) + 2 * WALL(ts))
#define ROWH(p, ts) (((p)->nlayers + 1) * (ts) + 2 * WALL(ts))

/*
 * Up to 5 tubes are in a row, up to 16 in two rows (as they always were),
 * and more of them in as many rows as make the picture about as high as it
 * is wide.
 */
static void tube_layout(const game_params *p, int *cols, int *rows)
{
    int nt = p->ntubes;
    if (nt <= WRAP_TUBES) {
        *cols = nt;
        *rows = 1;
    } else if (nt <= 16) {
        *cols = (nt + 1) / 2;
        *rows = 2;
    } else {
        double unitw = 1.67, unith = (double)p->nlayers + 1.67;
        int r0 = (int)(sqrt((double)nt * unitw / unith) + 0.5), r, best = 2;
        double bestd = 1e30;
        for (r = r0 - 1; r <= r0 + 1; r++) {
            int c;
            double d;
            if (r < 2 || r > nt) continue;
            c = (nt + r - 1) / r;
            d = fabs(log((double)c * unitw / ((double)r * unith)));
            if (d < bestd) { bestd = d; best = r; }
        }
        *rows = best;
        *cols = (nt + best - 1) / best;
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
        else if (rows > 1) {
            if (button == CURSOR_UP) {
                cur -= cols;
                if (cur < 0) {
                    cur += rows * cols;
                    while (cur >= nt) cur -= cols;
                }
            } else {
                cur += cols;
                if (cur >= nt) cur %= cols;
            }
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
        if (tube_count(state->tubes + (size_t)tube * nl, nl) == 0) return MOVE_NO_EFFECT;
        ui->selected = tube;
        return MOVE_UI_UPDATE;
    }
    k = pour_amount(state->tubes, nl, ui->selected, tube);
    if (k) {
        char buf[64];
        sprintf(buf, "p %d %d %d", ui->selected, tube, k);
        ui->selected = -1;
        ui->cursor = tube;
        return dupstr(buf);
    }
    /* not possible: the tube that was touched is the one that is selected now */
    ui->selected = tube_count(state->tubes + (size_t)tube * nl, nl) > 0 ? tube : -1;
    return MOVE_UI_UPDATE;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static float *game_colours(frontend *fe, int *ncolours)
{
    int n, c, i;
    float *ret;

    npal_given = npal;
    n = COL_0 + npal_given;
    ret = snewn(3 * n, float);

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
    for (c = 0; c < npal_given; c++)
        liquid_rgb(c, &ret[(COL_0 + c) * 3]);

    *ncolours = n;
    return ret;
}

/* the colour of the drawing of the liquid number c */
static int liquid_colour(int c)
{
    return COL_0 + c % npal_given;
}

static bool label_is_light(int colour)
{
    float rgb[3];
    double y;
    liquid_rgb(colour % npal_given, rgb);
    y = 0.299 * rgb[0] * 255.0 + 0.587 * rgb[1] * 255.0 + 0.114 * rgb[2] * 255.0;
    return y < 128.0;
}

/* the text on a layer: its number (from 1), or letters (A to Z, then AA, AB and so on) */
static void layer_label(char *buf, int labels, int col)
{
    if (labels == LABELS_NUMBERS) {
        sprintf(buf, "%d", col + 1);
    } else {
        char tmp[16];
        int n = 0, v = col, i;
        do {
            tmp[n++] = (char)('A' + v % 26);
            v = v / 26 - 1;
        } while (v >= 0 && n < 12);
        for (i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
        buf[n] = '\0';
    }
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    game_drawstate *ds = snew(game_drawstate);
    int nt = state->p.ntubes, nl = state->p.nlayers;
    memset(ds, 0, sizeof(*ds));
    ds->tilesize = 0;
    ds->started = false;
    ds->labels = -1;
    ds->nt = nt;
    ds->nl = nl;
    ds->layer = snewn((size_t)nt * nl, int);
    ds->want = snewn(nl, int);
    ds->sel = snewn(nt, signed char);
    ds->cur = snewn(nt, signed char);
    ds->flash = snewn(nt, signed char);
    memset(ds->layer, 0, (size_t)nt * nl * sizeof(int));
    memset(ds->sel, -3, nt);
    memset(ds->cur, 0, nt);
    memset(ds->flash, 0, nt);
    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->layer);
    sfree(ds->want);
    sfree(ds->sel);
    sfree(ds->cur);
    sfree(ds->flash);
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
        if (memcmp(os->tubes + (size_t)t * nl, ns->tubes + (size_t)t * nl,
                   (size_t)nl * sizeof(int)) != 0) {
            if (++changed > 2) return false;
            if (a < 0) a = t; else b = t;
        }
    }
    if (changed != 2) return false;
    {
        int ca = tube_count(os->tubes + (size_t)a * nl, nl), cb = tube_count(os->tubes + (size_t)b * nl, nl);
        int na = tube_count(ns->tubes + (size_t)a * nl, nl), nb = tube_count(ns->tubes + (size_t)b * nl, nl);
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
                      const int *layers, bool selected, bool cursor, bool flash)
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
        draw_rect(dr, x0 + wall, y, ts, ts, col == -2 ? COL_HIDDEN : liquid_colour(col));
        /* a thin line between two layers of different colours makes the layers easier to tell */
        if (i > 0 && layers[i - 1] != col)
            draw_rect(dr, x0 + wall, y + ts - 1, ts, 1, COL_TUBE);
        if (ds->labels != LABELS_NONE || col == -2) {
            char buf[32];
            if (col == -2) {
                strcpy(buf, "?");
                tc = COL_LABEL_DARK;
            } else {
                layer_label(buf, ds->labels, col);
                tc = label_is_light(col) ? COL_LABEL_LIGHT : COL_LABEL_DARK;
            }
            draw_text(dr, x0 + wall + ts / 2, y + ts / 2, FONT_VARIABLE,
                      (strlen(buf) > 3 ? ts / 4 : strlen(buf) > 1 ? ts * 2 / 5 : ts / 2),
                      ALIGN_HCENTRE | ALIGN_VCENTRE, tc, buf);
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
    char status[96];

    if (!ds->started || ds->labels != ui->labels) {
        int w, h;
        game_compute_size(p, ds->tilesize, ui, &w, &h);
        draw_rect(dr, 0, 0, w, h, COL_BACKGROUND);
        draw_update(dr, 0, 0, w, h);
        for (t = 0; t < nt; t++) ds->sel[t] = -3;
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
        int *want = ds->want;
        bool sel, cur;
        for (l = 0; l < nl; l++) want[l] = -1;
        if (src >= 0 && (t == src || t == dst)) {
            /* in the middle of a pour: some of the layers have moved already */
            int sn = tube_count(oldstate->tubes + (size_t)src * nl, nl);
            int dn = tube_count(oldstate->tubes + (size_t)dst * nl, nl);
            int colour = oldstate->tubes[(size_t)src * nl + sn - 1];
            if (t == src) {
                for (l = 0; l < sn - moved; l++)
                    want[l] = layer_known(oldstate, t, l) ? oldstate->tubes[(size_t)t * nl + l] : -2;
            } else {
                for (l = 0; l < dn; l++)
                    want[l] = layer_known(oldstate, t, l) ? oldstate->tubes[(size_t)t * nl + l] : -2;
                for (; l < dn + moved; l++) want[l] = colour;
            }
        } else {
            for (l = 0; l < nl; l++) {
                if (state->tubes[(size_t)t * nl + l] < 0) continue;
                want[l] = layer_known(state, t, l) ? state->tubes[(size_t)t * nl + l] : -2;
            }
        }
        sel = ui->selected == t;
        cur = ui->cursor_visible && ui->cursor == t;
        if (memcmp(want, ds->layer + (size_t)t * nl, (size_t)nl * sizeof(int)) != 0 ||
            ds->sel[t] != sel || ds->cur[t] != cur || ds->flash[t] != flash) {
            memcpy(ds->layer + (size_t)t * nl, want, (size_t)nl * sizeof(int));
            ds->sel[t] = (signed char)sel;
            ds->cur[t] = (signed char)cur;
            ds->flash[t] = (signed char)flash;
            draw_tube(dr, ds, p, t, want, sel, cur, flash);
        }
    }

    if (state->solved)
        sprintf(status, "COMPLETED! Moves: %d", state->moves);
    else if (!has_pour(state->tubes, nt, nl, p->ncolours))
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
