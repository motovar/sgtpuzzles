/*
 * 3d Maze....
 *
 * Version 2 - 15-Jan-2006 - Better Type selections (and fi )
 *
 * Written by Edward Macnaghten in 2006 (MIT licence, see LICENCE.Maze3D); this
 * is the version from the stppc2x collection by Lee Dowling
 * (https://github.com/ledow/stppc2x), a first-person maze in a cube of rooms.
 *
 * Android port: moved to the current puzzle API (const, bool, the new
 * signatures), and the faults found on the way were fixed - they are marked
 * "FIX" in the comments.  The state is still kept the way it was in the
 * original: there is one maze object that all the game states share (it
 * counts the references to it) and changed_state() takes it to the position
 * of the state that is shown, so undo, redo and restart work by moving the
 * maze there.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"
#include "maze3dc.h"

#define PREFERRED_TILE_SIZE 450
#define ANIM_TIME 0.1
#define INSTRUCTION_TEXT_SIZE 13

enum {
    COL_BACKGROUND,
    COL_WHITE,
    COL_LINE,
    COL_MARK,
    COL_TARGET,
    COL_THREAD,

    COL_N,
/*
    COL_S,
 */
/*
    COL_W,
 */
    COL_E,
    COL_U,
    COL_D,

/*
    COL_WALL,
    COL_FLOOR,
    COL_CIEL,
 */

    COL_FOG,

    NCOLOURS
};


enum {
  DISPLAY_NONE,
  DISPLAY_COORD,
  DISPLAY_DIRDIST
};


#define COL_S COL_N
#define COL_W COL_E

/*
#define COL_N COL_WALL
#define COL_S COL_WALL
#define COL_E COL_WALL
#define COL_W COL_WALL
#define COL_U COL_CIEL
#define COL_D COL_FLOOR
 */

/*
 * I do not use this at the moment
 * It is required to force a call of changed_state()
 *
 */
struct game_ui {
	int dummy;	/* Dummy variable */
};

struct game_params {
    int xx, yy, zz;	/* Dimensions of maze */
    int targetd, isdouble; /* Extra parameters, target dest and doubles allowed */
    int display;   /* What to display */
    int seed;		    /* The maze seed */
    int valid;	    /* A trick I do to use decode_params as a validator */
};

struct game_state {
	MAZE *maze;
	/*
	 * Using chars here to save memory space in BIG games
	 * a 5 x 5 x 5 has well over 250 moves
	 */
	char x, y, z;		/* Position */
	char dir;		/* Direction I am facing */
	char mark;		/* If marked  - 1 is marked 0 */
	char target;		/* 0 is no target, 1 is on it, 2 is one away facing it */
	long moves, rotations;	/* The counts of these */
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->xx = ret->yy = ret->zz = 4;	/* Not too difficult */
    ret->targetd = MAZE_CORNER;		/* Seems as goods as place as any */
    ret->isdouble = false;		/* Lets not do this by default :-) */
    ret->display = DISPLAY_DIRDIST;
    ret->valid = true;

    ret->seed = 0;

    return ret;
}

static const struct {
    const char *name;
    int xx, yy, zz, targetd, isdouble, display;
} maze3d_presets[] = {
    { "Easy (3x3x3)",                     3,  3,  3, MAZE_CORNER,   false, DISPLAY_DIRDIST },
    { "Basic (4x4x4)",                    4,  4,  4, MAZE_CORNER,   false, DISPLAY_DIRDIST },
    { "More Challenging (5x5x5)",         5,  5,  5, MAZE_CORNER,   false, DISPLAY_DIRDIST },
    { "Flat (8x8)",                       8,  8,  1, MAZE_CORNER,   false, DISPLAY_DIRDIST },
    { "Big flat (20x20)",                20, 20,  1, MAZE_CORNER,   false, DISPLAY_COORD },
    { "Difficult (5x5x5 no hints)",       5,  5,  5, MAZE_ANYWHERE, false, DISPLAY_COORD },
    { "Impossible (10x10x10 with doubles)", 10, 10, 10, MAZE_ANYWHERE, true, DISPLAY_NONE },
};

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *pars;
    if(i < 0 || i >= (int)lenof(maze3d_presets)) return false;

    pars = default_params();
    *params = pars;

    *name = dupstr(maze3d_presets[i].name);
    pars->xx = maze3d_presets[i].xx;
    pars->yy = maze3d_presets[i].yy;
    pars->zz = maze3d_presets[i].zz;
    pars->targetd = maze3d_presets[i].targetd;
    pars->isdouble = maze3d_presets[i].isdouble;
    pars->display = maze3d_presets[i].display;

    return true;
}

static void free_params(game_params *params)
{
    sfree(params);
}

static game_params *dup_params(const game_params *params)
{
    game_params *ret = snew(game_params);
    *ret = *params;		/* structure copy */
    return ret;
}

static void decode_params(game_params *params, char const *string)
{
	int mode;

	params->xx = 0;
	params->yy = 0;
	params->zz = 0;
	params->targetd = MAZE_CORNER;
	params->isdouble = false;
        params->display = DISPLAY_DIRDIST;
	params->seed = 0;
	params->valid = true;

	mode = 0;

	for(;*string;string++)
	{
		/* FIX: the numbers had no limit and overflowed for very long strings */
		switch(mode) {
		case 0:
			if(tolower((unsigned char)*string) == 'x')
				mode = 1;
			else if(!isdigit((unsigned char)*string))
			{
				mode = 3;
				string--;
			}
			else if(params->xx < 100000)
			{
				params->xx *= 10;
				params->xx += *string - '0';
			}
			break;
		case 1:
			if(tolower((unsigned char)*string) == 'x')
				mode = 2;
			else if(!isdigit((unsigned char)*string))
			{
				mode = 3;
				string--;
			}
			else if(params->yy < 100000)
			{
				params->yy *= 10;
				params->yy += *string - '0';
			}
			break;
		case 2:
			if(!isdigit((unsigned char)*string))
			{
				mode = 3;
				string--;
			}
			else if(params->zz < 100000)
			{
				params->zz *= 10;
				params->zz += *string - '0';
			}
			break;
		case 3:
			if(isdigit((unsigned char)*string))
			{
				mode = 4;
				string--;
			}
			else if (tolower((unsigned char)*string) == 'c')
				params->targetd = MAZE_CORNER;
			else if (tolower((unsigned char)*string) == 'e')
				params->targetd = MAZE_EDGE;
			else if (tolower((unsigned char)*string) == 'f')
				params->targetd = MAZE_FACE;
			else if (tolower((unsigned char)*string) == 'a')
				params->targetd = MAZE_ANYWHERE;
			else if (tolower((unsigned char)*string) == 'd')
				params->isdouble = true;
			else if (tolower((unsigned char)*string) == 's')
				params->isdouble = false;
			else if (tolower((unsigned char)*string) == 'n')
				params->display = DISPLAY_NONE;
			else if (tolower((unsigned char)*string) == 'l')
				params->display = DISPLAY_COORD;
			else if (tolower((unsigned char)*string) == 'w')
				params->display = DISPLAY_DIRDIST;
			break;
		case 4:
			if(!isdigit((unsigned char)*string))
			{
				params->valid = false;
				mode = 5;
			}
			else if(params->seed < 100000000)
			{
				params->seed *= 10;
				params->seed += *string - '0';
			}
			break;
		default:
			break;
		}
	}
	/*
	 * To place some defaults here
	 */

	if((!params->zz) && params->yy) params->zz = 1;
	if(!params->yy && params->xx)
	{
		if(params->zz)
			params->yy = 1;
		else
			params->yy = params->zz = params->xx;
	}
	if(!params->xx)
	{
		if(params->yy)
			params->xx = 1;
		else
			params->xx = params->yy = params->zz = 4;
	}
}

