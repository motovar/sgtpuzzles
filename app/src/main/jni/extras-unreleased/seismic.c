/*
 * seismic.c : Implementation of Hakyuu puzzles.
 * (C) 2013 Lennard Sprong
 * Created for Simon Tatham's Portable Puzzle Collection
 * See LICENCE for licence details
 *
 * Objective of the game: 
 * The grid is divided into regions. A region of size N should contain
 * one of each number between 1 and N. Identical numbers Z on the same
 * row/column must have at least Z other cells between them.
 *
 * This puzzle is also known as Hakyukoka or Ripple Effect.
 * https://www.nikoli.co.jp/en/puzzles/ripple_effect/
 */

/*
 * To do list:
 *
 * - Add symmetric clues
 *
 * - Optimize drawing routines
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"

#ifdef STANDALONE_SOLVER
bool solver_verbose = false;
#endif

#define DIFFLIST(A)                             \
	A(EASY,Easy, e)                             \
	A(HARD,Hard, h)                             \

#define ENUM(upper,title,lower) DIFF_ ## upper,
#define TITLE(upper,title,lower) #title,
#define ENCODE(upper,title,lower) #lower
#define CONFIG(upper,title,lower) ":" #title
enum { DIFFLIST(ENUM) DIFFCOUNT };
static char const *const seismic_diffnames[] = { DIFFLIST(TITLE) };

static char const seismic_diffchars[] = DIFFLIST(ENCODE);
#define DIFFCONFIG DIFFLIST(CONFIG)

enum {
	MODE_SEISMIC,
	MODE_TECTONIC
};

enum {
	COL_BACKGROUND,
	COL_HIGHLIGHT,
	COL_LOWLIGHT,
	COL_BORDER,
	COL_NUM_FIXED,
	COL_NUM_GUESS,
	COL_NUM_ERROR,
	COL_NUM_PENCIL,
	COL_ERRORDIST,
	COL_GRIDLINE,
	NCOLOURS
};

enum {
    PREF_PENCIL_KEEP_HIGHLIGHT,
    N_PREF_ITEMS
};

struct game_params {
	int w, h;
	int diff;
	int mode;
};

const static struct game_params seismic_presets[] = {
	{ 4,  4, DIFF_EASY, MODE_SEISMIC },
	{ 4,  4, DIFF_EASY, MODE_TECTONIC },
	{ 4,  4, DIFF_HARD, MODE_SEISMIC },
	{ 4,  4, DIFF_HARD, MODE_TECTONIC },
	{ 6,  6, DIFF_EASY, MODE_SEISMIC },
	{ 6,  6, DIFF_EASY, MODE_TECTONIC },
	{ 6,  6, DIFF_HARD, MODE_SEISMIC },
	{ 6,  6, DIFF_HARD, MODE_TECTONIC },
	{ 7,  7, DIFF_EASY, MODE_SEISMIC },
	{ 7,  7, DIFF_EASY, MODE_TECTONIC },
	{ 7,  7, DIFF_HARD, MODE_SEISMIC },
	{ 7,  7, DIFF_HARD, MODE_TECTONIC },
	{ 8,  8, DIFF_EASY, MODE_SEISMIC },
	{ 8,  8, DIFF_HARD, MODE_SEISMIC },
	{ 8,  8, DIFF_EASY, MODE_TECTONIC },
	{ 10, 10, DIFF_EASY, MODE_SEISMIC }
};

#define DEFAULT_PRESET 4

static game_params *default_params(void)
{
	game_params *ret = snew(game_params);

	*ret = seismic_presets[DEFAULT_PRESET];        /* structure copy */

	return ret;
}

static bool game_fetch_preset(int i, char **name, game_params **params)
{
	game_params *ret;
	char buf[80];

	if (i < 0 || i >= lenof(seismic_presets))
		return false;

	ret = snew(game_params);
	*ret = seismic_presets[i];     /* structure copy */

	sprintf(buf, "%s: %dx%d %s", ret->mode == MODE_SEISMIC ? "Seismic" : "Tectonic",
	        ret->w, ret->h, seismic_diffnames[ret->diff]);

	*name = dupstr(buf);
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
	*ret = *params;             /* structure copy */
	return ret;
}

static void decode_params(game_params *params, char const *string)
{
	char const *p = string;

	params->w = atoi(p);
	while (*p && isdigit((unsigned char)*p)) p++;
	if (*p == 'x') {
		p++;
		params->h = atoi(p);
		while (*p && isdigit((unsigned char)*p)) p++;
	} else {
		params->h = params->w;
	}

	if (*p == 'T')
	{
		params->mode = MODE_TECTONIC;
		p++;
	}

	if (*p == 'd') {
		int i;
		p++;
		params->diff = DIFFCOUNT + 1;   /* ...which is invalid */
		if (*p) {
			for (i = 0; i < DIFFCOUNT; i++) {
				if (*p == seismic_diffchars[i])
					params->diff = i;
			}
			p++;
		}
	}
}

static char *encode_params(const game_params *params, bool full)
{
	char buf[80];
	char *p = buf;

	p += sprintf(p, "%dx%d", params->w, params->h);
	if(params->mode == MODE_TECTONIC)
		p += sprintf(p, "T");
	if (full)
		p += sprintf(p, "d%c", seismic_diffchars[params->diff]);

	return dupstr(buf);
}

static config_item *game_configure(const game_params *params)
{
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

	ret[2].name = "Difficulty";
	ret[2].type = C_CHOICES;
	ret[2].u.choices.choicenames = DIFFCONFIG;
	ret[2].u.choices.selected = params->diff;

	ret[3].name = "Game mode";
	ret[3].type = C_CHOICES;
	ret[3].u.choices.choicenames = ":Seismic:Tectonic";
	ret[3].u.choices.selected = params->mode;

	ret[4].name = NULL;
	ret[4].type = C_END;

	return ret;
}

static game_params *custom_params(const config_item *cfg)
{
	game_params *ret = snew(game_params);

	ret->w = atoi(cfg[0].u.string.sval);
	ret->h = atoi(cfg[1].u.string.sval);
	ret->diff = cfg[2].u.choices.selected;
	ret->mode = cfg[3].u.choices.selected;

	return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
	if (params->w < 4 || params->h < 4)
		return "Width and height must be at least 4";
	if (params->diff >= DIFFCOUNT)
		return "Unknown difficulty rating";

	return NULL;
}

#define FM_FIXED     0x001
#define FM_ERRORDUP  0x002
#define FM_ERRORDIST 0x004

#define FM_ERRORMASK (FM_ERRORDUP|FM_ERRORDIST)

struct game_state {
	int w, h, mode;
	char *grid;
	char *flags;
	int *marks;
	DSF *dsf;
	
	bool completed, cheated;
};

static game_state *blank_state(int w, int h, int mode)
{
	game_state *state = snew(game_state);
	int s = w * h;

	state->w = w;
	state->h = h;
	state->mode = mode;
	state->grid = snewn(s, char);
	state->flags = snewn(s, char);
	state->marks = snewn(s, int);
	state->dsf = dsf_new(s);
	
	state->completed = state->cheated = false;

	memset(state->grid, 0, s*sizeof(char));
	memset(state->flags, 0, s*sizeof(char));
	memset(state->marks, 0, s*sizeof(int));

	return state;
}

static game_state *dup_game(const game_state *state)
{
	int w = state->w;
	int h = state->h;
	int s = w * h;
	game_state *ret = blank_state(w, h, state->mode);

	memcpy(ret->grid, state->grid, s*sizeof(char));
	memcpy(ret->flags, state->flags, s*sizeof(char));
	memcpy(ret->marks, state->marks, s*sizeof(int));
	dsf_copy(ret->dsf, state->dsf);
	
	ret->completed = state->completed;
	ret->cheated = state->cheated;

	return ret;
}

static void free_game(game_state *state)
{
	sfree(state->grid);
	sfree(state->flags);
	sfree(state->marks);
	dsf_free(state->dsf);
	sfree(state);
}

/* ****** *
 * Solver *
 * ****** */
#define NUM_BIT(x) ( 1 << ( (x) - 1 ) )
#define AREA_BITS(x) ( NUM_BIT((x)+1)-1 )
#define FM_MARKS AREA_BITS(9)

static int seismic_unset(game_state *state, int x, int y, char n)
{
	/* Remove one mark from a cell */
	
	int w = state->w;
	
	if(x < 0 || y < 0 || x >= w || y >= state->h)
		return 0;
	
	if(state->marks[y*w+x] & NUM_BIT(n))
	{
		state->marks[y*w+x] &= ~NUM_BIT(n);
		return 1;
	}
	return 0;
}

static int seismic_place_number(game_state *state, int x, int y, char n)
{
	/* Place a number in the grid, and rule out this number
	 * in all cells in range, and in the rest of the region. */
	
	int ret = 0;
	int w = state->w;
	int h = state->h;
	int i = y*w+x;
	int c1;
	int j;
	
	if(x < 0 || y < 0 || x >= w || y >= h)
		return 0;
	
	if(state->grid[i] != n)
	{
		state->grid[i] = n;
		ret += 1;
	}
	if(state->marks[i] != NUM_BIT(n))
	{
		state->marks[i] = NUM_BIT(n);
		ret += 1;
	}
	
	if (state->mode == MODE_SEISMIC)
	{
		for (j = 1; j <= n; j++)
		{
			ret += seismic_unset(state, x + j, y, n);
			ret += seismic_unset(state, x - j, y, n);
			ret += seismic_unset(state, x, y + j, n);
			ret += seismic_unset(state, x, y - j, n);
		}
	}
	else
	{
		int dx, dy;
		for (dx = -1; dx <= 1; dx++)
			for (dy = -1; dy <= 1; dy++)
			{
				if (!dx && !dy) continue;
				ret += seismic_unset(state, x + dx, y + dy, n);
			}
	}
	
	c1 = dsf_canonify(state->dsf, i);
	for(j = 0; j < w*h; j++)
	{
		if(j == i)
			continue;
		if(c1 == dsf_canonify(state->dsf, j))
			ret += seismic_unset(state, j%w, j/w, n);
	}
	
	return ret;
}

static void seismic_solver_init(game_state *state)
{
	/* Add the maximum amount of marks to each cell, and 
	 * process the marks for each given clue. */
	
	int w = state->w;
	int h = state->h;
	int s = w*h;
	int i;
	
	for(i = 0; i < w*h; i++)
		state->marks[i] = AREA_BITS(dsf_size(state->dsf, i));
	
	for(i = 0; i < s; i++)
	{
		if(state->grid[i] != 0)
			seismic_place_number(state, i%w, i/w, state->grid[i]);
	}
}

/* Hint support: record the order of the solver's placements. */
static bool hint_on = false;
static int *hint_log = NULL;
static int hint_n = 0, hint_cap = 0;

static int seismic_solver_marks(game_state *state)
{
	/* Check if a cell has one mark left, then place that number */
	
	int w = state->w;
	int h = state->h;
	int s = w*h;
	int i, j;
	
	int ret = 0;
	
	for(i = 0; i < s; i++)
	{
		if(state->grid[i] != 0)
			continue;
		
		for(j = 1; j <= 9; j++)
		{
			if(state->marks[i] == NUM_BIT(j))
			{
				if(hint_on)
				{
					if(hint_n >= hint_cap)
					{
						hint_cap = hint_cap ? hint_cap * 2 : 128;
						hint_log = sresize(hint_log, 2 * hint_cap, int);
					}
					hint_log[2*hint_n] = i; hint_log[2*hint_n+1] = j;
					hint_n++;
				}
				ret += seismic_place_number(state, i%w, i/w, j);
			}
		}
	}
	
	return ret;
}

