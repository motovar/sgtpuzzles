/*
 * separate.c: Implementation of `Block Puzzle', a Japanese-only
 * Nikoli puzzle seen at
 *   http://www.nikoli.co.jp/ja/puzzles/block_puzzle/
 * 
 * It's difficult to be absolutely sure of the rules since online
 * Japanese translators are so bad, but looking at the sample
 * puzzle it seems fairly clear that the rules of this one are
 * very simple. You have an mxn grid in which every square
 * contains a letter, there are k distinct letters with k dividing
 * mn, and every letter occurs the same number of times; your aim
 * is to find a partition of the grid into disjoint k-ominoes such
 * that each k-omino contains exactly one of each letter.
 * 
 * (It may be that Nikoli always have m,n,k equal to one another.
 * However, I don't see that that's critical to the puzzle; k|mn
 * is the only really important constraint, and even that could
 * probably be dispensed with if some squares were marked as
 * unused.)
 */

/*
 * Status of the solver/generator (the interface was added later for the
 * Android build): it is working in principle but _very_ slow. It generates
 * 5x5n5 or 6x6n4 readily enough, 6x6n6 with a bit of effort, and
 * 7x7n7 only with a serious strain. I haven't dared try it higher
 * than that yet.
 * 
 * One idea to speed it up is to implement more of the solver.
 * Ideas I've so far had include:
 * 
 *  - Generalise the deduction currently expressed as `an
 *    undersized chain with only one direction to extend must take
 *    it'. More generally, the deduction should say `if all the
 *    possible k-ominoes containing a given chain also contain
 *    square x, then mark square x as part of that k-omino'.
 *     + For example, consider this case:
 * 
 *         a ? b    This represents the top left of a board; the letters
 *         ? ? ?    a,b,c do not represent the letters used in the puzzle,
 *         c ? ?    but indicate that those three squares are known to be
 *                  of different ominoes. Now if k >= 4, we can immediately
 *         deduce that the square midway between b and c belongs to the
 *         same omino as a, because there is no way we can make a 4-or-
 *         more-omino containing a which does not also contain that square.
 *         (Most easily seen by imagining cutting that square out of the 
 *         grid; then, clearly, the omino containing a has only two
 *         squares to expand into, and needs at least three.)
 * 
 *    The key difficulty with this mode of reasoning is
 *    identifying such squares. I can't immediately think of a
 *    simple algorithm for finding them on a wholesale basis.
 * 
 *  - Bfs out from a chain looking for the letters it lacks. For
 *    example, in this situation (top three rows of a 7x7n7 grid):
 * 
 *        +-----------+-+
 *        |E-A-F-B-C D|D|
 *        +-------     ||
 *        |E-C-G-D G|G E|
 *        +-+---        |
 *        |E|E G A B F A|
 *
 *    In this situation we can be sure that the top left chain
 *    E-A-F-B-C does extend rightwards to the D, because there is
 *    no other D within reach of that chain. Note also that the
 *    bfs can skip squares which are known to belong to other
 *    ominoes than this one.
 * 
 *    (This deduction, I fear, should only be used in an
 *    emergency, because it relies on _all_ squares within range
 *    of the bfs having particular values and so using it during
 *    incremental generation rather nails down a lot of the grid.)
 * 
 * It's conceivable that another thing we could do would be to
 * increase the flexibility in the grid generator: instead of
 * nailing down the _value_ of any square depended on, merely nail
 * down its equivalence to other squares. Unfortunately this turns
 * the letter-selection phase of generation into a general graph
 * colouring problem (we must draw a graph with equivalence
 * classes of squares as the vertices, and an edge between any two
 * vertices representing equivalence classes which contain squares
 * that share an omino, and then k-colour the result) and hence
 * requires recursion, which bodes ill for something we're doing
 * that many times per generation.
 * 
 * I suppose a simple thing I could try would be tuning the retry
 * count, just in case it's set too high or too low for efficient
 * generation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#ifdef NO_TGMATH_H
#  include <math.h>
#else
#  include <tgmath.h>
#endif

#include "puzzles.h"

#define MAXLETTERS 12

enum {
    COL_BACKGROUND,
    COL_GRID,
    COL_LINK,
    COL_TEXT,
    COL_ERROR,
    COL_DONE,
    COL_PEND,
    COL_PENDDEL,
    COL_CURSOR,
    COL_LETTER0,
    NCOLOURS = COL_LETTER0 + MAXLETTERS
};

struct game_params {
    int w, h, k;
};

struct letters {
    int refcount;
    unsigned char *grid;               /* letter index 0..k-1 per square */
};

struct game_state {
    int w, h, k;
    struct letters *letters;
    /* links[i]: bit 0 = squares i and i+1 are joined, bit 1 = i and i+w */
    unsigned char *links;
    bool completed, cheated;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->w = ret->h = ret->k = 5;      /* FIXME: a bit bigger? */

    return ret;
}

static const struct game_params separate_presets[] = {
    { 4, 4, 4 },
    { 5, 5, 5 },
    { 6, 6, 4 },
    { 6, 6, 6 },
};

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    char buf[80];

    if (i < 0 || i >= lenof(separate_presets))
        return false;

    ret = snew(game_params);
    *ret = separate_presets[i];
    sprintf(buf, "%dx%d, %d letters", ret->w, ret->h, ret->k);
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
    *ret = *params;		       /* structure copy */
    return ret;
}

static void decode_params(game_params *params, char const *string)
{
    params->w = params->h = params->k = atoi(string);
    while (*string && isdigit((unsigned char)*string)) string++;
    if (*string == 'x') {
        string++;
        params->h = atoi(string);
	while (*string && isdigit((unsigned char)*string)) string++;
    }
    if (*string == 'n') {
        string++;
        params->k = atoi(string);
	while (*string && isdigit((unsigned char)*string)) string++;
    }
}

