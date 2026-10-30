/*
 * sokoban.c: An implementation of the well-known Sokoban barrel-
 * pushing game. Random generation is too simplistic to be
 * credible, but the rest of the gameplay works well enough to use
 * it with hand-written level descriptions.
 */

/*
 * TODO:
 * 
 *  - I think it would be better to ditch the `prev' array, and
 *    instead make the `dist' array strictly monotonic (by having
 *    each distance be something like I*A+S, where A is the grid
 *    area, I the number of INITIAL squares trampled on, and S the
 *    number of harmless spaces moved through). This would permit
 *    the path-tracing when a pull is actually made to choose
 *    randomly from all the possible shortest routes, which would
 *    be superior in terms of eliminating directional bias.
 *     + So when tracing the path back to the current px,py, we
 * 	 look at all four adjacent squares, find the minimum
 * 	 distance, check that it's _strictly smaller_ than that of
 * 	 the current square, and restrict our choice to precisely
 * 	 those squares with that minimum distance.
 *     + The other place `prev' is currently used is in the check
 * 	 for consistency of a pull. We would have to replace the
 * 	 check for whether prev[ny*w+nx]==oy*w+ox with a check that
 * 	 made sure there was at least one adjacent square with a
 * 	 smaller distance which _wasn't_ oy*w+ox. Then when we did
 * 	 the path-tracing we'd also have to take this special case
 * 	 into account.
 * 
 *  - More discriminating choice of pull. (Snigger.)
 *     + favour putting targets in clumps
 *     + try to shoot for a reasonably consistent number of barrels
 * 	 (adjust willingness to generate a new barrel depending on
 * 	 how many are already present)
 *     + adjust willingness to break new ground depending on how
 * 	 much is already broken
 * 
 *  - generation time parameters:
 *     + enable NetHack mode (and find a better place for the hole)
 *     + decide how many of the remaining Is should be walls
 * 
 *  - at the end of generation, randomly position the starting
 *    player coordinates, probably by (somehow) reusing the same
 *    bfs currently inside the loop.
 * 
 *  - possible backtracking?
 * 
 *  - IWBNI we could spot completely unreachable bits of level at
 *    the outside, and not bother drawing grid lines for them. The
 *    NH levels currently look a bit weird with grid lines on the
 *    outside of the boundary.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <time.h>
#ifdef NO_TGMATH_H
#  include <math.h>
#else
#  include <tgmath.h>
#endif

#include "puzzles.h"

/*
 * Various subsets of these constants are used during game
 * generation, game play, game IDs and the game_drawstate.
 */
#define INITIAL      'i'               /* used only in game generation */
#define SPACE        's'
#define WALL         'w'
#define PIT          'p'
#define DEEP_PIT     'd'
#define TARGET       't'
#define BARREL       'b'
#define BARRELTARGET 'f'               /* target is 'f'illed */
#define PLAYER       'u'               /* yo'u'; used in game IDs */
#define PLAYERTARGET 'v'               /* bad letter: v is to u as t is to s */
#define INVALID      '!'               /* used in drawstate to force redraw */
/*
 * We also support the use of any capital letter as a barrel, which
 * will be displayed with that letter as a label. (This facilitates
 * people distributing annotated game IDs for particular Sokoban
 * levels, so they can accompany them with verbal instructions
 * about pushing particular barrels in particular ways.) Therefore,
 * to find out whether something is a barrel, we need a test
 * function which does a bit more than just comparing to BARREL.
 * 
 * When resting on target squares, capital-letter barrels are
 * replaced with their control-character value (A -> ^A).
 */
#define IS_PLAYER(c) ( (c)==PLAYER || (c)==PLAYERTARGET )
#define IS_BARREL(c) ( (c)==BARREL || (c)==BARRELTARGET || \
                       ((c)>='A' && (c)<='Z') || ((c)>=1 && (c)<=26) )
#define IS_ON_TARGET(c) ( (c)==TARGET || (c)==BARRELTARGET || \
                          (c)==PLAYERTARGET || ((c)>=1 && (c)<=26) )
#define TARGETISE(b) ( (b)==BARREL ? BARRELTARGET : (b)-('A'-1) )
#define DETARGETISE(b) ( (b)==BARRELTARGET ? BARREL : (b)+('A'-1) )
#define BARREL_LABEL(b) ( (b)>='A'&&(b)<='Z' ? (b) : \
                          (b)>=1 && (b)<=26 ? (b)+('A'-1) : 0 )

#define DX(d) (d == 0 ? -1 : d == 2 ? +1 : 0)
#define DY(d) (d == 1 ? -1 : d == 3 ? +1 : 0)

#define FLASH_LENGTH 0.3F

enum {
    COL_BACKGROUND,
    COL_TARGET,
    COL_PIT,
    COL_DEEP_PIT,
    COL_BARREL,
    COL_PLAYER,
    COL_TEXT,
    COL_GRID,
    COL_OUTLINE,
    COL_HIGHLIGHT,
    COL_LOWLIGHT,
    COL_WALL,
    /* "Classic" graphics theme */
    COL_C_FLOOR,
    COL_C_BRICK,
    COL_C_MORTAR,
    COL_C_CRATE,
    COL_C_CRATE_EDGE,
    COL_C_CRATE_OK,
    COL_C_GOAL,
    COL_C_SUIT,
    COL_C_SKIN,
    COL_T_PIT,                         /* colours shared by the non-original themes */
    COL_T_DEEP_PIT,
    COL_T_OUTLINE,
    COL_T_TEXT,
    COL_T_FLASH,
    NCOLOURS
};

struct game_params {
    int w, h;
    int gen;        /* 0 original, 1 boulders, 2 MCTS, 3 YASGen */
    /* Boulders generator */
    int bm_count, bm_time, bm_nodes;
    /* MCTS generator */
    int mc_time, mc_keep, mc_pick;
    /* YASGen generator: the settings of the generator window of YASGen 1.50 */
    int yg_goals;   /* maximum number of boxes (0 = 4, as in YASGen; a share per tile on big boards) */
    int yg_walls;   /* interior walls, percent of the interior */
    int yg_time;    /* time limit in seconds (0 = auto) */
    int yg_mode;    /* 0 backward search (pull boxes from the goals), 1 forward */
    int yg_conn;    /* 0 goals at random, 1 connected goals */
    int yg_fit;     /* 0 fitness is the number of pushes, 1 box lines and box changes */
    int yg_rand;    /* trivial opening pushes are discarded (1) */
    int yg_tail;    /* tail pushes threshold (1..4; 0 = not used) */
    /* Rooms generator */
    int rm_boxes, rm_limit, rm_attempts;
    /* not part of the game ID: time for one tile of a big board, in ms */
    int tile_ms;
    /*
     * FIXME: a parameter involving degree of filling in?
     */
};

struct game_state {
    game_params p;
    unsigned char *grid;
    int px, py;
    bool completed;
};

static void sokoban_gen_defaults(game_params *p)
{
    p->bm_count = 0; p->bm_time = 0; p->bm_nodes = 500;
    p->mc_time = 0; p->mc_keep = 1; p->mc_pick = 1;
    p->yg_goals = 0; p->yg_walls = 40; p->yg_time = 0;
    p->yg_mode = 0; p->yg_conn = 0; p->yg_fit = 0;
    p->yg_rand = 1; p->yg_tail = 1;
    p->rm_boxes = 0; p->rm_limit = 100; p->rm_attempts = 20;
    p->tile_ms = 0;
}

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->w = 12;
    ret->h = 10;
    ret->gen = 0;
    sokoban_gen_defaults(ret);

    return ret;
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

/* only the size matters here; game_fetch_preset() adds the defaults */
static const struct game_params sokoban_presets[] = {
    { 12, 10, 0 },
    { 16, 12, 0 },
    { 20, 16, 0 },
};

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    game_params p, *ret;
    char *retname;
    char namebuf[80];

    if (i < 0 || i >= lenof(sokoban_presets))
	return false;

    p = sokoban_presets[i];
    p.gen = 0;
    sokoban_gen_defaults(&p);
    ret = dup_params(&p);
    sprintf(namebuf, "%dx%d", ret->w, ret->h);
    retname = dupstr(namebuf);

    *params = ret;
    *name = retname;
    return true;
}

static void decode_params(game_params *params, char const *string)
{
    params->w = params->h = atoi(string);
    while (*string && isdigit((unsigned char)*string)) string++;
    if (*string == 'x') {
        string++;
        params->h = atoi(string);
        while (*string && isdigit((unsigned char)*string)) string++;
    }
    params->gen = 0;
    sokoban_gen_defaults(params);
    if (*string == 'g') {
        string++;
        params->gen = atoi(string);
        if (params->gen < 0 || params->gen > 4) params->gen = 0;
        while (*string && isdigit((unsigned char)*string)) string++;
        while (*string) {
            char key = *string++;
            int v = atoi(string);
            while (*string && isdigit((unsigned char)*string)) string++;
            switch (key) {
              case 'c': params->bm_count = v; break;
              /* ('a', the attempts of an earlier version, is ignored) */
              case 'z': params->bm_time = v; break;
              case 'n': params->bm_nodes = v; break;
              case 't': params->mc_time = v; break;
              case 'k': params->mc_keep = v; break;
              case 'p': params->mc_pick = v; break;
              case 'o': params->yg_goals = v; break;
              case 'w': params->yg_walls = v; break;
              case 'u': params->yg_time = v; break;
              case 'm': params->yg_mode = v; break;
              case 'q': params->yg_conn = v; break;
              case 'f': params->yg_fit = v; break;
              case 'r': params->yg_rand = v; break;
              case 'y': params->yg_tail = v; break;
              /* (the keys 'l' and 'e' of an earlier YASGen are ignored) */
              case 'b': params->rm_boxes = v; break;
              case 's': params->rm_limit = v; break;
              case 'x': params->rm_attempts = v; break;
              default: break;
            }
        }
    }
}

static char *encode_params(const game_params *params, bool full)
{
    char data[512];

    sprintf(data, "%dx%d", params->w, params->h);
    if (params->gen) {
        game_params d;
        char *p;
        sokoban_gen_defaults(&d);
        sprintf(data + strlen(data), "g%d", params->gen);
        p = data + strlen(data);
        if (params->bm_count != d.bm_count) p += sprintf(p, "c%d", params->bm_count);
        if (params->bm_time != d.bm_time) p += sprintf(p, "z%d", params->bm_time);
        if (params->bm_nodes != d.bm_nodes) p += sprintf(p, "n%d", params->bm_nodes);
        if (params->mc_time != d.mc_time) p += sprintf(p, "t%d", params->mc_time);
        if (params->mc_keep != d.mc_keep) p += sprintf(p, "k%d", params->mc_keep);
        if (params->mc_pick != d.mc_pick) p += sprintf(p, "p%d", params->mc_pick);
        if (params->yg_goals != d.yg_goals) p += sprintf(p, "o%d", params->yg_goals);
        if (params->yg_walls != d.yg_walls) p += sprintf(p, "w%d", params->yg_walls);
        if (params->yg_time != d.yg_time) p += sprintf(p, "u%d", params->yg_time);
        if (params->yg_mode != d.yg_mode) p += sprintf(p, "m%d", params->yg_mode);
        if (params->yg_conn != d.yg_conn) p += sprintf(p, "q%d", params->yg_conn);
        if (params->yg_fit != d.yg_fit) p += sprintf(p, "f%d", params->yg_fit);
        if (params->yg_rand != d.yg_rand) p += sprintf(p, "r%d", params->yg_rand);
        if (params->yg_tail != d.yg_tail) p += sprintf(p, "y%d", params->yg_tail);
        if (params->rm_boxes != d.rm_boxes) p += sprintf(p, "b%d", params->rm_boxes);
        if (params->rm_limit != d.rm_limit) p += sprintf(p, "s%d", params->rm_limit);
        if (params->rm_attempts != d.rm_attempts) p += sprintf(p, "x%d", params->rm_attempts);
    }

    return dupstr(data);
}

/*
 * The custom game dialog lists the settings of all the level generators,
 * in this order. The front end shows only the rows of the generator that is
 * selected (it recognises them by the generator's name in front of the
 * colon in the row's name), and changes which rows are shown as soon as
 * another generator is picked. custom_params() reads them back from the
 * same positions.
 */
enum {
    CFG_W, CFG_H, CFG_GEN,
    CFG_BM,                            /* Boulders: count, attempts, nodes */
    CFG_MC = CFG_BM + 3,               /* MCTS: time, keep, pick */
    CFG_YG = CFG_MC + 3,               /* YASGen: direction, boxes, walls, time, level type, fitness, trivial pushes, tail pushes */
    CFG_RM = CFG_YG + 8,               /* Rooms: boxes, limit, placements */
    CFG_COUNT = CFG_RM + 3
};

static config_item *game_configure(const game_params *params)
{
    config_item *ret;
    char buf[80];
    int n = 0;

    ret = snewn(CFG_COUNT + 1, config_item);

    ret[n].name = "Width";
    ret[n].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[n].u.string.sval = dupstr(buf);
    n++;

    ret[n].name = "Height";
    ret[n].type = C_STRING;
    sprintf(buf, "%d", params->h);
    ret[n].u.string.sval = dupstr(buf);
    n++;

    ret[n].name = "Level generator";
    ret[n].type = C_CHOICES;
    ret[n].u.choices.choicenames = ":Original:Boulders (NetHack style):MCTS:YASGen:Rooms (Taylor-Parberry)";
    ret[n].u.choices.selected = params->gen;
    n++;

#define STR_ITEM(NAME, VAL) do { \
        ret[n].name = NAME; ret[n].type = C_STRING; \
        sprintf(buf, "%d", VAL); ret[n].u.string.sval = dupstr(buf); n++; } while (0)
    assert(n == CFG_BM);
    STR_ITEM("Boulders: number of boulders (0 = 4, as in the original)", params->bm_count);
    STR_ITEM("Boulders: search time in seconds (0 = 5, as in the original)", params->bm_time);
    STR_ITEM("Boulders: search nodes per attempt", params->bm_nodes);
    assert(n == CFG_MC);
    STR_ITEM("MCTS: search time in seconds, in total (0 = auto)", params->mc_time);
    STR_ITEM("MCTS: levels to keep", params->mc_keep);
    STR_ITEM("MCTS: level to use (1 = hardest ... levels to keep = easiest)", params->mc_pick);
    assert(n == CFG_YG);
    ret[n].name = "YASGen: search direction";
    ret[n].type = C_CHOICES;
    ret[n].u.choices.choicenames = ":Backward (pull boxes away from the goals):Forward (push boxes to the goals)";
    ret[n].u.choices.selected = params->yg_mode;
    n++;
    STR_ITEM("YASGen: maximum number of boxes in total (0 = 4 per tile)", params->yg_goals);
    STR_ITEM("YASGen: interior walls in percent", params->yg_walls);
    STR_ITEM("YASGen: time limit in seconds (0 = auto)", params->yg_time);
    ret[n].name = "YASGen: level type";
    ret[n].type = C_CHOICES;
    ret[n].u.choices.choicenames = ":Goals at random:Connected goals";
    ret[n].u.choices.selected = params->yg_conn;
    n++;
    ret[n].name = "YASGen: fitness";
    ret[n].type = C_CHOICES;
    ret[n].u.choices.choicenames = ":Number of pushes:Box lines and box changes";
    ret[n].u.choices.selected = params->yg_fit;
    n++;
    ret[n].name = "YASGen: discard trivial opening pushes";
    ret[n].type = C_BOOLEAN;
    ret[n].u.boolean.bval = params->yg_rand != 0;
    n++;
    STR_ITEM("YASGen: tail pushes threshold (0 = not used)", params->yg_tail);
    assert(n == CFG_RM);
    STR_ITEM("Rooms: number of boxes in total (0 = auto)", params->rm_boxes);
    STR_ITEM("Rooms: search limit (thousand states)", params->rm_limit);
    STR_ITEM("Rooms: goal placements to try (the original: until one works)", params->rm_attempts);
