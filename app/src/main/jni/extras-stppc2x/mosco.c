/*
 * mosco.c: "Magic Arrow Tile Puzzles", after the books by Ivan Moscovich.
 *
 * This is the Mosco puzzle from Lee Dowling's stppc2x collection of puzzles
 * for hand-held consoles, brought over to this app.
 *
 * The board is an arena of w x h tiles inside a ring of clue squares (the
 * four corners are clue squares as well).  The player lays an arrow on every
 * tile of the arena, any of eight kinds (up, up-left, left, ... up-right),
 * and exactly w*h/8 tiles are to carry each kind, so a 4x4 arena has two
 * of every kind and an 8x8 one eight.
 *
 * The number in a clue square says how many of the arrows point at that
 * square: an arrow points along its row, its column or its diagonal and
 * what lies between it and the clue square doesn't matter.  Each arrow
 * therefore points at exactly one clue square, which makes the sum of all
 * the clues equal to the number of tiles.
 *
 * A puzzle that is made here has just one solution, and one that can be
 * reached from the clues by reasoning alone (see the levels below).  For a
 * description that was typed in that need not be so, and then any arrangement
 * that meets all the clues and the tile counts is a win.
 *
 * What is new compared to the original:
 *
 *  - The game description holds only the clues.  The original put the
 *    whole arrangement it had made the clues from into the description
 *    (and cleared the tiles again when it set the game up), so reading a
 *    game ID gave the answer away.  The arrangement is the "aux" data now.
 *  - The puzzles have just one solution that the reasoning of their level
 *    (Easy, Normal, Hard) gets to without a guess: the original's hardly ever
 *    did.  A board of any size from 2 x 4 up is made (see "The generator").
 *  - There is a solver.  It narrows the kinds of arrow each tile can be
 *    from the clues, the counts of the tiles and the arrows already laid,
 *    with "what if" at the level Hard, and searches where that isn't enough
 *    (a description that was typed in).  "Show next step" uses it to tell a
 *    mistake from a good start and to give a tile that has to be like it is.
 *  - Tap a tile to turn the arrow it has, swipe over a tile to lay the
 *    arrow of that direction, long press (or right click) to mark the
 *    kinds of arrow a tile might have, long press without moving to clear
 *    the tile.  Tapping a clue shows the tiles that can point at it.
 *  - The clues, the number of arrows left to lay of each kind and the
 *    kinds that have been used too often are shown while playing.
 *  - The original's pile of small defects is gone (it made its clues
 *    again by copying the whole game for every clue square on every
 *    redraw, leaked and read outside the grid on some clicks, shared the
 *    game parameters between the states, took a completed game for an
 *    unfinished one after the next move...).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <limits.h>

#include "puzzles.h"

#define PREFERRED_TILE_SIZE 40
/* The memory is what limits the size of the board (a few dozen bytes for
 * every tile of the arena, while a puzzle is made); there is nothing else */
#define MAX_TILES 1048576
#define FLASH_TIME 0.5F
#define FLASH_FRAME 0.1F

/*
 * Colours.  The ones that are called after the colour of an arrow are the
 * (light) colours of the tiles: up/down blue, the diagonal from top left to
 * bottom right green, left/right red and the other diagonal yellow, as in
 * the original.
 */
enum {
    COL_BACKGROUND,
    COL_ARENA,
    COL_GRID,
    COL_AFFECT,
    COL_CURSOR,
    COL_FLASH,
    COL_TEXT,
    COL_SATISFIED,
    COL_ERROR,
    COL_BLUE,
    COL_GREEN,
    COL_RED,
    COL_YELLOW,
    COL_INK,
    COL_MARK,
    NCOLOURS
};

/* The eight kinds of arrow, counter-clockwise from "up" (as in the original) */
enum { UP, UP_LEFT, LEFT, DOWN_LEFT, DOWN, DOWN_RIGHT, RIGHT, UP_RIGHT, NDIR };

static const int dxs[NDIR] = {  0, -1, -1, -1,  0,  1,  1,  1 };
static const int dys[NDIR] = { -1, -1,  0,  1,  1,  1,  0, -1 };
static const int arrow_colour[NDIR] = {
    COL_BLUE, COL_GREEN, COL_RED, COL_YELLOW,
    COL_BLUE, COL_GREEN, COL_RED, COL_YELLOW
};
static const char *const dir_name[NDIR] = {
    "up", "up-left", "left", "down-left",
    "down", "down-right", "right", "up-right"
};
/* The same as arrows, for the status bar */
static const char *const dir_glyph[NDIR] = {
    "\xe2\x86\x91", "\xe2\x86\x96", "\xe2\x86\x90", "\xe2\x86\x99",
    "\xe2\x86\x93", "\xe2\x86\x98", "\xe2\x86\x92", "\xe2\x86\x97"
};

static int dir_of(int dx, int dy)
{
    int d;
    for (d = 0; d < NDIR; d++)
        if (dxs[d] == dx && dys[d] == dy) return d;
    return -1;
}

/*
 * The levels differ in what it takes to solve a puzzle (and in how flat the
 * clues are: the harder the level, the fewer big numbers).  Every puzzle has
 * just one solution that can be reached from the clues by reasoning alone:
 *
 *  Easy    the arrows that a tile can still have are told by the clues that
 *          have all their arrows and by the kinds that are used up; a tile
 *          with a single arrow left has it, and a clue that wants as many
 *          arrows as there are tiles that can still point at it gets them
 *          all.
 *  Normal  as Easy, and a kind that wants as many tiles as can still be of
 *          that kind gets them all.
 *  Hard    as Normal, and where that stops: a tile of which all but one of
 *          the arrows that it can still have are found to lead to a
 *          contradiction (by the rules above) has the one that is left.
 */
enum { DIFF_EASY, DIFF_NORMAL, DIFF_HARD, DIFFCOUNT };
static const char *const diff_names[DIFFCOUNT] = { "Easy", "Normal", "Hard" };
static const char diff_chars[DIFFCOUNT + 1] = "enh";

struct game_params {
    int w, h, diff;
};

/*
 * What the clues of a game are, and where the arrows of the tiles point:
 * shared by all the states of a game.  Tile (x, y) of the arena has the
 * number y * w + x; the clues are numbered clockwise around the ring,
 * starting from the top left corner.
 */
struct mosco_clues {
    int refcount;
    int w, h;
    int n;              /* tiles in the arena, w * h */
    int k;              /* clue squares, 2 * w + 2 * h + 4 */
    int *clue;          /* k: the numbers */
    int *target;        /* n * NDIR: the clue that tile t points at when
                           it has the arrow d is target[t * NDIR + d] */
    int kcount[NDIR];   /* how many tiles have each kind of arrow: n / 8 on
                           a board (see "The chain" for when it isn't) */
};

struct game_state {
    struct mosco_clues *cl;
    signed char *grid;          /* n: the arrow of each tile, -1 for none */
    unsigned char *marks;       /* n: the kinds that the player has marked */
    bool completed, used_solve;
};

/* A number (of digits) at *p, which is moved past it; more than MAX_TILES
 * is MAX_TILES + 1 (and validate_params says what is wrong with it) */
static int parse_number(const char **p)
{
    long long v = 0;

    while (isdigit((unsigned char)**p)) {
        if (v <= MAX_TILES) v = v * 10 + (**p - '0');
        (*p)++;
    }
    return v > MAX_TILES ? MAX_TILES + 1 : (int)v;
}

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);
    ret->w = ret->h = 4;
    ret->diff = DIFF_NORMAL;
    return ret;
}

/* Any size that gives an area that is a multiple of 8 is a valid grid */
static const struct { int w, h; } mosco_sizes[] = {
    { 4, 4 },
    { 6, 4 },
    { 8, 4 },
    { 8, 8 },
};
#define NPRESETS (lenof(mosco_sizes) * DIFFCOUNT)

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    char str[32];
    game_params *ret;

    if (i < 0 || i >= (int)NPRESETS) return false;

    ret = snew(game_params);
    ret->w = mosco_sizes[i / DIFFCOUNT].w;
    ret->h = mosco_sizes[i / DIFFCOUNT].h;
    ret->diff = i % DIFFCOUNT;
    sprintf(str, "%dx%d %s", ret->w, ret->h, diff_names[ret->diff]);
    *name = dupstr(str);
    *params = ret;
    return true;
}

static void free_params(game_params *params)
{
    sfree(params);
}

static game_params *dup_params(const game_params *params)
{
    game_params *ret = snew(game_params);
    *ret = *params;		       /* structure copy */
    return ret;
}

static void decode_params(game_params *params, char const *string)
{
    const char *p = string;

    params->diff = DIFF_NORMAL;
    if (isdigit((unsigned char)*p)) {
        params->w = parse_number(&p);
        if (*p == 'x') {
            p++;
            params->h = parse_number(&p);
        } else {
            params->h = params->w;
        }
    }
    if (*p == 'd') {
        int i;
        p++;
        for (i = 0; i < DIFFCOUNT; i++)
            if (*p == diff_chars[i]) params->diff = i;
    }
}

static char *encode_params(const game_params *params, bool full)
{
    char buf[32];
    sprintf(buf, "%dx%d", params->w, params->h);
    if (full) sprintf(buf + strlen(buf), "d%c", diff_chars[params->diff]);
    return dupstr(buf);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret = snewn(4, config_item);
    char buf[32];

    memset(ret, 0, 4 * sizeof(config_item));

    ret[0].name = "Width";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[0].u.string.sval = dupstr(buf);

    ret[1].name = "Height";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->h);
    ret[1].u.string.sval = dupstr(buf);

    ret[2].name = "Difficulty";
    ret[2].type = C_CHOICES;
    ret[2].u.choices.choicenames = ":Easy:Normal:Hard";
    ret[2].u.choices.selected = params->diff;

    ret[3].name = NULL;
    ret[3].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);
    const char *p = cfg[0].u.string.sval;

    ret->w = parse_number(&p);
    p = cfg[1].u.string.sval;
    ret->h = parse_number(&p);
    ret->diff = cfg[2].u.choices.selected;

    return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
    /* (in a single row or column the arrows that point up and those that
     * point down, or those that point left and those that point right, can
     * be changed round without any clue seeing it, so there is never just
     * one solution) */
    if (params->w < 2 || params->h < 2)
        return "Width and height must both be at least two";
    if (params->w > MAX_TILES || params->h > MAX_TILES ||
        (long long)params->w * params->h > MAX_TILES)
        return "The arena may have at most " STR(MAX_TILES) " tiles";
    if (params->diff < 0 || params->diff >= DIFFCOUNT)
        return "Unknown difficulty level";
    if ((params->w * params->h) % NDIR != 0)
        return "Width times height must be a multiple of 8";
    return NULL;
}

/* ----------------------------------------------------------------------
 * The ring of clues.
 *
 * Squares of the board are written (rx, ry) with the arena at 1..w, 1..h and
 * the clues around it, at 0 and w + 1 / h + 1.  The clue squares are
 * numbered clockwise from the top left corner.
 */

static int ring_index(int w, int h, int rx, int ry)
{
    if (ry == 0) return rx;                             /* top, with both corners */
    if (rx == w + 1) return w + 1 + ry;                 /* right, with bottom right corner */
    if (ry == h + 1) return w + h + 2 + (w + 1 - rx);   /* bottom, with bottom left corner */
    return 2 * w + h + 3 + (h + 1 - ry);                /* left */
}

static void ring_pos(int w, int h, int k, int *rx, int *ry)
{
    if (k <= w + 1) {
        *rx = k; *ry = 0;
    } else if (k <= w + h + 2) {
        *rx = w + 1; *ry = k - (w + 1);
    } else if (k <= 2 * w + h + 3) {
        *rx = w + 1 - (k - (w + h + 2)); *ry = h + 1;
    } else {
        *rx = 0; *ry = h + 1 - (k - (2 * w + h + 3));
    }
}

/* The clue that the arrow d of tile (x, y) points at: the square of the
 * ring where a ray from the tile leaves the arena. */
static int target_clue(int w, int h, int x, int y, int d)
{
    int dx = dxs[d], dy = dys[d];
    int rx = x + 1, ry = y + 1;
    int t = INT_MAX;

    if (dx > 0) t = min(t, w + 1 - rx); else if (dx < 0) t = min(t, rx);
    if (dy > 0) t = min(t, h + 1 - ry); else if (dy < 0) t = min(t, ry);
    return ring_index(w, h, rx + dx * t, ry + dy * t);
}

/* Where a clue is, for the messages (the result lasts until the next call) */
static const char *describe_clue(int w, int h, int k)
{
    static char buf[64];
    int rx, ry;

    ring_pos(w, h, k, &rx, &ry);
    if (ry == 0 && rx == 0) return "in the top left corner";
    if (ry == 0 && rx == w + 1) return "in the top right corner";
    if (ry == h + 1 && rx == 0) return "in the bottom left corner";
    if (ry == h + 1 && rx == w + 1) return "in the bottom right corner";
    if (ry == 0) sprintf(buf, "above column %d", rx);
    else if (ry == h + 1) sprintf(buf, "below column %d", rx);
    else if (rx == 0) sprintf(buf, "to the left of row %d", ry);
    else sprintf(buf, "to the right of row %d", ry);
    return buf;
}

static struct mosco_clues *clues_new(int w, int h, const int *values)
{
    struct mosco_clues *cl = snew(struct mosco_clues);
    int x, y, d;

    cl->refcount = 1;
    cl->w = w;
    cl->h = h;
    cl->n = w * h;
    cl->k = 2 * w + 2 * h + 4;
    cl->clue = snewn(cl->k, int);
    memcpy(cl->clue, values, cl->k * sizeof(int));
    cl->target = snewn(cl->n * NDIR, int);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            for (d = 0; d < NDIR; d++)
                cl->target[(y * w + x) * NDIR + d] = target_clue(w, h, x, y, d);
    for (d = 0; d < NDIR; d++) cl->kcount[d] = cl->n / NDIR;
    return cl;
}

/*
 * Not a board but a puzzle of the same kind that is a part of one (the core
 * of a big board, see the generator): n tiles, k clue squares, the clue that
 * the arrow d of tile t points at is target[t * NDIR + d], and kcount[d]
 * tiles are to have the arrow d.  The clues are all 0 to begin with.
 */
