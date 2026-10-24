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
    NCOLOURS
};

struct game_params {
    int w, h;
    int gen;        /* 0 original, 1 boulders, 2 MCTS, 3 YASGen */
    /* Boulders generator */
    int bm_count, bm_attempts, bm_nodes;
    /* MCTS generator */
    int mc_time, mc_keep, mc_pick;
    /* YASGen generator */
    int yg_goals, yg_walls, yg_limit, yg_mode, yg_rand, yg_gens;
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
    p->bm_count = 0; p->bm_attempts = 150; p->bm_nodes = 500;
    p->mc_time = 0; p->mc_keep = 1; p->mc_pick = 1;
    p->yg_goals = 0; p->yg_walls = 25; p->yg_limit = 400;
    p->yg_mode = 0; p->yg_rand = 1; p->yg_gens = 8;
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

static const struct game_params sokoban_presets[] = {
    { 12, 10, 0, 0, 150, 500, 0, 1, 1, 0, 14, 400, 0, 1 },
    { 16, 12, 0, 0, 150, 500, 0, 1, 1, 0, 14, 400, 0, 1 },
    { 20, 16, 0, 0, 150, 500, 0, 1, 1, 0, 14, 400, 0, 1 },
};

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    game_params p, *ret;
    char *retname;
    char namebuf[80];

    if (i < 0 || i >= lenof(sokoban_presets))
	return false;

    p = sokoban_presets[i];
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
        if (params->gen < 0 || params->gen > 3) params->gen = 0;
        while (*string && isdigit((unsigned char)*string)) string++;
        while (*string) {
            char key = *string++;
            int v = atoi(string);
            while (*string && isdigit((unsigned char)*string)) string++;
            switch (key) {
              case 'c': params->bm_count = v; break;
              case 'a': params->bm_attempts = v; break;
              case 'n': params->bm_nodes = v; break;
              case 't': params->mc_time = v; break;
              case 'k': params->mc_keep = v; break;
              case 'p': params->mc_pick = v; break;
              case 'o': params->yg_goals = v; break;
              case 'w': params->yg_walls = v; break;
              case 'l': params->yg_limit = v; break;
              case 'm': params->yg_mode = v; break;
              case 'r': params->yg_rand = v; break;
              case 'e': params->yg_gens = v; break;
              default: break;
            }
        }
    }
}