static int seismic_solver_areas(game_state *state)
{
	/* Check if a region has a single possibility for a certain number,
	 * then remove all other marks from that cell */
	
	int w = state->w;
	int h = state->h;
	int s = w*h;
	int i, c, prev;
	
	int ret = 0;
	
	/* Marks which appear at least once */
	int *singles = snewn(s, int);
	/* Marks which appear at least twice */
	int *doubles = snewn(s, int);
	
	memset(singles, 0, s*sizeof(int));
	memset(doubles, 0, s*sizeof(int));
	
	for(i = 0; i < s; i++)
	{
		c = dsf_canonify(state->dsf, i);
		doubles[c] |= state->marks[i] & singles[c];
		singles[c] |= state->marks[i];
	}
	
	for(i = 0; i < s; i++)
	{
		c = dsf_canonify(state->dsf, i);
		prev = state->marks[i];
		if(state->marks[i] & (singles[c] ^ doubles[c]))
			state->marks[i] &= singles[c] ^ doubles[c];
		
		if(prev != state->marks[i])
			ret++;
	}
	
	sfree(singles);
	sfree(doubles);
	return ret;
}

static int seismic_solver_attempt(game_state *state)
{
	/* Try to place a number, and see if this directly leads to an error */
	
	int ret = 0;
	int w = state->w;
	int s = w * state->h;
	int i, j, n;
	bool valid;
	
	char *grid = snewn(s, char);
	int *marks = snewn(s, int);
	int *areas = snewn(s, int);
	
	for(i = 0; i < s; i++)
	{
		if(state->grid[i] != 0)
			continue;
		
		for(n = 1; n <= 9; n++)
		{
			if(!(state->marks[i] & NUM_BIT(n)))
				continue;
			
			memcpy(grid, state->grid, s*sizeof(char));
			memcpy(marks, state->marks, s*sizeof(int));
			memset(areas, 0, s*sizeof(int));
			
			valid = true;
			seismic_place_number(state, i%w, i/w, n);
			
			/* Get all marks for each region */
			for(j = 0; j < s; j++)
			{
				areas[dsf_canonify(state->dsf, j)] |= state->marks[j];
			}
			
			/* If any number no longer appears in the total marks, 
			 * we have found an error */
			for(j = 0; j < s && valid; j++)
			{
				if(j != dsf_canonify(state->dsf, j)) continue;
				
				if(areas[j] != AREA_BITS(dsf_size(state->dsf, j)))
					valid = false;
			}
			
			memcpy(state->grid, grid, s*sizeof(char));
			memcpy(state->marks, marks, s*sizeof(int));
			
			if(!valid)
			{
				ret += seismic_unset(state, i%w, i/w, n);
			}
		}
	}
	
	sfree(grid);
	sfree(marks);
	sfree(areas);
	return ret;
}

enum { STATUS_COMPLETE, STATUS_UNFINISHED, STATUS_INVALID };

static int seismic_validate_game(game_state *state)
{
	int w = state->w;
	int h = state->h;
	int s = w * h;
	int i, j, n, c;
	int ret = STATUS_COMPLETE;
	
	/* Numbers which appear at least once */
	int *singles = snewn(s, int);
	/* Numbers which appear at least twice */
	int *doubles = snewn(s, int);
	/* Numbers in range of an equal number */
	int *ranges = snewn(s, int);
	
	memset(singles, 0, s*sizeof(int));
	memset(doubles, 0, s*sizeof(int));
	memset(ranges, 0, s*sizeof(int));
	
	/* Find errors */
	for(i = 0; i < s; i++)
	{
		int x = i % w, y = i / w;

		if(state->grid[i] == 0)
			continue;
		
		n = NUM_BIT(state->grid[i]);
		
		c = dsf_canonify(state->dsf, i);
		doubles[c] |= n & singles[c];
		singles[c] |= n;
		
		if (state->mode == MODE_SEISMIC)
		{
			for (j = 1; j <= state->grid[i]; j++)
			{
				if (x + j < w) /* Right */
					ranges[i + j] |= n;
				if (x - j >= 0) /* Left */
					ranges[i - j] |= n;
				if (y - j >= 0) /* Up */
					ranges[i - (j*w)] |= n;
				if (y + j < h) /* Down */
					ranges[i + (j*w)] |= n;
			}
		}
		else
		{
			int dx, dy;
			for (dx = -1; dx <= 1; dx++)
				for (dy = -1; dy <= 1; dy++)
				{
					if (!dx && !dy) continue;
					if(x + dx >= 0 && x + dx < w && y + dy >= 0 && y + dy < h)
						ranges[i+dx+(dy*w)] |= n;
				}
		}
	}
	
	/* Mark errors */
	for(i = 0; i < s; i++)
	{
		if(state->grid[i] == 0)
			continue;
		
		c = dsf_canonify(state->dsf, i);
		if(doubles[c] & NUM_BIT(state->grid[i]))
		{
			ret = STATUS_INVALID;
			state->flags[i] |= FM_ERRORDUP;
		}
		else
			state->flags[i] &= ~FM_ERRORDUP;
		
		if(ranges[i] & NUM_BIT(state->grid[i]))
		{
			ret = STATUS_INVALID;
			state->flags[i] |= FM_ERRORDIST;
		}
		else
			state->flags[i] &= ~FM_ERRORDIST;
	}
	
	if(ret != STATUS_INVALID)
	{
		for(i = 0; i < s && ret != STATUS_UNFINISHED; i++)
		{
			if(state->grid[i] == 0)
				ret = STATUS_UNFINISHED;
		}
	}
	
	sfree(singles);
	sfree(doubles);
	sfree(ranges);
	
	return ret;
}

static int seismic_solve_game(game_state *state, int maxdiff)
{
	int diff = DIFF_EASY;
	
	seismic_solver_init(state);
	
	while(true)
	{
		if(seismic_validate_game(state) != STATUS_UNFINISHED)
			break;
		
		if(seismic_solver_marks(state))
			continue;
		
		if(seismic_solver_areas(state))
			continue;
			
		if(maxdiff < DIFF_HARD)
			break;
		diff = max(diff, DIFF_HARD);
		
		if(seismic_solver_attempt(state))
			continue;
		
		break;
	}
	
	if(seismic_validate_game(state) != STATUS_COMPLETE)
		return -1;
	
	return diff;
}

static bool seismic_gen_numbers(game_state *state, random_state *rs)
{
	/* Fill a grid with numbers by randomly picking squares, then
	 * placing the lowest possible number. */
	
	int w = state->w;
	int h = state->h;
	int s = w * h;
	int i, j, k;
	int *spaces = snewn(s, int);
	
	for(i = 0; i < s; i++)
	{
		state->marks[i] = FM_MARKS;
		spaces[i] = i;
	}
	
	shuffle(spaces, s, sizeof(*spaces), rs);
	
	for(j = 0; j < s; j++)
	{
		i = spaces[j];
		for(k = 1; k <= 9; k++)
		{
			if(state->marks[i] & NUM_BIT(k))
			{
				seismic_place_number(state, i%w, i/w, k);
				break;
			}
		}
		if (k > 9)
		{
			sfree(spaces);
			return false;
		}
	}
	
	sfree(spaces);
	
	return true;
}

static bool tectonic_gen_numbers(game_state *state, random_state *rs)
{
	int w = state->w;
	int h = state->h;
	int s = w * h;
	int i, j, k;
	int spaces[5] = { 1, 2, 3, 4, 5 };
	int counts[5] = { 0, 0, 0, 0, 0 };
	for (i = 0; i < s; i++)
	{
		state->marks[i] = AREA_BITS(5);
	}

	/* Visit all grid spaces sequentially and place a random number. */
	for (i = 0; i < s; i++)
	{
		shuffle(spaces, 5, sizeof(int), rs);
		for (j = 0; j < 5; j++)
		{
			k = spaces[j];
			if (state->marks[i] & NUM_BIT(k))
			{
				seismic_place_number(state, i%w, i/w, k);
				counts[k - 1]++;
				break;
			}
		}
	}

	/* Build a map of each number based on how often it appears in the grid. */
	for (j = 0; j < 5; j++)
	{
		int imax = -1;
		int cmax = -1;
		for (k = 0; k < 5; k++)
		{
			if (counts[k] > cmax)
			{
				imax = k;
				cmax = counts[k];
			}
		}

		spaces[j] = imax + 1;
		counts[imax] = -1;
	}

	/*
	 * Translate the grid numbers based on the map.
	 * The 1 must appear most often in the grid, followed by 2, then 3, etc. 
	 */
	for (i = 0; i < s; i++)
		state->grid[i] = spaces[state->grid[i] - 1];

	return true;
}

static bool seismic_gen_areas(game_state *state, random_state *rs)
{
	/* Examine borders between two areas in a random order,
	 * and attempt to merge the areas whenever possible.
	 * Returns false if an area turns out to miss a number. */
	 
	/* TODO: This is a dumb way of generating a region layout.
	 * Think of a replacement for this algorithm */
	
	int w = state->w;
	int h = state->h;
	int x, y, i, i1, i2;
	int hs = ((w-1)*h);
	int ws = hs + (w*(h-1));
	bool ret = true;
	
	int *cells = snewn(w*h, int);
	int c, c1, c2;
	int *spaces = snewn(ws, int);
	
	/* Initialize horizontal mergers */
	i = 0;
	for(y = 0; y < h; y++)
	for(x = 0; x < w-1; x++)
	{
		spaces[i++] = y*w+x;
	}
	
	/* Initialize vertical mergers */
	for(y = 0; y < h-1; y++)
	for(x = 0; x < w; x++)
	{
		spaces[i++] = (w*h) + y*w+x;
	}
	
	/* Initialize region array */
	for(i = 0; i < w*h; i++)
	{
		assert(i == dsf_canonify(state->dsf, i));
		cells[i] = NUM_BIT(state->grid[i]);
	}
	
	shuffle(spaces, ws, sizeof(*spaces), rs);
	
	for(i = 0; i < ws; i++)
	{
		i1 = spaces[i] % (w*h);
		i2 = spaces[i] >= w*h ? i1 + w : i1 + 1;
		
		c1 = cells[dsf_canonify(state->dsf, i1)];
		c2 = cells[dsf_canonify(state->dsf, i2)];
		
		/* If these two regions have numbers in common, they cannot merge */
		if(c1 & c2)
			continue;
		
		c = c1 | c2;
		
		dsf_merge(state->dsf, i1, i2);
		
		cells[dsf_canonify(state->dsf, i1)] |= c;
	}
	
	for(i = 0; i < w*h; i++)
	{
		if(cells[dsf_canonify(state->dsf, i)] != AREA_BITS(dsf_size(state->dsf, i)))
			ret = false;
	}
	
	sfree(spaces);
	sfree(cells);
	
	return ret;
}

