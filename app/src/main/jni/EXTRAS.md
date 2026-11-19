# Extra puzzles

This fork adds 29 puzzles that do not exist in Simon Tatham's released collection: 24 from three extension collections and
5 that were brought over from other sources (the last four rows of the table). It also switches on the four games of
upstream's own `unfinished/` directory. All the sources are MIT-licensed (see `LICENCE`, same text as upstream); the one
remark on a licence, for Mosco, is in its section below.

| Directory | Source | Commit | Games |
|---|---|---|---|
| `extras-aho/` | https://github.com/Anders-Holst/sgt-puzzles-aho-extensions | 87efc95 | Alphacrypt, Factorcross, Identifier, Kakuro, Supermaze |
| `extras-unreleased/` | https://github.com/x-sheep/puzzles-unreleased | 6e2d2b0 | ABCD, Ascent, Boats, Bricks, Clusters, Crossing, Mathrax, Rome, Salad, Seismic, Spokes, Sticks, Subsets |
| `unfinished/` | Simon Tatham's upstream `unfinished/` directory (enabled via `PUZZLES_ENABLE_UNFINISHED`) | in tree | Group, Slide, Sokoban, Separate (Separate had only a generator upstream; its interface was written for this app) |
| `extras-extended/` | https://github.com/SteffenBauer/sgtpuzzles-extended | aaba49f | Flow, Solo+, Stellar, Undead+, Unequal+, Walls |
| `extras-stppc2x/` | https://github.com/ledow/stppc2x (Lee Dowling's collection for hand-held consoles) | cff7daf | Maze 3D, Mosco |
| `extras-pocket/` | https://github.com/SteffenBauer/PocketPuzzles | 8cb6d00 | Binary |
| `extras-lalo/` | https://github.com/lalomartins/sgt-puzzles | 21f9f44 | Decanting |
| `extras-holderbach/` | https://github.com/elsamuray7/Minorfinder, branch `isomtest-srchtree-optim` | dc9580c | Minor Finder |

## Changes made to the imported sources
* `struct game` initialisers: added `set_public_desc`, `get_prefs`/`set_prefs` (older repos),
  `android_cursor_visibility`, wrapped print fields in `#ifndef NO_PRINTING`.
* `changed_state` now returns `bool`, `decode_ui` takes `state`, `request_keys` takes `arrow_mode`,
  `compute_size` takes `ui`, `UI_UPDATE` -> `MOVE_UI_UPDATE`, `move_cursor` extra argument,
  `int *dsf` -> `DSF *` API (solo_plus). Print functions guarded by `NO_PRINTING`.
* `#define thegame` fixed to match the CMake name (solo_plus, undead_plus, unequal_plus; stellar was `nullgame`).
* `midend.c`: `midend_undo` made non-static (required by the Kakuro/Factorcross/Alphacrypt multi-digit input).
* Fixes: crossing text-format buffer overflow and 0-shift, rome uninitialised `completed/cheated`,
  small leaks in clusters/mathrax/rome. Crossing's `free_puzzle` is still disabled upstream ("TODO FIX!").
* Help topics set for every game; help pages generated from the repos' markdown docs.
* `extras-aho/supermaze.c`: its static helper `binary()` is now `bernoulli_flip()`, so that it does not clash with the symbol of the game Binary.
* The help pages of the five games brought over from other sources (below) were written for this app.

## Games brought over from other sources

These five games each have a help page, icons (day and night), night colours and a "Show next step" that is an ordinary move, which can be undone. They were tested with native programs outside the app (the build script, the test programs and the icon tools are on the branch `dev-tools`): the solvers against brute force or an exhaustive search, the generators against the originals where there is one, descriptions, moves and random key events under AddressSanitizer and UBSan, and games played through the midend with the hint.