static char *encode_params(const game_params *params, bool full)
{
    char data[256];

    sprintf(data, "%dx%d", params->w, params->h);
    if (params->gen) {
        game_params d;
        char *p;
        sokoban_gen_defaults(&d);
        sprintf(data + strlen(data), "g%d", params->gen);
        p = data + strlen(data);
        if (params->bm_count != d.bm_count) p += sprintf(p, "c%d", params->bm_count);
        if (params->bm_attempts != d.bm_attempts) p += sprintf(p, "a%d", params->bm_attempts);
        if (params->bm_nodes != d.bm_nodes) p += sprintf(p, "n%d", params->bm_nodes);
        if (params->mc_time != d.mc_time) p += sprintf(p, "t%d", params->mc_time);
        if (params->mc_keep != d.mc_keep) p += sprintf(p, "k%d", params->mc_keep);
        if (params->mc_pick != d.mc_pick) p += sprintf(p, "p%d", params->mc_pick);
        if (params->yg_goals != d.yg_goals) p += sprintf(p, "o%d", params->yg_goals);
        if (params->yg_walls != d.yg_walls) p += sprintf(p, "w%d", params->yg_walls);
        if (params->yg_limit != d.yg_limit) p += sprintf(p, "l%d", params->yg_limit);
        if (params->yg_mode != d.yg_mode) p += sprintf(p, "m%d", params->yg_mode);
        if (params->yg_rand != d.yg_rand) p += sprintf(p, "r%d", params->yg_rand);
        if (params->yg_gens != d.yg_gens) p += sprintf(p, "e%d", params->yg_gens);
    }

    return dupstr(data);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret;
    char buf[80];
    int n = 0;

    ret = snewn(12, config_item);

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
    ret[n].u.choices.choicenames = ":Original:Boulders (NetHack style):MCTS:YASGen";
    ret[n].u.choices.selected = params->gen;
    n++;

    /* Only the settings of the generator in use are shown; reopen this
     * dialog after changing the generator to see the new settings. */
#define STR_ITEM(NAME, VAL) do { \
        ret[n].name = NAME; ret[n].type = C_STRING; \
        sprintf(buf, "%d", VAL); ret[n].u.string.sval = dupstr(buf); n++; } while (0)
    if (params->gen == 1) {
        STR_ITEM("Boulders: number of boulders (0 = auto)", params->bm_count);
        STR_ITEM("Boulders: attempts", params->bm_attempts);
        STR_ITEM("Boulders: search nodes per attempt", params->bm_nodes);
    } else if (params->gen == 2) {
        STR_ITEM("MCTS: search time in seconds (0 = auto)", params->mc_time);
        STR_ITEM("MCTS: levels to keep", params->mc_keep);
        STR_ITEM("MCTS: level to use (1 = hardest ... levels to keep = easiest)", params->mc_pick);
    } else if (params->gen == 3) {
        ret[n].name = "YASGen: mode";
        ret[n].type = C_CHOICES;
        ret[n].u.choices.choicenames = ":Reverse (pull boxes from goals):Forward (push given boxes)";
        ret[n].u.choices.selected = params->yg_mode;
        n++;
        STR_ITEM("YASGen: number of goals/boxes (0 = auto)", params->yg_goals);
        STR_ITEM("YASGen: wall density in percent", params->yg_walls);
        STR_ITEM("YASGen: search limit (thousand positions)", params->yg_limit);
        STR_ITEM("YASGen: genetic generations", params->yg_gens);
        ret[n].name = "YASGen: random player start";
        ret[n].type = C_BOOLEAN;
        ret[n].u.boolean.bval = params->yg_rand != 0;
        n++;
    }
#undef STR_ITEM

    ret[n].name = NULL;
    ret[n].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);
    int shown = 0;

    ret->w = atoi(cfg[0].u.string.sval);
    ret->h = atoi(cfg[1].u.string.sval);
    ret->gen = cfg[2].u.choices.selected;
    sokoban_gen_defaults(ret);

    /* which generator's settings were displayed? */
    if (cfg[3].type != C_END && cfg[3].name) {
        if (!strncmp(cfg[3].name, "Boulders", 8)) shown = 1;
        else if (!strncmp(cfg[3].name, "MCTS", 4)) shown = 2;
        else if (!strncmp(cfg[3].name, "YASGen", 6)) shown = 3;
    }
    if (shown && shown == ret->gen) {
        if (shown == 1) {
            ret->bm_count = atoi(cfg[3].u.string.sval);
            ret->bm_attempts = atoi(cfg[4].u.string.sval);
            ret->bm_nodes = atoi(cfg[5].u.string.sval);
        } else if (shown == 2) {
            ret->mc_time = atoi(cfg[3].u.string.sval);
            ret->mc_keep = atoi(cfg[4].u.string.sval);
            ret->mc_pick = atoi(cfg[5].u.string.sval);
        } else {
            ret->yg_mode = cfg[3].u.choices.selected;
            ret->yg_goals = atoi(cfg[4].u.string.sval);
            ret->yg_walls = atoi(cfg[5].u.string.sval);
            ret->yg_limit = atoi(cfg[6].u.string.sval);
            ret->yg_gens = atoi(cfg[7].u.string.sval);
            ret->yg_rand = cfg[8].u.boolean.bval ? 1 : 0;
        }
    }

    return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
    if (params->w < 4 || params->h < 4)
	return "Width and height must both be at least 4";
    if (params->gen < 0 || params->gen > 3)
        return "Unknown level generator";
    if (params->bm_count < 0 || params->bm_count > 6)
        return "Number of boulders must be between 0 and 6";
    if (params->bm_attempts < 1 || params->bm_attempts > 2000)
        return "Boulders attempts must be between 1 and 2000";
    if (params->bm_nodes < 10 || params->bm_nodes > 5000)
        return "Boulders search nodes must be between 10 and 5000";
    if (params->mc_time < 0 || params->mc_time > 120)
        return "MCTS search time must be between 0 and 120 seconds";
    if (params->mc_keep < 1)
        return "MCTS levels to keep must be at least 1";
    if (params->mc_pick < 1 || params->mc_pick > params->mc_keep)
        return "MCTS level to use must be between 1 and the number of levels to keep";
    if (params->yg_goals < 0 || params->yg_goals > 40)
        return "YASGen number of goals must be between 0 and 40";
    if (params->yg_mode < 0 || params->yg_mode > 1)
        return "Unknown YASGen mode";
    if (params->yg_walls < 0 || params->yg_walls > 50)
        return "YASGen wall density must be between 0 and 50 percent";
    if (params->yg_gens < 1 || params->yg_gens > 100)
        return "YASGen generations must be between 1 and 100";
    if (params->yg_limit < 10 || params->yg_limit > 2000)
        return "YASGen search limit must be between 10 and 2000 thousand positions";

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