static bool seismic_gen_clues(game_state *state, random_state *rs, int diff)
{
	/* Randomly remove numbers to create a puzzle */
	
	int s = state->w * state->h;
	int i, j;
	char status;
	
	int *spaces = snewn(s, int);
	char *grid = snewn(s, char);
	
	for(i = 0; i < s; i++)
		spaces[i] = i;
	
	shuffle(spaces, s, sizeof(*spaces), rs);
	memcpy(grid, state->grid, s*sizeof(char));
	
	for(j = 0; j < s; j++)
	{
		i = spaces[j];
		
		state->grid[i] = 0;
		
		status = seismic_solve_game(state, diff);
		memcpy(state->grid, grid, s*sizeof(char));
		
		if(status != -1)
		{
			state->grid[i] = 0;
			grid[i] = 0;
		}
	}
	
	sfree(spaces);
	sfree(grid);
	
	return true;
}

static bool seismic_gen_diff(game_state *state, int diff)
{
	/* Verify the difficulty of the puzzle */
	
	game_state *solved;
	bool ret = true;
	
	/* Check if puzzle is solvable */
	solved = dup_game(state);
	if(seismic_solve_game(solved, diff) == -1)
		ret = false;
	free_game(solved);
	
	if(diff <= 0 || !ret)
		return ret;
	
	/* Check if puzzle is not solvable on lower difficulty */
	solved = dup_game(state);
	if(seismic_solve_game(solved, diff-1) != -1)
		ret = false;
	free_game(solved);
	
	return ret;
}

/* ---- Generator -----------------------------------------------------------
 *
 * The generator is the one of the original game.  Fill the grid with numbers
 * (Seismic: visit the cells in random order and give each the lowest number
 * that is still allowed; Tectonic: random numbers 1-5, then relabel so that
 * the most frequent one is 1, the next one 2 and so on), then merge
 * neighbouring cells at random as long as their numbers differ, and accept the
 * result only if every region of size N holds exactly 1..N.  Nothing about the
 * sizes of the regions is decided by hand: many singletons and dominoes and a
 * few big regions are simply what this process produces.
 *
 * The weak point of the original is that the final test is all or nothing, so
 * the chance of success drops exponentially with the size of the grid.  Hence:
 *
 *  - Up to GEN_ORIGINAL_CELLS cells the original algorithm is run as it is
 *    (with a limit on the number of tries).
 *
 *  - Seismic, larger: the same algorithm, window by window.  The numbers and
 *    the regions of a small part of the grid are drawn the way the original
 *    draws them, with the numbers around it kept as they are, again and
 *    again over the whole grid (see below).  Nothing is decided by hand, so
 *    the sizes of the regions come out as in the original.
 *
 *  - Tectonic, larger: the regions come from the same random merging (a region
 *    can have at most 5 cells because Tectonic only uses the numbers 1-5), the
 *    numbers are then filled in by a search, and wherever that fails the
 *    neighbourhood is merged and filled again.
 */

#ifndef GEN_ORIGINAL_CELLS
#define GEN_ORIGINAL_CELLS 36
#endif
#define GEN_ORIGINAL_TRIES 30000

static bool seismic_gen_original(game_state *state, random_state *rs)
{
	int w = state->w;
	int h = state->h;
	int tries;
	bool ok;

	for (tries = 0; tries < GEN_ORIGINAL_TRIES; tries++)
	{
		memset(state->grid, 0, w*h*sizeof(char));
		dsf_reinit(state->dsf);

		if (state->mode == MODE_TECTONIC)
			ok = tectonic_gen_numbers(state, rs);
		else
			ok = seismic_gen_numbers(state, rs);

		if (ok && seismic_gen_areas(state, rs))
			return true;
	}
	return false;
}

/* ---- Seismic, larger grids: the original algorithm, window by window ----
 *
 * The all or nothing test of the original generator makes it hopeless on a big
 * grid, but nothing in it is global: a number only looks at its neighbours in
 * the row and column, and a region is complete or not no matter what happens
 * elsewhere.  So the same algorithm is run on small windows of the grid, one
 * after the other:
 *
 *   - give the cells of the window a random order and numbers in that order:
 *     each gets the lowest number that none of the neighbours placed before it
 *     has (the original's way of choosing the numbers);
 *   - merge neighbouring cells of the window at random while their numbers
 *     differ (the original's way of making regions);
 *   - if every region of size N holds exactly 1..N, keep the result,
 *     otherwise try again (up to SG_WIN_TRIES times, then leave the window as
 *     it was).
 *
 * That is the same all or nothing test as the original, but on a window, so
 * it succeeds after a few hundred tries instead of billions.  The cells
 * outside a window keep their numbers, so they take part as neighbours, and
 * none of their numbers may change.  A window is the cells around a random
 * cell plus every region that touches them (so whole regions only).
 *
 * It is done in two steps.  First a grid is made out of nothing: windows are
 * put around cells that have no number yet, and the cells outside count as
 * placed before the cells of the window.  That is easy and always works, but
 * the numbers it draws are too large, because every cell sees all its
 * neighbours as placed first.  Then the order of the cells is made explicit
 * (the order the original draws the numbers in) and windows are drawn again
 * and again around random cells until every cell has been drawn about 40
 * times: a window gets a new random order for its cells, the cells outside
 * keep their places in the order, and every number outside must still be the
 * one the original rule gives it.  This second step is what makes the numbers
 * and the regions come out like the original's.
 *
 * Nothing says how big a region may be or how many small ones there are; that
 * comes out of the numbers as in the original, and the puzzles come out with
 * the same kind of layout, the same numbers around each number and about as
 * many clues as theirs.  (Measured against the original on 6x6 and 7x7: the
 * sizes of the regions are within 2-3 percentage points of its, a bit more
 * single cells and a bit fewer dominoes, and the clues are within a few
 * percent.)  It runs on a private random generator seeded from the game's
 * one, so the same seed gives the same game.
 */

#ifndef SG_WIN_RADIUS
#define SG_WIN_RADIUS 2		/* window: cells within this distance of a random cell */
#endif
#ifndef SG_WIN_CELLS
#define SG_WIN_CELLS 20		/* at most this many cells with the regions touching them */
#endif
#ifndef SG_WIN_TRIES
#define SG_WIN_TRIES 300	/* tries of the original algorithm per window */
#endif
#ifndef SG_WIN_STEPS
#define SG_WIN_STEPS(s) ((s) * 40 * 5 / (SG_WIN_CELLS * 3))	/* about 40 draws per cell, 0.6 SG_WIN_CELLS cells per window */
#endif
#define SG_ARM 9		/* a number n only looks at cells up to n away */

struct sg_ctx {
	int w, h, s;
	char *num;		/* the numbers: the grid of the game */
	unsigned *rank;		/* place of each cell in the order */
	int *rep;		/* region of each cell, named by one of its cells; -1: no number yet */
	char *touched, *inwin, *isaff;
	int *lidx, *aff;
	int nun;		/* cells that have no number yet */
	unsigned long long st;	/* the private random generator */
	int nwin;
	int win[SG_WIN_CELLS], order[SG_WIN_CELLS];
	int bord[4 * SG_WIN_CELLS], bo[2 * SG_WIN_CELLS];
	int dsf[SG_WIN_CELLS], size[SG_WIN_CELLS], mask[SG_WIN_CELLS];
	char oldnum[SG_WIN_CELLS];
	unsigned oldrank[SG_WIN_CELLS];
};

static unsigned sg_rand32(unsigned long long *st)
{
	unsigned long long x = *st;

	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	*st = x;
	return (unsigned)(x >> 32);
}

static unsigned sg_rand(unsigned long long *st, unsigned n)
{
	return (unsigned)(((unsigned long long)sg_rand32(st) * n) >> 32);
}

static int sg_find(int *dsf, int x)
{
	while (dsf[x] != x)
	{
		dsf[x] = dsf[dsf[x]];
		x = dsf[x];
	}
	return x;
}

/* Does cell a come before cell b in the order? */
static bool sg_before(const unsigned *rank, int a, int b)
{
	return rank[a] < rank[b] || (rank[a] == rank[b] && a < b);
}

/* Is n allowed in cell i if only the numbers of the cells that come before
 * it count? */
static bool sg_allowed_before(const char *num, const unsigned *rank,
			      int w, int h, int i, int n)
{
	int x = i % w, y = i / w, d;

	for (d = 1; d <= n; d++)
	{
		if (x + d < w && num[i + d] == n && sg_before(rank, i + d, i))
			return false;
		if (x - d >= 0 && num[i - d] == n && sg_before(rank, i - d, i))
			return false;
		if (y + d < h && num[i + d*w] == n && sg_before(rank, i + d*w, i))
			return false;
		if (y - d >= 0 && num[i - d*w] == n && sg_before(rank, i - d*w, i))
			return false;
	}
	return true;
}

/* The number the original rule gives cell i: the lowest one that none of the
 * cells before it has within that distance. */
static int sg_lowest_before(const char *num, const unsigned *rank,
			    int w, int h, int i)
{
	int k;

	for (k = 1; k <= 9; k++)
		if (sg_allowed_before(num, rank, w, h, i, k))
			return k;
	return 0;
}

/* Is every smaller number k there to stop cell i from having k, that is, is
 * there a k within distance k of it?  (First step: no order yet.) */
static bool sg_justified(const char *num, int w, int h, int i)
{
	int n = num[i], x = i % w, y = i / w, k, d;

	for (k = 1; k < n; k++)
	{
		bool found = false;
		for (d = 1; d <= k && !found; d++)
			found = (x + d < w && num[i + d] == k) ||
				(x - d >= 0 && num[i - d] == k) ||
				(y + d < h && num[i + d*w] == k) ||
				(y - d >= 0 && num[i - d*w] == k);
		if (!found)
			return false;
	}
	return true;
}

/* The window around cell cc: the cells within SG_WIN_RADIUS of it and every
 * region that touches them, if that is no more than SG_WIN_CELLS cells;
 * otherwise a smaller ball. */
static void sg_window_around(struct sg_ctx *c, int cc)
{
	int w = c->w, s = c->s, cx = cc % w, cy = cc / w;
	int rad, i, n = 0;

	for (rad = SG_WIN_RADIUS; rad >= 0; rad--)
	{
		memset(c->touched, 0, s);
		for (i = 0; i < s; i++)
		{
			int x = i % w, y = i / w;
			if (c->rep[i] >= 0 && x >= cx - rad && x <= cx + rad &&
			    y >= cy - rad && y <= cy + rad)
				c->touched[c->rep[i]] = 1;
		}
		n = 0;
		for (i = 0; i < s; i++)
		{
			int x = i % w, y = i / w;
			if ((x >= cx - rad && x <= cx + rad &&
			     y >= cy - rad && y <= cy + rad) ||
			    (c->rep[i] >= 0 && c->touched[c->rep[i]]))
			{
				if (n == SG_WIN_CELLS)
				{
					n++;
					break;
				}
				c->win[n++] = i;
			}
		}
		if (n <= SG_WIN_CELLS)
			break;
	}
	c->nwin = n <= SG_WIN_CELLS ? n : 0;
	for (i = 0; i < c->nwin; i++)
		c->inwin[c->win[i]] = 1;
}

/* Try to give the cells of the window new numbers and new regions, as the
 * original generator would, and keep them if they pass the test.  In the first
 * step (first) there is no order of the cells yet. */
