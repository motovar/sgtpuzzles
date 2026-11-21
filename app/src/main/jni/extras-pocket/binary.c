/*
 * binary.c: "Binary", an implementation of the sliding tile game '2048' and
 * of its relative "Threes".
 *
 * Written by Steffen Bauer for his PocketPuzzles collection (MIT licence)
 * and brought over to this app.
 *
 * 2048.  Tiles carry powers of two.  A move slides all the tiles as far as
 * they go in one of the four directions; two tiles with the same number that
 * run into each other merge into one with the next power of two, and a new
 * tile (a 2, now and then a 4) turns up at a random empty square.  The game
 * is won when a tile with the goal number appears (the default goal is
 * 2048; the game can be played on after that) and over when no move is left.
 * The numbers are generated as in the original game of Gabriele Cirulli: two
 * tiles at the start, then after each move that changed the board one tile
 * that is a 2 nine times in ten and a 4 otherwise, on an empty square that
 * is picked at random (all of them are as likely).
 * With the goal "Endless" there is nothing to win: the game is played until
 * no move is left.  The random numbers come from a random state that is part
 * of every game state, so a game (and every undo and redo) is the same every
 * time.
 *
 * Threes.  The tiles are 1, 2, 3, 6, 12, 24 and so on.  A move pushes every
 * tile one square (not as far as it goes) in the direction of the move: a
 * tile moves into an empty square or on to a tile that it can merge with, a 1
 * and a 2 into a 3, two equal tiles of 3 or more into one with the double
 * number.  Of the tiles of a row (or column) that moves, the ones behind the
 * first tile that can move or merge move together with it; the row does not
 * change if no tile in it can.  The new tile comes in at the far end of one
 * of the rows that have moved, and the game shows which tile will come next
 * (a 1, a 2, a 3, or a "+" for a bonus tile: a number from 6 up to an
 * eighth of the biggest tile, which comes now and then once there is a tile
 * of 48).  The new tiles are drawn from a bag with four 1s, four 2s and
 * four 3s, which is filled again when it is empty.  The score is that of
 * the real game: a tile of 3 or more is worth 3 to the power of the number
 * of times that it has been doubled, plus one (3: 3, 6: 9, 12: 27, ...).  The
 * game is over when no move is left.  The numbers are generated the way that
 * the real game does it, as far as it is known (it has been worked out by
 * players): nine tiles at the start, a bonus tile instead of the next tile of
 * the bag once in 21 times (a bonus tile does not use up a tile of the bag),
 * and the new tile goes to a row (or column) that moved, picked at random.
 *
 * What is new compared to the original:
 *
 *  - Tiles merge once per move, as in 2048: the tile that is made by a merge
 *    does not merge again in the same move.  The original merged on in a
 *    chain, so that a row of 2 2 4 8 became a 16 and not 4 4 8.
 *  - The tiles slide, the merged tiles pop up and the new tile grows when a
 *    move is made (and the other way round when it is undone), with the
 *    timing of the original page of 2048 (100 ms to slide, 200 ms to pop up
 *    and to grow, the curves of CSS); in Threes the tiles slide a square and
 *    the new card comes in from beyond the edge, in 200 ms.  The speed can be
 *    chosen, or the animation can be turned off.
 *  - The goal "Endless", and the rules of Threes.
 *  - "Show next step" plays the move that a look-ahead (expectimax) search
 *    finds best: it tries the four moves, the new tiles that may turn up
 *    after each of them and the moves after that, and rates the boards by
 *    the number of empty squares, the possible merges and how orderly the
 *    rows and columns are.  The move is an ordinary one, so it can be undone.
 *  - The cursor keys and the arrow buttons move the tiles, too.  The swipe
 *    has to be as long as a part of a tile, not a fixed number of pixels, so
 *    it is the same on every screen, and the "tap" method works on the
 *    triangles of the board that the pressed point is in.
 *  - The looks are those of the originals: for 2048 the cream page, the brown
 *    board with its rounded corners and squares, the colours of the tiles
 *    from 2 to 2048 and the dark ones above that, the blue haze round the
 *    tiles from 128 on and the veil over a game that is over; for Threes the
 *    upright cards (blue 1, red 2, white for the rest) with a darker edge
 *    below, on the light blue screen with hollows for the empty places, and
 *    the next card above the board.  (The faces on the cards of Threes are
 *    the artwork of the game and are not copied.)
 *  - A move that does nothing is refused (the original did not check that
 *    when a saved game was loaded), as is a move in a game that is over; the
 *    game description is checked down to the random state; the default
 *    goal is 2048 for a game ID without a goal, as the presets have it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"

#define PREFERRED_TILE_SIZE 48

#define MAX_SIZE 9
#define MAX_CELLS (MAX_SIZE * MAX_SIZE)
#define MAXPOWER 17                    /* the biggest tile is 2^17 = 131072 */
#define RS_DIGITS 122                  /* the length of an encoded random state */

/*
 * The colours.  The ones from COL_NUM_DARK to COL_NUM_WHITE are those of the
 * tiles and their numbers.  When the game is over the board is veiled, as
 * the original 2048 does it with a milky layer: there is a veiled copy of
 * each of those colours (COL_DEAD + the distance from COL_NUM_DARK), and
 * COL_GLOW has two rings of the glow of the big tiles for each of five
 * strengths.  (The Android build reads the names of this first list for the
 * night colours: no braces or comments with commas in here.)
 */
enum {
    COL_BACKGROUND,
    COL_BOARD,
    COL_SLOT,
    COL_TEXT,
    COL_NUM_DARK,
    COL_NUM_LIGHT,
    COL_TILE1,
    COL_TILE2,
    COL_TILE3,
    COL_TILE4,
    COL_TILE5,
    COL_TILE6,
    COL_TILE7,
    COL_TILE8,
    COL_TILE9,
    COL_TILE10,
    COL_TILE11,
    COL_TILE12,
    COL_TILE13,
    COL_TILE14,
    COL_TILE15,
    COL_TILE16,
    COL_TILE17,
    COL_BLUE,
    COL_BLUE_EDGE,
    COL_RED,
    COL_RED_EDGE,
    COL_WHITE,
    COL_WHITE_EDGE,
    COL_NUM_BLACK,
    COL_NUM_WHITE,
    COL_DEAD,
    COL_GLOW = COL_DEAD + (COL_NUM_WHITE - COL_NUM_DARK + 1),
    NCOLOURS = COL_GLOW + 10
};

/* the colour of a tile that carries 2^e (e = 1 ... MAXPOWER) */
#define COL_TILE(e) (COL_TILE1 + (e) - 1)
/* the veiled copy of a colour of a tile or of a number */
#define COL_VEILED(c) ((c) - COL_NUM_DARK + COL_DEAD)
/* the glow of the big tiles: strength 0 ... 4, ring 0 (inner) or 1 (outer) */
#define COL_GLOW_RING(strength, ring) (COL_GLOW + 2 * (strength) + (ring))

enum {
    PREF_GAME_BACKGROUND,
    PREF_INPUT_METHOD,
    PREF_ANIMATION,
    N_PREF_ITEMS
};

enum {
    BACKGROUND_SLOTS,
    BACKGROUND_EMPTY,
    BACKGROUND_CHESS,
    BACKGROUND_LINES
};

enum {
    INPUT_SWIPE,
    INPUT_TAP
};

enum {
    ANIM_NORMAL,
    ANIM_FAST,
    ANIM_OFF
};

/* the rules: 2048 (merge equal powers of two) or Threes */
enum { RULES_2048, RULES_THREES, RULESCOUNT };
static char const binary_rulechars[] = "2t";

/*
 * The screen of 2048 is cream with a brown board, that of Threes is light
 * blue, so the colours depend on the rules.  game_colours() is not told which
 * game is going on, so it takes the rules of the game that was made last
 * (the app asks for the colours again each time it has started a game).
 */
static int binary_last_rules = RULES_2048;

struct game_params {
    int w,h;
    int goal;
    int mode;
    int rules;
};

#define GOALLIST(A)                             \
    A(32,32,1)                                  \
    A(64,64,2)                                  \
    A(128,128,3)                                \
    A(256,256,4)                                \
    A(512,512,5)                                \
    A(1024,1024,6)                              \
    A(2048,2048,7)                              \
    A(4096,4096,8)                              \
    A(8192,8192,9)                              \
    A(ENDLESS,Endless,e)                        \

#define MODELIST(A)                             \
    A(RECT,Rectangular, R)                      \
    A(HEX,Hexagonal, H)                         \

#define TITLE(upper,title,lower) #title,
#define ENCODE(upper,title,lower) #lower
#define CONFIG(upper,title,lower) ":" #title
#define GOALENUM(upper,title,lower) GOAL_ ## upper,
#define MODEENUM(upper,title,lower) MODE_ ## upper,

enum { GOALLIST(GOALENUM) GOALCOUNT };
static char const *const binary_goalnames[] = { GOALLIST(TITLE) };
static char const binary_goalchars[] = GOALLIST(ENCODE);
#define GOALCONFIG GOALLIST(CONFIG)

enum { MODELIST(MODEENUM) MODECOUNT };
static char const binary_modechars[] = MODELIST(ENCODE);

static const int POWER[MAXPOWER + 1] = {
    1, 2, 4, 8,
    16, 32, 64, 128,
    256, 512, 1024, 2048,
    4096, 8192, 16384, 32768,
    65536, 131072
};

/* the exponent that a tile has to reach for a goal (never for "Endless") */
#define GOAL_EXP(goal) ((goal) == GOAL_ENDLESS ? MAXPOWER + 1 : (goal) + 5)

struct game_state {
    int w, h;
    int goal;
    int mode;
    int rules;
    int score;
    bool won;
    bool finished;
    /*
     * The tiles.  2048: exponents, 0 = empty, 1 = 2, 2 = 4 ...  Threes: 0 =
     * empty, 1 = 1, 2 = 2, 3 = 3, 4 = 6, 5 = 12 ... (3 * 2^(n-3)).
     */
    unsigned char *tiles;
    random_state *rs;
    /* Threes: the tile that comes next (4 or more: a bonus tile) and what is left in the bag */
    int next;
    int bag[3];
    /* the move that led to this state (or -1), for the animation, and the tile that turned up */
    int lastdir;
    int spawn;
};