static char *encode_params(const game_params *params, bool full)
{
    char buf[256];
    sprintf(buf, "%dx%dn%d", params->w, params->h, params->k);
    return dupstr(buf);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret = snewn(4, config_item);
    char buf[80];

    ret[0].name = "Width";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[0].u.string.sval = dupstr(buf);

    ret[1].name = "Height";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->h);
    ret[1].u.string.sval = dupstr(buf);

    ret[2].name = "Letters (region size)";
    ret[2].type = C_STRING;
    sprintf(buf, "%d", params->k);
    ret[2].u.string.sval = dupstr(buf);

    ret[3].name = NULL;
    ret[3].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].u.string.sval);
    ret->h = atoi(cfg[1].u.string.sval);
    ret->k = atoi(cfg[2].u.string.sval);

    return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
    if (params->w < 2 || params->h < 2)
        return "Width and height must both be at least two";
    if (params->w > INT_MAX / params->h)
        return "Width times height must not be unreasonably large";
    if (params->k < 2)
        return "Number of letters must be at least two";
    if (params->k > MAXLETTERS)
        return "Number of letters must be at most 12";
    if ((params->w * params->h) % params->k)
        return "Number of letters must divide the number of squares";
    return NULL;
}

/* ----------------------------------------------------------------------
 * Solver and generator.
 */

struct solver_scratch {
    int w, h, k;

    /*
     * Tracks connectedness between squares.
     */
    DSF *dsf;

    /*
     * size[dsf_canonify(dsf, yx)] tracks the size of the
     * connected component containing yx.
     */
    int *size;

    /*
     * contents[dsf_canonify(dsf, yx)*k+i] tracks whether or not
     * the connected component containing yx includes letter i. If
     * the value is -1, it doesn't; otherwise its value is the
     * index in the main grid of the square which contributes that
     * letter to the component.
     */
    int *contents;

    /*
     * disconnect[dsf_canonify(dsf, yx1)*w*h + dsf_canonify(dsf, yx2)]
     * tracks whether or not the connected components containing
     * yx1 and yx2 are known to be distinct.
     */
    bool *disconnect;

    /*
     * Temporary space used only inside particular solver loops.
     */
    int *tmp;
};

static struct solver_scratch *solver_scratch_new(int w, int h, int k)
{
    int wh = w*h;
    struct solver_scratch *sc = snew(struct solver_scratch);

    sc->w = w;
    sc->h = h;
    sc->k = k;

    sc->dsf = dsf_new(wh);
    sc->size = snewn(wh, int);
    sc->contents = snewn(wh * k, int);
    sc->disconnect = snewn(wh*wh, bool);
    sc->tmp = snewn(wh, int);

    return sc;
}

static void solver_scratch_free(struct solver_scratch *sc)
{
    dsf_free(sc->dsf);
    sfree(sc->size);
    sfree(sc->contents);
    sfree(sc->disconnect);
    sfree(sc->tmp);
    sfree(sc);
}

static void solver_connect(struct solver_scratch *sc, int yx1, int yx2)
{
    int w = sc->w, h = sc->h, k = sc->k;
    int wh = w*h;
    int i, yxnew;

    yx1 = dsf_canonify(sc->dsf, yx1);
    yx2 = dsf_canonify(sc->dsf, yx2);
    assert(yx1 != yx2);

    /*
     * To connect two components together into a bigger one, we
     * start by merging them in the dsf itself.
     */
    dsf_merge(sc->dsf, yx1, yx2);
    yxnew = dsf_canonify(sc->dsf, yx2);

    /*
     * The size of the new component is the sum of the sizes of the
     * old ones.
     */
    sc->size[yxnew] = sc->size[yx1] + sc->size[yx2];

    /*
     * The contents bitmap of the new component is the union of the
     * contents of the old ones.
     * 
     * Given two numbers at most one of which is not -1, we can
     * find the other one by adding the two and adding 1; this
     * will yield -1 if both were -1 to begin with, otherwise the
     * other.
     * 
     * (A neater approach would be to take their bitwise AND, but
     * this is unfortunately not well-defined standard C when done
     * to signed integers.)
     */
    for (i = 0; i < k; i++) {
	assert(sc->contents[yx1*k+i] < 0 || sc->contents[yx2*k+i] < 0);
	sc->contents[yxnew*k+i] = (sc->contents[yx1*k+i] +
				   sc->contents[yx2*k+i] + 1);
    }

    /*
     * We must combine the rows _and_ the columns in the disconnect
     * matrix.
     */
    for (i = 0; i < wh; i++)
	sc->disconnect[yxnew*wh+i] = (sc->disconnect[yx1*wh+i] ||
				      sc->disconnect[yx2*wh+i]);
    for (i = 0; i < wh; i++)
	sc->disconnect[i*wh+yxnew] = (sc->disconnect[i*wh+yx1] ||
				      sc->disconnect[i*wh+yx2]);
}

static void solver_disconnect(struct solver_scratch *sc, int yx1, int yx2)
{
    int w = sc->w, h = sc->h;
    int wh = w*h;

    yx1 = dsf_canonify(sc->dsf, yx1);
    yx2 = dsf_canonify(sc->dsf, yx2);
    assert(yx1 != yx2);
    assert(!sc->disconnect[yx1*wh+yx2]);
    assert(!sc->disconnect[yx2*wh+yx1]);

    /*
     * Mark the components as disconnected from each other in the
     * disconnect matrix.
     */
    sc->disconnect[yx1*wh+yx2] = true;
    sc->disconnect[yx2*wh+yx1] = true;
}