static struct mosco_clues *clues_new_core(int n, int k, const int *target,
                                          const int *kcount)
{
    struct mosco_clues *cl = snew(struct mosco_clues);
    int i;

    cl->refcount = 1;
    cl->w = cl->h = 0;
    cl->n = n;
    cl->k = k;
    cl->clue = snewn(k, int);
    for (i = 0; i < k; i++) cl->clue[i] = 0;
    cl->target = snewn(n * NDIR, int);
    memcpy(cl->target, target, n * NDIR * sizeof(int));
    memcpy(cl->kcount, kcount, sizeof(cl->kcount));
    return cl;
}

static void clues_free(struct mosco_clues *cl)
{
    if (--cl->refcount > 0) return;
    sfree(cl->clue);
    sfree(cl->target);
    sfree(cl);
}

/*
 * The game description is the list of the clues, separated by commas,
 * clockwise from the top left corner.  (And the aux data is a string of
 * the arrows of a solution, one digit - the number of the kind - for each
 * tile, row by row.)
 */
static const char *parse_clues(const game_params *params, const char *desc,
                               int *values /* k numbers, or NULL */)
{
    int w = params->w, h = params->h, n = w * h, k = 2 * w + 2 * h + 4;
    int x, y, d, i = 0, v, sum = 0;
    const char *p = desc;
    const char *err = NULL;
    int *vals = values ? values : snewn(k, int);
    int *maxc = snewn(k, int);

    for (;;) {
        if (!isdigit((unsigned char)*p)) {
            err = "Expected a number in the list of clues";
            break;
        }
        v = 0;
        while (isdigit((unsigned char)*p)) {
            v = v * 10 + (*p++ - '0');
            if (v > n) {
                err = "A clue is greater than the number of tiles";
                goto done;
            }
        }
        if (i >= k) {
            err = "Too many clues";
            break;
        }
        vals[i++] = v;
        sum += v;
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == '\0') break;
        err = "Unexpected character in the list of clues";
        break;
    }
    if (!err && i < k)
        err = "Too few clues";
    if (!err && sum != n)
        err = "The clues don't add up to the number of tiles";
    if (!err) {
        /* none can be greater than the number of tiles that can point at it */
        for (i = 0; i < k; i++) maxc[i] = 0;
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++)
                for (d = 0; d < NDIR; d++)
                    maxc[target_clue(w, h, x, y, d)]++;
        for (i = 0; i < k; i++)
            if (vals[i] > maxc[i]) {
                err = "A clue is greater than the number of tiles that can point at it";
                break;
            }
    }
done:
    sfree(maxc);
    if (!values) sfree(vals);
    return err;
}

/* ----------------------------------------------------------------------
 * The solver.
 *
 * Each tile has the set of arrows it can still have (a set of the eight
 * kinds, as bits).  All that has to be met are counts: a clue square wants
 * exactly its number of arrows pointing at it and every kind of arrow wants
 * to be used exactly n / 8 times.  For each of these we keep the least and
 * the most it can still come to - the tiles that are settled and point
 * there, and the tiles that still could - and when what is wanted is the
 * least, no undecided tile is allowed to count any more, and when it is the
 * most, all the tiles that could count have to.  That settles quite a lot
 * and a search takes over where it stops.
 */

struct msolver {
    int n, k;
    const int *clue;
    const int *target;
    int kcount[NDIR];           /* how many tiles each kind of arrow is to have */
    /* for each clue, the tiles that can point at it, and with which arrow */
    int *lstart;                /* k + 1 */
    int *lcell;                 /* n * NDIR */
    unsigned char *ldir;        /* n * NDIR */
    /* the constraints still to look at: the clues, then the kinds */
    int *queue;
    unsigned char *inq;
    int qsize, qhead, qlen;
    /* may a kind that wants as many tiles as can still be of that kind
     * settle them all?  (Not on level Easy.) */
    bool kindforce;
    /* what the "what if" may still take (about the number of bytes it
     * copies); when that is used up it finds nothing more */
    double work;
};

struct sstate {
    unsigned char *dom;         /* n: the arrows each tile can have */
    int *cmin, *cmax;           /* k: what each clue can still come to */
    int dmin[NDIR], dmax[NDIR]; /* the same for the count of each kind */
};

static int popcnt8(unsigned v)
{
    static const unsigned char pop4[16] = { 0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4 };
    return pop4[v & 15] + pop4[(v >> 4) & 15];
}

static int lowbit(unsigned v)
{
    int d = 0;
    while (!(v & 1)) { v >>= 1; d++; }
    return d;
}

static struct msolver *msolver_new(const struct mosco_clues *cl)
{
    struct msolver *sv = snew(struct msolver);
    int i, t, d, *fill;

    sv->n = cl->n;
    sv->k = cl->k;
    sv->clue = cl->clue;
    sv->target = cl->target;
    memcpy(sv->kcount, cl->kcount, sizeof(sv->kcount));

    sv->lstart = snewn(sv->k + 1, int);
    for (i = 0; i <= sv->k; i++) sv->lstart[i] = 0;
    for (i = 0; i < sv->n * NDIR; i++) sv->lstart[sv->target[i] + 1]++;
    for (i = 0; i < sv->k; i++) sv->lstart[i + 1] += sv->lstart[i];
    sv->lcell = snewn(sv->n * NDIR, int);
    sv->ldir = snewn(sv->n * NDIR, unsigned char);
    fill = snewn(sv->k, int);
    for (i = 0; i < sv->k; i++) fill[i] = sv->lstart[i];
    for (t = 0; t < sv->n; t++)
        for (d = 0; d < NDIR; d++) {
            int c = sv->target[t * NDIR + d];
            sv->lcell[fill[c]] = t;
            sv->ldir[fill[c]] = d;
            fill[c]++;
        }
    sfree(fill);

    sv->qsize = sv->k + NDIR;
    sv->queue = snewn(sv->qsize, int);
    sv->inq = snewn(sv->qsize, unsigned char);
    memset(sv->inq, 0, sv->qsize);
    sv->qhead = sv->qlen = 0;
    sv->kindforce = true;
    sv->work = 1e30;
    return sv;
}

static void msolver_free(struct msolver *sv)
{
    sfree(sv->lstart);
    sfree(sv->lcell);
    sfree(sv->ldir);
    sfree(sv->queue);
    sfree(sv->inq);
    sfree(sv);
}

static void sv_push(struct msolver *sv, int id)
{
    if (sv->inq[id]) return;
    sv->inq[id] = 1;
    sv->queue[(sv->qhead + sv->qlen) % sv->qsize] = id;
    sv->qlen++;
}

static int sv_pop(struct msolver *sv)
{
    int id = sv->queue[sv->qhead];
    sv->qhead = (sv->qhead + 1) % sv->qsize;
    sv->qlen--;
    sv->inq[id] = 0;
    return id;
}

static void sv_drain(struct msolver *sv)
{
    while (sv->qlen > 0) sv_pop(sv);
}

/* Every tile can have any arrow, and every constraint wants looking at */
static struct sstate *ss_new(struct msolver *sv)
{
    struct sstate *ss = snew(struct sstate);
    int i;

    ss->dom = snewn(sv->n, unsigned char);
    memset(ss->dom, (1 << NDIR) - 1, sv->n);
    ss->cmin = snewn(2 * sv->k, int);
    ss->cmax = ss->cmin + sv->k;
    for (i = 0; i < sv->k; i++) {
        ss->cmin[i] = 0;
        ss->cmax[i] = sv->lstart[i + 1] - sv->lstart[i];
    }
    for (i = 0; i < NDIR; i++) {
        ss->dmin[i] = 0;
        ss->dmax[i] = sv->n;
    }
    for (i = 0; i < sv->k + NDIR; i++) sv_push(sv, i);
    return ss;
}

static struct sstate *ss_dup(const struct msolver *sv, const struct sstate *src)
{
    struct sstate *ss = snew(struct sstate);

    ss->dom = snewn(sv->n, unsigned char);
    memcpy(ss->dom, src->dom, sv->n);
    ss->cmin = snewn(2 * sv->k, int);
    memcpy(ss->cmin, src->cmin, 2 * sv->k * sizeof(int));
    ss->cmax = ss->cmin + sv->k;
    memcpy(ss->dmin, src->dmin, sizeof(ss->dmin));
    memcpy(ss->dmax, src->dmax, sizeof(ss->dmax));
    return ss;
}

static void ss_free(struct sstate *ss)
{
    sfree(ss->dom);
    sfree(ss->cmin);
    sfree(ss);
}

/* Back to the start, without allocating anything */
static void ss_reset(struct msolver *sv, struct sstate *ss)
{
    int i;

    memset(ss->dom, (1 << NDIR) - 1, sv->n);
    for (i = 0; i < sv->k; i++) {
        ss->cmin[i] = 0;
        ss->cmax[i] = sv->lstart[i + 1] - sv->lstart[i];
    }
    for (i = 0; i < NDIR; i++) {
        ss->dmin[i] = 0;
        ss->dmax[i] = sv->n;
    }
    sv_drain(sv);
    for (i = 0; i < sv->k + NDIR; i++) sv_push(sv, i);
}

static void ss_copy(const struct msolver *sv, struct sstate *dst,
                    const struct sstate *src)
{
    memcpy(dst->dom, src->dom, sv->n);
    memcpy(dst->cmin, src->cmin, 2 * sv->k * sizeof(int));
    memcpy(dst->dmin, src->dmin, sizeof(dst->dmin));
    memcpy(dst->dmax, src->dmax, sizeof(dst->dmax));
}

/* How many arrows are still open: 0 when every tile is settled */
static int ss_open(const struct msolver *sv, const struct sstate *ss)
{
    int t, c = 0;

    for (t = 0; t < sv->n; t++) c += popcnt8(ss->dom[t]) - 1;
    return c;
}

/* Take the arrow d away from tile t: false if that is all that was left */
static bool ss_remove(struct msolver *sv, struct sstate *ss, int t, int d)
{
    unsigned dom = ss->dom[t];
    int c;

    if (!(dom & (1u << d))) return true;
    if (!(dom & (dom - 1))) return false;

    dom &= ~(1u << d);
    ss->dom[t] = dom;
    c = sv->target[t * NDIR + d];
    ss->cmax[c]--;
    ss->dmax[d]--;
    sv_push(sv, c);
    sv_push(sv, sv->k + d);
    if (!(dom & (dom - 1))) {
        /* the tile is settled now */
        int e = lowbit(dom);
        c = sv->target[t * NDIR + e];
        ss->cmin[c]++;
        ss->dmin[e]++;
        sv_push(sv, c);
        sv_push(sv, sv->k + e);
    }
    return true;
}

/* Settle tile t as having the arrow d: false if it can't have it */
static bool ss_assign(struct msolver *sv, struct sstate *ss, int t, int d)
{
    int e;

    if (!(ss->dom[t] & (1u << d))) return false;
    for (e = 0; e < NDIR; e++)
        if (e != d && (ss->dom[t] & (1u << e)))
            if (!ss_remove(sv, ss, t, e)) return false;
    return true;
}

/* Follow up what has changed: false if something can't be met any more */
static bool ss_propagate(struct msolver *sv, struct sstate *ss)
{
    while (sv->qlen > 0) {
        int id = sv_pop(sv);
        int i;

        if (id < sv->k) {
            int c = id, v = sv->clue[c];

            if (ss->cmin[c] > v || ss->cmax[c] < v) goto fail;
            if (ss->cmin[c] == v && ss->cmax[c] > v) {
                for (i = sv->lstart[c]; i < sv->lstart[c + 1]; i++) {
                    int t = sv->lcell[i], d = sv->ldir[i];
                    unsigned dom = ss->dom[t];
                    if ((dom & (1u << d)) && (dom & (dom - 1)))
                        if (!ss_remove(sv, ss, t, d)) goto fail;
                }
            } else if (ss->cmax[c] == v && ss->cmin[c] < v) {
                for (i = sv->lstart[c]; i < sv->lstart[c + 1]; i++) {
                    int t = sv->lcell[i], d = sv->ldir[i];
                    unsigned dom = ss->dom[t];
                    if ((dom & (1u << d)) && (dom & (dom - 1)))
                        if (!ss_assign(sv, ss, t, d)) goto fail;
                }
            }
        } else {
            int d = id - sv->k, v = sv->kcount[d], t;

            if (ss->dmin[d] > v || ss->dmax[d] < v) goto fail;
            if (ss->dmin[d] == v && ss->dmax[d] > v) {
                for (t = 0; t < sv->n; t++) {
                    unsigned dom = ss->dom[t];
                    if ((dom & (1u << d)) && (dom & (dom - 1)))
                        if (!ss_remove(sv, ss, t, d)) goto fail;
                }
            } else if (sv->kindforce && ss->dmax[d] == v && ss->dmin[d] < v) {
                for (t = 0; t < sv->n; t++) {
                    unsigned dom = ss->dom[t];
                    if ((dom & (1u << d)) && (dom & (dom - 1)))
                        if (!ss_assign(sv, ss, t, d)) goto fail;
                }
            }
        }
    }
    return true;

fail:
    sv_drain(sv);
    return false;
}

/* Lay the arrows that are there already (all but the one on tile skip, if
 * that is 0 or more) and follow that up.  The state is of no use if false. */
static bool ss_apply_grid(struct msolver *sv, struct sstate *ss,
                          const signed char *grid, int skip)
{
    int t;

    for (t = 0; t < sv->n; t++)
        if (t != skip && grid[t] >= 0)
            if (!ss_assign(sv, ss, t, grid[t])) {
                sv_drain(sv);
                return false;
            }
    return ss_propagate(sv, ss);
}

/*
 * What the level Hard adds: "what if".  A tile that can still have several
 * arrows is tried with each of them in turn (and the clues and counts are
 * followed up as usual); if all but one of them end in a contradiction, the
 * tile has the one that is left.  Returns 1 if there is such a tile (it is
 * settled in ss, and what follows from that is not followed up), 0 if there
 * is none and -1 if some tile has no arrow that doesn't end in a
 * contradiction (then the position is no good).  scratch is a state of the
 * same solver, used for the tries.  The tile with the fewest arrows to try
 * comes first, and the tile and its arrow are given if the pointers aren't
 * NULL.
 */