static game_params *default_params(void) {
    game_params *ret = snew(game_params);

    ret->w = 4;
    ret->h = 4;
    ret->goal = GOAL_2048;
    ret->mode = MODE_RECT;
    ret->rules = RULES_2048;

    return ret;
}

static const struct game_params binary_presets[] = {
    { 4,  4, GOAL_512, MODE_RECT, RULES_2048},
    { 4,  4, GOAL_2048, MODE_RECT, RULES_2048},
    { 5,  5, GOAL_2048, MODE_RECT, RULES_2048},
    { 6,  6, GOAL_2048, MODE_RECT, RULES_2048},
    { 4,  4, GOAL_ENDLESS, MODE_RECT, RULES_2048},
    { 5,  5, GOAL_ENDLESS, MODE_RECT, RULES_2048},
    { 4,  4, GOAL_ENDLESS, MODE_RECT, RULES_THREES},
    { 5,  5, GOAL_ENDLESS, MODE_RECT, RULES_THREES},
};

static bool game_fetch_preset(int i, char **name, game_params **params) {
    game_params *ret;
    char str[80];

    if (i < 0 || i >= (int)lenof(binary_presets))
        return false;

    ret = snew(game_params);
    *ret = binary_presets[i];

    if (ret->rules == RULES_THREES)
        sprintf(str, "%dx%d, Threes", ret->w, ret->h);
    else if (ret->goal == GOAL_ENDLESS)
        sprintf(str, "%dx%d, Endless", ret->w, ret->h);
    else
        sprintf(str, "%dx%d, Goal %s", ret->w, ret->h, binary_goalnames[ret->goal]);

    *name = dupstr(str);
    *params = ret;
    return true;
}

static void free_params(game_params *params) {
    sfree(params);
}

static game_params *dup_params(const game_params *params) {
    game_params *ret = snew(game_params);
    *ret = *params;            /* structure copy */
    return ret;
}

static void decode_params(game_params *params, char const *string) {
    int i;
    params->w = params->h = atoi(string);
    params->goal = GOAL_2048;
    params->mode = MODE_RECT;
    params->rules = RULES_2048;

    while (*string && isdigit((unsigned char)*string)) string++;
    if (*string == 'x') {
        string++;
        params->h = atoi(string);
        while (*string && isdigit((unsigned char)*string)) string++;
    }
    if (*string && *string == 'g') {
        string++;
        if (*string) {
            for (i = 0; i < GOALCOUNT; i++) {
                if (*string == binary_goalchars[i])
                    params->goal = i;
            }
            string++;
        }
    }
    if (*string == 'm') {
        string++;
        if (*string) {
            for (i = 0; i < MODECOUNT; i++) {
                if (*string == binary_modechars[i])
                    params->mode = i;
            }
            string++;
        }
    }
    if (*string == 'v') {
        string++;
        if (*string) {
            for (i = 0; i < RULESCOUNT; i++) {
                if (*string == binary_rulechars[i])
                    params->rules = i;
            }
            string++;
        }
    }
    /* Threes has no goal */
    if (params->rules == RULES_THREES)
        params->goal = GOAL_ENDLESS;
}

static char *encode_params(const game_params *params, bool full) {
    char data[256];
    sprintf(data, "%dx%dg%cm%c", params->w, params->h,
            binary_goalchars[params->goal], binary_modechars[params->mode]);
    /* the rules only when they are not those of 2048, so that the IDs of the old games stay the same */
    if (params->rules != RULES_2048)
        sprintf(data + strlen(data), "v%c", binary_rulechars[params->rules]);
    return dupstr(data);
}

static config_item *game_configure(const game_params *params) {
    config_item *ret;
    char buf[80];

    ret = snewn(5, config_item);

    ret[0].name = "Width";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[0].u.string.sval = dupstr(buf);

    ret[1].name = "Height";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->h);
    ret[1].u.string.sval = dupstr(buf);

    ret[2].name = "Rules";
    ret[2].type = C_CHOICES;
    ret[2].u.choices.choicenames = ":2048:Threes";
    ret[2].u.choices.selected = params->rules;

    ret[3].name = "Goal (2048 only)";
    ret[3].type = C_CHOICES;
    ret[3].u.choices.choicenames = GOALCONFIG;
    ret[3].u.choices.selected = params->goal;

    ret[4].name = NULL;
    ret[4].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg) {
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].u.string.sval);
    ret->h = atoi(cfg[1].u.string.sval);
    ret->rules = cfg[2].u.choices.selected;
    ret->goal = cfg[3].u.choices.selected;
    ret->mode = MODE_RECT;
    if (ret->rules == RULES_THREES)
        ret->goal = GOAL_ENDLESS;

    return ret;
}

static const char *validate_params(const game_params *params, bool full) {
    if (params->w < 2 || params->h < 2)
        return "Width and height must both be at least two";
    if (params->w > MAX_SIZE || params->h > MAX_SIZE)
        return "Width and height must both be at most 9";
    if (params->goal < 0 || params->goal >= GOALCOUNT)
        return "Invalid goal number";
    if (params->rules < 0 || params->rules >= RULESCOUNT)
        return "Invalid rules";
    if (params->rules == RULES_THREES && params->goal != GOAL_ENDLESS)
        return "Threes has no goal number";
    if (params->mode == MODE_HEX)
        return "Hexagonal grid not implemented yet";
    if (params->mode != MODE_RECT)
        return "Invalid grid type";
    return NULL;
}

/* ----------------------------------------------------------------------
 * The rules of the game, on bare boards of tiles.
 */

enum { DIR_L, DIR_R, DIR_U, DIR_D, NDIRS };

static const char dir_chars[NDIRS] = { 'L', 'R', 'U', 'D' };

static int dir_from_char(char c) {
    int d;
    for (d = 0; d < NDIRS; d++)
        if (dir_chars[d] == c) return d;
    return -1;
}

/*
 * What a move did to every tile, for the animation: where the tile that was
 * on a square is when the move is over (-1 for a square without a tile), and
 * whether it merged there with another tile.
 */
struct trace {
    signed char dst[MAX_CELLS];
    unsigned char join[MAX_CELLS];
};

/* the i'th square of line k, counted from the side that the tiles move to */
static int line_pos(int w, int h, int d, int k, int i) {
    switch (d) {
      case DIR_L: return k * w + i;
      case DIR_R: return k * w + (w - 1 - i);
      case DIR_U: return i * w + k;
      default:    return (h - 1 - i) * w + k;
    }
}

static void trace_init(struct trace *tr, int n) {
    int i;
    for (i = 0; i < n; i++) {
        tr->dst[i] = -1;
        tr->join[i] = 0;
    }
}

/*
 * 2048: slide the tiles of a board as far as they go in direction d.  Two
 * tiles with the same number that meet merge into one with the next power of
 * two, once per move and tile.  Adds the numbers of the new tiles to *score
 * (if not NULL), says what happened to each tile in *tr (if not NULL) and
 * returns whether anything on the board changed at all.
 */
static bool slide_classic(unsigned char *tiles, int w, int h, int d, int *score,
                          struct trace *tr) {
    bool horizontal = (d == DIR_L || d == DIR_R);
    int nlines = horizontal ? h : w;
    int len = horizontal ? w : h;
    bool changed = false;
    int k, i;

    if (tr) trace_init(tr, w * h);

    for (k = 0; k < nlines; k++) {
        unsigned char out[MAX_SIZE];
        bool merged[MAX_SIZE];
        int first[MAX_SIZE];           /* the square that the tile in out[] came from */
        int pos[MAX_SIZE];
        int n = 0;

        for (i = 0; i < len; i++)
            pos[i] = line_pos(w, h, d, k, i);

        for (i = 0; i < len; i++) {
            unsigned char v = tiles[pos[i]];
            if (!v) continue;
            if (n > 0 && !merged[n-1] && out[n-1] == v && v < MAXPOWER) {
                out[n-1] = v + 1;
                merged[n-1] = true;
                if (score) *score += POWER[v + 1];
                if (tr) {
                    tr->dst[pos[i]] = pos[n-1];
                    tr->join[pos[i]] = 1;
                    tr->join[pos[first[n-1]]] = 1;
                }
            } else {
                out[n] = v;
                merged[n] = false;
                first[n] = i;
                if (tr) tr->dst[pos[i]] = pos[n];
                n++;
            }
        }
        for (i = n; i < len; i++) out[i] = 0;

        for (i = 0; i < len; i++) {
            if (tiles[pos[i]] != out[i]) {
                tiles[pos[i]] = out[i];
                changed = true;
            }
        }
    }
    return changed;
}

/* the old name, for the plain rules of 2048 */
static bool slide_board(unsigned char *tiles, int w, int h, int d, int *score) {
    return slide_classic(tiles, w, h, d, score, NULL);
}

/* Threes: can the tile a (that moves) merge with the tile b? */
static bool threes_can_merge(int a, int b) {
    if (!a || !b) return false;
    if (a < 3 || b < 3) return (a == 1 && b == 2) || (a == 2 && b == 1);
    return a == b && a < MAXPOWER;
}

static int threes_merge(int a, int b) {
    return a < 3 ? 3 : a + 1;
}

/* the number on a tile of Threes: 1, 2, 3, 6, 12 ... */
static int threes_value(int c) {
    return c < 3 ? c : 3 << (c - 3);
}

/* what a tile is worth in the score: 3 to the power of the number of doublings plus one */
static int threes_points(int c) {
    int p = 3, i;
    if (c < 3) return 0;
    for (i = 3; i < c; i++) p *= 3;
    return p;
}

static int threes_score(const unsigned char *tiles, int n) {
    long s = 0;
    int i;
    for (i = 0; i < n; i++) s += threes_points(tiles[i]);
    return s > 2000000000L ? 2000000000 : (int)s;
}

/*
 * Threes: move the tiles one square in direction d.  In every line the first
 * place from the side that the tiles move to where a tile can move into an
 * empty square or merge with its neighbour decides: the tiles from there on
 * all move one square (the merged tile takes the place of the two).  Lines
 * where nothing can move stay as they are.  moved[k] (if not NULL) says
 * whether line k changed; the square at the far end of such a line is empty
 * afterwards.  Returns whether anything changed.
 */