static char encode_target(int targetd)
{
	const char *s = "cefa";
	if(targetd < 0 || targetd > 3)
		return 'c';
	else
		return s[targetd];
}

static char encode_display(int displayi)
{
	const char *s = "nlw";
	if(displayi < 0 || displayi > 2)	/* FIX: it allowed 3, which is the NUL */
		return 'w';
	else
		return s[displayi];
}

/* The text form of the parameters and of a game: 4x4x4cws12345 */
static char *encode_maze(int xx, int yy, int zz, int targetd, int display, int isdouble, int seed)
{
	char buf[80];	/* FIX: the original used 20 characters, which is one short in the worst case */

	sprintf(buf, "%dx%dx%d%c%c%c%d",
		xx % 100, yy % 100, zz % 100,
		encode_target(targetd),
		encode_display(display),
		isdouble ? 'd' : 's',
		seed % 1000000000);
	return dupstr(buf);
}

static char *encode_params(const game_params *params, bool full)
{
	return encode_maze(params->xx, params->yy, params->zz,
			   params->targetd, params->display, params->isdouble,
			   full ? params->seed : 0);
}

static config_item *game_configure(const game_params *params)
{
    config_item *itms = snewn(7, config_item);
    config_item *itm = itms;
    char ws[32];

    memset(itms, 0, 7 * sizeof(config_item));

    itm->name = "Width";
    itm->type = C_STRING;
    sprintf(ws, "%d", params->xx);
    itm->u.string.sval = dupstr(ws);

    itm++;

    itm->name = "Length";
    itm->type = C_STRING;
    sprintf(ws, "%d", params->yy);
    itm->u.string.sval = dupstr(ws);

    itm++;

    itm->name = "Height";
    itm->type = C_STRING;
    sprintf(ws, "%d", params->zz);
    itm->u.string.sval = dupstr(ws);

    itm++;

    itm->name = "Target location";
    itm->type = C_CHOICES;
    itm->u.choices.choicenames = ":Corner:Edge:Face:Anywhere";
    itm->u.choices.selected = params->targetd;

    itm++;

    itm->name = "Double Routes/Circles";
    itm->type = C_BOOLEAN;
    itm->u.boolean.bval = params->isdouble ? true : false;

    itm++;

    itm->name = "Display";
    itm->type = C_CHOICES;
    itm->u.choices.choicenames = ":Nothing:Coordinates:Direction/Distance";
    itm->u.choices.selected = params->display;

    itm++;

    itm->type = C_END;


    return itms;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *pars = default_params();

    pars->xx = atoi(cfg[0].u.string.sval);
    pars->yy = atoi(cfg[1].u.string.sval);
    pars->zz = atoi(cfg[2].u.string.sval);

    pars->targetd  = cfg[3].u.choices.selected;
    pars->isdouble = cfg[4].u.boolean.bval;
    pars->display  = cfg[5].u.choices.selected;

    return pars;
}

static const char *validate_params(const game_params *params, bool full)
{
    if( ((params->xx < 2) || (params->yy < 3)) && ((params->xx < 3) || (params->yy < 2)) )
        return("Minimum size is 3 x 2 x 1");
    if( params->xx > MAX_DIM)
        return("Width must not be >" STR(MAX_DIM));
    if( params->yy > MAX_DIM)
        return("Length must not be >" STR(MAX_DIM));
    if( params->zz > MAX_DIM)
        return("Height must not be >" STR(MAX_DIM));
    if( params->xx < 2)
        return("Width must be at least 2.");
    if( params->yy < 2)
        return("Length must be at least 2.");
    if( params->zz < 1)
        return("Height must be at least 1.");

    return NULL;
}

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
	/*
	 * This does not create the maze
	 *
	 * instead I will try and use UI for that
	 */

	/*
	 * Format for desc - 2 char xx, t char yy, t char zz, 1 char targetd, 1 char isdouble, seed
	 */

	int seed = params->seed;

	/* If no seed then make one */

	if(!seed)
	{
		seed = 1 + random_upto(rs, 0xffffffe);
	}

	/* (The original also made negative sizes positive here, but that can't be
	 * got to - validate_params has had its say before.) */
	return encode_maze(params->xx, params->yy, params->zz,
			   params->targetd, params->display, params->isdouble,
			   seed);
}

static const char *validate_desc(const game_params *params, const char *desc)
{
	game_params *p = default_params();
	const char *ret = NULL;

	/* FIX: this only looked at the part after the seed; whatever it was given
	 * was taken, and what was wrong with the sizes only came out in new_game(),
	 * which quietly made it into a 3x2x1 maze. */
	if(!isdigit((unsigned char)*desc))
	{
		ret = "Format needs to be something like '4x4x4cws12345' - see docs for details";
	}
	else
	{
		decode_params(p, desc);
		if(!p->valid)
			ret = "Format needs to be something like '4x4x4cws12345' - see docs for details";
		else
			ret = validate_params(p, true);
	}
	free_params(p);
	return ret;
}


#define ng_dim_s(x) if(x < 1) x = 1; if (x > 20) x = 20