static void solver_init(struct solver_scratch *sc)
{
    int w = sc->w, h = sc->h;
    int wh = w*h;
    int i;

    /*
     * Set up most of the scratch space. We don't set up the
     * contents array, however, because this will change if we
     * adjust the letter arrangement and re-run the solver.
     */
    dsf_reinit(sc->dsf);
    for (i = 0; i < wh; i++) sc->size[i] = 1;
    memset(sc->disconnect, 0, wh*wh * sizeof(bool));
}

static int solver_attempt(struct solver_scratch *sc, const unsigned char *grid,
                          bool *gen_lock)
{
    int w = sc->w, h = sc->h, k = sc->k;
    int wh = w*h;
    int i, x, y;
    bool done_something_overall = false;

    /*
     * Set up the contents array from the grid.
     */
    for (i = 0; i < wh*k; i++)
	sc->contents[i] = -1;
    for (i = 0; i < wh; i++)
	sc->contents[dsf_canonify(sc->dsf, i)*k+grid[i]] = i;

    while (1) {
	bool done_something = false;

	/*
	 * Go over the grid looking for reasons to add to the
	 * disconnect matrix. We're after pairs of squares which:
	 * 
	 *  - are adjacent in the grid
	 *  - belong to distinct dsf components
	 *  - their components are not already marked as
	 *    disconnected
	 *  - their components share a letter in common.
	 */
	for (y = 0; y < h; y++) {
	    for (x = 0; x < w; x++) {
		int dir;
		for (dir = 0; dir < 2; dir++) {
		    int x2 = x + dir, y2 = y + 1 - dir;
		    int yx = y*w+x, yx2 = y2*w+x2;

		    if (x2 >= w || y2 >= h)
			continue;      /* one square is outside the grid */

		    yx = dsf_canonify(sc->dsf, yx);
		    yx2 = dsf_canonify(sc->dsf, yx2);
		    if (yx == yx2)
			continue;      /* same dsf component */

		    if (sc->disconnect[yx*wh+yx2])
			continue;      /* already known disconnected */

		    for (i = 0; i < k; i++)
			if (sc->contents[yx*k+i] >= 0 &&
			    sc->contents[yx2*k+i] >= 0)
			    break;
		    if (i == k)
			continue;      /* no letter in common */

		    /*
		     * We've found one. Mark yx and yx2 as
		     * disconnected from each other.
		     */
#ifdef SOLVER_DIAGNOSTICS
		    printf("Disconnecting %d and %d (%c)\n", yx, yx2, 'A'+i);
#endif
		    solver_disconnect(sc, yx, yx2);
		    done_something = done_something_overall = true;

		    /*
		     * We have just made a deduction which hinges
		     * on two particular grid squares being the
		     * same. If we are feeding back to a generator
		     * loop, we must therefore mark those squares
		     * as fixed in the generator, so that future
		     * rearrangement of the grid will not break
		     * the information on which we have already
		     * based deductions.
		     */
		    if (gen_lock) {
			gen_lock[sc->contents[yx*k+i]] = true;
			gen_lock[sc->contents[yx2*k+i]] = true;
		    }
		}
	    }
	}

	/*
	 * Now go over the grid looking for dsf components which
	 * are below maximum size and only have one way to extend,
	 * and extending them.
	 */
	for (i = 0; i < wh; i++)
	    sc->tmp[i] = -1;
	for (y = 0; y < h; y++) {
	    for (x = 0; x < w; x++) {
		int yx = dsf_canonify(sc->dsf, y*w+x);
		int dir;

		if (sc->size[yx] == k)
		    continue;

		for (dir = 0; dir < 4; dir++) {
		    int x2 = x + (dir==0 ? -1 : dir==2 ? 1 : 0);
		    int y2 = y + (dir==1 ? -1 : dir==3 ? 1 : 0);
		    int yx2, yx2c;

		    if (y2 < 0 || y2 >= h || x2 < 0 || x2 >= w)
			continue;
		    yx2 = y2*w+x2;
		    yx2c = dsf_canonify(sc->dsf, yx2);

		    if (yx2c != yx && !sc->disconnect[yx2c*wh+yx]) {
			/*
			 * Component yx can be extended into square
			 * yx2.
			 */
			if (sc->tmp[yx] == -1)
			    sc->tmp[yx] = yx2;
			else if (sc->tmp[yx] != yx2)
			    sc->tmp[yx] = -2;   /* multiple choices found */
		    }
		}
	    }
	}
	for (i = 0; i < wh; i++) {
	    if (sc->tmp[i] >= 0) {
		/*
		 * Make sure we haven't connected the two already
		 * during this loop (which could happen if for
		 * _both_ components this was the only way to
		 * extend them).
		 */
		if (dsf_canonify(sc->dsf, i) ==
		    dsf_canonify(sc->dsf, sc->tmp[i]))
		    continue;

#ifdef SOLVER_DIAGNOSTICS
		printf("Connecting %d and %d\n", i, sc->tmp[i]);
#endif
		solver_connect(sc, i, sc->tmp[i]);
		done_something = done_something_overall = true;
		break;
	    }
	}

	if (!done_something)
	    break;
    }

    /*
     * Return 0 if we haven't made any progress; 1 if we've done
     * something but not solved it completely; 2 if we've solved
     * it completely.
     */
    for (i = 0; i < wh; i++)
	if (sc->size[dsf_canonify(sc->dsf, i)] != k)
	    break;
    if (i == wh)
	return 2;
    if (done_something_overall)
	return 1;
    return 0;
}