static bool sg_try_window(struct sg_ctx *c, bool first, int tries)
{
	int w = c->w, h = c->h, nwin = c->nwin;
	int *win = c->win, *order = c->order;
	char *num = c->num;
	unsigned *rank = c->rank;
	int nb = 0, naff = 0, t, i, j, k, d;
	bool ok = false;

	for (i = 0; i < nwin; i++)
	{
		c->lidx[win[i]] = i;
		c->oldnum[i] = num[win[i]];
		c->oldrank[i] = rank[win[i]];
	}
	/* the borders between two cells of the window */
	for (i = 0; i < nwin; i++)
	{
		int cc = win[i], x = cc % w, y = cc / w;
		if (x + 1 < w && c->inwin[cc + 1])
		{
			c->bord[2*nb] = i;
			c->bord[2*nb + 1] = c->lidx[cc + 1];
			nb++;
		}
		if (y + 1 < h && c->inwin[cc + w])
		{
			c->bord[2*nb] = i;
			c->bord[2*nb + 1] = c->lidx[cc + w];
			nb++;
		}
	}
	for (i = 0; i < nb; i++)
		c->bo[i] = i;
	/* the numbers outside the window that look at a cell of it */
	for (i = 0; i < nwin; i++)
	{
		int x = win[i] % w, y = win[i] / w;
		for (d = 1; d <= SG_ARM; d++)
			for (k = 0; k < 4; k++)
			{
				int xx = x + (k == 0 ? d : k == 1 ? -d : 0);
				int yy = y + (k == 2 ? d : k == 3 ? -d : 0);
				int cc;
				if (xx < 0 || xx >= w || yy < 0 || yy >= h)
					continue;
				cc = yy * w + xx;
				if (c->inwin[cc] || c->isaff[cc] || num[cc] < d + (first ? 1 : 0))
					continue;
				c->isaff[cc] = 1;
				c->aff[naff++] = cc;
			}
	}

	for (t = 0; t < tries && !ok; t++)
	{
		for (i = 0; i < nwin; i++)
		{
			num[win[i]] = 0;
			order[i] = i;
		}
		if (first)
		{
			for (i = nwin - 1; i > 0; i--)
			{
				j = (int)sg_rand(&c->st, i + 1);
				k = order[i];
				order[i] = order[j];
				order[j] = k;
			}
			/* the numbers, in that order, next to all the numbers that are there */
			for (j = 0; j < nwin; j++)
			{
				int cc = win[order[j]], x = cc % w, y = cc / w;
				for (k = 1; k <= 9; k++)
				{
					bool free = true;
					for (d = 1; d <= k && free; d++)
						free = !((x + d < w && num[cc + d] == k) ||
							 (x - d >= 0 && num[cc - d] == k) ||
							 (y + d < h && num[cc + d*w] == k) ||
							 (y - d >= 0 && num[cc - d*w] == k));
					if (free)
						break;
				}
				if (k > 9)
					break;
				num[cc] = k;
			}
			if (j < nwin)
				continue;
		}
		else
		{
			for (i = 0; i < nwin; i++)
				rank[win[i]] = sg_rand32(&c->st);
			for (i = 1; i < nwin; i++)
			{
				int v = order[i];
				for (j = i - 1; j >= 0 && sg_before(rank, win[v], win[order[j]]); j--)
					order[j + 1] = order[j];
				order[j + 1] = v;
			}
			/* the numbers, in that order */
			for (j = 0; j < nwin; j++)
			{
				int cc = win[order[j]];
				k = sg_lowest_before(num, rank, w, h, cc);
				if (!k)
					break;
				num[cc] = k;
			}
			if (j < nwin)
				continue;
		}
		/* the numbers outside must still be the ones they would pick */
		for (j = 0; j < naff; j++)
			if (first ? !sg_justified(num, w, h, c->aff[j])
				  : sg_lowest_before(num, rank, w, h, c->aff[j]) != num[c->aff[j]])
				break;
		if (j < naff)
			continue;

		/* merge at random while the numbers differ */
		for (i = 0; i < nwin; i++)
		{
			c->dsf[i] = i;
			c->size[i] = 1;
			c->mask[i] = NUM_BIT(num[win[i]]);
		}
		for (i = nb - 1; i > 0; i--)
		{
			j = (int)sg_rand(&c->st, i + 1);
			k = c->bo[i];
			c->bo[i] = c->bo[j];
			c->bo[j] = k;
		}
		for (j = 0; j < nb; j++)
		{
			int a = sg_find(c->dsf, c->bord[2*c->bo[j]]);
			int b = sg_find(c->dsf, c->bord[2*c->bo[j] + 1]);
			if (a == b || (c->mask[a] & c->mask[b]))
				continue;
			c->dsf[b] = a;
			c->size[a] += c->size[b];
			c->mask[a] |= c->mask[b];
		}

		/* every region of size N must hold exactly 1..N */
		ok = true;
		for (i = 0; i < nwin; i++)
			if (c->dsf[i] == i && c->mask[i] != AREA_BITS(c->size[i]))
			{
				ok = false;
				break;
			}
	}

	if (ok)
	{
		for (i = 0; i < nwin; i++)
		{
			if (c->rep[win[i]] < 0)
				c->nun--;
			c->rep[win[i]] = win[sg_find(c->dsf, i)];
		}
	}
	else
	{
		for (i = 0; i < nwin; i++)
		{
			num[win[i]] = c->oldnum[i];
			rank[win[i]] = c->oldrank[i];
		}
	}
	for (i = 0; i < nwin; i++)
		c->inwin[win[i]] = 0;
	for (i = 0; i < naff; i++)
		c->isaff[c->aff[i]] = 0;
	return ok;
}

/* Does the grid obey the rules: every number n has no equal number within
 * distance n in its row and column, and every region of size N holds 1..N? */
static bool sg_valid(const game_state *state)
{
	int w = state->w, h = state->h, s = w * h, i, d;
	int *mask = snewn(s, int);
	bool ok = true;

	memset(mask, 0, s * sizeof(int));
	for (i = 0; i < s && ok; i++)
	{
		int n = state->grid[i], x = i % w, y = i / w;

		if (n < 1 || n > 9)
		{
			ok = false;
			break;
		}
		mask[dsf_canonify(state->dsf, i)] |= NUM_BIT(n);
		for (d = 1; d <= n; d++)
			if ((x + d < w && state->grid[i + d] == n) ||
			    (y + d < h && state->grid[i + d*w] == n))
				ok = false;
	}
	for (i = 0; i < s && ok; i++)
		if (dsf_canonify(state->dsf, i) == i &&
		    mask[i] != AREA_BITS(dsf_size(state->dsf, i)))
			ok = false;
	sfree(mask);
	return ok;
}

static bool seismic_gen_windows(game_state *state, random_state *rs)
{
	int w = state->w, h = state->h, s = w * h;
	struct sg_ctx *c = snew(struct sg_ctx);
	long step = 0, steps = SG_WIN_STEPS(s), maxsteps = 4 * steps + 1000;
	int scan = 0, i, j, k;
	bool done = true;

	c->w = w;
	c->h = h;
	c->s = s;
	c->num = state->grid;
	c->rank = snewn(s, unsigned);
	c->rep = snewn(s, int);
	c->touched = snewn(s, char);
	c->inwin = snewn(s, char);
	c->isaff = snewn(s, char);
	c->lidx = snewn(s, int);
	c->aff = snewn(s, int);
	c->nun = s;
	c->nwin = 0;
	c->st = (unsigned long long)random_bits(rs, 32) << 32;
	c->st |= random_bits(rs, 32);
	if (!c->st)
		c->st = 88172645463325252ULL;

	memset(c->num, 0, s);
	memset(c->inwin, 0, s);
	memset(c->isaff, 0, s);
	for (i = 0; i < s; i++)
	{
		c->rep[i] = -1;
		c->rank[i] = 0;
	}

	/* first step: a grid out of nothing, the first cell without a number
	 * and the cells around it again and again */
	while (c->nun > 0)
	{
		int cc;

		if (step++ >= maxsteps)
		{
			done = false;
			break;
		}
		while (c->rep[scan] >= 0)
			scan++;
		cc = scan;
		if (step % 2 == 0)
		{
			/* now and then somewhere near it */
			int x = cc % w + (int)sg_rand(&c->st, 5) - 2;
			int y = cc / w + (int)sg_rand(&c->st, 5) - 2;
			x = x < 0 ? 0 : x >= w ? w - 1 : x;
			y = y < 0 ? 0 : y >= h ? h - 1 : y;
			cc = y * w + x;
		}
		sg_window_around(c, cc);
		if (c->nwin > 0)
			sg_try_window(c, true, SG_WIN_TRIES);
	}

	if (done)
	{
		/* the order: by number, then at random (every number is justified by
		 * smaller ones, so each cell comes after the ones that stop it from
		 * having a smaller number) */
		int *perm = snewn(s, int);
		int n = 0;

		for (i = 0; i < s; i++)
			perm[i] = i;
		for (i = s - 1; i > 0; i--)
		{
			j = (int)sg_rand(&c->st, i + 1);
			k = perm[i];
			perm[i] = perm[j];
			perm[j] = k;
		}
		for (k = 1; k <= 9; k++)
			for (i = 0; i < s; i++)
				if (c->num[perm[i]] == k)
					c->rank[perm[i]] = (unsigned)(((unsigned long long)n++ << 32) / ((unsigned long long)s + 1));
		sfree(perm);

		/* second step: the original's order of the cells */
		for (step = 0; step < steps; step++)
		{
			sg_window_around(c, (int)sg_rand(&c->st, s));
			if (c->nwin > 0)
				sg_try_window(c, false, SG_WIN_TRIES);
		}
	}

	if (done)
	{
		dsf_reinit(state->dsf);
		for (i = 0; i < s; i++)
			dsf_merge(state->dsf, i, c->rep[i]);
		done = sg_valid(state);
	}

	sfree(c->rank);
	sfree(c->rep);
	sfree(c->touched);
	sfree(c->inwin);
	sfree(c->isaff);
	sfree(c->lidx);
	sfree(c->aff);
	sfree(c);
	return done;
}

/* ---- Tectonic, larger grids: random merging, then numbers by search ---- */

#define TEC_MAX 5      /* Tectonic only uses the numbers 1 to 5 */

struct tecgen {
	int w, h, s;
	char *val;             /* the numbers */
	int *rid, *rsz;        /* region of a cell (its lowest cell) and its size */
	int *usedm;            /* numbers used so far, by region */
	int *uf, *usz;
	int *wl, nwl;          /* cells that are being filled in */
	long nodes, limit;
	random_state *rs;
};

static int tg_find(struct tecgen *t, int x)
{
	while (t->uf[x] != x)
	{
		t->uf[x] = t->uf[t->uf[x]];
		x = t->uf[x];
	}
	return x;
}

/* Merge neighbouring cells at random into regions of at most TEC_MAX cells,
 * either everywhere (act == NULL) or only among the cells marked in act. */
