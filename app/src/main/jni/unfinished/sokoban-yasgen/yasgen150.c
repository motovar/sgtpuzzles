/*
 * YASGen - Yet Another Sokoban Level Generator - For Small Levels
 * Version 1.50
 *
 * Copyright (c) 2001-2024 by Brian Damgaard, Denmark
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * ---------------------------------------------------------------------------
 *
 * This file is a translation to C of the Delphi source "YASGen_.pas" (the
 * generator plug-in for Sokoban YASC, with the genetic algorithm generator),
 * done function by function so that the method of generating levels is the
 * same as in the original:
 *
 *   - the board generator for the random seed candidates,
 *   - the breadth-first search (backward: boxes are pulled away from the goal
 *     squares; forward: boxes are pushed) with the transposition table, the
 *     "tail pushes" search, the trimming of trivial opening pushes,
 *   - the genetic algorithm: rank selection, cross-over, mutations,
 *     replacement, inactivity rule, history of seen candidates, and the
 *     sequence in which the plug-in generates and tests the candidates.
 *
 * What is not translated: everything that has to do with the user interface,
 * files, templates with fixed squares, statistics and the plug-in threads.
 * What is added: the stop criteria (time, number of candidates) that a phone
 * needs, because the original runs until the user stops it.  The names of the
 * Delphi functions are kept so that the code can be compared with the original.
 *
 * Differences caused by the C language are noted where they matter:
 *   - the 'with' statement of Pascal gives precedence to record fields over
 *     local variables; this is kept in mind in the places where names clash;
 *   - positions are addressed by 32-bit indices into the table instead of
 *     pointers (the table is a single array here, the original uses 8 equal
 *     banks; the search order through the banks is the same).
 */
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "yasgen150.h"

/* ------------------------------------------------------------------------ */
/* Constants */

#define BOX     4   /* board squares, internal representation (as in YASC) */
#define FLOOR   1
#define GOAL    2
#define PLAYER  8
#define WALL    16

#define MAX_BOARD_WIDTH   50
#define MAX_BOARD_HEIGHT  MAX_BOARD_WIDTH
#define MAX_BOARD_SIZE    ((MAX_BOARD_WIDTH + 2) * (MAX_BOARD_HEIGHT + 2))
#define MAX_BOX_COUNT     255
#define MAX_HASH_COLLISION_COUNT (32 * 1024)
#define MAX_HISTORY_MOVES 499
#define MEMORY_BLOCKS_COUNT 8
#define MIN_BOARD_WIDTH   3
#define MIN_BOARD_HEIGHT  3
#define MAX_TAIL_POSITION_THRESHOLD 4

#define DIRECTION_BIT_COUNT 3
#define DIRECTION_BIT_MASK  ((1 << DIRECTION_BIT_COUNT) - 1)
#define POSITION_TAG_CLOSED (1 << (DIRECTION_BIT_COUNT + 1))
#define POSITION_TAG_TAIL   (POSITION_TAG_CLOSED << 1)
#define POSITION_TAG_BIT_MASK (POSITION_TAG_CLOSED + POSITION_TAG_TAIL)

enum { dUp, dLeft, dDown, dRight };
static const int OPPOSITE_DIRECTION[4] = { dDown, dRight, dUp, dLeft };

#define GA_MAX_INDIVIDUALS      100
#define GA_MAX_PENDING_CANDIDATES 50
#define GA_HIGH_INDIVIDUALS     (GA_MAX_INDIVIDUALS + GA_MAX_PENDING_CANDIDATES)
#define GA_RANDOM_CANDIDATES_COUNT 10
#define GA_DEFAULT_MAX_CANDIDATE_COUNT (100 * 1000)

enum { ffPushes, ffScore };
enum { gaAddItem, gaDeleteItem, gaMoveItem, gaAddWall, gaDeleteWall, gaMoveWall };
static const int GA_DEFAULT_MUTATION_TYPE_ACCUMULATED_FREQUENCIES[6] = { 10, 20, 70, 80, 90, 100 };

/* ------------------------------------------------------------------------ */
/* Types */

typedef int64_t THashValue;
typedef uint8_t TBoard[MAX_BOARD_SIZE + 1];
typedef int TBoardInteger[MAX_BOARD_SIZE + 1];

typedef struct { uint8_t BoxNo; uint8_t Direction; } TMove;

typedef struct {
    int Count;                      /* number of pushes */
    int Fitness;                    /* estimated complexity */
    TMove Moves[MAX_HISTORY_MOVES + 1];
    int Score;                      /* estimated number of box-lines + box-changes */
    int TailCount;                  /* trivial end-game pushes */
} THistory;

typedef struct {
    TBoard Board;                   /* current position */
    THashValue BoardHashValue;
    int BoardHeight;
    int BoardSize;
    int BoardWidth;
    int BoxCount;
    int BoxPos[MAX_BOX_COUNT + 1];
    int FloorCount;
    int GoalCount;
    int GoalPos[MAX_BOX_COUNT + 1];
    THistory History;
    int PlayerPos;
    int NeighbourSquareOffset[4];
    TBoardInteger PlayersReachableSquares;
    int PlayersReachableSquaresTimeStamp;
    int ReverseMode;                /* generate level by pulling boxes away from goal positions */
} TGame;

typedef struct {
    int BoxCount;
    int MaxBoxCount;
    int MaxBoxCountEnabled;
    int MaxOpenPositions;
    int MaxOpenPositionsEnabled;
    int PlayerAccessAreaNo;
    int PlayerIndex;
    int RandomSeed;
    int TailPushesThreshold;
    int TailPushesThresholdEnabled;
    int TemplatePlayerPos;          /* always 0 (no template) */
    int TrivialOpeningPushesIgnored;
    uint8_t VisitedPlayerPos[MAX_BOARD_SIZE + 1];
    int WallCount;
} TGenerator;

/* A position in the transposition table.  'Move.Direction' carries the
 * direction in the low bits and the flags POSITION_TAG_xxx above them. */
typedef struct {
    THashValue HashValue;
    uint32_t Next;                  /* open list / later the current path */
    uint32_t Parent;                /* ancestor node */
    uint16_t Depth;                 /* minimum number of pushes for reaching this position */
    uint16_t PlayerPos;
    uint16_t Score;                 /* estimated box-lines + box-changes */
    uint8_t MoveBoxNo;
    uint8_t MoveDirection;
} TPosition;

typedef struct {
    int Count;
    uint32_t Head, Tail;
} TPositionQueue;

typedef struct {
    uint32_t BestPosition;          /* best position found so far */
    size_t Capacity;                /* total capacity */
    int Count;                      /* number of stored positions */
    uint32_t CurrentPosition;       /* Game.Board state matches this position */
    int DroppedCount;               /* number of dropped positions */
    struct {
        int CollisionCount;
        size_t ItemIndex;
        size_t VectorIndex;         /* the bank */
        THashValue HashValue;
    } LookupState;
    TPositionQueue OpenPositions;
    int RepetitionCount;
    int TailPositionCount;          /* number of trivial end-game positions */
    int TotalCount, TotalDroppedCount, TotalRepetitionCount;
    size_t VectorCapacity;          /* capacity per bank */
    TPosition *Slots;
    uint32_t *Used;                 /* slots in use, so clearing is cheap */
    size_t UsedCount, UsedCap;
} TPositions;

typedef struct {
    char Alleles[MAX_BOARD_SIZE + 1];   /* the chromosome: a candidate board as text */
    int CandidateNo;
    int ChildrenCount;
    int Fitness;
    int PushCount;
    int Score;
    int Uid;                        /* identity of the individual (the 'level' in YASC) */
} TGAIndividual;

typedef struct {
    TBoard Board;
    TGAIndividual Individual;
} TGAExtendedIndividual;

typedef struct {
    int CrossOverProbabilityPct;
    int FitnessFunction;
    int ForwardSearch;
    int GenerateConnectedGoals;
    int InactivityThreshold;
    int InactivityThresholdEnabled;
    int MaxMutationCount;
    int MutateWallsOnly;
    int MutationProbabilityPct;
    int MutationTypeAccumulatedFrequencies[6];
    int PopulationSize;
} TGAControl;

typedef struct {
    int CandidatesCount;
    int CrossoverCount;
    int InactivityCount;
    int LastNewCandidateNo;
    int MemoryFullCandidatesCount;
    int MutationCount;
} TGAStatistics;

typedef struct {
    int AllelesCount;               /* number of alleles in the chromosomes */
    int BestIndividualNo;
    TGAControl Control;
    int GenerationCount;
    int IndividualCount;            /* population size + candidates not analysed yet */
    TGAIndividual *Individuals;     /* item 0 unused (temporary) */
    int NextUid;
    int CurrentUid;                 /* the candidate being processed ('Level' of the generator plug-in), 0 = none */
    int ParentUid1, ParentUid2;     /* the parents of the candidate (the 'ParentLevel's), 0 = none */
    TGAStatistics Statistics;
    int WorstIndividualNo;
} TGA;

/* ------------------------------------------------------------------------ */
/* Global variables */

static TGame Game;
static THashValue HashSquareValues[MAX_BOARD_SIZE + 1];   /* Zobrist values */
static TGenerator Generator;
static TPositions Positions;
static struct { int RandomNumber; } RandomState;
static TGA GA;

/* The tests for "the user has terminated the generator" in the plug-in
 * become a deadline here. */
static int Aborted;
static double AbortTime;            /* 0 = no hard deadline */
static unsigned AbortTick;

#define P(id) (&Positions.Slots[(size_t)(id) - 1])

/* ------------------------------------------------------------------------ */
/* General utilities */

static double GetTimeSeconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int ThreadActive(void)       /* ThreadState=ptsProcessLevel */
{
    return !Aborted;
}

static int ThreadActiveTick(void)   /* the same, with an occasional look at the clock */
{
    if (!Aborted && AbortTime > 0 && ((++AbortTick) & 63) == 0 &&
        GetTimeSeconds() > AbortTime)
        Aborted = 1;
    return !Aborted;
}

static int Max(int a, int b) { return a >= b ? a : b; }
static int Min(int a, int b) { return a <= b ? a : b; }
static int Sign(int v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }

static void InitializeRandomState(int RandomNumber)
{
    RandomState.RandomNumber = RandomNumber < 0 ? -RandomNumber : RandomNumber;
}

/* the level generator uses its own simple random number function so results
 * are reproducible */
static int Random(int Range)
{
    const int a = 1366, c = 150889, m = 714025;
    RandomState.RandomNumber = (a * (RandomState.RandomNumber % m) + c) % m;
    if (Range != 0)
        return RandomState.RandomNumber % Range;
    return 0;
}

static int ColRowToSquare(int Col, int Row, int ColCount, int RowCount)
{
    (void)RowCount;
    return (Row * (ColCount + 2)) + Col;
}

static void SquareToColRow(int Square, int *Col, int *Row)
{
    *Row = Square / (Game.BoardWidth + 2);
    *Col = Square - *Row * (Game.BoardWidth + 2);
}

static int IsABoxSquare(int Square)   { return (Game.Board[Square] & BOX) != 0; }
static int IsAFloorSquare(int Square) { return (Game.Board[Square] & FLOOR) != 0; }
static int IsAGoalSquare(int Square)  { return (Game.Board[Square] & GOAL) != 0; }

/* chromosomes are boards as text (XSB notation) */
static char BoardSquareValueToChar(int Value)
{
    switch (Value & (PLAYER + BOX + GOAL + WALL)) {
      case PLAYER:        return '@';
      case PLAYER + GOAL: return '+';
      case BOX:           return '$';
      case BOX + GOAL:    return '*';
      case GOAL:          return '.';
      case WALL:          return '#';
      default:            return ' ';
    }
}

static int CharToBoardSquareValue(char c)
{
    switch (c) {
      case '$': return BOX + FLOOR;
      case '*': return BOX + GOAL + FLOOR;
      case ' ': return FLOOR;
      case '.': return GOAL + FLOOR;
      case '#': return WALL;
      case '@': return PLAYER + FLOOR;
      case '+': return PLAYER + GOAL + FLOOR;
      default:  return 0;
    }
}

/* ------------------------------------------------------------------------ */
/* Pathfinding: the player's reachable squares */

static int CalculatePlayersReachableSquares(int *MinPlayerPos)
{
    static int Stack[MAX_BOARD_SIZE + 2];
    int Result = 0, Square, NeighbourSquare, Dir, Top;

    *MinPlayerPos = Game.PlayerPos;

    /* a complete reset is only necessary when the timestamp overflows */
    if (Game.PlayersReachableSquaresTimeStamp == INT_MAX) {
        Game.PlayersReachableSquaresTimeStamp = 0;
        memset(Game.PlayersReachableSquares, 0,
               sizeof(Game.PlayersReachableSquares[0]) * (size_t)(Game.BoardSize + 1));
    }
    Game.PlayersReachableSquaresTimeStamp++;

    if (Game.PlayerPos != 0) {
        Result = 1;
        Top = 1;
        Stack[1] = Game.PlayerPos;
        Game.PlayersReachableSquares[Game.PlayerPos] = Game.PlayersReachableSquaresTimeStamp;
        while (Top != 0) {
            Square = Stack[Top--];
            for (Dir = dUp; Dir <= dRight; Dir++) {
                NeighbourSquare = Square + Game.NeighbourSquareOffset[Dir];
                if ((Game.Board[NeighbourSquare] & (WALL + BOX)) == 0 &&
                    Game.PlayersReachableSquares[NeighbourSquare] != Game.PlayersReachableSquaresTimeStamp) {
                    Result++;
                    Stack[++Top] = NeighbourSquare;
                    Game.PlayersReachableSquares[NeighbourSquare] = Game.PlayersReachableSquaresTimeStamp;
                    if (NeighbourSquare < *MinPlayerPos) *MinPlayerPos = NeighbourSquare;
                }
            }
        }
    }
    return Result;
}

/* ------------------------------------------------------------------------ */
/* Moves */

static void DoMove(int BoxNo, int Direction)
{
    int FromPos, ToPos;
    if (BoxNo != 0) {               /* only pushes are handled */
        Direction = Direction & DIRECTION_BIT_MASK;
        FromPos = Game.BoxPos[BoxNo];
        ToPos = FromPos + Game.NeighbourSquareOffset[Direction];
        Game.Board[FromPos] -= BOX;
        Game.Board[ToPos] += BOX;
        Game.BoxPos[BoxNo] = ToPos;
        Game.Board[Game.PlayerPos] &= (uint8_t)~PLAYER;
        if (Game.ReverseMode)
            Game.PlayerPos = ToPos + Game.NeighbourSquareOffset[Direction];
        else
            Game.PlayerPos = FromPos;
        Game.Board[Game.PlayerPos] += PLAYER;
        Game.BoardHashValue = (Game.BoardHashValue ^ HashSquareValues[FromPos]) ^ HashSquareValues[ToPos];
    }
}

static void MovePlayer(int PlayerPos)
{
    Game.Board[Game.PlayerPos] &= (uint8_t)~PLAYER;     /* remove player from the board */
    Game.PlayerPos = PlayerPos;
    if (Game.PlayerPos != 0) Game.Board[Game.PlayerPos] |= PLAYER;
}