static unsigned char *generate(int w, int h, int k, random_state *rs)
{
    int wh = w*h;
    int n = wh/k;
    struct solver_scratch *sc;
    unsigned char *grid;
    unsigned char *shuffled;
    int i, j, m, retries;
    int *permutation;
    bool *gen_lock;

    sc = solver_scratch_new(w, h, k);
    grid = snewn(wh, unsigned char);
    shuffled = snewn(k, unsigned char);
    permutation = snewn(wh, int);
    gen_lock = snewn(wh, bool);

    do {
	DSF *dsf = divvy_rectangle(w, h, k, rs);

	/*
	 * Go through the dsf and find the indices of all the
	 * squares involved in each omino, in a manner conducive
	 * to per-omino indexing. We set permutation[i*k+j] to be
	 * the index of the jth square (ordered arbitrarily) in
	 * omino i.
	 */
	for (i = j = 0; i < wh; i++)
	    if (dsf_canonify(dsf, i) == i) {
		sc->tmp[i] = j;
		/*
		 * During this loop and the following one, we use
		 * the last element of each row of permutation[]
		 * as a counter of the number of indices so far
		 * placed in it. When we place the final index of
		 * an omino, that counter is overwritten, but that
		 * doesn't matter because we'll never use it
		 * again. Of course this depends critically on
		 * divvy_rectangle() having returned correct
		 * results, or else chaos would ensue.
		 */
		permutation[j*k+k-1] = 0;
		j++;
	    }
	for (i = 0; i < wh; i++) {
	    j = sc->tmp[dsf_canonify(dsf, i)];
	    m = permutation[j*k+k-1]++;
	    permutation[j*k+m] = i;
	}

	/*
	 * Track which squares' letters we have already depended
	 * on for deductions. This is gradually updated by
	 * solver_attempt().
	 */
	memset(gen_lock, 0, wh * sizeof(bool));

	/*
	 * Now repeatedly fill the grid with letters, and attempt
	 * to solve it. If the solver makes progress but does not
	 * fail completely, then gen_lock will have been updated
	 * and we try again. On a complete failure, though, we
	 * have no option but to give up and abandon this set of
	 * ominoes.
	 */
	solver_init(sc);
	retries = k*k;
	while (1) {
	    /*
	     * Fill the grid with letters. We can safely use
	     * sc->tmp to hold the set of letters required at each
	     * stage, since it's at least size k and is currently
	     * unused.
	     */
	    for (i = 0; i < n; i++) {
		/*
		 * First, determine the set of letters already
		 * placed in this omino by gen_lock.
		 */
		for (j = 0; j < k; j++)
		    sc->tmp[j] = j;
		for (j = 0; j < k; j++) {
		    int index = permutation[i*k+j];
		    int letter = grid[index];
		    if (gen_lock[index])
			sc->tmp[letter] = -1;
		}
		/*
		 * Now collect together all the remaining letters
		 * and randomly shuffle them.
		 */
		for (j = m = 0; j < k; j++)
		    if (sc->tmp[j] >= 0)
			sc->tmp[m++] = sc->tmp[j];
		shuffle(sc->tmp, m, sizeof(*sc->tmp), rs);
		/*
		 * Finally, write the shuffled letters into the
		 * grid.
		 */
		for (j = 0; j < k; j++) {
		    int index = permutation[i*k+j];
		    if (!gen_lock[index])
			grid[index] = sc->tmp[--m];
		}
		assert(m == 0);
	    }

	    /*
	     * Now we have a candidate grid. Attempt to progress
	     * the solution.
	     */
	    m = solver_attempt(sc, grid, gen_lock);
	    if (m == 2 ||	       /* success */
		(m == 0 && retries-- <= 0))   /* failure */
		break;
	    if (m == 1)
		retries = k*k;	       /* reset this counter, and continue */
	}

	dsf_free(dsf);
    } while (m == 0);

    sfree(gen_lock);
    sfree(permutation);
    sfree(shuffled);
    solver_scratch_free(sc);

    return grid;
}

/* ----------------------------------------------------------------------
 * End of solver/generator code.
 */

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
    int w = params->w, h = params->h, wh = w*h, k = params->k;
    unsigned char *grid;
    char *desc;
    int i;

    grid = generate(w, h, k, rs);

    desc = snewn(wh+1, char);
    for (i = 0; i < wh; i++)
	desc[i] = 'A' + grid[i];
    desc[wh] = '\0';

    sfree(grid);

    return desc;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    int wh = params->w * params->h, k = params->k, i;
    int count[MAXLETTERS];

    if ((int)strlen(desc) != wh)
        return "Game description is the wrong length";
    memset(count, 0, sizeof(count));
    for (i = 0; i < wh; i++) {
        if (desc[i] < 'A' || desc[i] >= 'A' + k)
            return "Game description contains an invalid letter";
        count[desc[i] - 'A']++;
    }
    for (i = 0; i < k; i++)
        if (count[i] != wh / k)
            return "Each letter must occur the same number of times";
    return NULL;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    game_state *state = snew(game_state);
    int wh = params->w * params->h, i;

    state->w = params->w;
    state->h = params->h;
    state->k = params->k;
    state->letters = snew(struct letters);
    state->letters->refcount = 1;
    state->letters->grid = snewn(wh, unsigned char);
    for (i = 0; i < wh; i++)
        state->letters->grid[i] = desc[i] - 'A';
    state->links = snewn(wh, unsigned char);
    memset(state->links, 0, wh);
    state->completed = state->cheated = false;

    return state;
}

static game_state *dup_game(const game_state *state)
{
    game_state *ret = snew(game_state);
    int wh = state->w * state->h;

    *ret = *state;
    ret->letters->refcount++;
    ret->links = snewn(wh, unsigned char);
    memcpy(ret->links, state->links, wh);

    return ret;
}