#undef STR_ITEM
    assert(n == CFG_COUNT);

    ret[n].name = NULL;
    ret[n].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[CFG_W].u.string.sval);
    ret->h = atoi(cfg[CFG_H].u.string.sval);
    ret->gen = cfg[CFG_GEN].u.choices.selected;
    sokoban_gen_defaults(ret);

    /* only the settings of the selected generator are used */
    if (ret->gen == 1) {
        ret->bm_count = atoi(cfg[CFG_BM].u.string.sval);
        ret->bm_time = atoi(cfg[CFG_BM + 1].u.string.sval);
        ret->bm_nodes = atoi(cfg[CFG_BM + 2].u.string.sval);
    } else if (ret->gen == 2) {
        ret->mc_time = atoi(cfg[CFG_MC].u.string.sval);
        ret->mc_keep = atoi(cfg[CFG_MC + 1].u.string.sval);
        ret->mc_pick = atoi(cfg[CFG_MC + 2].u.string.sval);
    } else if (ret->gen == 3) {
        ret->yg_mode = cfg[CFG_YG].u.choices.selected;
        ret->yg_goals = atoi(cfg[CFG_YG + 1].u.string.sval);
        ret->yg_walls = atoi(cfg[CFG_YG + 2].u.string.sval);
        ret->yg_time = atoi(cfg[CFG_YG + 3].u.string.sval);
        ret->yg_conn = cfg[CFG_YG + 4].u.choices.selected;
        ret->yg_fit = cfg[CFG_YG + 5].u.choices.selected;
        ret->yg_rand = cfg[CFG_YG + 6].u.boolean.bval ? 1 : 0;
        ret->yg_tail = atoi(cfg[CFG_YG + 7].u.string.sval);
    } else if (ret->gen == 4) {
        ret->rm_boxes = atoi(cfg[CFG_RM].u.string.sval);
        ret->rm_limit = atoi(cfg[CFG_RM + 1].u.string.sval);
        ret->rm_attempts = atoi(cfg[CFG_RM + 2].u.string.sval);
    }

    return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
    if (params->w < 4 || params->h < 4)
	return "Width and height must both be at least 4";
    if (params->gen < 0 || params->gen > 4)
        return "Unknown level generator";
    /* the smallest board each generator can make (a new game only; the
     * generators of big boards work on tiles, so there is no upper limit) */
    if (full && params->gen == 1 && (params->w < 10 || params->h < 6 ||
                                     (params->h < 8 && params->w < 16)))
        return "Boulders needs a board of at least 10x8 (or 16x6)";
    if (full && params->gen == 2 && params->w * params->h > 254 &&
        (params->w < 5 || params->h < 5))
        return "MCTS needs a board of at least 5x5 when it has more than 254 squares";
    if (full && (params->gen == 3 || params->gen == 4) &&
        (params->w < 5 || params->h < 5))
        return "YASGen and Rooms need a board of at least 5x5";
    if (params->rm_boxes < 0 || params->rm_boxes > 400)
        return "Rooms number of boxes must be between 0 and 400";
    if (params->rm_limit < 10 || params->rm_limit > 3000)
        return "Rooms search limit must be between 10 and 3000 thousand positions";
    if (params->rm_attempts < 1 || params->rm_attempts > 50)
        return "Rooms goal placements must be between 1 and 50";
    if (params->bm_count < 0 || params->bm_count > 7)
        return "Number of boulders must be between 0 and 7";
    if (params->bm_time < 0 || params->bm_time > 120)
        return "Boulders search time must be between 0 and 120 seconds";
    if (params->bm_nodes < 10 || params->bm_nodes > 5000)
        return "Boulders search nodes must be between 10 and 5000";
    if (params->mc_time < 0 || params->mc_time > 120)
        return "MCTS search time must be between 0 and 120 seconds";
    if (params->mc_keep < 1)
        return "MCTS levels to keep must be at least 1";
    if (params->mc_pick < 1 || params->mc_pick > params->mc_keep)
        return "MCTS level to use must be between 1 and the number of levels to keep";
    if (params->yg_goals < 0 || params->yg_goals > 400)
        return "YASGen number of goals must be between 0 and 400";
    if (params->yg_mode < 0 || params->yg_mode > 1)
        return "Unknown YASGen search direction";
    if (params->yg_conn < 0 || params->yg_conn > 1)
        return "Unknown YASGen level type";
    if (params->yg_fit < 0 || params->yg_fit > 1)
        return "Unknown YASGen fitness";
    if (params->yg_rand < 0 || params->yg_rand > 1)
        return "YASGen: trivial opening pushes must be 0 or 1";
    if (params->yg_walls < 0 || params->yg_walls > 50)
        return "YASGen interior walls must be between 0 and 50 percent";
    if (params->yg_time < 0 || params->yg_time > 120)
        return "YASGen time limit must be between 0 and 120 seconds";
    if (params->yg_tail < 0 || params->yg_tail > 4)
        return "YASGen tail pushes threshold must be between 0 and 4";

    return NULL;
}

/* ----------------------------------------------------------------------
 * Game generation mechanism.
 * 
 * To generate a Sokoban level, we begin with a completely blank
 * grid and make valid inverse moves. Grid squares can be in a
 * number of states. The states are:
 * 
 *  - INITIAL: this square has not as yet been touched by any
 *    inverse move, which essentially means we haven't decided what
 *    it is yet.
 * 
 *  - SPACE: this square is a space.
 * 
 *  - TARGET: this square is a space which is also the target for a
 *    barrel.
 * 
 *  - BARREL: this square contains a barrel.
 * 
 *  - BARRELTARGET: this square contains a barrel _on_ a target.
 * 
 *  - WALL: this square is a wall.
 * 
 *  - PLAYER: this square contains the player.
 * 
 *  - PLAYERTARGET: this square contains the player on a target.
 * 
 * We begin with every square of the in state INITIAL, apart from a
 * solid ring of WALLs around the edge. We randomly position the
 * PLAYER somewhere. Thereafter our valid moves are:
 * 
 *  - to move the PLAYER in one direction _pulling_ a barrel after
 *    us. For this to work, we must have SPACE or INITIAL in the
 *    direction we're moving, and BARREL or BARRELTARGET in the
 *    direction we're moving away from. We leave SPACE or TARGET
 *    respectively in the vacated square.
 * 
 *  - to create a new barrel by transforming an INITIAL square into
 *    BARRELTARGET.
 * 
 *  - to move the PLAYER freely through SPACE and TARGET squares,
 *    leaving SPACE or TARGET where it started.
 * 
 *  - to move the player through INITIAL squares, carving a tunnel
 *    of SPACEs as it goes.
 * 
 * We try to avoid destroying INITIAL squares wherever possible (if
 * there's a path to where we want to be using only SPACE, then we
 * should always use that). At the end of generation, every square
 * still in state INITIAL is one which was not required at any
 * point during generation, which means we can randomly choose
 * whether to make it SPACE or WALL.
 * 
 * It's unclear as yet what the right strategy for wall placement
 * should be. Too few WALLs will yield many alternative solutions
 * to the puzzle, whereas too many might rule out so many
 * possibilities that the intended solution becomes obvious.
 */

static void sokoban_generate(int w, int h, unsigned char *grid, int moves,
			     bool nethack, random_state *rs)
{
    struct pull {
	int ox, oy, nx, ny, score;
    };

    struct pull *pulls;
    int *dist, *prev, *heap;
    int x, y, px, py, i, j, d, heapsize, npulls;

    pulls = snewn(w * h * 4, struct pull);
    dist = snewn(w * h, int);
    prev = snewn(w * h, int);
    heap = snewn(w * h, int);

    /*
     * Configure the initial grid.
     */
    for (y = 0; y < h; y++)
	for (x = 0; x < w; x++)
	    grid[y*w+x] = (x == 0 || y == 0 || x == w-1 || y == h-1 ?
			   WALL : INITIAL);
    if (nethack)
	grid[1] = DEEP_PIT;

    /*
     * Place the player.
     */
    i = random_upto(rs, (w-2) * (h-2));
    x = 1 + i % (w-2);
    y = 1 + i / (w-2);
    grid[y*w+x] = SPACE;
    px = x;
    py = y;

    /*
     * Now loop around making random inverse Sokoban moves. In this
     * loop we aim to make one actual barrel-pull per iteration,
     * plus as many free moves as are necessary to get into
     * position for that pull.
     */
    while (moves-- >= 0) {
	/*
	 * First enumerate all the viable barrel-pulls we can
	 * possibly make, counting two pulls of the same barrel in
	 * different directions as different. We also include pulls
	 * we can perform by creating a new barrel. Each pull is
	 * marked with the amount of violence it would have to do
	 * to the grid.
	 */
	npulls = 0;
	for (y = 0; y < h; y++)
	    for (x = 0; x < w; x++)
		for (d = 0; d < 4; d++) {
		    int dx = DX(d);
		    int dy = DY(d);
		    int nx = x + dx, ny = y + dy;
		    int npx = nx + dx, npy = ny + dy;
		    int score = 0;

		    /*
		     * The candidate move is to put the player at
		     * (nx,ny), and move him to (npx,npy), pulling
		     * a barrel at (x,y) to (nx,ny). So first we
		     * must check that all those squares are within
		     * the boundaries of the grid. For this it is
		     * sufficient to check npx,npy.
		     */
		    if (npx < 0 || npx >= w || npy < 0 || npy >= h)
			continue;

		    /*
		     * (x,y) must either be a barrel, or a square
		     * which we can convert into a barrel.
		     */
		    switch (grid[y*w+x]) {
		      case BARREL: case BARRELTARGET:
			break;
		      case INITIAL:
			if (nethack)
			    continue;
			score += 10 /* new_barrel_score */;
			break;
		      case DEEP_PIT:
			if (!nethack)
			    continue;
			break;
		      default:
			continue;
		    }

		    /*
		     * (nx,ny) must either be a space, or a square
		     * which we can convert into a space.
		     */
		    switch (grid[ny*w+nx]) {
		      case SPACE: case TARGET:
			break;
		      case INITIAL:
			score += 3 /* new_space_score */;
			break;
		      default:
			continue;
		    }

		    /*
		     * (npx,npy) must also either be a space, or a
		     * square which we can convert into a space.
		     */
		    switch (grid[npy*w+npx]) {
		      case SPACE: case TARGET:
			break;
		      case INITIAL:
			score += 3 /* new_space_score */;
			break;
		      default:
			continue;
		    }

		    /*
		     * That's sufficient to tag this as a possible
		     * pull right now. We still don't know if we
		     * can reach the required player position, but
		     * that's a job for the subsequent BFS phase to
		     * tell us.
		     */
		    pulls[npulls].ox = x;
		    pulls[npulls].oy = y;
		    pulls[npulls].nx = nx;
		    pulls[npulls].ny = ny;
		    pulls[npulls].score = score;
#ifdef GENERATION_DIAGNOSTICS
		    printf("found potential pull: (%d,%d)-(%d,%d) cost %d\n",
			   pulls[npulls].ox, pulls[npulls].oy,
			   pulls[npulls].nx, pulls[npulls].ny,
			   pulls[npulls].score);
#endif
		    npulls++;
		}
#ifdef GENERATION_DIAGNOSTICS
	printf("found %d potential pulls\n", npulls);
#endif

	/*
	 * If there are no pulls available at all, we give up.
	 * 
	 * (FIXME: or perhaps backtrack?)
	 */
	if (npulls == 0)
	    break;

	/*
	 * Now we do a BFS from our current position, to find all
	 * the squares we can get the player into.
	 * 
	 * This BFS is unusually tricky. We want to give a positive
	 * distance only to squares which we have to carve through
	 * INITIALs to get to, which means we can't just stick
	 * every square we reach on the end of our to-do list.
	 * Instead, we must maintain our list as a proper priority
	 * queue.
	 */
	for (i = 0; i < w*h; i++)
	    dist[i] = prev[i] = -1;

	heap[0] = py*w+px;
	heapsize = 1;
	dist[py*w+px] = 0;

#define PARENT(n) ( ((n)-1)/2 )
#define LCHILD(n) ( 2*(n)+1 )
#define RCHILD(n) ( 2*(n)+2 )
#define SWAP(i,j) do { int swaptmp = (i); (i) = (j); (j) = swaptmp; } while (0)

	while (heapsize > 0) {
	    /*
	     * Pull the smallest element off the heap: it's at
	     * position 0. Move the arbitrary element from the very
	     * end of the heap into position 0.
	     */
	    y = heap[0] / w;
	    x = heap[0] % w;

	    heapsize--;
	    heap[0] = heap[heapsize];

	    /*
	     * Now repeatedly move that arbitrary element down the
	     * heap by swapping it with the more suitable of its
	     * children.
	     */
	    i = 0;
	    while (1) {
		int lc, rc;

		lc = LCHILD(i);
		rc = RCHILD(i);

		if (lc >= heapsize)
		    break;	       /* we've hit bottom */

		if (rc >= heapsize) {
		    /*
		     * Special case: there is only one child to
		     * check.
		     */
		    if (dist[heap[i]] > dist[heap[lc]])
			SWAP(heap[i], heap[lc]);

		    /* _Now_ we've hit bottom. */
		    break;
		} else {
		    /*
		     * The common case: there are two children and
		     * we must check them both.
		     */
		    if (dist[heap[i]] > dist[heap[lc]] ||
			dist[heap[i]] > dist[heap[rc]]) {
			/*
			 * Pick the more appropriate child to swap with
			 * (i.e. the one which would want to be the
			 * parent if one were above the other - as one
			 * is about to be).
			 */
			if (dist[heap[lc]] > dist[heap[rc]]) {
			    SWAP(heap[i], heap[rc]);
			    i = rc;
			} else {
			    SWAP(heap[i], heap[lc]);
			    i = lc;
			}
		    } else {
			/* This element is in the right place; we're done. */
			break;
		    }
		}
	    }

	    /*
	     * OK, that's given us (x,y) for this phase of the
	     * search. Now try all directions from here.
	     */

	    for (d = 0; d < 4; d++) {
		int dx = DX(d);
		int dy = DY(d);
		int nx = x + dx, ny = y + dy;
		if (nx < 0 || nx >= w || ny < 0 || ny >= h)
		    continue;
		if (grid[ny*w+nx] != SPACE && grid[ny*w+nx] != TARGET &&
		    grid[ny*w+nx] != INITIAL)
		    continue;
		if (dist[ny*w+nx] == -1) {
		    dist[ny*w+nx] = dist[y*w+x] + (grid[ny*w+nx] == INITIAL);
		    prev[ny*w+nx] = y*w+x;

		    /*
		     * Now insert ny*w+nx at the end of the heap,
		     * and move it down to its appropriate resting
		     * place.
		     */
		    i = heapsize;
		    heap[heapsize++] = ny*w+nx;

		    /*
		     * Swap element n with its parent repeatedly to
		     * preserve the heap property.
		     */

		    while (i > 0) {
			int p = PARENT(i);

			if (dist[heap[p]] > dist[heap[i]]) {
			    SWAP(heap[p], heap[i]);
			    i = p;
			} else
			    break;
		    }
		}
	    }
	}

#undef PARENT
#undef LCHILD
#undef RCHILD
#undef SWAP

#ifdef GENERATION_DIAGNOSTICS
	printf("distance map:\n");
	for (i = 0; i < h; i++) {
	    for (j = 0; j < w; j++) {
		int d = dist[i*w+j];
		int c;
		if (d < 0)
		    c = '#';
		else if (d >= 36)
		    c = '!';
		else if (d >= 10)
		    c = 'A' - 10 + d;
		else
		    c = '0' + d;
		putchar(c);
	    }
	    putchar('\n');
	}
#endif

	/*
	 * Now we can go back through the `pulls' array, adjusting
	 * the score for each pull depending on how hard it is to
	 * reach its starting point, and also throwing out any
	 * whose starting points are genuinely unreachable even
	 * with the possibility of carving through INITIAL squares.
	 */
	for (i = j = 0; i < npulls; i++) {
#ifdef GENERATION_DIAGNOSTICS
	    printf("potential pull (%d,%d)-(%d,%d)",
		   pulls[i].ox, pulls[i].oy,
		   pulls[i].nx, pulls[i].ny);
#endif
	    x = pulls[i].nx;
	    y = pulls[i].ny;
	    if (dist[y*w+x] < 0) {
#ifdef GENERATION_DIAGNOSTICS
		printf(" unreachable\n");
#endif
		continue;	       /* this pull isn't feasible at all */
	    } else {
		/*
		 * Another nasty special case we have to check is
		 * whether the initial barrel location (ox,oy) is
		 * on the path used to reach the square. This can
		 * occur if that square is in state INITIAL: the
		 * pull is initially considered valid on the basis
		 * that the INITIAL can become BARRELTARGET, and
		 * it's also considered reachable on the basis that
		 * INITIAL can be turned into SPACE, but it can't
		 * be both at once.
		 * 
		 * Fortunately, if (ox,oy) is on the path at all,
		 * it must be only one space from the end, so this
		 * is easy to spot and rule out.
		 */
		if (prev[y*w+x] == pulls[i].oy*w+pulls[i].ox) {
#ifdef GENERATION_DIAGNOSTICS
		    printf(" goes through itself\n");
#endif
		    continue;	       /* this pull isn't feasible at all */
		}
		pulls[j] = pulls[i];   /* structure copy */
		pulls[j].score += dist[y*w+x] * 3 /* new_space_score */;
#ifdef GENERATION_DIAGNOSTICS
		printf(" reachable at distance %d (cost now %d)\n",
		       dist[y*w+x], pulls[j].score);
#endif
		j++;
	    }
	}
	npulls = j;

	/*
	 * Again, if there are no pulls available at all, we give
	 * up.
	 * 
	 * (FIXME: or perhaps backtrack?)
	 */
	if (npulls == 0)
	    break;

	/*
	 * Now choose which pull to make. On the one hand we should
	 * prefer pulls which do less damage to the INITIAL squares
	 * (thus, ones for which we can already get into position
	 * via existing SPACEs, and for which the barrel already
	 * exists and doesn't have to be invented); on the other,
	 * we want to avoid _always_ preferring such pulls, on the
	 * grounds that that will lead to levels without very much
	 * stuff in.
	 * 
	 * When creating new barrels, we prefer creations which are
	 * next to existing TARGET squares.
	 * 
	 * FIXME: for the moment I'll make this very simple indeed.
	 */
	i = random_upto(rs, npulls);

	/*
	 * Actually make the pull, including carving a path to get
	 * to the site if necessary.
	 */
	x = pulls[i].nx;
	y = pulls[i].ny;
	while (prev[y*w+x] >= 0) {
	    int p;

	    if (grid[y*w+x] == INITIAL)
		grid[y*w+x] = SPACE;

	    p = prev[y*w+x];
	    y = p / w;
	    x = p % w;
	}
	px = 2*pulls[i].nx - pulls[i].ox;
	py = 2*pulls[i].ny - pulls[i].oy;
	if (grid[py*w+px] == INITIAL)
	    grid[py*w+px] = SPACE;
	if (grid[pulls[i].ny*w+pulls[i].nx] == TARGET)
	    grid[pulls[i].ny*w+pulls[i].nx] = BARRELTARGET;
	else
	    grid[pulls[i].ny*w+pulls[i].nx] = BARREL;
	if (grid[pulls[i].oy*w+pulls[i].ox] == BARREL)
	    grid[pulls[i].oy*w+pulls[i].ox] = SPACE;
	else if (grid[pulls[i].oy*w+pulls[i].ox] != DEEP_PIT)
	    grid[pulls[i].oy*w+pulls[i].ox] = TARGET;
    }

    sfree(heap);
    sfree(prev);
    sfree(dist);
    sfree(pulls);

    if (grid[py*w+px] == TARGET)
	grid[py*w+px] = PLAYERTARGET;
    else
	grid[py*w+px] = PLAYER;
}