**Maze 3D** (`extras-stppc2x/maze3d.c`, `maze3dc.c`, `maze3dc.h`). Edward Macnaghten's first-person maze in a cube of rooms (2006, MIT, `LICENCE.Maze3D` of stppc2x), taken from Lee Dowling's collection. The state is kept as in the original: one maze object that all game states share (it counts the references to it), which `changed_state()` takes to the position of the state that is shown, so undo, redo and restart work by moving the maze there; the logic of the game was not changed, only faults were corrected (marked `FIX` in the source):
* the click scaling (an integer division gave 0 for pictures bigger than 450 pixels), and a click outside the picture or a step into a wall made a move that did nothing;
* the midend does not call `changed_state()` after a saved game is loaded, so with undone moves in the saved history the maze stood in the wrong place and the next move did not follow from the state before it (or failed an assertion): the maze is now taken to the right state before a move and before drawing;
* `m3d_reset` loops, a stray `;` after an `if`, `setpos` ignoring the counters;
* `make_double` moved the current position after a failed attempt, assumed that there was a wall (one-sided walls in 120 of 4800 test mazes, now none) and could loop for ever;
* description and parameter buffers one char too short, display index 3, seed 0 read from a pointer, a `validate_desc()` that was too lenient, numbers without bounds;
* the first column of the north-west row of the rotation table, a zero-length array in the drawing of the thread, and triangles, fonts and the update rectangle that were not scaled for tile sizes other than 450.

Added: "Show next step" (the shortest way to the target), click regions that scale with the tile size, strict checks of moves.

**Mosco** (`extras-stppc2x/mosco.c`). Ivan Moscovich's "Magic Arrow Tile Puzzles" as implemented by Lee Dowling for his collection. The rules, the sizes (the arena holds a multiple of 8 tiles, every kind of arrow is laid w*h/8 times) and the meaning of the clues follow his version. The code is new: it has its own game description (only the clues, clockwise from the top left corner; the original put the arrangement it had made the clues from into the description, so a game ID gave the answer away: here that arrangement is the `aux` data) and its own move strings, and a solver (propagation of the clue counts and of the per-kind counts, a short search, and simulated annealing, which finds 16x16 arrangements in a few milliseconds) behind Solve and "Show next step": the hint lays an arrow that the clues force, otherwise an arrow of a solution, and names the tile of a mistake. As in the original, the puzzles are almost never unique, so the game is complete when every clue and every per-kind count is right, whichever arrangement that is. Touch: a tap turns the arrow, a swipe sets it, a long press and swipe (or on the edge or corner of a tile) marks the kinds of arrow that a tile might have, a long press in the middle clears it, a tap on a clue shows the tiles that can point at it; the keyboard has keys for the eight directions; the status bar shows how many arrows of each kind are left. About the licence: in stppc2x `mosco.c` has no licence header; `LICENCE.GP2XPort` gives the MIT text for the changes for the hand-held port and `LICENCE.STPPC2X` says that the collection as a whole is GPL v2. The code here shares nothing with the original but the lines that the puzzle API prescribes (24 of 681 non-trivial lines: signatures, `snew` calls and the like), so it is treated as new code under the licence of this app.

**Binary** (`extras-pocket/binary.c`). Steffen Bauer's version of 2048 from PocketPuzzles (MIT): boards from 2x2 to 9x9, goals from 32 to 8192, presets 4x4 with 512 and 4x4, 5x5 and 6x6 with 2048. Differences from the original:
* tiles merge once per move, as in 2048 (the original merged on in a chain, so that 2 2 4 8 became a 16 and not 4 4 8; checked against a reference implementation on 20000 random lines);
* a game ID without a goal means 2048 (it meant 4096), the description is checked down to the random state and the numbers on the tiles, and a move that changes nothing, or is made in a finished game, is refused;
* "Show next step" plays the move that an expectimax look-ahead finds best (4x4 with the goal 2048 was won in 80 of 80 test games, a hint takes 4 ms on average);
* the cursor keys and the arrow buttons slide the tiles, the swipe has to be as long as a part of a tile (not a number of pixels), the "tap" method uses the four triangles of the board, the tiles are coloured by their numbers, and empty squares show as faint slots (a preference).

**Decanting** (`extras-lalo/decanting.c`). The "water sort" puzzle. Lalo Martins' fork has only a skeleton of it (tubes, selection and pouring; no generator, solver or hint, no checks of descriptions or moves); the idea and the first sketch are his. Added here: 2 to 12 colours, up to 16 tubes of 2 to 8 layers, six presets, and
* a generator that deals the layers at random and keeps only the games that the solver can finish; where that takes too long to find out (few spare tubes, many layers) it scrambles a finished game with pours that run backwards, which is solvable by construction;
* a solver (weighted best-first search over unordered sets of tubes, a 64-bit position hash, an admissible heuristic, only forced moves that are provably safe) behind Solve and "Show next step". It agrees with an exhaustive search on 5340 positions (it finds a solution exactly when there is one, and its solutions are at most two pours longer than the shortest). The hint avoids positions that came earlier in the game, so it never leads in circles;
* an option that hides the layers below the top (shown as "?" until they have been seen), a complete check of descriptions, a check of every move before it is made, a move counter, a message when no pour is left;
* the tube is lifted and turns blue when selected, a pour that is not possible moves the selection to the tube that was tapped, a long press (or the second select key) lets go, the cursor keys and arrow buttons work, the pour is animated, and a preference puts numbers or letters on the layers for those who cannot tell the colours apart.