static bool slide_threes(unsigned char *tiles, int w, int h, int d, bool *moved,
                         struct trace *tr) {
    bool horizontal = (d == DIR_L || d == DIR_R);
    int nlines = horizontal ? h : w;
    int len = horizontal ? w : h;
    bool changed = false;
    int k, i;

    if (tr) trace_init(tr, w * h);

    for (k = 0; k < nlines; k++) {
        unsigned char src[MAX_SIZE];
        int pos[MAX_SIZE];
        int trig = -1;

        if (moved) moved[k] = false;
        for (i = 0; i < len; i++) {
            pos[i] = line_pos(w, h, d, k, i);
            src[i] = tiles[pos[i]];
            if (tr && src[i]) tr->dst[pos[i]] = pos[i];
        }
        for (i = 0; i + 1 < len; i++) {
            if ((!src[i] && src[i+1]) || threes_can_merge(src[i], src[i+1])) {
                trig = i;
                break;
            }
        }
        if (trig < 0) continue;

        changed = true;
        if (moved) moved[k] = true;
        if (src[trig]) {
            tiles[pos[trig]] = threes_merge(src[trig], src[trig+1]);
            if (tr) {
                tr->dst[pos[trig+1]] = pos[trig];
                tr->join[pos[trig]] = tr->join[pos[trig+1]] = 1;
            }
        } else {
            tiles[pos[trig]] = src[trig+1];
            if (tr) tr->dst[pos[trig+1]] = pos[trig];
        }
        for (i = trig + 1; i + 1 < len; i++) {
            tiles[pos[i]] = src[i+1];
            if (tr && src[i+1]) tr->dst[pos[i+1]] = pos[i];
        }
        tiles[pos[len-1]] = 0;
    }
    return changed;
}

static bool slide_any(int rules, unsigned char *tiles, int w, int h, int d, int *score,
                      bool *moved, struct trace *tr) {
    if (rules == RULES_THREES)
        return slide_threes(tiles, w, h, d, moved, tr);
    return slide_classic(tiles, w, h, d, score, tr);
}

static bool move_possible(const unsigned char *tiles, int w, int h, int rules, int d) {
    unsigned char copy[MAX_CELLS];
    memcpy(copy, tiles, w * h);
    return slide_any(rules, copy, w, h, d, NULL, NULL, NULL);
}

static bool moves_possible(const unsigned char *tiles, int w, int h, int rules) {
    int d;
    for (d = 0; d < NDIRS; d++)
        if (move_possible(tiles, w, h, rules, d)) return true;
    return false;
}

/* a random empty square, or -1 if the board is full */
static int random_empty(random_state *rs, const unsigned char *grid, int size) {
    int freecells[MAX_CELLS];
    int i, c = 0;

    for (i = 0; i < size; i++)
        if (grid[i] == 0)
            freecells[c++] = i;
    if (c == 0)
        return -1;
    return freecells[random_upto(rs, c)];
}

/* 2048: a tile with 2 (mostly) or 4 at a random empty square; its index, or -1 if the board is full */
static int add_new_number(random_state *rs, unsigned char *grid, int size) {
    int i = random_empty(rs, grid, size);
    if (i < 0)
        return -1;
    grid[i] = random_upto(rs, 10) == 0 ? 2 : 1;
    return i;
}

/*
 * Threes: the tile that is next.  Mostly it comes from the bag (four 1s,
 * four 2s and four 3s; the bag is filled again when it is empty); once there
 * is a tile of 48 or more, one time in 21 it is a bonus tile instead: from 6
 * up to an eighth of the biggest tile.
 */
static int threes_draw(random_state *rs, int bag[3], const unsigned char *tiles, int n) {
    int i, maxc = 0, total, r, t;

    for (i = 0; i < n; i++)
        if (tiles[i] > maxc) maxc = tiles[i];
    if (maxc >= 7 && random_upto(rs, 21) == 0)
        return 4 + (int)random_upto(rs, maxc - 6);

    total = bag[0] + bag[1] + bag[2];
    if (total == 0) {
        bag[0] = bag[1] = bag[2] = 4;
        total = 12;
    }
    r = (int)random_upto(rs, total);
    for (t = 0; t < 3; t++) {
        if (r < bag[t]) break;
        r -= bag[t];
    }
    bag[t]--;
    return t + 1;
}

/* the number to show on a tile */
static int tile_number(int rules, int c) {
    return rules == RULES_THREES ? threes_value(c) : POWER[c];
}

/* ----------------------------------------------------------------------
 * Game descriptions: the tiles at the start (runs of empty squares as
 * a ... z, tiles as 1 ... 9 and A ... F), a comma and the random state.  For
 * Threes there is a part between the two commas: the tile that comes next
 * and the numbers of 1s, 2s and 3s that are left in the bag.
 */

static char *encode_board(char *p, const unsigned char *grid, int n) {
    int run = 0, i;

    for (i = 0; i < n; i++) {
        if (grid[i] == 0)
            run++;
        else {
            while (run >= 26) {
                *p++ = 'z';
                run -= 26;
            }
            if (run)
                *p++ = 'a' + run - 1;
            run = 0;
            if (grid[i] < 10)
                *p++ = '0' + grid[i];
            else
                *p++ = 'A' + (grid[i] - 10);
        }
    }
    while (run >= 26) {
        *p++ = 'z';
        run -= 26;
    }
    if (run)
        *p++ = 'a' + run - 1;
    return p;
}

static char *new_game_desc(const game_params *params, random_state *rs,
                           char **aux, bool interactive) {
    int w = params->w, h = params->h, n = w * h;
    char *p;
    char *desc;
    int i;
    unsigned char grid[MAX_CELLS];
    char *rsdesc;
    int next = 0, bag[3];

    memset(grid, 0, sizeof(grid));

    if (params->rules == RULES_THREES) {
        /* about nine tenths... no: nine sixteenths of the board, as nine of the sixteen squares of Threes */
        int ntiles = n * 9 / 16;
        if (ntiles < 2) ntiles = 2;
        do {
            memset(grid, 0, sizeof(grid));
            bag[0] = bag[1] = bag[2] = 4;
            for (i = 0; i < ntiles; i++) {
                int cell = random_empty(rs, grid, n);
                grid[cell] = threes_draw(rs, bag, grid, n);
            }
            next = threes_draw(rs, bag, grid, n);
        } while (!moves_possible(grid, w, h, RULES_THREES));
    } else {
        add_new_number(rs, grid, n);
        add_new_number(rs, grid, n);
    }

    rsdesc = random_state_encode(rs);

    desc = snewn(n * 2 + strlen(rsdesc) + 16, char);
    p = encode_board(desc, grid, n);
    *p++ = ',';
    if (params->rules == RULES_THREES) {
        *p++ = next < 10 ? '0' + next : 'A' + (next - 10);
        *p++ = '0' + bag[0];
        *p++ = '0' + bag[1];
        *p++ = '0' + bag[2];
        *p++ = ',';
    }
    for (i = 0; rsdesc[i]; i++)
        *p++ = rsdesc[i];
    *p++ = '\0';

    sfree(rsdesc);
    return desc;
}

static const char *validate_desc(const game_params *params, const char *desc) {
    int w = params->w, h = params->h;
    int i = 0;
    const char *p = desc;
    bool threes = params->rules == RULES_THREES;

    while (*p != ',') {
        if(*p >= 'a' && *p <= 'z')
            i += ((*p) - 'a') + 1;
        else if (*p >= '1' && *p <= '9') {
            if (threes && *p > '3')
                return "Wrong tile in the game description";
            i++;
        } else if (*p >= 'A' && *p <= 'F') {
            if (threes)
                return "Wrong tile in the game description";
            i++;
        } else if (!*p)
            return "Game description is missing its random state";
        else
            return "Wrong character in game description";
        p++;
        if (i > w*h)
            return "Game description does not match grid size";
    }
    if (i != w*h)
        return "Game description does not match grid size";

    p++;
    if (threes) {
        if (!(p[0] >= '1' && p[0] <= '3'))
            return "Wrong next tile in the game description";
        for (i = 1; i <= 3; i++)
            if (!(p[i] >= '0' && p[i] <= '4'))
                return "Wrong bag in the game description";
        if (p[4] != ',')
            return "Game description is missing its random state";
        p += 5;
    }
    for (i = 0; p[i]; i++)
        if (!isxdigit((unsigned char)p[i]))
            return "Wrong character in the random state of the game description";
    if (i != RS_DIGITS)
        return "Wrong length of the random state of the game description";

    return NULL;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc) {
    int i, wh;
    const char *p = desc;
    game_state *state = snew(game_state);

    binary_last_rules = params->rules;
    state->w = params->w;
    state->h = params->h;
    state->goal = params->goal;
    state->mode = params->mode;
    state->rules = params->rules;
    state->score = 0;
    state->won = state->finished = false;
    state->next = 0;
    state->bag[0] = state->bag[1] = state->bag[2] = 0;
    state->lastdir = -1;
    state->spawn = -1;

    wh = state->w * state->h;
    state->tiles = snewn(wh, unsigned char);
    memset(state->tiles, 0, wh*sizeof(unsigned char));
    i = 0;
    while(*p != ',') {
        if(*p >= 'a' && *p <= 'z')
            i += ((*p++) - 'a') + 1;
        else if (*p >= '1' && *p <= '9')
            state->tiles[i++] = (*p++) - '0';
        else if (*p >= 'A' && *p <= 'F')
            state->tiles[i++] = 10 + (*p++) - 'A';
        else p++;
    }
    p++;
    if (state->rules == RULES_THREES) {
        state->next = *p++ - '0';
        for (i = 0; i < 3; i++)
            state->bag[i] = *p++ - '0';
        p++;
        state->score = threes_score(state->tiles, wh);
    }
    state->rs = random_state_decode(p);

    for (i = 0; i < wh; i++)
        if (state->tiles[i] >= GOAL_EXP(state->goal))
            state->won = true;
    if (!moves_possible(state->tiles, state->w, state->h, state->rules))
        state->finished = true;

    return state;
}