static game_state *new_game(midend *me, const game_params *params, const char *desc)
{

    int x, y, z, d, m, t;
    long moves, rots;
    game_state *state = snew(game_state);


    /*
     * I use the params to handle desc
     * So I will create my own copy  here
     */

    game_params *pars = default_params();

    decode_params(pars, desc);

    /*
     * To do some sanity checks and alterations
     */

    ng_dim_s(pars->xx);
    ng_dim_s(pars->yy);
    ng_dim_s(pars->zz);

    /* The minimum dimension is 1 * 2 * 3 */

    m = 0;
    if((pars->xx == 1 && pars->yy == 1) ||
	(pars->xx == 1 && pars->zz == 1) ||
	(pars->yy == 1 && pars->zz == 1)) m = -1;
    if(pars->xx <= 2 && pars->yy <= 2 && pars->zz <= 2) m = -1;
    if(m)
    {
	pars->xx = 3;
	pars->yy = 2;
	pars->zz = 1;
    }


    state->maze = m3d_new(pars->xx, pars->yy, pars->zz,
			    pars->targetd, pars->isdouble,
			    pars->seed);
    assert(state->maze);


    /*
     * getpos gets ints, I store chars, be careful!
     */
    m3d_getpos(state->maze, &x, &y, &z, &d, &m, &t, &moves, &rots);
    m3d_setmode(state->maze, pars->display);
    state->x = x;
    state->y = y;
    state->z = z;
    state->mark = 0;	/* A special value I will use for the first state */
    state->target = t;
    state->dir = d;
    state->moves = moves;
    state->rotations = rots;

    free_params(pars);

    return state;
}

static game_state *dup_game(const game_state *state)
{
    game_state *ret = snew(game_state);


    *ret = *state;	/* Structure copy */

    /* The following incrments reference count, and ret the same */
    ret->maze = m3d_copy(state->maze);

    return ret;
}

static void free_game(game_state *state)
{
    /* This decrements the count and frees on zero */
    m3d_free(state->maze);
    sfree(state);
}

/*
 * "Solve" shows (or hides) the thread - the line that leads to the target.
 */
static char *solve_game(const game_state *state, const game_state *currstate,
			const char *aux, const char **error)
{
	return dupstr("=");
}

static bool game_can_format_as_text_now(const game_params *params)
{
    return true;
}

static char *game_text_format(const game_state *state)
{
    return NULL;
}

/* game_ui needs to be defined else chnged_state is not called */
static game_ui *new_ui(const game_state *state)
{
   game_ui *ui = snew(game_ui);
   ui->dummy = 0;
   return ui;
}

static void free_ui(game_ui *ui)
{
	sfree(ui);
}

static char *encode_ui(const game_ui *ui)
{
	return dupstr("0");
}

static void decode_ui(game_ui *ui, const char *encoding, const game_state *state)
{
}

/*
 * The buttons that the keyboard of a touch screen shows: what can't be done by
 * tapping the picture (the arrow keys turn, Select goes forward and Select 2
 * puts or removes a mark).
 */
static key_label *game_request_keys(const game_params *params, int *nkeys, int *arrow_mode)
{
	static const struct { const char *label; int button; } keys[] = {
		{ "Fwd", ' ' }, { "Back", 'b' }, { "Turn", 't' },
		{ "Roll L", 'a' }, { "Roll R", 'c' }, { "Mark", 'm' }
	};
	int i;
	key_label *ret = snewn(lenof(keys), key_label);

	for(i = 0; i < (int)lenof(keys); i++)
	{
		ret[i].label = dupstr(keys[i].label);
		ret[i].button = keys[i].button;
		ret[i].needs_arrows = false;
	}
	*nkeys = lenof(keys);
	*arrow_mode = ANDROID_ARROWS_LEFT_RIGHT;
	return ret;
}

/*
 * The maze is shared by all the states of a game and remembers where we are
 * itself, so it has to be taken to the position of the state that is in use.
 * That is done when the state is shown (changed_state below), but also
 * before a move is made from a state and before one is drawn - FIX: the midend
 * doesn't call changed_state after it has loaded a saved game, and a saved
 * game can hold moves that were undone, so the maze was left at the last of
 * them and the next move was made from there (the state it gave didn't follow
 * the one before, or the midend didn't get a state at all).
 */
static void sync_maze(const game_state *state)
{
	m3d_setpos(state->maze, state->x, state->y, state->z, state->dir, state->mark,
			state->moves, state->rotations);
}

/*
 * The shared maze is taken to the position of the state that is shown.  (The
 * return value is for "this move completed the game", which is getting to the
 * target.)  The ui can be NULL here.
 */
static bool game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
	/*
	 * this simply updates the maze accordingly
	 *
	 * I think restarts are doing by running new_game again
	 */
	sync_maze(newstate);
	return newstate->target == 1 && oldstate && oldstate->target != 1;
}

struct game_drawstate {
    int tilesize;
    int targrad;
};

static char *interpret_move(const game_state *state, game_ui *ui, const game_drawstate *ds,
			    int x, int y, int button)
{
	char move[2];
	int mv = -1;

	/*
	 * The tilesize is the entire screen
	 *
	 * This assumes screen of 450 x 450
	 *
	 * FIX: this used to multiply by (450 / tilesize) in integers, which is
	 * 0 as soon as the picture is bigger than 450 pixels - everything was
	 * taken as a click in the top left corner.
	 */

	if(ds->tilesize > 0)
	{
		x = (int)((long)x * 450 / ds->tilesize);
		y = (int)((long)y * 450 / ds->tilesize);
	}

	switch(button & ~MOD_MASK) {
	case LEFT_BUTTON:	/* Left button - but WHERE */
		if     (  0 <= x && x < 100 &&   0 <= y && y < 100) mv = MAZE_ANTICLOCK;
		else if(350 <= x && x < 450 && 350 <= y && y < 450) mv = MAZE_ANTICLOCK;
		else if(  0 <= x && x < 100 && 350 <= y && y < 450) mv = MAZE_CLOCK;
		else if(350 <= x && x < 450 &&   0 <= y && y < 100) mv = MAZE_CLOCK;
		else if(100 <= x && x < 350 &&   0 <= y && y < 100) mv = MAZE_UP;
		else if(350 <= x && x < 450 && 100 <= y && y < 350) mv = MAZE_RIGHT;
		else if(100 <= x && x < 350 && 350 <= y && y < 450) mv = MAZE_DOWN;
		else if(  0 <= x && x < 100 && 100 <= y && y < 350) mv = MAZE_LEFT;
		else if(100 <= x && x < 350 && 100 <= y && y < 350) mv = MAZE_FORWARD;
		break;
	case RIGHT_BUTTON:
	case 'M':
	case 'm':
	case '0':
			mv = MAZE_MARK;
			break;
	case '8':
	case CURSOR_UP:
			mv = MAZE_UP;
			break;
	case '6':
	case CURSOR_RIGHT:
			mv = MAZE_RIGHT;
			break;
	case '2':
	case CURSOR_DOWN:
			mv = MAZE_DOWN;
			break;
	case '4':
	case CURSOR_LEFT:
			mv = MAZE_LEFT;
			break;
	case CURSOR_SELECT:
			mv = MAZE_FORWARD;
			break;
	case CURSOR_SELECT2:
			mv = MAZE_MARK;
			break;
	case '9':
	case '1':
	case 'c':
	case 'C':
			mv = MAZE_CLOCK;
			break;
	case '7':
	case '3':
	case 'a':
	case 'A':
			mv = MAZE_ANTICLOCK;
			break;
	case ' ':
	case '+':
	case '5':
			mv = MAZE_FORWARD;
			break;
	case 'B':
	case 'b':
	case '-':
			mv = MAZE_BACK;
			break;
	case 'T':
	case 't':
	case '*':
			mv = MAZE_TURN;
			break;
	default:
		return MOVE_UNUSED;
		break;
	}

	/* FIX: a click outside the picture made a move that did nothing (and was
	 * put into the history) - and so did a step into a wall. */
	if(mv < 0)
		return MOVE_UNUSED;
	if((mv == MAZE_FORWARD || mv == MAZE_BACK) &&
	   !m3d_can_step(state->maze, state->x, state->y, state->z, state->dir, mv == MAZE_BACK))
		return MOVE_NO_EFFECT;

	move[0] = mv + 'A';	/* Better than '0' in cas I need > 9 here sometime */
	move[1] = 0;
	return dupstr(move);
}