/* ----------------------------------------------------------------------
 * Alternative level generators (selectable under Custom).
 */
#ifdef __cplusplus
extern "C"
#endif
int sokoban_mcts_generate(int width, int height, unsigned long long seed,
                          double timeout_sec, int keep, int pick, unsigned rnd,
                          char *out, int outcap);

/*
 * MCTS generator (lkseg/sokoban-level-generator-mcts), with its code as it
 * is in the original.  The original app gives the generator the width and
 * the height of the board, which may have 16 to 254 squares (the board index
 * is a u8), and a board of that size is made here in the same way.  The
 * tiles of a bigger board (tile_ms is set) are made inside a ring of wall of
 * their own.
 */
static bool sokoban_gen_mcts(const game_params *params, random_state *rs,
                             unsigned char *grid)
{
    int w = params->w, h = params->h, bw, bh, o, x, y;
    unsigned long long seed;
    char *buf;
    bool ok = false;
    const char *p;
    double secs;

    if (params->tile_ms > 0 || w * h > 254) {
        bw = w - 2; bh = h - 2; o = 1;
    } else {
        bw = w; bh = h; o = 0;
    }
    if (bw < 3 || bh < 3 || bw * bh < 16 || bw * bh > 254)
        return false;

    seed = ((unsigned long long)random_bits(rs, 31) << 32) |
           random_bits(rs, 31);
    secs = params->tile_ms > 0 ? params->tile_ms / 1000.0 :
           params->mc_time > 0 ? (double)params->mc_time :
                                 2.0 + 0.02 * bw * bh;
    buf = snewn(bw * bh * 2 + 16, char);
    if (!sokoban_mcts_generate(bw, bh, seed ? seed : 1, secs,
                               params->mc_keep, params->mc_pick, 0,
                               buf, bw * bh * 2 + 16)) {
        sfree(buf);
        return false;
    }
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            grid[y*w+x] = WALL;
    p = buf;
    ok = true;
    for (y = 0; y < bh && ok; y++) {
        for (x = 0; x < bw; x++) {
            int c;
            while (*p == '\n' || *p == '\r') p++;
            switch (*p) {
              case 'x': c = WALL; break;
              case '-': c = SPACE; break;
              case 'g': c = TARGET; break;
              case 'c': c = BARREL; break;
              case 'C': c = BARRELTARGET; break;
              case 'p': c = PLAYER; break;
              case 'P': c = PLAYERTARGET; break;
              default: ok = false; c = WALL; break;
            }
            if (!ok) break;
            p++;
            grid[(y+o)*w + x+o] = c;
        }
    }
    sfree(buf);
    return ok;
}


/* ----------------------------------------------------------------------
 * "Boulders" generator: a port of the level maker from miki151/sokoban
 * (NetHack-style levels in which a row of pits has to be filled with
 * boulders). Levels are produced by playing backwards from the solved
 * position and keeping the deepest one found.
 */
struct bm_rect { int l, t, r, b; };          /* half-open */
struct bm_v { int x, y; };
#define BM_V(X,Y) ((struct bm_v){(X),(Y)})

struct boulder_maker {
    random_state *rs;
    int W, H, nb, numNodes, numRooms, numDoors;
    bool fail;
    char *level, *best;
    int maxDepth;
    struct bm_v finalPos, curPos;
    int middleLine;
    struct bm_rect work;
    struct bm_v boulders[16];
    int nbl;
    unsigned *visited;                /* open-addressing hash set */
    int visitedN, visitedCap;
};

static int bm_rnd(struct boulder_maker *m, int lo, int hi)
{
    if (hi <= lo) { m->fail = true; return lo; }
    return lo + random_upto(m->rs, hi - lo);
}

static bool bm_inside(const struct boulder_maker *m, struct bm_v v)
{
    return v.x >= 0 && v.x < m->W && v.y >= 0 && v.y < m->H;
}

static char bm_get(struct boulder_maker *m, char *g, struct bm_v v)
{
    if (!bm_inside(m, v)) { m->fail = true; return '#'; }
    return g[v.y * m->W + v.x];
}

static void bm_set(struct boulder_maker *m, char *g, struct bm_v v, char c)
{
    if (!bm_inside(m, v)) { m->fail = true; return; }
    g[v.y * m->W + v.x] = c;
}

static bool bm_in_rect(struct bm_rect r, struct bm_v v)
{
    return v.x >= r.l && v.x < r.r && v.y >= r.t && v.y < r.b;
}

static void bm_fill_rect(struct boulder_maker *m, struct bm_rect r, char c)
{
    int x, y;
    if (r.r <= r.l || r.b <= r.t) { m->fail = true; return; }
    for (x = r.l; x < r.r; x++)
        for (y = r.t; y < r.b; y++)
            bm_set(m, m->level, BM_V(x, y), c);
}

static int bm_max(int a, int b) { return a > b ? a : b; }
static int bm_min(int a, int b) { return a < b ? a : b; }

static void bm_perm(struct boulder_maker *m, int *a, int n)
{
    int i;
    for (i = n - 1; i > 0; i--) {
        int j = random_upto(m->rs, i + 1), t = a[i];
        a[i] = a[j]; a[j] = t;
    }
}

static void bm_prepare_rooms(struct boulder_maker *m, struct bm_rect area)
{
    int aw = area.r - area.l, ah = area.b - area.t;
    struct bm_v mainSize, mainPos;
    struct bm_rect mr;
    int sides[4] = {0, 1, 2, 3}, i;

    if (aw <= 0 || ah <= 0) { m->fail = true; return; }
    mainSize = BM_V(bm_rnd(m, 3, 5), bm_rnd(m, 3, 5));
    mainPos = BM_V((aw - mainSize.x) / 2, (ah - mainSize.y) / 2);
    mr = (struct bm_rect){mainPos.x, mainPos.y,
                          mainPos.x + mainSize.x, mainPos.y + mainSize.y};
    bm_fill_rect(m, mr, '.');
    bm_perm(m, sides, 4);
    for (i = 0; i < m->numRooms - 1 && !m->fail; i++) {
        struct bm_v size, pos = BM_V(0, 0);
        bool door = i < m->numDoors;
        do {
            size = BM_V(bm_rnd(m, 2, 4), bm_rnd(m, 2, 4));
        } while (size.x <= 2 && size.y <= 2 && !m->fail);
        switch (sides[i % 4]) {
          case 0:
            pos = BM_V(mr.r, bm_rnd(m, bm_max(area.t, mr.t - size.y + 1),
                                    bm_min(area.b, mr.b)));
            if (door) {
                pos.x += 1;
                bm_set(m, m->level, BM_V(mr.r, bm_rnd(m, bm_max(mr.t, pos.y),
                       bm_min(mr.b, pos.y + size.y))), '.');
            }
            break;
          case 1:
            pos = BM_V(bm_rnd(m, bm_max(area.l, mr.l - size.x + 1),
                              bm_min(area.r, mr.r)), mr.b);
            if (door) {
                pos.y += 1;
                bm_set(m, m->level, BM_V(bm_rnd(m, bm_max(mr.l, pos.x),
                       bm_min(mr.r, pos.x + size.x)), mr.b), '.');
            }
            break;
          case 2:
            pos = BM_V(mr.l - size.x, bm_rnd(m, bm_max(area.t, mr.t - size.y + 1),
                                             bm_min(area.b, mr.b)));
            if (door) {
                pos.x -= 1;
                bm_set(m, m->level, BM_V(mr.l - 1, bm_rnd(m, bm_max(mr.t, pos.y),
                       bm_min(mr.b, pos.y + size.y))), '.');
            }
            break;
          default:
            pos = BM_V(bm_rnd(m, bm_max(area.l, mr.l - size.x + 1),
                              bm_min(area.r, mr.r)), mr.t - size.y);
            if (door) {
                pos.y -= 1;
                bm_set(m, m->level, BM_V(bm_rnd(m, bm_max(mr.l, pos.x),
                       bm_min(mr.r, pos.x + size.x)), mr.t - 1), '.');
            }
            break;
        }
        if (!m->fail)
            bm_fill_rect(m, (struct bm_rect){pos.x, pos.y, pos.x + size.x,
                                             pos.y + size.y}, '.');
    }
}

static bool bm_is_free(struct boulder_maker *m, struct bm_v p)
{
    return bm_in_rect(m->work, p) && bm_inside(m, p) &&
           m->level[p.y * m->W + p.x] == '.';
}

static const struct bm_v bm_dirs[4] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};

static void bm_reachable(struct boulder_maker *m, struct bm_v from, bool *r)
{
    int *q = snewn(m->W * m->H, int), qh = 0, qt = 0, d;
    memset(r, 0, m->W * m->H * sizeof(bool));
    r[from.y * m->W + from.x] = true;
    q[qt++] = from.y * m->W + from.x;
    while (qh < qt) {
        int c = q[qh++];
        for (d = 0; d < 4; d++) {
            struct bm_v n = BM_V(c % m->W + bm_dirs[d].x, c / m->W + bm_dirs[d].y);
            if (bm_in_rect(m->work, n) && bm_inside(m, n) &&
                !r[n.y * m->W + n.x] && bm_is_free(m, n)) {
                r[n.y * m->W + n.x] = true;
                q[qt++] = n.y * m->W + n.x;
            }
        }
    }
    sfree(q);
}

static int bm_hash(struct boulder_maker *m)
{
    int seed = 0, i;
    for (i = 0; i < m->nbl; i++) {
        seed ^= (int)((unsigned)m->boulders[i].x + 0x9e3779b9u +
                      ((unsigned)seed << 6) + (unsigned)(seed >> 2));
        seed ^= (int)((unsigned)m->boulders[i].y + 0x9e3779c9u +
                      ((unsigned)seed << 6) + (unsigned)(seed >> 2));
    }
    return seed;
}

static void bm_move_boulder(struct boulder_maker *m, int depth,
                            int *visitedKeys, int *nvisited)
{
    int W = m->W, H = m->H;
    bool *reach;
    int order[16], dorder[4], bi, di, i;

    if (m->fail) return;
    if (depth > m->maxDepth) {
        memcpy(m->best, m->level, W * H);
        m->maxDepth = depth;
        m->finalPos = m->curPos;
    }
    if (*nvisited > m->numNodes) return;
    reach = snewn(W * H, bool);
    bm_reachable(m, m->curPos, reach);
    for (i = 0; i < m->nbl; i++) order[i] = i;
    bm_perm(m, order, m->nbl);
    for (bi = 0; bi < m->nbl && !m->fail; bi++) {
        struct bm_v boulderPos = m->boulders[order[bi]];
        for (i = 0; i < 4; i++) dorder[i] = i;
        bm_perm(m, dorder, 4);
        for (di = 0; di < 4 && !m->fail; di++) {
            struct bm_v v = bm_dirs[dorder[di]], pos, dest = BM_V(0, 0), t;
            bool have = false;
            int i2 = 1, idx, h, k;
            struct bm_v prev;
            t = BM_V(boulderPos.x + v.x, boulderPos.y + v.y);
            if (!(bm_inside(m, t) && reach[t.y * W + t.x]) ||
                (boulderPos.x >= m->middleLine - 1 && v.x > 0))
                continue;
            pos = t;
            while (bm_is_free(m, BM_V(pos.x + v.x * i2, pos.y + v.y * i2)) &&
                   (v.x <= 0 || pos.x + v.x * i2 <= m->middleLine)) {
                int step = bm_rnd(m, 1, i2 + 1);
                dest = BM_V(pos.x + v.x * step, pos.y + v.y * step);
                have = true;
                i2++;
            }
            if (!have) continue;
            idx = -1;
            for (k = 0; k < m->nbl; k++)
                if (m->boulders[k].x == boulderPos.x && m->boulders[k].y == boulderPos.y)
                    idx = k;
            if (idx < 0) continue;
            m->boulders[idx] = BM_V(dest.x - v.x, dest.y - v.y);
            bm_set(m, m->level, m->boulders[idx], '0');
            bm_set(m, m->level, boulderPos, '.');
            prev = m->curPos;
            m->curPos = dest;
            h = bm_hash(m);
            {
                bool seen = false;
                for (k = 0; k < *nvisited; k++)
                    if (visitedKeys[k] == h) { seen = true; break; }
                if (!seen) {
                    if (*nvisited < m->numNodes * 3 + 1000) visitedKeys[(*nvisited)++] = h;
                    bm_move_boulder(m, depth + 1, visitedKeys, nvisited);
                }
            }
            bm_set(m, m->level, BM_V(dest.x - v.x, dest.y - v.y), '.');
            bm_set(m, m->level, boulderPos, '0');
            m->boulders[idx] = boulderPos;
            m->curPos = prev;
        }
    }
    sfree(reach);
}

static bool bm_make(struct boulder_maker *m)
{
    int W = m->W, H = m->H, x, i, k;
    struct bm_v start = BM_V(-1, -1), c;
    int *keys;
    int nvis = 0;
    int ys[64];
    int nys;

    bm_prepare_rooms(m, (struct bm_rect){0, 0, W - 1 - 2 - 1 - m->nb, H});
    if (m->fail) return false;
    nys = H - 2;
    if (nys < 1 || nys > 64) return false;
    for (x = W - 2; x >= 1 && start.x < 0; x--) {
        for (i = 0; i < nys; i++) ys[i] = 1 + i;
        bm_perm(m, ys, nys);
        for (i = 0; i < nys; i++)
            if (bm_get(m, m->level, BM_V(x, ys[i])) == '.') {
                start = BM_V(x, ys[i]);
                break;
            }
    }
    if (m->fail || start.x < 0) return false;
    m->middleLine = start.x;
    m->work = (struct bm_rect){0, 0, start.x + m->nb, H};
    m->nbl = 0;
    for (i = 1; i <= m->nb; i++) {
        struct bm_v p = BM_V(start.x + i, start.y);
        bm_set(m, m->level, p, '0');
        m->boulders[m->nbl++] = p;
    }
    bm_set(m, m->level, BM_V(start.x + m->nb + 1, start.y), '+');
    c = BM_V(start.x + m->nb + 3, start.y);
    bm_fill_rect(m, (struct bm_rect){c.x - 1, c.y - 1, c.x + 2, c.y + 2}, '.');
    if (m->fail) return false;
    m->curPos = start;
    keys = snewn(m->numNodes * 3 + 1000, int);
    bm_move_boulder(m, 0, keys, &nvis);
    sfree(keys);
    if (m->fail) return false;
    for (i = 1; i <= m->nb; i++) {
        struct bm_v hole = BM_V(start.x + i, start.y);
        if ((hole.x == m->finalPos.x && hole.y == m->finalPos.y) ||
            bm_get(m, m->best, hole) != '.')
            return false;
        bm_set(m, m->best, hole, '^');
    }
    bm_set(m, m->best, m->finalPos, '@');
    (void)k;
    return !m->fail;
}

/* seconds on a monotonic clock */
static double sokoban_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/*
 * The loop of Generator.generate() of the original app: many randomised
 * attempts for a time budget (5 seconds there), the deepest level wins.  The
 * original board is 28 by 16 with 3 to 7 boulders (4 by default); here the
 * size is the one of the board.
 */
static bool sokoban_gen_boulders(const game_params *params, random_state *rs,
                                 unsigned char *grid)
{
    int w = params->w, h = params->h, bestDepth = -1, i;
    char *bestGrid = NULL;
    int nb = params->bm_count ? params->bm_count : 4;
    double t0 = sokoban_now(), budget;

    /* the boulders and the room of the prize take the width of the board */
    if (nb > w - 8) nb = w - 8;
    if (nb < 1) nb = 1;
    if (w < 10 || h < 6)
        return false;
    budget = params->tile_ms > 0 ? params->tile_ms / 1000.0 :
             params->bm_time > 0 ? (double)params->bm_time : 5.0;

    for (;;) {
        struct boulder_maker m;
        bool ok;
        double el = sokoban_now() - t0;
        if ((bestGrid && el >= budget) || el > 60.0)
            break;
        memset(&m, 0, sizeof m);
        m.rs = rs; m.W = w; m.H = h; m.nb = nb; m.numNodes = params->bm_nodes;
        m.numRooms = 3; m.numDoors = 1; m.maxDepth = -1;
        m.level = snewn(w * h, char);
        m.best = snewn(w * h, char);
        memset(m.level, '#', w * h);
        memset(m.best, '?', w * h);
        ok = bm_make(&m);
        if (ok && m.maxDepth > bestDepth) {
            bestDepth = m.maxDepth;
            sfree(bestGrid);
            bestGrid = snewn(w * h, char);
            memcpy(bestGrid, m.best, w * h);
        }
        sfree(m.level);
        sfree(m.best);
    }
    if (!bestGrid)
        return false;
    for (i = 0; i < w * h; i++) {
        switch (bestGrid[i]) {
          case '.': case '+': grid[i] = SPACE; break;
          case '0': grid[i] = BARREL; break;
          case '^': grid[i] = PIT; break;
          case '@': grid[i] = PLAYER; break;
          default: grid[i] = WALL; break;
        }
    }
    sfree(bestGrid);
    return true;
}


/* ----------------------------------------------------------------------
 * YASGen generator: Brian Damgaard's YASGen 1.50 ("Yet Another Sokoban
 * Level Generator - For Small Levels", the generator of Sokoban YASC),
 * translated to C in sokoban-yasgen/yasgen150.c function by function.  It
 * is a genetic algorithm: the chromosomes are small boards (walls and
 * goals), the fitness of a board is the number of pushes of the best
 * level that a breadth-first search finds on it.
 *
 * The original runs until the user stops it; here it runs for a number of
 * seconds, and the best level found is used.  The game's own settings are
 * the ones of the generator window of the original, with the original's
 * defaults for everything that is not a setting here (population 50,
 * inactivity threshold 20, cross-over 50 %, mutation 80 %, 2 mutations).
 */