static int ss_whatif(struct msolver *sv, struct sstate *ss,
                     struct sstate *scratch, int *tile, int *dir)
{
    int pass, t, e;

    for (pass = 2; pass <= NDIR; pass++) {
        for (t = 0; t < sv->n; t++) {
            unsigned dom = ss->dom[t], refuted = 0, rest;

            if (!(dom & (dom - 1)) || popcnt8(dom) != pass) continue;
            for (e = 0; e < NDIR; e++) {
                if (!(dom & (1u << e))) continue;
                sv->work -= (double)sv->n + 2.0 * sv->k;
                if (sv->work < 0) return 0;
                ss_copy(sv, scratch, ss);
                if (!ss_assign(sv, scratch, t, e) ||
                    !ss_propagate(sv, scratch)) {
                    sv_drain(sv);
                    refuted |= 1u << e;
                }
            }
            if (refuted == dom) return -1;
            rest = dom & ~refuted;
            if (!(rest & (rest - 1))) {
                if (tile) *tile = t;
                if (dir) *dir = lowbit(rest);
                if (!ss_assign(sv, ss, t, lowbit(rest))) {
                    sv_drain(sv);
                    return -1;
                }
                return 1;
            }
        }
    }
    return 0;
}

/*
 * Follow up the clues as far as the level lets one: 1 if every tile is
 * settled (that is the one solution), 0 if not, -1 if it comes to a
 * contradiction.  ss must be at the start or on its way (what has been laid
 * is in it); the state is changed.
 */
static int solve_level(struct msolver *sv, struct sstate *ss,
                       struct sstate *scratch, int level)
{
    sv->kindforce = level >= DIFF_NORMAL;
    for (;;) {
        int r;

        if (!ss_propagate(sv, ss)) return -1;
        if (ss_open(sv, ss) == 0) return 1;
        if (level < DIFF_HARD) return 0;
        r = ss_whatif(sv, ss, scratch, NULL, NULL);
        if (r <= 0) return r;
    }
}

/*
 * How much work the reasoning may take where a person is waiting for it (a
 * hint, Solve): far more than any puzzle that is made here needs.
 */
#define LOGIC_WORK 2e9

/*
 * The arrangement that the clues lead to by reasoning alone, as far as the
 * level Hard goes, with the arrows of grid (if it isn't NULL) laid: 1 and the
 * arrangement in sol if that is the end of it, 0 if the reasoning stops short
 * (or has used up its work), -1 if the clues and the arrows can't be met.
 */
static int logical_solution(const struct mosco_clues *cl,
                            const signed char *grid, signed char *sol)
{
    struct msolver *sv = msolver_new(cl);
    struct sstate *ss = ss_new(sv), *scratch;
    int r, t;

    sv->work = LOGIC_WORK;
    if (grid && !ss_apply_grid(sv, ss, grid, -1)) {
        r = -1;
    } else {
        scratch = ss_dup(sv, ss);
        r = solve_level(sv, ss, scratch, DIFF_HARD);
        ss_free(scratch);
    }
    if (r == 1 && sol)
        for (t = 0; t < cl->n; t++) sol[t] = lowbit(ss->dom[t]);
    sv_drain(sv);
    ss_free(ss);
    msolver_free(sv);
    return r;
}

#define MAX_SEARCH_DEPTH 3000

/*
 * Find an arrangement, in the state that ss is the narrowed-down start of
 * (ss is taken apart by this).  The arrows of pref (if it isn't NULL) are
 * tried first, so that a search that starts from a solution that fits ends
 * up with just that.  Returns 1 for found (in out), 0 for there is none and
 * -1 for gave up (after *budget guesses).
 */
static int ss_search(struct msolver *sv, struct sstate *ss,
                     const signed char *pref, signed char *out,
                     long *budget, int depth)
{
    for (;;) {
        int t, best = -1, bestn = NDIR + 1, v, res;
        struct sstate *child;
        bool ok;

        /* the undecided tile with the fewest arrows to choose from */
        for (t = 0; t < sv->n; t++) {
            unsigned dom = ss->dom[t];
            if (dom & (dom - 1)) {
                int pc = popcnt8(dom);
                if (pc < bestn) {
                    best = t;
                    bestn = pc;
                    if (pc == 2) break;
                }
            }
        }
        if (best < 0) {
            for (t = 0; t < sv->n; t++) out[t] = lowbit(ss->dom[t]);
            return 1;
        }

        if (*budget <= 0 || depth > MAX_SEARCH_DEPTH) return -1;
        (*budget)--;

        if (pref && (ss->dom[best] & (1u << pref[best])))
            v = pref[best];
        else
            v = lowbit(ss->dom[best]);

        child = ss_dup(sv, ss);
        ok = ss_assign(sv, child, best, v) && ss_propagate(sv, child);
        if (ok) {
            res = ss_search(sv, child, pref, out, budget, depth + 1);
        } else {
            sv_drain(sv);
            res = 0;
        }
        ss_free(child);
        if (res != 0) return res;

        /* it can't be v: go on with what is left */
        if (!ss_remove(sv, ss, best, v) || !ss_propagate(sv, ss)) {
            sv_drain(sv);
            return 0;
        }
    }
}

/* How many guesses a search may take before it gives up: about what can be
 * done within a fraction of a second, and less the bigger the board is. */
static long search_budget(int n)
{
    long b = 12000000L / (n + 100);
    if (b > 100000L) b = 100000L;
    if (b < 100L) b = 100L;
    return b;
}

/*
 * A search that works by chance.  Start from a random arrangement with the
 * right number of every kind of arrow (the arrows that are laid stay where
 * they are), then swap the arrows of two tiles and keep the swap if the
 * clues come out no further off, or - very rarely - if they come out a
 * little worse.  Swaps keep the number of every kind of arrow right, and
 * what is left to match is the clues.  These puzzles have so very many
 * solutions that this finds one in a few milliseconds even where a search
 * with nothing but the narrowing above is lost for minutes.
 */
struct rng {
    unsigned long long s;
};

static unsigned rng_next(struct rng *r)
{
    r->s ^= r->s << 13;
    r->s ^= r->s >> 7;
    r->s ^= r->s << 17;
    return (unsigned)(r->s >> 16);
}

/* The same position always gets the same random numbers, so the same hint */
static void rng_seed(struct rng *r, const signed char *grid, int n)
{
    int i;
    r->s = 0x9e3779b97f4a7c15ULL ^ (unsigned long long)n;
    if (grid)
        for (i = 0; i < n; i++)
            r->s = r->s * 6364136223846793005ULL + (unsigned long long)(grid[i] + 2) + 1442695040888963407ULL;
    r->s |= 1;
}

static bool anneal(const struct mosco_clues *cl, const signed char *grid,
                   signed char *out, long moves, int tries, struct rng *rng)
{
    int n = cl->n, k = cl->k, i, d, nfree = 0, np = 0, rem[NDIR], attempt;
    int *cnt = snewn(k, int);
    int *freet = snewn(n, int);
    signed char *pool = snewn(n, signed char);
    bool found = false;

    for (d = 0; d < NDIR; d++) rem[d] = cl->kcount[d];
    for (i = 0; i < n; i++) {
        if (grid && grid[i] >= 0) {
            out[i] = grid[i];
            rem[(int)grid[i]]--;
        } else {
            freet[nfree++] = i;
        }
    }
    for (d = 0; d < NDIR; d++)
        if (rem[d] < 0) goto done;
    for (d = 0; d < NDIR; d++) {
        int c;
        for (c = 0; c < rem[d]; c++) pool[np++] = d;
    }
    assert(np == nfree);

    for (attempt = 0; attempt < tries && !found; attempt++) {
        long m;
        int cost = 0;

        for (i = np - 1; i > 0; i--) {
            int j = rng_next(rng) % (i + 1);
            signed char t = pool[i];
            pool[i] = pool[j];
            pool[j] = t;
        }
        for (i = 0; i < nfree; i++) out[freet[i]] = pool[i];
        for (i = 0; i < k; i++) cnt[i] = 0;
        for (i = 0; i < n; i++) cnt[cl->target[i * NDIR + out[i]]]++;
        for (i = 0; i < k; i++) cost += abs(cnt[i] - cl->clue[i]);

        for (m = 0; m < moves && cost > 0 && nfree >= 2; m++) {
            int a = freet[rng_next(rng) % nfree];
            int b = freet[rng_next(rng) % nfree];
            int da = out[a], db = out[b];
            int ks[4], u[4], nu = 0, x, y, delta, oldc = 0, newc = 0;

            if (da == db) continue;
            ks[0] = cl->target[a * NDIR + da];
            ks[1] = cl->target[a * NDIR + db];
            ks[2] = cl->target[b * NDIR + db];
            ks[3] = cl->target[b * NDIR + da];
            for (x = 0; x < 4; x++) {
                for (y = 0; y < nu; y++) if (u[y] == ks[x]) break;
                if (y == nu) u[nu++] = ks[x];
            }
            for (x = 0; x < nu; x++) oldc += abs(cnt[u[x]] - cl->clue[u[x]]);
            cnt[ks[0]]--; cnt[ks[1]]++; cnt[ks[2]]--; cnt[ks[3]]++;
            for (x = 0; x < nu; x++) newc += abs(cnt[u[x]] - cl->clue[u[x]]);
            delta = newc - oldc;
            if (delta <= 0 || (delta == 1 && (rng_next(rng) & 1023) == 0)) {
                out[a] = db;
                out[b] = da;
                cost += delta;
            } else {
                cnt[ks[0]]++; cnt[ks[1]]--; cnt[ks[2]]++; cnt[ks[3]]--;
            }
        }
        if (cost == 0) found = true;
        if (nfree < 2) break;   /* nothing to swap: there is only this one */
    }

done:
    sfree(cnt);
    sfree(freet);
    sfree(pool);
    return found;
}

/*
 * An arrangement for the clues, with some arrows (or none) laid already:
 * 1 for found (in out), 0 for there is none and -1 for couldn't tell.  If
 * the arrangement that the puzzle was made from (pref, may be NULL) fits
 * with the arrows laid, that is the one.  Else a short search with the
 * narrowing (which is the way to go where most of the arrows are laid
 * already, and can show that there is none), then the search by chance.
 * The higher the effort the longer it goes on.
 */
static int find_solution(const struct mosco_clues *cl, const signed char *grid,
                         const signed char *pref, signed char *out, int effort)
{
    int n = cl->n, i, res;
    struct msolver *sv;
    struct sstate *ss;
    struct rng rng;
    long budget, moves;

    if (pref) {
        bool fits = true;
        for (i = 0; i < n; i++)
            if (grid && grid[i] >= 0 && grid[i] != pref[i]) fits = false;
        if (fits) {
            memcpy(out, pref, n);
            return 1;
        }
    }

    sv = msolver_new(cl);
    ss = ss_new(sv);
    if (grid ? !ss_apply_grid(sv, ss, grid, -1) : !ss_propagate(sv, ss)) {
        res = 0;
    } else {
        budget = search_budget(n) / 4 * effort;
        res = ss_search(sv, ss, pref, out, &budget, 0);
    }
    ss_free(ss);
    msolver_free(sv);
    if (res >= 0) return res;

    moves = 600L * n;
    if (moves > 4000000L) moves = 4000000L;
    rng_seed(&rng, grid, n);
    if (anneal(cl, grid, out, moves, 6 * effort, &rng)) return 1;

    if (effort > 1) {
        /* a puzzle that is typed in may have just one solution, and that
         * is a job for the narrowing */
        sv = msolver_new(cl);
        ss = ss_new(sv);
        if (grid ? !ss_apply_grid(sv, ss, grid, -1) : !ss_propagate(sv, ss)) {
            res = 0;
        } else {
            budget = search_budget(n) * effort;
            res = ss_search(sv, ss, pref, out, &budget, 0);
        }
        ss_free(ss);
        msolver_free(sv);
        return res;
    }
    return -1;
}

/* ----------------------------------------------------------------------
 * The generator
 *
 * A puzzle is the clues of an arrangement, and the arrangement has to be the
 * only one that comes to these clues, and one that the reasoning of the level
 * gets to without a guess.  An arrangement that is made at random is hardly
 * ever that (the clues are far too few to say where the arrows of a big arena
 * are), so there are two ways of looking for one.
 *
 * The search.  Start with a random arrangement that has the right number of
 * every kind, swap the arrows of two tiles (that keeps the number of every
 * kind right) and keep the swap if the number of arrows that are still open
 * after the reasoning of the level isn't any bigger - or, now and then, if it
 * is a little bigger, less so as it goes (that is simulated annealing); and
 * go on until nothing is open.  Every swap takes a round of reasoning over
 * the whole board, so this is for boards of a few dozen tiles.
 *
 * The chain.  Put the clue squares in a random order and let every tile point
 * at the first of them that it can.  The first square then has all the tiles
 * that can point at it pointing at it, the second all but those that went to
 * the first, and so on: every clue is as big as it can be once those before
 * it are out of the way, and that is just what the reasoning of Easy sees,
 * one clue after the other, so nothing but the one arrangement comes to these
 * clues.  That costs next to nothing, whatever the size of the arena (the
 * kinds of arrow have to come out right, and the order is shuffled until they
 * do).  A big board is made with it, but for a patch of it - the core: the
 * tiles all of whose squares are left out of the chain - which is a small
 * puzzle of its own, with the clue squares that its tiles can point at and
 * the kinds of arrow that the chain leaves over.  The search is done on that,
 * for the level that is wanted.  (The chain can't see the core and the core
 * can't see the chain: the core's squares are pointed at by the core's tiles
 * only, and the squares of the chain by the tiles of the chain only.  So the
 * whole puzzle takes the reasoning that the core takes, besides the Easy one
 * of the chain.)
 *
 * Levels (see the comment at the top for what each of them may use):
 *   Easy:   nothing is open after the reasoning of Easy
 *   Normal: nothing is open after that of Normal, but there is after Easy's
 *   Hard:   some arrows are open after that of Normal, but with a "what if"
 *           here and there all of them can be told
 */

/* Boards with up to this many tiles are done by the search alone */
#define SEARCH_TILES 72
/* The core of a bigger board has about this many tiles (and at most
 * SEARCH_TILES).  A small core is found as easily as a big one, and a
 * puzzle that is no good as a core is soon given up: so many short goes, at
 * other places of the board, are better than a few long ones. */