static game_state *execute_move(const game_state *state, const char *move)
{
	/*
	 * I will execute the move here, even though I zap to it in changed_state
	 * It is a bit inneficient but I do not see how else I can easily
	 * do this
	 */
	int mv;
	game_state *ret;
	int x, y, z, d, m, t;
	long moves, rots;
	long mode;

	if(!move[0] || move[1]) return NULL;

	/* FIX: see sync_maze() - the maze may not be where this state is */
	sync_maze(state);

	/*
	 * First - a special one to solve things
	 */

	if(*move == '=')
	{
		mode = m3d_getmode(state->maze);
		m3d_setmode(state->maze, (mode & 4) ? mode & ~4 : mode | 4);
	}
	else
	{
		mv = *move - 'A';

		switch(mv) {
		case MAZE_FORWARD:
		case MAZE_BACK:
			/* FIX: a step into a wall used to give a state that was the same as the one before */
			if(!m3d_can_step(state->maze, state->x, state->y, state->z, state->dir, mv == MAZE_BACK))
				return NULL;
			/* fall through */
		case MAZE_TURN:
		case MAZE_LEFT:
		case MAZE_RIGHT:
		case MAZE_UP:
		case MAZE_DOWN:
		case MAZE_MARK:
		case MAZE_CLOCK:
		case MAZE_ANTICLOCK:
			if(m3d_move(state->maze, mv))
				return NULL;
			break;
		default:
			/* FIX: anything else (and the click outside the picture) was taken as a valid move */
			return NULL;
		}
	}

	ret = dup_game(state);
	/* Use temp ints because of type conflicts */
	m3d_getpos(state->maze, &x, &y, &z, &d, &m, &t, &moves, &rots);
	ret->x = x;
	ret->y = y;
	ret->z = z;
	ret->mark = m;
	ret->dir = d;
	ret->target = t;
	ret->moves = moves;
	ret->rotations = rots;
	return ret;
}

/*
 * The hint: the next step on the shortest way to the target - a turn that
 * brings the right side to the front, a step forward, or a step back if the
 * way goes behind you.  (The yellow thread shown by Solve is that way too.)
 */
static char *hint_game(const game_state *orig, const game_state *curr,
		       const char **error)
{
	int mv;
	char buf[2];

	if(curr->target == 1)
	{
		*error = "You are at the target already";
		return NULL;
	}
	mv = m3d_hint_move(curr->maze, curr->x, curr->y, curr->z, curr->dir);
	if(mv < 0)
	{
		*error = "No way to the target found";
		return NULL;
	}
	buf[0] = mv + 'A';
	buf[1] = 0;
	return dupstr(buf);
}

static int game_status(const game_state *state)
{
	return state->target == 1 ? +1 : 0;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_compute_size(const game_params *params, int tilesize,
			      const game_ui *ui, int *x, int *y)
{

    *x = tilesize + (tilesize/3);
    *y = tilesize;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
			  const game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
}

#define RGB_R(r,i) (r[(i) * 3])
#define RGB_G(r,i) (r[((i) * 3) + 1])
#define RGB_B(r,i) (r[((i) * 3) + 2])
static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    RGB_R(ret, COL_WHITE) = 1.0;
    RGB_G(ret, COL_WHITE) = 1.0;
    RGB_B(ret, COL_WHITE) = 1.0;

    RGB_R(ret, COL_LINE) = 0.0;
    RGB_G(ret, COL_LINE) = 0.0;
    RGB_B(ret, COL_LINE) = 0.0;

    RGB_R(ret, COL_MARK) = 1.0;
    RGB_G(ret, COL_MARK) = 0.5;
    RGB_B(ret, COL_MARK) = 0.5;

    RGB_R(ret, COL_TARGET) = 1.0;
    RGB_G(ret, COL_TARGET) = 1.0;
    RGB_B(ret, COL_TARGET) = 0.0;

    RGB_R(ret, COL_THREAD) = 1.0;
    RGB_G(ret, COL_THREAD) = 1.0;
    RGB_B(ret, COL_THREAD) = 0.0;

    RGB_R(ret, COL_E) = 1.0;
    RGB_G(ret, COL_E) = 0.7;
    RGB_B(ret, COL_E) = 0.7;

/*
    RGB_R(ret, COL_S) = 1.0;
    RGB_G(ret, COL_S) = 0.7;
    RGB_B(ret, COL_S) = 0.7;
 */

/*
    RGB_R(ret, COL_W) = 0.7;
    RGB_G(ret, COL_W) = 1.0;
    RGB_B(ret, COL_W) = 0.7;
 */

    RGB_R(ret, COL_N) = 0.7;
    RGB_G(ret, COL_N) = 1.0;
    RGB_B(ret, COL_N) = 0.7;

    RGB_R(ret, COL_D) = 0.7;
    RGB_G(ret, COL_D) = 0.7;
    RGB_B(ret, COL_D) = 1.0;

    RGB_R(ret, COL_U) = 0.7;
    RGB_G(ret, COL_U) = 0.7;
    RGB_B(ret, COL_U) = 1.0;
/*
    RGB_R(ret, COL_WALL) = 1.0;
    RGB_G(ret, COL_WALL) = 0.7;
    RGB_B(ret, COL_WALL) = 0.7;

    RGB_R(ret, COL_FLOOR) = 0.7;
    RGB_G(ret, COL_FLOOR) = 1.0;
    RGB_B(ret, COL_FLOOR) = 0.7;

    RGB_R(ret, COL_CIEL) = 0.7;
    RGB_G(ret, COL_CIEL) = 1.00;
    RGB_B(ret, COL_CIEL) = 0.7;
 */

    RGB_R(ret, COL_FOG) = 0.7;
    RGB_G(ret, COL_FOG) = 0.7;
    RGB_B(ret, COL_FOG) = 0.7;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);

    ds->tilesize = 0;
    ds->targrad = 0;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds);
}