static game_state *dup_game(const game_state *state) {
    int wh;
    game_state *ret = snew(game_state);

    wh = state->w * state->h;
    ret->w = state->w;
    ret->h = state->h;
    ret->goal = state->goal;
    ret->mode = state->mode;
    ret->rules = state->rules;
    ret->score = state->score;
    ret->won = state->won;
    ret->finished = state->finished;
    ret->next = state->next;
    memcpy(ret->bag, state->bag, sizeof(ret->bag));
    ret->lastdir = state->lastdir;
    ret->spawn = state->spawn;
    ret->rs = random_copy(state->rs);
    ret->tiles = snewn(wh, unsigned char);
    memcpy(ret->tiles, state->tiles, wh*sizeof(unsigned char));

    return ret;
}

static void free_game(game_state *state) {
    random_free(state->rs);
    sfree(state->tiles);
    sfree(state);
}

/* ----------------------------------------------------------------------
 * "Show next step": the move that a look-ahead search rates best.
 *
 * An expectimax search: after a move a new tile turns up at one of the
 * empty squares (a 2 nine times in ten, else a 4), then it is our move
 * again, and so on for a few plies.  The boards at the end are rated by
 * the number of empty squares, the possible merges, how much the rows and
 * columns go up or down in one direction and the sum of the tiles (the
 * weights are those of the well known 2048 players).  The search deepens
 * as long as it stays within a budget of positions.
 */

#define HINT_BUDGET 120000L            /* positions per hint */
#define HINT_MAX_DEPTH 8
#define HINT_CPROB_MIN 0.0001
#define HINT_LOST (-1.0e9)

struct hint_search {
    int w, h;
    long nodes, budget;
    bool aborted;
    double pmono[MAXPOWER + 1], psum[MAXPOWER + 1];
};

static void hint_init(struct hint_search *hs, int w, int h) {
    int e;
    hs->w = w;
    hs->h = h;
    hs->nodes = 0;
    hs->budget = HINT_BUDGET;
    hs->aborted = false;
    for (e = 0; e <= MAXPOWER; e++) {
        hs->pmono[e] = e ? pow((double)e, 4.0) : 0.0;
        hs->psum[e] = e ? pow((double)e, 3.5) : 0.0;
    }
}

/* the rating of one row or column of len tiles that are stride apart */
static double rate_line(const struct hint_search *hs, const unsigned char *t,
                        int len, int stride) {
    int i, empty = 0, merges = 0, counter = 0, prev = 0;
    double sum = 0, mono_left = 0, mono_right = 0;

    for (i = 0; i < len; i++) {
        int rank = t[i * stride];
        sum += hs->psum[rank];
        if (rank == 0) {
            empty++;
        } else {
            if (prev == rank) {
                counter++;
            } else if (counter > 0) {
                merges += 1 + counter;
                counter = 0;
            }
            prev = rank;
        }
    }
    if (counter > 0) merges += 1 + counter;

    for (i = 1; i < len; i++) {
        int a = t[(i - 1) * stride], b = t[i * stride];
        if (a > b) mono_left += hs->pmono[a] - hs->pmono[b];
        else mono_right += hs->pmono[b] - hs->pmono[a];
    }

    return 200000.0 + 270.0 * empty + 700.0 * merges
           - 47.0 * (mono_left < mono_right ? mono_left : mono_right)
           - 11.0 * sum;
}

static double rate_board(const struct hint_search *hs, const unsigned char *t) {
    int x, y;
    double score = 0;
    for (y = 0; y < hs->h; y++)
        score += rate_line(hs, t + y * hs->w, hs->w, 1);
    for (x = 0; x < hs->w; x++)
        score += rate_line(hs, t + x, hs->h, hs->w);
    return score;
}

static double hint_move_node(struct hint_search *hs, const unsigned char *board,
                             int depth, double cprob);

/* the rating of a board at the end of a branch */
static double hint_leaf(struct hint_search *hs, const unsigned char *board) {
    if (++hs->nodes > hs->budget) hs->aborted = true;
    return rate_board(hs, board);
}

/* board: after a move; the value of what comes next, with depth moves to go */
static double hint_chance_node(struct hint_search *hs, const unsigned char *board,
                               int depth, double cprob) {
    int n = hs->w * hs->h, i, empty = 0;
    unsigned char copy[MAX_CELLS];
    double total = 0;

    if (depth <= 0 || cprob < HINT_CPROB_MIN || hs->aborted)
        return hint_leaf(hs, board);

    for (i = 0; i < n; i++)
        if (!board[i]) empty++;
    if (!empty)
        return hint_leaf(hs, board);

    memcpy(copy, board, n);
    cprob /= empty;
    for (i = 0; i < n; i++) {
        if (copy[i]) continue;
        copy[i] = 1;
        total += 0.9 * hint_move_node(hs, copy, depth, cprob * 0.9);
        copy[i] = 2;
        total += 0.1 * hint_move_node(hs, copy, depth, cprob * 0.1);
        copy[i] = 0;
        if (hs->aborted) return 0;
    }
    return total / empty;
}

/* board: a new tile has turned up; the best of our moves, with depth to go */
static double hint_move_node(struct hint_search *hs, const unsigned char *board,
                             int depth, double cprob) {
    int n = hs->w * hs->h, d;
    double best = HINT_LOST;
    unsigned char copy[MAX_CELLS];

    if (++hs->nodes > hs->budget) hs->aborted = true;
    if (hs->aborted) return 0;

    for (d = 0; d < NDIRS; d++) {
        memcpy(copy, board, n);
        if (slide_board(copy, hs->w, hs->h, d, NULL)) {
            double v = hint_chance_node(hs, copy, depth - 1, cprob);
            if (hs->aborted) return 0;
            if (v > best) best = v;
        }
    }
    return best;
}

/*
 * The best direction for a board, or -1 if there is no move.  The deepest
 * search that stays within the budget decides: a search that is to look one
 * ply further is started only when the one before was cheap enough (the work
 * grows by a factor of ten or more with every ply).
 */
static int hint_best_direction(const unsigned char *board, int w, int h) {
    struct hint_search hs;
    int chosen = -1, depth, ndirs = 0, d;
    long prev_nodes = 0;
    double ratio = 8.0;

    hint_init(&hs, w, h);
    for (d = 0; d < NDIRS; d++)
        if (move_possible(board, w, h, RULES_2048, d)) { ndirs++; chosen = d; }
    if (ndirs <= 1)
        return chosen;                 /* no choice */

    chosen = -1;
    for (depth = 1; depth <= HINT_MAX_DEPTH; depth++) {
        int best = -1;
        double bestv = 0;
        unsigned char copy[MAX_CELLS];

        hs.nodes = 0;
        hs.aborted = false;
        for (d = 0; d < NDIRS && !hs.aborted; d++) {
            memcpy(copy, board, w * h);
            if (slide_board(copy, w, h, d, NULL)) {
                double v = hint_chance_node(&hs, copy, depth - 1, 1.0);
                if (!hs.aborted && (best < 0 || v > bestv)) { best = d; bestv = v; }
            }
        }
        if (hs.aborted || best < 0)
            break;
        chosen = best;
        /* did the search look deeper at all? */
        if (depth > 1 && hs.nodes == prev_nodes)
            break;
        if (prev_nodes > 0 && hs.nodes > prev_nodes) {
            double r = (double)hs.nodes / prev_nodes;
            if (r > ratio) ratio = r;
        }
        if ((double)hs.nodes * ratio > (double)hs.budget)
            break;
        prev_nodes = hs.nodes;
    }
    return chosen;
}

/*
 * "Show next step" for Threes.  The same kind of search, with the moves of
 * Threes: after a move the tile that comes next enters at the far end of one
 * of the lines that moved (the first tile is known, the ones after it are
 * taken to be a 1, a 2 or a 3 with the same chance).  The boards are rated
 * by the empty squares, the pairs of neighbours that can merge, how orderly
 * the lines are (the 1s and 2s count as one rank, 3 as the next, 6 as the
 * next and so on), the sum of the tiles, and by the number of 1s and 2s, which
 * are in the way until a 1 and a 2 meet.
 */

/* the weights of the rating of a board of Threes (found by playing many games with different ones) */
#define THR_W_EMPTY 270.0              /* every empty square */
#define THR_W_MERGE 700.0              /* every pair of neighbours that can merge */
#define THR_W_MONO 47.0                /* disorder of the lines (the lesser of the two directions) */
#define THR_W_SUM 11.0                 /* the sum of the tiles (by rank) */
#define THR_W_SMALL 200.0              /* every 1 and 2: they only merge with each other */

struct thr_search {
    int w, h;
    long nodes, budget;
    bool aborted;
    double pmono[MAXPOWER + 1], psum[MAXPOWER + 1];
};

static int thr_rank(int c) {
    return c == 0 ? 0 : c < 3 ? 1 : c - 1;
}

static void thr_init(struct thr_search *hs, int w, int h) {
    int e;
    hs->w = w;
    hs->h = h;
    hs->nodes = 0;
    hs->budget = HINT_BUDGET;
    hs->aborted = false;
    for (e = 0; e <= MAXPOWER; e++) {
        hs->pmono[e] = e ? pow((double)e, 4.0) : 0.0;
        hs->psum[e] = e ? pow((double)e, 3.5) : 0.0;
    }
}

static double thr_rate_line(const struct thr_search *hs, const unsigned char *t,
                            int len, int stride) {
    int i, empty = 0, merges = 0;
    double sum = 0, mono_left = 0, mono_right = 0;

    for (i = 0; i < len; i++) {
        int c = t[i * stride];
        sum += hs->psum[thr_rank(c)];
        if (!c) empty++;
    }
    for (i = 1; i < len; i++) {
        int a = t[(i - 1) * stride], b = t[i * stride];
        int ra = thr_rank(a), rb = thr_rank(b);
        if (threes_can_merge(a, b)) merges++;
        if (ra > rb) mono_left += hs->pmono[ra] - hs->pmono[rb];
        else mono_right += hs->pmono[rb] - hs->pmono[ra];
    }
    return 200000.0 + THR_W_EMPTY * empty + THR_W_MERGE * merges
           - THR_W_MONO * (mono_left < mono_right ? mono_left : mono_right)
           - THR_W_SUM * sum;
}

