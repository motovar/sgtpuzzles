/*
 * binary.c: "Binary", an implementation of the sliding tile game '2048'.
 *
 * Written by Steffen Bauer for his PocketPuzzles collection (MIT licence)
 * and brought over to this app.
 *
 * Tiles carry powers of two.  A move slides all the tiles as far as they
 * go in one of the four directions; two tiles with the same number that
 * run into each other merge into one with the next power of two, and a new
 * tile (a 2, now and then a 4) turns up at a random empty square.  The game
 * is won when a tile with the goal number appears (the default goal is
 * 2048; the game can be played on after that) and over when no move is left.
 * The random numbers come from a random state that is part of every game
 * state, so a game (and every undo and redo) is the same every time.
 *
 * What is new compared to the original:
 *
 *  - Tiles merge once per move, as in 2048: the tile that is made by a merge
 *    does not merge again in the same move.  The original merged on in a
 *    chain, so that a row of 2 2 4 8 became a 16 and not 4 4 8.
 *  - "Show next step" plays the move that a look-ahead (expectimax) search
 *    finds best: it tries the four moves, the new tiles that may turn up
 *    after each of them and the moves after that, and rates the boards by
 *    the number of empty squares, the possible merges and how orderly the
 *    rows and columns are.  The move is an ordinary one, so it can be undone.
 *  - The cursor keys and the arrow buttons move the tiles, too.  The swipe
 *    has to be as long as a part of a tile, not a fixed number of pixels, so
 *    it is the same on every screen, and the "tap" method works on the
 *    triangles of the board that the pressed point is in.
 *  - Tiles are coloured by their numbers.
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
#define TILE_SIZE (ds->tilesize)
#define BORDER    (TILE_SIZE / 2)
#define HIGHLIGHT_WIDTH (TILE_SIZE / 10)
#define COORD(x)  ( (x) * TILE_SIZE + BORDER )

#define MAX_SIZE 9
#define MAXPOWER 15                    /* the biggest tile is 2^15 = 32768 */
#define RS_DIGITS 122                  /* the length of an encoded random state */

enum {
    COL_BACKGROUND,
    COL_BOARD,
    COL_TEXT,
    COL_HIGHLIGHT,
    COL_LOWLIGHT,
    COL_SLOT,
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
    NCOLOURS
};

/* the colour of a tile that carries 2^e (e = 1 ... MAXPOWER) */
#define COL_TILE(e) (COL_TILE1 + (e) - 1)

enum {
    PREF_GAME_BACKGROUND,
    PREF_INPUT_METHOD,
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

struct game_params {
    int w,h;
    int goal;
    int mode;
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
    4096, 8192, 16384, 32768
};

/* the exponent that a tile has to reach for a goal */
#define GOAL_EXP(goal) ((goal) + 5)

struct game_state {
    int w, h;
    int goal;
    int mode;
    int score;
    bool won;
    bool finished;
    unsigned char *tiles;              /* exponents: 0 = empty, 1 = 2, 2 = 4 ... */
    random_state *rs;
};

static game_params *default_params(void) {
    game_params *ret = snew(game_params);

    ret->w = 4;
    ret->h = 4;
    ret->goal = GOAL_2048;
    ret->mode = MODE_RECT;

    return ret;
}

static const struct game_params binary_presets[] = {
    { 4,  4, GOAL_512, MODE_RECT},
    { 4,  4, GOAL_2048, MODE_RECT},
    { 5,  5, GOAL_2048, MODE_RECT},
    { 6,  6, GOAL_2048, MODE_RECT},
};

static bool game_fetch_preset(int i, char **name, game_params **params) {
    game_params *ret;
    char str[80];

    if (i < 0 || i >= (int)lenof(binary_presets))
        return false;

    ret = snew(game_params);
    *ret = binary_presets[i];

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
}

static char *encode_params(const game_params *params, bool full) {
    char data[256];
    sprintf(data, "%dx%dg%cm%c", params->w, params->h,
            binary_goalchars[params->goal], binary_modechars[params->mode]);
    return dupstr(data);
}

static config_item *game_configure(const game_params *params) {
    config_item *ret;
    char buf[80];

    ret = snewn(4, config_item);

    ret[0].name = "Width";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[0].u.string.sval = dupstr(buf);

    ret[1].name = "Height";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->h);
    ret[1].u.string.sval = dupstr(buf);