#define CORE_TILES_EASY 32
#define CORE_TILES 24
#define CORE_ITERS 300
/* The core of a puzzle that is not Easy has at least about this many tiles
 * (a smaller one is hardly a puzzle of the level) */
#define MIN_CORE 6
/* Boards up to this size that have no chain are left to the search too */
#define SEARCH_MAX_TILES 256
/*
 * How much work a level may take at most, before the one below is looked at
 * (Easy has the last say and doesn't stop at the first limit).  The unit of
 * it is about what a step of the annealing takes for a tile: some tens of
 * nanoseconds.  It is a number of steps and not a time so that the same
 * seed makes the same puzzle on any machine, fast or slow.
 */
#define LEVEL_WORK 1.5e8
#define EASY_WORK 2e9

struct mrand {
    unsigned long long s;
};

static unsigned mrand_next(struct mrand *r)
{
    r->s ^= r->s >> 12;
    r->s ^= r->s << 25;
    r->s ^= r->s >> 27;
    return (unsigned)((r->s * 0x2545F4914F6CDD1DULL) >> 32);
}

static unsigned mrand_below(struct mrand *r, unsigned n)
{
    return mrand_next(r) % n;
}

struct gen {
    struct msolver *sv;
    struct sstate *ss, *scratch, *check;
    struct mosco_clues *cl;     /* the clues of sol (changed with it) */
    signed char *sol;
    int n, k, level, theta;
    int *und[2];                /* the tiles that are open, of the kept position and of the try */
    int nund[2];
    long spent;                 /* the swaps that the last go has looked at */
    struct mrand rng;
};

#define gen_rand(g) mrand_next(&(g)->rng)

/* A generator for puzzles of the shape of shape (the clues of it are not used) */
static struct gen *gen_new(const struct mosco_clues *shape, int level,
                           unsigned long long seed)
{
    struct gen *g = snew(struct gen);
    int i;

    g->n = shape->n;
    g->k = shape->k;
    g->level = level;
    g->theta = 4 + g->n / 8;
    g->cl = clues_new_core(shape->n, shape->k, shape->target, shape->kcount);
    g->sv = msolver_new(g->cl);
    g->ss = ss_new(g->sv);
    sv_drain(g->sv);
    g->scratch = ss_new(g->sv);
    sv_drain(g->sv);
    g->check = ss_new(g->sv);
    sv_drain(g->sv);
    g->sol = snewn(g->n, signed char);
    for (i = 0; i < 2; i++) {
        g->und[i] = snewn(g->n, int);
        g->nund[i] = 0;
    }
    g->spent = 0;
    g->rng.s = seed | 1;
    return g;
}

static void gen_free(struct gen *g)
{
    ss_free(g->ss);
    ss_free(g->scratch);
    ss_free(g->check);
    msolver_free(g->sv);
    clues_free(g->cl);
    sfree(g->sol);
    sfree(g->und[0]);
    sfree(g->und[1]);
    sfree(g);
}

/* The clues that an arrangement comes to */
static void arrangement_clues(const struct mosco_clues *cl,
                              const signed char *sol, int *clue)
{
    int i;

    for (i = 0; i < cl->k; i++) clue[i] = 0;
    for (i = 0; i < cl->n; i++)
        clue[cl->target[i * NDIR + sol[i]]]++;
}

static void gen_make_clues(struct gen *g)
{
    arrangement_clues(g->cl, g->sol, g->cl->clue);
}

/* Swap the arrows of two tiles (and the clues with it); doing it again
 * takes it back */
static void gen_swap(struct gen *g, int a, int b)
{
    int da = g->sol[a], db = g->sol[b];
    int *clue = g->cl->clue;
    const int *target = g->cl->target;

    clue[target[a * NDIR + da]]--;
    clue[target[b * NDIR + db]]--;
    clue[target[a * NDIR + db]]++;
    clue[target[b * NDIR + da]]++;
    g->sol[a] = db;
    g->sol[b] = da;
}

/*
 * The position that the clues of sol come to, after what the level can
 * reason out (Hard starts with what Normal can): the number of arrows
 * that are open.  The state is left in g->ss and the tiles that are open in
 * g->und[which].
 */
static int gen_cost(struct gen *g, int which)
{
    int t, cost = 0;
    bool ok;

    g->sv->kindforce = g->level >= DIFF_NORMAL;
    ss_reset(g->sv, g->ss);
    ok = ss_propagate(g->sv, g->ss);
    assert(ok);                 /* an arrangement meets its own clues */
    (void)ok;
    g->nund[which] = 0;
    for (t = 0; t < g->n; t++) {
        int pc = popcnt8(g->ss->dom[t]);
        if (pc > 1) {
            cost += pc - 1;
            g->und[which][g->nund[which]++] = t;
        }
    }
    return cost;
}

/*
 * Is the position in g->ss (with the cost) a puzzle of the level?  1 if it
 * is; 2 if it is just short of that - the reasoning of the level gets to the
 * end but so does that of the level below (or, for Hard, that of Normal),
 * which is where the search for one that does need the more sets out from;
 * and 0 if it is none of that.
 */
static int gen_done(struct gen *g, int cost)
{
    if (g->level == DIFF_EASY)
        return cost == 0;
    if (g->level == DIFF_NORMAL) {
        if (cost > 0) return 0;
        /* does Easy get to the end as well? */
        g->sv->kindforce = false;
        ss_reset(g->sv, g->check);
        if (!ss_propagate(g->sv, g->check)) { sv_drain(g->sv); return 0; }
        return ss_open(g->sv, g->check) > 0 ? 1 : 2;
    }
    if (cost == 0) return 2;
    if (cost > g->theta) return 0;
    ss_copy(g->sv, g->check, g->ss);
    return solve_level(g->sv, g->check, g->scratch, DIFF_HARD) == 1;
}

/*
 * What the chance is (of 65536) of keeping a swap that makes it worse by one
 * open arrow, for the sixteen steps of the cooling: e^(-1/T) for T that goes
 * from 3 down to 0.3.  (Done with whole numbers so that the same seed makes
 * the same puzzle on any machine.)  With d open arrows more it is that to
 * the power d.
 */
static const unsigned short sa_keep[16] = {
    46959, 44432, 41657, 38641, 35398, 31959, 28369, 24690,
    20998, 17384, 13949, 10791, 8000, 5643, 3757, 2338
};

/* A random arrangement with the right number of every kind of arrow */
static void random_arrangement(const struct mosco_clues *cl, signed char *sol,
                               struct mrand *rng)
{
    int n = cl->n, pos = 0, d, c, i;

    for (d = 0; d < NDIR; d++)
        for (c = 0; c < cl->kcount[d]; c++) sol[pos++] = d;
    assert(pos == n);
    for (i = n - 1; i > 0; i--) {
        int j = mrand_below(rng, i + 1);
        signed char t = sol[i];
        sol[i] = sol[j];
        sol[j] = t;
    }
}

/*
 * One go: a random arrangement and iters swaps.  True if a puzzle of the
 * level has come out (the arrangement is g->sol, and its clues are in g->cl).
 *
 * For Normal and Hard the first part may get to an arrangement that is just
 * short of one (see gen_done), and then the swaps go on among those: a swap
 * is kept as long as the reasoning of the level still gets to the end, and
 * for Hard every swap that is not kept is looked at as well - if the basic
 * reasoning stops short but a "what if" gets to the end, that is the puzzle.
 */
static bool gen_attempt(struct gen *g, long iters)
{
    int n = g->n, kept = 0, cost, cur, r;
    long it;

    g->spent = 0;
    random_arrangement(g->cl, g->sol, &g->rng);
    gen_make_clues(g);
    cost = gen_cost(g, kept);
    cur = cost;
    r = gen_done(g, cost);
    if (r == 1) return true;

    for (it = 0; r == 0 && it < iters; it++) {
        int a, b, c2, d;

        g->spent = it + 1;

        if (g->nund[kept] > 0 && (gen_rand(g) & 3))
            a = g->und[kept][gen_rand(g) % g->nund[kept]];
        else
            a = gen_rand(g) % n;
        b = gen_rand(g) % n;
        if (g->sol[a] == g->sol[b]) continue;

        gen_swap(g, a, b);
        c2 = gen_cost(g, 1 - kept);
        d = c2 - cur;
        if (d > 0) {
            unsigned keep = sa_keep[it * 16 / iters], chance = 65536;
            int j;
            for (j = 0; j < d && chance; j++) chance = (chance * keep) >> 16;
            if ((gen_rand(g) & 0xffff) >= chance) {
                gen_swap(g, a, b);
                continue;
            }
        }
        cur = cost = c2;
        kept = 1 - kept;
        r = gen_done(g, cost);
        if (r == 1) return true;
    }
    if (r != 2) return false;

    /* on from there, among the arrangements that are just short */
    for (it = 0; it < iters / 2; it++) {
        int a = gen_rand(g) % n, b = gen_rand(g) % n, c2;

        g->spent = iters + it + 1;
        if (g->sol[a] == g->sol[b]) continue;
        gen_swap(g, a, b);
        c2 = gen_cost(g, 1 - kept);
        if (c2 == 0 || g->level == DIFF_HARD) {
            r = gen_done(g, c2);
            if (r == 1) return true;
            if (r == 2) continue;       /* it is still just short: keep it */
        }
        gen_swap(g, a, b);
    }
    return false;
}

#ifdef MOSCO_DEBUG
/* (for the tests that tune the generator: -1 is "as it is") */
static int dbg_core_easy = -1, dbg_core_hard = -1, dbg_core_iters = -1, dbg_core_goes = -1;
static int dbg_search_iters = -1, dbg_nochain = 0;
#endif

/* The number of swaps that a go takes, by the size of the arena */
static long gen_iters(int n)
{
#ifdef MOSCO_DEBUG
    if (dbg_search_iters >= 0) return (long)dbg_search_iters * n;
#endif
    return 4500L * n;
}

/*
 * The clues of the board cl are set to those of sol: does the reasoning of
 * the level bring them to sol, and does that of the level below not?  (sv is
 * the solver of cl.  The one that checks has nothing to do with how the
 * puzzle was made.)
 */
static bool verify_puzzle(struct msolver *sv, const signed char *sol, int level)
{
    struct sstate *s1 = ss_new(sv), *s2 = ss_new(sv);
    bool ok = false;
    int i;

    ss_reset(sv, s1);           /* every constraint is to be looked at */
    if (solve_level(sv, s1, s2, level) == 1) {
        ok = true;
        for (i = 0; i < sv->n; i++)
            if (lowbit(s1->dom[i]) != sol[i]) ok = false;
        if (ok && level > DIFF_EASY) {
            ss_reset(sv, s1);
            if (solve_level(sv, s1, s2, level - 1) == 1) ok = false;
        }
    }
    sv_drain(sv);
    sv->kindforce = true;
    ss_free(s1);
    ss_free(s2);
    return ok;
}

#ifdef MOSCO_DEBUG
struct mosco_stats { long blocks, toobig, infeasible, nobalance, coresearch, corefail, verifyfail, found; long coresum; };
static struct mosco_stats mstats;
#define STAT(x) (mstats.x++)
#else
#define STAT(x) ((void)0)
#endif

/* How many goes of the search at most for the same core or board */
#define SEARCH_GOES 100
#define CORE_GOES 12
/* How many cores and chains at most */
#define CHAIN_GOES 200
/* The chain needs this many clue squares outside the shadow of the core to come
 * out right with the kinds of arrow.  (With more than this, a board of 73 to 104
 * tiles turned down too many blocks and ended up with an easier puzzle than the
 * level asked for.) */
#define CHAIN_SQUARES 20
#ifdef MOSCO_DEBUG
static int dbg_chain_squares = -1;
#endif

/* The search on a board with few tiles (work: how much of it is left, see
 * LEVEL_WORK) */
static bool gen_search(struct mosco_clues *cl, struct msolver *sv, int level,
                       struct mrand *rng, signed char *sol, double *work)
{
    struct gen *g;
    long iters = gen_iters(cl->n);
    int attempt;
    bool found = false;

    g = gen_new(cl, level, ((unsigned long long)mrand_next(rng) << 32) | mrand_next(rng));
    for (attempt = 0; attempt < SEARCH_GOES && !found && *work > 0; attempt++) {
        bool ok = gen_attempt(g, iters);

        *work -= (double)g->spent * cl->n;
        if (!ok) continue;
        memcpy(sol, g->sol, cl->n);
        arrangement_clues(cl, sol, cl->clue);
        found = verify_puzzle(sv, sol, level);
        *work -= 20.0 * cl->n;
    }
    gen_free(g);
    return found;
}

/*
 * The chain: what the tiles of a board are made to point at by the clue
 * squares that are in it.  Every square of the chain has a key, and the
 * order of the chain is that of the keys (the same key: the number of the
 * square first).
 */
struct chain {
    const struct mosco_clues *cl;       /* the board */
    const struct msolver *sv;           /* the tiles that can point at each clue */
    long long *key;                     /* k: the place of a clue square in the order,
                                         * KEY_NONE if it is not in the chain */
    signed char *claim;                 /* n: the arrow a tile gets, -1 if none does */
    int count[NDIR];                    /* how many tiles have each arrow */
    long spent;                         /* the work that the last balance has taken */
};

#define KEY_NONE LLONG_MAX

/* Is the square c1 of the chain before the square c2 of it? */
static bool chain_before(const struct chain *ch, int c1, int c2)
{
    return ch->key[c1] < ch->key[c2] ||
           (ch->key[c1] == ch->key[c2] && c1 < c2);
}

/* The arrow of tile t that points at the first square of the chain that it can
 * point at, leaving out the arrow skip (-1: none): -1 if there is none */
static int chain_arrow_but(const struct chain *ch, int t, int skip)
{
    const int *tg = ch->cl->target + t * NDIR;
    int d, ad = -1;

    for (d = 0; d < NDIR; d++) {
        if (d == skip || ch->key[tg[d]] == KEY_NONE) continue;
        if (ad < 0 || chain_before(ch, tg[d], tg[ad])) ad = d;
    }
    return ad;
}