#include "sokoban-yasgen/yasgen150.h"

static bool sokoban_gen_yasgen(const game_params *params, random_state *rs,
                               unsigned char *grid)
{
    int gw = params->w, gh = params->h;
    int bw = gw - 2, bh = gh - 2;                /* the interior */
    int x, y, pushes, candidates = 0;
    double secs;
    yg150_params yp;
    char *out;

    if (bw < 3 || bh < 3)                        /* the smallest board of YASGen */
        return false;
    if (bw > 48) bw = 48;                        /* the largest board of YASGen */
    if (bh > 48) bh = 48;

    yg150_defaults(&yp);
    yp.width = bw; yp.height = bh;
    yp.wall_pct = params->yg_walls;
    yp.max_boxes = params->yg_goals > 0 ? params->yg_goals : 4;
    yp.forward = params->yg_mode;
    yp.connected_goals = params->yg_conn;
    yp.fitness_score = params->yg_fit;
    yp.trivial_opening = params->yg_rand;
    yp.tail_pushes = params->yg_tail;
    yp.memory_mb = 48;                           /* (128 in the original) */
    yp.seed = (unsigned)random_bits(rs, 30) + 1;

    /* the time: the soft limit stops the evolution, the hard one abandons
     * a search that is still running (a result is wanted by then) */
    if (params->tile_ms > 0)
        secs = params->tile_ms / 1000.0;
    else
        secs = params->yg_time > 0 ? params->yg_time : 5;
    yp.seconds = secs;
    yp.hard_seconds = secs * 2 + 3;

    out = snewn((bw + 2) * (bh + 2) + 1, char);
    pushes = yg150_generate(&yp, out, &candidates);
    if (pushes <= 0) {
        sfree(out);
        return false;
    }
    for (y = 0; y < gh; y++)
        for (x = 0; x < gw; x++) {
            int ox = x - (gw - (bw + 2)) / 2, oy = y - (gh - (bh + 2)) / 2;
            int c = WALL;
            if (ox >= 0 && oy >= 0 && ox < bw + 2 && oy < bh + 2) {
                switch (out[oy * (bw + 2) + ox]) {
                  case ' ': c = SPACE; break;
                  case '.': c = TARGET; break;
                  case '$': c = BARREL; break;
                  case '*': c = BARRELTARGET; break;
                  case '@': c = PLAYER; break;
                  case '+': c = PLAYERTARGET; break;
                  default: c = WALL; break;
                }
            }
            grid[y * gw + x] = c;
        }
    sfree(out);
    return true;
}

/* ----------------------------------------------------------------------
 * "Rooms" generator: a port of smartive/rust-sokoban-level-generator
 * (Apache-2.0), itself based on Joshua Taylor and Ian Parberry,
 * "Procedural Generation of Sokoban Levels".  The level is assembled
 * from randomly rotated 5x5 room templates (3x3 interior each), checked
 * against the paper's requirements, goals are placed at random and a
 * search over reverse moves ("pulls") finds the position of boxes and
 * player that is farthest from the solved state.
 */


#define RM_FLOOR(c) ((c) == 'F' || (c) == 'S')

static const char *const rm_templates[17] = {
    "EEEEEEFFFEEFFFEEFFFEEEEEE",
    "EEEEEEWFFEEFFFEEFFFEEEEEE",
    "EEEFFEWWFFEFFFEEFFFEEEEEE",
    "EEEEEEWWWEEFFFEEFFFEEEEEE",
    "EEEEEEWWWEEWFFEEWFFEEEEEE",
    "EEFEEEWFFEFFFFEEFFWEEEEEE",
    "EEEEEEWFFEFFFFEEWFFEEEEEE",
    "EEFEEEWFFEFFFFEEWFWEEEFEE",
    "EEFEEEWFWEFFFFFEWFWEEEFEE",
    "EEFEEEWFWEEWSFFEWWWEEEEEE",
    "EEEEEEWWWEFFFFFEWWWEEEEEE",
    "EEEEEEFFFFEFWFFEFFFEEEEEE",
    "EEEEEEWWWEEWWWEEWWWEEEEEE",
    "EEEEEEWWWEEWFFEFFFFEFFEEE",
    "EFEFEEFFFEEWFWEEFFFEEFEFE",
    "EEEEEEWWWEEWWWEEFFFEEFFFE",
    "EEEEEEWWWEFFWFFEFFFEEFFEE",
};

/* generate_rooms(): same neighbour matching as the original, including
 * the way it pairs template edges with neighbouring cells */
static void rm_rooms(int RW, int RH, char *g, random_state *rs)
{
    int GW = RW * 3, i, j, k, fh, fw;
    memset(g, 'E', GW * RH * 3);
    for (fh = 0; fh < RH; fh++)
        for (fw = 0; fw < RW; fw++) {
            char cp[12], tp[12];
            int nc = 0, nt = 0;
            int hb = fh * 3, wb = fw * 3;
            if (fw != 0) for (i = 0; i < 3; i++) cp[nc++] = g[(hb+i)*GW + wb-1];
            if (fw != RW-1) for (i = 0; i < 3; i++) cp[nc++] = g[(hb+i)*GW + wb+3];
            if (fh != 0) for (j = 0; j < 3; j++) cp[nc++] = g[(hb-1)*GW + wb+j];
            if (fh != RH-1) for (j = 0; j < 3; j++) cp[nc++] = g[(hb+3)*GW + wb+j];
            for (;;) {
                char t[25], u[25];
                memcpy(t, rm_templates[random_upto(rs, 17)], 25);
                for (k = random_upto(rs, 4); k > 0; k--) {
                    for (i = 0; i < 5; i++)
                        for (j = 0; j < 5; j++)
                            u[i*5+j] = t[(4-j)*5+i];
                    memcpy(t, u, 25);
                }
                nt = 0;
                if (fw != 0) for (j = 1; j <= 3; j++) tp[nt++] = t[0*5+j];
                if (fw != RW-1) for (j = 1; j <= 3; j++) tp[nt++] = t[4*5+j];
                if (fh != 0) for (i = 1; i <= 3; i++) tp[nt++] = t[i*5+0];
                if (fh != RH-1) for (i = 1; i <= 3; i++) tp[nt++] = t[i*5+4];
                for (i = 0; i < nc; i++)
                    if (cp[i] != 'E' && tp[i] != 'E' && cp[i] != tp[i]) break;
                if (i < nc) continue;
                for (i = 0; i < 3; i++)
                    for (j = 0; j < 3; j++)
                        g[(hb+i)*GW + wb+j] = t[(1+i)*5 + 1+j];
                break;
            }
        }
}

static bool rm_no_large(const char *g, int W, int H)
{
    int x, y, dy, dx;
    bool all;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            if (y + 3 <= H && x + 4 <= W) {
                all = true;
                for (dy = 0; dy < 3 && all; dy++) for (dx = 0; dx < 4; dx++)
                    if (!RM_FLOOR(g[(y+dy)*W+x+dx])) { all = false; break; }
                if (all) return false;
            }
            if (y + 4 <= H && x + 3 <= W) {
                all = true;
                for (dy = 0; dy < 4 && all; dy++) for (dx = 0; dx < 3; dx++)
                    if (!RM_FLOOR(g[(y+dy)*W+x+dx])) { all = false; break; }
                if (all) return false;
            }
        }
    return true;
}

/* goal candidates: floor cells with two floors in line next to them */
static int rm_goal_cells(const char *g, int W, int H, int *out)
{
    int x, y, n = 0;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            bool ok = false;
            if (!RM_FLOOR(g[y*W+x])) continue;
            if (y >= 2 && RM_FLOOR(g[(y-1)*W+x]) && RM_FLOOR(g[(y-2)*W+x])) ok = true;
            if (y + 3 <= H && RM_FLOOR(g[(y+1)*W+x]) && RM_FLOOR(g[(y+2)*W+x])) ok = true;
            if (x >= 2 && RM_FLOOR(g[y*W+x-1]) && RM_FLOOR(g[y*W+x-2])) ok = true;
            if (x + 3 <= W && RM_FLOOR(g[y*W+x+1]) && RM_FLOOR(g[y*W+x+2])) ok = true;
            if (ok) out[n++] = y*W+x;
        }
    return n;
}

static bool rm_requirements(const char *g, int W, int H, int nb)
{
    int x, y, i, nfloor = 0, first = -1, *stack, sp = 0, *goals;
    char *seen;
    bool ok = true;
    for (i = 0; i < W*H; i++)
        if (RM_FLOOR(g[i])) { nfloor++; if (first < 0) first = i; }
    if (first < 0 || nfloor < nb + 2) return false;
    /* connectivity: walk over plain floor squares only (the special
     * square of one template is exempt) */
    seen = snewn(W*H, char); memset(seen, 0, W*H);
    stack = snewn(W*H, int);
    stack[sp++] = first; seen[first] = 1;
    while (sp) {
        int p = stack[--sp], px = p % W, py = p / W;
        if (px > 0 && g[p-1] == 'F' && !seen[p-1]) { seen[p-1] = 1; stack[sp++] = p-1; }
        if (px < W-1 && g[p+1] == 'F' && !seen[p+1]) { seen[p+1] = 1; stack[sp++] = p+1; }
        if (py > 0 && g[p-W] == 'F' && !seen[p-W]) { seen[p-W] = 1; stack[sp++] = p-W; }
        if (py < H-1 && g[p+W] == 'F' && !seen[p+W]) { seen[p+W] = 1; stack[sp++] = p+W; }
    }
    for (i = 0; i < W*H; i++) if (g[i] == 'F' && !seen[i]) ok = false;
    sfree(seen); sfree(stack);
    if (!ok) return false;
    for (y = 0; y < H && ok; y++)
        for (x = 0; x < W && ok; x++) {
            int walls = 0;
            if (!RM_FLOOR(g[y*W+x])) continue;
            if (y == 0 || g[(y-1)*W+x] == 'W') walls++;
            if (y == H-1 || g[(y+1)*W+x] == 'W') walls++;
            if (x == 0 || g[y*W+x-1] == 'W') walls++;
            if (x == W-1 || g[y*W+x+1] == 'W') walls++;
            if (walls >= 3) ok = false;
        }
    if (!ok || !rm_no_large(g, W, H)) return false;
    goals = snewn(W*H, int);
    ok = rm_goal_cells(g, W, H, goals) >= nb;
    sfree(goals);
    return ok;
}

/*
 * create_box_backtrack_map() + the choice of the farthest state, as in
 * the original: depth-first search over reverse moves from every
 * possible player start, remembering for every box layout and every
 * region reachable by the player the lowest move count it was reached
 * with; the level with the highest recorded count wins.  A limit on
 * the number of recorded states (and on memory) replaces the original's
 * unbounded run.  A search frame is nb box squares, the player's square
 * and the move count (two halves), all unsigned shorts.
 */
struct rm_rec { int label; unsigned short maxcell; };

static bool rm_search(const char *g, int W, int H, const int *goals, int nb,
                      long maxrec, random_state *rs,
                      unsigned short *outbox, int *outplayer)
{
    int N = W * H, stride = nb + 1, fsz = nb + 3, i, j, d, s0, stamp = 0;
    long nrec = 0, tsize = 1;
    unsigned short *keys, *key = snewn(stride, unsigned short);
    struct rm_rec *recs;
    int *table, *mark = snewn(N, int), *stack = snewn(N, int);
    unsigned char *boxat = snewn(N, unsigned char);
    int dirx[4] = {0, 0, -1, 1}, diry[4] = {-1, 1, 0, 0};
    unsigned short *fs = NULL, *cur = snewn(fsz, unsigned short),
                   *nf = snewn(fsz, unsigned short),
                   *sorted = snewn(nb, unsigned short);
    long nfs = 0, fcap = 0, fmax = (32L << 20) / (fsz * 2);
    unsigned short *ib = snewn(nb, unsigned short);
    bool found = false;

    if (maxrec > (48L << 20) / (stride * 2 + 8)) maxrec = (48L << 20) / (stride * 2 + 8);
    if (maxrec < 1000) maxrec = 1000;
    keys = snewn(maxrec * stride, unsigned short);
    recs = snewn(maxrec, struct rm_rec);
    while (tsize < maxrec * 2) tsize <<= 1;
    table = snewn(tsize, int);
    for (i = 0; i < tsize; i++) table[i] = -1;
    memset(mark, 0, N * sizeof(int));

    for (i = 0, j = 0; i < N; i++)
        for (d = 0; d < nb; d++)
            if (goals[d] == i) ib[j++] = i;      /* row-major order */

    for (s0 = 0; s0 < N && nrec < maxrec; s0++) {
        if (!RM_FLOOR(g[s0])) continue;
        for (d = 0; d < nb; d++) if (goals[d] == s0) break;
        if (d < nb) continue;                   /* a box stands there */
        nfs = 0;
#define RM_PUSH(FR) do { if (nfs < fmax) { \
            if (nfs == fcap) { fcap = fcap ? fcap * 2 : 1024; \
                if (fcap > fmax) fcap = fmax; \
                fs = sresize(fs, fcap * fsz, unsigned short); } \
            memcpy(fs + nfs * fsz, (FR), fsz * sizeof(unsigned short)); nfs++; } } while (0)
        for (i = 0; i < nb; i++) nf[i] = ib[i];
        nf[nb] = s0; nf[nb+1] = 0; nf[nb+2] = 0;
        RM_PUSH(nf);
        while (nfs > 0 && nrec < maxrec) {
            int reg, regmax, sp = 0, cstamp, step, pl;
            nfs--;
            memcpy(cur, fs + nfs * fsz, fsz * sizeof(unsigned short));
            pl = cur[nb];
            step = cur[nb+1] | ((int)cur[nb+2] << 16);
            reg = regmax = pl;

            memset(boxat, 0, N);
            for (i = 0; i < nb; i++) boxat[cur[i]] = 1;
            /* player's region */
            stamp++; cstamp = stamp;
            stack[sp++] = pl; mark[pl] = stamp;
            while (sp) {
                int p = stack[--sp], px = p % W, py = p / W, e;
                if (p < reg) reg = p;
                if (p > regmax) regmax = p;
                for (e = 0; e < 4; e++) {
                    int nx = px + dirx[e], ny = py + diry[e], q;
                    if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                    q = ny * W + nx;
                    if (!RM_FLOOR(g[q]) || boxat[q] || mark[q] == stamp) continue;
                    mark[q] = stamp; stack[sp++] = q;
                }
            }
            if (step > 0) {
                unsigned h = 2166136261u;
                long slot;
                memcpy(sorted, cur, nb * sizeof(unsigned short));
                for (i = 1; i < nb; i++) {      /* insertion sort */
                    unsigned short t = sorted[i];
                    for (j = i - 1; j >= 0 && sorted[j] > t; j--) sorted[j+1] = sorted[j];
                    sorted[j+1] = t;
                }
                for (i = 0; i < nb; i++) { key[i] = sorted[i]; h = (h ^ key[i]) * 16777619u; }
                key[nb] = reg; h = (h ^ reg) * 16777619u;
                slot = h & (tsize - 1);
                while (table[slot] >= 0 &&
                       memcmp(keys + (long)table[slot] * stride, key, stride * sizeof(unsigned short)))
                    slot = (slot + 1) & (tsize - 1);
                if (table[slot] >= 0) {
                    struct rm_rec *r = &recs[table[slot]];
                    if (step < r->label) r->label = step;
                    continue;                   /* cached: do not expand */
                }
                memcpy(keys + nrec * stride, key, stride * sizeof(unsigned short));
                recs[nrec].label = step;
                recs[nrec].maxcell = regmax;
                table[slot] = nrec++;
                found = true;
            }
            for (i = 0; i < nb; i++) {
                int bx = cur[i] % W, by = cur[i] / W;
                for (d = 0; d < 4; d++) {
                    int b2x = bx + dirx[d], b2y = by + diry[d], p2x, p2y, b2, p2;
                    if (b2x < 0 || b2y < 0 || b2x >= W || b2y >= H) continue;
                    b2 = b2y * W + b2x;
                    if (!RM_FLOOR(g[b2]) || boxat[b2] || mark[b2] != cstamp) continue;
                    p2x = b2x + dirx[d]; p2y = b2y + diry[d];
                    if (p2x < 0 || p2y < 0 || p2x >= W || p2y >= H) continue;
                    p2 = p2y * W + p2x;
                    if (!RM_FLOOR(g[p2]) || boxat[p2] || mark[p2] != cstamp) continue;
                    memcpy(nf, cur, nb * sizeof(unsigned short));
                    nf[i] = b2; nf[nb] = p2;
                    nf[nb+1] = (step + 1) & 0xFFFF; nf[nb+2] = (step + 1) >> 16;
                    RM_PUSH(nf);
                }
            }
        }
#undef RM_PUSH
    }

    if (found) {
        int best = -1, ncand = 0, pick, k;
        long chosen = -1;
        for (i = 0; i < nrec; i++) if (recs[i].label > best) best = recs[i].label;
        for (i = 0; i < nrec; i++) if (recs[i].label == best) ncand++;
        pick = random_upto(rs, ncand);
        for (i = 0; i < nrec; i++)
            if (recs[i].label == best && pick-- == 0) { chosen = i; break; }
        {
            int cell = recs[chosen].maxcell;
            for (i = 0; i < nrec; i++)
                if (recs[i].label == best &&
                    !memcmp(keys + (long)i * stride, keys + chosen * stride, nb * sizeof(unsigned short)) &&
                    recs[i].maxcell > cell) cell = recs[i].maxcell;
            for (k = 0; k < nb; k++) outbox[k] = keys[chosen * stride + k];
            *outplayer = cell;
        }
    }
    sfree(keys); sfree(recs); sfree(table); sfree(mark); sfree(stack);
    sfree(boxat); sfree(fs); sfree(key); sfree(cur); sfree(nf); sfree(sorted);
    sfree(ib);
    return found;
}