**Minor Finder** (`extras-holderbach/minorfinder.c`). A puzzle about graph minors by Samuel Holderbach ("Minorensucher", from his bachelor thesis): a big graph is turned into a small one, the minor, by contracting edges, deleting edges and deleting points; points can also be moved to make the picture clear. The game and its generator are the original's: 111000 games (3000 seeds for every valid combination of parameters) were compared with the original generator and the descriptions were identical. Differences:
* the generator no longer writes past the end of its array of points when the number of points of the big graph is a multiple of that of the minor (found with AddressSanitizer; the presets never hit it, custom sizes did); it also checks every finished game with the solver;
* the header comment of the original says that only points without edges can be deleted; its code (`delete_vertex()`) deletes a point together with all its edges, and so does this version;
* an exact solver replaces the brute force search of the original: a constraint search for a division of the points into connected "branch sets", one for each point of the minor, such that two sets touch where the points of the minor are joined (bit masks, propagation, symmetry breaking with the automorphisms of the minor, restarts with random tie-breaking after 20000, 40000, ... nodes, at most 400000 nodes in all, after which "No solution was found in time" is shown). It sits behind Solve and "Show next step" (first it deletes the points that no group needs, then it contracts the leaves of each group, then it deletes the surplus edges), and it says so if the position on the screen cannot be solved any more. It was checked against brute force over all labellings, against the rules of the game themselves, and on planted instances; every sequence of hints of 260 generated games ends in a win, the slowest hint taking 0.02 s;
* the description and the moves are checked completely; the position is kept as bit masks (an adjacency matrix) and every operation was compared with a plain reference model;
* touch screens: points are easy to hit; tapping a point and then a neighbour, or dragging a point on to a neighbour, contracts the edge between them; a long press (the right button with a mouse) on a point or an edge turns it orange, and it is deleted when the finger is lifted there; the neighbours of the selected point get a halo; a status line shows the points and edges of the big graph and how many are needed; a preference puts the minor above or beside the big graph.

## Upstream sync

The 40 official games follow Simon Tatham's upstream sources. They were last merged, file by file with a three-way merge that keeps the Android changes, from the snapshot `puzzles-20260923.616da16` (previous base: `88e4b45` of 20 Oct 2025). The next merge should use `616da16` as its base.

Taken from the snapshot: Mines on generic grids (triangles, hexagons, ... with wrapping, question marks, new first-click handling; the layouts of old seeds are unchanged), Undead monster counters, Keen blocks without a clue, Dominosa right-click number highlight, Filling, Mosaic, "click outside deselects" in Unequal and Group, flags of Untangle, Map and Palisade, `grid.c`, `grid.h`, `malloc.c` and the manual.

Left out on purpose:
* the stylus series (`MOD_STYLUS` and `STYLUS_SUPPORT` in `midend.c` and `puzzles.h`, and the matching code in Lights Up, Loopy, Net, Pattern and Tents). The app keeps the compile-time `STYLUS_BASED` behaviour, which on a touch screen does exactly the same;
* the GTK front end, the CMake, web, KaiOS and OS X files, the icon sources and the version stamp, none of which the Android build uses.

Android changes kept or added in that merge: the Mines hint for every grid type, long press on an uncovered number clears around it, Undead keeps the highlighted cell, Filling key list for the on-screen keyboard, tapping outside the grid no longer moves the Filling cursor out of it, and Mines orders its remaining sets exactly as the old code did (the snapshot shifted by the wrong sign there). The extra variants Solo+, Unequal+ and Undead+ did not get the new upstream changes.

## Sokoban level generators

Sokoban (`unfinished/sokoban.c`) has four extra generators next to the original one, chosen under Custom. The presets use the original one.

