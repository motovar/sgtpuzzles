/*
 * YASGen 1.50 (Yet Another Sokoban Level Generator - For Small Levels) by
 * Brian Damgaard, translated to C.  See yasgen150.c.
 */
#ifndef YASGEN150_H
#define YASGEN150_H

/* The settings of the generator window in Sokoban YASC, with the defaults
 * that YASC uses (yg150_defaults). */
typedef struct {
    int width, height;      /* size of the interior; a wall border is added   */
    int wall_pct;           /* interior walls in percent of the interior (40) */
    int max_boxes;          /* "maximum number of boxes/goals" (4)            */
    int open_limit;         /* "maximum number of open positions"; 0 = off    */
    int memory_mb;          /* transposition table in MiB (YASGen: 128)       */
    int population;         /* 50                                             */
    int inactivity;         /* inactivity threshold (20); 0 = off             */
    int crossover_pct;      /* 50                                             */
    int mutation_pct;       /* 80                                             */
    int max_mutations;      /* 2                                              */
    int connected_goals;    /* 0 = goals scattered at random (default)        */
    int fitness_score;      /* 0 = pushes (default), 1 = box lines + changes  */
    int trivial_opening;    /* 1 = trivial opening pushes ignored (default)   */
    int tail_pushes;        /* tail position threshold (1); 0 = off           */
    int forward;            /* 0 = backward search from goals (default)       */
    unsigned seed;          /* random number seed                             */
    int max_candidates;     /* stop after this many candidates (YASGen: 100000) */
    double seconds;         /* no new candidate is started after this time    */
    double hard_seconds;    /* a running search is abandoned after this time  */
} yg150_params;

void yg150_defaults(yg150_params *p);

/* Runs the genetic algorithm and writes the best level found to 'out':
 * (width+2)*(height+2) characters (XSB notation, no line separators).
 * Returns the number of pushes of the level (0 = nothing found);
 * '*candidates' (may be NULL) receives the number of tested candidates. */
int yg150_generate(const yg150_params *p, char *out, int *candidates);

#endif