static void tg_remerge(struct tecgen *t, const char *act)
{
	int w = t->w, h = t->h, s = t->s, n = 0, c, x, y, q;
	int *edges = snewn(2 * s, int), *minc = snewn(s, int);

	for (c = 0; c < s; c++)
		if (!act || act[c])
		{
			t->uf[c] = c;
			t->usz[c] = 1;
		}
	for (y = 0; y < h; y++)
		for (x = 0; x < w - 1; x++)
		{
			c = y * w + x;
			if (!act || (act[c] && act[c + 1]))
				edges[n++] = c;
		}
	for (y = 0; y < h - 1; y++)
		for (x = 0; x < w; x++)
		{
			c = y * w + x;
			if (!act || (act[c] && act[c + w]))
				edges[n++] = s + c;
		}
	shuffle(edges, n, sizeof(int), t->rs);
	for (q = 0; q < n; q++)
	{
		int i1 = edges[q] % s, i2 = edges[q] >= s ? i1 + w : i1 + 1;
		int r1 = tg_find(t, i1), r2 = tg_find(t, i2);

		if (r1 == r2 || t->usz[r1] + t->usz[r2] > TEC_MAX)
			continue;
		t->uf[r2] = r1;
		t->usz[r1] += t->usz[r2];
	}
	for (c = 0; c < s; c++)
		if (!act || act[c])
			minc[c] = s;
	for (c = 0; c < s; c++)
		if (!act || act[c])
		{
			int r = tg_find(t, c);
			if (c < minc[r])
				minc[r] = c;
		}
	for (c = 0; c < s; c++)
		if (!act || act[c])
		{
			int r = tg_find(t, c);
			t->rid[c] = minc[r];
			t->rsz[c] = t->usz[r];
		}
	for (c = 0; c < s; c++)
		if (!act || act[c])
			t->usedm[t->rid[c]] = 0;
	sfree(edges);
	sfree(minc);
}

static bool tg_allowed(const struct tecgen *t, int i, int n)
{
	int w = t->w, h = t->h, x = i % w, y = i / w, dx, dy;

	if (n > t->rsz[i] || (t->usedm[t->rid[i]] & (1 << n)))
		return false;
	for (dy = -1; dy <= 1; dy++)
		for (dx = -1; dx <= 1; dx++)
		{
			int xx = x + dx, yy = y + dy;
			if ((!dx && !dy) || xx < 0 || yy < 0 || xx >= w || yy >= h)
				continue;
			if (t->val[yy * w + xx] == n)
				return false;
		}
	return true;
}

/* Fill in the numbers of the cells in wl: always the cell with the fewest
 * possibilities next. */
static bool tg_search(struct tecgen *t)
{
	int q, i, best = -1, bn = 99, n, nc, k, cand[TEC_MAX + 1];

	if (t->nodes++ > t->limit)
		return false;
	for (q = 0; q < t->nwl; q++)
	{
		int c = 0;

		i = t->wl[q];
		if (t->val[i])
			continue;
		for (n = 1; n <= t->rsz[i]; n++)
			if (tg_allowed(t, i, n))
				c++;
		if (!c)
			return false;
		if (c < bn || (c == bn && random_upto(t->rs, 2)))
		{
			bn = c;
			best = i;
		}
	}
	if (best < 0)
		return true;
	nc = 0;
	for (n = 1; n <= t->rsz[best]; n++)
		if (tg_allowed(t, best, n))
			cand[nc++] = n;
	shuffle(cand, nc, sizeof(int), t->rs);
	for (k = 0; k < nc; k++)
	{
		t->val[best] = cand[k];
		t->usedm[t->rid[best]] |= 1 << cand[k];
		if (tg_search(t))
			return true;
		t->usedm[t->rid[best]] &= ~(1 << cand[k]);
		t->val[best] = 0;
		if (t->nodes > t->limit)
			return false;
	}
	return false;
}

/* Numbers for one region, cell by cell. */
static bool tg_fill_region(struct tecgen *t, const int *cells, int k, int idx)
{
	int c, n, nc = 0, cand[TEC_MAX + 1], j;

	if (idx == k)
		return true;
	c = cells[idx];
	for (n = 1; n <= k; n++)
		if (tg_allowed(t, c, n))
			cand[nc++] = n;
	shuffle(cand, nc, sizeof(int), t->rs);
	for (j = 0; j < nc; j++)
	{
		t->val[c] = cand[j];
		t->usedm[t->rid[c]] |= 1 << cand[j];
		if (tg_fill_region(t, cells, k, idx + 1))
			return true;
		t->usedm[t->rid[c]] &= ~(1 << cand[j]);
		t->val[c] = 0;
	}
	return false;
}

static bool tectonic_gen_scalable(game_state *state, random_state *rs)
{
	int w = state->w, h = state->h, s = w * h;
	struct tecgen t;
	long it, itcap = 3000 + 60L * s;
	bool ok = false;
	int c, q, nr, k;
	int *reglist = snewn(s, int), *hl = snewn(s, int);
	int *bkr = snewn(s, int), *bks = snewn(s, int), *bku = snewn(s + 1, int);
	char *bkv = snewn(s, char), *inw = snewn(s, char), *ra = snewn(s, char);

	t.w = w; t.h = h; t.s = s;
	t.val = state->grid;
	t.rid = snewn(s, int);
	t.rsz = snewn(s, int);
	t.usedm = snewn(s + 1, int);
	t.uf = snewn(s, int);
	t.usz = snewn(s, int);
	t.wl = snewn(s, int);
	t.nwl = 0;
	t.rs = rs;

	memset(t.val, 0, s * sizeof(char));
	memset(t.usedm, 0, (s + 1) * sizeof(int));
	tg_remerge(&t, NULL);

	/* numbers region by region; regions that do not fit are left empty */
	nr = 0;
	for (c = 0; c < s; c++)
		if (t.rid[c] == c)
			reglist[nr++] = c;
	shuffle(reglist, nr, sizeof(int), rs);
	for (q = 0; q < nr; q++)
	{
		int cells[TEC_MAX];

		k = 0;
		for (c = 0; c < s; c++)
			if (t.rid[c] == reglist[q])
				cells[k++] = c;
		shuffle(cells, k, sizeof(int), rs);
		tg_fill_region(&t, cells, k, 0);
	}

	/* where numbers are missing, merge and fill in the neighbourhood again */
	for (it = 0; it < itcap; it++)
	{
		int nh = 0, f, rad, fx, fy, x, y, before = 0, after = 0;
		bool good;

		for (c = 0; c < s; c++)
			if (!t.val[c])
				hl[nh++] = c;
		if (!nh)
		{
			ok = true;
			break;
		}
		f = hl[random_upto(rs, nh)];
		rad = 1 + (int)((it / 4) % 2);
		if (it > 1500 && (it / 4) % 3 == 2)
			rad = 3;
		fx = f % w;
		fy = f / w;

		/* the regions within reach of the empty cell */
		memset(ra, 0, s);
		for (y = fy - rad; y <= fy + rad; y++)
			for (x = fx - rad; x <= fx + rad; x++)
				if (x >= 0 && y >= 0 && x < w && y < h)
					ra[t.rid[y * w + x]] = 1;
		t.nwl = 0;
		for (c = 0; c < s; c++)
		{
			inw[c] = ra[t.rid[c]];
			if (inw[c])
			{
				t.wl[t.nwl++] = c;
				if (!t.val[c])
					before++;
			}
		}

		memcpy(bkv, t.val, s * sizeof(char));
		memcpy(bkr, t.rid, s * sizeof(int));
		memcpy(bks, t.rsz, s * sizeof(int));
		memcpy(bku, t.usedm, (s + 1) * sizeof(int));

		for (q = 0; q < t.nwl; q++)
			t.val[t.wl[q]] = 0;
		tg_remerge(&t, inw);
		t.nodes = 0;
		t.limit = 400;
		good = tg_search(&t);
		for (q = 0; q < t.nwl; q++)
			if (!t.val[t.wl[q]])
				after++;
		if (!good && after >= before)
		{
			memcpy(t.val, bkv, s * sizeof(char));
			memcpy(t.rid, bkr, s * sizeof(int));
			memcpy(t.rsz, bks, s * sizeof(int));
			memcpy(t.usedm, bku, (s + 1) * sizeof(int));
		}
	}

	if (ok)
	{
		dsf_reinit(state->dsf);
		for (c = 0; c < s; c++)
			if (t.rid[c] != c)
				dsf_merge(state->dsf, c, t.rid[c]);
	}

	sfree(reglist); sfree(hl); sfree(bkr); sfree(bks); sfree(bku);
	sfree(bkv); sfree(inw); sfree(ra);
	sfree(t.rid); sfree(t.rsz); sfree(t.usedm); sfree(t.uf); sfree(t.usz);
	sfree(t.wl);
	return ok;
}

static bool seismic_gen_layout(game_state *state, random_state *rs)
{
	int w = state->w, h = state->h;

	if (w * h <= GEN_ORIGINAL_CELLS && seismic_gen_original(state, rs))
		return true;

	memset(state->grid, 0, w*h*sizeof(char));
	dsf_reinit(state->dsf);
	if (state->mode == MODE_TECTONIC)
		return tectonic_gen_scalable(state, rs);
	return seismic_gen_windows(state, rs);
}

static bool seismic_gen_puzzle(game_state *state, random_state *rs, int diff)
{
	if (!seismic_gen_layout(state, rs))
		return false;
	if(!seismic_gen_clues(state, rs, diff))
		return false;
	if(!seismic_gen_diff(state, diff))
		return false;

	return true;
}

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
	int w = params->w;
	int h = params->h;
	int diff = params->diff;
	int x, y, i, erun, wrun;
	int hs = ((w-1)*h);
	int ws = hs + (w*(h-1));
	char *ret, *p, c;
	
	char *walls = snewn(ws, char);
	game_state *state = blank_state(w, h, params->mode);
	
	do
	{
		memset(state->grid, 0, w*h*sizeof(char));
		dsf_reinit(state->dsf);
	}while(!seismic_gen_puzzle(state, rs, diff));
	
	/* Generate wall list */
	i = 0;
	for(y = 0; y < h; y++)
	for(x = 0; x < w-1; x++)
	{
		if(dsf_canonify(state->dsf, y*w+x) != dsf_canonify(state->dsf, y*w+x+1))
			walls[i] = true;
		else
			walls[i] = false;
		i++;
	}
	for(y = 0; y < h-1; y++)
	for(x = 0; x < w; x++)
	{
		if(dsf_canonify(state->dsf, y*w+x) != dsf_canonify(state->dsf, (y+1)*w+x))
			walls[i] = true;
		else
			walls[i] = false;
		i++;
	}
	
	ret = snewn(ws + (w*h), char);
	p = ret;
	
	erun = wrun = 0;
	for(i = 0; i < ws; i++)
	{
		if(!walls[i] && wrun > 0)
		{
			p += sprintf(p, "%d", wrun);
			wrun = 0;
			erun = 0;
		}
		else if(walls[i] && erun > 0)
		{
			*p++ = ('a' + erun - 1);
			erun = 0;
			wrun = -1;
		}
		
		if(!walls[i])
			erun++;
		else
			wrun++;
	}
	if(wrun > 0)
		p += sprintf(p, "%d", wrun);
	if(erun > 0)
		*p++ = ('a' + erun - 1);
	
	*p++ = ',';
	
	erun = 0;
	for(i = 0; i < w*h; i++)
	{
		c = state->grid[i];
		if(erun > 0 && c != 0)
		{
			while(erun >= 26)
			{
				*p++ = 'z';
				erun -= 26;
			}
			if(erun > 0)
				*p++ = ('a' + erun - 1);
			erun = 0;
		}
		if(c > 0)
			*p++ = '0' + c;
		else
			erun++;
	}
	while(erun >= 26)
	{
		*p++ = 'z';
		erun -= 26;
	}
	if(erun > 0)
		*p++ = ('a' + erun - 1);
	
	*p++ = '\0';
	
	free_game(state);
	sfree(walls);
	
	return ret;
}