static void free_game(game_state *state)
{
    if (--state->letters->refcount <= 0) {
        sfree(state->letters->grid);
        sfree(state->letters);
    }
    sfree(state->links);
    sfree(state);
}

/*
 * Solve with the generator's own solver. Returns, for each square, the
 * canonical index of the region it belongs to in the solution, or NULL
 * if the solver could not finish.
 */
static int *solve_regions(int w, int h, int k, const unsigned char *grid)
{
    int wh = w*h, i;
    struct solver_scratch *sc = solver_scratch_new(w, h, k);
    int *ret = NULL;

    solver_init(sc);
    if (solver_attempt(sc, grid, NULL) == 2) {
        ret = snewn(wh, int);
        for (i = 0; i < wh; i++)
            ret[i] = dsf_canonify(sc->dsf, i);
    }
    solver_scratch_free(sc);
    return ret;
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, const char **error)
{
    int w = state->w, h = state->h, wh = w*h, i;
    int *reg = solve_regions(w, h, state->k, state->letters->grid);
    char *ret;

    if (!reg) {
        *error = "Unable to solve this puzzle";
        return NULL;
    }
    ret = snewn(wh + 2, char);
    ret[0] = 'S';
    for (i = 0; i < wh; i++) {
        int x = i % w, y = i / w, v = 0;
        if (x+1 < w && reg[i] == reg[i+1]) v |= 1;
        if (y+1 < h && reg[i] == reg[i+w]) v |= 2;
        ret[i+1] = '0' + v;
    }
    ret[wh+1] = '\0';
    sfree(reg);
    return ret;
}

static bool game_can_format_as_text_now(const game_params *params)
{
    return true;
}

static char *game_text_format(const game_state *state)
{
    int w = state->w, h = state->h, x, y;
    char *ret = snewn((w+1)*h + 1, char), *p = ret;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++)
            *p++ = 'A' + state->letters->grid[y*w+x];
        *p++ = '\n';
    }
    *p = '\0';
    return ret;
}

struct game_ui {
    int cx, cy;
    bool cur_visible;
    bool dragging, kdrag;
    int *path, npath;
    int mode;                          /* +1 joining, -1 separating, 0 undecided */
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);
    ui->cx = ui->cy = 0;
    ui->cur_visible = getenv_bool("PUZZLES_SHOW_CURSOR", false);
    ui->dragging = ui->kdrag = false;
    ui->path = snewn(state->w * state->h, int);
    ui->npath = 0;
    ui->mode = 0;
    return ui;
}

static void free_ui(game_ui *ui)
{
    sfree(ui->path);
    sfree(ui);
}

static void android_cursor_visibility(game_ui *ui, int visible)
{
    ui->cur_visible = visible;
}

/* Components of the current links, used for error checking and drawing. */
static void compute_regions(const game_state *state, DSF *dsf)
{
    int w = state->w, h = state->h, i;
    dsf_reinit(dsf);
    for (i = 0; i < w*h; i++) {
        int x = i % w, y = i / w;
        if ((state->links[i] & 1) && x+1 < w) dsf_merge(dsf, i, i+1);
        if ((state->links[i] & 2) && y+1 < h) dsf_merge(dsf, i, i+w);
    }
}

static bool check_complete(const game_state *state)
{
    int w = state->w, h = state->h, k = state->k, wh = w*h, i;
    DSF *dsf = dsf_new(wh);
    unsigned long *mask = snewn(wh, unsigned long);
    bool ok = true;

    compute_regions(state, dsf);
    memset(mask, 0, wh * sizeof(unsigned long));
    for (i = 0; i < wh; i++) {
        int c = dsf_canonify(dsf, i);
        unsigned long b = 1UL << state->letters->grid[i];
        if (mask[c] & b) ok = false;
        mask[c] |= b;
    }
    for (i = 0; i < wh && ok; i++)
        if (dsf_size(dsf, i) != k) ok = false;
    dsf_free(dsf);
    sfree(mask);
    return ok;
}

static bool game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
    ui->dragging = ui->kdrag = false;
    ui->npath = 0;
    return newstate->completed && oldstate && !oldstate->completed &&
        !newstate->cheated;
}

#define PREFERRED_TILESIZE 40
#define TILESIZE (ds->tilesize)
#define BORDER (TILESIZE / 2)
#define COORD(c) ((c) * TILESIZE + BORDER)
#define FROMCOORD(p) (((p) - BORDER + TILESIZE) / TILESIZE - 1)

#define FLASH_TIME 0.4F

struct game_drawstate {
    int tilesize;
    bool started;
    unsigned long *cells;
};

/* link state between adjacent squares a and b (adjacent, a < b) */
static bool is_linked(const game_state *state, int a, int b)
{
    if (b == a + 1) return (state->links[a] & 1) != 0;
    return (state->links[a] & 2) != 0;
}

static bool path_step(game_ui *ui, const game_state *state, int next)
{
    int last, d;
    int w = state->w;
    if (ui->npath == 0) return false;
    last = ui->path[ui->npath - 1];
    d = abs(next % w - last % w) + abs(next / w - last / w);
    if (d != 1) return false;
    if (ui->npath >= 2 && ui->path[ui->npath - 2] == next) {
        ui->npath--;                   /* stepped back */
        if (ui->npath == 1) ui->mode = 0;
        return true;
    }
    {
        int i;
        for (i = 0; i < ui->npath; i++)
            if (ui->path[i] == next) return false;
    }
    if (ui->npath == 1) {
        int a = last < next ? last : next, b = last < next ? next : last;
        ui->mode = is_linked(state, a, b) ? -1 : +1;
    }
    ui->path[ui->npath++] = next;
    return true;
}