static double thr_rate_board(const struct thr_search *hs, const unsigned char *t) {
    int x, y, i, nsmall = 0;
    double score = 0;
    for (y = 0; y < hs->h; y++)
        score += thr_rate_line(hs, t + y * hs->w, hs->w, 1);
    for (x = 0; x < hs->w; x++)
        score += thr_rate_line(hs, t + x, hs->h, hs->w);
    for (i = 0; i < hs->w * hs->h; i++)
        if (t[i] == 1 || t[i] == 2) nsmall++;
    return score - THR_W_SMALL * nsmall;
}

static double thr_leaf(struct thr_search *hs, const unsigned char *board) {
    if (++hs->nodes > hs->budget) hs->aborted = true;
    return thr_rate_board(hs, board);
}

static double thr_move_node(struct thr_search *hs, const unsigned char *board, int depth);

/*
 * The board after our move d (the lines marked in moved[] have changed): the
 * value of the position when the tile next (0: not known, then a 1, a 2 or a
 * 3) has come in at the far end of one of them, with depth moves to go.
 */
static double thr_after_move(struct thr_search *hs, const unsigned char *board,
                             const bool *moved, int d, int next, int depth) {
    int w = hs->w, h = hs->h, n = w * h;
    bool horizontal = (d == DIR_L || d == DIR_R);
    int nlines = horizontal ? h : w, len = horizontal ? w : h;
    int k, t, nm = 0, ntiles = next ? 1 : 3;
    unsigned char copy[MAX_CELLS];
    double total = 0;

    for (k = 0; k < nlines; k++)
        if (moved[k]) nm++;
    if (!nm)
        return thr_leaf(hs, board);
    memcpy(copy, board, n);
    for (k = 0; k < nlines; k++) {
        int cell;
        if (!moved[k]) continue;
        cell = line_pos(w, h, d, k, len - 1);
        for (t = 0; t < ntiles; t++) {
            copy[cell] = next ? next : t + 1;
            total += depth <= 1 ? thr_leaf(hs, copy) : thr_move_node(hs, copy, depth - 1);
            if (hs->aborted) return 0;
        }
        copy[cell] = 0;
    }
    return total / (nm * ntiles);
}

/* board: a new tile has come in; the best of our moves, with depth to go */
static double thr_move_node(struct thr_search *hs, const unsigned char *board, int depth) {
    int n = hs->w * hs->h, d;
    double best = HINT_LOST;
    unsigned char copy[MAX_CELLS];
    bool moved[MAX_SIZE];

    if (++hs->nodes > hs->budget) hs->aborted = true;
    if (hs->aborted) return 0;

    for (d = 0; d < NDIRS; d++) {
        memcpy(copy, board, n);
        if (slide_threes(copy, hs->w, hs->h, d, moved, NULL)) {
            double v = thr_after_move(hs, copy, moved, d, 0, depth);
            if (hs->aborted) return 0;
            if (v > best) best = v;
        }
    }
    return best;
}

static int thr_best_direction(const game_state *s) {
    struct thr_search hs;
    int w = s->w, h = s->h;
    int chosen = -1, depth, ndirs = 0, d;
    long prev_nodes = 0;
    double ratio = 8.0;

    thr_init(&hs, w, h);
    for (d = 0; d < NDIRS; d++)
        if (move_possible(s->tiles, w, h, RULES_THREES, d)) { ndirs++; chosen = d; }
    if (ndirs <= 1)
        return chosen;

    chosen = -1;
    for (depth = 1; depth <= HINT_MAX_DEPTH; depth++) {
        int best = -1;
        double bestv = 0;
        unsigned char copy[MAX_CELLS];
        bool moved[MAX_SIZE];

        hs.nodes = 0;
        hs.aborted = false;
        for (d = 0; d < NDIRS && !hs.aborted; d++) {
            memcpy(copy, s->tiles, w * h);
            if (slide_threes(copy, w, h, d, moved, NULL)) {
                double v = thr_after_move(&hs, copy, moved, d, s->next, depth);
                if (!hs.aborted && (best < 0 || v > bestv)) { best = d; bestv = v; }
            }
        }
        if (hs.aborted || best < 0)
            break;
        chosen = best;
        if (depth > 1 && hs.nodes == prev_nodes)
            break;
        if (prev_nodes > 0 && hs.nodes > prev_nodes) {
            double r = (double)hs.nodes / prev_nodes;
            if (r > ratio) ratio = r;
        }
        if ((double)hs.nodes * ratio > (double)hs.budget)
            break;
        prev_nodes = hs.nodes;
    }
    return chosen;
}

static char *hint_game(const game_state *orig, const game_state *curr,
                       const char **error) {
    int d;
    char buf[2];

    if (curr->finished) {
        *error = "No more moves";
        return NULL;
    }
    if (curr->rules == RULES_THREES)
        d = thr_best_direction(curr);
    else
        d = hint_best_direction(curr->tiles, curr->w, curr->h);
    if (d < 0) {
        *error = "No more moves";
        return NULL;
    }
    buf[0] = dir_chars[d];
    buf[1] = '\0';
    return dupstr(buf);
}

/* ----------------------------------------------------------------------
 * The user interface and the moves.
 *
 * A move is one of the letters L, R, U and D: all the tiles slide that way.
 */

struct game_ui {
    int background;
    int inputtype;
    int anim;
    int x, y;                          /* where the press that is going on started, or -1 */
};

static game_ui *new_ui(const game_state *state) {
    game_ui *ui = snew(game_ui);
    ui->background = BACKGROUND_SLOTS;
    ui->inputtype = INPUT_SWIPE;
    ui->anim = ANIM_NORMAL;
    ui->x = ui->y = -1;
    return ui;
}

static void free_ui(game_ui *ui) {
    sfree(ui);
}

static config_item *get_prefs(game_ui *ui)
{
    config_item *ret;

    ret = snewn(N_PREF_ITEMS+1, config_item);

    ret[PREF_GAME_BACKGROUND].name = "Game background";
    ret[PREF_GAME_BACKGROUND].kw = "background";
    ret[PREF_GAME_BACKGROUND].type = C_CHOICES;
    ret[PREF_GAME_BACKGROUND].u.choices.choicenames = ":Slots:Empty:Chessboard:Lines";
    ret[PREF_GAME_BACKGROUND].u.choices.choicekws = ":slots:empty:chess:lines";
    ret[PREF_GAME_BACKGROUND].u.choices.selected = ui->background;

    ret[PREF_INPUT_METHOD].name = "Input method";
    ret[PREF_INPUT_METHOD].kw = "input";
    ret[PREF_INPUT_METHOD].type = C_CHOICES;
    ret[PREF_INPUT_METHOD].u.choices.choicenames = ":Swipe:Tap";
    ret[PREF_INPUT_METHOD].u.choices.choicekws = ":swipe:tap";
    ret[PREF_INPUT_METHOD].u.choices.selected = ui->inputtype;

    ret[PREF_ANIMATION].name = "Animation";
    ret[PREF_ANIMATION].kw = "animation";
    ret[PREF_ANIMATION].type = C_CHOICES;
    ret[PREF_ANIMATION].u.choices.choicenames = ":Normal:Fast:Off";
    ret[PREF_ANIMATION].u.choices.choicekws = ":normal:fast:off";
    ret[PREF_ANIMATION].u.choices.selected = ui->anim;

    ret[N_PREF_ITEMS].name = NULL;
    ret[N_PREF_ITEMS].type = C_END;

    return ret;
}

static void set_prefs(game_ui *ui, const config_item *cfg)
{
    ui->background = cfg[PREF_GAME_BACKGROUND].u.choices.selected;
    ui->inputtype = cfg[PREF_INPUT_METHOD].u.choices.selected;
    ui->anim = cfg[PREF_ANIMATION].u.choices.selected;
}

static key_label *game_request_keys(const game_params *params, int *nkeys, int *arrow_mode)
{
    *nkeys = 0;
    *arrow_mode = ANDROID_ARROWS_ONLY;
    return NULL;
}

static bool game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate) {
    ui->x = ui->y = -1;
    /* the goal has been reached with this move (the game goes on) */
    return oldstate && !oldstate->won && newstate->won;
}

/*
 * The look of the two games, taken from the originals.
 *
 * 2048 (Gabriele Cirulli): a board of 500 by 500 units (colour #bbada0, rounded
 * with 6) with 4 by 4 squares of 106.25 units, 15 units apart and from the
 * edge, rounded with 3, in the colour of the milky 35% of #eee4da on the board;
 * the tiles are as big as the squares.  That makes the pitch of the squares
 * 121.25 units, the size of the tile 0.876 of it and the gap 0.124.
 *
 * Threes (Asher Vollmer, Greg Wohlwend, Jimmy Hinson): the cards are upright
 * (about 5 wide to 7 high) with round corners and a darker lower edge, on the
 * light blue of the screen; the empty places are darker hollows; the next card
 * is shown above the board.
 *
 * Everything is worked out from the pitch of the columns (the tile size of
 * the puzzle framework) by geom_set().
 */
struct geom {
    int w, h;                          /* the squares of the board */
    int p, py;                         /* the pitch of the columns and of the rows */
    int g;                             /* the gap between tiles, and from the tiles to the edge of the board */
    int m;                             /* the margin round the board */
    int tw, th;                        /* the size of a tile (the pitch less the gap) */
    int top;                           /* the height of the strip above the board (Threes: the next card) */
    int bw, bh;                        /* the size of the board without the margin */
    int width, height;                 /* the size of the whole picture */
};

static void geom_set(struct geom *G, int rules, int w, int h, int p)
{
    G->w = w;
    G->h = h;
    G->p = p;
    if (rules == RULES_THREES) {
        G->py = p * 11 / 8;
        G->g = (p * 9 + 50) / 100;
        G->top = p * 7 / 8;
    } else {
        G->py = p;
        G->g = (p * 1237 + 5000) / 10000;
        G->top = 0;
    }
    if (G->g < 1) G->g = 1;
    G->m = p / 10;
    if (G->m < 1) G->m = 1;
    G->tw = p - G->g;
    G->th = G->py - G->g;
    G->bw = w * p + G->g;
    G->bh = h * G->py + G->g;
    G->width = G->bw + 2 * G->m;
    G->height = G->top + G->bh + 2 * G->m;
}

/* where the tile of column i or row j starts */
#define TILE_X(G, i) ((G)->m + (G)->g + (i) * (G)->p)
#define TILE_Y(G, j) ((G)->top + (G)->m + (G)->g + (j) * (G)->py)

