# Extra puzzles

This fork adds 24 puzzles that do not exist in Simon Tatham's upstream collection.
All three source collections are MIT-licensed (see `LICENCE`, same text as upstream).

| Directory | Source | Commit | Games |
|---|---|---|---|
| `extras-aho/` | https://github.com/Anders-Holst/sgt-puzzles-aho-extensions | 87efc95 | Alphacrypt, Factorcross, Identifier, Kakuro, Supermaze |
| `extras-unreleased/` | https://github.com/x-sheep/puzzles-unreleased | 6e2d2b0 | ABCD, Ascent, Boats, Bricks, Clusters, Crossing, Mathrax, Rome, Salad, Seismic, Spokes, Sticks, Subsets |
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