static void path_extend(game_ui *ui, const game_state *state, int cell)
{
    int w = state->w;
    int guard = state->w + state->h + 2;
    while (guard-- > 0 && ui->npath > 0 && ui->path[ui->npath-1] != cell) {
        int last = ui->path[ui->npath-1];
        int lx = last % w, ly = last / w, cx = cell % w, cy = cell / w;
        int next;
        if (lx != cx) lx += (cx > lx ? 1 : -1);
        else ly += (cy > ly ? 1 : -1);
        next = ly * w + lx;
        if (!path_step(ui, state, next)) break;
    }
}

static char *path_commit(game_ui *ui, const game_state *state)
{
    int i, nops = 0;
    char *ret = snewn(ui->npath * 24 + 4, char), *p = ret;
    int w = state->w;
    *p = '\0';
    for (i = 0; i + 1 < ui->npath; i++) {
        int a = ui->path[i], b = ui->path[i+1], t;
        bool linked, want = (ui->mode > 0);
        if (a > b) { t = a; a = b; b = t; }
        linked = is_linked(state, a, b);
        if (linked == want) continue;
        p += sprintf(p, "%s%c%d,%d,%d", nops ? ";" : "", want ? '+' : '-',
                     a % w, a / w, b == a + 1 ? 0 : 1);
        nops++;
    }
    ui->dragging = ui->kdrag = false;
    ui->npath = 0;
    ui->mode = 0;
    if (!nops) { sfree(ret); return NULL; }
    return ret;
}

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    int w = state->w, h = state->h;
    int cx, cy, cell;

    button &= ~MOD_MASK;

    if (IS_MOUSE_DOWN(button) || IS_MOUSE_DRAG(button) || IS_MOUSE_RELEASE(button)) {
        cx = FROMCOORD(x);
        cy = FROMCOORD(y);
        if (x < BORDER) cx = -1;
        if (y < BORDER) cy = -1;
        cell = (cx >= 0 && cx < w && cy >= 0 && cy < h) ? cy * w + cx : -1;
        ui->cur_visible = false;

        if (button == LEFT_BUTTON) {
            if (cell < 0) return MOVE_UNUSED;
            ui->dragging = true;
            ui->kdrag = false;
            ui->npath = 1;
            ui->path[0] = cell;
            ui->mode = 0;
            return MOVE_UI_UPDATE;
        } else if (button == LEFT_DRAG) {
            if (!ui->dragging) return MOVE_UNUSED;
            if (cell >= 0) path_extend(ui, state, cell);
            return MOVE_UI_UPDATE;
        } else if (button == LEFT_RELEASE) {
            char *m;
            if (!ui->dragging) return MOVE_UNUSED;
            if (cell >= 0) path_extend(ui, state, cell);
            m = path_commit(ui, state);
            return m ? m : MOVE_UI_UPDATE;
        } else if (button == RIGHT_BUTTON) {
            char buf[32];
            if (cell < 0) return MOVE_UNUSED;
            /* long press: detach this square from its neighbours */
            sprintf(buf, "D%d,%d", cx, cy);
            return dupstr(buf);
        }
        return MOVE_UI_UPDATE;
    }

    if (IS_CURSOR_MOVE(button)) {
        if (!ui->cur_visible) {
            ui->cur_visible = true;
            return MOVE_UI_UPDATE;
        }
        move_cursor(button, &ui->cx, &ui->cy, w, h, false, NULL);
        if (ui->kdrag)
            path_extend(ui, state, ui->cy * w + ui->cx);
        return MOVE_UI_UPDATE;
    }

    if (IS_CURSOR_SELECT(button)) {
        if (button == CURSOR_SELECT2) {
            char buf[32];
            if (!ui->cur_visible) { ui->cur_visible = true; return MOVE_UI_UPDATE; }
            sprintf(buf, "D%d,%d", ui->cx, ui->cy);
            return dupstr(buf);
        }
        if (!ui->cur_visible) {
            ui->cur_visible = true;
            return MOVE_UI_UPDATE;
        }
        if (!ui->kdrag) {
            ui->kdrag = ui->dragging = true;
            ui->npath = 1;
            ui->path[0] = ui->cy * w + ui->cx;
            ui->mode = 0;
            return MOVE_UI_UPDATE;
        } else {
            char *m = path_commit(ui, state);
            return m ? m : MOVE_UI_UPDATE;
        }
    }

    return MOVE_UNUSED;
}

static game_state *execute_move(const game_state *state, const char *move)
{
    int w = state->w, h = state->h, wh = w*h, i;
    game_state *ret;

    if (move[0] == 'S') {
        if ((int)strlen(move + 1) != wh) return NULL;
        ret = dup_game(state);
        for (i = 0; i < wh; i++) {
            int v = move[i+1] - '0';
            if (v < 0 || v > 3) { free_game(ret); return NULL; }
            ret->links[i] = v;
        }
        ret->cheated = true;
        ret->completed = check_complete(ret);
        return ret;
    }

    ret = dup_game(state);
    while (*move) {
        int x, y, d, n = 0;
        char op = *move;
        if (op == 'D' && sscanf(move + 1, "%d,%d%n", &x, &y, &n) == 2 &&
            x >= 0 && x < w && y >= 0 && y < h) {
            int c = y * w + x;
            ret->links[c] = 0;
            if (x > 0) ret->links[c-1] &= ~1;
            if (y > 0) ret->links[c-w] &= ~2;
            move += 1 + n;
        } else if ((op == '+' || op == '-') &&
                   sscanf(move + 1, "%d,%d,%d%n", &x, &y, &d, &n) == 3 &&
                   x >= 0 && x < w && y >= 0 && y < h && (d == 0 || d == 1) &&
                   (d == 0 ? x + 1 < w : y + 1 < h)) {
            if (op == '+') ret->links[y*w+x] |= (d ? 2 : 1);
            else ret->links[y*w+x] &= ~(d ? 2 : 1);
            move += 1 + n;
        } else {
            free_game(ret);
            return NULL;
        }
        if (*move == ';') move++;
    }
    if (!ret->completed && check_complete(ret))
        ret->completed = true;
    return ret;
}