static void chain_refresh(struct chain *ch, int t)
{
    int a = chain_arrow_but(ch, t, -1), old = ch->claim[t];

    if (a != old) {
        if (old >= 0) ch->count[old]--;
        if (a >= 0) ch->count[a]++;
        ch->claim[t] = a;
    }
}

/* The clue square c has a new place in the order: look at the tiles that can point at it */
static void chain_refresh_square(struct chain *ch, int c)
{
    int i;

    for (i = ch->sv->lstart[c]; i < ch->sv->lstart[c + 1]; i++)
        chain_refresh(ch, ch->sv->lcell[i]);
}

/* How far the numbers of the arrows that are left over (what the board has of
 * each kind, less what the chain has given) are from lo to hi */
static int chain_penalty(const struct chain *ch, int lo, int hi)
{
    int d, pen = 0;

    for (d = 0; d < NDIR; d++) {
        int left = ch->cl->kcount[d] - ch->count[d];
        if (left < lo) pen += lo - left;
        else if (left > hi) pen += left - hi;
    }
    return pen;
}

/*
 * Change the places of squares of the chain now and then (sq has the nq
 * squares of it) until the arrows that are left over come to between lo and
 * hi of each kind.  Most of the changes are made for a purpose: if the chain
 * gives too few of a kind of arrow, a tile that has another is made to have
 * this kind by taking the square that it points at with it to before the
 * square that it has now; if it gives too many, a tile that has one is made
 * to have its next by taking the square that it points at with it to after
 * that.  (That changes the other tiles that point at the square as well, so a
 * change is kept only if the numbers don't come out worse.)  False if it
 * hasn't worked out within the work (about the number of tiles that have
 * been looked at).
 */
static bool chain_balance(struct chain *ch, const int *sq, int nq, int lo, int hi,
                          struct mrand *rng, long work)
{
    int n = ch->cl->n, pen = chain_penalty(ch, lo, hi);
    const int *lstart = ch->sv->lstart;
    const int *target = ch->cl->target;

    ch->spent = 0;
    while (work > 0 && pen > 0 && nq >= 1) {
        int c = -1, d, np, tries, nfew = 0, nmany = 0;
        int few[NDIR], many[NDIR];
        long long oldkey;
        unsigned mode = mrand_below(rng, 8);

        work--;
        ch->spent++;
        for (d = 0; d < NDIR; d++) {
            int left = ch->cl->kcount[d] - ch->count[d];
            if (left > hi) few[nfew++] = d;
            else if (left < lo) many[nmany++] = d;
        }

        if (mode < 5 && (nfew > 0 || nmany > 0)) {
            bool more = nfew > 0 && (nmany == 0 || (mrand_next(rng) & 1));
            int kind = more ? few[mrand_below(rng, nfew)] : many[mrand_below(rng, nmany)];
            long long newkey = 0;
            bool have = false;

            /* of some tiles (at random) the one that gives the smallest
             * change of the place of the square */
            for (tries = 0; tries < 40; tries++) {
                int t = (int)mrand_below(rng, n), cur = ch->claim[t];
                int sqr;
                long long nk;

                if (cur < 0) continue;
                if (more) {
                    sqr = target[t * NDIR + kind];
                    if (cur == kind || ch->key[sqr] == KEY_NONE) continue;
                    nk = ch->key[target[t * NDIR + cur]] - 1;
                    if (!have || nk > newkey) {
                        have = true;
                        c = sqr;
                        newkey = nk;
                    }
                } else {
                    int next;
                    if (cur != kind) continue;
                    next = chain_arrow_but(ch, t, kind);
                    if (next < 0) continue;
                    nk = ch->key[target[t * NDIR + next]] + 1;
                    if (!have || nk < newkey) {
                        have = true;
                        c = target[t * NDIR + kind];
                        newkey = nk;
                    }
                }
            }
            if (!have) continue;
            oldkey = ch->key[c];
            ch->key[c] = newkey;
        } else {
            /* any square, to some other place */
            c = sq[mrand_below(rng, nq)];
            oldkey = ch->key[c];
            if (mrand_next(rng) & 1)
                ch->key[c] = ((long long)(mrand_next(rng) & 0x7fffffff) << 24) + c;
            else
                ch->key[c] = oldkey + (long long)(mrand_next(rng) & 0x7ffffff) - 0x4000000;
        }
        work -= 2 * (lstart[c + 1] - lstart[c]);
        ch->spent += 2 * (lstart[c + 1] - lstart[c]);
        chain_refresh_square(ch, c);
        np = chain_penalty(ch, lo, hi);
        if (np <= pen) {
            pen = np;
        } else {
            ch->key[c] = oldkey;
            chain_refresh_square(ch, c);
        }
    }
    return pen == 0;
}

/*
 * Could a chain of the squares that are not in the core (in_core_sq) come out
 * right?  A quick look that says no to the blocks that would be a waste of
 * the work of the balance:
 *
 * - a kind of arrow that the chain can't make enough of (the tiles that have
 *   a chain square in that direction are too few), or can't help making too
 *   many of (the tiles that have just one chain square, and in that
 *   direction);
 * - the tiles of a line (a column, a row or a diagonal) can have the arrow
 *   to one end of it or the arrow to the other end, but not some of each, as
 *   the one of the two squares that comes first in the order takes them all.
 *   So a line that has a square of the chain at one end only can give that
 *   arrow only, a line that has both can give either (all its tiles or none),
 *   and the lines that have both have to be shared out so that both arrows
 *   have enough.  (In a board that is a strip the tiles of a column all point
 *   at the same two squares, and there it matters a good deal.)
 */
static bool chain_feasible(const struct mosco_clues *cl,
                           const unsigned char *in_core_sq, int lo, int hi)
{
    static const int fam_dir[4] = { UP, LEFT, UP_LEFT, DOWN_LEFT };
    int w = cl->w, h = cl->h, n = cl->n, t, d, f, i;
    int pmax[NDIR], pmin[NDIR];
    int nline[4];
    int *lsize[4];
    unsigned char *lend[4];
    bool ok = true;

    nline[0] = w;
    nline[1] = h;
    nline[2] = nline[3] = w + h - 1;
    for (f = 0; f < 4; f++) {
        lsize[f] = snewn(nline[f], int);
        lend[f] = snewn(nline[f], unsigned char);
        memset(lsize[f], 0, nline[f] * sizeof(int));
        memset(lend[f], 0, nline[f]);
    }
    for (d = 0; d < NDIR; d++) pmax[d] = pmin[d] = 0;
    for (t = 0; t < n; t++) {
        int x = t % w, y = t / w, cnt = 0, last = -1;
        int id[4];

        for (d = 0; d < NDIR; d++)
            if (!in_core_sq[cl->target[t * NDIR + d]]) {
                pmax[d]++;
                cnt++;
                last = d;
            }
        if (cnt == 1) pmin[last]++;
        if (cnt == 0) continue;         /* a tile of the core */
        id[0] = x;
        id[1] = y;
        id[2] = x - y + h - 1;
        id[3] = x + y;                  /* (x and y from 0) */
        for (f = 0; f < 4; f++) {
            int d1 = fam_dir[f], d2 = d1 + NDIR / 2;
            lsize[f][id[f]]++;
            lend[f][id[f]] = (!in_core_sq[cl->target[t * NDIR + d1]] ? 1 : 0) |
                             (!in_core_sq[cl->target[t * NDIR + d2]] ? 2 : 0);
        }
    }
    for (d = 0; d < NDIR; d++)
        if (pmax[d] < cl->kcount[d] - hi || pmin[d] > cl->kcount[d] - lo)
            ok = false;

    for (f = 0; f < 4 && ok; f++) {
        int d1 = fam_dir[f], d2 = d1 + NDIR / 2;
        int a1 = 0, a2 = 0, nflex = 0, sf1, sf2;
        int *flex = snewn(nline[f], int);

        for (i = 0; i < nline[f]; i++) {
            if (!lsize[f][i]) continue;
            if (lend[f][i] == 1) a1 += lsize[f][i];
            else if (lend[f][i] == 2) a2 += lsize[f][i];
            else if (lend[f][i] == 3) flex[nflex++] = lsize[f][i];
        }
        sf1 = cl->kcount[d1] - hi - a1;
        sf2 = cl->kcount[d2] - hi - a2;
        if (sf1 < 0) sf1 = 0;
        if (sf2 < 0) sf2 = 0;
        /* the big lines first, each to the arrow that needs more */
        while (nflex > 0 && (sf1 > 0 || sf2 > 0)) {
            int best = 0;
            for (i = 1; i < nflex; i++)
                if (flex[i] > flex[best]) best = i;
            if (sf1 >= sf2) sf1 -= flex[best];
            else sf2 -= flex[best];
            if (sf1 < 0) sf1 = 0;
            if (sf2 < 0) sf2 = 0;
            flex[best] = flex[--nflex];
        }
        if (sf1 > 0 || sf2 > 0) ok = false;
        sfree(flex);
    }
    for (f = 0; f < 4; f++) {
        sfree(lsize[f]);
        sfree(lend[f]);
    }
    return ok;
}

/*
 * A puzzle of the level for the board cl (the clues of it are set): the
 * arrangement is sol.  The core is a patch of about core_target tiles (none:
 * the chain does it all).  False if it hasn't worked out.
 */
static bool gen_chain(struct mosco_clues *cl, struct msolver *sv, int level,
                      struct mrand *rng, signed char *sol, int core_target,
                      int min_core, double *work)
{
    int w = cl->w, h = cl->h, n = cl->n, k = cl->k, t, c, d, i, attempt;
    struct chain ch;
    unsigned char *in_core_sq = snewn(k, unsigned char);
    int *sq = snewn(k, int);
    int *sidx = snewn(k, int);
    int *core_tiles = snewn(SEARCH_TILES + 1, int);
    bool found = false;

    ch.cl = cl;
    ch.sv = sv;
    ch.key = snewn(k, long long);
    ch.claim = snewn(n, signed char);

    for (attempt = 0; attempt < CHAIN_GOES && !found && *work > 0; attempt++) {
        int m = core_target;
        int nk = 0, nq = 0, ns = 0, lo = 0, hi = 0, slack, tries = 0;
        bool usable = m == 0, balanced;

        STAT(blocks);
        memset(in_core_sq, 0, k);
        while (m > 0) {
            /* A block of about m tiles anywhere in the board; the core is
             * the tiles that can only point at the clue squares that the
             * tiles of the block can point at, and the chain is all the
             * other squares.  The chain needs squares enough, and of the
             * right kinds, to come out right with the numbers of arrows,
             * and the core mustn't be too big a puzzle: so another block
             * if it is no good, and a smaller one if that goes on (a small
             * board has a small core or none).  The more square the block,
             * the fewer clue squares its tiles can point at. */
            static const int shape[7] = { 4, 2, 8, 1, 16, 3, 6 };
            int root = 1, a, b, x0, y0, x, y;
            bool fits = true;

            while ((root + 1) * (root + 1) <= m) root++;
            a = root * shape[mrand_below(rng, 2) ? 0 : (int)mrand_below(rng, 7)] / 4;
            if (a < 1) a = 1;
            if (a > w) a = w;
            b = (m + a - 1) / a;
            if (b > h) {
                b = h;
                a = (m + b - 1) / b;
                if (a > w) a = w;
            }
            x0 = (int)mrand_below(rng, w - a + 1);
            y0 = (int)mrand_below(rng, h - b + 1);
            memset(in_core_sq, 0, k);
            ns = 0;
            for (y = y0; y < y0 + b; y++)
                for (x = x0; x < x0 + a; x++)
                    for (d = 0; d < NDIR; d++) {
                        int c2 = cl->target[(y * w + x) * NDIR + d];
                        if (!in_core_sq[c2]) {
                            in_core_sq[c2] = 1;
                            ns++;
                        }
                    }
            nk = 0;
            for (t = 0; t < n && fits; t++) {
                for (d = 0; d < NDIR; d++)
                    if (!in_core_sq[cl->target[t * NDIR + d]]) break;
                if (d == NDIR) {
                    if (nk >= SEARCH_TILES) fits = false;
                    else core_tiles[nk++] = t;
                }
            }
            *work -= 2.0 * n;           /* (what the look at the block takes) */
            if (fits && k - ns >=
#ifdef MOSCO_DEBUG
                (dbg_chain_squares >= 0 ? dbg_chain_squares : CHAIN_SQUARES)
#else
                CHAIN_SQUARES
#endif
                ) {
                /* what the chain may leave of each kind: about nk / 8, and
                 * the more tiles the core has the more it may be off (and
                 * the more often it has gone wrong a little more, but not
                 * much: a core with uneven kinds is a hard search) */
                slack = (nk + 15) / 16 + (attempt / 8 < 3 ? attempt / 8 : 3);
                lo = nk / NDIR - slack;
                if (lo < 0) lo = 0;
                hi = (nk + NDIR - 1) / NDIR + slack;
                if (chain_feasible(cl, in_core_sq, lo, hi)) {
                    usable = true;
                    break;
                }
                STAT(infeasible);
            } else {
                STAT(toobig);
            }
            if (++tries % 6 == 0) {
                if (m <= min_core) break;
                m = m * 3 / 4;
                if (m < min_core) m = min_core;
            }
            if (*work <= 0) break;
        }
        if (m == 0) {
            /* no core: the chain does it all */
            memset(in_core_sq, 0, k);
            nk = ns = 0;
            usable = true;
            lo = 0;
            hi = 0;
        }
        if (!usable) continue;

        /* the other squares are the chain, in a random order */
        for (c = 0; c < k; c++) {
            if (in_core_sq[c]) {
                ch.key[c] = KEY_NONE;
            } else {
                sq[nq++] = c;
                ch.key[c] = ((long long)(mrand_next(rng) & 0x7fffffff) << 24) + c;
            }
        }
        for (d = 0; d < NDIR; d++) ch.count[d] = 0;
        for (t = 0; t < n; t++) {
            ch.claim[t] = -1;
            chain_refresh(&ch, t);
        }

        balanced = chain_balance(&ch, sq, nq, lo, hi, rng, 20000000L + 50L * n);
        *work -= (double)ch.spent + 4.0 * n;
        if (!balanced) {
            STAT(nobalance);
#ifdef MOSCO_DEBUG
            if (getenv("MOSCO_TRACE")) {
                fprintf(stderr, "nobalance: attempt %d m %d nk %d ns %d nq %d window [%d,%d] left:", attempt, m, nk, ns, nq, lo, hi);
                for (d = 0; d < NDIR; d++) fprintf(stderr, " %d", cl->kcount[d] - ch.count[d]);
                fprintf(stderr, "\n");
            }
#endif
            continue;
        }

        if (nk == 0) {
            memcpy(sol, ch.claim, n);
        } else {
            /* the core as a puzzle of its own */
            struct mosco_clues *core;
            struct gen *g;
            int *ctarget = snewn(nk * NDIR, int);
            int kc[NDIR], goes, ngoes;
            long iters;
            bool core_ok = false;

            for (c = 0, ns = 0; c < k; c++) sidx[c] = in_core_sq[c] ? ns++ : -1;
            for (i = 0; i < nk; i++)
                for (d = 0; d < NDIR; d++)
                    ctarget[i * NDIR + d] = sidx[cl->target[core_tiles[i] * NDIR + d]];
            for (d = 0; d < NDIR; d++) kc[d] = cl->kcount[d] - ch.count[d];
            core = clues_new_core(nk, ns, ctarget, kc);
            g = gen_new(core, level, ((unsigned long long)mrand_next(rng) << 32) | mrand_next(rng));
            STAT(coresearch);
#ifdef MOSCO_DEBUG
            mstats.coresum += nk;
#endif
            iters = (long)CORE_ITERS * nk;
            ngoes = CORE_GOES;
#ifdef MOSCO_DEBUG
            if (dbg_core_iters >= 0) iters = (long)dbg_core_iters * nk;
            if (dbg_core_goes >= 0) ngoes = dbg_core_goes;
#endif
            for (goes = 0; goes < ngoes && !core_ok && *work > 0; goes++) {
                core_ok = gen_attempt(g, iters);
                *work -= (double)g->spent * nk;
            }
            if (core_ok) {
                for (t = 0; t < n; t++) sol[t] = ch.claim[t];
                for (i = 0; i < nk; i++) sol[core_tiles[i]] = g->sol[i];
            }
            gen_free(g);
            clues_free(core);
            sfree(ctarget);
            if (!core_ok) { STAT(corefail); continue; }
        }

        arrangement_clues(cl, sol, cl->clue);
        found = verify_puzzle(sv, sol, level);
        *work -= 20.0 * n;
        if (!found) STAT(verifyfail); else STAT(found);
    }

    sfree(in_core_sq);
    sfree(sq);
    sfree(sidx);
    sfree(core_tiles);
    sfree(ch.key);
    sfree(ch.claim);
    return found;
}