static void UndoMove(int BoxNo, int Direction)
{
    int FromPos, ToPos;
    if (BoxNo != 0) {               /* only pushes are handled */
        Direction = Direction & DIRECTION_BIT_MASK;
        FromPos = Game.BoxPos[BoxNo];
        ToPos = FromPos - Game.NeighbourSquareOffset[Direction];
        Game.Board[FromPos] -= BOX;
        Game.Board[ToPos] += BOX;
        Game.BoxPos[BoxNo] = ToPos;
        Game.Board[Game.PlayerPos] &= (uint8_t)~PLAYER;
        if (Game.ReverseMode)
            Game.PlayerPos = FromPos;
        else
            Game.PlayerPos = ToPos - Game.NeighbourSquareOffset[Direction];
        Game.Board[Game.PlayerPos] += PLAYER;
        Game.BoardHashValue = (Game.BoardHashValue ^ HashSquareValues[FromPos]) ^ HashSquareValues[ToPos];
    }
}

/* ------------------------------------------------------------------------ */
/* Board */

static THashValue CalculateBoardHashValue(void)
{
    THashValue Result = 0;      /* player's position isn't considered; it's saved separately */
    int i;
    for (i = 1; i <= Game.BoxCount; i++)
        Result ^= HashSquareValues[Game.BoxPos[i]];
    return Result;
}

static int CalculateConnectedGoals(int GoalSquare, int TimeStamp, int VisitedCount,
                                   const uint8_t *Board, int *VisitedSquares)
{
    int NeighbourSquare, Direction;
    int Result = VisitedCount + 1;      /* 'GoalSquare' is a not previously visited goal */
    VisitedSquares[GoalSquare] = TimeStamp;
    for (Direction = dUp; Direction <= dRight; Direction++) {
        NeighbourSquare = GoalSquare + Game.NeighbourSquareOffset[Direction];
        if ((Board[NeighbourSquare] & GOAL) != 0 &&
            VisitedSquares[NeighbourSquare] != TimeStamp)
            Result = CalculateConnectedGoals(NeighbourSquare, TimeStamp, Result, Board, VisitedSquares);
    }
    return Result;
}

/* boards with goals and boxes in dead-end tubes (floors with walls on all but
 * one side) are not good level candidates; they tend to be trivial levels */
static int FillTubes(int MoveItemsEnabled)
{
    int i, j, BoxOnGoalCount, NeighbourFloorCount, NewBoxPos, NewPlayerPos;
    int DeadEnd, More, Dir, Direction;
    int Result = 0;

    BoxOnGoalCount = 0; Direction = dUp;
    for (i = 1; i <= Game.BoxCount; i++)
        if (IsAGoalSquare(Game.BoxPos[i])) BoxOnGoalCount++;

    do {
        More = 0;
        for (i = 0; i <= Game.BoardSize; i++)
            if ((Game.Board[i] & (WALL + BOX + GOAL)) == 0) {    /* for each non-goal, non-box floor-square... */
                NeighbourFloorCount = 0;
                for (Dir = dUp; Dir <= dRight; Dir++)
                    if (IsAFloorSquare(i + Game.NeighbourSquareOffset[Dir])) {
                        NeighbourFloorCount++; Direction = Dir;
                    }
                if (NeighbourFloorCount <= 1) {     /* the floor-square is surrounded by 3 or 4 walls */
                    DeadEnd = 1; NewBoxPos = 0; NewPlayerPos = 0;
                    if (i == Game.PlayerPos) {
                        if (NeighbourFloorCount == 0 || !MoveItemsEnabled)
                            DeadEnd = 0;            /* the player is on an isolated square or moving items isn't allowed */
                        else {                      /* try to move the player to the neighbour floor-square */
                            NewPlayerPos = i + Game.NeighbourSquareOffset[Direction];
                            if (IsABoxSquare(NewPlayerPos)) {
                                NewBoxPos = NewPlayerPos + Game.NeighbourSquareOffset[Direction];
                                DeadEnd = ((Game.Board[NewBoxPos] & (WALL + BOX)) == 0) &&  /* the box can be pushed forward */
                                          (BoxOnGoalCount < Game.BoxCount) &&              /* the position isn't a solution */
                                          (Game.History.Count < MAX_HISTORY_MOVES);        /* the game history isn't full */
                            }
                        }
                    }
                    if (DeadEnd) {                  /* dead-end: place a wall on the square */
                        if (NewBoxPos != 0) {       /* move a box */
                            for (j = 1; j <= Game.BoxCount; j++)
                                if (Game.BoxPos[j] == NewPlayerPos) {
                                    if (IsAGoalSquare(Game.BoxPos[j])) BoxOnGoalCount--;
                                    Game.Board[Game.BoxPos[j]] -= BOX; Game.BoxPos[j] = NewBoxPos;
                                    Game.Board[Game.BoxPos[j]] += BOX;
                                    if (IsAGoalSquare(Game.BoxPos[j])) BoxOnGoalCount++;
                                    Game.History.Count++;
                                    Game.History.Moves[Game.History.Count].BoxNo = (uint8_t)j;
                                    Game.History.Moves[Game.History.Count].Direction = (uint8_t)Direction;
                                    break;
                                }
                        }
                        if (NewPlayerPos != 0) {    /* move the player */
                            Game.Board[Game.PlayerPos] -= PLAYER; Game.PlayerPos = NewPlayerPos;
                            Game.Board[Game.PlayerPos] += PLAYER;
                        }
                        Game.Board[i] = WALL; More = 1; Result++;
                    }
                }
            }
    } while (More);
    return Result;
}

static void InitializeBoard(int BoardWidth, int BoardHeight, int FillSquares)
{
    int i, RowOffset;
    Game.BoardSize = (BoardWidth + 2) * (BoardHeight + 2);  /* the extra 2 is for a wall-filled border */
    Game.NeighbourSquareOffset[dUp]    = -(BoardWidth + 2);
    Game.NeighbourSquareOffset[dLeft]  = -1;
    Game.NeighbourSquareOffset[dDown]  = +(BoardWidth + 2);
    Game.NeighbourSquareOffset[dRight] = +1;

    if (FillSquares) {
        for (i = 0; i <= Game.BoardSize; i++) Game.Board[i] = WALL;
    } else if (Game.BoardWidth != BoardWidth || Game.BoardHeight != BoardHeight) {
        /* add a wall-filled border to ensure that the player always is surrounded by walls */
        for (i = 0; i <= BoardWidth + 1; i++) Game.Board[i] = WALL;                 /* top row */
        for (i = Game.BoardSize; i >= Game.BoardSize - BoardWidth - 2; i--) Game.Board[i] = WALL;  /* bottom row */
        RowOffset = 0;
        for (i = 1; i <= BoardHeight; i++) {
            RowOffset += BoardWidth + 2;
            Game.Board[RowOffset] = WALL; Game.Board[RowOffset + BoardWidth + 1] = WALL;   /* left and right columns */
        }
    }
    Game.BoardWidth = BoardWidth; Game.BoardHeight = BoardHeight;
}

static void RemoveUnconnectedGoals(void)
{
    static TBoardInteger VisitedSquares;
    int i, j, k, BestGroupNo, BestGroupSize;

    memset(VisitedSquares, 0, sizeof(VisitedSquares));
    BestGroupNo = 0; BestGroupSize = 0;
    for (i = 1; i <= Game.GoalCount; i++) {     /* find largest group */
        j = CalculateConnectedGoals(Game.GoalPos[i], i, 0, Game.Board, VisitedSquares);
        if (j > BestGroupSize) {
            BestGroupSize = j; BestGroupNo = i;
        }
    }

    if (BestGroupSize > 0 && BestGroupSize < Game.GoalCount) {
        CalculateConnectedGoals(Game.GoalPos[BestGroupNo], Game.GoalCount + 1, 0, Game.Board, VisitedSquares);
        j = Game.GoalCount;
        for (i = j; i >= 1; i--)
            if (VisitedSquares[Game.GoalPos[i]] <= j) {     /* goal[i] is not a member of the largest group */
                Game.GoalCount--; Game.BoxCount = Game.GoalCount;
                Game.Board[Game.GoalPos[i]] -= GOAL;
                Game.Board[Game.BoxPos[i]] -= BOX;
                for (k = i; k <= Game.GoalCount; k++) {
                    Game.GoalPos[k] = Game.GoalPos[k + 1];
                    Game.BoxPos[k] = Game.BoxPos[k + 1];
                }
            }
        Game.BoardHashValue = CalculateBoardHashValue();
    }
}

static void InitializeGame(void)
{
    int i, OriginalMaxBoxCount;

    OriginalMaxBoxCount = Generator.MaxBoxCount;
    if (!Generator.MaxBoxCountEnabled)
        Generator.MaxBoxCount = MAX_BOX_COUNT;

    InitializeBoard(Game.BoardWidth, Game.BoardHeight, 0);

    Game.BoxCount = 0; Game.GoalCount = 0; Game.PlayerPos = 0; Game.FloorCount = 0;
    for (i = 0; i <= Game.BoardSize; i++) {
        if ((Game.Board[i] & BOX) != 0) {
            if (Game.BoxCount < Generator.MaxBoxCount) {
                Game.BoxCount++;
                Game.BoxPos[Game.BoxCount] = i;
            } else
                Game.Board[i] -= BOX;
        }
        if ((Game.Board[i] & GOAL) != 0) {
            if (Game.GoalCount < Generator.MaxBoxCount) {
                Game.GoalCount++;
                Game.GoalPos[Game.GoalCount] = i;
            } else
                Game.Board[i] -= GOAL;
        }
        if ((Game.Board[i] & PLAYER) != 0) {
            if (Game.PlayerPos == 0)
                Game.PlayerPos = i;
            else
                Game.Board[i] -= PLAYER;
        }
        Game.Board[i] &= (uint8_t)(FLOOR + PLAYER + BOX + GOAL + WALL);     /* reset old flags, if any */
        if ((Game.Board[i] & FLOOR) != 0) Game.FloorCount++;
    }
    if (Game.BoxCount == 0) {
        Game.BoxCount = Game.GoalCount;
        for (i = 1; i <= Game.BoxCount; i++) {
            Game.BoxPos[i] = Game.GoalPos[i]; Game.Board[Game.BoxPos[i]] += BOX;
        }
    }
    if (Game.GoalCount == 0) {
        Game.GoalCount = Game.BoxCount;
        for (i = 1; i <= Game.GoalCount; i++) {
            Game.GoalPos[i] = Game.BoxPos[i]; Game.Board[Game.GoalPos[i]] += GOAL;
        }
    }
    if (Game.BoxCount != Game.GoalCount) {
        for (i = Min(Game.BoxCount, Game.GoalCount) + 1; i <= Game.BoxCount; i++)
            Game.Board[Game.BoxPos[i]] -= BOX;
        for (i = Min(Game.BoxCount, Game.GoalCount) + 1; i <= Game.GoalCount; i++)
            Game.Board[Game.GoalPos[i]] -= GOAL;
        Game.BoxCount = Min(Game.BoxCount, Game.GoalCount); Game.GoalCount = Game.BoxCount;
    }

    if (!GA.Control.ForwardSearch && GA.Control.GenerateConnectedGoals && Game.ReverseMode)
        RemoveUnconnectedGoals();

    Game.BoardHashValue = CalculateBoardHashValue();
    Game.PlayersReachableSquaresTimeStamp = INT_MAX;    /* reset visited squares before calculating the player's reachable squares the next (first) time */
    Positions.CurrentPosition = 0;

    Generator.MaxBoxCount = OriginalMaxBoxCount;
}

/* fills dead floor squares with walls; returns the number of floor squares
 * filled with walls */
static int LoadAndNormalizeBoard(uint8_t *Board)
{
    int BoxNo, BoxesOnBoardCount, MinPlayerPos, OriginalPlayerPos, SquareNo;
    int BoxesOnBoard[MAX_BOX_COUNT + 1];
    int Result = 0;

    if (Board != Game.Board)        /* 'Board' may be 'Game.Board' itself */
        memcpy(Game.Board, Board, sizeof(TBoard));
    InitializeGame();
    if (Game.BoxCount != 0) {
        OriginalPlayerPos = Game.PlayerPos; BoxesOnBoardCount = 0;
        for (BoxNo = 1; BoxNo <= Game.BoxCount; BoxNo++)    /* remove boxes from the board, if any */
            if ((Game.Board[Game.BoxPos[BoxNo]] & BOX) != 0) {
                Game.Board[Game.BoxPos[BoxNo]] -= BOX;
                BoxesOnBoardCount++; BoxesOnBoard[BoxesOnBoardCount] = Game.BoxPos[BoxNo];
            }

        for (BoxNo = 1; BoxNo <= Game.BoxCount; BoxNo++) {
            if (Game.ReverseMode)   /* trick 'CalculatePlayersReachableSquares' so it calculates floor squares connected to the box/goal square */
                Game.PlayerPos = Game.GoalPos[BoxNo];
            else
                Game.PlayerPos = Game.BoxPos[BoxNo];
            CalculatePlayersReachableSquares(&MinPlayerPos);
            if (BoxNo != Game.BoxCount)     /* accumulate floor squares for all boxes/goals */
                Game.PlayersReachableSquaresTimeStamp--;
        }

        for (SquareNo = 0; SquareNo <= Game.BoardSize; SquareNo++)
            if ((Game.Board[SquareNo] & (WALL + FLOOR + BOX + GOAL + PLAYER)) == FLOOR &&
                Game.PlayersReachableSquares[SquareNo] != Game.PlayersReachableSquaresTimeStamp &&
                SquareNo != OriginalPlayerPos) {
                Game.Board[SquareNo] = WALL; Game.FloorCount--; Result++;
            }

        for (BoxNo = 1; BoxNo <= BoxesOnBoardCount; BoxNo++)    /* put boxes back on the board, if any */
            Game.Board[BoxesOnBoard[BoxNo]] += BOX;
        Game.PlayerPos = OriginalPlayerPos;                     /* restore the player position */
        Game.BoardHashValue = CalculateBoardHashValue();
    }
    if (Board != Game.Board)
        memcpy(Board, Game.Board, sizeof(TBoard));
    return Result;
}

/* makes an empty board with a wall border */
static void MakeBoard(int BoardWidth, int BoardHeight)
{
    int Col, Row, RowOffset;
    InitializeBoard(BoardWidth, BoardHeight, 1);
    for (Row = 2; Row <= Game.BoardHeight - 1; Row++) {     /* 2..Pred(Height): leave a wall border */
        RowOffset = ColRowToSquare(0, Row, Game.BoardWidth, Game.BoardHeight);
        for (Col = 2; Col <= Game.BoardWidth - 1; Col++) Game.Board[RowOffset + Col] = FLOOR;
    }
    Game.PlayerPos = 0; Game.BoxCount = 0; Game.GoalCount = 0;
    Game.FloorCount = (Game.BoardWidth - 2) * (Game.BoardHeight - 2);
    Game.PlayersReachableSquaresTimeStamp = INT_MAX;
    Game.BoardHashValue = CalculateBoardHashValue();
}

/* ------------------------------------------------------------------------ */
/* Positions (the transposition table) and the open queue */

static void SetPosition(uint32_t Position_);

static THashValue AbsHash(THashValue v)
{
    if (v < 0) return (THashValue)(0 - (uint64_t)v);    /* as 'Abs' of an Int64: Low(Int64) stays negative */
    return v;
}