/* Next step: remove a wrong link first, otherwise join two squares that
 * belong together in the solution. */
static char *hint_game(const game_state *orig, const game_state *curr,
                       const char **error)
{
    int w = curr->w, h = curr->h, wh = w*h, i;
    int *reg = solve_regions(w, h, curr->k, curr->letters->grid);
    DSF *dsf;
    char buf[40], *ret = NULL;

    if (!reg) {
        *error = "Unable to solve this puzzle";
        return NULL;
    }
    dsf = dsf_new(wh);
    compute_regions(curr, dsf);
    for (i = 0; i < wh && !ret; i++) {
        int x = i % w, y = i / w;
        if ((curr->links[i] & 1) && x+1 < w && reg[i] != reg[i+1]) {
            sprintf(buf, "-%d,%d,0", x, y); ret = dupstr(buf);
        } else if ((curr->links[i] & 2) && y+1 < h && reg[i] != reg[i+w]) {
            sprintf(buf, "-%d,%d,1", x, y); ret = dupstr(buf);
        }
    }
    for (i = 0; i < wh && !ret; i++) {
        int x = i % w, y = i / w;
        if (x+1 < w && reg[i] == reg[i+1] && !dsf_equivalent(dsf, i, i+1)) {
            sprintf(buf, "+%d,%d,0", x, y); ret = dupstr(buf);
        } else if (y+1 < h && reg[i] == reg[i+w] && !dsf_equivalent(dsf, i, i+w)) {
            sprintf(buf, "+%d,%d,1", x, y); ret = dupstr(buf);
        }
    }
    dsf_free(dsf);
    sfree(reg);
    if (!ret) *error = "No next step found";
    return ret;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_compute_size(const game_params *params, int tilesize,
                              const game_ui *ui, int *x, int *y)
{
    *x = params->w * tilesize + tilesize;
    *y = params->h * tilesize + tilesize;
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

    for (i = 0; i < 3; i++) {
        ret[COL_GRID * 3 + i] = ret[COL_BACKGROUND * 3 + i] * 0.6F;
        ret[COL_LINK * 3 + i] = 0.15F;
        ret[COL_TEXT * 3 + i] = 0.0F;
    }
    ret[COL_ERROR*3+0] = 0.85F; ret[COL_ERROR*3+1] = 0.0F; ret[COL_ERROR*3+2] = 0.0F;
    ret[COL_DONE*3+0] = 0.0F; ret[COL_DONE*3+1] = 0.55F; ret[COL_DONE*3+2] = 0.15F;
    ret[COL_PEND*3+0] = 0.0F; ret[COL_PEND*3+1] = 0.3F; ret[COL_PEND*3+2] = 0.95F;
    ret[COL_PENDDEL*3+0] = 0.95F; ret[COL_PENDDEL*3+1] = 0.5F; ret[COL_PENDDEL*3+2] = 0.0F;
    ret[COL_CURSOR*3+0] = 0.8F; ret[COL_CURSOR*3+1] = 0.0F; ret[COL_CURSOR*3+2] = 0.8F;

    for (i = 0; i < MAXLETTERS; i++) {
        /* pastel colours spread around the hue circle */
        float hue = (float)i / MAXLETTERS * 6.0F;
        int sector = (int)hue;
        float f = hue - sector, lo = 0.72F, hi = 0.97F;
        float r, g, b, mid = lo + (hi - lo) * f, mid2 = hi - (hi - lo) * f;
        switch (sector % 6) {
          case 0: r = hi;   g = mid;  b = lo;   break;
          case 1: r = mid2; g = hi;   b = lo;   break;
          case 2: r = lo;   g = hi;   b = mid;  break;
          case 3: r = lo;   g = mid2; b = hi;   break;
          case 4: r = mid;  g = lo;   b = hi;   break;
          default: r = hi;  g = lo;   b = mid2; break;
        }
        ret[(COL_LETTER0 + i) * 3 + 0] = r;
        ret[(COL_LETTER0 + i) * 3 + 1] = g;
        ret[(COL_LETTER0 + i) * 3 + 2] = b;
    }

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);
    int wh = state->w * state->h, i;

    ds->tilesize = 0;
    ds->started = false;
    ds->cells = snewn(wh, unsigned long);
    for (i = 0; i < wh; i++) ds->cells[i] = ~0UL;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->cells);
    sfree(ds);
}

/* per-square display word */
#define F_LETTER_MASK 0x1F
#define F_LINK_SHIFT  5     /* 4 bits: R, D, L, U */
#define F_PEND_SHIFT  9     /* 4 bits: R, D, L, U pending */
#define F_PENDDEL     (1UL << 13)
#define F_ERROR       (1UL << 14)
#define F_DONE        (1UL << 15)
#define F_CURSOR      (1UL << 16)
#define F_FLASH       (1UL << 17)