enum { VALID, INVALID_WALLS, INVALID_REGION, INVALID_CLUESIZE };

static int seismic_read_desc(const game_params *params, const char *desc, game_state **retstate)
{
	int w = params->w;
	int h = params->h;
	int erun, wrun;
	int i, x, y, i1, i2;
	int valid = VALID;
	const char *p = desc;
	int hs = ((w-1)*h);
	int ws = hs + (w*(h-1));
	char *walls = snewn(ws, char);
	game_state *state = blank_state(w, h, params->mode);
	
	dsf_reinit(state->dsf);
	
	memset(walls, false, ws*sizeof(char));
	
	/* Read list of walls */
	erun = wrun = 0;
	for(i = 0; i < ws; i++)
	{
		//assert(*p != ',');
		if(erun == 0 && wrun == 0)
		{
			if(isdigit((unsigned char)*p))
			{
				wrun = atoi(p);
				while (*p && isdigit((unsigned char)*p)) p++;
			}
			else if(*p >= 'a' && *p <= 'y')
			{
				erun = *p - 'a' + 1;
				wrun = 1;
				p++;
			}
			else if(*p == 'z')
			{
				erun = 'z' - 'a' + 1;
				p++;
			}
			else
				valid = INVALID_WALLS;
		}
		if(erun > 0)
		{
			walls[i] = false;
			erun--;
		}
		else if(erun == 0 && wrun > 0)
		{
			walls[i] = true;
			wrun--;
		}
	}
	
	/* Merge horizontally */
	for(y = 0; y < h; y++)
	for(x = 0; x < w-1; x++)
	{
		i = (y*(w-1))+x;
		i1 = y*w+x;
		i2 = y*w+x+1;
		if(!walls[i])
			dsf_merge(state->dsf, i1, i2);
	}
	
	/* Merge vertically */
	for(y = 0; y < h-1; y++)
	for(x = 0; x < w; x++)
	{
		i = hs + (y*w+x);
		i1 = y*w+x;
		i2 = (y+1)*w+x;
		if(!walls[i])
			dsf_merge(state->dsf, i1, i2);
	}
	p++;
	erun = 0;
	for(i = 0; i < w*h; i++)
	{
		char c = 0;
		if (erun == 0)
		{
			if(*p)
			{
				c = *p++;
				if (c >= 'a' && c <= 'z')
					erun = c - 'a' + 1;
			}
			else
				c = 'S';
		}

		if (erun > 0)
		{
			c = 'S';
			erun--;
		}
		
		if(c >= '1' && c <= '9')
		{
			state->grid[i] = c - '0';
			state->flags[i] = FM_FIXED;
		}
		else
		{
			state->grid[i] = 0;
			state->flags[i] = 0;
		}
	}
	
	sfree(walls);
	*retstate = state;
	return valid;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
	int i, s, valid;
	s = params->w * params->h;
	game_state *state = NULL;
	
	valid = seismic_read_desc(params, desc, &state);
	
	if(valid == VALID)
	{
		for(i = 0; i < s; i++)
		{
			int size = dsf_size(state->dsf, i);
			if(size > 9)
				valid = INVALID_REGION;
			if(state->grid[i] > size)
				valid = INVALID_CLUESIZE;
		}
	}
	
	free_game(state);
	
	if(valid == INVALID_WALLS)
		return "Region description contains invalid characters";
	if(valid == INVALID_REGION)
		return "A region is too large";
	if(valid == INVALID_CLUESIZE)
		return "A clue is too large";
	
	return NULL;
}

static key_label *game_request_keys(const game_params *params, int *nkeys, int *arrow_mode)
{
	int i;
	int n = params->mode == MODE_TECTONIC ? 5 : 9;

	key_label *keys = snewn(n + 2, key_label);
    memset(keys, 0, (n + 2) * sizeof(key_label));
	*nkeys = n + 2;

	for (i = 0; i < n; i++)
	{
		char lb[2];
		lb[0] = (char)('1' + i); lb[1] = '\0';
		keys[i].button = '1' + i;
		keys[i].label = dupstr(lb);
		keys[i].needs_arrows = false;
	}
	keys[n].button = '\b';
	keys[n].label = dupstr("Clear");
	keys[n].needs_arrows = false;
	keys[n+1].button = 'M';
	keys[n+1].label = dupstr("Mark");
	keys[n+1].needs_arrows = false;
	*arrow_mode = ANDROID_ARROWS_LEFT_RIGHT;

	return keys;
}

static game_state *new_game(midend *me, const game_params *params, const char *desc)
{
	game_state *state = NULL;
	
	seismic_read_desc(params, desc, &state);
	
	return state;
}

static char *hint_game(const game_state *orig, const game_state *curr,
	const char **error)
{
	static char errbuf[96];
	int w = orig->w, s = w * orig->h, i, res;
	game_state *t = dup_game(orig);
	char *out = NULL, buf[40];

	hint_n = 0;
	hint_on = true;
	res = seismic_solve_game(t, DIFFCOUNT);
	hint_on = false;
	if(res != -1)
	{
		for(i = 0; i < s; i++)
		{
			if(curr->grid[i] && curr->grid[i] != t->grid[i])
			{
				sprintf(errbuf, _("There is a mistake in row %d, column %d"), i / w + 1, i % w + 1);
				*error = errbuf;
				free_game(t);
				return NULL;
			}
		}
	}
	free_game(t);
	for(i = 0; i < hint_n; i++)
	{
		int cl = hint_log[2*i], v = hint_log[2*i+1];
		if(curr->grid[cl]) continue;
		sprintf(buf, "R%d,%d,%c", cl % w, cl / w, '0' + v);
		out = dupstr(buf);
		break;
	}
	if(!out) *error = _("No logical next step found");
	return out;
}

static char *solve_game(const game_state *state, const game_state *currstate,
			const char *aux, const char **error)
{
	int i;
	int s = state->w * state->h;
	char *ret = snewn(s+2, char);
	char *p = ret;
	game_state *solved = dup_game(state);
	
	seismic_solve_game(solved, DIFFCOUNT);
	
	*p++ = 'S';
	for(i = 0; i < s; i++)
	{
		if(solved->grid[i] == 0)
			*p++ = '-';
		else
			*p++ = '0' + solved->grid[i];
	}
	*p++ = '\0';
	
	free_game(solved);
	return ret;
}

static bool game_can_format_as_text_now(const game_params *params)
{
	return true;
}

static char *game_text_format(const game_state *state)
{
	int w = state->w, h = state->h;
	int lr = w*2 + 2;

	char *ret = snewn((lr * (h*2+1)) + 1, char);
	char *p = ret;

	int x, y;
	
	*p++ = '+';
	for (x = 0; x < w; x++) {
		/* Place top line */
		*p++ = '-';
		*p++ = '+';
	}
	*p++ = '\n';
	
	for (y = 0; y < h; y++) {
		*p++ = '|';
		for (x = 0; x < w; x++) {
			/* Place number */
			char c = state->grid[y * w + x];
			*p++ = c > 0 ? c + '0' : '.';
			*p++ = x == w-1 || 
				dsf_canonify(state->dsf, y*w+x) != 
				dsf_canonify(state->dsf, y*w+x+1)
				? '|' : ' ';
		}
		
		*p++ = '\n';
		
		*p++ = '+';
		for (x = 0; x < w; x++) {
			/* Place bottom line */
			*p++ = y == h-1 || 
				dsf_canonify(state->dsf, y*w+x) != 
				dsf_canonify(state->dsf, (y+1)*w+x)
				? '-' : ' ';
			*p++ = '+';
		}
		/* End line */
		*p++ = '\n';
	}
	/* End with NUL */
	*p++ = '\0';

	return ret;
}

struct game_ui
{
	int hx, hy;
	bool cshow, ckey, cpencil;
	/* UI option for keeping cursor visible when typing mark */
	bool pencil_keep_highlight;
};