static void word_rect(drawing *dr, double sz, int left, int top, int width, int height, int col, const char *word);
static const char *letter_compass(int p);
static void mazedraw_rect(drawing *dr, double sz, int ax, int ay, int bx, int by, int col);
static void mazedraw_triangle(drawing *dr, double sz, int p1x, int p1y,  int p2x, int p2y, int p3x,int p3y, int col);
static void mazedraw_romb(drawing *dr, double sz, int p1x, int p1y, int p2x, int p2y, char orient, int col);
static void mazedraw_square(drawing *dr, double sz, int topleft,int col);
static void mazedraw_thread(drawing *dr, double sz, MAZEVIEW *view, int targ, int *d_l, int thread);
static int col_wall(int dir, MAZEVIEW *view);

#define SEE_DIST 8
#define scale(p) (int)((p) * sz)

static void game_redraw(drawing *dr, game_drawstate *ds, const game_state *oldstate,
			const game_state *state, int dir, const game_ui *ui,
			float animtime, float flashtime)
{
    /*
     * The initial contents of the window are not guaranteed and
     * can vary with front ends. To be on the safe side, all games
     * should start by drawing a big background-colour rectangle
     * covering the whole window.
     */
   /*             0             1          2          3            4          5              6         7 */
   /*             25     15    25     12    22     10    18    8    15     7    13        6    12   6   11      6 */
    int d_l[] = {0, 25,      40,  65,    77,  99,    109, 127,   135, 150,   157, 170,   176, 188,  194, 205,  209} ;
    int d_h[] = {450, 425,   410, 385,   373, 351,   341, 323,   315, 300,   293, 280,   274, 262,  256, 245,  241} ;
    int i,  a, b;

    int wl, wr, wu, wd, wf;
    int mk;
    int colline;
    int thread = -1;
    MAZEVIEW view;
    int targ=0;
    char buf[32];
    double sz = ((double)ds->tilesize) / 450.0;
    int mkl, mkh;



/*
 * Instructions - maybe remove at some point
 */

   const char *inst[] = {
	"Click on centre to",
	"move forward.",
	"Edges to rotate",
	"(pitch, yaw).",
	"Corners to spin",
	"(roll).",
	"Right click to mark."};

    sync_maze(state);		/* FIX: see sync_maze() */
    m3d_view(state->maze, &view);
   if(sz == 0) sz = 450;


    if(animtime != 0)
    {
    	if(ds->targrad)
	{
		int frame = (int)(animtime / ANIM_TIME);
		int radius = scale(225 - ds->targrad);
		if(frame % 2) 
			colline = COL_WHITE;
		else
			colline = COL_TARGET;
		draw_circle(dr, scale(225), scale(225), radius, colline, COL_LINE);
		
    		draw_update(dr, scale(225)-radius, scale(225)-radius, radius*2+1, radius*2+1);
	}
	return;
    }

    

    draw_rect(dr, 0, 0, scale(600), scale(450),  COL_BACKGROUND);
    draw_rect(dr, 0, 0, scale(450), scale(450),  COL_WHITE);

    draw_text(dr, scale(525), scale(20), FONT_VARIABLE, scale(INSTRUCTION_TEXT_SIZE), ALIGN_HCENTRE | ALIGN_VNORMAL, COL_LINE, inst[0]);
    draw_text(dr, scale(525), scale(36), FONT_VARIABLE, scale(INSTRUCTION_TEXT_SIZE), ALIGN_HCENTRE | ALIGN_VNORMAL, COL_LINE, inst[1]);
    draw_text(dr, scale(525), scale(56), FONT_VARIABLE, scale(INSTRUCTION_TEXT_SIZE), ALIGN_HCENTRE | ALIGN_VNORMAL, COL_LINE, inst[2]);
    draw_text(dr, scale(525), scale(72), FONT_VARIABLE, scale(INSTRUCTION_TEXT_SIZE), ALIGN_HCENTRE | ALIGN_VNORMAL, COL_LINE, inst[3]);
    draw_text(dr, scale(525), scale(92), FONT_VARIABLE, scale(INSTRUCTION_TEXT_SIZE), ALIGN_HCENTRE | ALIGN_VNORMAL, COL_LINE, inst[4]);
    draw_text(dr, scale(525), scale(108), FONT_VARIABLE, scale(INSTRUCTION_TEXT_SIZE), ALIGN_HCENTRE | ALIGN_VNORMAL, COL_LINE, inst[5]);
    draw_text(dr, scale(525), scale(128), FONT_VARIABLE, scale(INSTRUCTION_TEXT_SIZE), ALIGN_HCENTRE | ALIGN_VNORMAL, COL_LINE, inst[6]);
/*
    draw_text(dr, scale(525), scale(144), FONT_VARIABLE, scale(INSTRUCTION_TEXT_SIZE), ALIGN_HCENTRE | ALIGN_VNORMAL, COL_LINE, inst[7]);
 */

/*
 * Drawing colours - I will give it a go
 *
 * Start from the back and go in....
 */


/*
 * Some totals
 */

    draw_line(dr, scale(450), scale(135), scale(600), scale(135), COL_LINE);

    sprintf(buf, "%d", (int)view.moves);
    draw_text(dr, scale(460), scale(165), FONT_VARIABLE, scale(15), ALIGN_HLEFT | ALIGN_VNORMAL, COL_LINE, "Moves:");
    word_rect(dr, sz, 515, 145, 75, 20, COL_WHITE, buf);

    sprintf(buf, "%d", (int)view.rotations);
    draw_text(dr, scale(460), scale(195), FONT_VARIABLE, scale(15), ALIGN_HLEFT | ALIGN_VNORMAL, COL_LINE, "Turns:");
    word_rect(dr, sz, 515, 175, 75, 20, COL_WHITE, buf);

    sprintf(buf, "%d", (int)view.marks);
    draw_text(dr, scale(460), scale(225), FONT_VARIABLE, scale(15), ALIGN_HLEFT | ALIGN_VNORMAL, COL_LINE, "Marks:");
    word_rect(dr, sz, 515, 205, 75, 20, COL_WHITE, buf);

    draw_line(dr, scale(450), scale(235), scale(600), scale(235), COL_LINE);

    snprintf(buf, 30, "Size - %d x %d x %d", view.size_x, view.size_y, view.size_z);
    draw_text(dr, scale(525), scale(260), FONT_VARIABLE, scale(15), ALIGN_HCENTRE | ALIGN_VNORMAL, COL_LINE, buf);
/*
 * A direction pointer or coordinates
 */


    switch(view.mode & 3) {
    case DISPLAY_DIRDIST:
   
    	sprintf(buf, "%d", (int)view.howfar);
    	draw_text(dr, scale(460), scale(290), FONT_VARIABLE, scale(15), ALIGN_HLEFT | ALIGN_VNORMAL, COL_LINE, "Distance:");
    	word_rect(dr, sz, 535, 270, 55, 20, COL_WHITE, buf);
	

    	draw_text(dr, scale(460), scale(320), FONT_VARIABLE, scale(15), ALIGN_HLEFT | ALIGN_VNORMAL, COL_LINE, "Direction:");
    	word_rect(dr, sz, 535, 300, 55, 20, COL_WHITE, view.direction);

	break;
    case DISPLAY_COORD:
    	sprintf(buf, "%d-%d-%d", view.x, view.y, view.z);
    	draw_text(dr, scale(525), scale(290), FONT_VARIABLE, scale(15), ALIGN_HCENTRE | ALIGN_VNORMAL, COL_LINE, "Current location");
    	word_rect(dr, sz, 460, 295, 130, 20, COL_WHITE, buf);
	break;
    }
	


    draw_line(dr, scale(450), scale(330), scale(600), scale(330), COL_LINE);

   /*
    * A compass
    */

    if((view.mode & 4) && (!view.cell[0].target))
    {
    	thread = view.cell[0].d_thread;
    }

    draw_line(dr, scale(525), scale(360), scale(525), scale(420), COL_LINE);
    draw_line(dr, scale(495), scale(390), scale(555), scale(390), COL_LINE);
    draw_line(dr, scale(504), scale(369), scale(546), scale(411), COL_LINE);

    word_rect(dr, sz, (515), (340), (20), (20), thread == MAZE_D_U ? COL_THREAD : COL_WHITE, letter_compass(view.compass[MAZE_D_U]));
    word_rect(dr, sz, (515), (420), (20), (20), thread == MAZE_D_D ? COL_THREAD : COL_WHITE, letter_compass(view.compass[MAZE_D_D]));
    word_rect(dr, sz, (475), (380), (20), (20), thread == MAZE_D_L ? COL_THREAD : COL_WHITE, letter_compass(view.compass[MAZE_D_L]));
    word_rect(dr, sz, (555), (380), (20), (20), thread == MAZE_D_R ? COL_THREAD : COL_WHITE, letter_compass(view.compass[MAZE_D_R]));
    word_rect(dr, sz, (484), (349), (20), (20), thread == MAZE_D_F ? COL_THREAD : COL_WHITE, letter_compass(view.compass[MAZE_D_F]));
    word_rect(dr, sz, (546), (411), (20), (20), thread == MAZE_D_B ? COL_THREAD : COL_WHITE, letter_compass(view.compass[MAZE_D_B]));


   

    mazedraw_square(dr, sz, 0, col_wall(MAZE_D_F, &view));
   
    if(view.distance)
    {
    	if(view.distance > 8)
		a = 16;
    	else
		a = (view.distance * 2) - 1;

    	mazedraw_romb(dr, sz, 0, 0, d_l[a], d_l[a], 'H', col_wall(MAZE_D_U, &view));
    	mazedraw_romb(dr, sz, 450, 0, d_h[a], d_l[a], 'V', col_wall(MAZE_D_R, &view));
    	mazedraw_romb(dr, sz, 0, 450, d_l[a], d_h[a], 'H', col_wall(MAZE_D_D, &view));
    	mazedraw_romb(dr, sz, 0, 0, d_l[a], d_l[a], 'V', col_wall(MAZE_D_L, &view));
   }

    
    i = 0;
    a = 0;
    b = 1;

    

    for(;;)
    {
	wu = view.cell[i].wall[MAZE_D_U] ? -1 : 0;
	wd = view.cell[i].wall[MAZE_D_D] ? -1 : 0;
	wl = view.cell[i].wall[MAZE_D_L] ? -1 : 0;
	wr = view.cell[i].wall[MAZE_D_R] ? -1 : 0;
	wf = view.cell[i].wall[MAZE_D_F] ? -1 : 0;

	

        mk = view.cell[i].mark;

        if(mk)
		colline = COL_MARK;
	else
		colline = COL_LINE;

	if(view.cell[i].target)
        {
		targ = (d_l[a] + d_l[b]) / 2;
		ds->targrad = targ;
	}

	/* First the walls of the trajectories */
	if(!wu)
	{
		mazedraw_rect(dr, sz, d_l[b], d_l[a], d_h[b],  d_l[b], col_wall(MAZE_D_F, &view));
		mazedraw_triangle(dr, sz, d_l[a], d_l[a], d_l[b], d_l[b], d_l[b], d_l[a], col_wall(MAZE_D_L, &view));
		mazedraw_triangle(dr, sz, d_h[a], d_l[a], d_h[b], d_l[b], d_h[b], d_l[a], col_wall(MAZE_D_R, &view));
		draw_line(dr, scale(d_l[b]), scale(d_l[a]), scale(d_l[b]), scale(d_l[b]), COL_LINE);
		draw_line(dr, scale(d_h[b]), scale(d_l[a]), scale(d_h[b]), scale(d_l[b]), COL_LINE);
	}
	if(!wr)
	{
		mazedraw_rect(dr, sz, d_h[b], d_l[b], d_h[a],  d_h[b], col_wall(MAZE_D_F, &view));
		mazedraw_triangle(dr, sz, d_h[a], d_l[a], d_h[b], d_l[b], d_h[a], d_l[b], col_wall(MAZE_D_U, &view));
		mazedraw_triangle(dr, sz, d_h[a], d_h[a], d_h[b], d_h[b], d_h[a], d_h[b], col_wall(MAZE_D_D, &view));
		draw_line(dr, scale(d_h[a]), scale(d_l[b]), scale(d_h[b]), scale(d_l[b]), COL_LINE);
		draw_line(dr, scale(d_h[a]), scale(d_h[b]), scale(d_h[b]), scale(d_h[b]), COL_LINE);
	}
	if(!wd)
	{
		mazedraw_rect(dr, sz, d_l[b], d_h[b], d_h[b],  d_h[a], col_wall(MAZE_D_F, &view));
		mazedraw_triangle(dr, sz, d_h[a], d_h[a], d_h[b], d_h[b], d_h[b], d_h[a], col_wall(MAZE_D_R, &view));
		mazedraw_triangle(dr, sz, d_l[a], d_h[a], d_l[b], d_h[b], d_l[b], d_h[a], col_wall(MAZE_D_L, &view));
		draw_line(dr, scale(d_h[b]), scale(d_h[a]), scale(d_h[b]), scale(d_h[b]), COL_LINE);
		draw_line(dr, scale(d_l[b]), scale(d_h[a]), scale(d_l[b]), scale(d_h[b]), COL_LINE);
	}
	if(!wl)
	{
		mazedraw_rect(dr, sz, d_l[a], d_l[b], d_l[b],  d_h[b], col_wall(MAZE_D_F, &view));
		mazedraw_triangle(dr, sz, d_l[a], d_h[a], d_l[b], d_h[b], d_l[a], d_h[b], col_wall(MAZE_D_D, &view));
		mazedraw_triangle(dr, sz, d_l[a], d_l[a], d_l[b], d_l[b], d_l[a], d_l[b], col_wall(MAZE_D_U, &view));
		draw_line(dr, scale(d_l[a]), scale(d_h[b]), scale(d_l[b]), scale(d_h[b]), COL_LINE);
		draw_line(dr, scale(d_l[a]), scale(d_l[b]), scale(d_l[b]), scale(d_l[b]), COL_LINE);
	}

	/* If NOT the first draw front of the square */
	if(i)
	{
		/*
		 * This is technically if back = wall, but as back is always off (other we
		 * cannot see it) just check for wall
		 */
		if(!wu || mk) draw_line(dr, scale(d_l[a]), scale(d_l[a]), scale(d_h[a]), scale(d_l[a]), colline);
		if(!wr || mk) draw_line(dr, scale(d_h[a]), scale(d_l[a]), scale(d_h[a]), scale(d_h[a]), colline);
		if(!wd || mk) draw_line(dr, scale(d_h[a]), scale(d_h[a]), scale(d_l[a]), scale(d_h[a]), colline);
		if(!wl || mk) draw_line(dr, scale(d_l[a]), scale(d_h[a]), scale(d_l[a]), scale(d_l[a]), colline);
	}

	/* Now the corners */
	if(wl == wu || mk) draw_line(dr, scale(d_l[a]), scale(d_l[a]), scale(d_l[b]), scale(d_l[b]), colline);
	if(wu == wr || mk) draw_line(dr, scale(d_h[a]), scale(d_l[a]), scale(d_h[b]), scale(d_l[b]), colline);
	if(wr == wd || mk) draw_line(dr, scale(d_h[a]), scale(d_h[a]), scale(d_h[b]), scale(d_h[b]), colline);
	if(wd == wl || mk) draw_line(dr, scale(d_l[a]), scale(d_h[a]), scale(d_l[b]), scale(d_h[b]), colline);


	/* The edgets of the tradectories */
	if(wf == wu || mk) draw_line(dr, scale(d_l[b]), scale(d_l[b]), scale(d_h[b]), scale(d_l[b]), colline);
	if(wf == wr || mk) draw_line(dr, scale(d_h[b]), scale(d_l[b]), scale(d_h[b]), scale(d_h[b]), colline);
	if(wf == wd || mk) draw_line(dr, scale(d_h[b]), scale(d_h[b]), scale(d_l[b]), scale(d_h[b]), colline);
	if(wf == wl || mk) draw_line(dr, scale(d_l[b]), scale(d_h[b]), scale(d_l[b]), scale(d_l[b]), colline);

	if(mk)
	{
		if(!i)
		{
			mkh = 225;
			mkl = 225;
		}
		else
		{
			mkh = d_h[a];
			mkl = d_l[a];
		}
		if(wf)
		{
			draw_line(dr, scale(d_l[b]), scale(d_l[b]), scale(d_h[b]), scale(d_h[b]), colline);
			draw_line(dr, scale(d_l[b]), scale(d_h[b]), scale(d_h[b]), scale(d_l[b]), colline);
		}
		if(wu)
		{
			draw_line(dr, scale(mkl   ), scale(d_l[a]), scale(d_h[b]), scale(d_l[b]), colline);
			draw_line(dr, scale(d_l[b]), scale(d_l[b]), scale(mkh  ), scale(d_l[a]), colline);
		}
		if(wr)
		{
			draw_line(dr, scale(d_h[a]), scale(mkl   ), scale(d_h[b]), scale(d_h[b]), colline);
			draw_line(dr, scale(d_h[b]), scale(d_l[b]), scale(d_h[a]), scale(mkh   ), colline);
		}
		if(wd)
		{
			draw_line(dr, scale(mkl   ), scale(d_h[a]), scale(d_h[b]), scale(d_h[b]), colline);
			draw_line(dr, scale(d_l[b]), scale(d_h[b]), scale(mkh   ), scale(d_h[a]), colline);
		}
		if(wl)
		{
			draw_line(dr, scale(d_l[a]), scale(mkh   ), scale(d_l[b]), scale(d_l[b]), colline);
			draw_line(dr, scale(d_l[b]), scale(d_h[b]), scale(d_l[a]), scale(mkl   ), colline);
		}
	}


	if(!wf)
	{

		a++;
		b++;

		if(i == 7 && (!wf))
			mazedraw_square(dr, sz, d_l[a], COL_FOG);
		
		
		draw_line(dr, scale(d_l[a]), scale(d_l[a]), scale(d_l[b]), scale(d_l[b]), COL_LINE);
		draw_line(dr, scale(d_h[a]), scale(d_l[a]), scale(d_h[b]), scale(d_l[b]), COL_LINE);
		draw_line(dr, scale(d_h[a]), scale(d_h[a]), scale(d_h[b]), scale(d_h[b]), COL_LINE);
		draw_line(dr, scale(d_l[a]), scale(d_h[a]), scale(d_l[b]), scale(d_h[b]), COL_LINE);

	}


	if(wf) break;

	i++;
	if(i >= SEE_DIST) break;

	a++;
	b++;
    }

    if(targ)
	draw_circle(dr, scale(225), scale(225), scale(225 - targ), COL_TARGET, COL_LINE);

    if(thread > -1)
	mazedraw_thread(dr, sz, &view, targ, d_l, thread);
		
    draw_line(dr, scale(450), 0, scale(450), scale(450), COL_LINE);
    draw_update(dr, 0, 0, scale(600), scale(450));

}