static void draw_cell(drawing *dr, game_drawstate *ds, int x, int y,
                      unsigned long f)
{
    int T = TILESIZE, px = COORD(x), py = COORD(y);
    int cxm = px + T/2, cym = py + T/2, i;
    int letter = f & F_LETTER_MASK;
    int linkcol = (f & F_ERROR) ? COL_ERROR : (f & F_DONE) ? COL_DONE : COL_LINK;
    float thick = T / 5.0F;
    static const int dx[4] = {1, 0, -1, 0}, dy[4] = {0, 1, 0, -1};

    clip(dr, px, py, T, T);
    draw_rect(dr, px, py, T, T, COL_LETTER0 + letter);
    if (f & F_FLASH)
        draw_rect(dr, px, py, T, T, COL_BACKGROUND);
    draw_rect_outline(dr, px, py, T + 1, T + 1, COL_GRID);

    for (i = 0; i < 4; i++)
        if (f & (1UL << (F_LINK_SHIFT + i)))
            draw_thick_line(dr, thick, cxm, cym,
                            cxm + dx[i] * T / 2.0F, cym + dy[i] * T / 2.0F, linkcol);
    for (i = 0; i < 4; i++)
        if (f & (1UL << (F_PEND_SHIFT + i)))
            draw_thick_line(dr, (f & F_PENDDEL) ? thick / 2 : thick * 0.7F,
                            cxm, cym,
                            cxm + dx[i] * T / 2.0F, cym + dy[i] * T / 2.0F,
                            (f & F_PENDDEL) ? COL_PENDDEL : COL_PEND);
    draw_circle(dr, cxm, cym, T / 3, COL_LETTER0 + letter, linkcol);
    {
        char s[2] = { (char)('A' + letter), 0 };
        draw_text(dr, cxm, cym, FONT_VARIABLE, T / 2,
                  ALIGN_HCENTRE | ALIGN_VCENTRE,
                  (f & F_ERROR) ? COL_ERROR : COL_TEXT, s);
    }
    if (f & F_CURSOR)
        draw_rect_outline(dr, px + 1, py + 1, T - 2, T - 2, COL_CURSOR);
    unclip(dr);
    draw_update(dr, px, py, T + 1, T + 1);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int w = state->w, h = state->h, wh = w*h, k = state->k, i;
    DSF *dsf = dsf_new(wh);
    unsigned long *mask = snewn(wh, unsigned long);
    bool *dup = snewn(wh, bool);
    unsigned long *word = snewn(wh, unsigned long);
    bool flash = flashtime > 0 && (int)(flashtime / (FLASH_TIME / 3)) % 2 == 0;

    if (!ds->started) {
        draw_rect(dr, 0, 0, (w + 1) * TILESIZE, (h + 1) * TILESIZE, COL_BACKGROUND);
        draw_update(dr, 0, 0, (w + 1) * TILESIZE, (h + 1) * TILESIZE);
        ds->started = true;
    }

    compute_regions(state, dsf);
    memset(mask, 0, wh * sizeof(unsigned long));
    memset(dup, 0, wh * sizeof(bool));
    for (i = 0; i < wh; i++) {
        int c = dsf_canonify(dsf, i);
        unsigned long b = 1UL << state->letters->grid[i];
        if (mask[c] & b) dup[c] = true;
        mask[c] |= b;
    }
    for (i = 0; i < wh; i++) {
        int x = i % w, y = i / w, c = dsf_canonify(dsf, i);
        unsigned long f = state->letters->grid[i];
        int sz = dsf_size(dsf, i);
        if (x+1 < w && (state->links[i] & 1)) f |= 1UL << (F_LINK_SHIFT + 0);
        if (y+1 < h && (state->links[i] & 2)) f |= 1UL << (F_LINK_SHIFT + 1);
        if (x > 0 && (state->links[i-1] & 1)) f |= 1UL << (F_LINK_SHIFT + 2);
        if (y > 0 && (state->links[i-w] & 2)) f |= 1UL << (F_LINK_SHIFT + 3);
        if (dup[c] || sz > k) f |= F_ERROR;
        else if (sz == k) f |= F_DONE;
        if (ui && ui->cur_visible && ui->cx == x && ui->cy == y) f |= F_CURSOR;
        if (flash) f |= F_FLASH;
        word[i] = f;
    }
    if (ui && ui->dragging) {
        int j;
        for (j = 0; j + 1 < ui->npath; j++) {
            int a = ui->path[j], b = ui->path[j+1];
            int ax = a % w, ay = a / w, bx = b % w, by = b / w;
            int da = bx > ax ? 0 : by > ay ? 1 : bx < ax ? 2 : 3;
            word[a] |= 1UL << (F_PEND_SHIFT + da);
            word[b] |= 1UL << (F_PEND_SHIFT + ((da + 2) & 3));
            if (ui->mode < 0) { word[a] |= F_PENDDEL; word[b] |= F_PENDDEL; }
        }
        if (ui->npath > 0 && ui->kdrag)
            word[ui->path[ui->npath-1]] |= F_CURSOR;
    }
    for (i = 0; i < wh; i++)
        if (ds->cells[i] != word[i]) {
            ds->cells[i] = word[i];
            draw_cell(dr, ds, i % w, i / w, word[i]);
        }

    dsf_free(dsf);
    sfree(mask);
    sfree(dup);
    sfree(word);
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
        !oldstate->cheated && !newstate->cheated)
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
        *x = COORD(ui->cx);
        *y = COORD(ui->cy);
        *w = *h = TILESIZE;
    }
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
#define thegame separate
#endif

const struct game thegame = {
    "Separate", "games.separate", "separate",
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
    NULL, NULL, /* get_prefs, set_prefs */
    new_ui,
    free_ui,
    NULL, /* encode_ui */
    NULL, /* decode_ui */
    NULL, /* game_request_keys */
    android_cursor_visibility,
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