/*
 * generate_level() of the original: room layouts are made until one meets
 * the requirements (the original goes on for ever, a layout of 4 by 4 rooms
 * takes some thousand tries, one of 5 by 5 rooms some hundred thousand), then
 * goals are placed at random and the farthest state is found; if that gives
 * nothing, goals are placed again.  The number of tries is limited here.
 */
#define RM_LAYOUT_TRIES 800000
#define RM_LAYOUT_SECONDS 8.0

static bool sokoban_gen_rooms(const game_params *params, random_state *rs,
                              unsigned char *grid)
{
    int gw = params->w, gh = params->h;
    int RW = (gw - 2) / 3, RH = (gh - 2) / 3, W, H, i, tries, nb;
    char *g;
    int *cand, ncand, *goalset, bplayer = -1;
    unsigned short *bbox;
    bool good = false;
    double t0 = sokoban_now();

    if (RW < 1 || RH < 1) return false;
    if (RW > 30) RW = 30;
    if (RH > 30) RH = 30;
    W = RW * 3; H = RH * 3;
    nb = params->rm_boxes ? params->rm_boxes : 2 + (RW * RH) / 3;
    if (!params->rm_boxes && nb > 8) nb = 8;
    if (nb + 2 > W * H) return false;
    g = snewn(W * H, char);
    cand = snewn(W * H, int);

    for (tries = 0; tries < RM_LAYOUT_TRIES && !good; tries++) {
        rm_rooms(RW, RH, g, rs);
        if (rm_requirements(g, W, H, nb)) good = true;
        else if ((tries & 1023) == 1023 && sokoban_now() - t0 > RM_LAYOUT_SECONDS)
            break;
    }
    if (!good) {
        sfree(g); sfree(cand);
        return false;
    }
    ncand = rm_goal_cells(g, W, H, cand);        /* at least nb of them */
    goalset = snewn(nb, int);
    bbox = snewn(nb, unsigned short);
    for (tries = 0; tries < params->rm_attempts; tries++) {
        shuffle(cand, ncand, sizeof(int), rs);
        for (i = 0; i < nb; i++) goalset[i] = cand[i];
        if (rm_search(g, W, H, goalset, nb, (long)params->rm_limit * 1000,
                      rs, bbox, &bplayer))
            break;
    }
    if (bplayer < 0) {
        sfree(g); sfree(cand); sfree(goalset); sfree(bbox);
        return false;
    }
    for (i = 0; i < gw * gh; i++) grid[i] = WALL;
    {
        int ox = 1 + (gw - 2 - W) / 2, oy = 1 + (gh - 2 - H) / 2, x, y;
        char *isb = snewn(W * H, char), *isg = snewn(W * H, char);
        memset(isb, 0, W * H); memset(isg, 0, W * H);
        for (i = 0; i < nb; i++) { isb[bbox[i]] = 1; isg[goalset[i]] = 1; }
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                int idx = y*W + x;
                unsigned char v;
                if (!RM_FLOOR(g[idx])) v = WALL;
                else if (isg[idx]) v = isb[idx] ? BARRELTARGET : TARGET;
                else v = isb[idx] ? BARREL : SPACE;
                if (idx == bplayer)
                    v = (v == TARGET) ? PLAYERTARGET : PLAYER;
                grid[(oy + y) * gw + ox + x] = v;
            }
        sfree(isb); sfree(isg);
    }
    sfree(g); sfree(cand); sfree(goalset); sfree(bbox);
    return true;
}

/* ----------------------------------------------------------------------
 * Big boards.
 *
 * The alternative generators only give good puzzles on boards of modest
 * size: the searches of YASGen and Rooms run out of steam, the MCTS level
 * maker leaves unreachable islands and a single box, and the NetHack
 * style Boulders level is a structure of fixed size.  A big board is
 * therefore built as a chain of tiles, each made by the chosen generator
 * and each a puzzle of its own.  Between the tiles runs a one square
 * wide street; the tiles are linked in a snake-like order by carving a
 * tunnel out of one tile, a corridor along the street, and a tunnel into
 * the next one.  The player can leave a finished tile because the tunnel
 * starts in the part of the finished tile that is connected to every
 * square the player may stand on after pushing the last box, and enters
 * the next tile in the part where its own player would start.  Carving
 * only ever turns walls into floor, so the solution of every tile stays
 * valid.
 */

static bool sokoban_tile_gen(const game_params *params, random_state *rs,
                             unsigned char *grid)
{
    switch (params->gen) {
      case 1: return sokoban_gen_boulders(params, rs, grid);
      case 2: return sokoban_gen_mcts(params, rs, grid);
      case 3: return sokoban_gen_yasgen(params, rs, grid);
      case 4: return sokoban_gen_rooms(params, rs, grid);
      default: return false;
    }
}

/* mode 0: squares the player can walk on; mode 1: anything but a wall */
#define MO_PASS(c, m) ((m) ? (c) != WALL : \
                       ((c) != WALL && !IS_BARREL(c) && \
                        (c) != PIT && (c) != DEEP_PIT))

static const int mo_dx[4] = { 0, 1, 0, -1 }, mo_dy[4] = { -1, 0, 1, 0 };

static int mo_flood(const unsigned char *g, int w, int h, int start, int mode,
                    unsigned char *mark, int *q)
{
    int head = 0, tail = 0;
    memset(mark, 0, (size_t)w * h);
    if (start < 0 || !MO_PASS(g[start], mode)) return 0;
    mark[start] = 1; q[tail++] = start;
    while (head < tail) {
        int p = q[head++], x = p % w, y = p / w, k;
        for (k = 0; k < 4; k++) {
            int nx = x + mo_dx[k], ny = y + mo_dy[k], np;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            np = ny * w + nx;
            if (mark[np] || !MO_PASS(g[np], mode)) continue;
            mark[np] = 1; q[tail++] = np;
        }
    }
    return tail;
}

/*
 * Checks one generated tile and tidies it: when 'ring' is set the outer
 * ring becomes wall (a tile of a big board has to be closed), floor the
 * player could never get to (not even by pushing boxes) is filled with
 * wall.  A box or a goal that cannot be reached makes the tile unusable.
 * Returns the player's square, or -1.
 */
static int mo_tidy_tile(unsigned char *g, int w, int h, bool ring)
{
    int i, pl = -1, nfree = 0, ngoal = 0;
    unsigned char *mark = snewn(w * h, unsigned char);
    int *q = snewn(w * h, int);
    bool ok = true;

    for (i = 0; i < w * h; i++) {
        int x = i % w, y = i / w;
        if (ring && (x == 0 || y == 0 || x == w - 1 || y == h - 1)) {
            if (g[i] == SPACE) g[i] = WALL;
            else if (g[i] != WALL) ok = false;
        }
        if (IS_PLAYER(g[i])) {
            if (pl >= 0) ok = false;
            pl = i;
        }
    }
    if (pl < 0) ok = false;
    if (ok) {
        mo_flood(g, w, h, pl, 1, mark, q);
        for (i = 0; i < w * h; i++) {
            if (mark[i]) continue;
            if (g[i] == SPACE || g[i] == BARRELTARGET) g[i] = WALL;
            else if (g[i] != WALL) ok = false;
        }
    }
    if (ok) {
        for (i = 0; i < w * h; i++) {
            if (g[i] == BARREL) nfree++;
            if (g[i] == TARGET || g[i] == PIT || g[i] == PLAYERTARGET) ngoal++;
        }
        if (nfree < 1 || ngoal < 1) ok = false;
    }
    sfree(mark); sfree(q);
    return ok ? pl : -1;
}

/* the position in which every box stands on a goal (or fills a pit) */
static void mo_solved_state(const unsigned char *g, int n, unsigned char *s)
{
    int i;
    for (i = 0; i < n; i++) {
        switch (g[i]) {
          case BARREL: case PLAYER: case PIT: s[i] = SPACE; break;
          case TARGET: case PLAYERTARGET: s[i] = BARRELTARGET; break;
          default: s[i] = g[i]; break;
        }
    }
}

/*
 * Squares on which the player can stand after pushing the last box: next
 * to a goal, with the square behind it free so that the push was
 * possible.  Returns how many there are.
 */
static int mo_needed(const unsigned char *g, const unsigned char *s,
                     int w, int h, unsigned char *need)
{
    int i, k, n = 0;
    memset(need, 0, (size_t)w * h);
    for (i = 0; i < w * h; i++) {
        int x = i % w, y = i / w;
        if (g[i] != TARGET && g[i] != BARRELTARGET && g[i] != PIT &&
            g[i] != PLAYERTARGET) continue;
        for (k = 0; k < 4; k++) {
            int cx = x + mo_dx[k], cy = y + mo_dy[k];
            int px = cx + mo_dx[k], py = cy + mo_dy[k];
            if (px < 0 || py < 0 || px >= w || py >= h) continue;
            if (s[cy * w + cx] != SPACE || g[py * w + px] == WALL) continue;
            if (!need[cy * w + cx]) { need[cy * w + cx] = 1; n++; }
        }
    }
    return n;
}

#define MO_INF 1000000
#define MO_RING(X, Y) ((X) == 0 || (Y) == 0 || (X) == w - 1 || (Y) == h - 1)

/*
 * Finds the cheapest route from a square of the region src to a square
 * of the outer ring of the tile on the given side (0 top, 1 right, 2
 * bottom, 3 left).  The route may cross walls (cost 3, to be carved) and
 * squares of walk (cost 1); other ring squares are never used.  Returns
 * the number of squares in path[], the ring square first, or 0.
 */
static int mo_route(const unsigned char *g, int w, int h,
                    const unsigned char *src, const unsigned char *walk,
                    const unsigned char *forbid, int side, int want,
                    random_state *rs, int *path)
{
    int N = w * h, i, k, best = -1, bestscore = 0, n = 0;
    int *dist = snewn(N, int), *prev = snewn(N, int);
    unsigned char *done = snewn(N, unsigned char);

#define MO_TARGET(X, Y) (side == 0 ? ((Y) == 0 && (X) >= 1 && (X) <= w - 2) : \
                         side == 2 ? ((Y) == h - 1 && (X) >= 1 && (X) <= w - 2) : \
                         side == 3 ? ((X) == 0 && (Y) >= 1 && (Y) <= h - 2) : \
                                     ((X) == w - 1 && (Y) >= 1 && (Y) <= h - 2))
    for (i = 0; i < N; i++) {
        dist[i] = src[i] ? 0 : MO_INF; prev[i] = -1; done[i] = 0;
    }
    for (;;) {
        int p = -1, x, y;
        for (i = 0; i < N; i++)
            if (!done[i] && dist[i] < MO_INF && (p < 0 || dist[i] < dist[p]))
                p = i;
        if (p < 0) break;
        done[p] = 1;
        x = p % w; y = p / w;
        if (MO_RING(x, y)) continue;          /* a ring square ends a route */
        for (k = 0; k < 4; k++) {
            int nx = x + mo_dx[k], ny = y + mo_dy[k], np, c;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            np = ny * w + nx;
            if (g[np] == WALL) {
                if (forbid && forbid[np]) continue;
                if (MO_RING(nx, ny) && !MO_TARGET(nx, ny)) continue;
                c = 3;
            } else if (walk[np]) c = 1;
            else continue;
            if (dist[p] + c < dist[np]) { dist[np] = dist[p] + c; prev[np] = p; }
        }
    }
    for (i = 0; i < N; i++) {
        int x = i % w, y = i / w, c, score;
        if (dist[i] >= MO_INF || !MO_RING(x, y) || !MO_TARGET(x, y)) continue;
        c = (side % 2 == 0) ? x : y;
        score = dist[i] * 2 +
                (want >= 0 ? abs(c - want)
                           : abs(c - ((side % 2 == 0 ? w : h) - 1) / 2)) +
                (int)random_upto(rs, 3);
        if (best < 0 || score < bestscore) { best = i; bestscore = score; }
    }
    if (best >= 0)
        for (i = best; i >= 0; i = prev[i]) path[n++] = i;
#undef MO_TARGET
    sfree(dist); sfree(prev); sfree(done);
    return n;
}

/*
 * In the finished position the free squares may fall apart into several
 * parts (a box standing on a goal in a corridor, say).  Carves walls so
 * that all the squares where the player may end up are in one part.
 * Returns the number of squares carved, or -1 if that takes too many.
 */
static int mo_join(unsigned char *g, unsigned char *s,
                   const unsigned char *need, int w, int h, int limit)
{
    int N = w * h, total = 0, i, k, result = 0;
    int *label = snewn(N, int), *dist = snewn(N, int), *prev = snewn(N, int);
    int *q = snewn(N, int);
    unsigned char *done = snewn(N, unsigned char), *mark = snewn(N, unsigned char);
    unsigned char *needlab = snewn(N + 1, unsigned char);

    for (;;) {
        int nl = 0, first = -1, A, nneed = 0, found = -1, p;
        for (i = 0; i < N; i++) label[i] = -1;
        for (i = 0; i < N; i++)
            if (label[i] < 0 && MO_PASS(s[i], 0)) {
                int cnt = mo_flood(s, w, h, i, 0, mark, q), j;
                for (j = 0; j < cnt; j++) label[q[j]] = nl;
                nl++;
            }
        memset(needlab, 0, (size_t)nl + 1);
        for (i = 0; i < N; i++)
            if (need[i] && label[i] >= 0) {
                if (first < 0) first = i;
                if (!needlab[label[i]]) { needlab[label[i]] = 1; nneed++; }
            }
        if (first < 0) { result = -1; break; }
        if (nneed <= 1) break;
        A = label[first];
        for (i = 0; i < N; i++) {
            dist[i] = (label[i] == A) ? 0 : MO_INF; prev[i] = -1; done[i] = 0;
        }
        for (;;) {
            int x, y;
            p = -1;
            for (i = 0; i < N; i++)
                if (!done[i] && dist[i] < MO_INF && (p < 0 || dist[i] < dist[p]))
                    p = i;
            if (p < 0) break;
            if (label[p] >= 0 && label[p] != A && needlab[label[p]]) { found = p; break; }
            done[p] = 1;
            x = p % w; y = p / w;
            for (k = 0; k < 4; k++) {
                int nx = x + mo_dx[k], ny = y + mo_dy[k], np, c;
                if (nx < 1 || ny < 1 || nx >= w - 1 || ny >= h - 1) continue;
                np = ny * w + nx;
                if (s[np] == WALL) c = 3;
                else if (MO_PASS(s[np], 0)) c = 1;
                else continue;
                if (dist[p] + c < dist[np]) { dist[np] = dist[p] + c; prev[np] = p; }
            }
        }
        if (found < 0) { result = -1; break; }
        for (i = found; i >= 0; i = prev[i])
            if (s[i] == WALL) { s[i] = g[i] = SPACE; total++; }
        if (total > limit) { result = -1; break; }
    }
    sfree(label); sfree(dist); sfree(prev); sfree(q);
    sfree(done); sfree(mark); sfree(needlab);
    return result < 0 ? -1 : total;
}

struct mo_tile {
    int x0, y0, w, h;                  /* where it lies in the whole grid */
    unsigned char *g;
    unsigned char *need;               /* squares where the player may end */
    int pl;                            /* player's square, tile coordinates */
    int entry_ring, exit_ring;         /* squares of the ring, or -1 */
};

/*
 * Cuts the tunnels of one tile.  The way in leads to the region the
 * player can walk about in at the beginning; the way out starts in the
 * region of the finished position that holds every square the player
 * can stand on at the end.
 */
static bool mo_tile_doors(struct mo_tile *t, int entry_side, int entry_want,
                          int exit_side, random_state *rs)
{
    int w = t->w, h = t->h, N = w * h, i, k;
    unsigned char *mark = snewn(N, unsigned char), *src = snewn(N, unsigned char);
    unsigned char *walk = snewn(N, unsigned char);
    unsigned char *forbid = snewn(N, unsigned char), *s = snewn(N, unsigned char);
    int *q = snewn(N, int), *epath = snewn(N, int), *xpath = snewn(N, int);
    int ne = 0, nx = 0, ncarve = 0;
    bool ok = true;

    memset(forbid, 0, N);
    t->entry_ring = t->exit_ring = -1;
    if (exit_side >= 0) {
        int cnt;
        mo_solved_state(t->g, N, s);
        cnt = mo_needed(t->g, s, w, h, t->need);
        if (cnt == 0) ok = false;
        else if (mo_join(t->g, s, t->need, w, h, 6 + N / 20) < 0)
            ok = false;
    }
    if (ok && entry_side >= 0) {
        mo_flood(t->g, w, h, t->pl, 0, mark, q);
        for (i = 0; i < N; i++) {
            src[i] = mark[i];
            walk[i] = !mark[i] && MO_PASS(t->g[i], 0);
        }
        ne = mo_route(t->g, w, h, src, walk, NULL, entry_side, entry_want, rs, epath);
        if (!ne) ok = false;
        else {
            t->entry_ring = epath[0];
            for (i = 0; i < ne; i++) {
                int x = epath[i] % w, y = epath[i] / w;
                forbid[epath[i]] = 1;
                for (k = 0; k < 4; k++) {
                    int ax = x + mo_dx[k], ay = y + mo_dy[k];
                    if (ax >= 0 && ay >= 0 && ax < w && ay < h)
                        forbid[ay * w + ax] = 1;
                }
            }
        }
    }
    if (ok && exit_side >= 0) {
        int first = -1;
        for (i = 0; i < N; i++) if (t->need[i]) { first = i; break; }
        mo_flood(s, w, h, first, 0, mark, q);
        for (i = 0; i < N; i++) {
            src[i] = mark[i];
            walk[i] = !mark[i] && MO_PASS(s[i], 0);
        }
        nx = mo_route(t->g, w, h, src, walk, forbid, exit_side, -1, rs, xpath);
        if (!nx) ok = false;
        else t->exit_ring = xpath[0];
    }
    if (ok) {
        for (i = 0; i < ne; i++)
            if (t->g[epath[i]] == WALL) { t->g[epath[i]] = SPACE; ncarve++; }
        for (i = 0; i < nx; i++)
            if (t->g[xpath[i]] == WALL) { t->g[xpath[i]] = SPACE; ncarve++; }
    }
    sfree(mark); sfree(src); sfree(walk); sfree(forbid); sfree(s);
    sfree(q); sfree(epath); sfree(xpath);
    return ok;
}