static char *new_game_desc(const game_params *params, random_state *rs,
                           char **aux, bool interactive)
{
    int w = params->w, h = params->h, n = w * h, k = 2 * w + 2 * h + 4;
    int i, level;
    int *zeros = snewn(k, int);
    struct mosco_clues *cl;
    struct msolver *sv;
    struct mrand rng;
    signed char *sol = snewn(n, signed char);
    char *desc, *p, *auxs = snewn(n + 1, char);
    bool found = false;

    for (i = 0; i < k; i++) zeros[i] = 0;
    cl = clues_new(w, h, zeros);
    sfree(zeros);
    sv = msolver_new(cl);
    rng.s = ((unsigned long long)random_bits(rs, 32) << 32) | random_bits(rs, 32);
    rng.s |= 1;

    /* If the level can't be had (not for any size that is offered) the one below is */
    for (level = params->diff; level >= DIFF_EASY && !found; level--) {
        double work = level == DIFF_EASY ? EASY_WORK : LEVEL_WORK;

        if (n <= SEARCH_TILES) work = 1e30;     /* (the search is all there is) */
        if (n > SEARCH_TILES
#ifdef MOSCO_DEBUG
            && !dbg_nochain
#endif
            ) {
            int ct = level == DIFF_EASY ? CORE_TILES_EASY : CORE_TILES;
#ifdef MOSCO_DEBUG
            if (level == DIFF_EASY && dbg_core_easy >= 0) ct = dbg_core_easy;
            if (level != DIFF_EASY && dbg_core_hard >= 0) ct = dbg_core_hard;
#endif
            found = gen_chain(cl, sv, level, &rng, sol, ct,
                              level == DIFF_EASY ? 0 : MIN_CORE, &work);
        }
        /* (a board that has too few clue squares for a chain and a core is
         * left to the search, as long as that is a matter of seconds) */
        if (!found && n <= SEARCH_MAX_TILES) {
            /* (the search is a poor way to a bigger board: it has a share
             * of the work only) */
            if (n > SEARCH_TILES && work > LEVEL_WORK / 4) work = LEVEL_WORK / 4;
            found = gen_search(cl, sv, level, &rng, sol, &work);
        }
    }
    if (!found) {
        /* There has to be an end: an arrangement at random (the puzzle may
         * have more solutions) */
        random_arrangement(cl, sol, &rng);
        arrangement_clues(cl, sol, cl->clue);
    }

    desc = snewn(k * 12 + 1, char);
    p = desc;
    for (i = 0; i < k; i++)
        p += sprintf(p, i ? ",%d" : "%d", cl->clue[i]);
    for (i = 0; i < n; i++) auxs[i] = '0' + sol[i];
    auxs[n] = '\0';
    *aux = auxs;
    sv_drain(sv);
    msolver_free(sv);
    clues_free(cl);
    sfree(sol);
    return desc;
}

static bool parse_aux(const char *aux, int n, signed char *out)
{
    int i;

    if (!aux || (int)strlen(aux) != n) return false;
    for (i = 0; i < n; i++) {
        if (aux[i] < '0' || aux[i] > '7') return false;
        out[i] = aux[i] - '0';
    }
    return true;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    int w = params->w, h = params->h, k = 2 * w + 2 * h + 4;
    int *values = snewn(k, int);
    const char *err = parse_clues(params, desc, values);

    if (!err) {
        /* what can be told without a search must not rule the clues out */
        struct mosco_clues *cl = clues_new(w, h, values);
        struct msolver *sv = msolver_new(cl);
        struct sstate *ss = ss_new(sv);
        if (!ss_propagate(sv, ss))
            err = "The clues can't be met";
        ss_free(ss);
        msolver_free(sv);
        clues_free(cl);
    }
    sfree(values);
    return err;
}

/* ----------------------------------------------------------------------
 * The game state
 */

static game_state *blank_state(struct mosco_clues *cl)
{
    game_state *state = snew(game_state);

    state->cl = cl;
    cl->refcount++;
    state->grid = snewn(cl->n, signed char);
    memset(state->grid, -1, cl->n);
    state->marks = snewn(cl->n, unsigned char);
    memset(state->marks, 0, cl->n);
    state->completed = state->used_solve = false;
    return state;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    int k = 2 * params->w + 2 * params->h + 4;
    int *values = snewn(k, int);
    struct mosco_clues *cl;
    game_state *state;
    const char *err = parse_clues(params, desc, values);

    assert(!err);
    cl = clues_new(params->w, params->h, values);
    state = blank_state(cl);
    clues_free(cl);
    sfree(values);
    return state;
}

static game_state *dup_game(const game_state *state)
{
    game_state *ret = snew(game_state);
    int n = state->cl->n;

    ret->cl = state->cl;
    ret->cl->refcount++;
    ret->grid = snewn(n, signed char);
    memcpy(ret->grid, state->grid, n);
    ret->marks = snewn(n, unsigned char);
    memcpy(ret->marks, state->marks, n);
    ret->completed = state->completed;
    ret->used_solve = state->used_solve;
    return ret;
}

static void free_game(game_state *state)
{
    clues_free(state->cl);
    sfree(state->grid);
    sfree(state->marks);
    sfree(state);
}

/*
 * How many arrows of the arrangement point at each clue (cnt, k numbers)
 * and how many of each kind there are (used).  Returns the number of tiles
 * that have an arrow.
 */
static int count_arrows(const struct mosco_clues *cl, const signed char *grid,
                        int *cnt, int *used)
{
    int i, t, laid = 0;

    for (i = 0; i < cl->k; i++) cnt[i] = 0;
    for (i = 0; i < NDIR; i++) used[i] = 0;
    for (t = 0; t < cl->n; t++)
        if (grid[t] >= 0) {
            cnt[cl->target[t * NDIR + grid[t]]]++;
            used[(int)grid[t]]++;
            laid++;
        }
    return laid;
}

/* Is the arrangement of the state a solution?  (If every clue has the
 * arrows it wants all the tiles have one, as the clues add up to n.) */
static bool is_complete(const game_state *state)
{
    const struct mosco_clues *cl = state->cl;
    int *cnt = snewn(cl->k, int);
    int used[NDIR], i;
    bool ok;

    ok = count_arrows(cl, state->grid, cnt, used) == cl->n;
    for (i = 0; ok && i < cl->k; i++)
        if (cnt[i] != cl->clue[i]) ok = false;
    for (i = 0; ok && i < NDIR; i++)
        if (used[i] != cl->n / NDIR) ok = false;
    sfree(cnt);
    return ok;
}

static char *solve_game(const game_state *state, const game_state *curr,
                        const char *aux, const char **error)
{
    int n = state->cl->n, i;
    signed char *sol = snewn(n, signed char);
    char *ret;

    if (!parse_aux(aux, n, sol)) {
        /* a game that was typed in: there is only the solver to ask (the
         * reasoning first: it is the one solution if it gets to the end) */
        int res = logical_solution(state->cl, NULL, sol);
        if (res != 1) res = find_solution(state->cl, NULL, NULL, sol, 4);
        if (res != 1) {
            sfree(sol);
            *error = res == 0 ? "This puzzle has no solution"
                              : "Couldn't find a solution";
            return NULL;
        }
    }
    ret = snewn(n + 2, char);
    ret[0] = 'S';
    for (i = 0; i < n; i++) ret[i + 1] = '0' + sol[i];
    ret[n + 1] = '\0';
    sfree(sol);
    return ret;
}

/* ----------------------------------------------------------------------
 * "Show next step"
 */

extern const char *midend_hint_aux;

static char *hint_move(int w, int t, int d)
{
    char buf[48];
    sprintf(buf, "P%d,%d,%d", t % w, t / w, d);
    return dupstr(buf);
}

static char *mistake_message(int w, int t)
{
    static char errbuf[160];
    sprintf(errbuf, _("There is a mistake in row %d, column %d"),
            t / w + 1, t % w + 1);
    return errbuf;
}

/*
 * What to say about arrows that can't all be right (there is no solution
 * with all of them), when it is for sure that they can't: find the arrow
 * that is in the way, one that - taken away - leaves a position that has a
 * solution.  (Only those that the narrowing finds to be in the way are
 * tried, and if there are several, the one that isn't what the puzzle was
 * made from is the one to blame.)  Returns the tile, or -1 if it isn't one
 * tile that is to blame.
 */
static int find_culprit(const struct mosco_clues *cl, struct msolver *sv,
                        const struct sstate *root, const signed char *grid,
                        const signed char *pref, bool have_pref)
{
    int n = cl->n, t, first = -1, firstdiff = -1;
    signed char *sol = snewn(n, signed char);
    signed char *g = snewn(n, signed char);

    memcpy(g, grid, n);
    for (t = 0; t < n; t++) {
        struct sstate *s2;
        bool ok;

        if (grid[t] < 0) continue;
        s2 = ss_dup(sv, root);
        ok = ss_apply_grid(sv, s2, grid, t);
        ss_free(s2);
        if (!ok) continue;
        g[t] = -1;
        if (find_solution(cl, g, have_pref ? pref : NULL, sol, 1) == 1) {
            if (first < 0) first = t;
            if (firstdiff < 0 && have_pref && grid[t] != pref[t]) firstdiff = t;
        }
        g[t] = grid[t];
    }
    sfree(sol);
    sfree(g);
    return firstdiff >= 0 ? firstdiff : first;
}