/* MCTS generator (lkseg/sokoban-level-generator-mcts). */
static bool sokoban_gen_mcts(const game_params *params, random_state *rs,
                             unsigned char *grid)
{
    int w = params->w, h = params->h, bw = w, bh = h, x, y, ox, oy;
    unsigned long long seed;
    char *buf;
    bool ok = false;
    const char *p;

    while (bw * bh > 254) {
        if (bw >= bh) bw--; else bh--;
    }
    while (bw * bh < 16) {
        if (bw <= bh) bw++; else bh++;
    }
    if (bw > w || bh > h)
        return false;

    seed = ((unsigned long long)random_bits(rs, 31) << 32) |
           random_bits(rs, 31);
    buf = snewn(bw * bh * 2 + 16, char);
    if (!sokoban_mcts_generate(bw, bh, seed ? seed : 1,
                               params->mc_time > 0 ? (double)params->mc_time
                                                   : 2.0 + 0.02 * bw * bh,
                               params->mc_keep, params->mc_pick,
                               0,
                               buf, bw * bh * 2 + 16)) {
        sfree(buf);
        return false;
    }
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            grid[y*w+x] = WALL;
    ox = (w - bw) / 2;
    oy = (h - bh) / 2;
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
            grid[(y+oy)*w + x+ox] = c;
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

static bool sokoban_gen_boulders(const game_params *params, random_state *rs,
                                 unsigned char *grid)
{
    int w = params->w, h = params->h, attempt, bestDepth = -1, i;
    char *bestGrid = NULL;
    int nb = params->bm_count ? params->bm_count : w / 4;
    if (nb < 2) nb = 2;
    if (nb > 6) nb = 6;
    if (w < 10 || h < 6)
        return false;

    for (attempt = 0; attempt < params->bm_attempts; attempt++) {
        struct boulder_maker m;
        bool ok;
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
 * YASGen generator (Brian Damgaard's YASGen0, from YASC): we build a
 * random small room with goals, and YASGen searches for the starting
 * position of boxes that needs the most pushes.
 */
int sokoban_yasgen_generate(int w, int h, const char *rows, unsigned seed,
                            int megabytes, int max_open, int reverse,
                            int randplayer, char *out);

/* ---- genetic algorithm around the YASGen search (as in YASGen 1.50) ---- */

#define YG_POP 8

static bool yg_fix(char *c, int bw, int bh, int nb, int fwd, random_state *rs)
{
    int area = bw * bh, i, n, head, tail;
    int *q = snewn(area, int), *seen = snewn(area, int);
    int start = -1, np = 0;
    char gch = fwd ? '$' : '.';
    bool ok = true;

    for (i = 0; i < area; i++) {
        if (c[i] == '@') { if (start < 0) start = i; else c[i] = ' '; np++; }
        if (c[i] == '$' || c[i] == '.') c[i] = (c[i] == gch) ? c[i] : ' ';
        seen[i] = 0;
    }
    if (start < 0) {
        n = 0;
        for (i = 0; i < area; i++) if (c[i] != '#') n++;
        if (!n) { sfree(q); sfree(seen); return false; }
        n = random_upto(rs, n);
        for (i = 0; i < area; i++)
            if (c[i] != '#' && n-- == 0) { start = i; break; }
        c[start] = '@';
    }
    head = tail = 0; q[tail++] = start; seen[start] = 1;
    while (head < tail) {
        int p = q[head++], x = p % bw, y = p / bw;
        if (x > 0 && !seen[p-1] && c[p-1] != '#') { seen[p-1] = 1; q[tail++] = p-1; }
        if (x < bw-1 && !seen[p+1] && c[p+1] != '#') { seen[p+1] = 1; q[tail++] = p+1; }
        if (y > 0 && !seen[p-bw] && c[p-bw] != '#') { seen[p-bw] = 1; q[tail++] = p-bw; }
        if (y < bh-1 && !seen[p+bw] && c[p+bw] != '#') { seen[p+bw] = 1; q[tail++] = p+bw; }
    }
    if (tail < nb * 3 + 4) ok = false;
    if (ok) {
        int ng = 0;
        for (i = 0; i < area; i++)
            if (!seen[i]) c[i] = '#';
            else if (c[i] == gch) ng++;
        while (ng > nb) {
            int k = random_upto(rs, ng);
            for (i = 0; i < area; i++)
                if (c[i] == gch && k-- == 0) { c[i] = ' '; ng--; break; }
        }
        while (ng < nb) {
            int k = random_upto(rs, tail);
            for (i = 0; i < area; i++)
                if (seen[i] && c[i] == ' ' && k-- <= 0) { c[i] = gch; ng++; break; }
            if (i == area) continue;
        }
    }
    sfree(q); sfree(seen);
    return ok;
}

static void yg_random(char *c, int bw, int bh, int nb, int fwd, int wallpct,
                      random_state *rs)
{
    int area = bw * bh, i, tries = 0;
    do {
        for (i = 0; i < area; i++)
            c[i] = (random_upto(rs, 100) < (unsigned)wallpct) ? '#' : ' ';
        c[random_upto(rs, area)] = ' ';
        tries++;
        if (tries > 50) wallpct /= 2;
    } while (!yg_fix(c, bw, bh, nb, fwd, rs));
}

/* returns fitness (pushes, discounted for boxes that start on a goal) and
 * leaves the generated level in out */
static int yg_eval(const char *c, int bw, int bh, int nb, int fwd, int randp,
                   int maxopen, random_state *rs, char *out)
{
    int pushes = sokoban_yasgen_generate(bw, bh, c, random_bits(rs, 20), 32,
                                         maxopen, fwd ? 0 : 1, randp, out);
    int i, stars = 0;
    if (pushes <= 0) return 0;
    for (i = 0; i < bw * bh; i++) if (out[i] == '*' || out[i] == '+') stars++;
    {
        int floors = 0;
        for (i = 0; i < bw * bh; i++) if (c[i] != '#') floors++;
        /* prefer levels that use more of the board */
        return pushes * (bw * bh + floors) / (2 * bw * bh) / (1 + 4 * stars);
    }
}

static void yg_mutate(char *c, int bw, int bh, int fwd, random_state *rs)
{
    int area = bw * bh, k, p, q, t;
    char gch = fwd ? '$' : '.';
    for (k = 1 + random_upto(rs, 3); k > 0; k--) {
        t = random_upto(rs, 10);
        p = random_upto(rs, area);
        if (t < 5) {            /* flip wall / floor */
            if (c[p] == ' ') c[p] = '#';
            else if (c[p] == '#') c[p] = ' ';
        } else if (t < 9) {     /* move a goal/box */
            int tries;
            for (tries = 0; tries < 200 && c[p] != gch; tries++)
                p = random_upto(rs, area);
            if (c[p] != gch) continue;
            for (tries = 0; tries < 200; tries++) {
                q = random_upto(rs, area);
                if (c[q] == ' ') { c[q] = gch; c[p] = ' '; break; }
            }
        } else {                /* move the player */
            int tries;
            for (tries = 0; tries < 200 && c[p] != '@'; tries++)
                p = random_upto(rs, area);
            if (c[p] != '@') continue;
            for (tries = 0; tries < 200; tries++) {
                q = random_upto(rs, area);
                if (c[q] == ' ') { c[q] = '@'; c[p] = ' '; break; }
            }
        }
    }
}

static bool sokoban_gen_yasgen(const game_params *params, random_state *rs,
                               unsigned char *grid)
{
    int gw = params->w, gh = params->h;
    int bw = gw - 2, bh = gh - 2, x, y, i, g, a, b;
    int area, nb, fwd = params->yg_mode, maxopen;
    char *pop[YG_POP], *kid[YG_POP], *outs[YG_POP], *best, *work;
    int fit[YG_POP], kfit[YG_POP], order[YG_POP], bestfit = -1;
    clock_t t0; double budget;

    if (bw > 50) bw = 50;
    if (bh > 50) bh = 50;
    if (bw < 4 || bh < 4)
        return false;
    area = bw * bh;
    nb = params->yg_goals ? params->yg_goals : area / 16;
    if (!params->yg_goals) {
        if (nb < 3) nb = 3;
        if (nb > 6) nb = 6;
    }
    if (nb > area / 4) nb = area / 4;
    if (nb < 1) nb = 1;
    t0 = clock();
    budget = (double)params->yg_limit / 80.0;
    if (budget < 2) budget = 2;
    maxopen = params->yg_limit * 1000 / YG_POP;
    if (maxopen < 20000) maxopen = 20000;

    best = snewn(area, char);
    work = snewn(area, char);
    for (i = 0; i < YG_POP; i++) {
        pop[i] = snewn(area, char);
        kid[i] = snewn(area, char);
        outs[i] = snewn(area, char);
    }
    memset(best, '#', area);

    for (i = 0; i < YG_POP; i++) {
        yg_random(pop[i], bw, bh, nb, fwd, params->yg_walls, rs);
        fit[i] = yg_eval(pop[i], bw, bh, nb, fwd, params->yg_rand, maxopen,
                         rs, outs[i]);
    }
    for (g = 0; g <= params->yg_gens; g++) {
        /* rank: descending fitness */
        for (i = 0; i < YG_POP; i++) order[i] = i;
        for (a = 0; a < YG_POP; a++)
            for (b = a + 1; b < YG_POP; b++)
                if (fit[order[b]] > fit[order[a]]) {
                    int t = order[a]; order[a] = order[b]; order[b] = t;
                }
        if (fit[order[0]] > bestfit) {
            bestfit = fit[order[0]];
            memcpy(best, outs[order[0]], area);
        }
        if (g == params->yg_gens ||
            (double)(clock() - t0) / CLOCKS_PER_SEC > budget) break;
        /* elitism: two best go on, the rest are bred by rank selection */
        for (i = 0; i < YG_POP; i++) {
            if (i < 2) {
                memcpy(kid[i], pop[order[i]], area);
                kfit[i] = fit[order[i]];
                continue;
            }
            {
                int p1 = order[random_upto(rs, random_upto(rs, YG_POP) + 1)];
                int p2 = order[random_upto(rs, random_upto(rs, YG_POP) + 1)];
                int cut = 1 + random_upto(rs, area - 1), tries = 0;
                do {
                    for (x = 0; x < area; x++)
                        work[x] = (random_upto(rs, 100) < 70 ?
                                   (x < cut ? pop[p1][x] : pop[p2][x]) : pop[p1][x]);
                    yg_mutate(work, bw, bh, fwd, rs);
                    tries++;
                } while (!yg_fix(work, bw, bh, nb, fwd, rs) && tries < 30);
                if (tries >= 30) memcpy(work, pop[p1], area);
                memcpy(kid[i], work, area);
                kfit[i] = -1;
            }
        }
        {
            char *tmpout[YG_POP];
            for (i = 0; i < YG_POP; i++) tmpout[i] = snewn(area, char);
            for (i = 0; i < YG_POP; i++) {
                if (kfit[i] >= 0) {
                    /* elite: copy its level */
                    memcpy(tmpout[i], outs[order[i]], area);
                } else {
                    kfit[i] = yg_eval(kid[i], bw, bh, nb, fwd, params->yg_rand,
                                      maxopen, rs, tmpout[i]);
                }
            }
            for (i = 0; i < YG_POP; i++) {
                char *t;
                t = pop[i]; pop[i] = kid[i]; kid[i] = t;
                memcpy(outs[i], tmpout[i], area);
                fit[i] = kfit[i];
                sfree(tmpout[i]);
            }
        }
    }

    for (i = 0; i < YG_POP; i++) { sfree(pop[i]); sfree(kid[i]); sfree(outs[i]); }
    sfree(work);
    if (bestfit <= 0) {
        sfree(best);
        return false;
    }
    for (i = 0; i < gw * gh; i++) grid[i] = WALL;
    for (y = 0; y < bh; y++)
        for (x = 0; x < bw; x++) {
            int c;
            switch (best[y*bw + x]) {
              case ' ': c = SPACE; break;
              case '.': c = TARGET; break;
              case '$': c = BARREL; break;
              case '*': c = BARRELTARGET; break;
              case '@': c = PLAYER; break;
              case '+': c = PLAYERTARGET; break;
              default: c = WALL; break;
            }
            grid[(y + 1 + (gh - 2 - bh) / 2) * gw + x + 1 + (gw - 2 - bw) / 2] = c;
        }
    sfree(best);
    return true;
}

static bool sokoban_external(const game_params *params, random_state *rs,
                             unsigned char *grid)
{
    switch (params->gen) {
      case 1: return sokoban_gen_boulders(params, rs, grid);
      case 2: return sokoban_gen_mcts(params, rs, grid);
      case 3: return sokoban_gen_yasgen(params, rs, grid);
      default: return false;
    }
}

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
    int theme;                         /* 0 = original, 1 = classic */
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);
    ui->theme = 0;
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
    ret[0].u.choices.choicenames = ":Original:Classic (bricks and crates)";
    ret[0].u.choices.choicekws = ":original:classic";
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
    int bg = flash ? COL_HIGHLIGHT : COL_C_FLOOR;

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
                    v == PIT ? COL_PIT : COL_DEEP_PIT, COL_OUTLINE);
    } else {
        if (IS_ON_TARGET(v) && !IS_BARREL(v)) {
            int d = T / 6, c[8];
            c[0] = cx; c[1] = cy - d;
            c[2] = cx + d; c[3] = cy;
            c[4] = cx; c[5] = cy + d;
            c[6] = cx - d; c[7] = cy;
            draw_polygon(dr, c, 4, COL_C_GOAL, COL_OUTLINE);
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
                          ALIGN_VCENTRE | ALIGN_HCENTRE, COL_TEXT, str);
        } else if (IS_PLAYER(v)) {
            draw_circle(dr, cx, cy + T/8, T/4, COL_C_SUIT, COL_OUTLINE);
            draw_circle(dr, cx, cy - T/5, T/6, COL_C_SKIN, COL_OUTLINE);
        }
    }

    unclip(dr);
    draw_update(dr, tx, ty, T, T);
}

static void draw_tile(drawing *dr, game_drawstate *ds, int x, int y, int v)
{
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
        if (ds->theme == 1)
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

#define SK_MAXN 120000
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
static unsigned long long *sk_planh = NULL;
static char *sk_planm = NULL;
static int sk_plann = 0;

static char *hint_game(const game_state *orig, const game_state *curr,
                       const char **error)
{
    int w = curr->p.w, h = curr->p.h, n = w*h;
    unsigned long long hh = sk_hash(curr, 1 + curr->py*w + curr->px);
    int i, head, tail, hcap = 1 << 20, found = -1;
    game_state **nodes;
    int *parent, *prev, *queue, *gcost, *heap, hn = 0, *key;
    char **mv;
    unsigned long long *hs;
    char buf[2];

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
    while (hn > 0 && found < 0) {
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