static void ClearOpenPositions(void)
{
    Positions.DroppedCount += Positions.OpenPositions.Count;
    memset(&Positions.OpenPositions, 0, sizeof(Positions.OpenPositions));
}

static int DequeueOpenPosition(uint32_t *Position_)
{
    TPositionQueue *Q = &Positions.OpenPositions;
    int Result;
    *Position_ = Q->Head;
    Result = (*Position_ != 0) && ThreadActiveTick();
    if (Result) {
        Q->Head = P(Q->Head)->Next;
        if (Q->Head == 0) Q->Tail = 0;
        P(*Position_)->Next = 0;    /* after removal from the open-queue, the 'Next' field is reused for storing the current path in the search */
        Q->Count--;
    }
    return Result;
}

static void EnqueueOpenPosition(uint32_t Position_)
{
    TPositionQueue *Q = &Positions.OpenPositions;
    if (Q->Tail != 0)
        P(Q->Tail)->Next = Position_;
    else
        Q->Head = Position_;
    Q->Tail = Position_;
    P(Q->Tail)->Next = 0;
    Q->Count++;
}

static void RemovePositionFromOpenPositions(uint32_t Position_)
{
    SetPosition(0);
    while (Position_ != 0) {        /* the open queue uses 'next' to link members: reset the links */
        P(Position_)->Next = 0;
        Position_ = P(Position_)->Parent;
    }
}

/* The original clears the table by zero-filling the whole memory; here only
 * the slots that have been used are cleared, which gives the same result. */
static void ClearPositions(void)
{
    size_t i;
    for (i = 0; i < Positions.UsedCount; i++)
        memset(&Positions.Slots[Positions.Used[i]], 0, sizeof(TPosition));
    Positions.UsedCount = 0;
    ClearOpenPositions();
    Positions.Count = 0; Positions.DroppedCount = 0; Positions.RepetitionCount = 0;
    Positions.TailPositionCount = 0;
    Positions.BestPosition = 0; Positions.CurrentPosition = 0;
}

static void FinalizePositions(void)
{
    free(Positions.Slots);
    free(Positions.Used);
    memset(&Positions, 0, sizeof(Positions));
}

static int LookupPosition(THashValue HashValue_, int PlayerPos_, uint32_t *Position_)
{
    int Result = 0;
    const size_t VC = Positions.VectorCapacity;
    Positions.LookupState.HashValue = AbsHash(HashValue_);
    Positions.LookupState.CollisionCount = 0;   /* caution: there is only one set of static information controlling 'Lookup' and 'GetFirst'/'GetNext' */
    Positions.LookupState.ItemIndex = (size_t)(Positions.LookupState.HashValue % (THashValue)VC);
    {
        size_t VectorIndex = (size_t)(Positions.LookupState.HashValue & (MEMORY_BLOCKS_COUNT - 1));
        Positions.LookupState.VectorIndex = VectorIndex;
        *Position_ = (uint32_t)(VectorIndex * VC + Positions.LookupState.ItemIndex + 1);
    }

    do {
        TPosition *p = P(*Position_);
        Positions.LookupState.CollisionCount++;
        if (p->PlayerPos != 0) {                /* has this slot been used in the current run? */
            if (p->HashValue == Positions.LookupState.HashValue &&
                (p->PlayerPos == PlayerPos_ || PlayerPos_ == 0))    /* '0': don't care */
                Result = 1;                     /* found */
            else {
                Positions.LookupState.ItemIndex++;
                if (Positions.LookupState.ItemIndex < VC)
                    (*Position_)++;             /* try next slot */
                else {
                    Positions.LookupState.VectorIndex =
                        (Positions.LookupState.VectorIndex + 1) & (MEMORY_BLOCKS_COUNT - 1);   /* try next bank */
                    Positions.LookupState.ItemIndex = 0;
                    *Position_ = (uint32_t)(Positions.LookupState.VectorIndex * VC + 1);
                }
            }
        }
    } while (!(Result ||
               P(*Position_)->PlayerPos == 0 ||     /* '0': not found, return free slot */
               Positions.LookupState.CollisionCount >= MAX_HASH_COLLISION_COUNT));  /* not found, no free slot */
    return Result;
}

static int GetFirstPosition(THashValue HashValue_, uint32_t *Position_)
{   /* searches for a box constellation, ignoring player position */
    return LookupPosition(HashValue_, 0, Position_);
}

static int GetNextPosition(uint32_t *Position_)
{   /* searches for a box constellation, ignoring player position */
    int Result = 0;
    const size_t VC = Positions.VectorCapacity;
    while (!Result &&
           Positions.LookupState.CollisionCount < MAX_HASH_COLLISION_COUNT &&
           P(*Position_)->PlayerPos != 0) {
        Positions.LookupState.CollisionCount++;
        Positions.LookupState.ItemIndex++;                  /* try next slot */
        if (Positions.LookupState.ItemIndex >= VC) {
            Positions.LookupState.VectorIndex =
                (Positions.LookupState.VectorIndex + 1) & (MEMORY_BLOCKS_COUNT - 1);   /* try next bank */
            Positions.LookupState.ItemIndex = 0;
        }
        *Position_ = (uint32_t)(Positions.LookupState.VectorIndex * VC +
                                Positions.LookupState.ItemIndex + 1);
        Result = P(*Position_)->PlayerPos != 0 &&
                 P(*Position_)->HashValue == Positions.LookupState.HashValue;
    }
    return Result;
}

/* The square values of the Zobrist hash keys (calculated once) */
static int HashSquareValuesReady;

static void InitializeHashSquareValues(void)
{
    int i, j;
    if (HashSquareValuesReady) return;
    InitializeRandomState(0);
    for (i = 0; i <= 255; i++) (void)Random(INT_MAX);      /* warm up the random number generator */
    for (i = 0; i <= MAX_BOARD_SIZE; i++) {
        do {
            uint64_t a, b, c, d, e, s;
            a = (uint64_t)Random(INT_MAX) << 48;
            b = (uint64_t)Random(INT_MAX) << 32;
            c = (uint64_t)Random(INT_MAX) << 16;
            d = (uint64_t)Random(INT_MAX) << 8;
            e = (uint64_t)Random(INT_MAX);
            /* Abs(Abs(a)+Abs(b)+Abs(c)+Abs(d)+Abs(e)) with wrap-around as in Delphi */
            s = (uint64_t)AbsHash((THashValue)a) + (uint64_t)AbsHash((THashValue)b) +
                (uint64_t)AbsHash((THashValue)c) + (uint64_t)AbsHash((THashValue)d) +
                (uint64_t)AbsHash((THashValue)e);
            HashSquareValues[i] = AbsHash((THashValue)s);
            for (j = 0; j < i; j++)
                if (HashSquareValues[j] == HashSquareValues[i])
                    HashSquareValues[i] = 0;
        } while (HashSquareValues[i] <= (THashValue)INT_MAX);
    }
    HashSquareValuesReady = 1;
}

static int InitializePositions(size_t MemoryByteSize)
{
    free(Positions.Slots);
    free(Positions.Used);
    memset(&Positions, 0, sizeof(Positions));
    Positions.VectorCapacity = (MemoryByteSize / MEMORY_BLOCKS_COUNT) /
                               (sizeof(TPosition) + sizeof(uint32_t));
    if (Positions.VectorCapacity == 0) return 0;
    Positions.Capacity = Positions.VectorCapacity * MEMORY_BLOCKS_COUNT;
    if (Positions.Capacity >= 0xFFFFFFF0u) return 0;
    Positions.Slots = (TPosition *)calloc(Positions.Capacity, sizeof(TPosition));
    Positions.Used = (uint32_t *)malloc(Positions.Capacity * sizeof(uint32_t));
    if (Positions.Slots == NULL || Positions.Used == NULL) {
        FinalizePositions();
        return 0;
    }
    ClearPositions();
    InitializeHashSquareValues();
    return 1;
}

static int MakePosition(THashValue HashValue_, int PlayerPos_, int Depth_, int Score_,
                        int BoxNo_, int Direction_, uint32_t Parent_, uint32_t *Position_)
{
    int Result;
    *Position_ = 0;
    if (LookupPosition(HashValue_, PlayerPos_, Position_)) {
        /* 'True': the position already exists in the transposition table */
        TPosition *p = P(*Position_);
        Positions.RepetitionCount++;

        Result = 0;
        if (Depth_ < (int)p->Depth) {
            if (p->Depth == 0xFFFF) {
                /* this is the first time the position is encountered during
                 * the normal breadth-first search (as opposed to the initial
                 * tail-positions search) */
                EnqueueOpenPosition(*Position_);
            }
            Result = 1;
            p->Depth = (uint16_t)Depth_;
        }
        if (Score_ < (int)p->Score) {
            Result = 1;
            p->Score = (uint16_t)Score_;
        }

        if (Result &&       /* 'True': this is a new better path to the position */
            (p->MoveDirection & POSITION_TAG_CLOSED) == 0) {
            /* the position hasn't been expanded yet, hence, it's ok to
             * update the path to it because no successor-positions depend on
             * the current path to this position */
            p->MoveBoxNo = (uint8_t)BoxNo_;
            p->MoveDirection = (uint8_t)(Direction_ | (p->MoveDirection & POSITION_TAG_BIT_MASK));
            p->Parent = Parent_;
            p->PlayerPos = (uint16_t)PlayerPos_;
        }
        /* else: 'Depth' and 'Score' may be out of sync, coming from different
         * paths to this position (as in the original) */
    } else if (P(*Position_)->PlayerPos != 0) {
        /* no room for a new position, i.e., the memory is full */
        Positions.DroppedCount++;
        ClearOpenPositions();           /* stop when positions are lost */
        Result = 0;
        *Position_ = 0;                 /* '0' (nil) signals that memory was filled */
    } else {                            /* save new position */
        TPosition *p = P(*Position_);
        Positions.Count++;
        p->Depth = (uint16_t)Depth_;
        p->HashValue = AbsHash(HashValue_);
        p->MoveBoxNo = (uint8_t)BoxNo_;
        p->MoveDirection = (uint8_t)Direction_;
        p->Parent = Parent_;
        p->PlayerPos = (uint16_t)PlayerPos_;
        p->Score = (uint16_t)Score_;
        Positions.Used[Positions.UsedCount++] = *Position_ - 1;
        EnqueueOpenPosition(*Position_);
        Result = 1;
    }
    return Result;
}

static void SetPosition(uint32_t Position_)
{   /* do/undo moves so the board matches the position at the tree-node 'Position_' */
    uint32_t p, Next;

    /* find path back to common ancestor of new position and current position */
    p = Position_; Next = 0;
    while (p != 0 &&
           P(p)->Parent != 0 &&
           P(p)->Next == 0 &&       /* after a position is removed from the breadth-first queue, 'Next' is reused as forward chain for the current path */
           p != Positions.CurrentPosition) {
        P(p)->Next = Next;          /* 'Next' is used as forward chain for the current path */
        Next = p;
        p = P(p)->Parent;
    }

    /* undo the old moves that aren't on the common path, if any */
    while (Positions.CurrentPosition != 0 &&
           Positions.CurrentPosition != p) {
        TPosition *cp = P(Positions.CurrentPosition);
        UndoMove(cp->MoveBoxNo, cp->MoveDirection);
        cp->Next = 0;               /* this position isn't on the current path anymore (Pascal 'with': the field, not the local variable) */
        Positions.CurrentPosition = cp->Parent;
    }

    /* perform new moves, starting with the common ancestor's successor */
    if (Next != 0) {
        if (P(Next)->Parent != 0)
            P(P(Next)->Parent)->Next = Next;    /* ensure that the forward chain is updated for the common ancestor, if any */
        do {
            DoMove(P(Next)->MoveBoxNo, P(Next)->MoveDirection);
            Next = P(Next)->Next;
        } while (Next != 0);
    } else if (Position_ != 0)
        MovePlayer(P(Position_)->PlayerPos);

    Positions.CurrentPosition = Position_;      /* save new current position */
}

/* ------------------------------------------------------------------------ */
/* The player's access areas, and the board generator for the random candidates */

static int GetNextPlayerAccessArea(void)
{
    int i, MinPlayerPos = 0, Result;
    MovePlayer(0);                  /* remove player from the board */
    for (;;) {
        Game.PlayerPos++;
        if (Game.PlayerPos > Game.BoardSize) break;
        if (((Game.Board[Game.PlayerPos] & (WALL + BOX)) == 0) &&
            !Generator.VisitedPlayerPos[Game.PlayerPos] &&
            CalculatePlayersReachableSquares(&MinPlayerPos) != 0)
            break;
    }
    Result = Game.PlayerPos <= Game.BoardSize;
    if (Result) {
        MovePlayer(MinPlayerPos);   /* put player on the board */
        for (i = 0; i <= Game.BoardSize; i++)
            if (Game.PlayersReachableSquares[i] == Game.PlayersReachableSquaresTimeStamp)
                Generator.VisitedPlayerPos[i] = 1;      /* mark visited access area for the player */
        if (!Game.ReverseMode) Generator.PlayerAccessAreaNo++;
    } else
        Game.PlayerPos = 0;
    return Result;
}

static int GetFirstPlayerAccessArea(void)
{
    memset(Generator.VisitedPlayerPos, 0, sizeof(Generator.VisitedPlayerPos));
    Generator.PlayerAccessAreaNo = 0;
    return GetNextPlayerAccessArea();
}

/* Makes a random board: walls and boxes (backward search: goals with boxes on
 * them) are placed on a board with a wall border.  This is the 'YASC' variant
 * of the original function; there is no template, so the board is made from
 * scratch each time. */
static int GenerateNextBoard_Random(void)
{
    int i, j, k, Mask, Col, Row;
    int Result = (Generator.WallCount + Generator.BoxCount) != 0;
    if (Result) {
        if (Generator.RandomSeed < INT_MAX) Generator.RandomSeed++;
        else Generator.RandomSeed = 1;
        InitializeRandomState(Generator.RandomSeed);

        MakeBoard(Game.BoardWidth, Game.BoardHeight);

        for (i = 1; i <= Generator.PlayerIndex - 1; i++) {   /* 'Pred': the player position is filled in by the search */
            Mask = WALL + BOX + GOAL + PLAYER;
            Col = 1 + Random(Game.BoardWidth);          /* Delphi evaluates the arguments from left to right */
            Row = 1 + Random(Game.BoardHeight);
            j = ColRowToSquare(Col, Row, Game.BoardWidth, Game.BoardHeight);
            k = j;

            do {
                if (j != Game.BoardSize) j++;
                else j = 1;
            } while (!(((Game.Board[j] & Mask) == 0) || j == k));
            if ((Game.Board[j] & Mask) == 0) {
                if (i <= Generator.WallCount)
                    Game.Board[j] = WALL;
                else if (Game.ReverseMode)
                    Game.Board[j] = BOX + GOAL + FLOOR;
                else
                    Game.Board[j] = BOX + FLOOR;
            }
        }
    }
    return Result;
}

/* The settings of the board generator for the genetic algorithm (the part of
 * 'InitializeBoardGenerator' that is used without a template). */