struct game_drawstate {
    bool started;
    bool finished;
    bool animated;                     /* the last picture was a step of an animation */
    int background;
    int tilesize;
    struct geom g;
    int w, h, rules;
    int next;
    unsigned char *tiles;
};

/* a swipe has to be at least this long (in pixels of the picture) */
static int swipe_threshold(const game_drawstate *ds) {
    int t = ds->tilesize / 2;
    return t < 8 ? 8 : t;
}

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button) {
    int dx, dy, xr, yr, c1, c2, thr;
    int w = state->w, h = state->h;
    int d = -1;

    button &= ~MOD_MASK;

    if (IS_CURSOR_MOVE(button)) {
        if (state->finished)
            return MOVE_NO_EFFECT;
        d = button == CURSOR_LEFT ? DIR_L : button == CURSOR_RIGHT ? DIR_R :
            button == CURSOR_UP ? DIR_U : DIR_D;
    } else if (button == LEFT_BUTTON) {
        ui->x = x;
        ui->y = y;
        if (state->finished)
            return MOVE_NO_EFFECT;
        if (ui->inputtype == INPUT_TAP) {
            /* the four triangles of the board (without the strip above it) */
            int yy = y - ds->g.top;
            if (yy < 0)
                return MOVE_NO_EFFECT;
            xr = ds->g.bw + 2 * ds->g.m;
            yr = ds->g.bh + 2 * ds->g.m;
            c1 = xr*yy-yr*x;
            c2 = xr*(yy-yr)+yr*x;
            if ((c1 > 0) && (c2 < 0))
                d = DIR_L;
            else if ((c1 < 0) && (c2 > 0))
                d = DIR_R;
            else if ((c1 < 0) && (c2 < 0))
                d = DIR_U;
            else if ((c1 > 0) && (c2 > 0))
                d = DIR_D;
        } else {
            return MOVE_NO_EFFECT;
        }
    } else if (button == LEFT_DRAG || button == RIGHT_BUTTON ||
               button == RIGHT_DRAG || button == RIGHT_RELEASE) {
        return MOVE_NO_EFFECT;
    } else if (button == LEFT_RELEASE) {
        int sx = ui->x, sy = ui->y;
        ui->x = ui->y = -1;
        if (sx < 0 || sy < 0 || state->finished || ui->inputtype != INPUT_SWIPE)
            return MOVE_NO_EFFECT;
        dx = x - sx;
        dy = y - sy;
        thr = swipe_threshold(ds);
        if ((dx < -thr) && (abs(dx) > 2*abs(dy)))
            d = DIR_L;
        else if ((dx > thr) && (abs(dx) > 2*abs(dy)))
            d = DIR_R;
        else if ((dy < -thr) && (abs(dy) > 2*abs(dx)))
            d = DIR_U;
        else if ((dy > thr) && (abs(dy) > 2*abs(dx)))
            d = DIR_D;
    }

    if (d < 0)
        return MOVE_UNUSED;
    if (!move_possible(state->tiles, w, h, state->rules, d))
        return MOVE_NO_EFFECT;
    {
        char buf[2];
        buf[0] = dir_chars[d];
        buf[1] = '\0';
        return dupstr(buf);
    }
}