static char *hint_game(const game_state *orig, const game_state *curr,
                       const char **error)
{
    static char errbuf[160];
    const struct mosco_clues *cl = curr->cl;
    int w = cl->w, n = cl->n, k = cl->k;
    int t, d, j, res = -1;
    int *cnt = snewn(k, int);
    int used[NDIR];
    signed char *pref = snewn(n, signed char);
    signed char *sol = snewn(n, signed char);
    bool have_pref = parse_aux(midend_hint_aux, n, pref);
    struct msolver *sv = NULL;
    struct sstate *root = NULL, *ss = NULL, *full = NULL, *scratch = NULL;
    const char *msg = NULL;
    char *ret = NULL;
    bool contradiction;
    int logical;

    /*
     * What is wrong whatever the solutions are: too many arrows pointing
     * at a clue or too many of a kind.
     */
    count_arrows(cl, curr->grid, cnt, used);
    for (j = 0; j < k; j++)
        if (cnt[j] > cl->clue[j]) {
            sprintf(errbuf, _("There are too many arrows pointing at the clue %s"),
                    describe_clue(w, cl->h, j));
            msg = errbuf;
            goto out;
        }
    for (d = 0; d < NDIR; d++)
        if (used[d] > n / NDIR) {
            sprintf(errbuf, _("There are too many arrows pointing %s"),
                    dir_name[d]);
            msg = errbuf;
            goto out;
        }

    sv = msolver_new(cl);
    sv->work = LOGIC_WORK;
    root = ss_new(sv);
    if (!ss_propagate(sv, root)) {
        msg = _("This puzzle has no solution");
        goto out;
    }

    /* An arrow that no solution has, by what the clues alone tell */
    for (t = 0; t < n; t++)
        if (curr->grid[t] >= 0 && !(root->dom[t] & (1u << curr->grid[t]))) {
            msg = mistake_message(w, t);
            goto out;
        }

    /*
     * Do the clues lead to one arrangement by reasoning alone?  (Every
     * puzzle that is made here does.)  Then that is the solution, an arrow
     * that isn't like it is a mistake, and what is to be laid next is what
     * the same reasoning finds when it starts from the arrows that are laid:
     * a tile that has just one arrow left, or else one where a "what if"
     * leaves one.
     */
    full = ss_dup(sv, root);
    scratch = ss_dup(sv, root);
    logical = solve_level(sv, full, scratch, DIFF_HARD);
    if (logical < 0) {
        msg = _("This puzzle has no solution");
        goto out;
    }
    if (logical == 1) {
        int best = -1, bestn = NDIR + 1, wt, wd;

        for (t = 0; t < n; t++) sol[t] = lowbit(full->dom[t]);
        for (t = 0; t < n; t++)
            if (curr->grid[t] >= 0 && curr->grid[t] != sol[t]) {
                msg = mistake_message(w, t);
                goto out;
            }
        ss = ss_dup(sv, root);
        if (!ss_apply_grid(sv, ss, curr->grid, -1)) {
            msg = _("The arrows laid so far can't all be right");
            goto out;
        }
        for (t = 0; t < n; t++)
            if (curr->grid[t] < 0 && !(ss->dom[t] & (ss->dom[t] - 1))) {
                ret = hint_move(w, t, lowbit(ss->dom[t]));
                goto out;
            }
        if (ss_whatif(sv, ss, scratch, &wt, &wd) == 1 && curr->grid[wt] < 0) {
            ret = hint_move(w, wt, wd);
            goto out;
        }
        /* (the reasoning has used up its work: a tile of the solution) */
        for (t = 0; t < n; t++)
            if (curr->grid[t] < 0) {
                int pc = popcnt8(ss->dom[t]);
                if (pc < bestn) {
                    best = t;
                    bestn = pc;
                }
            }
        if (best >= 0)
            ret = hint_move(w, best, sol[best]);
        else
            msg = _("No logical next step found");
        goto out;
    }

    /*
     * The reasoning stops short of the end (a description that was typed in,
     * mostly): what is left is the search for a solution, and a hint that is
     * only as good as that.
     */
    ss = ss_dup(sv, root);
    contradiction = !ss_apply_grid(sv, ss, curr->grid, -1);
    if (!contradiction) {
        res = find_solution(cl, curr->grid, have_pref ? pref : NULL, sol, 1);
        if (res == 1) {
            int best = -1, bestn = NDIR + 1;

            /* A tile that has to have one arrow, given what is laid */
            for (t = 0; t < n; t++)
                if (curr->grid[t] < 0 && !(ss->dom[t] & (ss->dom[t] - 1))) {
                    ret = hint_move(w, t, lowbit(ss->dom[t]));
                    goto out;
                }
            /* Else the tile of a solution that has the fewest arrows to
             * choose from.  (The solution is a different one if what is
             * laid doesn't fit the one the puzzle was made from.) */
            for (t = 0; t < n; t++)
                if (curr->grid[t] < 0) {
                    int pc = popcnt8(ss->dom[t]);
                    if (pc < bestn) {
                        best = t;
                        bestn = pc;
                    }
                }
            if (best >= 0) {
                ret = hint_move(w, best, sol[best]);
                goto out;
            }
            msg = _("No logical next step found");
            goto out;
        }
    }

    if (contradiction || res == 0) {
        /* There is no solution: so what is wrong? */
        int culprit = find_culprit(cl, sv, root, curr->grid, pref, have_pref);
        if (culprit < 0 && contradiction) {
            /* Several arrows are wrong together.  Blame the first that
             * doesn't go with those before it. */
            struct sstate *s2 = ss_dup(sv, root);
            for (t = 0; t < n; t++) {
                if (curr->grid[t] < 0) continue;
                if (!ss_assign(sv, s2, t, curr->grid[t]) ||
                    !ss_propagate(sv, s2)) {
                    sv_drain(sv);
                    culprit = t;
                    break;
                }
            }
            ss_free(s2);
        }
        if (culprit >= 0)
            msg = mistake_message(w, culprit);
        else
            msg = _("The arrows laid so far can't all be right");
    } else {
        /* Neither a solution found nor a proof that there is none */
        msg = _("No logical next step found");
    }

out:
    if (sv) sv_drain(sv);
    if (ss) ss_free(ss);
    if (full) ss_free(full);
    if (scratch) ss_free(scratch);
    if (root) ss_free(root);
    if (sv) msolver_free(sv);
    sfree(cnt);
    sfree(pref);
    sfree(sol);
    if (!ret) *error = msg ? msg : _("No logical next step found");
    return ret;
}

/* ----------------------------------------------------------------------
 * The user interface and the moves
 *
 * Moves:
 *   Px,y,d    lay the arrow d on tile (x, y) (and wipe the marks of the tile)
 *   Ex,y      clear tile (x, y): the arrow and the marks
 *   Mx,y,d    switch the mark d of tile (x, y) (and take its arrow away)
 *   S<n digits>   what Solve lays: an arrow for every tile, row by row
 */

#define SWIPE_MIN 0.30F         /* of a tile: how far a swipe has to go */

struct game_ui {
    int cur_x, cur_y;           /* the cursor, on the board: 0 .. w + 1, 0 .. h + 1 */
    bool cur_visible;
    int hl;                     /* the clue whose tiles are shown, or -1 */
    int press;                  /* LEFT_BUTTON or RIGHT_BUTTON while pressed, else 0 */
    int press_fx, press_fy;     /* the square of the board that was pressed ... */
    int press_px, press_py;     /* ... and the point of it, in pixels */
    int dragdir;                /* the arrow that the swipe so far shows, or -1 */
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);

    ui->cur_x = ui->cur_y = 1;
    ui->cur_visible = false;
    ui->hl = -1;
    ui->press = 0;
    ui->press_fx = ui->press_fy = ui->press_px = ui->press_py = 0;
    ui->dragdir = -1;
    return ui;
}

static void free_ui(game_ui *ui)
{
    sfree(ui);
}

static char *encode_ui(const game_ui *ui)
{
    return NULL;
}

static void decode_ui(game_ui *ui, const char *encoding, const game_state *state)
{
}

static void android_cursor_visibility(game_ui *ui, int visible)
{
    ui->cur_visible = visible != 0;
}

static bool game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
    /* the swipe and the press that are going on are of no use any more */
    ui->press = 0;
    ui->dragdir = -1;
    return oldstate && !oldstate->completed &&
           newstate->completed && !newstate->used_solve;
}

static const char *game_current_key_label(const game_ui *ui,
                                          const game_state *state, int button)
{
    int w = state->cl->w, h = state->cl->h, t;

    if (!ui->cur_visible) return "";
    if (ui->cur_x < 1 || ui->cur_x > w || ui->cur_y < 1 || ui->cur_y > h)
        return button == CURSOR_SELECT ? "Show" : "";
    t = (ui->cur_y - 1) * w + (ui->cur_x - 1);
    if (button == CURSOR_SELECT) return "Turn";
    return (state->grid[t] >= 0 || state->marks[t]) ? "Clear" : "";
}

struct game_drawstate {
    int tilesize;
    int w, h;
    unsigned long long *cache;  /* (w + 2) * (h + 2): what is drawn */
    int *cnt;                   /* k: scratch */
    bool *aff;                  /* n: scratch */
};

/* What a swipe of (dx, dy) pixels shows: the arrow of the nearest direction,
 * or -1 if it hasn't gone far enough */
static int swipe_dir(int dx, int dy, int ts)
{
    float thr = SWIPE_MIN * ts;
    int ax = abs(dx), ay = abs(dy), sx, sy;

    if ((float)dx * dx + (float)dy * dy < thr * thr) return -1;
    /* the sectors are 45 degrees wide: tan 22.5 degrees is about 0.414 */
    sx = (ax * 1000 < ay * 414) ? 0 : (dx > 0 ? 1 : -1);
    sy = (ay * 1000 < ax * 414) ? 0 : (dy > 0 ? 1 : -1);
    return dir_of(sx, sy);
}

/* The arrow that the keys of a number pad stand for (as the arrow keys of
 * a roguelike): 7 8 9 / 4 _ 6 / 1 2 3 */
static int key_dir(int c)
{
    switch (c) {
      case '8': return UP;
      case '7': return UP_LEFT;
      case '4': return LEFT;
      case '1': return DOWN_LEFT;
      case '2': return DOWN;
      case '3': return DOWN_RIGHT;
      case '6': return RIGHT;
      case '9': return UP_RIGHT;
    }
    return -1;
}

enum { ACT_TURN, ACT_CLEAR, ACT_SET, ACT_MARK };

/* What it takes to do something to a square of the board */
static char *square_action(const game_state *state, game_ui *ui,
                           int fx, int fy, int act, int d)
{
    const struct mosco_clues *cl = state->cl;
    int w = cl->w, h = cl->h;
    char buf[48];
    int t;

    if (fx < 1 || fx > w || fy < 1 || fy > h) {
        /* a clue: show the tiles that can point at it (or don't) */
        if (act == ACT_TURN) {
            int c = ring_index(w, h, fx, fy);
            ui->hl = ui->hl == c ? -1 : c;
            return MOVE_UI_UPDATE;
        }
        return MOVE_NO_EFFECT;
    }

    t = (fy - 1) * w + (fx - 1);
    switch (act) {
      case ACT_TURN:
        d = state->grid[t] >= NDIR - 1 ? -1 : state->grid[t] + 1;
        if (d < 0) goto clear;
        sprintf(buf, "P%d,%d,%d", fx - 1, fy - 1, d);
        return dupstr(buf);
      case ACT_SET:
        if (state->grid[t] == d) return MOVE_NO_EFFECT;
        sprintf(buf, "P%d,%d,%d", fx - 1, fy - 1, d);
        return dupstr(buf);
      case ACT_MARK:
        sprintf(buf, "M%d,%d,%d", fx - 1, fy - 1, d);
        return dupstr(buf);
      case ACT_CLEAR:
      clear:
        if (state->grid[t] < 0 && !state->marks[t]) return MOVE_NO_EFFECT;
        sprintf(buf, "E%d,%d", fx - 1, fy - 1);
        return dupstr(buf);
    }
    return MOVE_NO_EFFECT;
}

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds, int x, int y, int button)
{
    const struct mosco_clues *cl = state->cl;
    int w = cl->w, h = cl->h, ts = ds->tilesize;
    int btn = button & ~MOD_MASK;

    if (IS_CURSOR_MOVE(btn)) {
        if (btn == CURSOR_LEFT) ui->cur_x = max(ui->cur_x - 1, 0);
        if (btn == CURSOR_RIGHT) ui->cur_x = min(ui->cur_x + 1, w + 1);
        if (btn == CURSOR_UP) ui->cur_y = max(ui->cur_y - 1, 0);
        if (btn == CURSOR_DOWN) ui->cur_y = min(ui->cur_y + 1, h + 1);
        ui->cur_visible = true;
        return MOVE_UI_UPDATE;
    }

    if (IS_CURSOR_SELECT(btn)) {
        if (!ui->cur_visible) {
            ui->cur_visible = true;
            return MOVE_UI_UPDATE;
        }
        return square_action(state, ui, ui->cur_x, ui->cur_y,
                             btn == CURSOR_SELECT2 ? ACT_CLEAR : ACT_TURN, 0);
    }

    if (key_dir(btn) >= 0 || btn == '5' || btn == '0' || btn == '\b') {
        if (!ui->cur_visible ||
            ui->cur_x < 1 || ui->cur_x > w || ui->cur_y < 1 || ui->cur_y > h)
            return MOVE_UNUSED;
        if (key_dir(btn) >= 0)
            return square_action(state, ui, ui->cur_x, ui->cur_y,
                                 ACT_SET, key_dir(btn));
        return square_action(state, ui, ui->cur_x, ui->cur_y, ACT_CLEAR, 0);
    }

    if (btn == LEFT_BUTTON || btn == RIGHT_BUTTON) {
        int fx = x < 0 ? -1 : x / ts, fy = y < 0 ? -1 : y / ts;
        bool was_visible = ui->cur_visible;

        ui->press = 0;
        ui->dragdir = -1;
        if (fx < 0 || fx > w + 1 || fy < 0 || fy > h + 1)
            return MOVE_UNUSED;
        ui->press = btn;
        ui->press_fx = fx;
        ui->press_fy = fy;
        ui->press_px = x;
        ui->press_py = y;
        ui->cur_visible = false;
        return was_visible ? MOVE_UI_UPDATE : MOVE_NO_EFFECT;
    }

    if (btn == LEFT_DRAG || btn == RIGHT_DRAG) {
        int nd;

        if (!ui->press) return MOVE_UNUSED;
        nd = swipe_dir(x - ui->press_px, y - ui->press_py, ts);
        if (nd == ui->dragdir) return MOVE_NO_EFFECT;
        ui->dragdir = nd;
        return MOVE_UI_UPDATE;
    }

    if (btn == LEFT_RELEASE || btn == RIGHT_RELEASE) {
        int press = ui->press, fx = ui->press_fx, fy = ui->press_fy;
        bool had_preview = ui->dragdir >= 0;
        int nd = swipe_dir(x - ui->press_px, y - ui->press_py, ts);
        char *ret;

        if (!press) return MOVE_UNUSED;
        ui->press = 0;
        ui->dragdir = -1;
        if (press != (btn == LEFT_RELEASE ? LEFT_BUTTON : RIGHT_BUTTON))
            return had_preview ? MOVE_UI_UPDATE : MOVE_NO_EFFECT;

        if (fx < 1 || fx > w || fy < 1 || fy > h) {
            /* a clue: a tap (not a swipe) shows the tiles that point at it */
            if (nd < 0) return square_action(state, ui, fx, fy, ACT_TURN, 0);
            return had_preview ? MOVE_UI_UPDATE : MOVE_NO_EFFECT;
        }

        if (press == LEFT_BUTTON) {
            ret = nd >= 0 ? square_action(state, ui, fx, fy, ACT_SET, nd)
                          : square_action(state, ui, fx, fy, ACT_TURN, 0);
        } else if (nd >= 0) {
            ret = square_action(state, ui, fx, fy, ACT_MARK, nd);
        } else {
            /* no swipe: the part of the tile that was pressed says which
             * mark, and the middle of the tile clears it */
            int ox = ui->press_px - fx * ts, oy = ui->press_py - fy * ts;
            int sx = ox < ts / 3 ? -1 : ox >= ts * 2 / 3 ? 1 : 0;
            int sy = oy < ts / 3 ? -1 : oy >= ts * 2 / 3 ? 1 : 0;
            if (sx == 0 && sy == 0)
                ret = square_action(state, ui, fx, fy, ACT_CLEAR, 0);
            else
                ret = square_action(state, ui, fx, fy, ACT_MARK,
                                    dir_of(sx, sy));
        }
        if (ret == MOVE_NO_EFFECT && had_preview) return MOVE_UI_UPDATE;
        return ret;
    }

    return MOVE_UNUSED;
}