static void InitializeBoardGenerator(int ReverseMode, int WallCount, int BoxCount, int RandomSeed,
                                     int MaxBoxCount, int MaxOpenPositions,
                                     int TailPushesThreshold, int MaxBoxCountEnabled,
                                     int MaxOpenPositionsEnabled, int TrivialOpeningPushesIgnored,
                                     int TailPushesThresholdEnabled)
{
    memset(&Generator, 0, sizeof(Generator));
    Generator.BoxCount = Max(1, Min(BoxCount, MAX_BOX_COUNT));
    Generator.WallCount = Max(0, Min(WallCount,
                              ((Game.BoardWidth - 2) * (Game.BoardHeight - 2)) - Generator.BoxCount - 1));  /* '-1': make room for the player */
    Generator.PlayerIndex = Generator.WallCount + Generator.BoxCount + 1;   /* player index must be after walls and boxes */
    Generator.TailPushesThreshold = TailPushesThreshold;
    Generator.RandomSeed = RandomSeed < 0 ? -RandomSeed : RandomSeed;
    Generator.MaxBoxCount = Max(0, Min(MaxBoxCount, MAX_BOX_COUNT));
    Generator.MaxOpenPositions = MaxOpenPositions;
    Generator.TemplatePlayerPos = 0;
    Generator.MaxBoxCountEnabled = MaxBoxCountEnabled;
    Generator.MaxOpenPositionsEnabled = MaxOpenPositionsEnabled;
    Generator.TrivialOpeningPushesIgnored = TrivialOpeningPushesIgnored;
    Generator.TailPushesThresholdEnabled = TailPushesThresholdEnabled;
    Game.ReverseMode = ReverseMode;
}

/* ------------------------------------------------------------------------ */
/* The solution and the goal squares */

static int GAFitnessValue(int FitnessFunction, int PushCount, int Score)
{   /* 'fitness value' for a position, using either pushes or score as first criteria */
    if (FitnessFunction == ffScore)
        return (Score * (2 * (MAX_HISTORY_MOVES + 1))) + PushCount;
    return (PushCount * (2 * (MAX_HISTORY_MOVES + 1))) + Score;
}

static void SaveVariationToHistory(uint32_t Position_)
{
    int BoxNo, NewBoxCount, SquareNo, TruePositionDepth;
    int NewBoxNo[MAX_BOX_COUNT + 1];
    uint32_t p, q;
    THistory *H = &Game.History;

    if (Position_ == 0) return;
    memset(NewBoxNo, 0, sizeof(NewBoxNo));
    if (Game.ReverseMode) {     /* assign new box-numbers so they are sorted in order of appearance on the board */
        NewBoxCount = 0;
        for (SquareNo = 1; SquareNo <= Game.BoardSize; SquareNo++)
            if ((Game.Board[SquareNo] & BOX) != 0)
                for (BoxNo = 1; BoxNo <= Game.BoxCount; BoxNo++)
                    if (Game.BoxPos[BoxNo] == SquareNo) {
                        NewBoxCount++;
                        NewBoxNo[BoxNo] = NewBoxCount;
                        break;
                    }
    }

    if (Positions.TailPositionCount != 0) {
        TruePositionDepth = 0; p = Position_;   /* calculate the true depth for the position (the stored depth is relative to a tail position ancestor, if any) */
        while (P(p)->Parent != 0) {
            TruePositionDepth++; p = P(p)->Parent;
        }
    } else
        TruePositionDepth = P(Position_)->Depth;

    H->Count = 0; H->Score = 0; H->Fitness = 0; H->TailCount = 0; p = Position_;
    if (TruePositionDepth <= MAX_HISTORY_MOVES) {
        while (p != 0 && H->Count < MAX_HISTORY_MOVES) {
            q = P(p)->Parent;
            if (q != 0) {       /* 'Parent'=nil: the root node is a dummy */
                H->Count++;
                if (Game.ReverseMode) {
                    H->Moves[H->Count].BoxNo = (uint8_t)NewBoxNo[P(p)->MoveBoxNo];
                    H->Moves[H->Count].Direction =
                        (uint8_t)OPPOSITE_DIRECTION[P(p)->MoveDirection & DIRECTION_BIT_MASK];
                } else {
                    TMove *m = &H->Moves[TruePositionDepth + 1 - H->Count];
                    m->BoxNo = P(p)->MoveBoxNo;
                    m->Direction = (uint8_t)(P(p)->MoveDirection & DIRECTION_BIT_MASK);
                }

                /* calculate box-lines + box-changes; during the search the score
                 * may have been a different metric or heuristic score; this value
                 * isn't in use; further down, the score is overwritten by the
                 * original score found during the search */
                if ((P(p)->MoveDirection & POSITION_TAG_TAIL) == 0 ||
                    Positions.TailPositionCount <= 0) {
                    if (P(q)->MoveBoxNo != P(p)->MoveBoxNo)
                        H->Score += 2;      /* box-lines and box-changes have the same weights */
                    else if ((P(q)->MoveDirection & DIRECTION_BIT_MASK) !=
                             (P(p)->MoveDirection & DIRECTION_BIT_MASK))
                        H->Score++;
                } else
                    H->TailCount++;         /* count trivial pushes at the end of the game */
            }
            p = q;
        }

        /* the score registered in the position is the minimum number of
         * box-lines + box-changes found during the search; keep the minimum value
         * even if the current path to the position has a higher score */
        H->Score = P(Position_)->Score;
        /* the fitness value is based on the position depth; if trivial opening
         * and closing pushes are ignored, then the depth is smaller than the
         * number of pushes on the path */
        H->Fitness = GAFitnessValue(GA.Control.FitnessFunction, P(Position_)->Depth, H->Score);
    }
}

static void MakeGoalSquares(uint32_t Position_)
{   /* precondition: 'Position_' is not 0 */
    int i, OriginalPlayerPos;
    OriginalPlayerPos = Game.PlayerPos; Game.GoalCount = 0;
    RemovePositionFromOpenPositions(Position_);     /* the position might still be a member of 'Open'; get it out before calling 'SetPosition()' */
    SetPosition(Position_);                         /* recreate position for this node */
    for (i = 0; i <= Game.BoardSize; i++) {         /* make goals, i.e., use current box-positions as goals */
        Game.Board[i] &= (uint8_t)~GOAL;            /* remove old goal positions, if any */
        if ((Game.Board[i] & BOX) != 0) {
            Game.Board[i] += GOAL;
            Game.GoalCount++; Game.GoalPos[Game.GoalCount] = i;
        }
    }
    SetPosition(0);                                 /* reset moves again */
    MovePlayer(OriginalPlayerPos);
}

/* ------------------------------------------------------------------------ */
/* Search */

#define MAX_MOVES (MAX_BOARD_WIDTH * MAX_BOARD_HEIGHT + 4 * MAX_BOX_COUNT)

/* Variables of the Delphi function 'Search' that the nested procedures use */
static int SearchResult;                /* 'Result' of Search; ExpandPosition sets it to false when the memory is full */
static uint32_t SearchPosition;         /* 'Position' of Search: the position being expanded */
static int SearchOriginalPlayerPos;
static int TailSearchResult;            /* 'Result' of TailPushesSearch */
static TMove SearchMoves[MAX_MOVES + 1];
static TMove TailMoves[MAX_MOVES + 1];

static int EnqueueStartPositions(void)
{   /* precondition: the game board state matches the starting position, and the OPEN-queue is empty */
    int Result, MinPlayerPos = 0;
    if (Game.PlayerPos == 0 || Game.ReverseMode) {  /* enqueue all start-positions; this is required when the backward generator is used */
        if (GetFirstPlayerAccessArea()) {
            do {
                Result = MakePosition(Game.BoardHashValue, Game.PlayerPos, 0, 0, 0, dUp, 0,
                                      &Positions.BestPosition);
            } while (GetNextPlayerAccessArea() && Result);
        } else
            Result = 0;
    } else {                                        /* enqueue a single start position */
        Result = (CalculatePlayersReachableSquares(&MinPlayerPos) > 0) &&
                 MakePosition(Game.BoardHashValue, MinPlayerPos, 0, 0, 0, dUp, 0,
                              &Positions.BestPosition);
    }
    return Result;
}

/* a position is not a legal goal position if there exists a shorter path to a
 * position with an identical box constellation, ignoring the player position;
 * (as in the original, the depth is compared with the depth of the position
 * being expanded) */
static int IsALegalGoalPosition(uint32_t Position_)
{
    uint32_t p;
    int Result;
    if (Game.ReverseMode)
        /* this constraint applied to the backward generator when it handled
         * each player's access area separately, but it became obsolete when the
         * backward generator switched to handling all access areas simultaneously */
        return 1;
    Result = GetFirstPosition(P(Position_)->HashValue, &p);
    if (Result) {
        do {
            Result = P(p)->Depth >= P(SearchPosition)->Depth;
        } while (Result && GetNextPosition(&p));
    }
    return Result;
}

static void GenerateMoves(int LastPushedBoxNo, int *MoveCount, TMove *Moves)
{
    int BoxNo, MinPlayerPos, Square, NeighbourSquare, Direction;
    *MoveCount = 0;
    CalculatePlayersReachableSquares(&MinPlayerPos);
    BoxNo = LastPushedBoxNo; if (BoxNo == 0) BoxNo = 1;
    if (Game.ReverseMode) {
        while (BoxNo <= Game.BoxCount) {
            Square = Game.BoxPos[BoxNo];
            for (Direction = dUp; Direction <= dRight; Direction++) {
                NeighbourSquare = Square + Game.NeighbourSquareOffset[Direction];
                if ((Game.Board[NeighbourSquare] & (BOX + WALL)) == 0 &&
                    (Game.Board[NeighbourSquare + Game.NeighbourSquareOffset[Direction]] & (BOX + WALL)) == 0 &&
                    Game.PlayersReachableSquares[NeighbourSquare] == Game.PlayersReachableSquaresTimeStamp) {
                    (*MoveCount)++;
                    Moves[*MoveCount].BoxNo = (uint8_t)BoxNo;
                    Moves[*MoveCount].Direction = (uint8_t)Direction;
                }
            }
            if (BoxNo != LastPushedBoxNo) BoxNo++;
            else BoxNo = 1;                                 /* first time through the loop */
            if (BoxNo == LastPushedBoxNo) BoxNo++;          /* skip last pushed box when seeing it for the second time */
        }
    } else {
        while (BoxNo <= Game.BoxCount) {
            Square = Game.BoxPos[BoxNo];
            for (Direction = dUp; Direction <= dRight; Direction++)
                if (Game.PlayersReachableSquares[Square - Game.NeighbourSquareOffset[Direction]] ==
                        Game.PlayersReachableSquaresTimeStamp &&
                    (Game.Board[Square + Game.NeighbourSquareOffset[Direction]] & (BOX + WALL)) == 0) {
                    (*MoveCount)++;
                    Moves[*MoveCount].BoxNo = (uint8_t)BoxNo;
                    Moves[*MoveCount].Direction = (uint8_t)Direction;
                }
            if (BoxNo != LastPushedBoxNo) BoxNo++;
            else BoxNo = 1;                                 /* first time through the loop */
            if (BoxNo == LastPushedBoxNo) BoxNo++;          /* skip last pushed box when seeing it for the second time */
        }
    }
}

static void ExpandPosition(uint32_t Position_)
{
    int i, MinPlayerPos = 0, MoveCount, SuccessorDepth, SuccessorScore;
    THashValue BoardHashValue;
    uint32_t Successor;
    TPosition *pp = P(Position_);

    /* 'CLOSED': with the current machinery, the path to an expanded node
     * cannot change after expansion, even if a new better path is discovered later */
    pp->MoveDirection |= POSITION_TAG_CLOSED;

    SetPosition(Position_);                         /* update board so it matches the position */
    GenerateMoves(pp->MoveBoxNo, &MoveCount, SearchMoves);

    for (i = 1; i <= MoveCount; i++)
        if (SearchResult) {                         /* for each move... */
            const int BoxNo = SearchMoves[i].BoxNo;
            const int Direction = SearchMoves[i].Direction;
            DoMove(BoxNo, Direction);               /* perform the move */
            BoardHashValue = Game.BoardHashValue;   /* save the hash-value for the new position */
            CalculatePlayersReachableSquares(&MinPlayerPos);    /* board hash-value and minimum player-position identifies the position */
            UndoMove(BoxNo, Direction);             /* take the move back again */

            if (Positions.TailPositionCount <= 0 ||             /* 'True': include tail positions in the fitness calculation */
                (pp->MoveDirection & POSITION_TAG_TAIL) == 0) { /* 'True': this isn't a tail position */
                SuccessorDepth = pp->Depth + 1;
                if (pp->MoveBoxNo != BoxNo)
                    SuccessorScore = pp->Score + 2;             /* the score is box-lines + box-changes */
                else if ((pp->MoveDirection & DIRECTION_BIT_MASK) != (Direction & DIRECTION_BIT_MASK))
                    SuccessorScore = pp->Score + 1;             /* the box changes direction */
                else
                    SuccessorScore = pp->Score;
            } else {    /* the current position is a tail position, i.e., it's considered a trivial end-game position */
                SuccessorDepth = 1;
                SuccessorScore = 2;
            }

            if (MakePosition(BoardHashValue, MinPlayerPos, SuccessorDepth, SuccessorScore,
                             BoxNo, Direction, Position_, &Successor)) {
                const TPosition *sp = P(Successor);
                const TPosition *bp = P(Positions.BestPosition);
                int Better;
                if (GA.Control.FitnessFunction == ffPushes)
                    Better = sp->Depth >= bp->Depth && (sp->Depth > bp->Depth || sp->Score > bp->Score);
                else
                    Better = sp->Score >= bp->Score && (sp->Score > bp->Score || sp->Depth > bp->Depth);
                if (Better && IsALegalGoalPosition(Successor))
                    Positions.BestPosition = Successor;         /* new best path */
                /* else: not a new best path: do nothing */
            } else if (Successor == 0) {                        /* '0': memory was filled */
                SearchResult = 0;                               /* exhaustive search failed */
                ClearOpenPositions();                           /* clear open-queue */
                break;                                          /* exit 'for' loop, i.e., stop expanding this position */
            }
        }
}