    ret[2].name = "Goal";
    ret[2].type = C_CHOICES;
    ret[2].u.choices.choicenames = GOALCONFIG;
    ret[2].u.choices.selected = params->goal;

    ret[3].name = NULL;
    ret[3].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg) {
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].u.string.sval);
    ret->h = atoi(cfg[1].u.string.sval);
    ret->goal = cfg[2].u.choices.selected;
    ret->mode = MODE_RECT;

    return ret;
}

static const char *validate_params(const game_params *params, bool full) {
    if (params->w < 2 || params->h < 2)
        return "Width and height must both be at least two";
    if (params->w > MAX_SIZE || params->h > MAX_SIZE)
        return "Width and height must both be at most 9";
    if (params->goal < 0 || params->goal >= GOALCOUNT)
        return "Invalid goal number";
    if (params->mode == MODE_HEX)
        return "Hexagonal grid not implemented yet";
    if (params->mode != MODE_RECT)
        return "Invalid grid type";
    return NULL;
}

/* ----------------------------------------------------------------------
 * The rules of the game, on bare boards of exponents.
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
 * Slide the tiles of a board as far as they go in direction d.  Two tiles
 * with the same number that meet merge into one with the next power of two,
 * once per move and tile.  Adds the numbers of the new tiles to *score (if
 * not NULL) and says whether anything on the board changed at all.
 */