| Generator | Source | Licence |
|---|---|---|
| Original | upstream `sokoban.c` (`sokoban_generate()` is byte-identical to upstream) | MIT |
| Boulders | port of the level maker in miki151/sokoban (via a Kotlin port), in `unfinished/sokoban.c` | not stated upstream |
| MCTS | `unfinished/sokoban-mcts/`, lkseg/sokoban-level-generator-mcts, C entry point `mcts_bridge.cpp` | no licence file upstream |
| YASGen | `unfinished/sokoban-yasgen/yasgen150.c`, YASGen 1.50 by Brian Damgaard from Sokoban YASC (Delphi plugin `YASGen_.pas`), translated to C function by function | GPLv3 or later |
| Rooms | Port (in `unfinished/sokoban.c`) of smartive/rust-sokoban-level-generator, after Taylor & Parberry | Apache-2.0 |

What differs from the originals (nothing else was changed in the way a level is generated):

- Original: `sokoban_generate()` is untouched. `new_game_desc()` keeps the tweak of commit 21346d0 (four candidates, the one with the most floor wins; then, in two passes, each unused square next to floor becomes floor with a chance of 50 %), and writes pits and a terminating NUL into the description.
- Boulders: the maker (rooms, search, hash of boulder positions, 500 nodes) is the Kotlin code; the loop around it is the original's `Generator.generate()` (randomised attempts for 5 s, deepest level wins). Differences: the board has the size of the game (the original: 28x16 fixed; a board higher than wide is made turned by 90 degrees, so the maker always gets its wide board), the number of boulders is whatever the maker can place on a board of that size (`bm_cap`, found by trying: at least one attempt in a thousand gives a level; 3 on 10x10, 5 on 12x10, 10 on 28x16; the original app asks for 3 to 7, default 4), the random numbers come from the puzzle's generator, the visited set is an array. `bm_has_stuck()` only checks the result (no boulder in a corner of walls); the maker never makes such a level, and the check never fired in the tests.
- MCTS: the files of the original are used as they are, except `util.h`/`settings.h` (console output silenced). The generator gets the width and height of the board as in the original app (16 to 254 squares: the board index is a u8). The bridge does what the original `bridge.cpp` does (the extra "level to use" setting picks one of the best levels).
- YASGen: `GARun`, the mutations, the cross-over, the rank selection, the replacement rule, the history of chromosomes and the push-optimal reverse search (with transposition table and the "tail", "box lines" and "connected goals" options) are translated one by one; the settings window of YASC is the Custom dialog. Differences: no user interface, a time limit instead of the Stop button (no new candidate is started after the time is up; a search still running is abandoned a little later), 48 MB table instead of 128 MB (`memory_mb`), a hard deadline that abandons a search that is still running.
- Rooms: layout generation (`generate_rooms`), the requirements, goal placement and `create_box_backtrack_map` are translated from the Rust code, including the way template edges are paired with neighbouring cells. Differences: the number of layout tries is limited (800000 or 8 s), the backtrack search records at most "search limit" thousand states (the original has no limit), goal placements are repeated at most "goal placements" times, and among equally far states one box layout is picked at random (the Rust code takes whichever comes first in a hash map).

Big boards: the alternative generators work well only up to a modest size (MCTS 254 squares, Boulders 28x16, YASGen 14x14, Rooms 14x14, i.e. 4x4 rooms, because the chance that a random layout meets the requirements falls by about a factor of 1.6 with every room). A bigger board is built from tiles generated by the chosen generator, separated by one square wide streets and chained in a snake-like order. Each tile is cleaned (unreachable floor becomes wall; a tile with an unreachable box or goal is thrown away), then tunnels are cut through walls only: the way in leads to the region the player can walk in at the start of the tile, the way out starts in the region of the finished tile that is connected to every square where the player can stand after the last push (small bypasses are carved if boxes on goals cut that region). Carving only removes walls, so each tile's solution stays valid. A final check walks the chain on the finished board. A tile is made with a ring of wall of its own; a board that is made in one piece is not changed except that unreachable floor becomes wall. The Boulders maker walks on the squares along the edge of its board, so a tile is made *inside* its ring (the maker gets the tile without the ring), never by closing the edge squares afterwards: that left boulders in corners of walls and levels with no solution. Boulders tiles are cut along the long side of the board first (up to 30 squares including the ring, 28 inside, the board of the original), then along the short side (up to 12); a board narrower than 8 squares is not cut. The number of boulders asked for is a total, shared out among the tiles by area (at least one each, at most what a tile can take); without a number every tile gets 4.