static void TailGenerateMoves(uint32_t Position_, int *MoveCount, TMove *Moves)
{
    int BoxNo, LastPushedBoxNo, MinPlayerPos, Square, NeighbourSquare, Direction;
    uint32_t p;
    int VisitedBoxesCount[MAX_BOX_COUNT + 1];

    *MoveCount = 0;
    CalculatePlayersReachableSquares(&MinPlayerPos);            /* calculate player's reachable squares */

    memset(VisitedBoxesCount, 0, sizeof(VisitedBoxesCount));
    p = Position_;
    while (P(p)->Parent != 0) {                                 /* calculate the boxes that have moved */
        if (P(p)->MoveBoxNo != P(P(p)->Parent)->MoveBoxNo)      /* '<>': in a sequence of moves like 'AAABBAAA', box 'A' is counted for 2 box sessions, and box 'B' is counted for 1 box session */
            VisitedBoxesCount[P(p)->MoveBoxNo]++;
        p = P(p)->Parent;
    }

    LastPushedBoxNo = P(Position_)->MoveBoxNo;
    BoxNo = LastPushedBoxNo; if (BoxNo == 0) BoxNo = 1;

    while (BoxNo <= Game.BoxCount) {
        if (VisitedBoxesCount[BoxNo] < abs(Generator.TailPushesThreshold) ||
            BoxNo == P(Position_)->MoveBoxNo) {
            Square = Game.BoxPos[BoxNo];
            for (Direction = dUp; Direction <= dRight; Direction++) {
                NeighbourSquare = Square + Game.NeighbourSquareOffset[Direction];
                if ((Game.Board[NeighbourSquare] & (BOX + WALL)) == 0 &&
                    (Game.Board[NeighbourSquare + Game.NeighbourSquareOffset[Direction]] & (BOX + WALL)) == 0 &&
                    Game.PlayersReachableSquares[NeighbourSquare] == Game.PlayersReachableSquaresTimeStamp) {
                    (*MoveCount)++;
                    Moves[*MoveCount].BoxNo = (uint8_t)BoxNo;
                    Moves[*MoveCount].Direction = (uint8_t)Direction;
                }
            }
        }
        if (BoxNo != LastPushedBoxNo) BoxNo++;
        else BoxNo = 1;                                 /* first time through the loop */
        if (BoxNo == LastPushedBoxNo) BoxNo++;          /* skip last pushed box when seeing it for the second time */
    }
}

static void TailExpandPosition(uint32_t Position_)
{
    int i, MinPlayerPos = 0, MoveCount;
    THashValue BoardHashValue;
    uint32_t Successor;
    TPosition *pp = P(Position_);

    SetPosition(Position_);                             /* update board so it matches the position */
    TailGenerateMoves(Position_, &MoveCount, TailMoves);

    for (i = 1; i <= MoveCount; i++)
        if (TailSearchResult) {                         /* for each move... */
            const int BoxNo = TailMoves[i].BoxNo;
            const int Direction = TailMoves[i].Direction;
            DoMove(BoxNo, Direction);                   /* perform the move */
            BoardHashValue = Game.BoardHashValue;       /* save hash-value */
            CalculatePlayersReachableSquares(&MinPlayerPos);    /* board hash-value and minimum player-position identifies the position */
            UndoMove(BoxNo, Direction);                 /* take the move back again */

            if (MakePosition(BoardHashValue, MinPlayerPos, 0xFFFF, 0xFFFF, BoxNo, Direction,
                             Position_, &Successor)) {
                /* a new tail position */
            } else if (Successor == 0) {                /* '0': memory was filled */
                TailSearchResult = 0;                   /* exhaustive search failed */
                ClearOpenPositions();                   /* clear open-queue */
                break;                                  /* exit 'for' loop, i.e., stop expanding this position */
            }
        }

    pp->Depth = 0xFFFF; pp->Score = 0xFFFF;             /* high-values: informs the main search that the position hasn't been visited yet */
    pp->MoveDirection |= POSITION_TAG_TAIL;             /* mark the position as being a tail-position */
}

static void TailPushesSearch(void)
{
    uint32_t Position;

    TailSearchResult = EnqueueStartPositions();         /* put all starting positions on the OPEN-queue */

    if (Generator.MaxOpenPositionsEnabled && Generator.MaxOpenPositions >= 0) {
        while (Positions.OpenPositions.Count <= Generator.MaxOpenPositions &&
               DequeueOpenPosition(&Position))
            TailExpandPosition(Position);
    } else {                                            /* no 'maximum number of open positions' limit */
        while (DequeueOpenPosition(&Position))
            TailExpandPosition(Position);
    }

    /* in YASC, dropped positions indicate memory full during processing, hence,
     * clear open positions here before calling 'ClearOpenPositions' which counts
     * any remaining positions as dropped positions */
    Positions.OpenPositions.Count = 0;

    SetPosition(0);                                     /* restore the board starting position */
    MovePlayer(SearchOriginalPlayerPos);                /* restore original player-position, if any */
    ClearOpenPositions();
    Positions.RepetitionCount = 0;
    Positions.TailPositionCount = Positions.Count;

    if (!Generator.TailPushesThresholdEnabled || Generator.TailPushesThreshold <= 0)
        Positions.TailPositionCount = -Positions.TailPositionCount;     /* negated: don't use tail position information in the fitness function */
}

static int Search(int *BestDepth, int *BestScore)
{
    int OriginalTailPushesThreshold;

    *BestDepth = 0; *BestScore = 0;
    ClearPositions(); InitializeGame();
    SearchOriginalPlayerPos = Game.PlayerPos;
    OriginalTailPushesThreshold = Generator.TailPushesThreshold;
    Generator.TailPushesThreshold = Sign(Generator.TailPushesThreshold) *
                                    Min(abs(Generator.TailPushesThreshold), Game.BoxCount - 1);

    if (Generator.TailPushesThresholdEnabled && Generator.TailPushesThreshold != 0)
        TailPushesSearch();     /* find all trivial tail-pushes sequences, so candidates with long tails don't get good scores just because of their length */

    SearchResult = EnqueueStartPositions();

    if (SearchResult) {
        /* the central breadth-first search loop */
        if (Generator.MaxOpenPositionsEnabled && Generator.MaxOpenPositions >= 0) {
            while (Positions.OpenPositions.Count <= Generator.MaxOpenPositions &&
                   DequeueOpenPosition(&SearchPosition))
                ExpandPosition(SearchPosition);
        } else {                /* no 'maximum number of open positions' limit */
            while (DequeueOpenPosition(&SearchPosition))
                ExpandPosition(SearchPosition);
        }

        /* in YASC, dropped positions indicate memory full during processing, hence,
         * clear open positions here before calling 'ClearOpenPositions' which counts
         * any remaining positions as dropped positions */
        Positions.OpenPositions.Count = 0;
    }

    RemovePositionFromOpenPositions(Positions.BestPosition);    /* the best position may still be a member of the open-list */
    ClearOpenPositions();

    SetPosition(0);                                     /* undo moves, if any */
    MovePlayer(SearchOriginalPlayerPos);                /* restore original player-position, if any */
    Generator.TailPushesThreshold = OriginalTailPushesThreshold;    /* restore tail position threshold */

    if (Positions.BestPosition != 0 && ThreadActive()) {
        *BestDepth = P(Positions.BestPosition)->Depth;
        *BestScore = P(Positions.BestPosition)->Score;
    }
    return SearchResult && *BestDepth > 0;
}

static void RandomizePlayerStartPosition(void)
{
    int i, j, MinPlayerPos;
    j = Random(CalculatePlayersReachableSquares(&MinPlayerPos));
    for (i = 0; i <= Game.BoardSize; i++)
        if (Game.PlayersReachableSquares[i] == Game.PlayersReachableSquaresTimeStamp) {
            if (j == 0) MovePlayer(i);
            j--;
        }
}

/* trims trivial opening pushes, e.g., forced pushes where the player is in a
 * "tunnel" which is blocked by a box; preconditions: the game board matches
 * 'BestPosition', and the search leading to the best position was a backward
 * search */
static int TrimTrivialOpeningPushes(uint32_t *BestPosition_, int *Depth_, int *Score_)
{
    int BoxNo, BoxOnGoalCount, NeighbourFloorCount, NewBoxPos, PlayerPos, NewPlayerPos;
    int IsBoardModified, IsTunnelSquare, Dir, Direction, Result;
    static TBoard OriginalBoard;

    Result = 0; BoxOnGoalCount = 0; Direction = dUp; PlayerPos = Game.PlayerPos;
    NewBoxPos = 0; NewPlayerPos = 0;
    memcpy(OriginalBoard, Game.Board, (size_t)(Game.BoardSize + 1));    /* save a copy of the original board */
    IsBoardModified = FillTubes(0) != 0;    /* fill tunnels without moving the player or the boxes; this helps squeezing the player out of a tunnel */
    for (BoxNo = 1; BoxNo <= Game.BoxCount; BoxNo++)        /* count boxes on goal squares */
        if (IsAGoalSquare(Game.BoxPos[BoxNo])) BoxOnGoalCount++;
    do {
        NeighbourFloorCount = 0;
        for (Dir = dUp; Dir <= dRight; Dir++)               /* count neighbour floor squares around the player position */
            if (IsAFloorSquare(PlayerPos + Game.NeighbourSquareOffset[Dir])) {
                NeighbourFloorCount++; Direction = Dir;
            }
        IsTunnelSquare = (NeighbourFloorCount == 1) &&      /* 'True': the player has exactly one floor square neighbour */
                         P(*BestPosition_)->Parent != 0;    /* don't treat the root position as a candidate for pruning */

        if (IsTunnelSquare) {   /* 'True': the player can only move in one direction */
            NewPlayerPos = PlayerPos + Game.NeighbourSquareOffset[Direction];
            if (IsABoxSquare(NewPlayerPos)) {
                NewBoxPos = NewPlayerPos + Game.NeighbourSquareOffset[Direction];
                IsTunnelSquare = ((Game.Board[NewBoxPos] & (WALL + BOX)) == 0) &&   /* the box can be pushed forward */
                                 (BoxOnGoalCount < Game.BoxCount);                  /* the position isn't a solution */
            } else
                NewBoxPos = 0;  /* a non-pushing player move */
            if (IsTunnelSquare) {   /* 'True': the player is in a tunnel and has only one legal move; move the player and put a wall on the square */
                if (!Result) {      /* 'True': first move; remember that the board has been modified, so it's restored on exit from the function */
                    Result = 1;
                    IsBoardModified = 1;
                }
                if (NewBoxPos != 0) {   /* push a box */
                    Game.Board[NewPlayerPos] -= BOX;
                    Game.Board[NewBoxPos] += BOX;
                    if (IsAGoalSquare(NewPlayerPos)) BoxOnGoalCount--;
                    if (IsAGoalSquare(NewBoxPos)) BoxOnGoalCount++;
                    *BestPosition_ = P(*BestPosition_)->Parent;     /* backtrack to the parent node, i.e., drop the push leading to the parent position */
                }
                Game.Board[PlayerPos] = WALL;       /* put a wall on the tunnel square */
                PlayerPos = NewPlayerPos;           /* move the player */
            }
        }
    } while (IsTunnelSquare);
    if (IsBoardModified)
        memcpy(Game.Board, OriginalBoard, (size_t)(Game.BoardSize + 1));    /* restore the original board */
    if (Result) {
        *Depth_ = P(*BestPosition_)->Depth;         /* update the returned best values 'Depth' and 'Score' */
        *Score_ = P(*BestPosition_)->Score;
        SetPosition(*BestPosition_);                /* update the board so it matches the new best position */
    }
    return Result;
}

/* Returns a 'fitness value' to the GA generator */
static int SearchPlayerAccessAreas(const uint8_t *Board_)
{
    int i, Depth = 0, Score = 0, BestScore;
    static TBoard BestBoard;

    if (Board_ != Game.Board) memcpy(Game.Board, Board_, sizeof(TBoard));
    BestScore = 0;
    for (i = 0; i <= Game.BoardSize; i++) Game.Board[i] &= (uint8_t)~PLAYER;    /* remove player from the board */
    Game.PlayerPos = 0; Game.History.Count = 0; Game.History.Score = 0; Game.History.Fitness = -1;    /* clear history and player */
    Positions.TotalCount = 0; Positions.TotalDroppedCount = 0; Positions.TotalRepetitionCount = 0;
    Generator.PlayerAccessAreaNo = 0;

    do {                                            /* for each player access area */
        if (!Game.ReverseMode &&                    /* forward generator... */
            Generator.PlayerAccessAreaNo == 0)
            GetFirstPlayerAccessArea();             /* get first player position */

        Search(&Depth, &Score);

        Positions.TotalCount += Positions.Count;
        Positions.TotalRepetitionCount += Positions.RepetitionCount;
        Positions.TotalDroppedCount += Positions.DroppedCount;

        if (GAFitnessValue(GA.Control.FitnessFunction, Depth, Score) > BestScore) {     /* new best variation of current level */
            if (Game.ReverseMode) {
                SetPosition(Positions.BestPosition);                    /* use best position as start position */
                if (Generator.TrivialOpeningPushesIgnored) {            /* 'True': trim trivial opening pushes */
                    TrimTrivialOpeningPushes(&Positions.BestPosition, &Depth, &Score);
                    if (Game.History.Fitness < 0) BestScore = -1;       /* 'True': this is the first variation of the current level; ensure that it's registered, also for a trimmed level with no legal moves */
                }
                RandomizePlayerStartPosition();                         /* randomize player's startposition; otherwise, the player is right next to the first pushed box */
            } else
                MakeGoalSquares(Positions.BestPosition);                /* use best position as goal position */

            if (GAFitnessValue(GA.Control.FitnessFunction, Depth, Score) > BestScore) {    /* this is a new best variation of the current level */
                BestScore = GAFitnessValue(GA.Control.FitnessFunction, Depth, Score);
                memcpy(BestBoard, Game.Board, sizeof(TBoard));
                SaveVariationToHistory(Positions.BestPosition);
            }
        }
    } while (!(Game.ReverseMode ||                  /* 'Search' handles all player access areas when the backward generator is used */
               !ThreadActive() ||
               !GetNextPlayerAccessArea()));        /* find next player access area, if any */

    if (Game.History.Count != 0 && ThreadActive()) {    /* 'True': found a variation */
        memcpy(Game.Board, BestBoard, sizeof(TBoard));
        InitializeGame();
    } else {
        Game.History.Count = 0; Game.History.Score = 0;
        if (Game.BoxCount > 0)
            Game.History.Fitness = -1;      /* a board with boxes and goals, but no legal pushes/pulls */
        else
            Game.History.Fitness = 0;       /* a board with no boxes or goals */
    }

    return ThreadActive() ? Game.History.Fitness : -1;
}

/* ------------------------------------------------------------------------ */
/* Genetic algorithm: chromosomes, history of seen candidates */

/* The settings of the generator window (the 'Tag' values of the original) */
static struct {
    int Width, Height;              /* the size of the interior; the board has a wall border around it */
    int WallPct;
    int MaxBoxes;
    int OpenLimit, OpenLimitEnabled;
    int TrivialOpening;
    int TailPushes, TailEnabled;
    int MaxCandidates;
    int Seed;
} Settings;

static double SoftTime;             /* 0 = none; see TimeIsUp */

static int HaveResult(void)
{
    return GA.IndividualCount >= 1 && GA.Individuals[1].Fitness > 0;
}

/* The user terminates the generator in the original.  Here that is a time
 * limit: a hard limit ('Aborted'), and a soft limit that only counts when
 * there is a result to return. */
static int TimeIsUp(void)
{
    double Now;
    if (Aborted) return 1;
    Now = GetTimeSeconds();
    if (AbortTime > 0 && Now > AbortTime) { Aborted = 1; return 1; }
    if (SoftTime > 0 && Now > SoftTime && HaveResult()) return 1;
    return 0;
}

static struct { THashValue *Keys; size_t Cap, Count; } GAHistory;     /* seen candidates */

static void GAHistoryClear(void)
{
    free(GAHistory.Keys);
    memset(&GAHistory, 0, sizeof(GAHistory));
}

static size_t GAHistorySlot(THashValue Key, size_t Cap)
{
    uint64_t x = (uint64_t)Key;
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL; x ^= x >> 33;
    return (size_t)(x & (uint64_t)(Cap - 1));
}