static void mazedraw_thread(drawing *dr, double sz, MAZEVIEW *view, int targ, int *d_l, int thread)
{
	/*
	 * Simple ideas can get REALLY complex
	 *
	 * If there are doubles it is possible to see threads behind the current one.
	 * This routine is complex because of it.
	 *
	 * It is not perfect as the thread can still be seen even if it has already
	 * hit you, but if this is a problem then I need to use the "simple" method
	 * of only showing the first thread seen, whidh is OM, however as I am writing this
  	 * to be a smart arse I will show all threads.
	 */
	int i, j;
	/* FIX: +1, as the number of cells to see is 0 when there is a wall right in front */
	int top[view->distance + 1];
	int left[view->distance + 1];
	int width[view->distance + 1];
	int height[view->distance + 1];
	int across = 0;

    	int w_t[] = {20, 16, 13, 11, 10, 9, 8, 7, 6 };

	if(!targ)
	{
		for(i=0;i<view->distance;i++) top[i] = -1;
		j = 0;
   		for(i=0;i < view->distance;i++)
		{
			if(i > 7) break;
			switch(view->cell[i].d_thread) {
			case MAZE_D_B:
				across = -1;
				break;
			case MAZE_D_F:
				break;
			case MAZE_D_U:
				if(view->cell[i].wall[MAZE_D_D]) across = 0;
				left[j] = 225 - (w_t[i]/2);
				top[j] = d_l[i * 2];
				width[j] =  w_t[i];
				if(across)
					height[j] = (225 - d_l[i * 2]) *2;
				else
					height[j] = 225 - d_l[i * 2];
				across = -1;
				j++;
				break;
			case MAZE_D_R:
				if(view->cell[i].wall[MAZE_D_L]) across = 0;
				top[j] = 225 - (w_t[i]/2);
				height[j] = w_t[i];
				if(across)
				{
					left[j] = d_l[i * 2];
					width[j] = (225 - d_l[i * 2]) * 2;
				}
				else
				{
					left[j] = 225;
					width[j] = 225 - d_l[i * 2];
				}
				j++;
				across = -1;
				break;
			case MAZE_D_D:
				if(view->cell[i].wall[MAZE_D_U]) across = 0;
				left[j] = 225 - (w_t[i]/2);
				width[j] = w_t[i];
				if(across)
				{
					top [j] = d_l[i * 2];
					height[j] = (225 - d_l[i * 2]) *2;
				}
				else
				{
					top[j] = 225;
					height[j] =  225 - d_l[i * 2];
				}
				across = -1;
				j++;
				break;
			case MAZE_D_L:
				if(view->cell[i].wall[MAZE_D_R]) across = 0;
				left[j] = d_l[i * 2];
				top [j] = 225 - (w_t[i]/2);
				height[j] = w_t[i];
				if (across)
					width[j] = (225 - d_l[i * 2]) * 2;
				else
					width[j] = 225 - d_l[i * 2];
				across = -1;
				j++;
				break;
			}
		}
		while(j > 0)
		{
			j--;
			if(top[j] >= 0) word_rect(dr, sz, left[j], top[j], width[j], height[j], COL_THREAD, "");
		}
	}
    	if(thread == MAZE_D_F)
		draw_circle(dr, scale(225), scale(225), scale(w_t[0]/2), COL_THREAD, COL_LINE);
    }