static bool slide_board(unsigned char *tiles, int w, int h, int d, int *score) {
    bool horizontal = (d == DIR_L || d == DIR_R);
    int nlines = horizontal ? h : w;
    int len = horizontal ? w : h;
    bool changed = false;
    int k, i;

    for (k = 0; k < nlines; k++) {
        unsigned char out[MAX_SIZE];
        bool merged[MAX_SIZE];
        int pos[MAX_SIZE];
        int n = 0;

        /* where the i'th tile from the side that the tiles slide to is */
        for (i = 0; i < len; i++) {
            switch (d) {
              case DIR_L: pos[i] = k * w + i; break;
              case DIR_R: pos[i] = k * w + (w - 1 - i); break;
              case DIR_U: pos[i] = i * w + k; break;
              default:    pos[i] = (h - 1 - i) * w + k; break;
            }
        }

        for (i = 0; i < len; i++) {
            unsigned char v = tiles[pos[i]];
            if (!v) continue;
            if (n > 0 && !merged[n-1] && out[n-1] == v && v < MAXPOWER) {
                out[n-1] = v + 1;
                merged[n-1] = true;
                if (score) *score += POWER[v + 1];
            } else {
                out[n] = v;
                merged[n] = false;
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

static bool move_possible(const unsigned char *tiles, int w, int h, int d) {
    unsigned char copy[MAX_SIZE * MAX_SIZE];
    memcpy(copy, tiles, w * h);
    return slide_board(copy, w, h, d, NULL);
}

static bool moves_possible(const unsigned char *tiles, int w, int h) {
    int d;
    for (d = 0; d < NDIRS; d++)
        if (move_possible(tiles, w, h, d)) return true;
    return false;
}

/* a tile with 2 (mostly) or 4 at a random empty square; false if the board is full */
static bool add_new_number(random_state *rs, unsigned char *grid, int size) {
    int freecells[MAX_SIZE * MAX_SIZE];
    int i, c = 0;

    for (i = 0; i < size; i++)
        if (grid[i] == 0)
            freecells[c++] = i;
    if (c == 0)
        return false;
    i = freecells[random_upto(rs, c)];
    grid[i] = random_upto(rs, 10) == 0 ? 2 : 1;
    return true;
}

/* ----------------------------------------------------------------------
 * Game descriptions: the tiles at the start (runs of empty squares as
 * a ... z, tiles as 1 ... 9 and A ... F), a comma and the random state.
 */

static char *new_game_desc(const game_params *params, random_state *rs,
                           char **aux, bool interactive) {
    int w = params->w, h = params->h;
    char *p;
    char *desc;
    int run, i;
    unsigned char grid[MAX_SIZE * MAX_SIZE];
    char *rsdesc;

    memset(grid, 0, sizeof(grid));

    add_new_number(rs, grid, w*h);
    add_new_number(rs, grid, w*h);

    rsdesc = random_state_encode(rs);

    desc = snewn(w*h*2 + strlen(rsdesc) + 2, char);
    p = desc;

    run = 0;
    for(i = 0; i < w*h; i++) {
        if (grid[i] == 0)
            run++;
        else {
            while(run >= 26) {
                *p++ = 'z';
                run -= 26;
            }
            if(run)
                *p++ = 'a' + run-1;
            run = 0;
            if (grid[i] < 10)
                *p++ = '0' + grid[i];
            else
                *p++ = 'A' + (grid[i]-10);
        }
    }
    while(run >= 26) {
        *p++ = 'z';
        run -= 26;
    }
    if(run)
        *p++ = 'a' + run-1;
    *p++ = ',';
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

    while (*p != ',') {
        if(*p >= 'a' && *p <= 'z')
            i += ((*p) - 'a') + 1;
        else if (*p >= '1' && *p <= '9')
            i++;
        else if (*p >= 'A' && *p <= 'F')
            i++;
        else if (!*p)
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

    state->w = params->w;
    state->h = params->h;
    state->goal = params->goal;
    state->mode = params->mode;
    state->score = 0;
    state->won = state->finished = false;

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
    state->rs = random_state_decode(p);

    for (i = 0; i < wh; i++)
        if (state->tiles[i] >= GOAL_EXP(state->goal))
            state->won = true;
    if (!moves_possible(state->tiles, state->w, state->h))
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
    ret->score = state->score;
    ret->won = state->won;
    ret->finished = state->finished;
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
    unsigned char copy[MAX_SIZE * MAX_SIZE];
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
    unsigned char copy[MAX_SIZE * MAX_SIZE];

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
        if (move_possible(board, w, h, d)) { ndirs++; chosen = d; }
    if (ndirs <= 1)
        return chosen;                 /* no choice */

    chosen = -1;
    for (depth = 1; depth <= HINT_MAX_DEPTH; depth++) {
        int best = -1;
        double bestv = 0;
        unsigned char copy[MAX_SIZE * MAX_SIZE];

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

static char *hint_game(const game_state *orig, const game_state *curr,
                       const char **error) {
    int d;
    char buf[2];

    if (curr->finished) {
        *error = "No more moves";
        return NULL;
    }
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
    int x, y;                          /* where the press that is going on started, or -1 */
};

static game_ui *new_ui(const game_state *state) {
    game_ui *ui = snew(game_ui);
    ui->background = BACKGROUND_SLOTS;
    ui->inputtype = INPUT_SWIPE;
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

    ret[N_PREF_ITEMS].name = NULL;
    ret[N_PREF_ITEMS].type = C_END;

    return ret;
}

static void set_prefs(game_ui *ui, const config_item *cfg)
{
    ui->background = cfg[PREF_GAME_BACKGROUND].u.choices.selected;
    ui->inputtype = cfg[PREF_INPUT_METHOD].u.choices.selected;
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

struct game_drawstate {
    bool started;
    bool finished;
    int background;
    int tilesize;
    int w, h;
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
            /* the four triangles of the picture */
            xr = 2*BORDER+w*TILE_SIZE;
            yr = 2*BORDER+h*TILE_SIZE;
            c1 = xr*y-yr*x;
            c2 = xr*(y-yr)+yr*x;
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
    if (!move_possible(state->tiles, w, h, d))
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
    int w = state->w, h = state->h, d, score = 0;

    if (state->finished)
        return NULL;
    if (!move[0] || move[1])
        return NULL;
    d = dir_from_char(move[0]);
    if (d < 0)
        return NULL;

    ret = dup_game(state);
    if (!slide_board(ret->tiles, w, h, d, &score)) {
        free_game(ret);
        return NULL;
    }
    ret->score += score;
    add_new_number(ret->rs, ret->tiles, w*h);
    {
        int i;
        for (i = 0; i < w*h; i++) {
            if (ret->tiles[i] >= GOAL_EXP(ret->goal)) ret->won = true;
            if (ret->tiles[i] >= MAXPOWER) ret->finished = true;
        }
    }
    if (!moves_possible(ret->tiles, w, h)) ret->finished = true;
    return ret;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_compute_size(const game_params *params, int tilesize,
                              const game_ui *ui, int *x, int *y) {
    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    struct { int tilesize; } ads, *ds = &ads;
    ads.tilesize = tilesize;

    *x = TILE_SIZE * params->w + 2 * BORDER;
    *y = TILE_SIZE * params->h + 2 * BORDER;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize) {
    ds->tilesize = tilesize;
}

static void set_rgb(float *ret, int colour, int rgb) {
    ret[colour * 3 + 0] = ((rgb >> 16) & 0xff) / 255.0F;
    ret[colour * 3 + 1] = ((rgb >> 8) & 0xff) / 255.0F;
    ret[colour * 3 + 2] = (rgb & 0xff) / 255.0F;
}

static float *game_colours(frontend *fe, int *ncolours) {
    float *ret = snewn(3 * NCOLOURS, float);
    /* the tiles, from 2 to 32768 */
    static const int tile_rgb[MAXPOWER] = {
        0xeee4da, 0xede0c8, 0xf2b179, 0xf59563, 0xf67c5f, 0xf65e3b, 0xedcf72, 0xedcc61,
        0xedc850, 0xedc53f, 0xedc22e, 0x5fba7d, 0x3fa7b5, 0x4a86d8, 0x7a5ad8
    };
    int i;

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);
    game_mkhighlight(fe, ret, COL_BACKGROUND, COL_HIGHLIGHT, COL_LOWLIGHT);
    for (i = 0; i < 3; i++) {
        ret[COL_BOARD * 3 + i] = ret[COL_BACKGROUND * 3 + i] * 0.7F;
        ret[COL_SLOT  * 3 + i] = ret[COL_BACKGROUND * 3 + i] * 0.88F;
        ret[COL_TEXT  * 3 + i] = 0.0F;
    }
    set_rgb(ret, COL_NUM_DARK, 0x776e65);
    set_rgb(ret, COL_NUM_LIGHT, 0xf9f6f2);
    for (i = 1; i <= MAXPOWER; i++)
        set_rgb(ret, COL_TILE(i), tile_rgb[i - 1]);
    *ncolours = NCOLOURS;

    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state) {
    struct game_drawstate *ds = snew(struct game_drawstate);
    int i;

    ds->started = false;
    ds->finished = false;
    ds->background = -1;
    ds->tilesize = 0;
    ds->w = state->w;
    ds->h = state->h;
    ds->tiles = snewn(ds->w*ds->h, unsigned char);
    for (i = 0; i < ds->w*ds->h; i++)
        ds->tiles[i] = 0;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds) {
    sfree(ds->tiles);
    sfree(ds);
}

/* the number of digits of 2^e */
static int digits_of(int e) {
    int v = POWER[e], n = 0;
    while (v) { n++; v /= 10; }
    return n;
}

/* a square with its corners cut off */
static void draw_chamfered(drawing *dr, int x0, int y0, int s, int cut, int colour)
{
    int coords[16];

    coords[0] = x0 + cut;     coords[1] = y0;
    coords[2] = x0 + s - cut; coords[3] = y0;
    coords[4] = x0 + s;       coords[5] = y0 + cut;
    coords[6] = x0 + s;       coords[7] = y0 + s - cut;
    coords[8] = x0 + s - cut; coords[9] = y0 + s;
    coords[10] = x0 + cut;    coords[11] = y0 + s;
    coords[12] = x0;          coords[13] = y0 + s - cut;
    coords[14] = x0;          coords[15] = y0 + cut;
    draw_polygon(dr, coords, 8, colour, colour);
}

static void draw_cell(drawing *dr, game_drawstate *ds, const game_ui *ui,
                      const game_state *state, int x, int y,
                      unsigned char tile, int colour)
{
    int gap = TILE_SIZE / 24 > 1 ? TILE_SIZE / 24 : 1;
    int cut = TILE_SIZE / 16 > 1 ? TILE_SIZE / 16 : 1;
    int x0 = x + gap, y0 = y + gap, s = TILE_SIZE - 2 * gap - 1;

    /* the square as it is when it is empty */
    draw_rect(dr, x, y, TILE_SIZE, TILE_SIZE,
              ui->background == BACKGROUND_CHESS ? colour : COL_BACKGROUND);
    if (ui->background == BACKGROUND_LINES) {
        draw_rect(dr, x+TILE_SIZE-2, y, 2, TILE_SIZE, COL_TEXT);
        draw_rect(dr, x, y+TILE_SIZE-2, TILE_SIZE, 2, COL_TEXT);
    } else if (ui->background == BACKGROUND_SLOTS) {
        draw_chamfered(dr, x0, y0, s, cut, COL_SLOT);
    }

    if (tile != 0) {
        int fill, numcol, textsize, nd;
        char str[40];

        fill = state->finished ? COL_BOARD : COL_TILE(tile);
        numcol = state->finished ? COL_TEXT : tile <= 2 ? COL_NUM_DARK : COL_NUM_LIGHT;
        draw_chamfered(dr, x0, y0, s, cut, fill);

        nd = digits_of(tile);
        textsize = nd <= 1 ? TILE_SIZE * 11 / 20 :
                   nd == 2 ? TILE_SIZE / 2 :
                   nd == 3 ? TILE_SIZE * 2 / 5 :
                   nd == 4 ? TILE_SIZE / 3 : TILE_SIZE * 7 / 25;
        sprintf(str, "%d", POWER[tile]);
        draw_text(dr, x + TILE_SIZE/2, y + TILE_SIZE/2,
                  FONT_VARIABLE, textsize, ALIGN_VCENTRE | ALIGN_HCENTRE,
                  numcol, str);
    }
    draw_update(dr, x, y, TILE_SIZE, TILE_SIZE);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime) {
    int i, w = state->w, h = state->h;
    char statusbuf[256];
    bool all = !ds->started || ds->background != ui->background;

    if (!ds->started) {
        int coords[10];

        draw_rect(dr, 0, 0, 2 * BORDER + w * TILE_SIZE, 2 * BORDER + h * TILE_SIZE,
                  COL_BACKGROUND);

        /*
         * Recessed area containing the whole puzzle.
         */
        coords[0] = COORD(w) + HIGHLIGHT_WIDTH - 1;
        coords[1] = COORD(h) + HIGHLIGHT_WIDTH - 1;
        coords[2] = COORD(w) + HIGHLIGHT_WIDTH - 1;
        coords[3] = COORD(0) - HIGHLIGHT_WIDTH;
        coords[4] = coords[2] - TILE_SIZE;
        coords[5] = coords[3] + TILE_SIZE;
        coords[8] = COORD(0) - HIGHLIGHT_WIDTH;
        coords[9] = COORD(h) + HIGHLIGHT_WIDTH - 1;
        coords[6] = coords[8] + TILE_SIZE;
        coords[7] = coords[9] - TILE_SIZE;
        draw_polygon(dr, coords, 5, COL_HIGHLIGHT, COL_HIGHLIGHT);

        coords[1] = COORD(0) - HIGHLIGHT_WIDTH;
        coords[0] = COORD(0) - HIGHLIGHT_WIDTH;
        draw_polygon(dr, coords, 5, COL_LOWLIGHT, COL_LOWLIGHT);
    }

    for (i = 0; i < w*h; i++) {
        int t, x, y;
        t = state->tiles[i];
        if (all || (state->finished != ds->finished) || (t != ds->tiles[i])) {
            x = COORD(i%w);
            y = COORD(i/w);
            draw_cell(dr, ds, ui, state, x, y, t, ((i/w)+(i%w))%2 ? COL_BACKGROUND : COL_BOARD);
            ds->tiles[i] = t;
        }
    }

    ds->started = true;
    ds->finished = state->finished;
    ds->background = ui->background;
    sprintf(statusbuf,
            "%s%sGoal %s Score %d",
                          state->won  ? "WON! " : "",
                          state->finished ? "NO MORE MOVES " : "",
                          binary_goalnames[state->goal], state->score);
    status_bar(dr, statusbuf);
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui) {
    return 0.0F;
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