static int GAHistoryLookup(THashValue Key)
{
    size_t i;
    if (GAHistory.Cap == 0) return 0;
    i = GAHistorySlot(Key, GAHistory.Cap);
    while (GAHistory.Keys[i] != 0) {
        if (GAHistory.Keys[i] == Key) return 1;
        i = (i + 1) & (GAHistory.Cap - 1);
    }
    return 0;
}

static void GAHistoryAdd(THashValue Key)
{
    size_t i;
    if (Key == 0 || GAHistoryLookup(Key)) return;
    if ((GAHistory.Count + 1) * 2 > GAHistory.Cap) {
        size_t NewCap = GAHistory.Cap ? GAHistory.Cap * 2 : 1024, k;
        THashValue *NewKeys = (THashValue *)calloc(NewCap, sizeof(THashValue));
        if (NewKeys == NULL) return;
        for (k = 0; k < GAHistory.Cap; k++)
            if (GAHistory.Keys[k] != 0) {
                i = GAHistorySlot(GAHistory.Keys[k], NewCap);
                while (NewKeys[i] != 0) i = (i + 1) & (NewCap - 1);
                NewKeys[i] = GAHistory.Keys[k];
            }
        free(GAHistory.Keys);
        GAHistory.Keys = NewKeys; GAHistory.Cap = NewCap;
    }
    i = GAHistorySlot(Key, GAHistory.Cap);
    while (GAHistory.Keys[i] != 0) i = (i + 1) & (GAHistory.Cap - 1);
    GAHistory.Keys[i] = Key;
    GAHistory.Count++;
}

static THashValue GACalculateHashKey(const uint8_t *Board_)
{
    THashValue Result = 0;
    int i, Col, Row, BoxOrGoalMask = Game.ReverseMode ? GOAL : BOX;
    for (i = 1; i <= Game.BoardSize; i++) {
        const int BoardSquareValue = Board_[i];
        if ((BoardSquareValue & BoxOrGoalMask) != 0) Result ^= HashSquareValues[i];
        if ((BoardSquareValue & WALL) != 0) {
            SquareToColRow(i, &Col, &Row);
            if (Col > 1 && Col < Game.BoardWidth &&
                Row > 1 && Row < Game.BoardHeight)      /* only interior walls are considered */
                Result ^= (THashValue)(0 - (uint64_t)HashSquareValues[i]);    /* '-Value' */
        }
    }
    if (Result == 0) Result = 1;                        /* '0' is a reserved value */
    return Result;
}

static int GAIsANewChromosome(const uint8_t *Board_)
{
    return !GAHistoryLookup(GACalculateHashKey(Board_));
}

static void GAAllelesToBoard(const char *Alleles, uint8_t *Board_)
{
    int Col, Row, RowOffset, Index;
    for (Index = 0; Index <= Game.BoardSize; Index++) Board_[Index] = WALL;
    Index = 0;
    for (Row = 1; Row <= Game.BoardHeight; Row++) {
        RowOffset = ColRowToSquare(0, Row, Game.BoardWidth, Game.BoardHeight);
        for (Col = 1; Col <= Game.BoardWidth; Col++)
            Board_[RowOffset + Col] = (uint8_t)CharToBoardSquareValue(Alleles[Index++]);
    }
}

static void GABoardToAlleles(const uint8_t *Board_, char *Alleles)
{
    int Col, Row, RowOffset, Index = 0;
    for (Row = 1; Row <= Game.BoardHeight; Row++) {
        RowOffset = ColRowToSquare(0, Row, Game.BoardWidth, Game.BoardHeight);
        for (Col = 1; Col <= Game.BoardWidth; Col++)
            Alleles[Index++] = BoardSquareValueToChar(Board_[RowOffset + Col]);
    }
    Alleles[Index] = 0;
}

/* ------------------------------------------------------------------------ */
/* Genetic algorithm: population */

static int GALookupUid(int Uid)
{
    int i;
    if (Uid == 0) return 0;
    for (i = 1; i <= GA.IndividualCount; i++)
        if (GA.Individuals[i].Uid == Uid) return i;
    return 0;
}

static void GADeleteIndividual(int IndividualNo)
{   /* deletes an individual from the population */
    int i;
    for (i = IndividualNo; i <= GA.IndividualCount - 1; i++)
        GA.Individuals[i] = GA.Individuals[i + 1];
    GA.Individuals[GA.IndividualCount].Alleles[0] = 0;
    GA.Individuals[GA.IndividualCount].Uid = 0;
    GA.IndividualCount--;
    if (GA.IndividualCount == 0) GA.BestIndividualNo = 0;
    GA.WorstIndividualNo = Min(GA.IndividualCount, GA.Control.PopulationSize);
    while (GA.WorstIndividualNo > 0 && GA.Individuals[GA.WorstIndividualNo].Fitness < 0)
        GA.WorstIndividualNo--;     /* the worst number denotes the last calculated candidate on the list; the currently processed candidate is located after the calculated members of the population */
}

/* 'Plugin.DeleteLevel' of YASC: the candidate leaves the population, and if
 * it was the current candidate, there is no current candidate anymore */
static void DeleteIndividualByUid(int Uid)
{
    int No = GALookupUid(Uid);
    if (Uid == GA.CurrentUid) GA.CurrentUid = 0;
    if (No > 0) GADeleteIndividual(No);
}

/* makes the candidate stored as member 'IndividualNo' a member of the
 * population, and keeps the population sorted in descending order on fitness */
static int AddIndividualSorted(int IndividualNo)
{
    int Result;
    GA.Individuals[0] = GA.Individuals[IndividualNo];   /* temporarily save the candidate as element 0 */
    Result = IndividualNo;
    while (GA.Individuals[0].Fitness > GA.Individuals[Result - 1].Fitness ||    /* '>': higher is better */
           (GA.Individuals[0].Fitness == GA.Individuals[Result - 1].Fitness &&
            GA.Individuals[0].PushCount > GA.Individuals[Result - 1].PushCount)) {
        GA.Individuals[Result] = GA.Individuals[Result - 1];    /* insertion sort, descending order */
        Result--;
    }
    GA.Individuals[Result] = GA.Individuals[0];         /* insert the new candidate at its sorted position in the vector */
    GA.BestIndividualNo = 1;
    GA.WorstIndividualNo = Min(GA.IndividualCount, GA.Control.PopulationSize);
    GA.Statistics.LastNewCandidateNo = GA.Statistics.CandidatesCount;
    return Result;
}

static int GAIndividualWithHighestChildCountExceptTheBest(void)
{
    int i, Result = -1, HighestChildCount = 0;
    for (i = 2; i <= GA.IndividualCount; i++)
        if (GA.Individuals[i].ChildrenCount >= HighestChildCount) {
            Result = i; HighestChildCount = GA.Individuals[i].ChildrenCount;
        }
    return Result;
}

/* Returns the number of a randomly selected individual (1..count), using rank selection */
static int GARankSelection(void)
{
    int i, Result;
    int AccumulatedRank[GA_HIGH_INDIVIDUALS + 2];
    if (GA.IndividualCount >= 1) {
        AccumulatedRank[0] = 0;
        for (i = 1; i <= GA.IndividualCount; i++)       /* individuals are sorted on fitness, best first */
            AccumulatedRank[i] = AccumulatedRank[i - 1] + (GA.IndividualCount + 1) - i;
        Result = 1; i = 1 + Random(AccumulatedRank[GA.IndividualCount]);
        while (i > AccumulatedRank[Result]) Result++;
    } else
        Result = 0;
    return Result;
}

static void GASelection(int *No1, int *No2)
{
    *No1 = GARankSelection();
    do {
        *No2 = GARankSelection();
    } while (!(*No1 != *No2 || GA.IndividualCount <= 1));
}

/* ------------------------------------------------------------------------ */
/* Genetic algorithm: cross-over and mutations */

static void ClearNewFields(TGAExtendedIndividual *N)
{
    N->Individual.CandidateNo = 0;
    N->Individual.ChildrenCount = 0;
    N->Individual.Uid = 0;
}

static int GACrossOver(int No1, int No2, TGAExtendedIndividual *New1_, TGAExtendedIndividual *New2_)
{
    int i, CrossOverPoint;
    int Result = (Random(100) < GA.Control.CrossOverProbabilityPct) && (No1 != No2);

    if (Result) {
        GA.Statistics.CrossoverCount++;

        if (GA.AllelesCount <= 3) CrossOverPoint = 1;
        else CrossOverPoint = 1 + Random(GA.AllelesCount - 1);    /* 'Pred': n-1 separators between n items */

        for (i = 1; i <= CrossOverPoint; i++) {
            New1_->Individual.Alleles[i - 1] = GA.Individuals[No1].Alleles[i - 1];
            New2_->Individual.Alleles[i - 1] = GA.Individuals[No2].Alleles[i - 1];
        }
        for (i = CrossOverPoint + 1; i <= GA.AllelesCount; i++) {
            New1_->Individual.Alleles[i - 1] = GA.Individuals[No2].Alleles[i - 1];
            New2_->Individual.Alleles[i - 1] = GA.Individuals[No1].Alleles[i - 1];
        }
        New1_->Individual.Alleles[GA.AllelesCount] = 0;
        New2_->Individual.Alleles[GA.AllelesCount] = 0;

        New1_->Individual.Fitness = -1; New2_->Individual.Fitness = -1;     /* signals 'not calculated yet' */

        /* ensure that candidates don't exceed the 'MaxBoxCount' limit */
        GAAllelesToBoard(New1_->Individual.Alleles, Game.Board);
        if (Game.ReverseMode)
            for (i = 0; i <= Game.BoardSize; i++) Game.Board[i] &= (uint8_t)~BOX;
        else
            for (i = 0; i <= Game.BoardSize; i++) Game.Board[i] &= (uint8_t)~GOAL;
        InitializeGame(); memcpy(New1_->Board, Game.Board, sizeof(TBoard));

        GAAllelesToBoard(New2_->Individual.Alleles, Game.Board);
        if (Game.ReverseMode)
            for (i = 0; i <= Game.BoardSize; i++) Game.Board[i] &= (uint8_t)~BOX;
        else
            for (i = 0; i <= Game.BoardSize; i++) Game.Board[i] &= (uint8_t)~GOAL;
        InitializeGame(); memcpy(New2_->Board, Game.Board, sizeof(TBoard));
    } else {            /* no crossover; copy the first individual */
        New1_->Individual = GA.Individuals[No1];
        GAAllelesToBoard(New1_->Individual.Alleles, New1_->Board);
    }

    /* clear fields for the new candidates */
    ClearNewFields(New1_); ClearNewFields(New2_);
    return Result;
}

/* the board and the items (boxes or goals) that the mutation operators work on */
static uint8_t *MutBoard;
static int MutItemCount;
static int MutItemPos[MAX_BOX_COUNT + 1];

/* boards with goals and boxes in dead-end tubes (i.e., floors with walls on
 * all but one side) are not good level candidates; they tend to be trivial levels */
static int MutFillTubes(void)
{
    int Square, NeighborSquare, NeighborFloorCount, NeighborFloorSquare, Direction, Result = 0;
    NeighborFloorSquare = 0;
    Square = 0;
    while (Square < Game.BoardSize) {
        Square++;
        if ((MutBoard[Square] & FLOOR) != 0) {
            NeighborFloorCount = 0;
            for (Direction = dUp; Direction <= dRight; Direction++) {
                NeighborSquare = Square + Game.NeighbourSquareOffset[Direction];
                if ((MutBoard[NeighborSquare] & FLOOR) != 0) {
                    NeighborFloorSquare = NeighborSquare;
                    NeighborFloorCount++;
                }
            }
            if (NeighborFloorCount == 1) {
                if ((MutBoard[Square] & (BOX + GOAL + PLAYER)) != 0)
                    MutBoard[NeighborFloorSquare] = MutBoard[Square];   /* move the box/goal/player from the square to the neighboring floor square, overwriting the existing value */
                MutBoard[Square] = WALL;                                /* put a wall on the square */
                Result++;
                Square = 0;                                             /* check the board again */
            }
        }
    }
    return Result;
}

static int RandomInteriorWallSquare(void)
{
    int i, j, Col, Row, Result = -1;
    i = 1 + Random(Game.BoardSize); j = i;
    do {
        SquareToColRow(i, &Col, &Row);
        if ((MutBoard[i] & WALL) == WALL &&
            Col > 1 && Col < Game.BoardWidth &&
            Row > 1 && Row < Game.BoardHeight)
            Result = i;
        else if (i == Game.BoardSize) i = 1;
        else i++;
    } while (!(Result > 0 || i == j));
    return Result;
}

static int RandomFreeNeighbourFloorSquare(int Item, int Square)
{
    int AvoidMask, Count = 0, Direction;
    int NeighbourSquares[5];
    AvoidMask = Item + WALL;
    for (Direction = dUp; Direction <= dRight; Direction++) {
        NeighbourSquares[Count + 1] = Square + Game.NeighbourSquareOffset[Direction];
        if ((MutBoard[NeighbourSquares[Count + 1]] & AvoidMask) == 0)
            Count++;
    }
    if (Count != 0) return NeighbourSquares[1 + Random(Count)];
    return 0;
}

static int RandomSquare(int AvoidMask)
{
    int i, j, Result = 0;
    i = 1 + Random(Game.BoardSize); j = i;
    do {
        if ((MutBoard[i] & AvoidMask) == 0) Result = i;
        else if (i == Game.BoardSize) i = 1;
        else i++;
    } while (!(Result != 0 || i == j));
    return Result;
}

static int RandomNonBoxFloorSquare(void)  { return RandomSquare(BOX + WALL); }
static int RandomNonGoalFloorSquare(void) { return RandomSquare(GOAL + WALL); }

static int RandomFreeFloorSquareNeighbourToAnItem(int Item, int ExceptItemPos)
{
    int AvoidMask, Square, Count = 0, Direction;
    static int CandidateSquares[MAX_BOARD_SIZE + 1];
    AvoidMask = Item + WALL;
    for (Square = 1; Square <= Game.BoardSize; Square++)
        if ((MutBoard[Square] & AvoidMask) == 0)
            for (Direction = dUp; Direction <= dRight; Direction++)
                if ((MutBoard[Square + Game.NeighbourSquareOffset[Direction]] & Item) != 0 &&
                    Square + Game.NeighbourSquareOffset[Direction] != ExceptItemPos) {
                    Count++; CandidateSquares[Count] = Square;
                    break;      /* ensure that the square isn't added to the list more than once */
                }
    if (Count != 0) return CandidateSquares[1 + Random(Count)];
    return -1;
}

/* single-target-area level: choose a removable goal, if any */
static int STARandomRemovableGoalIndex(void)
{
    int i, j, Count, Result;
    static int CandidateSquares[MAX_BOARD_SIZE + 1];
    static TBoardInteger VisitedSquares;
    if (MutItemCount < 2)
        return MutItemCount;
    Count = 0; memset(VisitedSquares, 0, sizeof(VisitedSquares));
    for (i = 1; i <= MutItemCount; i++) {       /* try to remove each non-fixed goal */
        MutBoard[MutItemPos[i]] -= GOAL;        /* remove the goal from the board */
        j = (i != 1) ? 1 : MutItemCount;
        if (CalculateConnectedGoals(MutItemPos[j], i, 0,    /* check if the rest of the goals are connected */
                                    MutBoard, VisitedSquares) == MutItemCount - 1) {
            Count++; CandidateSquares[Count] = i;
        }
        MutBoard[MutItemPos[i]] += GOAL;        /* put the goal back on the board */
    }
    if (Count != 0) Result = CandidateSquares[1 + Random(Count)];
    else Result = -1;
    return Result;
}