static int col_wall(int dir, MAZEVIEW *view)
{
	switch(view->compass[dir]) {
	case MAZE_C_N: return COL_N; break;
	case MAZE_C_S: return COL_S; break;
	case MAZE_C_W: return COL_W; break;
	case MAZE_C_E: return COL_E; break;
	case MAZE_C_U: return COL_U; break;
	case MAZE_C_D: return COL_D; break;
	default: return COL_WHITE; break;
	}
	return COL_WHITE;
}
	


static void mazedraw_rect(drawing *dr, double sz, int ax, int ay, int bx, int by, int col)
{
	ax *= sz;
	ay *= sz;
	bx *= sz;
	by *= sz;

	draw_rect(dr, ax, ay, bx - ax + 1, by - ay + 1, col);
}
static void mazedraw_square(drawing *dr, double sz, int topleft,int col)
{
	int botright;

	botright = 450 - topleft;

	topleft *= sz;
	botright *= sz;
	
	/*
  	 * botright also width/height...
 	 */
	botright -= topleft;
	botright++;

	draw_rect(dr, topleft, topleft, botright, botright, col);
}


static void mazedraw_romb(drawing *dr, double sz, int p1x, int p1y, int p2x, int p2y, char orient, int col)
{
	int coords[8];
	int i;

	coords[0] = p1x;
	coords[1] = p1y;
	coords[2] = p2x;
	coords[3] = p2y;
	if(orient == 'H')
	{
		p1x = 450 - p1x;
		p2x = 450 - p2x;
	}
	else
	{
		p1y = 450 - p1y;
		p2y = 450 - p2y;
	}
	coords[6] = p1x;
	coords[7] = p1y;
	coords[4] = p2x;
	coords[5] = p2y;

	for(i=0;i<8;i++)
		coords[i] *= sz;

	draw_polygon(dr, coords, 4, col, col);
}