static game_ui *new_ui(const game_state *state)
{
	game_ui *ret = snew(game_ui);
	ret->hx = 0;
	ret->hy = 0;
    ret->cshow = ret->ckey = getenv_bool("PUZZLES_SHOW_CURSOR", false);
	ret->cpencil = false;
#ifdef ANDROID
	ret->pencil_keep_highlight = true;
#else
	ret->pencil_keep_highlight = false;
#endif
	
	return ret;
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

static config_item *get_prefs(game_ui *ui)
{
    config_item *ret;

    ret = snewn(N_PREF_ITEMS+1, config_item);

    ret[PREF_PENCIL_KEEP_HIGHLIGHT].name =
        "Keep mouse highlight after changing a pencil mark";
    ret[PREF_PENCIL_KEEP_HIGHLIGHT].kw = "pencil-keep-highlight";
    ret[PREF_PENCIL_KEEP_HIGHLIGHT].type = C_BOOLEAN;
    ret[PREF_PENCIL_KEEP_HIGHLIGHT].u.boolean.bval = ui->pencil_keep_highlight;

    ret[N_PREF_ITEMS].name = NULL;
    ret[N_PREF_ITEMS].type = C_END;

    return ret;
}

static void set_prefs(game_ui *ui, const config_item *cfg)
{
    ui->pencil_keep_highlight = cfg[PREF_PENCIL_KEEP_HIGHLIGHT].u.boolean.bval;
}

static bool game_changed_state(game_ui *ui, const game_state *oldstate,
							   const game_state *newstate)
{
#ifdef ANDROID
    if (ui->cshow && !ui->ckey && ui->cpencil &&
        newstate->grid[ui->hy * newstate->w + ui->hx] != 0)
        ui->cshow = ui->cpencil = false;
#endif
    return newstate->completed && !newstate->cheated &&
        oldstate && !oldstate->completed;
}
static const char *current_key_label(const game_ui *ui,
                                     const game_state *state, int button)
{
    if (ui->cshow && (button == CURSOR_SELECT))
        return ui->cpencil ? "Ink" : "Pencil";
    return "";
}

struct game_drawstate {
	int tilesize;
	int FIXME;
};

#define FROMCOORD(x) ( ((x)-(tilesize/2)) / tilesize )

static char *interpret_move(const game_state *state, game_ui *ui, const game_drawstate *ds,
				int ox, int oy, int button)
{
	int w = state->w;
	int h = state->h;
	int tilesize = ds->tilesize;
	
	int gx = FROMCOORD(ox);
	int gy = FROMCOORD(oy);
	int hx = ui->hx;
	int hy = ui->hy;
	
	char buf[80];
	
	button &= ~MOD_MASK;
	
	/* Mouse click */
	if (gx >= 0 && gx < w && gy >= 0 && gy < h)
	{
		/* Select square for letter placement */
		if (button == LEFT_BUTTON)
		{
			/* Select */
#ifdef ANDROID
			if(ui->cshow && hx == gx && hy == gy)
			{
				/* Tap on the selected square toggles pencil/ink */
				ui->cpencil = state->grid[gy*w+gx] != 0 ? false : !ui->cpencil;
			}
			else
			{
				ui->hx = gx;
				ui->hy = gy;
				ui->cpencil = false;
				ui->cshow = true;
			}
#else
			if(!ui->cshow || ui->cpencil || hx != gx || hy != gy)
			{
				ui->hx = gx;
				ui->hy = gy;
				ui->cpencil = false;
				ui->cshow = true;
			}
			/* Deselect */
			else
			{
				ui->cshow = false;
			}
#endif
			
			if(state->flags[gy*w+gx] & FM_FIXED)
			{
				ui->cshow = false;
#ifdef ANDROID
				ui->cpencil = false;
#endif
			}
			
			ui->ckey = false;
			return MOVE_UI_UPDATE;
		}
		/* Select square for marking */
		else if (button == RIGHT_BUTTON)
		{
			/* Select */
			if(!ui->cshow || !ui->cpencil || hx != gx || hy != gy)
			{
				ui->hx = gx;
				ui->hy = gy;
				ui->cpencil = true;
				ui->cshow = true;
			}
			/* Deselect */
			else
			{
				ui->cshow = false;
			}
			
			/* Remove the cursor again if the clicked square has a confirmed number */
			if(state->grid[gy*w+gx] != 0)
				ui->cshow = false;
			
			ui->ckey = false;
			return MOVE_UI_UPDATE;
		}
	}
	
	/* Keyboard move */
	if (IS_CURSOR_MOVE(button))
	{
		move_cursor(button, &ui->hx, &ui->hy, w, h, 0, NULL);
		ui->cshow = ui->ckey = true;
		return MOVE_UI_UPDATE;
	}
	
	/* Keyboard change pencil cursor */
	if (ui->cshow && button == CURSOR_SELECT)
	{
		ui->cpencil = !ui->cpencil;
		ui->ckey = true;
		return MOVE_UI_UPDATE;
	}
	
	/* Enter or remove numbers */
	if(ui->cshow && (
			(button >= '1' && button <= '9') || 
			button == CURSOR_SELECT2 || button == '\b' || button == '0'))
	{
		char c;
		if (button >= '1' && button <= '9')
			c = button - '0';
		else
			c = 0;
			
		/* Don't enter numbers out of range */
		if (c > dsf_size(state->dsf, hy*w+hx))
			return NULL;
		/* When in pencil mode, filled in squares cannot be changed */
		if (ui->cpencil && state->grid[hy*w+hx] != 0)
			return NULL;
		/* Avoid moves which don't change anything */
		if (!ui->cpencil && state->grid[hy*w+hx] == c) /* TODO hide cursor? */
			return NULL;
		/* Don't edit immutable numbers */
		if (state->flags[hy*w+hx] & FM_FIXED)
			return NULL;
		
		
		sprintf(buf, "%c%d,%d,%c",
				(char)(ui->cpencil ? 'P' : 'R'),
				hx, hy,
				(char)(c != 0 ? '0' + c : '-')
		);
		
		/* When not in keyboard mode, hide cursor */
		if (!ui->ckey && !(ui->cpencil && ui->pencil_keep_highlight))
			ui->cshow = false;
		
		return dupstr(buf);
	}
	
	if(button == 'M' || button == 'm')
	{
		int i;
		bool found = false;
		
		for(i = 0; i < w*h; i++)
		{
			if(state->grid[i] == 0 &&
				state->marks[i] != AREA_BITS(dsf_size(state->dsf, i)))
				found = true;
		}
		
		if(found)
			return dupstr("M");
	}
	
	return NULL;
}

static game_state *execute_move(const game_state *oldstate, const char *move)
{
	int w = oldstate->w;
	int h = oldstate->h;
	int x, y;
	char c;
	game_state *state;
	
	if ((move[0] == 'P' || move[0] == 'R') &&
			sscanf(move+1, "%d,%d,%c", &x, &y, &c) == 3 &&
			x >= 0 && x < w && y >= 0 && y < h &&
			((c >= '1' && c <= '9' ) || c == '-')
			)
	{
		if(oldstate->flags[y*w+x] & FM_FIXED)
			return NULL;
		
		state = dup_game(oldstate);
		
		if(move[0] == 'R')
		{
			if(c == '-')
				state->grid[y*w+x] = 0;
			else
				state->grid[y*w+x] = c - '0';
		}
		if(move[0] == 'P')
		{
			if(c == '-')
				state->marks[y*w+x] = 0;
			else
				state->marks[y*w+x] ^= NUM_BIT(c - '0');
		}
		
		if(seismic_validate_game(state) == STATUS_COMPLETE)
			state->completed = true;
		return state;
	}
	
	if(move[0] == 'S')
	{
		const char *p = move + 1;
		int i = 0;
		state = dup_game(oldstate);
		
		while(*p && i < w*h)
		{
			if(!(state->flags[i] & FM_FIXED))
			{
				c = *p;
				if(c >= '1' && c <= '9')
					state->grid[i] = c - '0';
				else
					state->grid[i] = 0;
			}
			p++;
			i++;
		}
		
		state->completed = (seismic_validate_game(state) == STATUS_COMPLETE);
		state->cheated = state->completed;
		return state;
	}
	
	if(move[0] == 'M')
	{
		int i;
		state = dup_game(oldstate);
		for(i = 0; i < w*h; i++)
		{
			if(state->grid[i] == 0)
				state->marks[i] = AREA_BITS(dsf_size(state->dsf, i));
		}
		return state;
	}
	
	return NULL;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
	if(ui->cshow) {
		*x = (ui->hx+0.5) * ds->tilesize;
		*y = (ui->hy+0.5) * ds->tilesize;
		*w = *h = ds->tilesize;
	}
}

static void game_compute_size(const game_params *params, int tilesize,
                              const game_ui *ui, int *x, int *y)
{
	*x = (params->w + 1) * tilesize;
	*y = (params->h + 1) * tilesize;
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

	frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);
	game_mkhighlight(fe, ret, COL_BACKGROUND, COL_HIGHLIGHT, COL_LOWLIGHT);
	
	ret[COL_BORDER*3 + 0] = 0.0F;
	ret[COL_BORDER*3 + 1] = 0.0F;
	ret[COL_BORDER*3 + 2] = 0.0F;
	
	ret[COL_NUM_FIXED*3 + 0] = 0.0F;
	ret[COL_NUM_FIXED*3 + 1] = 0.0F;
	ret[COL_NUM_FIXED*3 + 2] = 0.0F;
	
	ret[COL_NUM_GUESS*3 + 0] = 0.0F;
	ret[COL_NUM_GUESS*3 + 1] = 0.5F;
	ret[COL_NUM_GUESS*3 + 2] = 0.0F;
	
	ret[COL_NUM_ERROR*3 + 0] = 1.0F;
	ret[COL_NUM_ERROR*3 + 1] = 0.0F;
	ret[COL_NUM_ERROR*3 + 2] = 0.0F;
	
	ret[COL_NUM_PENCIL*3 + 0] = 0.0F;
	ret[COL_NUM_PENCIL*3 + 1] = 0.5F;
	ret[COL_NUM_PENCIL*3 + 2] = 0.5F;
	
	/* thin lines inside a region: a soft shade of the background */
	for (i = 0; i < 3; i++)
		ret[COL_GRIDLINE*3 + i] = ret[COL_BACKGROUND*3 + i] * 0.72F;

	ret[COL_ERRORDIST*3 + 0] = 1.0F;
	ret[COL_ERRORDIST*3 + 1] = 0.0F;
	ret[COL_ERRORDIST*3 + 2] = 0.0F;

	*ncolours = NCOLOURS;
	return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
	struct game_drawstate *ds = snew(struct game_drawstate);

	ds->tilesize = 0;
	ds->FIXME = 0;

	return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
	sfree(ds);
}

#define GRIDEXTRA 1

#define FLASH_TIME 0.7F
#define FLASH_FRAME 0.1F