#define MO_MAXSIDE 16
#define MO_MAXTILES (MO_MAXSIDE * MO_MAXSIDE)

/*
 * Largest size of a tile for each generator: "soft" is the size the
 * tiles are cut to when a board is divided, "hard" the biggest board that
 * is still made in one piece; min is the smallest tile the generator can
 * make.
 */
static void mo_limits(int gen, int *soft, int *hard, int *minw, int *minh)
{
    switch (gen) {
      case 1:  soft[0] = hard[0] = 28; soft[1] = 10; hard[1] = 16;
               *minw = 16; *minh = 9; break;   /* 28 by 16: the board of the original */
      case 2:  soft[0] = soft[1] = 14; hard[0] = hard[1] = 16;
               *minw = *minh = 8; break;       /* (a board up to 254 squares is not cut) */
      case 3:  soft[0] = soft[1] = 13; hard[0] = hard[1] = 14;
               *minw = *minh = 7; break;
      default: soft[0] = soft[1] = 14; hard[0] = hard[1] = 14;
               *minw = *minh = 8; break;       /* at most 4 by 4 rooms in a tile */
    }
}

/* cuts a length into tiles separated by one square; returns their number */
static int mo_split(int len, int soft, int hard, int minlen, int *out)
{
    int n = 1, i, avail;
    if (len > hard) {
        n = (len + 1 + soft) / (soft + 1);
        if (n > MO_MAXSIDE) n = MO_MAXSIDE;
        while (n > 1 && (len - (n - 1)) / n < minlen) n--;
    }
    avail = len - (n - 1);
    for (i = 0; i < n; i++) out[i] = avail / n + (i < avail % n ? 1 : 0);
    return n;
}

/* every tile in its finished state, then can the player still get to the
 * start of the next one? */
static bool mo_chain_check(const unsigned char *grid, int w, int h,
                           const struct mo_tile *tiles, int n)
{
    unsigned char *hg = snewn(w * h, unsigned char);
    unsigned char *mark = snewn(w * h, unsigned char);
    int *q = snewn(w * h, int);
    bool ok = true;
    int k, i, x, y;

    memcpy(hg, grid, (size_t)w * h);
    for (k = 1; k < n && ok; k++) {
        const struct mo_tile *a = &tiles[k - 1], *b = &tiles[k];
        int first = -1;
        for (y = 0; y < a->h; y++)
            for (x = 0; x < a->w; x++) {
                unsigned char *c = &hg[(a->y0 + y) * w + a->x0 + x];
                mo_solved_state(c, 1, c);
            }
        for (i = 0; i < a->w * a->h; i++)
            if (a->need[i]) { first = (a->y0 + i / a->w) * w + a->x0 + i % a->w; break; }
        mo_flood(hg, w, h, first, 0, mark, q);
        if (first < 0) ok = false;
        for (i = 0; ok && i < a->w * a->h; i++)
            if (a->need[i] && !mark[(a->y0 + i / a->w) * w + a->x0 + i % a->w])
                ok = false;
        if (ok && !mark[(b->y0 + b->pl / b->w) * w + b->x0 + b->pl % b->w])
            ok = false;
    }
    sfree(hg); sfree(mark); sfree(q);
    return ok;
}

#ifdef SOKOBAN_STATS
/* for the test programs only: the tiles of the last mosaic, in chain order */
int sokoban_dbg_ntiles;
int sokoban_dbg_tile[MO_MAXTILES][6];  /* x0, y0, w, h, player x, player y */
#endif

static bool sokoban_mosaic(const game_params *params, random_state *rs,
                           unsigned char *grid, int tx, int ty,
                           const int *tw, const int *th)
{
    int w = params->w, h = params->h, n = tx * ty, i, j, k, x, y;
    struct mo_tile tiles[MO_MAXTILES];
    int ti[MO_MAXTILES], tj[MO_MAXTILES];
    int xs[MO_MAXTILES + 1], ys[MO_MAXTILES + 1];
    long totalarea = 0, areas[MO_MAXTILES];
    clock_t t0 = clock();
    bool ok = true;
    int maxtry = params->gen == 2 ? 6 : params->gen == 3 ? 5 : 30;

    if (n > MO_MAXTILES) return false;
    xs[0] = 0; for (i = 0; i < tx; i++) xs[i + 1] = xs[i] + tw[i] + 1;
    ys[0] = 0; for (j = 0; j < ty; j++) ys[j + 1] = ys[j] + th[j] + 1;
    {
        /* a snake through the tiles, starting in a random corner and
         * running along rows or along columns */
        int flipx = random_upto(rs, 2), flipy = random_upto(rs, 2);
        int trans = random_upto(rs, 2), a, b;
        int na = trans ? tx : ty, nb = trans ? ty : tx;
        k = 0;
        for (a = 0; a < na; a++)
            for (b = 0; b < nb; b++) {
                int bb = (a % 2 == 0) ? b : nb - 1 - b;
                i = trans ? a : bb; j = trans ? bb : a;
                if (flipx) i = tx - 1 - i;
                if (flipy) j = ty - 1 - j;
                ti[k] = i; tj[k] = j;
                areas[k] = (long)(tw[i] - 2) * (th[j] - 2);
                totalarea += areas[k];
                k++;
            }
    }
    memset(tiles, 0, sizeof tiles);

    for (k = 0; k < n && ok; k++) {
        struct mo_tile *t = &tiles[k];
        int entry_side = -1, exit_side = -1, entry_want = -1, attempt;
        bool done = false;

        t->x0 = xs[ti[k]]; t->y0 = ys[tj[k]];
        t->w = tw[ti[k]]; t->h = th[tj[k]];
        t->g = snewn(t->w * t->h, unsigned char);
        t->need = snewn(t->w * t->h, unsigned char);
        memset(t->need, 0, (size_t)t->w * t->h);
        t->entry_ring = t->exit_ring = -1;
        if (k > 0) {
            entry_side = ti[k - 1] < ti[k] ? 3 : ti[k - 1] > ti[k] ? 1 :
                         tj[k - 1] < tj[k] ? 0 : 2;
            {
                const struct mo_tile *a = &tiles[k - 1];
                int r = a->exit_ring;
                entry_want = (entry_side % 2 == 1) ? r / a->w : r % a->w;
            }
        }
        if (k < n - 1)
            exit_side = ti[k + 1] > ti[k] ? 1 : ti[k + 1] < ti[k] ? 3 :
                        tj[k + 1] > tj[k] ? 2 : 0;
        for (attempt = 0; attempt < maxtry && !done; attempt++) {
            game_params sub = *params;
            sub.w = t->w; sub.h = t->h;
            if (n > 1) {
                /* the boxes asked for are shared out by area */
                long total = params->gen == 3 ? params->yg_goals :
                             params->gen == 4 ? params->rm_boxes : 0;
                if (total) {
                    long done_area = 0, i2, lo, hi, share;
                    for (i2 = 0; i2 < k; i2++) done_area += areas[i2];
                    lo = (total * done_area + totalarea / 2) / totalarea;
                    hi = (total * (done_area + areas[k]) + totalarea / 2) / totalarea;
                    share = hi - lo < 1 ? 1 : hi - lo;
                    if (params->gen == 3) sub.yg_goals = (int)share;
                    else sub.rm_boxes = (int)share;
                }
                if (params->gen == 1) {
                    int ms = params->bm_time > 0 ? params->bm_time * 1000 / n : 1000;
                    sub.tile_ms = ms < 300 ? 300 : ms > 5000 ? 5000 : ms;
                }
                if (params->gen == 2)
                    sub.tile_ms = params->mc_time > 0 ?
                        (params->mc_time * 1000 / n < 500 ? 500 : params->mc_time * 1000 / n) :
                        1000;
                if (params->gen == 3) {
                    /* the time of the whole board is shared out */
                    int ms = params->yg_time > 0 ? params->yg_time * 1000 / n : 1500;
                    sub.tile_ms = ms < 600 ? 600 : ms > 8000 ? 8000 : ms;
                }
            }
            if ((double)(clock() - t0) / CLOCKS_PER_SEC > 20.0 + 4.0 * n) break;
            if (!sokoban_tile_gen(&sub, rs, t->g)) continue;
            t->pl = mo_tidy_tile(t->g, t->w, t->h, true);
            if (t->pl < 0) continue;
            if (mo_tile_doors(t, entry_side, entry_want, exit_side, rs))
                done = true;
        }
        if (!done) ok = false;
    }

    if (ok) {
        for (i = 0; i < w * h; i++) grid[i] = WALL;
        for (k = 0; k < n; k++) {
            const struct mo_tile *t = &tiles[k];
            for (y = 0; y < t->h; y++)
                for (x = 0; x < t->w; x++) {
                    unsigned char c = t->g[y * t->w + x];
                    if (k > 0) {
                        if (c == PLAYER) c = SPACE;
                        else if (c == PLAYERTARGET) c = TARGET;
                    }
                    grid[(t->y0 + y) * w + t->x0 + x] = c;
                }
        }
        for (k = 0; k + 1 < n; k++) {
            const struct mo_tile *a = &tiles[k], *b = &tiles[k + 1];
            int ax = a->x0 + a->exit_ring % a->w, ay = a->y0 + a->exit_ring / a->w;
            int bx = b->x0 + b->entry_ring % b->w, by = b->y0 + b->entry_ring / b->w;
            if (ti[k + 1] != ti[k]) {              /* neighbours in a row */
                int gx = (ax + bx) / 2, y0 = ay < by ? ay : by, y1 = ay < by ? by : ay;
                if (abs(ax - bx) != 2) { ok = false; break; }
                for (y = y0; y <= y1; y++) grid[y * w + gx] = SPACE;
            } else {                                /* one below the other */
                int gy = (ay + by) / 2, x0 = ax < bx ? ax : bx, x1 = ax < bx ? bx : ax;
                if (abs(ay - by) != 2) { ok = false; break; }
                for (x = x0; x <= x1; x++) grid[gy * w + x] = SPACE;
            }
        }
        if (ok)
            ok = mo_chain_check(grid, w, h, tiles, n);
        if (ok) {
            unsigned char *mark = snewn(w * h, unsigned char);
            int *q = snewn(w * h, int), pl = tiles[0].y0 * w + tiles[0].x0 +
                (tiles[0].pl / tiles[0].w) * w + tiles[0].pl % tiles[0].w;
            mo_flood(grid, w, h, pl, 1, mark, q);
            for (i = 0; i < w * h; i++)
                if (!mark[i] && grid[i] != WALL) ok = false;
            sfree(mark); sfree(q);
        }
    }
#ifdef SOKOBAN_STATS
    sokoban_dbg_ntiles = ok ? n : 0;
    for (k = 0; ok && k < n; k++) {
        sokoban_dbg_tile[k][0] = tiles[k].x0; sokoban_dbg_tile[k][1] = tiles[k].y0;
        sokoban_dbg_tile[k][2] = tiles[k].w;  sokoban_dbg_tile[k][3] = tiles[k].h;
        sokoban_dbg_tile[k][4] = tiles[k].x0 + tiles[k].pl % tiles[k].w;
        sokoban_dbg_tile[k][5] = tiles[k].y0 + tiles[k].pl / tiles[k].w;
    }
#endif
    for (k = 0; k < n; k++) { sfree(tiles[k].g); sfree(tiles[k].need); }
    return ok;
}

static bool sokoban_external(const game_params *params, random_state *rs,
                             unsigned char *grid)
{
    int soft[2], hard[2], minw, minh, tw[MO_MAXTILES + 1], th[MO_MAXTILES + 1];
    int tx, ty, attempt;

#ifdef SOKOBAN_STATS
    sokoban_dbg_ntiles = 0;
#endif
    mo_limits(params->gen, soft, hard, &minw, &minh);
    if (params->gen == 2 && params->w * params->h <= 254) {
        tx = ty = 1;                   /* a board of the original app */
        tw[0] = params->w; th[0] = params->h;
    } else {
        tx = mo_split(params->w, soft[0], hard[0], minw, tw);
        ty = mo_split(params->h, soft[1], hard[1], minh, th);
    }
    if (tx == 1 && ty == 1) {
        for (attempt = 0; attempt < 3; attempt++) {
            if (!sokoban_tile_gen(params, rs, grid)) continue;
            if (mo_tidy_tile(grid, params->w, params->h, false) >= 0) return true;
        }
        return false;
    }
    return sokoban_mosaic(params, rs, grid, tx, ty, tw, th);
}

#ifdef SOKOBAN_STATS
int sokoban_fallbacks;                 /* counted by the test programs only */
#endif

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
    int w = params->w, h = params->h;
    char *desc;
    int desclen, descpos, descsize, prev, count;
    unsigned char *grid;
    int i, j;

    /*
     * FIXME: perhaps some more interesting means of choosing how
     * many moves to try?
     */
    grid = snewn(w*h, unsigned char);
    if (!(params->gen && sokoban_external(params, rs, grid))) {
#ifdef SOKOBAN_STATS
        if (params->gen) sokoban_fallbacks++;
#endif
        /*
         * Generate a few candidates and keep the one that uses the
         * most floor (fewest unused squares), then open up much of
         * the unused area next to the used floor so the level is not
         * a maze of thick walls. Extra floor never breaks the
         * reverse-play solution.
         */
        unsigned char *cand = snewn(w*h, unsigned char);
        int best = -1, c, pass, x, y;
        for (c = 0; c < 4; c++) {
            int used = 0;
            sokoban_generate(w, h, cand, w*h, false, rs);
            for (i = 0; i < w*h; i++)
                if (cand[i] != INITIAL && cand[i] != WALL) used++;
            if (used > best) { best = used; memcpy(grid, cand, w*h); }
        }
        sfree(cand);
        for (pass = 0; pass < 2; pass++)
            for (y = 1; y < h-1; y++)
                for (x = 1; x < w-1; x++) {
                    int k = y*w+x, d, adj = 0;
                    if (grid[k] != INITIAL) continue;
                    for (d = 0; d < 4; d++) {
                        int v = grid[(y+DY(d))*w + x+DX(d)];
                        if (v != INITIAL && v != WALL) adj = 1;
                    }
                    if (adj && random_upto(rs, 100) < 50)
                        grid[k] = SPACE;
                }
    }

    desclen = descpos = descsize = 0;
    desc = NULL;
    prev = -1;
    count = 0;
    for (i = 0; i < w*h; i++) {
        if (descsize < desclen + 40) {
            descsize = desclen + 100;
            desc = sresize(desc, descsize, char);
            desc[desclen] = '\0';
        }
        switch (grid[i]) {
          case INITIAL:
            j = 'w';                   /* FIXME: make some of these 's'? */
            break;
          case SPACE:
            j = 's';
            break;
          case WALL:
            j = 'w';
            break;
          case TARGET:
            j = 't';
            break;
          case BARREL:
            j = 'b';
            break;
          case BARRELTARGET:
            j = 'f';
            break;
          case DEEP_PIT:
            j = 'd';
            break;
          case PIT:
            j = 'p';
            break;
          case PLAYER:
            j = 'u';
            break;
          case PLAYERTARGET:
            j = 'v';
            break;
          default:
            j = '?';
            break;
        }
        assert(j != '?');
        if (j != prev) {
            desc[desclen++] = j;
            descpos = desclen;
            prev = j;
            count = 1;
        } else {
            count++;
            desclen = descpos + sprintf(desc+descpos, "%d", count);
        }
    }

    desc[desclen] = '\0';
    sfree(grid);

    return desc;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    int w = params->w, h = params->h;
    int area = 0;
    int nplayers = 0;

    while (*desc) {
        int c = *desc++;
        int n = 1;
        if (*desc && isdigit((unsigned char)*desc)) {
            n = atoi(desc);
            while (*desc && isdigit((unsigned char)*desc)) desc++;
        }

        area += n;

        if (c == PLAYER || c == PLAYERTARGET)
            nplayers += n;
        else if (c == INITIAL || c == SPACE || c == WALL || c == TARGET ||
                 c == PIT || c == DEEP_PIT || IS_BARREL(c))
            /* ok */;
        else
            return "Invalid character in game description";
    }

    if (area > w*h)
        return "Too much data in game description";
    if (area < w*h)
        return "Too little data in game description";
    if (nplayers < 1)
        return "No starting player position specified";
    if (nplayers > 1)
        return "More than one starting player position specified";

    return NULL;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    int w = params->w, h = params->h;
    game_state *state = snew(game_state);
    int i;

    state->p = *params;                /* structure copy */
    state->grid = snewn(w*h, unsigned char);
    state->px = state->py = -1;
    state->completed = false;

    i = 0;

    while (*desc) {
        int c = *desc++;
        int n = 1;
        if (*desc && isdigit((unsigned char)*desc)) {
            n = atoi(desc);
            while (*desc && isdigit((unsigned char)*desc)) desc++;
        }

        if (c == PLAYER || c == PLAYERTARGET) {
            state->py = i / w;
            state->px = i % w;
            c = IS_ON_TARGET(c) ? TARGET : SPACE;
        }

        while (n-- > 0)
            state->grid[i++] = c;
    }

    assert(i == w*h);
    assert(state->px != -1 && state->py != -1);

    return state;
}

static game_state *dup_game(const game_state *state)
{
    int w = state->p.w, h = state->p.h;
    game_state *ret = snew(game_state);

    ret->p = state->p;                 /* structure copy */
    ret->grid = snewn(w*h, unsigned char);
    memcpy(ret->grid, state->grid, w*h);
    ret->px = state->px;
    ret->py = state->py;
    ret->completed = state->completed;

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->grid);
    sfree(state);
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, const char **error)
{
    return NULL;
}