/* choose a removable item, if any */
static int RandomRemovableItemIndex(void)
{
    if (MutItemCount < 2)
        return MutItemCount;
    return 1 + Random(MutItemCount);            /* no fixed squares: every item is removable */
}

static int SelectMutationType(void)
{
    int i, Result;
    const int *F = GA.Control.MutationTypeAccumulatedFrequencies;
    if (MutItemCount > 0) {
        Result = gaAddItem;
        if (GA.Control.MutateWallsOnly)
            i = F[gaAddWall] + Random(F[gaMoveWall] - F[gaAddWall]);
        else
            i = Random(F[gaMoveWall]);      /* 'High' of the array of accumulated frequencies */
        while (i >= F[Result]) Result++;
    } else
        Result = gaAddItem;     /* put an item (a box or a goal depending on the search direction) on the board */
    return Result;
}

static int AddWall(void)
{
    int Result = RandomNonGoalFloorSquare();
    if (Result > 0) MutBoard[Result] = WALL;
    return Result;
}

static int GAMutation(uint8_t *Board_)
{
    enum { MAX_MUTATION_ATTEMPTS = 100 };
    int i, j, Index, MutationCount, MutationAttemptsCountdown, Square, Result = 0;

    MutBoard = Board_; MutItemCount = 0; MutationAttemptsCountdown = MAX_MUTATION_ATTEMPTS;

    if (Game.ReverseMode)
        for (i = 0; i <= Game.BoardSize; i++) {
            Board_[i] &= (uint8_t)~(PLAYER + BOX);              /* remove player and boxes */
            if ((Board_[i] & GOAL) != 0) {
                MutItemCount++; MutItemPos[MutItemCount] = i;
                Board_[i] |= (BOX + GOAL);                      /* put boxes on goal-squares */
            }
        }
    else
        for (i = 0; i <= Game.BoardSize; i++) {
            Board_[i] &= (uint8_t)~GOAL;                        /* remove goals */
            if ((Board_[i] & BOX) != 0) {
                MutItemCount++; MutItemPos[MutItemCount] = i;
            }
        }

    if (MutItemCount > 0) {
        if (Random(100) < GA.Control.MutationProbabilityPct)
            MutationCount = 1 + Random(GA.Control.MaxMutationCount);
        else
            MutationCount = 0;
    } else
        MutationCount = 1;      /* put an item (a box or a goal depending on the search direction) on the board */

    while (Result < MutationCount && MutationAttemptsCountdown > 0) {
        MutationAttemptsCountdown--;
        switch (SelectMutationType()) {
          case gaAddItem:
            if (MutItemCount < Generator.MaxBoxCount ||
                (!Generator.MaxBoxCountEnabled && MutItemCount < MAX_BOX_COUNT)) {
                if (Game.ReverseMode) {
                    if (GA.Control.GenerateConnectedGoals && MutItemCount != 0)
                        Square = RandomFreeFloorSquareNeighbourToAnItem(GOAL, 0);
                    else
                        Square = RandomNonGoalFloorSquare();
                    if (Square > 0) {
                        MutItemCount++;
                        MutItemPos[MutItemCount] = Square;
                        MutBoard[Square] += BOX + GOAL;
                        Result++;
                    }
                } else {
                    Square = RandomNonBoxFloorSquare();
                    if (Square > 0) {
                        MutItemCount++;
                        MutItemPos[MutItemCount] = Square;
                        MutBoard[Square] += BOX;
                        Result++;
                    }
                }
            } else if (AddWall() > 0)
                Result++;
            break;
          case gaDeleteItem:
            if (MutItemCount > 1) {
                if (Game.ReverseMode) {
                    if (GA.Control.GenerateConnectedGoals)
                        Index = STARandomRemovableGoalIndex();
                    else
                        Index = RandomRemovableItemIndex();
                    if (Index > 0) {
                        MutBoard[MutItemPos[Index]] -= BOX + GOAL;
                        MutItemCount--;
                        for (j = Index; j <= MutItemCount; j++)
                            MutItemPos[j] = MutItemPos[j + 1];
                        Result++;
                    }
                } else {
                    Index = RandomRemovableItemIndex();
                    if (Index > 0) {
                        MutBoard[MutItemPos[Index]] -= BOX;
                        MutItemCount--;
                        for (j = Index; j <= MutItemCount; j++)
                            MutItemPos[j] = MutItemPos[j + 1];
                        Result++;
                    }
                }
            }
            break;
          case gaMoveItem:
            if (MutItemCount > 1) {
                if (Game.ReverseMode) {
                    if (GA.Control.GenerateConnectedGoals)
                        Index = STARandomRemovableGoalIndex();
                    else
                        Index = RandomRemovableItemIndex();
                    if (!GA.Control.GenerateConnectedGoals && Random(2) == 0)
                        Square = RandomFreeNeighbourFloorSquare(GOAL, Index > 0 ? MutItemPos[Index] : 0);   /* try moving the goal to a neighbour square */
                    else
                        Square = 0;
                    if (Square <= 0) {
                        if (GA.Control.GenerateConnectedGoals) {
                            if (Index > 0) {
                                if (MutItemCount == 1)
                                    Square = RandomNonGoalFloorSquare();
                                else
                                    Square = RandomFreeFloorSquareNeighbourToAnItem(GOAL, MutItemPos[Index]);
                            } else
                                Square = 0;
                        } else
                            Square = RandomNonGoalFloorSquare();
                    }
                    if (Index > 0 && Square > 0) {
                        MutBoard[MutItemPos[Index]] -= BOX + GOAL;
                        MutItemPos[Index] = Square;
                        MutBoard[MutItemPos[Index]] += BOX + GOAL;
                        Result++;
                    }
                } else {
                    Index = RandomRemovableItemIndex();
                    if (Random(2) == 0)
                        Square = RandomFreeNeighbourFloorSquare(BOX, Index > 0 ? MutItemPos[Index] : 0);    /* try moving the box to a neighbour square */
                    else
                        Square = 0;
                    if (Square <= 0) Square = RandomNonBoxFloorSquare();
                    if (Index > 0 && Square > 0) {
                        MutBoard[MutItemPos[Index]] -= BOX;
                        MutItemPos[Index] = Square;
                        MutBoard[MutItemPos[Index]] += BOX;
                        Result++;
                    }
                }
            }
            break;
          case gaAddWall:
            if (AddWall() > 0) Result++;
            break;
          case gaDeleteWall:
            Square = RandomInteriorWallSquare();
            if (Square > 0) {
                MutBoard[Square] = FLOOR; Result++;
            } else if (AddWall() > 0)
                Result++;
            break;
          case gaMoveWall:
            Square = RandomInteriorWallSquare();
            if (Square > 0 && Random(4) == 0) {
                if (Game.ReverseMode)
                    j = RandomFreeNeighbourFloorSquare(GOAL, Square);
                else
                    j = RandomFreeNeighbourFloorSquare(BOX, Square);
            } else
                j = 0;
            if (j <= 0) j = RandomNonGoalFloorSquare();
            if (Square > 0 && j > 0) {
                MutBoard[Square] = FLOOR;
                MutBoard[j] = WALL;
                Result++;
            } else if (Square <= 0 && AddWall() > 0)
                Result++;
            break;
          default:
            break;
        }
    }

    MutFillTubes();     /* boards with goals and boxes in dead-end tubes are not good level candidates; they tend to be trivial levels */
    GA.Statistics.MutationCount += Result;
    return Result;
}

/* ------------------------------------------------------------------------ */
/* The generator plug-in: candidates, offspring, and the main loop.
 *
 * This is the 'YASC' variant of 'GARun' of the original.  The candidates are
 * members of the array 'GA.Individuals' from the moment they are made, with
 * the fitness '-1' until it has been calculated; the calculated members of
 * the population are first in the array, sorted on fitness, best first.  In
 * the original the individuals are linked to 'level' objects of the
 * application; here they are identified by 'Uid'.  The procedures of the
 * application that the plug-in thread calls ('SynchronizedStopGeneratingLevel',
 * 'DeleteLevel') are translated here as 'StopGeneratingLevel' and
 * 'DeleteIndividualByUid'. */

enum { csNewCandidate1 = 1, csNewCandidate2 = 4 };      /* 'TCandidateState' as bit flags; only the "new" states are used */

static TGAExtendedIndividual New1, New2;    /* local variables of 'GARun' in the original */

/* creates alleles matching the current game state and assigns them to the candidate */
static void GAAddBoardToCandidateSet(int IndividualNo)
{
    Game.ReverseMode = !GA.Control.ForwardSearch;
    LoadAndNormalizeBoard(Game.Board);          /* ensure that unused squares are filled with walls */
    GABoardToAlleles(Game.Board, GA.Individuals[IndividualNo].Alleles);
    GA.AllelesCount = Game.BoardWidth * Game.BoardHeight;
}

/* adds the candidate on 'Game.Board' to the array of individuals (at the end) and makes it the current candidate */
static int AddIndividual(void)
{
    int Result = GA.IndividualCount < GA_HIGH_INDIVIDUALS;
    if (Result) {
        TGAIndividual *I;
        GA.IndividualCount++;
        I = &GA.Individuals[GA.IndividualCount];
        memset(I, 0, sizeof(*I));
        GAAddBoardToCandidateSet(GA.IndividualCount);
        GA.Statistics.CandidatesCount++;
        I->CandidateNo = GA.Statistics.CandidatesCount;
        I->Uid = ++GA.NextUid;
        I->Fitness = -1;                        /* '-1': not calculated yet */
        GA.CurrentUid = I->Uid;                 /* 'MainForm.Generator.Level:=Level' */
    }
    return Result;
}

/* finds the first uncalculated member of the population (with fitness = -1), if any */
static int FindUncalculatedCandidate(void)
{
    int IndividualNo, OriginalIndividualCount, Square, Result;

    GA.Individuals[0].Fitness = 0;              /* '0': vector element 0 is a sentinel for the backward search through the candidates */
    do {
        OriginalIndividualCount = GA.IndividualCount;
        IndividualNo = GA.IndividualCount + 1;
        while (GA.Individuals[IndividualNo - 1].Fitness < 0) IndividualNo--;
        Result = IndividualNo <= GA.IndividualCount;
        if (Result) {   /* 'True': the alleles are all right; now load the board and check whether the candidate is new or a duplicate */
            New1.Individual = GA.Individuals[IndividualNo];
            GAAllelesToBoard(New1.Individual.Alleles, New1.Board);
            if (Game.ReverseMode) {
                for (Square = 0; Square <= Game.BoardSize; Square++) New1.Board[Square] &= (uint8_t)~BOX;
                for (Square = 0; Square <= Game.BoardSize; Square++)
                    if ((New1.Board[Square] & GOAL) != 0) New1.Board[Square] += BOX;
            } else {
                for (Square = 0; Square <= Game.BoardSize; Square++) New1.Board[Square] &= (uint8_t)~GOAL;
            }
            LoadAndNormalizeBoard(New1.Board);  /* normalize the board before checking for duplicates */

            if (Game.BoxCount == 0) {           /* no boxes or goals on the board; try to put a box or a goal on the board */
                GAMutation(New1.Board);
                LoadAndNormalizeBoard(New1.Board);
            }

            Result = (Game.BoxCount != 0) && GAIsANewChromosome(New1.Board);
            if (!Result && GA.IndividualCount == OriginalIndividualCount) {
                DeleteIndividualByUid(GA.Individuals[IndividualNo].Uid);    /* it's a duplicate candidate; delete the candidate */
                IndividualNo = GA.IndividualCount;      /* stay in the 'repeat' loop and search for another uncalculated level */
            }
        }
    } while (!(Result || IndividualNo > GA.IndividualCount));

    if (Result) {
        GA.CurrentUid = GA.Individuals[IndividualNo].Uid;
        GA.GenerationCount++;
    }
    return Result;
}

static int MakeRandomLevel(void)
{
    int CountDown, Result;

    if (Game.BoardWidth < MIN_BOARD_WIDTH || Game.BoardHeight < MIN_BOARD_HEIGHT) {
        Game.BoardWidth  = Settings.Width + 2;      /* the settings are the user's confirmed values */
        Game.BoardHeight = Settings.Height + 2;
    }
    MakeBoard(Game.BoardWidth, Game.BoardHeight);
    Generator.WallCount = (Settings.WallPct * Max(1, Game.BoardWidth - 2) * Max(1, Game.BoardHeight - 2)) / 100;
    InitializeBoardGenerator(!GA.Control.ForwardSearch,
                             Generator.WallCount,
                             Max(1, Max(Generator.BoxCount, Generator.MaxBoxCount)),
                             Generator.RandomSeed,
                             Settings.MaxBoxes,
                             Settings.OpenLimit,
                             Settings.TailPushes,
                             1,                             /* the box limit is enabled */
                             Settings.OpenLimitEnabled,
                             Settings.TrivialOpening,
                             Settings.TailEnabled);

    CountDown = 10000;
    do {
        CountDown--;
        Result = GenerateNextBoard_Random();
        if (Result) {
            LoadAndNormalizeBoard(Game.Board);          /* normalize the board before checking for duplicates */
            Result = GAIsANewChromosome(Game.Board);
        }
    } while (!(Result || CountDown <= 0 || !ThreadActiveTick()));

    Result = Result && AddIndividual();
    if (Result) {
        GA.GenerationCount++;
        GA.Control.MutateWallsOnly = 0;     /* ensure that 'mutate walls only' is disabled */
    }
    return Result;
}

static int MakeRandomLevels(int Count)
{
    int Result = 0;
    while (Count > 0 && ThreadActive() && MakeRandomLevel()) {
        Count--;
        Result++;
    }
    GA.CurrentUid = 0;
    FindUncalculatedCandidate();
    return Result;
}

/* precondition: 'Game.Board' and 'Game.History' contain the candidate starting position and the solution;
 * returns 'True' if the new candidate should be added to the population */
static int CandidatePostprocessing(int Uid)
{
    int IndividualNo, Result;

    IndividualNo = GALookupUid(GA.ParentUid1);          /* update parent 1 if it still is a member of the population */
    if (IndividualNo > 0) GA.Individuals[IndividualNo].ChildrenCount++;
    IndividualNo = GALookupUid(GA.ParentUid2);          /* update parent 2, if any, and if it still is a member of the population */
    if (IndividualNo > 0) GA.Individuals[IndividualNo].ChildrenCount++;

    IndividualNo = GALookupUid(Uid);
    Result = IndividualNo > 0 &&
             GA.Individuals[IndividualNo].Fitness > 0 &&    /* '0': drop candidates with zero pushes */
             (IndividualNo <= GA.Control.PopulationSize ||  /* 'True': the population isn't full */
              GA.Individuals[IndividualNo].Fitness > GA.Individuals[GA.WorstIndividualNo].Fitness ||    /* 'True': the new candidate is better than the worst member of the population */
              (GA.IndividualCount > 1 &&
               GA.Control.InactivityThresholdEnabled &&
               GA.Statistics.LastNewCandidateNo + GA.Control.InactivityThreshold < GA.Statistics.CandidatesCount));  /* 'True': the population hasn't changed for some time */
    if (Result) {           /* 'True': the new candidate should be added to the population */
        TGAIndividual *I = &GA.Individuals[IndividualNo];
        GABoardToAlleles(Game.Board, I->Alleles);       /* create text version of the new board */
        I->PushCount = Game.History.Count;
        I->Score     = Game.History.Score;
        I->Fitness   = Game.History.Fitness;
    }
    return Result;
}