Moves and the fewest moves: the status bar of Sokoban shows "Moves: N", and "Moves: N (min M)" when M is known, as Slide does (`game_redraw()`, `wants_statusbar`). A step is one move, a diagonal step (tap) is two, so that it can never beat M; undo and redo restore the count with the game state, Restart starts at 0, and `final_moves` is the count at the moment the level was completed. M is found when the level is made and kept as a `,M` suffix of the game description (like the move count of Slide), so a saved game, a game ID and a level typed in without the suffix all load; the suffix is checked by `validate_desc()`. `sk_fewest_moves()` is an A* search over the pushes (the cost of a push is the walk to the square behind the box plus the push, the estimate is the sum over the boxes of the pushes to the nearest goal, squares from which no goal can be reached are dead). It is exact, and works only for a level of at most 8 boxes and 8 pits with as many boxes as goals (pits count as goals) and no deep pit, within 400000 positions, 1.5 seconds of processor time and a board of at most 60000 squares; otherwise no minimum is stored and the status bar shows only the count. It is not run for a board that is built from tiles. A level of the Original generator seldom comes that small (a 12x10 board has 16 to 21 boxes), so there the minimum is shown only on small boards. The search was checked against a plain breadth-first search over every position of the game (the real `execute_move()`) for more than a hundred small levels of all five generators: the numbers were the same every time.

## Seismic and Tectonic generator

Seismic (`extras-unreleased/seismic.c`) keeps the generator of the original game: random numbers, random merging of neighbouring cells with different numbers, and the all-or-nothing test that every region of size N holds exactly 1..N. Nothing about the sizes of the regions is set by hand; the original makes many one-cell regions and dominoes and a few big regions, and so does this.

- Up to 36 cells (4x4 to 6x6, both modes) the original algorithm runs as it is.
- Seismic on bigger grids: the same algorithm on small windows (cells around a random cell plus every region that touches them, at most 20 cells), over and over, with the numbers outside the window kept. First the grid is filled, then the order in which the original draws the numbers is kept for every cell and the windows are drawn again (about 40 draws per cell). Measured against the original on 6x6 and 7x7: the sizes of the regions are within 2-3 percentage points of the original (a little more one-cell regions, fewer dominoes), the clues within a few percent, and no puzzle breaks a rule. Generation takes about 0.05 s for 7x7, 0.1 s for 10x10, 0.3 s for 16x16 and 0.5 s for 20x20 on a server.
- Tectonic on bigger grids: random merging as in the original (a region has at most 5 cells because Tectonic uses the numbers 1-5), then the numbers are found by a search and repaired locally.

## Games that were looked at and not added

The files of every branch (1354) of the 456 forks and ports of the puzzle collection that could be found on GitHub were searched for games that are neither in upstream nor in this app. What turned up:

| Game | Where | Why it is not in the app |
|---|---|---|
| Hamilton | sbj42/puzzles, nmay231/sgt-puzzles (James Clark, 2018, `c73b8e6`) | The same puzzle as Ascent (the number path, known as Hidato, Hidoku, Numbrix and Jadium). Ascent has its rules and more. |
| Hidoku | jonaskoelker/sgt-puzzles, branch `new-puzzle-hidoku` (Jonas Kölker, 15 Nov 2026, on top of upstream `616da16`, not in upstream yet) | Again the same puzzle. Ascent covers the variants with 4 and 8 neighbours, the hexagon, honeycomb and edges grids, four levels of difficulty, a rotational clue symmetry, drag input and the notes for the ends. What Hidoku has and Ascent has not: the mirror and four-fold clue symmetries. |
| Ships | olzai/ships | Battleships, which is Boats. |
| Creek | Anders Holst's and Steffen Bauer's repositories | The old name of the game that is Flow here. |
| Crossnum | Anatou/PocketPuzzles | Kakuro, already in the app. |
| Knight's tour | nmay231/sgt-puzzles (`knightAttempt2.c`) | An unfinished attempt, no playable game. |

Hidoku is written for the current puzzle API (it was made on the same upstream snapshot as the one in this app), so it could be added as a second implementation of the number path if that is wanted.