static game_state *execute_move(const game_state *state, const char *move) {
    game_state *ret;
    int w = state->w, h = state->h, d, score = 0, i;

    if (state->finished)
        return NULL;
    if (!move[0] || move[1])
        return NULL;
    d = dir_from_char(move[0]);
    if (d < 0)
        return NULL;

    ret = dup_game(state);
    ret->lastdir = d;
    ret->spawn = -1;
    if (state->rules == RULES_THREES) {
        bool moved[MAX_SIZE];
        int nlines = (d == DIR_L || d == DIR_R) ? h : w;
        int len = (d == DIR_L || d == DIR_R) ? w : h;
        int k, nm = 0, pick;

        if (!slide_threes(ret->tiles, w, h, d, moved, NULL)) {
            free_game(ret);
            return NULL;
        }
        for (k = 0; k < nlines; k++)
            if (moved[k]) nm++;
        pick = (int)random_upto(ret->rs, nm);
        for (k = 0; k < nlines; k++) {
            if (!moved[k]) continue;
            if (pick-- == 0) {
                ret->spawn = line_pos(w, h, d, k, len - 1);
                break;
            }
        }
        ret->tiles[ret->spawn] = ret->next;
        ret->next = threes_draw(ret->rs, ret->bag, ret->tiles, w * h);
        ret->score = threes_score(ret->tiles, w * h);
    } else {
        if (!slide_classic(ret->tiles, w, h, d, &score, NULL)) {
            free_game(ret);
            return NULL;
        }
        ret->score += score;
        ret->spawn = add_new_number(ret->rs, ret->tiles, w * h);
    }
    for (i = 0; i < w * h; i++) {
        if (ret->tiles[i] >= GOAL_EXP(ret->goal)) ret->won = true;
        if (ret->tiles[i] >= MAXPOWER) ret->finished = true;
    }
    if (!moves_possible(ret->tiles, w, h, ret->rules)) ret->finished = true;
    return ret;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_compute_size(const game_params *params, int tilesize,
                              const game_ui *ui, int *x, int *y) {
    struct geom G;
    geom_set(&G, params->rules, params->w, params->h, tilesize);
    *x = G.width;
    *y = G.height;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize) {
    ds->tilesize = tilesize;
    geom_set(&ds->g, params->rules, params->w, params->h, tilesize);
}

static void set_rgb(float *ret, int colour, int rgb) {
    ret[colour * 3 + 0] = ((rgb >> 16) & 0xff) / 255.0F;
    ret[colour * 3 + 1] = ((rgb >> 8) & 0xff) / 255.0F;
    ret[colour * 3 + 2] = (rgb & 0xff) / 255.0F;
}

/* the colour a of the way from the colour in from[] to the one in to[] */
static void mix_rgb(float *ret, int colour, const float *from, const float *to, float a) {
    int i;
    for (i = 0; i < 3; i++)
        ret[colour * 3 + i] = from[i] + a * (to[i] - from[i]);
}

static float *game_colours(frontend *fe, int *ncolours) {
    float *ret = snewn(3 * NCOLOURS, float);
    /* the tiles of 2048, from 2 to 131072: the colours of the original, all that are over 2048 dark */
    static const int tile_rgb[MAXPOWER] = {
        0xeee4da, 0xede0c8, 0xf2b179, 0xf59563, 0xf67c5f, 0xf65e3b, 0xedcf72, 0xedcc61,
        0xedc850, 0xedc53f, 0xedc22e, 0x3c3a32, 0x3c3a32, 0x3c3a32, 0x3c3a32, 0x3c3a32,
        0x3c3a32
    };
    /* the glow of the tiles from 128 on: the colour of it and how strong it is in the original */
    static const float glow_alpha[5] = { 0.2381F, 0.31746F, 0.39683F, 0.47619F, 0.55556F };
    const bool threes = binary_last_rules == RULES_THREES;
    float veil[3], board[3], glow[3];
    float veil_alpha;
    int i, k;

    if (threes) {
        /* the light blue screen with darker hollows for the empty places */
        set_rgb(ret, COL_BACKGROUND, 0x74d3d6);
        set_rgb(ret, COL_BOARD, 0x74d3d6);
        set_rgb(ret, COL_SLOT, 0x5bbbbf);
        set_rgb(ret, COL_TEXT, 0x1f5a5e);
        veil[0] = 0xd9 / 255.0F; veil[1] = 0xf3 / 255.0F; veil[2] = 0xf3 / 255.0F;
        veil_alpha = 0.7F;
    } else {
        /* the cream page with the brown board of 2048; the squares are the milky 35% of #eee4da */
        set_rgb(ret, COL_BACKGROUND, 0xfaf8ef);
        set_rgb(ret, COL_BOARD, 0xbbada0);
        set_rgb(ret, COL_SLOT, 0xcdc0b4);
        set_rgb(ret, COL_TEXT, 0x776e65);
        veil[0] = 238 / 255.0F; veil[1] = 228 / 255.0F; veil[2] = 218 / 255.0F;
        veil_alpha = 0.73F;                /* the layer that 2048 puts over a game that is over */
    }
    set_rgb(ret, COL_NUM_DARK, 0x776e65);
    set_rgb(ret, COL_NUM_LIGHT, 0xf9f6f2);
    for (i = 1; i <= MAXPOWER; i++)
        set_rgb(ret, COL_TILE(i), tile_rgb[i - 1]);
    /* Threes: blue 1, red 2, white cards for the rest, each with a darker lower edge */
    set_rgb(ret, COL_BLUE, 0x66ccff);
    set_rgb(ret, COL_BLUE_EDGE, 0x3fa8e0);
    set_rgb(ret, COL_RED, 0xff6680);
    set_rgb(ret, COL_RED_EDGE, 0xd8405c);
    set_rgb(ret, COL_WHITE, 0xffffff);
    set_rgb(ret, COL_WHITE_EDGE, 0xc8e1e1);
    set_rgb(ret, COL_NUM_BLACK, 0x3a3f47);
    set_rgb(ret, COL_NUM_WHITE, 0xffffff);

    /* the veiled copies, for a game that is over */
    for (i = COL_NUM_DARK; i <= COL_NUM_WHITE; i++) {
        float c[3];
        c[0] = ret[i * 3]; c[1] = ret[i * 3 + 1]; c[2] = ret[i * 3 + 2];
        mix_rgb(ret, COL_VEILED(i), c, veil, veil_alpha);
    }

    /*
     * The glow of the original is a box-shadow of 0 0 30px 10px in the colour
     * (116, 185, 243) at the strengths above, on the brown board.  Two rings
     * round the tile stand for it: close to the tile it is about three
     * quarters of the strength, further out about two fifths.
     */
    board[0] = 0xbb / 255.0F; board[1] = 0xad / 255.0F; board[2] = 0xa0 / 255.0F;
    glow[0] = 116 / 255.0F; glow[1] = 185 / 255.0F; glow[2] = 243 / 255.0F;
    for (k = 0; k < 5; k++) {
        mix_rgb(ret, COL_GLOW_RING(k, 0), board, glow, 0.75F * glow_alpha[k]);
        mix_rgb(ret, COL_GLOW_RING(k, 1), board, glow, 0.40F * glow_alpha[k]);
    }
    *ncolours = NCOLOURS;

    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state) {
    struct game_drawstate *ds = snew(struct game_drawstate);
    int i;

    ds->started = false;
    ds->finished = false;
    ds->animated = false;
    ds->background = -1;
    ds->tilesize = 0;
    memset(&ds->g, 0, sizeof(ds->g));
    ds->w = state->w;
    ds->h = state->h;
    ds->rules = state->rules;
    ds->next = -1;
    ds->tiles = snewn(ds->w*ds->h, unsigned char);
    for (i = 0; i < ds->w*ds->h; i++)
        ds->tiles[i] = 0;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds) {
    sfree(ds->tiles);
    sfree(ds);
}

/*
 * A rectangle with rounded corners that covers w by h pixels from x, y (an
 * arc of up to four straight pieces for each corner; a chamfer if it is
 * small).
 */
static void draw_rounded(drawing *dr, int x, int y, int w, int h, int r, int colour)
{
    int coords[4 * 2 * 5];
    int n = 0, corner, k, seg, x1, y1;

    if (w <= 0 || h <= 0)
        return;
    if (2 * r > w) r = w / 2;
    if (2 * r > h) r = h / 2;
    if (r <= 1) {
        draw_rect(dr, x, y, w, h, colour);
        return;
    }
    seg = r <= 3 ? 1 : r <= 6 ? 2 : 4;
    x1 = x + w - 1;
    y1 = y + h - 1;
    for (corner = 0; corner < 4; corner++) {
        /* the middle of the arc and the angle that it starts at (clockwise on the screen) */
        int cx = (corner == 1 || corner == 2) ? x1 - r : x + r;
        int cy = (corner >= 2) ? y1 - r : y + r;
        int a0 = corner == 0 ? 180 : corner == 1 ? 270 : corner == 2 ? 0 : 90;
        for (k = 0; k <= seg; k++) {
            double a = (a0 + 90.0 * k / seg) * 3.14159265358979 / 180.0;
            coords[n++] = cx + (int)floor(r * cos(a) + 0.5);
            coords[n++] = cy + (int)floor(r * sin(a) + 0.5);
        }
    }
    draw_polygon(dr, coords, n / 2, colour, colour);
}

/* the colour of a tile, of its lower edge (Threes) and of its number */
static int tile_fill(int rules, int c) {
    if (rules == RULES_THREES)
        return c == 1 ? COL_BLUE : c == 2 ? COL_RED : COL_WHITE;
    return COL_TILE(c);
}

static int tile_edge(int c) {
    return c == 1 ? COL_BLUE_EDGE : c == 2 ? COL_RED_EDGE : COL_WHITE_EDGE;
}

static int tile_numcol(int rules, int c) {
    if (rules == RULES_THREES)
        return c <= 2 ? COL_NUM_WHITE : COL_NUM_BLACK;
    return c <= 2 ? COL_NUM_DARK : COL_NUM_LIGHT;
}

/* the corners of the squares, the tiles and the board of 2048 (3 and 6 units of 106.25 and 121.25) */
static int tile_radius(int rules, const struct geom *G) {
    return rules == RULES_THREES ? (int)(G->tw * 0.12F + 0.5F)
                                 : (int)(G->tw * 0.0282F + 0.5F);
}

static int board_radius(const struct geom *G) {
    return (G->p * 495 + 5000) / 10000;
}

/* the size of the number on a tile of c, as 2048 and Threes have it */
static int number_size(int rules, const struct geom *G, int nd, int c, float f) {
    float ratio, size;

    if (rules == RULES_THREES)
        ratio = nd <= 1 ? 0.62F : nd == 2 ? 0.52F : nd == 3 ? 0.40F : nd == 4 ? 0.31F : 0.25F;
    else
        ratio = nd <= 2 ? 0.514F :         /* 55 of 107 pixels */
                nd == 3 ? 0.42F :          /* 45 */
                nd == 4 ? (c > 11 ? 0.28F : 0.327F) :   /* 35, and 30 for the tiles over 2048 */
                nd == 5 ? 0.28F : 0.23F;
    size = G->tw * ratio * f;
    return (int)(size + 0.5F);
}

/*
 * A card of Threes at x, y, w by h: the face with a darker edge below it.
 */
static void draw_card(drawing *dr, int x, int y, int w, int h, int r,
                      int fill, int edgecol)
{
    int edge = h * 7 / 100;

    if (edge < 2) edge = 2;
    if (edge > h / 3) edge = h / 3;
    draw_rounded(dr, x, y, w, h, r, edgecol);
    draw_rounded(dr, x, y, w, h - edge, r, fill);
}

/*
 * A tile (c: its code) whose place on the board starts at x, y, scaled by f
 * about its middle.  veiled: the game is over.  text: what to write on it
 * instead of the number (or NULL).
 */
static void draw_tile(drawing *dr, const struct geom *G, int rules, int x, int y,
                      int c, bool veiled, const char *text, float f)
{
    int tw = (int)(G->tw * f + 0.5F), th = (int)(G->th * f + 0.5F);
    int x0 = x + (int)floorf((G->tw - tw) * 0.5F);
    int y0 = y + (int)floorf((G->th - th) * 0.5F);
    int fill = tile_fill(rules, c), numcol = tile_numcol(rules, c);
    int edgecol = rules == RULES_THREES ? tile_edge(c) : -1;
    int ymid, nd, size;
    char str[40];

    if (tw < 2 || th < 2)
        return;
    if (veiled) {
        fill = COL_VEILED(fill);
        numcol = COL_VEILED(numcol);
        if (edgecol >= 0) edgecol = COL_VEILED(edgecol);
    }

    if (rules == RULES_THREES) {
        int edge = th * 7 / 100;
        if (edge < 2) edge = 2;
        if (edge > th / 3) edge = th / 3;
        draw_card(dr, x0, y0, tw, th, (int)(tw * 0.12F + 0.5F), fill, edgecol);
        ymid = y0 + (th - edge) / 2;
    } else {
        /*
         * The tiles from 128 on glow, as in the original (a blue haze round
         * them), by two rings in the gap between the tiles.
         */
        if (!veiled && c >= 7) {
            int strength = c - 7 > 4 ? 4 : c - 7;
            int a2 = (int)(G->g * f * 0.5F), a1 = (int)(G->g * f * 0.25F);
            if (a1 < 1) a1 = 1;
            if (a2 <= a1) a2 = a1 + 1;
            if (f > 0.4F) {
                draw_rounded(dr, x0 - a2, y0 - a2, tw + 2 * a2, th + 2 * a2,
                             tile_radius(rules, G) + a2, COL_GLOW_RING(strength, 1));
                draw_rounded(dr, x0 - a1, y0 - a1, tw + 2 * a1, th + 2 * a1,
                             tile_radius(rules, G) + a1, COL_GLOW_RING(strength, 0));
            }
        }
        draw_rounded(dr, x0, y0, tw, th, tile_radius(rules, G), fill);
        ymid = y0 + th / 2;
    }

    if (text) {
        strncpy(str, text, sizeof(str) - 1);
        str[sizeof(str) - 1] = '\0';
    } else {
        sprintf(str, "%d", tile_number(rules, c));
    }
    nd = (int)strlen(str);
    size = number_size(rules, G, nd, c, f);
    if (size >= 5)
        draw_text(dr, x0 + tw / 2, ymid, FONT_VARIABLE, size,
                  ALIGN_VCENTRE | ALIGN_HCENTRE, numcol, str);
}

/* the colour that the board has between the squares (Threes has no board of its own) */
static int board_colour(int rules) {
    return rules == RULES_THREES ? COL_BACKGROUND : COL_BOARD;
}

/*
 * The empty square i, j with what the preference says of the background of the
 * game: the region of the square (the tile and half of the gap round it) is
 * painted over first.
 */
static void draw_cell_base(drawing *dr, const game_drawstate *ds, const game_ui *ui,
                           int i, int j, int rules)
{
    const struct geom *G = &ds->g;
    int tx = TILE_X(G, i), ty = TILE_Y(G, j);
    int ox = tx - G->g / 2, oy = ty - G->g / 2;
    int base = board_colour(rules);

    draw_rect(dr, ox, oy, G->p, G->py,
              ui->background == BACKGROUND_CHESS && (i + j) % 2 ? COL_SLOT : base);
    if (ui->background == BACKGROUND_LINES) {
        int lw = G->p / 48 > 1 ? G->p / 48 : 1;
        draw_rect(dr, ox + G->p - lw, oy, lw, G->py, COL_TEXT);
        draw_rect(dr, ox, oy + G->py - lw, G->p, lw, COL_TEXT);
    } else if (ui->background == BACKGROUND_SLOTS) {
        draw_rounded(dr, tx, ty, G->tw, G->th, tile_radius(rules, G), COL_SLOT);
    }
}

static void draw_cell(drawing *dr, game_drawstate *ds, const game_ui *ui,
                      const game_state *state, int i, int j)
{
    const struct geom *G = &ds->g;
    unsigned char tile = state->tiles[j * state->w + i];

    draw_cell_base(dr, ds, ui, i, j, state->rules);
    if (tile != 0)
        draw_tile(dr, G, state->rules, TILE_X(G, i), TILE_Y(G, j), tile,
                  state->finished, NULL, 1.0F);
    draw_update(dr, TILE_X(G, i) - G->g / 2, TILE_Y(G, j) - G->g / 2, G->p, G->py);
}

/* Threes: the card that comes next, above the board (blue 1, red 2, white 3, "+" for a bonus card) */
static void draw_next(drawing *dr, game_drawstate *ds, const game_state *state)
{
    const struct geom *G = &ds->g;
    int ch = G->top * 16 / 25, cw = ch * 5 / 7;
    int cx = (G->width - cw) / 2, cy = (G->top - ch) / 2;

    draw_rect(dr, 0, 0, G->width, G->top, COL_BACKGROUND);
    if (!state->finished && state->next > 0 && cw > 2 && ch > 2) {
        int c = state->next >= 4 ? 3 : state->next;
        int edge;
        draw_card(dr, cx, cy, cw, ch, (int)(cw * 0.12F + 0.5F), tile_fill(RULES_THREES, c),
                  tile_edge(c));
        edge = ch * 7 / 100;
        if (edge < 2) edge = 2;
        if (state->next >= 4)
            draw_text(dr, cx + cw / 2, cy + (ch - edge) / 2, FONT_VARIABLE, cw * 3 / 5,
                      ALIGN_VCENTRE | ALIGN_HCENTRE, COL_NUM_BLACK, "+");
    }
    draw_update(dr, 0, 0, G->width, G->top);
}

/*
 * The animation of a move, with the timing of the original games.
 *
 * 2048 (as the page does it): all the tiles slide to where they end up in a
 * third of the time (100 ms of 300, "ease-in-out"), the two tiles that
 * merge on top of each other; then the tile that is made by a merge pops up
 * from nothing over them (0, 1.2, 1) and the tile that turned up grows from
 * nothing (200 ms, "ease").
 *
 * Threes: the tiles of the lines that move all slide one square, and the
 * card that turns up slides in from beyond the edge, all at the same time.
 *
 * from: the state before the move, to: the state after it, t: how far it is
 * (0 to 1; an undo runs it backwards).
 */
#define SLIDE_PART (1.0F / 3.0F)

/* the timing functions of CSS: cubic-bezier(x1, y1, x2, y2) at x (0 to 1) */
static float bezier(float x1, float y1, float x2, float y2, float x) {
    float lo = 0.0F, hi = 1.0F, tt, u;
    int i;

    if (x <= 0.0F) return 0.0F;
    if (x >= 1.0F) return 1.0F;
    tt = x;
    for (i = 0; i < 20; i++) {
        u = 1.0F - tt;
        if (3.0F * u * u * tt * x1 + 3.0F * u * tt * tt * x2 + tt * tt * tt < x)
            lo = tt;
        else
            hi = tt;
        tt = (lo + hi) * 0.5F;
    }
    u = 1.0F - tt;
    return 3.0F * u * u * tt * y1 + 3.0F * u * tt * tt * y2 + tt * tt * tt;
}

#define EASE(x)        bezier(0.25F, 0.1F, 0.25F, 1.0F, (x))
#define EASE_IN_OUT(x) bezier(0.42F, 0.0F, 0.58F, 1.0F, (x))

/* how big a merged tile is u of the way through its pop (0 to 1.2 to 1) */
static float pop_scale(float u) {
    return u < 0.5F ? 1.2F * EASE(u * 2.0F) : 1.2F - 0.2F * EASE(u * 2.0F - 1.0F);
}

/* the length of an animation in seconds: that of the original, half of it, or none */
static float anim_duration(const game_ui *ui, int rules) {
    float full = rules == RULES_THREES ? 0.20F : 0.30F;
    return ui->anim == ANIM_OFF ? 0.0F : ui->anim == ANIM_FAST ? full / 2.0F : full;
}

static void draw_animation(drawing *dr, game_drawstate *ds, const game_ui *ui,
                           const game_state *from, const game_state *to, float t)
{
    const struct geom *G = &ds->g;
    int w = to->w, h = to->h, n = w * h, i, j, pass;
    const int rules = to->rules;
    const bool threes = rules == RULES_THREES;
    struct trace tr;
    unsigned char tmp[MAX_CELLS];
    bool popped[MAX_CELLS];

    memcpy(tmp, from->tiles, n);
    slide_any(rules, tmp, w, h, to->lastdir, NULL, NULL, &tr);

    draw_rect(dr, 0, G->top, G->width, G->height - G->top, COL_BACKGROUND);
    if (!threes)
        draw_rounded(dr, G->m, G->top + G->m, G->bw, G->bh, board_radius(G), COL_BOARD);
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            draw_cell_base(dr, ds, ui, i, j, rules);

    clip(dr, G->m, G->top + G->m, G->bw, G->bh);
    if (threes || t < SLIDE_PART) {
        float s = EASE_IN_OUT(threes ? t : t / SLIDE_PART);
        /* the ones that do not merge, then those that stay and merge, then those that move and merge */
        for (pass = 0; pass < 3; pass++) {
            for (i = 0; i < n; i++) {
                int v = from->tiles[i], k = tr.dst[i], x0, y0, x1, y1;
                if (!v || k < 0) continue;
                if ((!tr.join[i] ? 0 : k == i ? 1 : 2) != pass) continue;
                x0 = TILE_X(G, i % w); y0 = TILE_Y(G, i / w);
                x1 = TILE_X(G, k % w); y1 = TILE_Y(G, k / w);
                draw_tile(dr, G, rules, x0 + (int)floorf((x1 - x0) * s + 0.5F),
                          y0 + (int)floorf((y1 - y0) * s + 0.5F), v, false, NULL, 1.0F);
            }
        }
        if (threes && to->spawn >= 0 && to->tiles[to->spawn]) {
            /* the new card comes in from beyond the edge that the tiles move away from */
            int k = to->spawn, dx = 0, dy = 0;
            switch (to->lastdir) {
              case DIR_L: dx = G->p; break;
              case DIR_R: dx = -G->p; break;
              case DIR_U: dy = G->py; break;
              default:    dy = -G->py; break;
            }
            draw_tile(dr, G, rules,
                      TILE_X(G, k % w) + (int)floorf(dx * (1.0F - s) + 0.5F),
                      TILE_Y(G, k / w) + (int)floorf(dy * (1.0F - s) + 0.5F),
                      to->tiles[k], false, NULL, 1.0F);
        }
    } else {
        float u = (t - SLIDE_PART) / (1.0F - SLIDE_PART);
        for (i = 0; i < n; i++)
            popped[i] = false;
        for (i = 0; i < n; i++)
            if (tr.join[i] && tr.dst[i] >= 0)
                popped[tr.dst[i]] = true;
        /* the tiles at rest; under a merged tile there are still the two that merged */
        for (i = 0; i < n; i++) {
            int v = to->tiles[i];
            if (!v || i == to->spawn) continue;
            draw_tile(dr, G, rules, TILE_X(G, i % w), TILE_Y(G, i / w),
                      popped[i] ? v - 1 : v, false, NULL, 1.0F);
        }
        /* the tile that turned up grows from nothing */
        if (to->spawn >= 0 && to->tiles[to->spawn])
            draw_tile(dr, G, rules, TILE_X(G, to->spawn % w), TILE_Y(G, to->spawn / w),
                      to->tiles[to->spawn], false, NULL, EASE(u));
        /* the merged tiles pop up over all of that */
        for (i = 0; i < n; i++)
            if (popped[i] && to->tiles[i])
                draw_tile(dr, G, rules, TILE_X(G, i % w), TILE_Y(G, i / w),
                          to->tiles[i], false, NULL, pop_scale(u));
    }
    unclip(dr);
    draw_update(dr, 0, G->top, G->width, G->height - G->top);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime) {
    const struct geom *G = &ds->g;
    int i, w = state->w, h = state->h, maxc = 0;
    char statusbuf[256];
    bool all = !ds->started || ds->background != ui->background || ds->animated;
    const game_state *from = NULL, *to = NULL;
    float t = 0.0F, dur = anim_duration(ui, state->rules);

    if (oldstate && dir != 0 && dur > 0.0F) {
        /* the later of the two states says how it came about */
        to = dir > 0 ? state : oldstate;
        from = dir > 0 ? oldstate : state;
        if (to->lastdir < 0 || from->w != to->w || from->h != to->h) {
            from = to = NULL;
        } else {
            t = animtime / dur;
            if (t > 1.0F) t = 1.0F;
            if (dir < 0) t = 1.0F - t;
        }
    }

    if (!ds->started) {
        draw_rect(dr, 0, 0, G->width, G->height, COL_BACKGROUND);
        if (state->rules != RULES_THREES)
            draw_rounded(dr, G->m, G->top + G->m, G->bw, G->bh, board_radius(G), COL_BOARD);
        draw_update(dr, 0, 0, G->width, G->height);
    }

    if (G->top && (!ds->started || ds->next != state->next ||
                   ds->finished != state->finished)) {
        draw_next(dr, ds, state);
        ds->next = state->next;
    }

    if (from) {
        draw_animation(dr, ds, ui, from, to, t);
        ds->animated = true;
        for (i = 0; i < w*h; i++)
            ds->tiles[i] = 255;
    } else {
        for (i = 0; i < w*h; i++) {
            int tl = state->tiles[i];
            if (all || (state->finished != ds->finished) || (tl != ds->tiles[i])) {
                draw_cell(dr, ds, ui, state, i % w, i / w);
                ds->tiles[i] = tl;
            }
        }
        ds->animated = false;
    }

    ds->started = true;
    ds->finished = state->finished;
    ds->background = ui->background;
    for (i = 0; i < w*h; i++)
        if (state->tiles[i] > maxc) maxc = state->tiles[i];
    if (state->rules == RULES_THREES)
        sprintf(statusbuf, "%sThrees Score %d, biggest tile %d",
                state->finished ? "NO MORE MOVES " : "",
                state->score, maxc ? threes_value(maxc) : 0);
    else if (state->goal == GOAL_ENDLESS)
        sprintf(statusbuf, "%sEndless Score %d, biggest tile %d",
                state->finished ? "NO MORE MOVES " : "",
                state->score, maxc ? POWER[maxc] : 0);
    else
        sprintf(statusbuf,
                "%s%sGoal %s Score %d",
                state->won  ? "WON! " : "",
                state->finished ? "NO MORE MOVES " : "",
                binary_goalnames[state->goal], state->score);
    status_bar(dr, statusbuf);
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui) {
    /* the later of the two states says how it came about */
    const game_state *later = dir > 0 ? newstate : oldstate;
    if (later->lastdir < 0)
        return 0.0F;
    return anim_duration(ui, later->rules);
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate, int dir, game_ui *ui) {
    return 0.0F;
}

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h) {
}

static int game_status(const game_state *state) {
    /* the game goes on after the goal has been reached, until it is over */
    return state->finished ? (state->won ? +1 : -1) : 0;
}

#ifdef COMBINED
#define thegame binary
#endif

const struct game thegame = {
    "Binary", "games.binary", "binary",
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
    false, NULL, /* solve */
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