static bool game_can_format_as_text_now(const game_params *params)
{
    return true;
}

static char *game_text_format(const game_state *state)
{
    return NULL;
}

struct game_ui {
    int theme;     /* 0 = original, 1 = classic, 2..5 = Yoshi skins */
};

#ifndef SOKOBAN_TEST_THEME
#define SOKOBAN_TEST_THEME 0           /* only the test programs change this */
#endif

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);
    ui->theme = SOKOBAN_TEST_THEME;
    return ui;
}

static void free_ui(game_ui *ui)
{
    sfree(ui);
}

static config_item *get_prefs(game_ui *ui)
{
    config_item *ret = snewn(2, config_item);

    ret[0].name = "Graphics";
    ret[0].kw = "graphics";
    ret[0].type = C_CHOICES;
    ret[0].u.choices.choicenames = ":Original:Classic (bricks and crates):Yoshi New:Yoshi Classic+:Yoshi Classic:Yoshi Red";
    ret[0].u.choices.choicekws = ":original:classic:yoshi-new:yoshi-classic-plus:yoshi-classic:yoshi-red";
    ret[0].u.choices.selected = ui->theme;

    ret[1].name = NULL;
    ret[1].type = C_END;
    return ret;
}

static void set_prefs(game_ui *ui, const config_item *cfg)
{
    ui->theme = cfg[0].u.choices.selected;
}

static bool game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
    return newstate->completed && oldstate && !oldstate->completed;
}

struct game_drawstate {
    game_params p;
    int tilesize;
    bool started;
    int theme;
    unsigned short *grid;
};

#define PREFERRED_TILESIZE 32
#define TILESIZE (ds->tilesize)
#define BORDER    (TILESIZE)
#define HIGHLIGHT_WIDTH (TILESIZE / 10)
#define COORD(x)  ( (x) * TILESIZE + BORDER )
#define FROMCOORD(x)  ( ((x) - BORDER + TILESIZE) / TILESIZE - 1 )

/*
 * I'm going to need to do most of the move-type analysis in both
 * interpret_move and execute_move, so I'll abstract it out into a
 * subfunction. move_type() returns -1 for an illegal move, 0 for a
 * movement, and 1 for a push.
 */
static int move_type(const game_state *state, int dx, int dy)
{
    int w = state->p.w, h = state->p.h;
    int px = state->px, py = state->py;
    int nx, ny, nbx, nby;

    assert(dx >= -1 && dx <= +1);
    assert(dy >= -1 && dy <= +1);
    assert(dx || dy);

    nx = px + dx;
    ny = py + dy;

    /*
     * Disallow any move that goes off the grid.
     */
    if (nx < 0 || nx >= w || ny < 0 || ny >= h)
        return -1;

    /*
     * Examine the target square of the move to see whether it's a
     * space, a barrel, or a wall.
     */

    if (state->grid[ny*w+nx] == WALL ||
        state->grid[ny*w+nx] == PIT ||
        state->grid[ny*w+nx] == DEEP_PIT)
        return -1;                     /* this one's easy; just disallow it */

    if (IS_BARREL(state->grid[ny*w+nx])) {
        /*
         * This is a push move. For a start, that means it must not
         * be diagonal.
         */
        if (dy && dx)
            return -1;

        /*
         * Now find the location of the third square involved in
         * the push, and stop if it's off the edge.
         */
        nbx = nx + dx;
        nby = ny + dy;
        if (nbx < 0 || nbx >= w || nby < 0 || nby >= h)
            return -1;

        /*
         * That third square must be able to accept a barrel.
         */
        if (state->grid[nby*w+nbx] == SPACE ||
            state->grid[nby*w+nbx] == TARGET ||
            state->grid[nby*w+nbx] == PIT ||
            state->grid[nby*w+nbx] == DEEP_PIT) {
            /*
             * The push is valid.
             */
            return 1;
        } else {
            return -1;
        }
    } else {
        /*
         * This is just an ordinary move. We've already checked the
         * target square, so the only thing left to check is that a
         * diagonal move has a space on one side to have notionally
         * gone through.
         */
        if (dx && dy &&
            state->grid[(py+dy)*w+px] != SPACE &&
            state->grid[(py+dy)*w+px] != TARGET &&
            state->grid[py*w+(px+dx)] != SPACE &&
            state->grid[py*w+(px+dx)] != TARGET)
            return -1;

        /*
         * Otherwise, the move is valid.
         */
        return 0;
    }
}

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    int dx=0, dy=0;
    char *move;

    /*
     * Diagonal movement is supported as it is in NetHack: it's
     * for movement only (never pushing), and one of the two
     * squares adjacent to both the source and destination
     * squares must be free to move through. In other words, it
     * is only a shorthand for two orthogonal moves and cannot
     * change the nature of the actual puzzle game.
     */
    if (button == CURSOR_UP || button == (MOD_NUM_KEYPAD | '8'))
        dx = 0, dy = -1;
    else if (button == CURSOR_DOWN || button == (MOD_NUM_KEYPAD | '2'))
        dx = 0, dy = +1;
    else if (button == CURSOR_LEFT || button == (MOD_NUM_KEYPAD | '4'))
        dx = -1, dy = 0;
    else if (button == CURSOR_RIGHT || button == (MOD_NUM_KEYPAD | '6'))
        dx = +1, dy = 0;
    else if (button == (MOD_NUM_KEYPAD | '7'))
        dx = -1, dy = -1;
    else if (button == (MOD_NUM_KEYPAD | '9'))
        dx = +1, dy = -1;
    else if (button == (MOD_NUM_KEYPAD | '1'))
        dx = -1, dy = +1;
    else if (button == (MOD_NUM_KEYPAD | '3'))
        dx = +1, dy = +1;
    else if (button == LEFT_BUTTON)
    {
        if(x < COORD(state->px))
            dx = -1;
        else if (x > COORD(state->px + 1))
            dx = 1;
        if(y < COORD(state->py))
            dy = -1;
        else if (y > COORD(state->py + 1))
            dy = 1;
    }
    else
        return NULL;

    if((dx == 0) && (dy == 0))
        return(NULL);

    if (move_type(state, dx, dy) < 0)
        return NULL;

    move = snewn(2, char);
    move[1] = '\0';
    move[0] = '5' - 3*dy + dx;
    return move;
}

static game_state *execute_move(const game_state *state, const char *move)
{
    int w = state->p.w, h = state->p.h;
    int px = state->px, py = state->py;
    int dx, dy, nx, ny, nbx, nby, type, m, i;
    bool freebarrels, freetargets;
    game_state *ret;

    if (*move < '1' || *move == '5' || *move > '9' || move[1])
        return NULL;                   /* invalid move string */

    m = *move - '0';
    dx = (m + 2) % 3 - 1;
    dy = 2 - (m + 2) / 3;
    type = move_type(state, dx, dy);
    if (type < 0)
        return NULL;

    ret = dup_game(state);

    nx = px + dx;
    ny = py + dy;
    nbx = nx + dx;
    nby = ny + dy;

    if (type) {
        int b;

        /*
         * Push.
         */
        b = ret->grid[ny*w+nx];
        if (IS_ON_TARGET(b)) {
            ret->grid[ny*w+nx] = TARGET;
            b = DETARGETISE(b);
        } else
            ret->grid[ny*w+nx] = SPACE;

        if (ret->grid[nby*w+nbx] == PIT)
            ret->grid[nby*w+nbx] = SPACE;
        else if (ret->grid[nby*w+nbx] == DEEP_PIT)
            /* do nothing - the pit eats the barrel and remains there */;
        else if (ret->grid[nby*w+nbx] == TARGET)
            ret->grid[nby*w+nbx] = TARGETISE(b);
        else
            ret->grid[nby*w+nbx] = b;
    }

    ret->px = nx;
    ret->py = ny;

    /*
     * Check for completion. This is surprisingly complicated,
     * given the presence of pits and deep pits, and also the fact
     * that some Sokoban levels with pits have fewer pits than
     * barrels (due to providing spares, e.g. NetHack's). I think
     * the completion condition in fact must be that the game
     * cannot become any _more_ complete. That is, _either_ there
     * are no remaining barrels not on targets, _or_ there is a
     * good reason why any such barrels cannot be placed. The only
     * available good reason is that there are no remaining pits,
     * no free target squares, and no deep pits at all.
     */
    if (!ret->completed) {
        freebarrels = false;
        freetargets = false;
        for (i = 0; i < w*h; i++) {
            int v = ret->grid[i];

            if (IS_BARREL(v) && !IS_ON_TARGET(v))
                freebarrels = true;
            if (v == DEEP_PIT || v == PIT ||
                (!IS_BARREL(v) && IS_ON_TARGET(v)))
                freetargets = true;
        }

        if (!freebarrels || !freetargets)
            ret->completed = true;
    }

    return ret;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_compute_size(const game_params *params, int tilesize,
                              const game_ui *ui, int *x, int *y)
{
    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    struct { int tilesize; } ads, *ds = &ads;
    ads.tilesize = tilesize;

    *x = 2 * BORDER + 1 + params->w * TILESIZE;
    *y = 2 * BORDER + 1 + params->h * TILESIZE;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
}

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);
    int i;

    game_mkhighlight(fe, ret, COL_BACKGROUND, COL_HIGHLIGHT, COL_LOWLIGHT);

    ret[COL_OUTLINE * 3 + 0] = 0.0F;
    ret[COL_OUTLINE * 3 + 1] = 0.0F;
    ret[COL_OUTLINE * 3 + 2] = 0.0F;

    ret[COL_PLAYER * 3 + 0] = 0.0F;
    ret[COL_PLAYER * 3 + 1] = 1.0F;
    ret[COL_PLAYER * 3 + 2] = 0.0F;

    ret[COL_BARREL * 3 + 0] = 0.6F;
    ret[COL_BARREL * 3 + 1] = 0.3F;
    ret[COL_BARREL * 3 + 2] = 0.0F;

    ret[COL_TARGET * 3 + 0] = ret[COL_LOWLIGHT * 3 + 0];
    ret[COL_TARGET * 3 + 1] = ret[COL_LOWLIGHT * 3 + 1];
    ret[COL_TARGET * 3 + 2] = ret[COL_LOWLIGHT * 3 + 2];

    ret[COL_PIT * 3 + 0] = ret[COL_LOWLIGHT * 3 + 0] / 2;
    ret[COL_PIT * 3 + 1] = ret[COL_LOWLIGHT * 3 + 1] / 2;
    ret[COL_PIT * 3 + 2] = ret[COL_LOWLIGHT * 3 + 2] / 2;

    ret[COL_DEEP_PIT * 3 + 0] = 0.0F;
    ret[COL_DEEP_PIT * 3 + 1] = 0.0F;
    ret[COL_DEEP_PIT * 3 + 2] = 0.0F;

    ret[COL_TEXT * 3 + 0] = 1.0F;
    ret[COL_TEXT * 3 + 1] = 1.0F;
    ret[COL_TEXT * 3 + 2] = 1.0F;

    ret[COL_GRID * 3 + 0] = ret[COL_LOWLIGHT * 3 + 0];
    ret[COL_GRID * 3 + 1] = ret[COL_LOWLIGHT * 3 + 1];
    ret[COL_GRID * 3 + 2] = ret[COL_LOWLIGHT * 3 + 2];

    ret[COL_OUTLINE * 3 + 0] = 0.0F;
    ret[COL_OUTLINE * 3 + 1] = 0.0F;
    ret[COL_OUTLINE * 3 + 2] = 0.0F;

    for (i = 0; i < 3; i++) {
	ret[COL_WALL * 3 + i] = (3 * ret[COL_BACKGROUND * 3 + i] +
				 1 * ret[COL_HIGHLIGHT * 3 + i]) / 4;
    }

#define SETCOL(C, R, G, B) do { ret[(C)*3+0] = (R); ret[(C)*3+1] = (G); ret[(C)*3+2] = (B); } while (0)
    SETCOL(COL_C_FLOOR, 0.86F, 0.80F, 0.62F);
    SETCOL(COL_C_BRICK, 0.72F, 0.27F, 0.17F);
    SETCOL(COL_C_MORTAR, 0.35F, 0.14F, 0.10F);
    SETCOL(COL_C_CRATE, 0.80F, 0.55F, 0.20F);
    SETCOL(COL_C_CRATE_EDGE, 0.40F, 0.24F, 0.07F);
    SETCOL(COL_C_CRATE_OK, 0.45F, 0.70F, 0.25F);
    SETCOL(COL_C_GOAL, 0.80F, 0.10F, 0.10F);
    SETCOL(COL_C_SUIT, 0.15F, 0.25F, 0.75F);
    SETCOL(COL_C_SKIN, 0.98F, 0.80F, 0.62F);
    SETCOL(COL_T_PIT, 0.36F, 0.28F, 0.20F);
    SETCOL(COL_T_DEEP_PIT, 0.06F, 0.05F, 0.05F);
    SETCOL(COL_T_OUTLINE, 0.0F, 0.0F, 0.0F);
    SETCOL(COL_T_TEXT, 1.0F, 1.0F, 1.0F);
    SETCOL(COL_T_FLASH, 1.0F, 1.0F, 0.80F);
#undef SETCOL
    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    int w = state->p.w, h = state->p.h;
    struct game_drawstate *ds = snew(struct game_drawstate);
    int i;

    ds->tilesize = 0;
    ds->p = state->p;                  /* structure copy */
    ds->grid = snewn(w*h, unsigned short);
    for (i = 0; i < w*h; i++)
        ds->grid[i] = INVALID;
    ds->started = false;
    ds->theme = 0;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->grid);
    sfree(ds);
}

static void draw_tile_classic(drawing *dr, game_drawstate *ds, int tx, int ty,
                              int v, int flash)
{
    int T = TILESIZE, cx = tx + T/2, cy = ty + T/2;
    int bg = flash ? COL_T_FLASH : COL_C_FLOOR;

    clip(dr, tx+1, ty+1, T-1, T-1);
    draw_rect(dr, tx+1, ty+1, T-1, T-1, bg);

    if (v == WALL) {
        int r, rowh = T / 3;
        draw_rect(dr, tx+1, ty+1, T-1, T-1, COL_C_BRICK);
        for (r = 0; r < 3; r++) {
            int y0 = ty + 1 + r * rowh;
            if (r > 0)
                draw_line(dr, tx+1, y0, tx+T, y0, COL_C_MORTAR);
            if (r == 1) {
                draw_line(dr, tx + T/4, y0, tx + T/4, y0 + rowh, COL_C_MORTAR);
                draw_line(dr, tx + 3*T/4, y0, tx + 3*T/4, y0 + rowh, COL_C_MORTAR);
            } else {
                draw_line(dr, tx + T/2, y0, tx + T/2, y0 + rowh, COL_C_MORTAR);
            }
        }
    } else if (v == PIT || v == DEEP_PIT) {
        draw_circle(dr, cx, cy, T*3/7,
                    v == PIT ? COL_T_PIT : COL_T_DEEP_PIT, COL_T_OUTLINE);
    } else {
        if (IS_ON_TARGET(v) && !IS_BARREL(v)) {
            int d = T / 6, c[8];
            c[0] = cx; c[1] = cy - d;
            c[2] = cx + d; c[3] = cy;
            c[4] = cx; c[5] = cy + d;
            c[6] = cx - d; c[7] = cy;
            draw_polygon(dr, c, 4, COL_C_GOAL, COL_T_OUTLINE);
        }
        if (IS_BARREL(v)) {
            int m = T / 8, x0 = tx + 1 + m, y0 = ty + 1 + m, x1 = tx + T - m,
                y1 = ty + T - m, col = IS_ON_TARGET(v) ? COL_C_CRATE_OK : COL_C_CRATE;
            char str[2];
            draw_rect(dr, x0, y0, x1 - x0, y1 - y0, COL_C_CRATE_EDGE);
            draw_rect(dr, x0 + 2, y0 + 2, x1 - x0 - 4, y1 - y0 - 4, col);
            draw_line(dr, x0 + 2, y0 + 2, x1 - 2, y1 - 2, COL_C_CRATE_EDGE);
            draw_line(dr, x0 + 2, y1 - 2, x1 - 2, y0 + 2, COL_C_CRATE_EDGE);
            str[1] = '\0';
            str[0] = BARREL_LABEL(v);
            if (str[0])
                draw_text(dr, cx, cy, FONT_VARIABLE, T/2,
                          ALIGN_VCENTRE | ALIGN_HCENTRE, COL_T_TEXT, str);
        } else if (IS_PLAYER(v)) {
            draw_circle(dr, cx, cy + T/8, T/4, COL_C_SUIT, COL_T_OUTLINE);
            draw_circle(dr, cx, cy - T/5, T/6, COL_C_SKIN, COL_T_OUTLINE);
        }
    }

    unclip(dr);
    draw_update(dr, tx, ty, T, T);
}

/*
 * The "Yoshi" skins are 64x64 pictures (the original skin sheets, four
 * columns wide) which the front end keeps: the game only says which cell
 * of which sheet goes into the tile, and the front end scales it to the
 * exact pixel size. Cell = row * 4 + column.
 */
static const unsigned char yo_cell[10] = {
    0,      /* floor */
    4,      /* goal */
    8, 9,   /* wall, even and odd columns */
    12, 13, /* wall, odd rows (Yoshi New only) */
    1,      /* player */
    5,      /* player on a goal */
    2,      /* box */
    6       /* box on a goal */
};