static void game_redraw(drawing *dr, game_drawstate *ds, const game_state *oldstate,
			const game_state *state, int dir, const game_ui *ui,
			float animtime, float flashtime)
{
	int w = state->w;
	int h = state->h;
	int x, y, i1, i2, tx, ty;
	int cx, cy, cw, ch;
	int tilesize = ds->tilesize;
	DSF *dsf = state->dsf;
	int color;
	char c, p;
	char buf[2];
	bool cshow = ui->cshow;
	buf[1] = '\0';
	
	int flash = -1;
	if(flashtime > 0)
	{
		flash = (int)(flashtime / FLASH_FRAME) % 3;
		cshow = false;
	}
	
	/*
	 * The initial contents of the window are not guaranteed and
	 * can vary with front ends. To be on the safe side, all games
	 * should start by drawing a big background-colour rectangle
	 * covering the whole window.
	 */
	draw_rect(dr, 0, 0, (w+1)*tilesize, (h+1)*tilesize, COL_BACKGROUND);
	draw_update(dr, 0, 0, (w+1)*tilesize, (h+1)*tilesize);
	
	draw_rect(dr, (0.5*tilesize) - (GRIDEXTRA*2), 
		(0.5*tilesize) - (GRIDEXTRA*2),
		(w*tilesize) + (GRIDEXTRA*2),
		(h*tilesize) + (GRIDEXTRA*2),
		COL_BORDER);
	
	for(y = 0; y < h; y++)
	for(x = 0; x < w; x++)
	{
		i1 = y*w+x;
		c = state->grid[i1];
		p = state->marks[i1];
		
		if(flash == -1)
		{
			color = COL_BACKGROUND;
			
			if(cshow && !ui->cpencil && ui->hx == x && ui->hy == y)
				color = COL_HIGHLIGHT;
		}
		else
			color = (x+y) % 3 == flash ? COL_BACKGROUND :
				(x+y+1) % 3 == flash ? COL_LOWLIGHT : COL_HIGHLIGHT;
		
		tx = cx = (x+0.5) * tilesize;
		ty = cy = (y+0.5) * tilesize;
		cw = tilesize - 1;
		ch = tilesize - 1;
		
		i2 = y*w+x-1; /* Left */
		if(x == 0 || dsf_canonify(dsf, i1) != dsf_canonify(dsf, i2))
		{
			cx += GRIDEXTRA;
			cw -= GRIDEXTRA;
		}
		
		i2 = y*w+x+1; /* Right */
		if(x == w-1 || dsf_canonify(dsf, i1) != dsf_canonify(dsf, i2))
			cw -= GRIDEXTRA*2;
		
		i2 = (y-1)*w+x; /* Top */
		if(y == 0 || dsf_canonify(dsf, i1) != dsf_canonify(dsf, i2))
		{
			cy += GRIDEXTRA;
			ch -= GRIDEXTRA;
		}
		
		i2 = (y+1)*w+x; /* Bottom */
		if(y == h-1 || dsf_canonify(dsf, i1) != dsf_canonify(dsf, i2))
			ch -= GRIDEXTRA*2;
		
		draw_rect(dr, cx, cy, cw, ch, color);
		
		/* Soften the thin grid lines between cells of the same region, so that
		 * the heavy region borders stand out. */
		if(x+1 < w && dsf_canonify(dsf, i1) == dsf_canonify(dsf, y*w+x+1))
			draw_rect(dr, cx+cw, cy, 1, ch, COL_GRIDLINE);
		if(y+1 < h && dsf_canonify(dsf, i1) == dsf_canonify(dsf, (y+1)*w+x))
			draw_rect(dr, cx, cy+ch, cw, 1, COL_GRIDLINE);
		if(x+1 < w && y+1 < h &&
		   dsf_canonify(dsf, i1) == dsf_canonify(dsf, y*w+x+1) &&
		   dsf_canonify(dsf, i1) == dsf_canonify(dsf, (y+1)*w+x) &&
		   dsf_canonify(dsf, i1) == dsf_canonify(dsf, (y+1)*w+x+1))
			draw_rect(dr, cx+cw, cy+ch, 1, 1, COL_GRIDLINE);
		
		/* Pencil cursor */
		if(cshow && ui->cpencil && ui->hx == x && ui->hy == y)
		{
			int coords[6];
			
			coords[0] = cx;
			coords[1] = cy;
			coords[2] = cx+(tilesize/2);
			coords[3] = cy;
			coords[4] = cx;
			coords[5] = cy+(tilesize/2);
			draw_polygon(dr, coords, 3, COL_LOWLIGHT, COL_LOWLIGHT);
		}
		
		/* Draw pixels for cells located on a corner of the region */
		if (x > 0 && y > 0 && dsf_canonify(dsf, y*w+x) != dsf_canonify(dsf, (y-1)*w+x-1))
			draw_rect(dr, 1+tx-GRIDEXTRA, 1+ty-GRIDEXTRA, GRIDEXTRA, GRIDEXTRA, COL_BORDER);
		if (x+1 < w && y > 0 && dsf_canonify(dsf, y*w+x) != dsf_canonify(dsf, (y-1)*w+x+1))
			draw_rect(dr, tx+tilesize-2*GRIDEXTRA, 1+ty-GRIDEXTRA, GRIDEXTRA, GRIDEXTRA, COL_BORDER);
		if (x > 0 && y+1 < h && dsf_canonify(dsf, y*w+x) != dsf_canonify(dsf, (y+1)*w+x-1))
			draw_rect(dr, 1+tx-GRIDEXTRA, ty+tilesize-2*GRIDEXTRA, GRIDEXTRA, GRIDEXTRA, COL_BORDER);
		if (x+1 < w && y+1 < h && dsf_canonify(dsf, y*w+x) != dsf_canonify(dsf, (y+1)*w+x+1))
			draw_rect(dr, tx+tilesize-2*GRIDEXTRA, ty+tilesize-2*GRIDEXTRA, GRIDEXTRA, GRIDEXTRA, COL_BORDER);
		
		if(c == 0) /* Draw pencil marks */
		{
			int nhints, i, j, hw, hh, hmax, fontsz;
			for (i = nhints = 0; i < 9; i++) {
				if (p & (1<<i)) nhints++;
			}

			for (hw = 1; hw * hw < nhints; hw++);
			
			if (hw < 3) hw = 3;			
			hh = (nhints + hw - 1) / hw;
			if (hh < 2) hh = 2;
			hmax = max(hw, hh);
			fontsz = tilesize/(hmax*(11-hmax)/8);

			for (i = j = 0; i < 9; i++)
			{
				if (p & (1<<i))
				{
					int hx = j % hw, hy = j / hw;

					buf[0] = i+'1';
					
					draw_text(dr,
						cx + (4*hx+3) * tilesize / (4*hw+2),
						cy + (4*hy+3) * tilesize / (4*hh+2),
						FONT_VARIABLE, fontsz,
						ALIGN_VCENTRE | ALIGN_HCENTRE, COL_NUM_PENCIL, buf);
					j++;
				}
			}
		}
		else
		{
			color = state->flags[i1] & FM_FIXED ? COL_NUM_FIXED :
				state->flags[i1] & FM_ERRORMASK ? COL_NUM_ERROR :
				COL_NUM_GUESS;
			
			buf[0] = c + '0';
			
			draw_text(dr, (x+1)*tilesize, (y+1)*tilesize,
				FONT_VARIABLE, tilesize/2, ALIGN_HCENTRE|ALIGN_VCENTRE, color,
				buf);
		}
		
	}
}

static float game_anim_length(const game_state *oldstate, const game_state *newstate,
				  int dir, game_ui *ui)
{
	return 0.0F;
}

static float game_flash_length(const game_state *oldstate, const game_state *newstate,
				   int dir, game_ui *ui)
{
	if (!oldstate->completed && newstate->completed &&
			!oldstate->cheated && !newstate->cheated)
		return FLASH_TIME;
	return 0.0F;
}

static int game_status(const game_state *state)
{
	return state->completed ? +1 : 0;
}

static bool game_timing_state(const game_state *state, game_ui *ui)
{
	return true;
}

/* Using 9mm squares */
#define PRINT_SQUARE_SIZE 900
#ifndef NO_PRINTING
static void game_print_size(const game_params *params, const game_ui *ui,
                            float *x, float *y)
{
	int pw, ph;

	game_compute_size(params, PRINT_SQUARE_SIZE, ui, &pw, &ph);
	*x = pw / 100.0F;
	*y = ph / 100.0F;
}
#endif /* NO_PRINTING */

#ifndef NO_PRINTING
static void game_print(drawing *dr, const game_state *state, const game_ui *ui,
                       int tilesize)
{
	int w = state->w;
	int h = state->h;
	int x, y, i1, i2;
	int ink = print_mono_colour(dr, 0);
	int line = print_grey_colour(dr, 0.90F);
	char buf[2];
	buf[1] = '\0';
	
	for(x = 1; x < w; x++)
	for(y = 0; y < h; y++)
	{
		draw_line(dr, (x+0.5)*tilesize, (y+0.5)*tilesize, 
			(x+0.5)*tilesize, (y+1.5)*tilesize, line);
	}
	for(x = 0; x < w; x++)
	for(y = 1; y < h; y++)
	{
		draw_line(dr, (x+0.5)*tilesize, (y+0.5)*tilesize, 
				(x+1.5)*tilesize, (y+0.5)*tilesize, line);
	}
	
	print_line_width(dr, tilesize / 30);
	for(y = 0; y < h; y++)
	for(x = 0; x < w; x++)
	{
		i1 = y*w+x;
		
		i2 = y*w+x+1;
		
		if(x == 0)
			draw_line(dr, (x+0.5)*tilesize, (y+0.5)*tilesize, 
				(x+0.5)*tilesize, (y+1.5)*tilesize, ink);
		
		if(x == w-1 || dsf_canonify(state->dsf, i1) != dsf_canonify(state->dsf, i2))
			draw_line(dr, (x+1.5)*tilesize, (y+0.5)*tilesize, 
				(x+1.5)*tilesize, (y+1.5)*tilesize, ink);
		
		i2 = (y+1)*w+x;
		
		if(y == 0)
			draw_line(dr, (x+0.5)*tilesize, (y+0.5)*tilesize, 
				(x+1.5)*tilesize, (y+0.5)*tilesize, ink);
		
		if(y == h-1 || dsf_canonify(state->dsf, i1) != dsf_canonify(state->dsf, i2))
			draw_line(dr, (x+0.5)*tilesize, (y+1.5)*tilesize, 
				(x+1.5)*tilesize, (y+1.5)*tilesize, ink);
		
		if(state->grid[i1] > 0)
		{
			buf[0] = state->grid[i1] + '0';
			draw_text(dr, (x+1)*tilesize, (y+1)*tilesize,
				FONT_VARIABLE, tilesize/2, ALIGN_HCENTRE|ALIGN_VCENTRE, ink,
				buf);
		}
	}
}
#endif /* NO_PRINTING */

#ifdef COMBINED
#define thegame seismic
#endif

const struct game thegame = {
	"Seismic", "games.seismic", "seismic",
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
	encode_ui,
	decode_ui,
	game_request_keys,
	NULL, /* android_cursor_visibility */
	game_changed_state,
	current_key_label,
	interpret_move,
	execute_move,
	48, game_compute_size, game_set_size,
	game_colours,
	game_new_drawstate,
	game_free_drawstate,
	game_redraw,
	game_anim_length,
	game_flash_length,
	game_get_cursor_location,
	game_status,
#ifndef NO_PRINTING
	true, false, game_print_size, game_print,
#endif
	false,			       /* wants_statusbar */
	false, game_timing_state,
	REQUIRE_RBUTTON, /* flags */
	hint_game,
};

/* ***************** *
 * Standalone solver *
 * ***************** */

#ifdef STANDALONE_SOLVER
#include <time.h>
#include <stdarg.h>

/* Most of the standalone solver code was copied from unequal.c and singles.c */

const char *quis;

static void usage_exit(const char *msg)
{
	if (msg)
		fprintf(stderr, "%s: %s\n", quis, msg);
	fprintf(stderr,
			"Usage: %s [-v] [--seed SEED] <params> | [game_id [game_id ...]]\n",
			quis);
	exit(1);
}

int main(int argc, char *argv[])
{
	random_state *rs;
	time_t seed = time(NULL);

	game_params *params = NULL;

	char *id = NULL, *desc = NULL;
	const char *err;

	quis = argv[0];

	while (--argc > 0) {
		char *p = *++argv;
		if (!strcmp(p, "--seed")) {
			if (argc == 0)
				usage_exit("--seed needs an argument");
			seed = (time_t) atoi(*++argv);
			argc--;
		} else if (!strcmp(p, "-v"))
			solver_verbose = true;
		else if (*p == '-')
			usage_exit("unrecognised option");
		else
			id = p;
	}

	if (id) {
		desc = strchr(id, ':');
		if (desc)
			*desc++ = '\0';

		params = default_params();
		decode_params(params, id);
		err = validate_params(params, true);
		if (err) {
			fprintf(stderr, "Parameters are invalid\n");
			fprintf(stderr, "%s: %s", argv[0], err);
			exit(1);
		}
	}

	if (!desc) {
		char *desc_gen, *aux;
		rs = random_new((void *) &seed, sizeof(time_t));
		if (!params)
			params = default_params();
		printf("Generating puzzle with parameters %s\n",
			   encode_params(params, true));
		desc_gen = new_game_desc(params, rs, &aux, false);

		if (!solver_verbose) {
			char *fmt = game_text_format(new_game(NULL, params, desc_gen));
			fputs(fmt, stdout);
			sfree(fmt);
		}

		printf("Game ID: %s\n", desc_gen);
	} else {
		game_state *input;
		int maxdiff;

		err = validate_desc(params, desc);
		if (err) {
			fprintf(stderr, "Description is invalid\n");
			fprintf(stderr, "%s", err);
			exit(1);
		}

		input = new_game(NULL, params, desc);

		maxdiff = seismic_solve_game(input, DIFFCOUNT);

		char *fmt = game_text_format(input);
		fputs(fmt, stdout);
		sfree(fmt);
		if (maxdiff < 0)
			printf("No solution found.\n");
		else
			printf("Difficulty: %s\n", seismic_diffnames[maxdiff]);

		free_game(input);
	}

	return 0;
}
#endif