/* the offspring has been created by 'GACrossOver' and is ready for mutations by 'GAMutation'; if the candidate isn't a duplicate then this function loads the candidate and returns 'True' */
static int PrepareOffspring(TGAExtendedIndividual *New_)
{
    int Result;
    GAMutation(New_->Board);
    LoadAndNormalizeBoard(New_->Board);                 /* normalize the board before checking for duplicates */
    Result = (Game.BoxCount != 0) && GAIsANewChromosome(New_->Board);   /* 'True': the offspring is a new candidate; make it ready for processing */
    Result = Result && AddIndividual();
    return Result;
}

static int MakeOffspring(int *CandidateState)
{
    int No1, No2, Result = 0;

    *CandidateState = 0; GA.ParentUid1 = 0; GA.ParentUid2 = 0;
    if (GA.IndividualCount > 0 && GA.IndividualCount < GA_HIGH_INDIVIDUALS) {  /* otherwise, there is 1) no parents to create offspring from, or 2) not room for a new candidate */
        if (Generator.RandomSeed < INT_MAX) Generator.RandomSeed++;
        else Generator.RandomSeed = 1;
        InitializeRandomState(Generator.RandomSeed);

        do {
            GASelection(&No1, &No2);                    /* select 2 individuals based on rank selection */
            if (GACrossOver(No1, No2, &New1, &New2)) {  /* 'True': the cross-over produced 2 candidates */
                GA.ParentUid1 = GA.Individuals[No1].Uid;
                if (No1 != No2 && No2 != 0) GA.ParentUid2 = GA.Individuals[No2].Uid;
                else GA.ParentUid2 = 0;
                *CandidateState = csNewCandidate1 | csNewCandidate2;    /* two candidates pending; they still lack mutations which are performed by 'PrepareOffspring' */
            } else {    /* cross-over did not really take place; it merely selected a candidate for mutation */
                GA.ParentUid1 = GA.Individuals[No1].Uid;
                GA.ParentUid2 = 0;
                *CandidateState = csNewCandidate1;
            }

            *CandidateState &= ~csNewCandidate1;        /* the first candidate has been selected for processing, it's not a pending candidate anymore */
            Result = PrepareOffspring(&New1);
            if (!Result && (*CandidateState & csNewCandidate2) != 0) {
                *CandidateState &= ~csNewCandidate2;    /* the second candidate has been selected for processing */
                Result = PrepareOffspring(&New2);
            }
        } while (!(Result || TimeIsUp()));              /* (the original: 'or the user terminated the job') */
    }

    if (Result) GA.GenerationCount++;
    return Result;
}

static int CalculateFitness(void)
{
    int Result, IndividualNo;

    /* 'GACalculateFitnessForChromosome' */
    Result = SearchPlayerAccessAreas(Game.Board);
    if (Positions.TotalDroppedCount != 0) GA.Statistics.MemoryFullCandidatesCount++;

    if (!ThreadActive()) {
        Result = -1;
        if (Positions.TotalDroppedCount != 0)   /* the candidate has contributed to the number of 'memory full candidates'; remove the contribution */
            GA.Statistics.MemoryFullCandidatesCount--;
    }
    IndividualNo = GALookupUid(GA.CurrentUid);
    if (IndividualNo > 0) GA.Individuals[IndividualNo].Fitness = Result;
    return Result;
}

/* 'TGeneratorThread.SynchronizedStopGeneratingLevel': the current candidate
 * has been processed; 'IsNew': add the candidate to the population (it has been
 * checked by 'CandidatePostprocessing'), 'IsProcessed': its fitness has been
 * calculated, and it's up to the caller to decide on the candidate */
static void StopGeneratingLevel(int IsNew, int IsProcessed)
{
    int IndividualNo, Uid, UidToDelete;

    if (GA.CurrentUid == 0) return;
    Uid = GA.CurrentUid;

    if (IsNew) {
        IndividualNo = GALookupUid(GA.CurrentUid);
        if (IndividualNo > 0) {
            if (IndividualNo <= GA.Control.PopulationSize) {
                if (AddIndividualSorted(IndividualNo) <= 0)
                    DeleteIndividualByUid(GA.CurrentUid);   /* something went wrong; delete the level */
            } else if (GA.Individuals[IndividualNo].Fitness > GA.Individuals[GA.WorstIndividualNo].Fitness) {
                UidToDelete = GA.Individuals[GA.WorstIndividualNo].Uid;     /* this level is worse than the new candidate */
                if (UidToDelete != 0) {
                    if (UidToDelete == GA.ParentUid1) GA.ParentUid1 = 0;
                    if (UidToDelete == GA.ParentUid2) GA.ParentUid2 = 0;
                    DeleteIndividualByUid(UidToDelete);     /* remove the dethroned candidate from the population */
                }
                IndividualNo = GALookupUid(GA.CurrentUid);  /* perform a new lookup; the level changes position after the worst member has been deleted */
                if (IndividualNo <= 0 || AddIndividualSorted(IndividualNo) <= 0)
                    DeleteIndividualByUid(GA.CurrentUid);
            } else if (GA.IndividualCount > 1 &&
                       GA.Control.InactivityThresholdEnabled &&
                       GA.Statistics.LastNewCandidateNo + GA.Control.InactivityThreshold < GA.Statistics.CandidatesCount) {
                /* the population hasn't changed for some time; try to increase the diversity by deleting the individual which had the most chances to create offspring */
                GA.Statistics.InactivityCount++;
                IndividualNo = GAIndividualWithHighestChildCountExceptTheBest();
                UidToDelete = IndividualNo > 0 ? GA.Individuals[IndividualNo].Uid : 0;     /* this level is the one that will be deleted */
                if (UidToDelete != 0) {
                    if (UidToDelete == GA.ParentUid1) GA.ParentUid1 = 0;
                    if (UidToDelete == GA.ParentUid2) GA.ParentUid2 = 0;
                    DeleteIndividualByUid(UidToDelete);
                }
                IndividualNo = GALookupUid(GA.CurrentUid);
                if (IndividualNo <= 0 || AddIndividualSorted(IndividualNo) <= 0)
                    DeleteIndividualByUid(GA.CurrentUid);
            } else
                DeleteIndividualByUid(GA.CurrentUid);       /* discard the candidate; it doesn't qualify to be added to the population */
        } else
            DeleteIndividualByUid(GA.CurrentUid);           /* something is wrong; the level isn't a member of the candidate list */
    } else {            /* don't add the candidate to the population */
        if (IsProcessed)
            DeleteIndividualByUid(Uid);                     /* the fitness has been calculated, and the caller decided not to add the candidate */
        else {          /* the generator was terminated before the fitness had been calculated */
            IndividualNo = GALookupUid(Uid);
            if (IndividualNo > 0 && GA.Individuals[IndividualNo].CandidateNo == GA.Statistics.CandidatesCount)
                GA.Statistics.CandidatesCount--;
            DeleteIndividualByUid(Uid);
        }
    }
}

/* the main procedure for the generator thread */
static void GARun(void)
{
    int CandidateState = 0, Fitness, IsNew, IsProcessed;
    THashValue CandidateHashKey;

    GA.ParentUid1 = 0; GA.ParentUid2 = 0;
    for (;;) {
        GA.CurrentUid = 0;
        Game.ReverseMode = !GA.Control.ForwardSearch;
        if (FindUncalculatedCandidate())
            CandidateState = 0;                 /* drop a pending second candidate from a prior crossover, if any */
        else if (GA.Statistics.CandidatesCount < Settings.MaxCandidates) {     /* 'True': the maximum number of candidates hasn't been exceeded yet */
            if ((CandidateState & csNewCandidate2) != 0 &&      /* 'True': a candidate is pending from a prior crossover */
                PrepareOffspring(&New2)) {      /* 'True': after adding mutations to the candidate it's not a duplicate and now it's ready for processing */
                CandidateState = 0;
            } else if (MakeOffspring(&CandidateState)           /* 'MakeOffspring' creates one or two children depending on whether crossover is performed, and whether any of them are duplicates */
                       ||
                       MakeRandomLevels(Min(GA_RANDOM_CANDIDATES_COUNT,
                                            Max(1, GA.Control.PopulationSize - GA.IndividualCount))) >= 1) {    /* if the population is empty then create random levels */
            }
        }

        if (GA.CurrentUid == 0) break;          /* nothing to do right now */

        CandidateHashKey = GACalculateHashKey(Game.Board);
        IsNew = 0; IsProcessed = 0;
        Fitness = CalculateFitness();
        if (Fitness >= 0) {
            GAHistoryAdd(CandidateHashKey);
            if (GA.CurrentUid != 0) {           /* otherwise, something went wrong */
                IsProcessed = 1;
                IsNew = CandidatePostprocessing(GA.CurrentUid);     /* add the candidate to the population, or delete it */
            }
        } else if (GA.CurrentUid != 0)          /* calculated fitness < 0: the generator was terminated, the candidate has boxes but no box pushes/pulls, or something went wrong */
            IsProcessed = ThreadActive();       /* delete the candidate */
        StopGeneratingLevel(IsNew, IsProcessed);

        if (TimeIsUp()) break;                  /* 'until ThreadState<>ptsProcessLevel' */
    }
    GA.CurrentUid = 0;
}

/* ------------------------------------------------------------------------ */
/* The entry points */

static int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void yg150_defaults(yg150_params *p)
{
    memset(p, 0, sizeof(*p));
    p->width = 8; p->height = 8;
    p->wall_pct = 40;
    p->max_boxes = 4;
    p->open_limit = 0;
    p->memory_mb = 128;
    p->population = 50;
    p->inactivity = 20;
    p->crossover_pct = 50;
    p->mutation_pct = 80;
    p->max_mutations = 2;
    p->connected_goals = 0;
    p->fitness_score = 0;
    p->trivial_opening = 1;
    p->tail_pushes = 1;
    p->forward = 0;
    p->seed = 0;
    p->max_candidates = GA_DEFAULT_MAX_CANDIDATE_COUNT;
    p->seconds = 0;
    p->hard_seconds = 0;
}

int yg150_generate(const yg150_params *p, char *out, int *candidates)
{
    int i, Result = 0;
    double Start;

    if (candidates != NULL) *candidates = 0;

    /* 'SaveSettingsToGenerator': the settings of the generator window */
    memset(&Settings, 0, sizeof(Settings));
    Settings.Width  = ClampInt(p->width,  1, MAX_BOARD_WIDTH  - 2);
    Settings.Height = ClampInt(p->height, 1, MAX_BOARD_HEIGHT - 2);
    Settings.WallPct = ClampInt(p->wall_pct, 0, 100);
    Settings.MaxBoxes = ClampInt(p->max_boxes, 1, MAX_BOX_COUNT);
    Settings.OpenLimitEnabled = p->open_limit > 0;
    Settings.OpenLimit = p->open_limit > 0 ? p->open_limit : 0;
    Settings.TrivialOpening = p->trivial_opening != 0;
    Settings.TailEnabled = p->tail_pushes > 0;
    Settings.TailPushes = Max(1, p->tail_pushes);
    Settings.MaxCandidates = p->max_candidates > 0 ? p->max_candidates : GA_DEFAULT_MAX_CANDIDATE_COUNT;

    memset(&Game, 0, sizeof(Game));
    memset(&Generator, 0, sizeof(Generator));
    memset(&GA, 0, sizeof(GA));
    GAHistoryClear();
    GA.Control.PopulationSize = ClampInt(p->population, 1, GA_MAX_INDIVIDUALS);
    GA.Control.InactivityThresholdEnabled = p->inactivity > 0;
    GA.Control.InactivityThreshold = Max(0, p->inactivity);
    GA.Control.GenerateConnectedGoals = p->connected_goals != 0;
    GA.Control.FitnessFunction = p->fitness_score ? ffScore : ffPushes;
    GA.Control.ForwardSearch = p->forward != 0;
    GA.Control.CrossOverProbabilityPct = ClampInt(p->crossover_pct, 0, 100);
    GA.Control.MutationProbabilityPct = ClampInt(p->mutation_pct, 0, 100);
    GA.Control.MaxMutationCount = ClampInt(p->max_mutations, 1, 10);
    for (i = 0; i < 6; i++)
        GA.Control.MutationTypeAccumulatedFrequencies[i] = GA_DEFAULT_MUTATION_TYPE_ACCUMULATED_FREQUENCIES[i];
    Generator.MaxBoxCountEnabled = 1;
    Generator.MaxBoxCount = Settings.MaxBoxes;
    Generator.MaxOpenPositionsEnabled = Settings.OpenLimitEnabled;
    Generator.MaxOpenPositions = Settings.OpenLimit;
    Generator.TrivialOpeningPushesIgnored = Settings.TrivialOpening;
    Generator.TailPushesThresholdEnabled = Settings.TailEnabled;
    Generator.TailPushesThreshold = Settings.TailPushes;

    /* 'GAClearPopulation' */
    GA.Individuals = (TGAIndividual *)calloc(GA_HIGH_INDIVIDUALS + 1, sizeof(TGAIndividual));
    if (GA.Individuals == NULL) return 0;
    for (i = 0; i <= GA_HIGH_INDIVIDUALS; i++) GA.Individuals[i].Fitness = -1;

    /* 'TGenerator.Run' */
    if (!InitializePositions((size_t)Max(1, p->memory_mb) * 1024u * 1024u)) {
        free(GA.Individuals); GA.Individuals = NULL;
        return 0;
    }
    Start = GetTimeSeconds();
    Aborted = 0; AbortTick = 0;
    AbortTime = p->hard_seconds > 0 ? Start + p->hard_seconds : 0;
    SoftTime = p->seconds > 0 ? Start + p->seconds : 0;

    Generator.RandomSeed = (int)((p->seed != 0 ? p->seed : (unsigned)(Start * 1000.0) + 1u) & 0x7FFFFFFFu);    /* 'GetTimeMS' */
    InitializeRandomState(Generator.RandomSeed);
    for (i = 0; i <= Generator.RandomSeed % (10 * 1024); i++)
        Generator.RandomSeed = Random(INT_MAX);     /* warm up the random number generator */
    InitializeRandomState(Generator.RandomSeed);

    GARun();

    if (candidates != NULL) *candidates = GA.Statistics.CandidatesCount;
    if (HaveResult()) {
        memcpy(out, GA.Individuals[1].Alleles, (size_t)(Game.BoardWidth * Game.BoardHeight));
        Result = Max(1, GA.Individuals[1].PushCount);
    }

    free(GA.Individuals); GA.Individuals = NULL;
    GAHistoryClear();
    FinalizePositions();
    return Result;
}