static void draw_tile_yoshi(drawing *dr, game_drawstate *ds, int x, int y,
                            int v, int flash)
{
    int T = TILESIZE, tx = COORD(x), ty = COORD(y), ti;

    if (v == WALL) ti = 2 + (x & 1) + (ds->theme == 2 ? ((y & 1) << 1) : 0);
    else if (IS_BARREL(v)) ti = IS_ON_TARGET(v) ? 9 : 8;
    else if (IS_PLAYER(v)) ti = IS_ON_TARGET(v) ? 7 : 6;
    else if (IS_ON_TARGET(v)) ti = 1;
    else ti = 0;

    if (!draw_sprite(dr, ds->theme - 2, yo_cell[ti], tx, ty, T, T)) {
        /* the front end has no pictures: draw the Classic graphics */
        draw_tile_classic(dr, ds, tx, ty, v, flash);
        return;
    }
    if (v == PIT || v == DEEP_PIT || (IS_BARREL(v) && BARREL_LABEL(v)) || flash) {
        clip(dr, tx, ty, T, T);
        if (v == PIT || v == DEEP_PIT)
            draw_circle(dr, tx + T/2, ty + T/2, T*3/7,
                        v == PIT ? COL_T_PIT : COL_T_DEEP_PIT, COL_T_OUTLINE);
        if (IS_BARREL(v) && BARREL_LABEL(v)) {
            char str[2];
            str[0] = BARREL_LABEL(v); str[1] = '\0';
            draw_text(dr, tx + T/2, ty + T/2, FONT_VARIABLE, T/2,
                      ALIGN_VCENTRE | ALIGN_HCENTRE, COL_T_TEXT, str);
        }
        if (flash)
            draw_rect_outline(dr, tx + 1, ty + 1, T - 2, T - 2, COL_T_FLASH);
        unclip(dr);
    }
    draw_update(dr, tx, ty, T, T);
}

static void draw_tile(drawing *dr, game_drawstate *ds, int x, int y, int v)
{
    if (ds->theme >= 2) {
        draw_tile_yoshi(dr, ds, x, y, v & 0xFF, (v & 0x100) != 0);
        return;
    }
    if (ds->theme == 1) {
        draw_tile_classic(dr, ds, COORD(x), COORD(y), v & 0xFF, (v & 0x100) != 0);
        return;
    }
    int tx = COORD(x), ty = COORD(y);
    int bg = (v & 0x100 ? COL_HIGHLIGHT : COL_BACKGROUND);

    v &= 0xFF;

    clip(dr, tx+1, ty+1, TILESIZE-1, TILESIZE-1);
    draw_rect(dr, tx+1, ty+1, TILESIZE-1, TILESIZE-1, bg);

    if (v == WALL) {
	int coords[6];

        coords[0] = tx + TILESIZE;
        coords[1] = ty + TILESIZE;
        coords[2] = tx + TILESIZE;
        coords[3] = ty + 1;
        coords[4] = tx + 1;
        coords[5] = ty + TILESIZE;
        draw_polygon(dr, coords, 3, COL_LOWLIGHT, COL_LOWLIGHT);

        coords[0] = tx + 1;
        coords[1] = ty + 1;
        draw_polygon(dr, coords, 3, COL_HIGHLIGHT, COL_HIGHLIGHT);

        draw_rect(dr, tx + 1 + HIGHLIGHT_WIDTH, ty + 1 + HIGHLIGHT_WIDTH,
                  TILESIZE - 2*HIGHLIGHT_WIDTH,
		  TILESIZE - 2*HIGHLIGHT_WIDTH, COL_WALL);
    } else if (v == PIT) {
        draw_circle(dr, tx + TILESIZE/2, ty + TILESIZE/2,
                    TILESIZE*3/7, COL_PIT, COL_OUTLINE);
    } else if (v == DEEP_PIT) {
        draw_circle(dr, tx + TILESIZE/2, ty + TILESIZE/2,
                    TILESIZE*3/7, COL_DEEP_PIT, COL_OUTLINE);
    } else {
        if (IS_ON_TARGET(v)) {
            draw_circle(dr, tx + TILESIZE/2, ty + TILESIZE/2,
                        TILESIZE*3/7, COL_TARGET, COL_OUTLINE);
        }
        if (IS_PLAYER(v)) {
            draw_circle(dr, tx + TILESIZE/2, ty + TILESIZE/2,
                        TILESIZE/3, COL_PLAYER, COL_OUTLINE);
        } else if (IS_BARREL(v)) {
            char str[2];

            draw_circle(dr, tx + TILESIZE/2, ty + TILESIZE/2,
                        TILESIZE/3, COL_BARREL, COL_OUTLINE);
            str[1] = '\0';
            str[0] = BARREL_LABEL(v);
            if (str[0]) {
                draw_text(dr, tx + TILESIZE/2, ty + TILESIZE/2,
                          FONT_VARIABLE, TILESIZE/2,
                          ALIGN_VCENTRE | ALIGN_HCENTRE, COL_TEXT, str);
            }
        }
    }

    unclip(dr);
    draw_update(dr, tx, ty, TILESIZE, TILESIZE);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int w = state->p.w, h = state->p.h /*, wh = w*h */;
    int x, y;
    int flashtype;

    if (flashtime &&
	!((int)(flashtime * 3 / FLASH_LENGTH) % 2))
	flashtype = 0x100;
    else
	flashtype = 0;

    if (ui && ui->theme != ds->theme) {
        int i;
        ds->theme = ui->theme;
        ds->started = false;
        for (i = 0; i < w*h; i++)
            ds->grid[i] = INVALID;
    }

    /*
     * Initialise a fresh drawstate.
     */
    if (!ds->started) {
        if (ds->theme >= 1)
            draw_rect(dr, 0, 0, COORD(w) + BORDER, COORD(h) + BORDER,
                      COL_BACKGROUND);
	/*
	 * Draw the grid lines (not in the classic theme).
	 */
	if (ds->theme == 0) {
	    for (y = 0; y <= h; y++)
	        draw_line(dr, COORD(0), COORD(y), COORD(w), COORD(y),
		          COL_LOWLIGHT);
	    for (x = 0; x <= w; x++)
	        draw_line(dr, COORD(x), COORD(0), COORD(x), COORD(h),
		          COL_LOWLIGHT);
	}

	ds->started = true;
    }

    /*
     * Draw the grid contents.
     */
    for (y = 0; y < h; y++)
	for (x = 0; x < w; x++) {
            int v = state->grid[y*w+x];
            if (y == state->py && x == state->px) {
                if (v == TARGET)
                    v = PLAYERTARGET;
                else {
                    assert(v == SPACE);
                    v = PLAYER;
                }
            }

	    v |= flashtype;

	    if (ds->grid[y*w+x] != v) {
		draw_tile(dr, ds, x, y, v);
		ds->grid[y*w+x] = v;
	    }
	}

}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    return 0.0F;
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate, int dir, game_ui *ui)
{
    if (!oldstate->completed && newstate->completed)
        return FLASH_LENGTH;
    else
        return 0.0F;
}

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
}

static int game_status(const game_state *state)
{
    return state->completed ? +1 : 0;
}

static bool game_timing_state(const game_state *state, game_ui *ui)
{
    return true;
}

#ifndef NO_PRINTING
static void game_print_size(const game_params *params, const game_ui *ui,
                            float *x, float *y)
{
}

static void game_print(drawing *dr, const game_state *state, const game_ui *ui,
                       int tilesize)
{
}
#endif

#ifdef COMBINED
#define thegame sokoban
#endif


/* ---- Next-step hint: BFS over pushes (player position normalised). ---- */
static const int sk_dx[4] = {-1, 0, 1, 0}, sk_dy[4] = {0, -1, 0, 1};
#define SK_MOVECH(d) ((char)('5' - 3*sk_dy[d] + sk_dx[d]))

/* Flood-fill walkable region; prev[] = direction used to reach cell (-1 root, -2 unseen). */
static int sk_flood(const game_state *s, int *prev, int *queue)
{
    int w = s->p.w, h = s->p.h, n = w*h, i, qh = 0, qt = 0, d;
    game_state t = *s;
    for (i = 0; i < n; i++) prev[i] = -2;
    prev[s->py*w + s->px] = -1;
    queue[qt++] = s->py*w + s->px;
    while (qh < qt) {
        int c = queue[qh++];
        t.px = c % w; t.py = c / w;
        for (d = 0; d < 4; d++) {
            int nc;
            if (move_type(&t, sk_dx[d], sk_dy[d]) != 0) continue;
            nc = (t.py + sk_dy[d]) * w + t.px + sk_dx[d];
            if (prev[nc] != -2) continue;
            prev[nc] = d;
            queue[qt++] = nc;
        }
    }
    return qt;
}

static unsigned long long sk_hash(const game_state *s, int canon)
{
    unsigned long long h = 1469598103934665603ULL;
    int i, n = s->p.w * s->p.h;
    for (i = 0; i < n; i++) { h ^= s->grid[i]; h *= 1099511628211ULL; }
    h ^= (unsigned)canon; h *= 1099511628211ULL;
    h ^= h >> 29;
    return h ? h : 1;
}

static int sk_canon(const game_state *s, int *prev, int *queue)
{
    int qt = sk_flood(s, prev, queue), i, m = queue[0];
    for (i = 0; i < qt; i++) if (queue[i] < m) m = queue[i];
    return m;
}

#ifndef SK_MAXN
#define SK_MAXN 120000
#endif
#ifndef SK_HCAP_BITS
#define SK_HCAP_BITS 20
#endif
/* Simple deadlock: barrel off-target stuck in a wall corner. */
static bool sk_dead(const game_state *s)
{
    int w = s->p.w, h = s->p.h, x, y;
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
        int v = s->grid[y*w+x];
        bool wl, wr, wu, wd;
        if (!IS_BARREL(v) || IS_ON_TARGET(v)) continue;
        wl = x == 0 || s->grid[y*w+x-1] == WALL;
        wr = x == w-1 || s->grid[y*w+x+1] == WALL;
        wu = y == 0 || s->grid[(y-1)*w+x] == WALL;
        wd = y == h-1 || s->grid[(y+1)*w+x] == WALL;
        if ((wl || wr) && (wu || wd)) return true;
    }
    return false;
}
static int sk_heur(const game_state *s)
{
    int w = s->p.w, h = s->p.h, n = w*h, i, j, tot = 0;
    for (i = 0; i < n; i++) {
        int v = s->grid[i], best = 1000;
        if (!IS_BARREL(v) || IS_ON_TARGET(v)) continue;
        for (j = 0; j < n; j++) {
            int u = s->grid[j];
            if ((u == TARGET || u == PIT || u == DEEP_PIT)) {
                int d = abs(i%w - j%w) + abs(i/w - j/w);
                if (d < best) best = d;
            }
        }
        if (best == 1000) best = 0;
        tot += best;
    }
    return tot;
}
#ifndef SOKOBAN_HINT_SECONDS
#define SOKOBAN_HINT_SECONDS 6.0       /* the test programs may allow more */
#endif
static unsigned long long *sk_planh = NULL;
static char *sk_planm = NULL;
static int sk_plann = 0;

static char *hint_game(const game_state *orig, const game_state *curr,
                       const char **error)
{
    int w = curr->p.w, h = curr->p.h, n = w*h;
    unsigned long long hh = sk_hash(curr, 1 + curr->py*w + curr->px);
    int i, head, tail, hcap = 1 << SK_HCAP_BITS, found = -1;
    game_state **nodes;
    int *parent, *prev, *queue, *gcost, *heap, hn = 0, *key;
    char **mv;
    unsigned long long *hs;
    char buf[2];
    clock_t hint_t0;

    if (curr->completed) {
        *error = "The puzzle is already solved";
        return NULL;
    }
    for (i = 0; i < sk_plann; i++)
        if (sk_planh[i] == hh) { buf[0] = sk_planm[i]; buf[1] = 0; return dupstr(buf); }

    nodes = snewn(SK_MAXN, game_state *);
    parent = snewn(SK_MAXN, int);
    mv = snewn(SK_MAXN, char *);
    hs = snewn(hcap, unsigned long long);
    gcost = snewn(SK_MAXN, int); key = snewn(SK_MAXN, int); heap = snewn(SK_MAXN, int);
    prev = snewn(n, int);
    queue = snewn(n, int);
    memset(hs, 0, hcap * sizeof(*hs));
    nodes[0] = dup_game(curr); parent[0] = -1; mv[0] = NULL;
    {
        unsigned long long k = sk_hash(curr, sk_canon(curr, prev, queue));
        unsigned p = k & (hcap-1);
        hs[p] = k;
    }
    tail = 1; gcost[0] = 0; key[0] = 0; heap[hn++] = 0;
    hint_t0 = clock();
    while (hn > 0 && found < 0 &&
           (double)(clock() - hint_t0) / CLOCKS_PER_SEC < SOKOBAN_HINT_SECONDS) {
        game_state *s;
        {   /* pop min key */
            int top = heap[0], x = 0;
            heap[0] = heap[--hn];
            for (;;) {
                int l = 2*x+1, r = l+1, m = x;
                if (l < hn && key[heap[l]] < key[heap[m]]) m = l;
                if (r < hn && key[heap[r]] < key[heap[m]]) m = r;
                if (m == x) break;
                { int t = heap[x]; heap[x] = heap[m]; heap[m] = t; }
                x = m;
            }
            head = top;
        }
        s = nodes[head];
        int qt = sk_flood(s, prev, queue), qi;
        int *myprev = snewn(n, int);
        int *myq = snewn(n, int);
        memcpy(myprev, prev, n * sizeof(int));
        memcpy(myq, queue, qt * sizeof(int));
        for (qi = 0; qi < qt && found < 0; qi++) {
            int c = myq[qi], d;
            game_state t = *s;
            t.px = c % w; t.py = c / w;
            for (d = 0; d < 4 && found < 0; d++) {
                game_state *ns; char m[2]; unsigned long long k; unsigned p;
                int len = 0, cc = c, l2;
                char *path;
                if (move_type(&t, sk_dx[d], sk_dy[d]) != 1) continue;
                m[0] = SK_MOVECH(d); m[1] = 0;
                ns = execute_move(&t, m);
                if (!ns) continue;
                if (!ns->completed && sk_dead(ns)) { free_game(ns); continue; }
                k = sk_hash(ns, sk_canon(ns, prev, queue));
                p = k & (hcap-1);
                while (hs[p] && hs[p] != k) p = (p+1) & (hcap-1);
                if (hs[p] == k || tail >= SK_MAXN) { free_game(ns); continue; }
                hs[p] = k;
                /* walking path from s to c, then the push */
                while (myprev[cc] >= 0) {
                    int dd = myprev[cc];
                    len++;
                    cc = (cc / w - sk_dy[dd]) * w + cc % w - sk_dx[dd];
                }
                path = snewn(len + 2, char);
                path[len+1] = 0; path[len] = m[0];
                cc = c; l2 = len;
                while (myprev[cc] >= 0) {
                    int dd = myprev[cc];
                    path[--l2] = SK_MOVECH(dd);
                    cc = (cc / w - sk_dy[dd]) * w + cc % w - sk_dx[dd];
                }
                nodes[tail] = ns; parent[tail] = head; mv[tail] = path;
                gcost[tail] = gcost[head] + 1;
                key[tail] = gcost[tail] + 4 * sk_heur(ns);
                if (ns->completed) found = tail;
                {   int x = hn++;
                    heap[x] = tail;
                    while (x > 0 && key[heap[(x-1)/2]] > key[heap[x]]) {
                        int t = heap[x]; heap[x] = heap[(x-1)/2]; heap[(x-1)/2] = t;
                        x = (x-1)/2;
                    }
                }
                tail++;
            }
        }
        sfree(myprev); sfree(myq);
    }
    sfree(sk_planh); sfree(sk_planm); sk_planh = NULL; sk_planm = NULL; sk_plann = 0;
    if (found >= 0) {
        int t, total = 0, pos;
        char *all;
        for (t = found; parent[t] >= 0; t = parent[t]) total += strlen(mv[t]);
        all = snewn(total + 1, char);
        pos = total; all[pos] = 0;
        for (t = found; parent[t] >= 0; t = parent[t]) {
            int l = strlen(mv[t]);
            pos -= l; memcpy(all + pos, mv[t], l);
        }
        sk_planh = snewn(total, unsigned long long);
        sk_planm = all;
        sk_plann = total;
        {
            game_state *c = dup_game(curr);
            for (i = 0; i < total; i++) {
                char m[2] = { all[i], 0 };
                game_state *nx;
                sk_planh[i] = sk_hash(c, 1 + c->py*w + c->px);
                nx = execute_move(c, m);
                free_game(c);
                c = nx;
                if (!c) { sk_plann = i + 1; break; }
            }
            if (c) free_game(c);
        }
    }
    for (i = 0; i < tail; i++) { free_game(nodes[i]); sfree(mv[i]); }
    sfree(nodes); sfree(parent); sfree(mv); sfree(hs); sfree(prev); sfree(queue); sfree(gcost); sfree(key); sfree(heap);
    if (found < 0) {
        *error = "Search gave up";
        return NULL;
    }
    buf[0] = sk_planm[0]; buf[1] = 0;
    return dupstr(buf);
}

const struct game thegame = {
    "Sokoban", "games.sokoban", "sokoban",
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
    false, solve_game,
    false, game_can_format_as_text_now, game_text_format,
    get_prefs, set_prefs,
    new_ui,
    free_ui,
    NULL, /* encode_ui */
    NULL, /* decode_ui */
    NULL, /* game_request_keys */
    NULL, /* android_cursor_visibility */
    game_changed_state,
    NULL, /* current_key_label */
    interpret_move,
    execute_move,
    PREFERRED_TILESIZE, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_get_cursor_location,
    game_status,
#ifndef NO_PRINTING
    false, false, game_print_size, game_print,
#endif
    false,			       /* wants_statusbar */
    false, game_timing_state,
    0,				       /* flags */
    hint_game,
};

#ifdef SOKOBAN_TEST_INCLUDE
#include SOKOBAN_TEST_INCLUDE          /* test programs only: code that needs the internals */
#endif
