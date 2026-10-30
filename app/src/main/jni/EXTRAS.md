# Extra puzzles

This fork adds 24 puzzles that do not exist in Simon Tatham's upstream collection.
All three source collections are MIT-licensed (see `LICENCE`, same text as upstream).

| Directory | Source | Commit | Games |
|---|---|---|---|
| `extras-aho/` | https://github.com/Anders-Holst/sgt-puzzles-aho-extensions | 87efc95 | Alphacrypt, Factorcross, Identifier, Kakuro, Supermaze |
| `extras-unreleased/` | https://github.com/x-sheep/puzzles-unreleased | 6e2d2b0 | ABCD, Ascent, Boats, Bricks, Clusters, Crossing, Mathrax, Rome, Salad, Seismic, Spokes, Sticks, Subsets |
| `unfinished/` | Simon Tatham's upstream `unfinished/` directory (enabled via `PUZZLES_ENABLE_UNFINISHED`) | in tree | Group, Slide, Sokoban, Separate (Separate had only a generator upstream; its interface was written for this app) |
| `extras-extended/` | https://github.com/SteffenBauer/sgtpuzzles-extended | aaba49f | Flow, Solo+, Stellar, Undead+, Unequal+, Walls |

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
- Boulders: the maker (rooms, search, hash of boulder positions, 500 nodes) is the Kotlin code; the loop around it is the original's `Generator.generate()` (randomised attempts for 5 s, deepest level wins). Differences: the board has the size of the game (the original: 28x16 fixed), 1 to 7 boulders, at most the width minus 8 (the original app: 3 to 7, default 4), the random numbers come from the puzzle's generator, the visited set is an array.
- MCTS: the files of the original are used as they are, except `util.h`/`settings.h` (console output silenced). The generator gets the width and height of the board as in the original app (16 to 254 squares: the board index is a u8). The bridge does what the original `bridge.cpp` does (the extra "level to use" setting picks one of the best levels).
- YASGen: `GARun`, the mutations, the cross-over, the rank selection, the replacement rule, the history of chromosomes and the push-optimal reverse search (with transposition table and the "tail", "box lines" and "connected goals" options) are translated one by one; the settings window of YASC is the Custom dialog. Differences: no user interface, a time limit instead of the Stop button (no new candidate is started after the time is up; a search still running is abandoned a little later), 48 MB table instead of 128 MB (`memory_mb`), a hard deadline that abandons a search that is still running.
- Rooms: layout generation (`generate_rooms`), the requirements, goal placement and `create_box_backtrack_map` are translated from the Rust code, including the way template edges are paired with neighbouring cells. Differences: the number of layout tries is limited (800000 or 8 s), the backtrack search records at most "search limit" thousand states (the original has no limit), goal placements are repeated at most "goal placements" times, and among equally far states one box layout is picked at random (the Rust code takes whichever comes first in a hash map).

Big boards: the alternative generators work well only up to a modest size (MCTS 254 squares, Boulders 28x16, YASGen 14x14, Rooms 14x14, i.e. 4x4 rooms, because the chance that a random layout meets the requirements falls by about a factor of 1.6 with every room). A bigger board is built from tiles generated by the chosen generator, separated by one square wide streets and chained in a snake-like order. Each tile is cleaned (unreachable floor becomes wall; a tile with an unreachable box or goal is thrown away), then tunnels are cut through walls only: the way in leads to the region the player can walk in at the start of the tile, the way out starts in the region of the finished tile that is connected to every square where the player can stand after the last push (small bypasses are carved if boxes on goals cut that region). Carving only removes walls, so each tile's solution stays valid. A final check walks the chain on the finished board. A tile is made with a ring of wall of its own; a board that is made in one piece is not changed except that unreachable floor becomes wall.