static game_state *execute_move(const game_state *state, const char *move)
{
    const struct mosco_clues *cl = state->cl;
    int w = cl->w, h = cl->h, n = cl->n;
    int x, y, d, len, t, i;
    game_state *ret;

    if (move[0] == 'S') {
        if ((int)strlen(move + 1) != n) return NULL;
        for (i = 0; i < n; i++)
            if (move[1 + i] < '0' || move[1 + i] > '7') return NULL;
        ret = dup_game(state);
        for (i = 0; i < n; i++) {
            ret->grid[i] = move[1 + i] - '0';
            ret->marks[i] = 0;
        }
        ret->used_solve = true;
        ret->completed = is_complete(ret);
        return ret;
    }

    if (move[0] == 'P' || move[0] == 'M') {
        if (sscanf(move + 1, "%d,%d,%d%n", &x, &y, &d, &len) != 3 ||
            move[1 + len] != '\0' ||
            x < 0 || x >= w || y < 0 || y >= h || d < 0 || d >= NDIR)
            return NULL;
        t = y * w + x;
        ret = dup_game(state);
        if (move[0] == 'P') {
            ret->grid[t] = d;
            ret->marks[t] = 0;
        } else {
            ret->grid[t] = -1;
            ret->marks[t] ^= 1 << d;
        }
        ret->completed = is_complete(ret);
        return ret;
    }

    if (move[0] == 'E') {
        if (sscanf(move + 1, "%d,%d%n", &x, &y, &len) != 2 ||
            move[1 + len] != '\0' ||
            x < 0 || x >= w || y < 0 || y >= h)
            return NULL;
        t = y * w + x;
        ret = dup_game(state);
        ret->grid[t] = -1;
        ret->marks[t] = 0;
        ret->completed = is_complete(ret);
        return ret;
    }

    return NULL;
}

static int game_status(const game_state *state)
{
    return state->completed ? +1 : 0;
}

static bool game_can_format_as_text_now(const game_params *params)
{
    return false;
}

static char *game_text_format(const game_state *state)
{
    return NULL;
}

/* ----------------------------------------------------------------------
 * Drawing
 */

static void game_compute_size(const game_params *params, int tilesize,
                              const game_ui *ui, int *x, int *y)
{
    *x = (params->w + 2) * tilesize + 1;
    *y = (params->h + 2) * tilesize + 1;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    int i;

    ds->tilesize = tilesize;
    for (i = 0; i < (ds->w + 2) * (ds->h + 2); i++) ds->cache[i] = ~0ULL;
}

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);
    int i;

#define SETC(col, r, g, b) do { \
        ret[(col) * 3] = (r); ret[(col) * 3 + 1] = (g); ret[(col) * 3 + 2] = (b); \
    } while (0)

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);
    for (i = 0; i < 3; i++) {
        float bg = ret[COL_BACKGROUND * 3 + i];
        ret[COL_ARENA * 3 + i] = bg + (1.0F - bg) * 0.6F;
        ret[COL_GRID * 3 + i] = bg * 0.55F;
        ret[COL_FLASH * 3 + i] = 1.0F;
        ret[COL_TEXT * 3 + i] = 0.0F;
        ret[COL_INK * 3 + i] = 0.0F;
        ret[COL_MARK * 3 + i] = 0.35F;
    }
    SETC(COL_AFFECT, 1.0F, 0.93F, 0.5F);
    SETC(COL_CURSOR, 0.65F, 0.8F, 1.0F);
    SETC(COL_SATISFIED, 0.0F, 0.55F, 0.0F);
    SETC(COL_ERROR, 0.9F, 0.0F, 0.0F);
    SETC(COL_BLUE, 0.6F, 0.75F, 1.0F);
    SETC(COL_GREEN, 0.55F, 0.9F, 0.55F);
    SETC(COL_RED, 1.0F, 0.6F, 0.6F);
    SETC(COL_YELLOW, 1.0F, 0.92F, 0.45F);
#undef SETC

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);
    int i;

    ds->tilesize = PREFERRED_TILE_SIZE;
    ds->w = state->cl->w;
    ds->h = state->cl->h;
    ds->cache = snewn((ds->w + 2) * (ds->h + 2), unsigned long long);
    for (i = 0; i < (ds->w + 2) * (ds->h + 2); i++) ds->cache[i] = ~0ULL;
    ds->cnt = snewn(state->cl->k, int);
    ds->aff = snewn(state->cl->n, bool);
    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->cache);
    sfree(ds->cnt);
    sfree(ds->aff);
    sfree(ds);
}

static int roundf2i(float v)
{
    return (int)(v + 0.5F);
}

/*
 * An arrow of direction d with the middle at (cx, cy): the point is len from
 * the middle, the head is headlen long and 2 * headwid wide.
 */
static void draw_arrow_glyph(drawing *dr, float cx, float cy, float len,
                             float headlen, float headwid, float shaft,
                             int d, int colour)
{
    float dx = (float)dxs[d], dy = (float)dys[d];
    float norm = (dx != 0.0F && dy != 0.0F) ? 0.70710678F : 1.0F;
    float ux = dx * norm, uy = dy * norm;       /* along the arrow */
    float px = -uy, py = ux;                    /* across it */
    float tipx = cx + ux * len, tipy = cy + uy * len;
    float tailx = cx - ux * len, taily = cy - uy * len;
    float bx = tipx - ux * headlen, by = tipy - uy * headlen;
    int coords[6];

    draw_thick_line(dr, shaft, tailx, taily,
                    bx + ux * headlen * 0.3F, by + uy * headlen * 0.3F, colour);
    coords[0] = roundf2i(tipx);
    coords[1] = roundf2i(tipy);
    coords[2] = roundf2i(bx + px * headwid);
    coords[3] = roundf2i(by + py * headwid);
    coords[4] = roundf2i(bx - px * headwid);
    coords[5] = roundf2i(by - py * headwid);
    draw_polygon(dr, coords, 3, colour, colour);
}

/* A small arrow in the part of the tile that is on the side of it that the
 * arrow d points to (the middle for none) */
static void draw_mark(drawing *dr, int x0, int y0, int ts, int d)
{
    float cx = x0 + (2 * (1 + dxs[d]) + 1) * ts / 6.0F;
    float cy = y0 + (2 * (1 + dys[d]) + 1) * ts / 6.0F;

    draw_arrow_glyph(dr, cx, cy, 0.11F * ts, 0.10F * ts, 0.07F * ts,
                     ts >= 30 ? 1.5F : 1.0F, d, COL_MARK);
}

/*
 * What is drawn in each square of the board is a function of a number:
 *
 *   bit 20        a clue (else a tile)
 *   bit 19        the flash of a solved game
 *   bit 13        the cursor
 *   bit 12        shown because of the clue that is picked (or is it)
 *  for a clue:
 *   bits 10, 11   less than / as many as / more than the number is pointing at it
 *   bits 25 - 56  the number
 *  for a tile:
 *   bits 21 - 24  the mark that a long press shows (the direction + 1)
 *   bits 15 - 18  the arrow that a swipe shows (the direction + 1)
 *   bit 14        there are too many of the tiles of this kind
 *   bits 4 - 11   the marks
 *   bits 0 - 3    the arrow (NDIR for none)
 */
static void draw_square(drawing *dr, const game_drawstate *ds, int fx, int fy,
                        unsigned long long code)
{
    int ts = ds->tilesize, x0 = fx * ts, y0 = fy * ts;
    bool ring = (code >> 20) & 1, aff = (code >> 12) & 1;
    bool cursor = (code >> 13) & 1, flash = (code >> 19) & 1;
    int bg;

    if (flash) bg = COL_FLASH;
    else if (cursor) bg = COL_CURSOR;
    else if (aff) bg = COL_AFFECT;
    else bg = ring ? COL_BACKGROUND : COL_ARENA;

    clip(dr, x0, y0, ts + 1, ts + 1);
    draw_rect(dr, x0, y0, ts, ts, bg);
    draw_rect_outline(dr, x0, y0, ts + 1, ts + 1, COL_GRID);

    if (ring) {
        int val = (int)((code >> 25) & 0xffffffffULL), status = (int)((code >> 10) & 3);
        char buf[16];
        int digits, size;

        sprintf(buf, "%d", val);
        digits = (int)strlen(buf);
        /* the more digits, the smaller (up to seven, as a board may have
         * about a million tiles) */
        size = digits <= 1 ? ts / 2 :
               digits == 2 ? roundf2i(ts * 0.42F) :
               roundf2i(ts * (1.5F / digits < 0.34F ? 1.5F / digits : 0.34F));
        draw_text(dr, x0 + ts / 2, y0 + ts / 2, FONT_VARIABLE, size,
                  ALIGN_HCENTRE | ALIGN_VCENTRE,
                  status == 1 ? COL_SATISFIED : status == 2 ? COL_ERROR : COL_TEXT,
                  buf);
    } else {
        int arrow = code & 15, marks = (code >> 4) & 255, d;
        int lp = (code >> 15) & 15, rp = (code >> 21) & 15;

        if (lp) arrow = lp - 1;
        if (arrow < NDIR) {
            int in = max(1, ts / 10);
            draw_rect(dr, x0 + in, y0 + in, ts - 2 * in, ts - 2 * in,
                      arrow_colour[arrow]);
            draw_arrow_glyph(dr, x0 + ts / 2.0F, y0 + ts / 2.0F, 0.34F * ts,
                             0.30F * ts, 0.20F * ts,
                             max(2.0F, 0.12F * ts), arrow, COL_INK);
        } else {
            for (d = 0; d < NDIR; d++)
                if (marks & (1 << d)) draw_mark(dr, x0, y0, ts, d);
        }
        if (rp) draw_mark(dr, x0, y0, ts, rp - 1);
        if ((code >> 14) & 1) {
            draw_rect_outline(dr, x0 + 1, y0 + 1, ts - 1, ts - 1, COL_ERROR);
            if (ts >= 24)
                draw_rect_outline(dr, x0 + 2, y0 + 2, ts - 3, ts - 3, COL_ERROR);
        }
    }

    unclip(dr);
    draw_update(dr, x0, y0, ts + 1, ts + 1);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    const struct mosco_clues *cl = state->cl;
    int w = cl->w, h = cl->h, n = cl->n;
    int used[NDIR], fx, fy, t, d;
    bool flash = flashtime > 0 && ((int)(flashtime / FLASH_FRAME) % 2) == 0;
    char text[256];

    count_arrows(cl, state->grid, ds->cnt, used);

    for (t = 0; t < n; t++) ds->aff[t] = false;
    if (ui->hl >= 0)
        for (t = 0; t < n; t++)
            for (d = 0; d < NDIR; d++)
                if (cl->target[t * NDIR + d] == ui->hl) ds->aff[t] = true;

    for (fy = 0; fy < h + 2; fy++) {
        for (fx = 0; fx < w + 2; fx++) {
            unsigned long long code;
            bool cur = ui->cur_visible && ui->cur_x == fx && ui->cur_y == fy;

            if (fx == 0 || fy == 0 || fx == w + 1 || fy == h + 1) {
                int c = ring_index(w, h, fx, fy);
                int v = cl->clue[c];
                int status = ds->cnt[c] < v ? 0 : ds->cnt[c] == v ? 1 : 2;

                code = (1ULL << 20) | ((unsigned long long)v << 25) |
                       ((unsigned long long)status << 10);
                if (ui->hl == c) code |= 1ULL << 12;
            } else {
                int g;

                t = (fy - 1) * w + (fx - 1);
                g = state->grid[t];
                code = (unsigned long long)(g < 0 ? NDIR : g) |
                       ((unsigned long long)state->marks[t] << 4);
                if (ds->aff[t]) code |= 1ULL << 12;
                if (g >= 0 && used[g] > n / NDIR) code |= 1ULL << 14;
                if (ui->press && ui->dragdir >= 0 &&
                    ui->press_fx == fx && ui->press_fy == fy) {
                    if (ui->press == LEFT_BUTTON)
                        code |= (unsigned long long)(ui->dragdir + 1) << 15;
                    else
                        code |= (unsigned long long)(ui->dragdir + 1) << 21;
                }
            }
            if (cur) code |= 1ULL << 13;
            if (flash) code |= 1ULL << 19;

            if (ds->cache[fy * (w + 2) + fx] != code) {
                draw_square(dr, ds, fx, fy, code);
                ds->cache[fy * (w + 2) + fx] = code;
            }
        }
    }

    /* The status bar says what is left to lay of each kind, or how it ended */
    if (state->completed) {
        strcpy(text, state->used_solve ? "Auto-solved" : "COMPLETED!");
    } else {
        char *p = text;
        for (d = 0; d < NDIR; d++)
            p += sprintf(p, "%s%s%d", d ? " " : "", dir_glyph[d],
                         n / NDIR - used[d]);
    }
    status_bar(dr, text);
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    return 0.0F;
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate, int dir, game_ui *ui)
{
    if (!oldstate->completed && newstate->completed &&
        !oldstate->used_solve && !newstate->used_solve)
        return FLASH_TIME;
    return 0.0F;
}

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
    if (ui->cur_visible) {
        *x = ui->cur_x * ds->tilesize;
        *y = ui->cur_y * ds->tilesize;
        *w = *h = ds->tilesize;
    }
}

static bool game_timing_state(const game_state *state, game_ui *ui)
{
    return false;
}

#ifdef COMBINED
#define thegame mosco
#endif

const struct game thegame = {
    "Mosco", "games.mosco", "mosco",
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
    false, game_can_format_as_text_now, game_text_format,
    NULL, NULL, /* get_prefs, set_prefs */
    new_ui,
    free_ui,
    encode_ui,
    decode_ui,
    NULL, /* request_keys */
    android_cursor_visibility,
    game_changed_state,
    game_current_key_label,
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
    false, false, NULL, NULL,
#endif
    true,			       /* wants_statusbar */
    false, game_timing_state,
    REQUIRE_RBUTTON,	       /* flags */
    hint_game,
};