static void mazedraw_triangle(drawing *dr, double sz, int p1x, int p1y, int p2x, int p2y, int p3x,int p3y, int col)
{
	int coords[6];
	int i;

	coords[0] = p1x;
	coords[1] = p1y;
	coords[2] = p2x;
	coords[3] = p2y;
	coords[4] = p3x;
	coords[5] = p3y;

	/* FIX: the other shapes are scaled to the size of the picture but the
	 * triangles (the walls on the sides) were not */
	for(i=0;i<6;i++)
		coords[i] = (int)(coords[i] * sz);

	draw_polygon(dr, coords, 3, col, col);
}


static const char *letter_compass(int p)
{
	switch(p) {
	case MAZE_C_N: return "N"; break;
	case MAZE_C_S: return "S"; break;
	case MAZE_C_W: return "W"; break;
	case MAZE_C_E: return "E"; break;
	case MAZE_C_U: return "U"; break;
	case MAZE_C_D: return "D"; break;
	default: return "?"; break;
	}
}

	
static void word_rect(drawing *dr, double sz, int left, int top, int width, int height, int col, const char *word)
{
	left = scale(left);
	top = scale(top);
	width = scale(width);
	height = scale(height);

	draw_rect(dr, left, top, width, height, col);
	
	draw_line(dr, left, top, left+width, top, COL_LINE);
	draw_line(dr, left+width, top, left+width, top+height, COL_LINE);
	draw_line(dr, left+width, top+height, left, top+height, COL_LINE);
	draw_line(dr, left, top+height, left, top, COL_LINE);

	if(*word) draw_text(dr, left + (width/2), top + (height/2), FONT_FIXED, height-2, ALIGN_VCENTRE | ALIGN_HCENTRE, COL_LINE, word);
}

	
	

static float game_anim_length(const game_state *oldstate, const game_state *newstate,
			      int dir, game_ui *ui)
{
    if(newstate->target)
	return 1.0F;
    return 0.0F;
}

static float game_flash_length(const game_state *oldstate, const game_state *newstate,
			       int dir, game_ui *ui)
{
    return 0.0F;
}

static bool game_timing_state(const game_state *state, game_ui *ui)
{
    return true;
}

#ifdef COMBINED
#define thegame maze3d
#endif

const struct game thegame = {
    "Maze 3D", "games.maze3d", "maze3d",
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
    NULL, /* get_cursor_location */
    game_status,
#ifndef NO_PRINTING
    false, false, NULL, NULL,
#endif
    false,			       /* wants_statusbar */
    false, game_timing_state,
    REQUIRE_RBUTTON,	       /* flags */
    hint_game,
};
